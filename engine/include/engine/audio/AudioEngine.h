#pragma once

#include <memory>
#include <string>

namespace engine {

// A thin wrapper over miniaudio's high-level engine for fire-and-forget sound
// effects.
//
// Construction is deliberately FAULT-TOLERANT: if no audio device is available
// (a headless machine, a CI box, a system with sound disabled) the engine simply
// turns itself off and Play() becomes a no-op. A game never crashes for lack of
// audio — it just runs silently.
//
// miniaudio.h is ~95k lines, so it is kept out of this header behind a PIMPL.
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