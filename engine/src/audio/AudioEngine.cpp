#include "engine/audio/AudioEngine.h"

#include "miniaudio.h"      // API only — the implementation lives in miniaudio_impl.c

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <random>
#include <unordered_map>
#include <vector>

namespace engine {

const char* AudioBusName(AudioBus bus) {
    switch (bus) {
    case AudioBus::Master: return "Master";
    case AudioBus::Music: return "Music";
    case AudioBus::SFX: return "SFX";
    case AudioBus::Dialogue: return "Dialogue";
    case AudioBus::UI: return "UI";
    case AudioBus::Ambient: return "Ambient";
    case AudioBus::Count: break;
    }
    return "SFX";
}

const char* AudioSnapshotPresetName(AudioSnapshotPreset preset) {
    switch (preset) {
    case AudioSnapshotPreset::Default: return "Default";
    case AudioSnapshotPreset::Paused: return "Paused";
    case AudioSnapshotPreset::Underwater: return "Underwater";
    case AudioSnapshotPreset::Indoor: return "Indoor";
    case AudioSnapshotPreset::Cinematic: return "Cinematic";
    }
    return "Default";
}

namespace {

struct CompressorNode {
    ma_node_base base;
    std::atomic<float> thresholdLinear{1.0f};
    std::atomic<float> ratio{1.0f};
    ma_uint32 channels = 2;
};

void ProcessCompressor(ma_node* node, const float** input, ma_uint32*,
                       float** output, ma_uint32* frameCount) {
    CompressorNode* compressor = reinterpret_cast<CompressorNode*>(node);
    const float threshold = std::max(compressor->thresholdLinear.load(), 0.0001f);
    const float ratio = std::max(compressor->ratio.load(), 1.0f);
    const std::size_t samples = static_cast<std::size_t>(*frameCount) * compressor->channels;
    for (std::size_t i = 0; i < samples; ++i) {
        const float sample = input[0][i];
        const float magnitude = std::abs(sample);
        if (magnitude <= threshold || ratio <= 1.0001f) output[0][i] = sample;
        else {
            const float compressed = threshold + (magnitude - threshold) / ratio;
            output[0][i] = std::copysign(compressed, sample);
        }
    }
}

ma_node_vtable g_compressorVtable{
    ProcessCompressor, nullptr, 1, 1, 0
};

bool InitCompressor(ma_node_graph* graph, ma_uint32 channels, CompressorNode* node) {
    node->channels = channels;
    ma_node_config config = ma_node_config_init();
    config.vtable = &g_compressorVtable;
    config.pInputChannels = &channels;
    config.pOutputChannels = &channels;
    return ma_node_init(graph, &config, nullptr, node) == MA_SUCCESS;
}

} // namespace

struct AudioEngine::Impl {
    ma_engine engine;
    ma_sound  music;
    bool      musicLoaded = false;
    static constexpr std::size_t BusCount = static_cast<std::size_t>(AudioBus::Count);
    std::array<ma_sound_group, BusCount> groups{};
    std::array<bool, BusCount> groupInitialized{};
    std::array<float, BusCount> busVolumes{1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    std::array<bool, BusCount> busMuted{};
    std::array<AudioBusEffects, BusCount> busEffects{};
    std::array<CompressorNode, BusCount> compressors{};
    std::array<ma_delay_node, BusCount> reverbs{};
    std::array<ma_lpf_node, BusCount> lowPass{};
    std::array<ma_hpf_node, BusCount> highPass{};
    std::array<bool, BusCount> effectsInitialized{};
    std::array<float, BusCount> snapshotGain{1, 1, 1, 1, 1, 1};
    std::array<float, BusCount> snapshotStartGain{1, 1, 1, 1, 1, 1};
    std::array<float, BusCount> snapshotTargetGain{1, 1, 1, 1, 1, 1};
    std::array<AudioBusEffects, BusCount> snapshotStartEffects{};
    std::array<AudioBusEffects, BusCount> snapshotTargetEffects{};
    AudioSnapshotPreset activeSnapshot = AudioSnapshotPreset::Default;
    float snapshotTime = 0.0f;
    float snapshotDuration = 0.0f;
    bool duckingEnabled = true;
    float duckMusicGain = 0.35f;
    float duckAttack = 0.08f;
    float duckRelease = 0.4f;
    float currentDuckGain = 1.0f;
    float     minDist = 1.0f, maxDist = 40.0f, rolloff = 1.0f;   // spatial falloff

    // A pool of identical, decoded voices for one sound file. Cycling through
    // them lets the same effect overlap (rapid bounces).
    struct Pool {
        std::vector<ma_sound> voices;  // never resized after load -> stable addrs
        std::vector<int> priorities;
        std::vector<std::uint64_t> serials;
        std::size_t next = 0;
        AudioBus bus = AudioBus::SFX;
        std::string path;
    };
    std::unordered_map<std::string, std::unique_ptr<Pool>> pools;
    enum class SourceState { Stopped, Playing, Paused };
    struct ManagedSource {
        ma_sound sound{};
        SourceState state = SourceState::Stopped;
        AudioBus bus = AudioBus::SFX;
        int priority = 50;
        float baseVolume = 1.0f;
        float occlusion = 0.0f;
        bool streamed = false;
    };
    std::unordered_map<AudioEngine::SourceHandle, std::unique_ptr<ManagedSource>> sources;
    AudioEngine::SourceHandle nextSource = 1;
    std::size_t maxVoices = 64;
    std::uint64_t playSerial = 1;
    std::size_t stolenVoices = 0;
    std::size_t rejectedVoices = 0;
    std::unordered_map<std::string, AudioCueAsset> cueCache;
    std::unordered_map<std::string, std::size_t> cueSequence;
    std::unordered_map<std::string, std::size_t> cueLastChoice;
    std::unordered_map<std::string, float> cueCooldown;
    struct PendingCuePlay {
        std::string path;
        glm::vec3 position{0.0f};
        float delay = 0.0f;
        float pitch = 1.0f;
        float volume = 1.0f;
        AudioBus bus = AudioBus::SFX;
        int priority = 50;
        bool spatial = false;
    };
    std::vector<PendingCuePlay> pendingCuePlays;
    std::mt19937 random{std::random_device{}()};
    AdaptiveMusicAsset adaptiveMusic;
    std::string adaptiveMusicPath;
    std::string musicState;
    struct MusicStem {
        AudioEngine::SourceHandle handle = AudioEngine::InvalidSource;
        float from = 0.0f;
        float to = 0.0f;
        float time = 0.0f;
        float duration = 0.0f;
        bool destroyWhenSilent = false;
    };
    std::vector<MusicStem> musicStems;

    std::size_t ActiveVoiceCount() const {
        std::size_t result = musicLoaded ? 1u : 0u;
        for (const auto& entry : sources)
            if (ma_sound_is_playing(&entry.second->sound) == MA_TRUE) ++result;
        for (const auto& entry : pools)
            for (const ma_sound& voice : entry.second->voices)
                if (ma_sound_is_playing(&voice) == MA_TRUE) ++result;
        return result;
    }

    bool AcquireVoice(int priority) {
        if (ActiveVoiceCount() < maxVoices) return true;
        Pool* victimPool = nullptr;
        std::size_t victimIndex = 0;
        int victimPriority = 101;
        std::uint64_t victimSerial = UINT64_MAX;
        for (auto& entry : pools) {
            Pool& pool = *entry.second;
            for (std::size_t i = 0; i < pool.voices.size(); ++i) {
                if (ma_sound_is_playing(&pool.voices[i]) != MA_TRUE) continue;
                if (pool.priorities[i] < victimPriority
                    || (pool.priorities[i] == victimPriority && pool.serials[i] < victimSerial)) {
                    victimPool = &pool;
                    victimIndex = i;
                    victimPriority = pool.priorities[i];
                    victimSerial = pool.serials[i];
                }
            }
        }
        if (!victimPool || priority < victimPriority) {
            ++rejectedVoices;
            return false;
        }
        ma_sound_stop(&victimPool->voices[victimIndex]);
        ma_sound_seek_to_pcm_frame(&victimPool->voices[victimIndex], 0);
        ++stolenVoices;
        return true;
    }

    ma_sound_group* Group(AudioBus bus) {
        const std::size_t index = static_cast<std::size_t>(bus);
        return index > 0 && index < BusCount && groupInitialized[index] ? &groups[index] : nullptr;
    }

    void ApplyGain(AudioBus bus) {
        const std::size_t index = static_cast<std::size_t>(bus);
        if (index >= BusCount) return;
        float applied = busMuted[index] ? 0.0f : busVolumes[index] * snapshotGain[index];
        if (bus == AudioBus::Music) applied *= currentDuckGain;
        if (bus == AudioBus::Master) ma_engine_set_volume(&engine, applied);
        else if (ma_sound_group* group = Group(bus)) ma_sound_group_set_volume(group, applied);
    }

    Pool* GetOrLoad(const std::string& path, int voices, AudioBus bus) {
        const std::string key = std::to_string(static_cast<int>(bus)) + ":" + path;
        auto it = pools.find(key);
        if (it != pools.end()) return it->second.get();

        auto pool = std::make_unique<Pool>();
        pool->bus = bus;
        pool->path = path;
        pool->voices.resize(static_cast<std::size_t>(voices));  // zero-inits the structs
        pool->priorities.assign(static_cast<std::size_t>(voices), 50);
        pool->serials.assign(static_cast<std::size_t>(voices), 0);
        for (int i = 0; i < voices; ++i) {
            // MA_SOUND_FLAG_DECODE: decode fully into memory now. The resource
            // manager shares the decoded data across voices of the same path.
            if (ma_sound_init_from_file(&engine, path.c_str(), MA_SOUND_FLAG_DECODE,
                                        Group(bus), nullptr,
                                        &pool->voices[static_cast<std::size_t>(i)]) != MA_SUCCESS) {
                for (int k = 0; k < i; ++k)
                    ma_sound_uninit(&pool->voices[static_cast<std::size_t>(k)]);
                std::cerr << "[Audio] failed to load '" << path << "'\n";
                return nullptr;
            }
        }
        Pool* raw = pool.get();
        pools.emplace(key, std::move(pool));   // moves the unique_ptr, not the Pool
        return raw;
    }
};

AudioEngine::AudioEngine() : m_impl(std::make_unique<Impl>()) {
    if (ma_engine_init(nullptr, &m_impl->engine) == MA_SUCCESS) {
        m_ok = true;
        for (std::size_t i = 1; i < Impl::BusCount; ++i) {
            if (ma_sound_group_init(&m_impl->engine, 0, nullptr, &m_impl->groups[i]) != MA_SUCCESS)
                continue;
            m_impl->groupInitialized[i] = true;
            const ma_uint32 channels = ma_engine_get_channels(&m_impl->engine);
            const ma_uint32 sampleRate = ma_engine_get_sample_rate(&m_impl->engine);
            ma_node_graph* graph = ma_engine_get_node_graph(&m_impl->engine);
            const auto delayConfig = ma_delay_node_config_init(
                channels, sampleRate, std::max(sampleRate * 80u / 1000u, 1u), 0.35f);
            const auto lpfConfig = ma_lpf_node_config_init(channels, sampleRate, 20000.0, 2);
            const auto hpfConfig = ma_hpf_node_config_init(channels, sampleRate, 20.0, 2);
            const bool compressorOk = InitCompressor(graph, channels, &m_impl->compressors[i]);
            const bool reverbOk = compressorOk
                && ma_delay_node_init(graph, &delayConfig, nullptr, &m_impl->reverbs[i]) == MA_SUCCESS;
            const bool lpfOk = reverbOk
                && ma_lpf_node_init(graph, &lpfConfig, nullptr, &m_impl->lowPass[i]) == MA_SUCCESS;
            const bool hpfOk = lpfOk
                && ma_hpf_node_init(graph, &hpfConfig, nullptr, &m_impl->highPass[i]) == MA_SUCCESS;
            if (!hpfOk) continue;
            ma_delay_node_set_wet(&m_impl->reverbs[i], 0.0f);
            ma_delay_node_set_dry(&m_impl->reverbs[i], 1.0f);
            ma_node_attach_output_bus(reinterpret_cast<ma_node*>(&m_impl->groups[i]), 0,
                reinterpret_cast<ma_node*>(&m_impl->compressors[i]), 0);
            ma_node_attach_output_bus(reinterpret_cast<ma_node*>(&m_impl->compressors[i]), 0,
                reinterpret_cast<ma_node*>(&m_impl->reverbs[i]), 0);
            ma_node_attach_output_bus(reinterpret_cast<ma_node*>(&m_impl->reverbs[i]), 0,
                reinterpret_cast<ma_node*>(&m_impl->lowPass[i]), 0);
            ma_node_attach_output_bus(reinterpret_cast<ma_node*>(&m_impl->lowPass[i]), 0,
                reinterpret_cast<ma_node*>(&m_impl->highPass[i]), 0);
            ma_node_attach_output_bus(reinterpret_cast<ma_node*>(&m_impl->highPass[i]), 0,
                ma_node_graph_get_endpoint(graph), 0);
            m_impl->effectsInitialized[i] = true;
        }
    } else {
        // No device / no backend: run silently rather than failing the game.
        std::cerr << "[Audio] no audio device available; sound disabled.\n";
        m_ok = false;
    }
}

AudioEngine::~AudioEngine() {
    if (!m_ok) return;
    if (m_impl->musicLoaded) {
        ma_sound_stop(&m_impl->music);
        ma_sound_uninit(&m_impl->music);
        m_impl->musicLoaded = false;
    }
    for (auto& entry : m_impl->sources) ma_sound_uninit(&entry.second->sound);
    m_impl->sources.clear();
    for (auto& kv : m_impl->pools)
        for (auto& v : kv.second->voices)
            ma_sound_uninit(&v);
    for (std::size_t i = 1; i < Impl::BusCount; ++i)
        if (m_impl->groupInitialized[i]) ma_sound_group_uninit(&m_impl->groups[i]);
    for (std::size_t i = 1; i < Impl::BusCount; ++i) {
        if (!m_impl->effectsInitialized[i]) continue;
        ma_node_uninit(&m_impl->compressors[i], nullptr);
        ma_delay_node_uninit(&m_impl->reverbs[i], nullptr);
        ma_lpf_node_uninit(&m_impl->lowPass[i], nullptr);
        ma_hpf_node_uninit(&m_impl->highPass[i], nullptr);
    }
    ma_engine_uninit(&m_impl->engine);
}

void AudioEngine::Preload(const std::string& path, int voices) {
    if (!m_ok) return;
    if (voices < 1) voices = 1;
    m_impl->GetOrLoad(path, voices, AudioBus::SFX);
}

void AudioEngine::SetMasterVolume(float volume) {
    SetBusVolume(AudioBus::Master, volume);
}

void AudioEngine::SetBusVolume(AudioBus bus, float volume) {
    const std::size_t index = static_cast<std::size_t>(bus);
    if (index >= Impl::BusCount) return;
    m_impl->busVolumes[index] = std::max(volume, 0.0f);
    if (!m_ok) return;
    m_impl->ApplyGain(bus);
}

float AudioEngine::BusVolume(AudioBus bus) const {
    const std::size_t index = static_cast<std::size_t>(bus);
    return index < Impl::BusCount ? m_impl->busVolumes[index] : 1.0f;
}

void AudioEngine::SetBusMuted(AudioBus bus, bool muted) {
    const std::size_t index = static_cast<std::size_t>(bus);
    if (index >= Impl::BusCount) return;
    m_impl->busMuted[index] = muted;
    SetBusVolume(bus, m_impl->busVolumes[index]);
}

bool AudioEngine::IsBusMuted(AudioBus bus) const {
    const std::size_t index = static_cast<std::size_t>(bus);
    return index < Impl::BusCount && m_impl->busMuted[index];
}

void AudioEngine::PlayMusic(const std::string& path, float volume, AudioBus bus) {
    if (!m_ok) return;
    if (m_impl->musicLoaded) { ma_sound_uninit(&m_impl->music); m_impl->musicLoaded = false; }
    // Stream (don't fully decode) a long track, and loop it forever.
    if (ma_sound_init_from_file(&m_impl->engine, path.c_str(), MA_SOUND_FLAG_STREAM,
                                m_impl->Group(bus), nullptr, &m_impl->music) != MA_SUCCESS) {
        std::cerr << "[Audio] failed to load music '" << path << "'\n";
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

void AudioEngine::Play(const std::string& path, float pitch, float volume, AudioBus bus,
                       int priority) {
    if (!m_ok) return;
    Impl::Pool* pool = m_impl->GetOrLoad(path, 8, bus);
    if (!pool || pool->voices.empty()) return;
    priority = std::clamp(priority, 0, 100);
    if (!m_impl->AcquireVoice(priority)) return;

    const std::size_t index = pool->next;
    ma_sound& v = pool->voices[index];
    pool->next = (pool->next + 1) % pool->voices.size();
    pool->priorities[index] = priority;
    pool->serials[index] = m_impl->playSerial++;

    ma_sound_set_spatialization_enabled(&v, MA_FALSE);  // 2D: always centred
    ma_sound_set_pitch(&v, pitch);
    ma_sound_set_volume(&v, volume);
    ma_sound_seek_to_pcm_frame(&v, 0);  // rewind in case this voice just played
    ma_sound_start(&v);
}

void AudioEngine::SetListener(const glm::vec3& position, const glm::vec3& forward, const glm::vec3& up) {
    if (!m_ok) return;
    ma_engine_listener_set_position(&m_impl->engine, 0, position.x, position.y, position.z);
    ma_engine_listener_set_direction(&m_impl->engine, 0, forward.x, forward.y, forward.z);
    ma_engine_listener_set_world_up(&m_impl->engine, 0, up.x, up.y, up.z);
}

void AudioEngine::SetAttenuation(float minDistance, float maxDistance, float rolloff) {
    m_impl->minDist = std::max(minDistance, 0.01f);
    m_impl->maxDist = std::max(maxDistance, m_impl->minDist);
    m_impl->rolloff = std::max(rolloff, 0.0f);
}

void AudioEngine::SetBusEffects(AudioBus bus, const AudioBusEffects& effects) {
    const std::size_t index = static_cast<std::size_t>(bus);
    if (index >= Impl::BusCount) return;
    AudioBusEffects safe = effects;
    safe.lowPassHz = std::clamp(safe.lowPassHz, 20.0f, 20000.0f);
    safe.highPassHz = std::clamp(safe.highPassHz, 20.0f, 20000.0f);
    safe.reverbWet = std::clamp(safe.reverbWet, 0.0f, 1.0f);
    safe.reverbDecay = std::clamp(safe.reverbDecay, 0.0f, 0.95f);
    safe.compressorThresholdDb = std::clamp(safe.compressorThresholdDb, -60.0f, 0.0f);
    safe.compressorRatio = std::clamp(safe.compressorRatio, 1.0f, 20.0f);
    m_impl->busEffects[index] = safe;
    if (!m_ok || index == 0 || !m_impl->effectsInitialized[index]) return;
    const ma_uint32 channels = ma_engine_get_channels(&m_impl->engine);
    const ma_uint32 sampleRate = ma_engine_get_sample_rate(&m_impl->engine);
    const ma_lpf_config lpf = ma_lpf_config_init(ma_format_f32, channels, sampleRate,
                                                 safe.lowPassHz, 2);
    const ma_hpf_config hpf = ma_hpf_config_init(ma_format_f32, channels, sampleRate,
                                                 safe.highPassHz, 2);
    ma_lpf_node_reinit(&lpf, &m_impl->lowPass[index]);
    ma_hpf_node_reinit(&hpf, &m_impl->highPass[index]);
    ma_delay_node_set_wet(&m_impl->reverbs[index], safe.reverbWet);
    ma_delay_node_set_dry(&m_impl->reverbs[index], 1.0f - safe.reverbWet * 0.35f);
    ma_delay_node_set_decay(&m_impl->reverbs[index], safe.reverbDecay);
    m_impl->compressors[index].thresholdLinear.store(
        std::pow(10.0f, safe.compressorThresholdDb / 20.0f));
    m_impl->compressors[index].ratio.store(safe.compressorRatio);
}

AudioBusEffects AudioEngine::BusEffects(AudioBus bus) const {
    const std::size_t index = static_cast<std::size_t>(bus);
    return index < Impl::BusCount ? m_impl->busEffects[index] : AudioBusEffects{};
}

void AudioEngine::ApplySnapshot(AudioSnapshotPreset preset, float transitionSeconds) {
    m_impl->activeSnapshot = preset;
    m_impl->snapshotTime = 0.0f;
    m_impl->snapshotDuration = std::max(transitionSeconds, 0.0f);
    m_impl->snapshotStartGain = m_impl->snapshotGain;
    m_impl->snapshotStartEffects = m_impl->busEffects;
    m_impl->snapshotTargetGain.fill(1.0f);
    m_impl->snapshotTargetEffects.fill(AudioBusEffects{});

    auto gain = [this](AudioBus bus, float value) {
        m_impl->snapshotTargetGain[static_cast<std::size_t>(bus)] = value;
    };
    auto effects = [this](AudioBus bus) -> AudioBusEffects& {
        return m_impl->snapshotTargetEffects[static_cast<std::size_t>(bus)];
    };
    switch (preset) {
    case AudioSnapshotPreset::Default:
        break;
    case AudioSnapshotPreset::Paused:
        gain(AudioBus::Music, 0.45f);
        gain(AudioBus::SFX, 0.18f);
        gain(AudioBus::Dialogue, 0.35f);
        gain(AudioBus::Ambient, 0.2f);
        effects(AudioBus::Music).lowPassHz = 3500.0f;
        effects(AudioBus::SFX).lowPassHz = 2500.0f;
        effects(AudioBus::Ambient).lowPassHz = 1800.0f;
        break;
    case AudioSnapshotPreset::Underwater:
        gain(AudioBus::SFX, 0.65f);
        gain(AudioBus::Ambient, 0.8f);
        effects(AudioBus::SFX).lowPassHz = 850.0f;
        effects(AudioBus::SFX).reverbWet = 0.32f;
        effects(AudioBus::SFX).reverbDecay = 0.7f;
        effects(AudioBus::Ambient).lowPassHz = 700.0f;
        effects(AudioBus::Ambient).reverbWet = 0.4f;
        effects(AudioBus::Ambient).reverbDecay = 0.78f;
        break;
    case AudioSnapshotPreset::Indoor:
        gain(AudioBus::Ambient, 0.72f);
        effects(AudioBus::SFX).reverbWet = 0.18f;
        effects(AudioBus::SFX).reverbDecay = 0.48f;
        effects(AudioBus::Dialogue).reverbWet = 0.08f;
        effects(AudioBus::Ambient).lowPassHz = 9000.0f;
        break;
    case AudioSnapshotPreset::Cinematic:
        gain(AudioBus::Music, 1.08f);
        gain(AudioBus::SFX, 0.82f);
        gain(AudioBus::Dialogue, 1.12f);
        effects(AudioBus::SFX).compressorThresholdDb = -10.0f;
        effects(AudioBus::SFX).compressorRatio = 3.0f;
        effects(AudioBus::Dialogue).compressorThresholdDb = -12.0f;
        effects(AudioBus::Dialogue).compressorRatio = 2.5f;
        break;
    }
    if (m_impl->snapshotDuration <= 0.0f) UpdateMixer(0.0f);
}

AudioSnapshotPreset AudioEngine::ActiveSnapshot() const {
    return m_impl->activeSnapshot;
}

void AudioEngine::SetDialogueDucking(bool enabled, float musicGain,
                                     float attackSeconds, float releaseSeconds) {
    m_impl->duckingEnabled = enabled;
    m_impl->duckMusicGain = std::clamp(musicGain, 0.0f, 1.0f);
    m_impl->duckAttack = std::max(attackSeconds, 0.001f);
    m_impl->duckRelease = std::max(releaseSeconds, 0.001f);
}

bool AudioEngine::DialogueDuckingEnabled() const {
    return m_impl->duckingEnabled;
}

void AudioEngine::UpdateMixer(float dt) {
    const float elapsed = std::max(dt, 0.0f);
    for (auto& entry : m_impl->cueCooldown)
        entry.second = std::max(entry.second - elapsed, 0.0f);
    for (auto it = m_impl->pendingCuePlays.begin(); it != m_impl->pendingCuePlays.end();) {
        it->delay -= elapsed;
        if (it->delay > 0.0f) {
            ++it;
            continue;
        }
        if (it->spatial)
            PlayAt(it->path, it->position, it->pitch, it->volume, it->bus, it->priority);
        else
            Play(it->path, it->pitch, it->volume, it->bus, it->priority);
        it = m_impl->pendingCuePlays.erase(it);
    }
    for (auto it = m_impl->musicStems.begin(); it != m_impl->musicStems.end();) {
        it->time = std::min(it->time + elapsed, it->duration);
        const float t = it->duration <= 0.0f ? 1.0f : it->time / it->duration;
        SetSourceVolumePitch(it->handle, it->from + (it->to - it->from) * t, 1.0f);
        if (t >= 1.0f && it->destroyWhenSilent) {
            DestroySource(it->handle);
            it = m_impl->musicStems.erase(it);
        } else {
            ++it;
        }
    }
    if (m_impl->snapshotTime < m_impl->snapshotDuration || m_impl->snapshotDuration <= 0.0f) {
        m_impl->snapshotTime = std::min(m_impl->snapshotTime + elapsed,
                                        m_impl->snapshotDuration);
        const float t = m_impl->snapshotDuration <= 0.0f
            ? 1.0f : std::clamp(m_impl->snapshotTime / m_impl->snapshotDuration, 0.0f, 1.0f);
        auto lerp = [t](float a, float b) { return a + (b - a) * t; };
        for (std::size_t i = 0; i < Impl::BusCount; ++i) {
            m_impl->snapshotGain[i] = lerp(m_impl->snapshotStartGain[i],
                                           m_impl->snapshotTargetGain[i]);
            AudioBusEffects fx;
            const AudioBusEffects& a = m_impl->snapshotStartEffects[i];
            const AudioBusEffects& b = m_impl->snapshotTargetEffects[i];
            fx.lowPassHz = lerp(a.lowPassHz, b.lowPassHz);
            fx.highPassHz = lerp(a.highPassHz, b.highPassHz);
            fx.reverbWet = lerp(a.reverbWet, b.reverbWet);
            fx.reverbDecay = lerp(a.reverbDecay, b.reverbDecay);
            fx.compressorThresholdDb = lerp(a.compressorThresholdDb, b.compressorThresholdDb);
            fx.compressorRatio = lerp(a.compressorRatio, b.compressorRatio);
            SetBusEffects(static_cast<AudioBus>(i), fx);
        }
        if (m_impl->snapshotDuration <= 0.0f) m_impl->snapshotDuration = -1.0f;
    }

    bool dialoguePlaying = false;
    if (m_impl->duckingEnabled) {
        for (const auto& entry : m_impl->sources) {
            if (entry.second->bus == AudioBus::Dialogue
                && ma_sound_is_playing(&entry.second->sound) == MA_TRUE) {
                dialoguePlaying = true;
                break;
            }
        }
        if (!dialoguePlaying) {
            for (const auto& entry : m_impl->pools) {
                if (entry.second->bus != AudioBus::Dialogue) continue;
                for (const ma_sound& voice : entry.second->voices) {
                    if (ma_sound_is_playing(&voice) == MA_TRUE) {
                        dialoguePlaying = true;
                        break;
                    }
                }
                if (dialoguePlaying) break;
            }
        }
    }
    const float duckTarget = dialoguePlaying ? m_impl->duckMusicGain : 1.0f;
    const float duckTime = dialoguePlaying ? m_impl->duckAttack : m_impl->duckRelease;
    const float duckStep = duckTime <= 0.0f ? 1.0f : std::clamp(elapsed / duckTime, 0.0f, 1.0f);
    m_impl->currentDuckGain += (duckTarget - m_impl->currentDuckGain) * duckStep;
    for (std::size_t i = 0; i < Impl::BusCount; ++i)
        m_impl->ApplyGain(static_cast<AudioBus>(i));
}

void AudioEngine::PlayAt(const std::string& path, const glm::vec3& position,
                         float pitch, float volume, AudioBus bus, int priority) {
    if (!m_ok) return;
    Impl::Pool* pool = m_impl->GetOrLoad(path, 8, bus);
    if (!pool || pool->voices.empty()) return;
    priority = std::clamp(priority, 0, 100);
    if (!m_impl->AcquireVoice(priority)) return;

    const std::size_t index = pool->next;
    ma_sound& v = pool->voices[index];
    pool->next = (pool->next + 1) % pool->voices.size();
    pool->priorities[index] = priority;
    pool->serials[index] = m_impl->playSerial++;

    ma_sound_set_spatialization_enabled(&v, MA_TRUE);
    ma_sound_set_attenuation_model(&v, ma_attenuation_model_inverse);
    ma_sound_set_position(&v, position.x, position.y, position.z);
    ma_sound_set_min_distance(&v, m_impl->minDist);
    ma_sound_set_max_distance(&v, m_impl->maxDist);
    ma_sound_set_rolloff(&v, m_impl->rolloff);
    ma_sound_set_pitch(&v, pitch);
    ma_sound_set_volume(&v, volume);
    ma_sound_seek_to_pcm_frame(&v, 0);
    ma_sound_start(&v);
}

bool AudioEngine::PlayCue(const std::string& cuePath, const glm::vec3& position, bool force2D) {
    if (!m_ok || cuePath.empty()) return false;
    auto found = m_impl->cueCache.find(cuePath);
    if (found == m_impl->cueCache.end()) {
        AudioCueAsset cue;
        std::string error;
        if (!LoadAudioCue(cuePath, &cue, &error)) {
            std::cerr << "[Audio] " << error << " '" << cuePath << "'\n";
            return false;
        }
        const std::filesystem::path parent = std::filesystem::path(cuePath).parent_path();
        for (AudioCueClip& clip : cue.clips) {
            const std::filesystem::path source(clip.path);
            if (source.is_relative()) clip.path = (parent / source).lexically_normal().string();
        }
        found = m_impl->cueCache.emplace(cuePath, std::move(cue)).first;
    }
    AudioCueAsset& cue = found->second;
    float& cooldown = m_impl->cueCooldown[cuePath];
    if (cooldown > 0.0f) return false;
    std::size_t cueInstances = 0;
    for (const AudioCueClip& clip : cue.clips) {
        for (const auto& entry : m_impl->pools) {
            if (entry.second->path != clip.path) continue;
            for (const ma_sound& voice : entry.second->voices)
                if (ma_sound_is_playing(&voice) == MA_TRUE) ++cueInstances;
        }
    }
    if (cueInstances >= static_cast<std::size_t>(cue.maxInstances)) return false;

    std::vector<std::size_t> choices;
    if (cue.mode == AudioCueMode::Layered) {
        choices.resize(cue.clips.size());
        for (std::size_t i = 0; i < choices.size(); ++i) choices[i] = i;
    } else if (cue.mode == AudioCueMode::Sequence) {
        std::size_t& next = m_impl->cueSequence[cuePath];
        choices.push_back(next % cue.clips.size());
        next = (next + 1) % cue.clips.size();
    } else {
        float total = 0.0f;
        for (const AudioCueClip& clip : cue.clips) total += std::max(clip.weight, 0.0f);
        std::uniform_real_distribution<float> distribution(0.0f, std::max(total, 0.0001f));
        const std::size_t last = m_impl->cueLastChoice.count(cuePath)
            ? m_impl->cueLastChoice[cuePath] : cue.clips.size();
        std::size_t chosen = 0;
        for (int attempt = 0; attempt < 4; ++attempt) {
            float target = distribution(m_impl->random);
            for (std::size_t i = 0; i < cue.clips.size(); ++i) {
                target -= std::max(cue.clips[i].weight, 0.0f);
                if (target <= 0.0f) { chosen = i; break; }
            }
            if (!cue.noImmediateRepeat || cue.clips.size() == 1 || chosen != last) break;
        }
        m_impl->cueLastChoice[cuePath] = chosen;
        choices.push_back(chosen);
    }

    std::uniform_real_distribution<float> volumeVariation(cue.volumeMin, cue.volumeMax);
    std::uniform_real_distribution<float> pitchVariation(cue.pitchMin, cue.pitchMax);
    bool played = false;
    for (const std::size_t index : choices) {
        const AudioCueClip& clip = cue.clips[index];
        const float volume = clip.volume * volumeVariation(m_impl->random);
        const float pitch = clip.pitch * pitchVariation(m_impl->random);
        if (clip.delaySeconds > 0.0f) {
            m_impl->pendingCuePlays.push_back({clip.path, position, clip.delaySeconds, pitch,
                volume, cue.bus, cue.priority, cue.spatial && !force2D});
        } else if (cue.spatial && !force2D) {
            PlayAt(clip.path, position, pitch, volume, cue.bus, cue.priority);
        } else {
            Play(clip.path, pitch, volume, cue.bus, cue.priority);
        }
        played = true;
    }
    if (played) cooldown = cue.cooldownSeconds;
    return played;
}

void AudioEngine::StopAllSounds() {
    if (!m_ok) return;
    for (auto& entry : m_impl->pools) {
        for (ma_sound& voice : entry.second->voices) {
            ma_sound_stop(&voice);
            ma_sound_seek_to_pcm_frame(&voice, 0);
        }
    }
    for (auto& entry : m_impl->sources) {
        ma_sound_stop(&entry.second->sound);
        ma_sound_seek_to_pcm_frame(&entry.second->sound, 0);
        entry.second->state = Impl::SourceState::Stopped;
    }
}

AudioEngine::SourceHandle AudioEngine::CreateSource(const std::string& path, bool spatial,
                                                     bool looping, bool streamed, AudioBus bus) {
    if (!m_ok || path.empty()) return InvalidSource;
    auto source = std::make_unique<Impl::ManagedSource>();
    source->bus = bus;
    source->streamed = streamed;
    const ma_uint32 flags = streamed ? MA_SOUND_FLAG_STREAM : MA_SOUND_FLAG_DECODE;
    if (ma_sound_init_from_file(&m_impl->engine, path.c_str(), flags,
                                m_impl->Group(bus), nullptr, &source->sound) != MA_SUCCESS)
        return InvalidSource;
    ma_sound_set_spatialization_enabled(&source->sound, spatial ? MA_TRUE : MA_FALSE);
    ma_sound_set_positioning(&source->sound, ma_positioning_absolute);
    ma_sound_set_looping(&source->sound, looping ? MA_TRUE : MA_FALSE);
    SourceHandle handle = m_impl->nextSource++;
    if (handle == InvalidSource) handle = m_impl->nextSource++;
    m_impl->sources.emplace(handle, std::move(source));
    return handle;
}

void AudioEngine::DestroySource(SourceHandle source) {
    const auto found = m_impl->sources.find(source);
    if (found == m_impl->sources.end()) return;
    ma_sound_stop(&found->second->sound);
    ma_sound_uninit(&found->second->sound);
    m_impl->sources.erase(found);
}

void AudioEngine::DestroyAllSources() {
    for (auto& entry : m_impl->sources) {
        ma_sound_stop(&entry.second->sound);
        ma_sound_uninit(&entry.second->sound);
    }
    m_impl->sources.clear();
}

bool AudioEngine::PlaySource(SourceHandle source, bool restart) {
    const auto found = m_impl->sources.find(source);
    if (found == m_impl->sources.end()) return false;
    if (ma_sound_is_playing(&found->second->sound) != MA_TRUE
        && !m_impl->AcquireVoice(found->second->priority)) return false;
    if (restart || ma_sound_at_end(&found->second->sound)) ma_sound_seek_to_pcm_frame(&found->second->sound, 0);
    if (ma_sound_start(&found->second->sound) != MA_SUCCESS) return false;
    found->second->state = Impl::SourceState::Playing;
    return true;
}

bool AudioEngine::PauseSource(SourceHandle source) {
    const auto found = m_impl->sources.find(source);
    if (found == m_impl->sources.end()) return false;
    if (ma_sound_stop(&found->second->sound) != MA_SUCCESS) return false;
    found->second->state = Impl::SourceState::Paused;
    return true;
}

bool AudioEngine::ResumeSource(SourceHandle source) {
    const auto found = m_impl->sources.find(source);
    if (found == m_impl->sources.end()) return false;
    if (ma_sound_start(&found->second->sound) != MA_SUCCESS) return false;
    found->second->state = Impl::SourceState::Playing;
    return true;
}

bool AudioEngine::StopSource(SourceHandle source) {
    const auto found = m_impl->sources.find(source);
    if (found == m_impl->sources.end()) return false;
    ma_sound_stop(&found->second->sound);
    ma_sound_seek_to_pcm_frame(&found->second->sound, 0);
    found->second->state = Impl::SourceState::Stopped;
    return true;
}

bool AudioEngine::SeekSource(SourceHandle source, float seconds) {
    const auto found = m_impl->sources.find(source);
    return found != m_impl->sources.end()
        && ma_sound_seek_to_second(&found->second->sound, std::max(seconds, 0.0f)) == MA_SUCCESS;
}

bool AudioEngine::SetSourcePosition(SourceHandle source, const glm::vec3& position) {
    const auto found = m_impl->sources.find(source);
    if (found == m_impl->sources.end()) return false;
    ma_sound_set_position(&found->second->sound, position.x, position.y, position.z);
    return true;
}

bool AudioEngine::SetSourceVelocity(SourceHandle source, const glm::vec3& velocity) {
    const auto found = m_impl->sources.find(source);
    if (found == m_impl->sources.end()) return false;
    ma_sound_set_velocity(&found->second->sound, velocity.x, velocity.y, velocity.z);
    return true;
}

bool AudioEngine::SetSourceDirection(SourceHandle source, const glm::vec3& direction) {
    const auto found = m_impl->sources.find(source);
    if (found == m_impl->sources.end()) return false;
    const glm::vec3 safe = glm::length(direction) > 0.0001f
        ? glm::normalize(direction) : glm::vec3(0.0f, 0.0f, -1.0f);
    ma_sound_set_direction(&found->second->sound, safe.x, safe.y, safe.z);
    return true;
}

bool AudioEngine::SetSourceCone(SourceHandle source, float innerAngleDegrees,
                                float outerAngleDegrees, float outerGain) {
    const auto found = m_impl->sources.find(source);
    if (found == m_impl->sources.end()) return false;
    constexpr float radians = 0.01745329251994329577f;
    const float inner = std::clamp(innerAngleDegrees, 0.0f, 360.0f);
    const float outer = std::clamp(outerAngleDegrees, inner, 360.0f);
    ma_sound_set_cone(&found->second->sound, inner * radians, outer * radians,
                      std::clamp(outerGain, 0.0f, 1.0f));
    return true;
}

bool AudioEngine::SetSourceDoppler(SourceHandle source, float factor) {
    const auto found = m_impl->sources.find(source);
    if (found == m_impl->sources.end()) return false;
    ma_sound_set_doppler_factor(&found->second->sound, std::max(factor, 0.0f));
    return true;
}

bool AudioEngine::SetSourceOcclusion(SourceHandle source, float amount) {
    const auto found = m_impl->sources.find(source);
    if (found == m_impl->sources.end()) return false;
    found->second->occlusion = std::clamp(amount, 0.0f, 1.0f);
    ma_sound_set_volume(&found->second->sound,
        found->second->baseVolume * (1.0f - found->second->occlusion * 0.75f));
    return true;
}

bool AudioEngine::SetSourcePriority(SourceHandle source, int priority) {
    const auto found = m_impl->sources.find(source);
    if (found == m_impl->sources.end()) return false;
    found->second->priority = std::clamp(priority, 0, 100);
    return true;
}

bool AudioEngine::SetSourceSpatial(SourceHandle source, bool spatial) {
    const auto found = m_impl->sources.find(source);
    if (found == m_impl->sources.end()) return false;
    ma_sound_set_spatialization_enabled(&found->second->sound, spatial ? MA_TRUE : MA_FALSE);
    return true;
}

bool AudioEngine::SetSourceLooping(SourceHandle source, bool looping) {
    const auto found = m_impl->sources.find(source);
    if (found == m_impl->sources.end()) return false;
    ma_sound_set_looping(&found->second->sound, looping ? MA_TRUE : MA_FALSE);
    return true;
}

bool AudioEngine::SetSourceVolumePitch(SourceHandle source, float volume, float pitch) {
    const auto found = m_impl->sources.find(source);
    if (found == m_impl->sources.end()) return false;
    found->second->baseVolume = std::max(volume, 0.0f);
    ma_sound_set_volume(&found->second->sound,
        found->second->baseVolume * (1.0f - found->second->occlusion * 0.75f));
    ma_sound_set_pitch(&found->second->sound, std::max(pitch, 0.01f));
    return true;
}

bool AudioEngine::SetSourceAttenuation(SourceHandle source, float minDistance,
                                       float maxDistance, float rolloff) {
    const auto found = m_impl->sources.find(source);
    if (found == m_impl->sources.end()) return false;
    const float minimum = std::max(minDistance, 0.01f);
    ma_sound_set_attenuation_model(&found->second->sound, ma_attenuation_model_inverse);
    ma_sound_set_min_distance(&found->second->sound, minimum);
    ma_sound_set_max_distance(&found->second->sound, std::max(maxDistance, minimum));
    ma_sound_set_rolloff(&found->second->sound, std::max(rolloff, 0.0f));
    return true;
}

bool AudioEngine::IsSourcePlaying(SourceHandle source) const {
    const auto found = m_impl->sources.find(source);
    return found != m_impl->sources.end() && ma_sound_is_playing(&found->second->sound) == MA_TRUE;
}

bool AudioEngine::IsSourcePaused(SourceHandle source) const {
    const auto found = m_impl->sources.find(source);
    return found != m_impl->sources.end() && found->second->state == Impl::SourceState::Paused;
}

float AudioEngine::SourceCursorSeconds(SourceHandle source) const {
    const auto found = m_impl->sources.find(source);
    if (found == m_impl->sources.end()) return 0.0f;
    float seconds = 0.0f;
    ma_sound_get_cursor_in_seconds(&found->second->sound, &seconds);
    return seconds;
}

void AudioEngine::SetMaxVoices(std::size_t voices) {
    m_impl->maxVoices = std::max<std::size_t>(voices, 1);
}

std::size_t AudioEngine::MaxVoices() const {
    return m_impl->maxVoices;
}

AudioEngine::DebugStats AudioEngine::GetDebugStats() const {
    DebugStats stats;
    stats.activeVoices = m_impl->ActiveVoiceCount();
    stats.managedSources = m_impl->sources.size();
    stats.pooledAssets = m_impl->pools.size();
    stats.stolenVoices = m_impl->stolenVoices;
    stats.rejectedVoices = m_impl->rejectedVoices;
    if (m_impl->musicLoaded) {
        ++stats.voicesPerBus[static_cast<std::size_t>(AudioBus::Music)];
        ++stats.streamedVoices;
    }
    for (const auto& entry : m_impl->sources) {
        if (ma_sound_is_playing(&entry.second->sound) != MA_TRUE) continue;
        ++stats.voicesPerBus[static_cast<std::size_t>(entry.second->bus)];
        if (entry.second->streamed) ++stats.streamedVoices;
    }
    for (const auto& entry : m_impl->pools) {
        for (const ma_sound& voice : entry.second->voices)
            if (ma_sound_is_playing(&voice) == MA_TRUE)
                ++stats.voicesPerBus[static_cast<std::size_t>(entry.second->bus)];
    }
    return stats;
}

AudioEngine::DeviceInfo AudioEngine::GetDeviceInfo() const {
    DeviceInfo info;
    info.available = m_ok;
    info.backend = m_ok ? "miniaudio default output" : "unavailable";
    if (m_ok) {
        info.sampleRate = ma_engine_get_sample_rate(&m_impl->engine);
        info.channels = ma_engine_get_channels(&m_impl->engine);
    }
    return info;
}

void AudioEngine::ApplyMixerPreset(const AudioMixerPreset& preset) {
    const std::size_t count = static_cast<std::size_t>(AudioBus::Count);
    for (std::size_t i = 0; i < count; ++i) {
        const AudioBus bus = static_cast<AudioBus>(i);
        if (i < preset.volumes.size()) SetBusVolume(bus, preset.volumes[i]);
        if (i < preset.muted.size()) SetBusMuted(bus, preset.muted[i]);
        if (i < preset.effects.size()) SetBusEffects(bus, preset.effects[i]);
    }
    SetDialogueDucking(preset.dialogueDucking);
}

AudioMixerPreset AudioEngine::CaptureMixerPreset(const std::string& name) const {
    AudioMixerPreset preset;
    preset.name = name;
    preset.dialogueDucking = DialogueDuckingEnabled();
    const std::size_t count = static_cast<std::size_t>(AudioBus::Count);
    preset.volumes.reserve(count);
    preset.muted.reserve(count);
    preset.effects.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const AudioBus bus = static_cast<AudioBus>(i);
        preset.volumes.push_back(BusVolume(bus));
        preset.muted.push_back(IsBusMuted(bus));
        preset.effects.push_back(BusEffects(bus));
    }
    return preset;
}

bool AudioEngine::LoadAdaptiveMusicAsset(const std::string& path, std::string* error) {
    AdaptiveMusicAsset asset;
    if (!LoadAdaptiveMusic(path, &asset, error)) return false;
    const std::filesystem::path parent = std::filesystem::path(path).parent_path();
    for (AdaptiveMusicState& state : asset.states) {
        for (std::string& stem : state.stems) {
            const std::filesystem::path source(stem);
            if (source.is_relative()) stem = (parent / source).lexically_normal().string();
        }
    }
    m_impl->adaptiveMusic = std::move(asset);
    m_impl->adaptiveMusicPath = path;
    m_impl->musicState.clear();
    return true;
}

bool AudioEngine::SetMusicState(const std::string& stateName, bool synchronizeToBeat) {
    const auto found = std::find_if(m_impl->adaptiveMusic.states.begin(),
        m_impl->adaptiveMusic.states.end(), [&stateName](const AdaptiveMusicState& state) {
            return state.name == stateName;
        });
    if (found == m_impl->adaptiveMusic.states.end()) return false;
    if (m_impl->musicState == stateName) return true;
    float transition = found->crossfadeSeconds;
    if (synchronizeToBeat && found->bpm > 0.0f) {
        const float beat = 60.0f / found->bpm;
        transition = std::max(beat, std::round(transition / beat) * beat);
    }
    for (Impl::MusicStem& stem : m_impl->musicStems) {
        stem.from = stem.to;
        stem.to = 0.0f;
        stem.time = 0.0f;
        stem.duration = transition;
        stem.destroyWhenSilent = true;
    }
    for (const std::string& path : found->stems) {
        const SourceHandle handle = CreateSource(path, false, true, true, AudioBus::Music);
        if (handle == InvalidSource) continue;
        SetSourceVolumePitch(handle, 0.0f, 1.0f);
        PlaySource(handle, true);
        m_impl->musicStems.push_back({handle, 0.0f, found->volume, 0.0f,
                                      transition, false});
    }
    m_impl->musicState = stateName;
    return !found->stems.empty();
}

const std::string& AudioEngine::MusicState() const {
    return m_impl->musicState;
}

} // namespace engine
