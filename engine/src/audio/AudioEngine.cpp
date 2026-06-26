#include "engine/audio/AudioEngine.h"

#include "miniaudio.h"      // API only — the implementation lives in miniaudio_impl.c

#include <iostream>
#include <unordered_map>
#include <vector>

namespace engine {

struct AudioEngine::Impl {
    ma_engine engine;
    ma_sound  music;
    bool      musicLoaded = false;

    // A pool of identical, decoded voices for one sound file. Cycling through
    // them lets the same effect overlap (rapid bounces).
    struct Pool {
        std::vector<ma_sound> voices;  // never resized after load -> stable addrs
        std::size_t next = 0;
    };
    std::unordered_map<std::string, std::unique_ptr<Pool>> pools;

    Pool* GetOrLoad(const std::string& path, int voices) {
        auto it = pools.find(path);
        if (it != pools.end()) return it->second.get();

        auto pool = std::make_unique<Pool>();
        pool->voices.resize(static_cast<std::size_t>(voices));  // zero-inits the structs
        for (int i = 0; i < voices; ++i) {
            // MA_SOUND_FLAG_DECODE: decode fully into memory now. The resource
            // manager shares the decoded data across voices of the same path.
            if (ma_sound_init_from_file(&engine, path.c_str(), MA_SOUND_FLAG_DECODE,
                                        nullptr, nullptr,
                                        &pool->voices[static_cast<std::size_t>(i)]) != MA_SUCCESS) {
                for (int k = 0; k < i; ++k)
                    ma_sound_uninit(&pool->voices[static_cast<std::size_t>(k)]);
                std::cerr << "[Audio] failed to load '" << path << "'\n";
                return nullptr;
            }
        }
        Pool* raw = pool.get();
        pools.emplace(path, std::move(pool));   // moves the unique_ptr, not the Pool
        return raw;
    }
};

AudioEngine::AudioEngine() : m_impl(std::make_unique<Impl>()) {
    if (ma_engine_init(nullptr, &m_impl->engine) == MA_SUCCESS) {
        m_ok = true;
    } else {
        // No device / no backend: run silently rather than failing the game.
        std::cerr << "[Audio] no audio device available; sound disabled.\n";
        m_ok = false;
    }
}

AudioEngine::~AudioEngine() {
    if (!m_ok) return;
    for (auto& kv : m_impl->pools)
        for (auto& v : kv.second->voices)
            ma_sound_uninit(&v);
    ma_engine_uninit(&m_impl->engine);
}

void AudioEngine::Preload(const std::string& path, int voices) {
    if (!m_ok) return;
    if (voices < 1) voices = 1;
    m_impl->GetOrLoad(path, voices);
}

void AudioEngine::SetMasterVolume(float volume) {
    if (!m_ok) return;
    ma_engine_set_volume(&m_impl->engine, volume);
}

void AudioEngine::PlayMusic(const std::string& path, float volume) {
    if (!m_ok) return;
    if (m_impl->musicLoaded) { ma_sound_uninit(&m_impl->music); m_impl->musicLoaded = false; }
    // Stream (don't fully decode) a long track, and loop it forever.
    if (ma_sound_init_from_file(&m_impl->engine, path.c_str(), MA_SOUND_FLAG_STREAM,
                                nullptr, nullptr, &m_impl->music) != MA_SUCCESS) {
        std::cerr << "[Audio] failed to load misic '" << path << "'\n";
        return;
    }
    ma_sound_set_looping(&m_impl->music, MA_TRUE);
    ma_sound_set_volume(&m_impl->music, volume);
    ma_sound_start(&m_impl->music);
    m_impl->musicLoaded = true;
}

void AudioEngine::StopMusic() {
    if (m_ok && m_impl->musicLoaded) {
        ma_sound_stop(&m_impl->music);
        ma_sound_uninit(&m_impl->music);
        m_impl->musicLoaded = false;
    }
}

void AudioEngine::SetMusicVolume(float volume) {
    if (m_ok && m_impl->musicLoaded) ma_sound_set_volume(&m_impl->music, volume);
}

void AudioEngine::Play(const std::string& path, float pitch, float volume) {
    if (!m_ok) return;
    Impl::Pool* pool = m_impl->GetOrLoad(path, 8);
    if (!pool || pool->voices.empty()) return;

    ma_sound& v = pool->voices[pool->next];
    pool->next = (pool->next + 1) % pool->voices.size();

    ma_sound_set_pitch(&v, pitch);
    ma_sound_set_volume(&v, volume);
    ma_sound_seek_to_pcm_frame(&v, 0);  // rewind in case this voice just played
    ma_sound_start(&v);
}

} // namespace engine