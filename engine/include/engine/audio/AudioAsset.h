#pragma once

#include "engine/audio/AudioTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace engine {

enum class AudioCueMode : std::uint8_t {
    Random = 0,
    Sequence,
    Layered
};

struct AudioCueClip {
    std::string path;
    float weight = 1.0f;
    float volume = 1.0f;
    float pitch = 1.0f;
    float delaySeconds = 0.0f;
};

struct AudioCueAsset {
    std::string name = "Audio Cue";
    AudioCueMode mode = AudioCueMode::Random;
    AudioBus bus = AudioBus::SFX;
    std::vector<AudioCueClip> clips;
    float volumeMin = 1.0f;
    float volumeMax = 1.0f;
    float pitchMin = 1.0f;
    float pitchMax = 1.0f;
    float cooldownSeconds = 0.0f;
    int maxInstances = 8;
    int priority = 50;
    bool spatial = true;
    bool noImmediateRepeat = true;
};

struct AudioMixerPreset {
    std::string name = "Mixer";
    std::vector<float> volumes;
    std::vector<bool> muted;
    std::vector<AudioBusEffects> effects;
    bool dialogueDucking = true;
};

struct AdaptiveMusicState {
    std::string name;
    std::vector<std::string> stems;
    float bpm = 120.0f;
    float volume = 0.5f;
    float crossfadeSeconds = 1.0f;
};

struct AdaptiveMusicAsset {
    std::string name = "Adaptive Music";
    std::vector<AdaptiveMusicState> states;
};

bool LoadAudioCue(const std::string& path, AudioCueAsset* output,
                  std::string* error = nullptr);
bool SaveAudioCue(const std::string& path, const AudioCueAsset& cue,
                  std::string* error = nullptr);
bool LoadAudioMixerPreset(const std::string& path, AudioMixerPreset* output,
                          std::string* error = nullptr);
bool SaveAudioMixerPreset(const std::string& path, const AudioMixerPreset& preset,
                          std::string* error = nullptr);
bool LoadAdaptiveMusic(const std::string& path, AdaptiveMusicAsset* output,
                       std::string* error = nullptr);
bool SaveAdaptiveMusic(const std::string& path, const AdaptiveMusicAsset& music,
                       std::string* error = nullptr);

} // namespace engine
