#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace engine {

struct AudioBuffer {
    std::uint32_t sampleRate = 44100;
    std::uint32_t channels = 1;
    std::vector<float> samples;

    std::size_t FrameCount() const {
        return channels > 0 ? samples.size() / channels : 0;
    }
    float DurationSeconds() const {
        return sampleRate > 0 ? static_cast<float>(FrameCount()) / sampleRate : 0.0f;
    }
    bool Empty() const { return samples.empty() || channels == 0 || sampleRate == 0; }
};

bool DecodeAudioFile(const std::string& path, AudioBuffer* output, std::string* error = nullptr);
bool WriteAudioWav(const std::string& path, const AudioBuffer& buffer, std::string* error = nullptr);

} // namespace engine
