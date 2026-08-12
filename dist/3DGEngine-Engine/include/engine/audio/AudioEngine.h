#pragma once

#include "engine/audio/AudioAsset.h"

#include <glm/glm.hpp>

#include <memory>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace engine {

// A thin wrapper over miniaudio's high-level engine for sound effects.
//
// Sounds are played through small POOLS of preloaded, decoded "voices", so the
// same effect can overlap with itself and there is no per-hit file lookup or
// decode. Each Play can vary pitch and volume.
//
// Construction is FAULT-TOLERANT: with no audio device the engine disables
// itself and every call becomes a no-op, so a game never crashes for lack of
// audio. miniaudio.h (~95k lines) is kept out of this header behind a PIMPL.
class AudioEngine {
public:
    using SourceHandle = std::uint64_t;
    static constexpr SourceHandle InvalidSource = 0;

    struct DebugStats {
        std::size_t activeVoices = 0;
        std::size_t managedSources = 0;
        std::size_t pooledAssets = 0;
        std::size_t streamedVoices = 0;
        std::size_t stolenVoices = 0;
        std::size_t rejectedVoices = 0;
        std::array<std::size_t, static_cast<std::size_t>(AudioBus::Count)> voicesPerBus{};
    };

    struct DeviceInfo {
        bool available = false;
        std::uint32_t sampleRate = 0;
        std::uint32_t channels = 0;
        std::string backend;
    };

    AudioEngine();
    ~AudioEngine();

    AudioEngine(const AudioEngine&)            = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    // Decode a sound into a pool of `voices` overlapping copies up front, so the
    // first Play() has no hitch. Call once per sound during setup (optional —
    // Play() will lazily load if needed).
    void Preload(const std::string& path, int voices = 8);

    // Play a sound file (WAV/FLAC/MP3) fire-and-forget. Many can overlap.
    // miniaudio's resource manager caches the decoded data, so repeating the
    // same file is cheap. No-op when audio is unavailable.
    void Play(const std::string& path, float pitch = 1.0f, float volume = 1.0f,
              AudioBus bus = AudioBus::SFX, int priority = 50);


    // --- 3D spatial audio ------------------------------------------------
    // Set the listener (usually the camera) for spatial playback. Call each frame
    // before positional plays so panning/attenuation track the viewpoint.
    void SetListener(const glm::vec3& position, const glm::vec3& forward,
                     const glm::vec3& up = glm::vec3(0.0f, 1.0f, 0.0f));

    // Play a sound positioned in the world: distance-attenuated and stereo-panned
    // relative to the listener. Uses the same pooled/overlapping voices as Play().
    void PlayAt(const std::string& path, const glm::vec3& position,
                float pitch = 1.0f, float volume = 1.0f,
                AudioBus bus = AudioBus::SFX, int priority = 50);
    bool PlayCue(const std::string& cuePath, const glm::vec3& position = glm::vec3(0.0f),
                 bool force2D = false);

    // Stop every pooled sound-effect voice. Useful for editor audition controls
    // and scene transitions; background music is stopped separately.
    void StopAllSounds();

    // Persistent sources are owned by the audio engine and addressed by stable
    // handles. Unlike Play()/PlayAt(), their playback and spatial state can be
    // updated throughout an entity's lifetime.
    SourceHandle CreateSource(const std::string& path, bool spatial = true,
                              bool looping = false, bool streamed = false,
                              AudioBus bus = AudioBus::SFX);
    void DestroySource(SourceHandle source);
    void DestroyAllSources();
    bool PlaySource(SourceHandle source, bool restart = false);
    bool PauseSource(SourceHandle source);
    bool ResumeSource(SourceHandle source);
    bool StopSource(SourceHandle source);
    bool SeekSource(SourceHandle source, float seconds);
    bool SetSourcePosition(SourceHandle source, const glm::vec3& position);
    bool SetSourceVelocity(SourceHandle source, const glm::vec3& velocity);
    bool SetSourceDirection(SourceHandle source, const glm::vec3& direction);
    bool SetSourceCone(SourceHandle source, float innerAngleDegrees,
                       float outerAngleDegrees, float outerGain);
    bool SetSourceDoppler(SourceHandle source, float factor);
    bool SetSourceOcclusion(SourceHandle source, float amount);
    bool SetSourcePriority(SourceHandle source, int priority);
    bool SetSourceSpatial(SourceHandle source, bool spatial);
    bool SetSourceLooping(SourceHandle source, bool looping);
    bool SetSourceVolumePitch(SourceHandle source, float volume, float pitch);
    bool SetSourceAttenuation(SourceHandle source, float minDistance,
                              float maxDistance, float rolloff = 1.0f);
    bool IsSourcePlaying(SourceHandle source) const;
    bool IsSourcePaused(SourceHandle source) const;
    float SourceCursorSeconds(SourceHandle source) const;

    // Falloff for subsequent spatial plays: full volume within `minDistance`,
    // inaudible beyond `maxDistance`; `rolloff` shapes the inverse curve.
    void SetAttenuation(float minDistance, float maxDistance, float rolloff = 1.0f);

    // Master volume for all sounds (0 = silent, 1 = full). No-op if disabled.
    void SetMasterVolume(float volume);
    void SetBusVolume(AudioBus bus, float volume);
    float BusVolume(AudioBus bus) const;
    void SetBusMuted(AudioBus bus, bool muted);
    bool IsBusMuted(AudioBus bus) const;
    void SetBusEffects(AudioBus bus, const AudioBusEffects& effects);
    AudioBusEffects BusEffects(AudioBus bus) const;
    void ApplySnapshot(AudioSnapshotPreset preset, float transitionSeconds = 0.25f);
    AudioSnapshotPreset ActiveSnapshot() const;
    void SetDialogueDucking(bool enabled, float musicGain = 0.35f,
                            float attackSeconds = 0.08f, float releaseSeconds = 0.4f);
    bool DialogueDuckingEnabled() const;
    void UpdateMixer(float dt);
    void SetMaxVoices(std::size_t voices);
    std::size_t MaxVoices() const;
    DebugStats GetDebugStats() const;
    DeviceInfo GetDeviceInfo() const;
    void ApplyMixerPreset(const AudioMixerPreset& preset);
    AudioMixerPreset CaptureMixerPreset(const std::string& name = "Mixer") const;

    // Looping background music (streamed from disk). Replaces any current track.
    void PlayMusic(const std::string& path, float volume = 0.5f,
                   AudioBus bus = AudioBus::Music);
    void StopMusic();
    void SetMusicVolume(float volume);
    bool LoadAdaptiveMusicAsset(const std::string& path, std::string* error = nullptr);
    bool SetMusicState(const std::string& stateName, bool synchronizeToBeat = true);
    const std::string& MusicState() const;

    bool IsAvailable() const { return m_ok; }

private:
    struct Impl;            // holds the ma_engine; defined in the .cpp
    std::unique_ptr<Impl> m_impl;
    bool m_ok = false;
};

}   // namespace engine
