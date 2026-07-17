#include "engine/audio/AudioEditing.h"

#include "miniaudio.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <vector>

namespace engine {

bool DecodeAudioFile(const std::string& path, AudioBuffer* output, std::string* error) {
    if (!output) {
        if (error) *error = "Audio output buffer is null.";
        return false;
    }
    ma_decoder decoder{};
    const ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0, 0);
    if (ma_decoder_init_file(path.c_str(), &config, &decoder) != MA_SUCCESS) {
        if (error) *error = "Could not decode audio file.";
        return false;
    }
    ma_uint64 frameCount = 0;
    if (ma_decoder_get_length_in_pcm_frames(&decoder, &frameCount) != MA_SUCCESS || frameCount == 0
        || decoder.outputChannels == 0 || decoder.outputSampleRate == 0
        || frameCount > static_cast<ma_uint64>(std::numeric_limits<std::size_t>::max() / decoder.outputChannels)) {
        ma_decoder_uninit(&decoder);
        if (error) *error = "Audio file has an invalid or unsupported duration.";
        return false;
    }
    AudioBuffer decoded;
    decoded.channels = decoder.outputChannels;
    decoded.sampleRate = decoder.outputSampleRate;
    decoded.samples.resize(static_cast<std::size_t>(frameCount) * decoded.channels);
    ma_uint64 framesRead = 0;
    const ma_result readResult = ma_decoder_read_pcm_frames(&decoder, decoded.samples.data(), frameCount, &framesRead);
    ma_decoder_uninit(&decoder);
    if (readResult != MA_SUCCESS && readResult != MA_AT_END) {
        if (error) *error = "Audio decoding failed while reading samples.";
        return false;
    }
    decoded.samples.resize(static_cast<std::size_t>(framesRead) * decoded.channels);
    if (decoded.Empty()) {
        if (error) *error = "Audio file contains no samples.";
        return false;
    }
    *output = std::move(decoded);
    return true;
}

bool WriteAudioWav(const std::string& path, const AudioBuffer& buffer, std::string* error) {
    if (buffer.Empty()) {
        if (error) *error = "There is no audio to export.";
        return false;
    }
    std::error_code ec;
    const std::filesystem::path outputPath(path);
    if (outputPath.has_parent_path()) std::filesystem::create_directories(outputPath.parent_path(), ec);
    if (ec) {
        if (error) *error = "Could not create the output folder.";
        return false;
    }
    std::vector<ma_int16> pcm(buffer.samples.size());
    for (std::size_t i = 0; i < buffer.samples.size(); ++i) {
        const float sample = std::clamp(buffer.samples[i], -1.0f, 1.0f);
        pcm[i] = static_cast<ma_int16>(std::lround(sample * 32767.0f));
    }
    ma_encoder encoder{};
    const ma_encoder_config config = ma_encoder_config_init(ma_encoding_format_wav, ma_format_s16,
        buffer.channels, buffer.sampleRate);
    if (ma_encoder_init_file(path.c_str(), &config, &encoder) != MA_SUCCESS) {
        if (error) *error = "Could not open the WAV output file.";
        return false;
    }
    ma_uint64 framesWritten = 0;
    const ma_result result = ma_encoder_write_pcm_frames(&encoder, pcm.data(), buffer.FrameCount(), &framesWritten);
    ma_encoder_uninit(&encoder);
    if (result != MA_SUCCESS || framesWritten != buffer.FrameCount()) {
        if (error) *error = "Could not write the complete WAV file.";
        return false;
    }
    return true;
}

} // namespace engine
