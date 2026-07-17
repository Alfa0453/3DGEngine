#pragma once

#include "engine/audio/AudioEngine.h"
#include "engine/ecs/Entity.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace engine {
struct CollisionEvent;
class PhysicsWorld;
namespace ecs {
class Registry;
}

// Connects serializable ecs::AudioSource components to live AudioEngine voices.
// A game calls Update after gameplay/physics movement each frame and Stop before
// replacing its registry. Newly observed autoplay sources begin automatically.
class RuntimeAudioSystem {
public:
    explicit RuntimeAudioSystem(AudioEngine& audio);
    ~RuntimeAudioSystem();

    RuntimeAudioSystem(const RuntimeAudioSystem&) = delete;
    RuntimeAudioSystem& operator=(const RuntimeAudioSystem&) = delete;

    void Update(ecs::Registry& registry, float dt = 0.0f);
    void Stop();

    AudioEngine::SourceHandle SourceFor(ecs::Entity entity) const;
    bool Play(ecs::Entity entity, bool restart = false);
    bool Pause(ecs::Entity entity);
    bool Resume(ecs::Entity entity);
    bool Stop(ecs::Entity entity);
    bool Seek(ecs::Entity entity, float seconds);
    bool IsPlaying(ecs::Entity entity) const;
    bool IsPaused(ecs::Entity entity) const;
    float CursorSeconds(ecs::Entity entity) const;
    void ApplySnapshot(AudioSnapshotPreset preset, float transitionSeconds = 0.25f);
    void SetDialogueDucking(bool enabled);
    bool PlayCue(const std::string& path, const glm::vec3& position,
                 bool force2D = false);
    bool LoadAdaptiveMusic(const std::string& path, std::string* error = nullptr);
    bool SetMusicState(const std::string& stateName, bool synchronizeToBeat = true);
    void ProcessCollisionEvents(ecs::Registry& registry,
                                const std::vector<CollisionEvent>& events);
    bool ProcessAnimationEvent(ecs::Registry& registry, ecs::Entity emitter,
                               const std::string& eventName);
    void UpdateOcclusion(ecs::Registry& registry, const PhysicsWorld& physics,
                         const glm::vec3& listenerPosition);

private:
    struct Voice {
        AudioEngine::SourceHandle handle = AudioEngine::InvalidSource;
        std::string path;
        AudioBus bus = AudioBus::SFX;
        glm::vec3 position{0.0f};
        bool cue = false;
        bool force2D = false;
    };

    AudioEngine* m_audio = nullptr;
    std::unordered_map<ecs::Entity, Voice> m_voices;

    ecs::Entity FindNamedEntity(ecs::Registry& registry, const std::string& name) const;
    bool ApplyAction(ecs::Registry& registry, ecs::Entity target, int action);
};

} // namespace engine
