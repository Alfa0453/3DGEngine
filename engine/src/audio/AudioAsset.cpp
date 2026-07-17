#include "engine/audio/AudioAsset.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace engine {
namespace {

bool OpenOutput(const std::string& path, std::ofstream* out, std::string* error) {
    std::error_code ec;
    const std::filesystem::path target(path);
    if (target.has_parent_path()) std::filesystem::create_directories(target.parent_path(), ec);
    if (ec || !(*out = std::ofstream(path))) {
        if (error) *error = "Could not create audio asset.";
        return false;
    }
    return true;
}

bool OpenInput(const std::string& path, std::ifstream* in, std::string* error) {
    if (!(*in = std::ifstream(path))) {
        if (error) *error = "Could not open audio asset.";
        return false;
    }
    return true;
}

AudioBus SafeBus(int value) {
    return static_cast<AudioBus>(std::clamp(value, static_cast<int>(AudioBus::Master),
                                            static_cast<int>(AudioBus::Ambient)));
}

AudioBusEffects SafeEffects(AudioBusEffects fx) {
    fx.lowPassHz = std::clamp(fx.lowPassHz, 20.0f, 20000.0f);
    fx.highPassHz = std::clamp(fx.highPassHz, 20.0f, 20000.0f);
    fx.reverbWet = std::clamp(fx.reverbWet, 0.0f, 1.0f);
    fx.reverbDecay = std::clamp(fx.reverbDecay, 0.0f, 0.95f);
    fx.compressorThresholdDb = std::clamp(fx.compressorThresholdDb, -60.0f, 0.0f);
    fx.compressorRatio = std::clamp(fx.compressorRatio, 1.0f, 20.0f);
    return fx;
}

} // namespace

bool SaveAudioCue(const std::string& path, const AudioCueAsset& cue, std::string* error) {
    std::ofstream out;
    if (!OpenOutput(path, &out, error)) return false;
    out << "3DGAUDIO_CUE 1\n"
        << "name " << std::quoted(cue.name) << '\n'
        << "settings " << static_cast<int>(cue.mode) << ' ' << static_cast<int>(cue.bus) << ' '
        << cue.volumeMin << ' ' << cue.volumeMax << ' ' << cue.pitchMin << ' ' << cue.pitchMax << ' '
        << cue.cooldownSeconds << ' ' << cue.maxInstances << ' ' << cue.priority << ' '
        << (cue.spatial ? 1 : 0) << ' ' << (cue.noImmediateRepeat ? 1 : 0) << '\n';
    for (const AudioCueClip& clip : cue.clips)
        out << "clip " << std::quoted(clip.path) << ' ' << clip.weight << ' ' << clip.volume
            << ' ' << clip.pitch << ' ' << clip.delaySeconds << '\n';
    return static_cast<bool>(out);
}

bool LoadAudioCue(const std::string& path, AudioCueAsset* output, std::string* error) {
    if (!output) {
        if (error) *error = "Audio cue output is null.";
        return false;
    }
    std::ifstream in;
    if (!OpenInput(path, &in, error)) return false;
    std::string magic;
    int version = 0;
    if (!(in >> magic >> version) || magic != "3DGAUDIO_CUE" || version != 1) {
        if (error) *error = "Unsupported audio cue format.";
        return false;
    }
    AudioCueAsset cue;
    std::string token;
    while (in >> token) {
        if (token == "name") in >> std::quoted(cue.name);
        else if (token == "settings") {
            int mode = 0, bus = 0, spatial = 1, noRepeat = 1;
            in >> mode >> bus >> cue.volumeMin >> cue.volumeMax >> cue.pitchMin >> cue.pitchMax
               >> cue.cooldownSeconds >> cue.maxInstances >> cue.priority >> spatial >> noRepeat;
            cue.mode = static_cast<AudioCueMode>(std::clamp(mode, 0, 2));
            cue.bus = SafeBus(bus);
            cue.spatial = spatial != 0;
            cue.noImmediateRepeat = noRepeat != 0;
        } else if (token == "clip") {
            AudioCueClip clip;
            in >> std::quoted(clip.path) >> clip.weight >> clip.volume >> clip.pitch >> clip.delaySeconds;
            if (!clip.path.empty()) cue.clips.push_back(std::move(clip));
        } else {
            std::string ignored;
            std::getline(in, ignored);
        }
    }
    cue.volumeMin = std::max(cue.volumeMin, 0.0f);
    cue.volumeMax = std::max(cue.volumeMax, cue.volumeMin);
    cue.pitchMin = std::max(cue.pitchMin, 0.01f);
    cue.pitchMax = std::max(cue.pitchMax, cue.pitchMin);
    cue.cooldownSeconds = std::max(cue.cooldownSeconds, 0.0f);
    cue.maxInstances = std::max(cue.maxInstances, 1);
    cue.priority = std::clamp(cue.priority, 0, 100);
    for (AudioCueClip& clip : cue.clips) {
        clip.weight = std::max(clip.weight, 0.0f);
        clip.volume = std::max(clip.volume, 0.0f);
        clip.pitch = std::max(clip.pitch, 0.01f);
        clip.delaySeconds = std::max(clip.delaySeconds, 0.0f);
    }
    if (cue.clips.empty()) {
        if (error) *error = "Audio cue contains no clips.";
        return false;
    }
    *output = std::move(cue);
    return true;
}

bool SaveAudioMixerPreset(const std::string& path, const AudioMixerPreset& preset,
                          std::string* error) {
    std::ofstream out;
    if (!OpenOutput(path, &out, error)) return false;
    out << "3DGAUDIO_MIXER 1\nname " << std::quoted(preset.name) << "\nducking "
        << (preset.dialogueDucking ? 1 : 0) << '\n';
    const std::size_t count = static_cast<std::size_t>(AudioBus::Count);
    for (std::size_t i = 0; i < count; ++i) {
        const float volume = i < preset.volumes.size() ? preset.volumes[i] : 1.0f;
        const bool muted = i < preset.muted.size() && preset.muted[i];
        const AudioBusEffects fx = SafeEffects(
            i < preset.effects.size() ? preset.effects[i] : AudioBusEffects{});
        out << "bus " << i << ' ' << volume << ' ' << (muted ? 1 : 0) << ' '
            << fx.lowPassHz << ' ' << fx.highPassHz << ' ' << fx.reverbWet << ' '
            << fx.reverbDecay << ' ' << fx.compressorThresholdDb << ' '
            << fx.compressorRatio << '\n';
    }
    return static_cast<bool>(out);
}

bool LoadAudioMixerPreset(const std::string& path, AudioMixerPreset* output,
                          std::string* error) {
    if (!output) {
        if (error) *error = "Mixer preset output is null.";
        return false;
    }
    std::ifstream in;
    if (!OpenInput(path, &in, error)) return false;
    std::string magic;
    int version = 0;
    if (!(in >> magic >> version) || magic != "3DGAUDIO_MIXER" || version != 1) {
        if (error) *error = "Unsupported mixer preset format.";
        return false;
    }
    const std::size_t count = static_cast<std::size_t>(AudioBus::Count);
    AudioMixerPreset preset;
    preset.volumes.assign(count, 1.0f);
    preset.muted.assign(count, false);
    preset.effects.assign(count, AudioBusEffects{});
    std::string token;
    while (in >> token) {
        if (token == "name") in >> std::quoted(preset.name);
        else if (token == "ducking") {
            int enabled = 1;
            in >> enabled;
            preset.dialogueDucking = enabled != 0;
        } else if (token == "bus") {
            std::size_t index = count;
            int muted = 0;
            AudioBusEffects fx;
            float volume = 1.0f;
            in >> index >> volume >> muted >> fx.lowPassHz >> fx.highPassHz
               >> fx.reverbWet >> fx.reverbDecay >> fx.compressorThresholdDb >> fx.compressorRatio;
            if (index < count) {
                preset.volumes[index] = std::max(volume, 0.0f);
                preset.muted[index] = muted != 0;
                preset.effects[index] = SafeEffects(fx);
            }
        } else {
            std::string ignored;
            std::getline(in, ignored);
        }
    }
    *output = std::move(preset);
    return true;
}

bool SaveAdaptiveMusic(const std::string& path, const AdaptiveMusicAsset& music,
                       std::string* error) {
    std::ofstream out;
    if (!OpenOutput(path, &out, error)) return false;
    out << "3DGAUDIO_MUSIC 1\nname " << std::quoted(music.name) << '\n';
    for (const AdaptiveMusicState& state : music.states) {
        out << "state " << std::quoted(state.name) << ' ' << state.bpm << ' '
            << state.volume << ' ' << state.crossfadeSeconds << ' ' << state.stems.size();
        for (const std::string& stem : state.stems) out << ' ' << std::quoted(stem);
        out << '\n';
    }
    return static_cast<bool>(out);
}

bool LoadAdaptiveMusic(const std::string& path, AdaptiveMusicAsset* output,
                       std::string* error) {
    if (!output) {
        if (error) *error = "Adaptive music output is null.";
        return false;
    }
    std::ifstream in;
    if (!OpenInput(path, &in, error)) return false;
    std::string magic;
    int version = 0;
    if (!(in >> magic >> version) || magic != "3DGAUDIO_MUSIC" || version != 1) {
        if (error) *error = "Unsupported adaptive music format.";
        return false;
    }
    AdaptiveMusicAsset music;
    std::string token;
    while (in >> token) {
        if (token == "name") in >> std::quoted(music.name);
        else if (token == "state") {
            AdaptiveMusicState state;
            std::size_t count = 0;
            in >> std::quoted(state.name) >> state.bpm >> state.volume
               >> state.crossfadeSeconds >> count;
            state.bpm = std::max(state.bpm, 1.0f);
            state.volume = std::max(state.volume, 0.0f);
            state.crossfadeSeconds = std::max(state.crossfadeSeconds, 0.0f);
            for (std::size_t i = 0; i < count; ++i) {
                std::string stem;
                in >> std::quoted(stem);
                if (!stem.empty()) state.stems.push_back(std::move(stem));
            }
            if (!state.name.empty() && !state.stems.empty()) music.states.push_back(std::move(state));
        } else {
            std::string ignored;
            std::getline(in, ignored);
        }
    }
    if (music.states.empty()) {
        if (error) *error = "Adaptive music asset contains no valid states.";
        return false;
    }
    *output = std::move(music);
    return true;
}

} // namespace engine
