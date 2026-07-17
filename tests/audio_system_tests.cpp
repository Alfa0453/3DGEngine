#include <engine/audio/AudioAsset.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>

namespace {

void Check(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
}

bool Near(float a, float b) {
    return std::fabs(a - b) < 0.0001f;
}

} // namespace

int main() {
    const std::filesystem::path folder =
        std::filesystem::temp_directory_path() / "3dg_audio_tests";
    std::filesystem::create_directories(folder);
    std::string error;

    engine::AudioCueAsset cue;
    cue.name = "Fire Spell";
    cue.mode = engine::AudioCueMode::Layered;
    cue.bus = engine::AudioBus::SFX;
    cue.volumeMin = 0.8f;
    cue.volumeMax = 1.1f;
    cue.pitchMin = 0.9f;
    cue.pitchMax = 1.2f;
    cue.cooldownSeconds = 0.2f;
    cue.maxInstances = 3;
    cue.priority = 80;
    cue.clips.push_back({"cast.wav", 2.0f, 0.9f, 1.1f, 0.0f});
    cue.clips.push_back({"flame.wav", 1.0f, 1.0f, 1.0f, 0.08f});
    const auto cuePath = folder / "fire.3dgaudio";
    Check(engine::SaveAudioCue(cuePath.string(), cue, &error), "save audio cue");
    engine::AudioCueAsset loadedCue;
    Check(engine::LoadAudioCue(cuePath.string(), &loadedCue, &error), "load audio cue");
    Check(loadedCue.name == cue.name && loadedCue.clips.size() == 2,
          "cue identity and layers round trip");
    Check(loadedCue.mode == engine::AudioCueMode::Layered
          && loadedCue.priority == 80 && Near(loadedCue.clips[1].delaySeconds, 0.08f),
          "cue gameplay settings round trip");

    engine::AudioMixerPreset mixer;
    mixer.name = "Combat";
    const auto busCount = static_cast<std::size_t>(engine::AudioBus::Count);
    mixer.volumes.assign(busCount, 1.0f);
    mixer.muted.assign(busCount, false);
    mixer.effects.assign(busCount, engine::AudioBusEffects{});
    mixer.volumes[static_cast<std::size_t>(engine::AudioBus::Music)] = 0.65f;
    mixer.effects[static_cast<std::size_t>(engine::AudioBus::SFX)].compressorRatio = 4.0f;
    const auto mixerPath = folder / "combat.3dgmixer";
    Check(engine::SaveAudioMixerPreset(mixerPath.string(), mixer, &error), "save mixer");
    engine::AudioMixerPreset loadedMixer;
    Check(engine::LoadAudioMixerPreset(mixerPath.string(), &loadedMixer, &error), "load mixer");
    Check(Near(loadedMixer.volumes[static_cast<std::size_t>(engine::AudioBus::Music)], 0.65f)
          && Near(loadedMixer.effects[static_cast<std::size_t>(engine::AudioBus::SFX)]
              .compressorRatio, 4.0f), "mixer settings round trip");

    engine::AdaptiveMusicAsset music;
    music.name = "Game Music";
    music.states.push_back({"Exploration", {"explore_drums.ogg", "explore_pad.ogg"},
                            100.0f, 0.6f, 2.4f});
    music.states.push_back({"Combat", {"combat.ogg"}, 140.0f, 0.8f, 0.8f});
    const auto musicPath = folder / "music.3dgmusic";
    Check(engine::SaveAdaptiveMusic(musicPath.string(), music, &error), "save adaptive music");
    engine::AdaptiveMusicAsset loadedMusic;
    Check(engine::LoadAdaptiveMusic(musicPath.string(), &loadedMusic, &error),
          "load adaptive music");
    Check(loadedMusic.states.size() == 2 && loadedMusic.states[0].stems.size() == 2
          && Near(loadedMusic.states[1].bpm, 140.0f), "music states round trip");

    std::filesystem::remove_all(folder);
    std::cout << "Audio system regression tests passed.\n";
    return 0;
}
