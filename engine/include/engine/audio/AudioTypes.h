#pragma once

#include <cstdint>

namespace engine {

enum class AudioBus : std::uint8_t {
    Master = 0,
    Music,
    SFX,
    Dialogue,
    UI,
    Ambient,
    Count
};

const char* AudioBusName(AudioBus bus);

struct AudioBusEffects {
    float lowPassHz = 20000.0f;
    float highPassHz = 20.0f;
    float reverbWet = 0.0f;
    float reverbDecay = 0.35f;
    float compressorThresholdDb = 0.0f;
    float compressorRatio = 1.0f;
};

enum class AudioSnapshotPreset : std::uint8_t {
    Default = 0,
    Paused,
    Underwater,
    Indoor,
    Cinematic
};

const char* AudioSnapshotPresetName(AudioSnapshotPreset preset);

} // namespace engine
