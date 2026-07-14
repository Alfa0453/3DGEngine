#pragma once

#include <glm/glm.hpp>

#include <memory>
#include <string>

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
    void Play(const std::string& path, float pitch = 1.0f, float volume = 1.0f);


    // --- 3D spatial audio ------------------------------------------------
    // Set the listener (usually the camera) for spatial playback. Call each frame
    // before positional plays so panning/attenuation track the viewpoint.
    void SetListener(const glm::vec3& position, const glm::vec3& forward,
                     const glm::vec3& up = glm::vec3(0.0f, 1.0f, 0.0f));

    // Play a sound positioned in the world: distance-attenuated and stereo-panned
    // relative to the listener. Uses the same pooled/overlapping voices as Play().
    void PlayAt(const std::string& path, const glm::vec3& position,
                float pitch = 1.0f, float volume = 1.0f);

    // Falloff for subsequent spatial plays: full volume within `minDistance`,
    // inaudible beyond `maxDistance`; `rolloff` shapes the inverse curve.
    void SetAttenuation(float minDistance, float maxDistance, float rolloff = 1.0f);

    // Master volume for all sounds (0 = silent, 1 = full). No-op if disabled.
    void SetMasterVolume(float volume);

    // Looping background music (streamed from disk). Replaces any current track.
    void PlayMusic(const std::string& path, float volume = 0.5f);
    void StopMusic();
    void SetMusicVolume(float volume);

    bool IsAvailable() const { return m_ok; }

private:
    struct Impl;            // holds the ma_engine; defined in the .cpp
    std::unique_ptr<Impl> m_impl;
    bool m_ok = false;
};

}   // namespace engine