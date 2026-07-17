#pragma once

#include "engine/ecs/Components.h"
#include "engine/ecs/Entity.h"
#include "engine/ecs/Registry.h"
#include "engine/physics/PhysicsWorld.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <utility>
#include <vector>

namespace engine {
    
namespace ecs { class Registry; }
class RuntimeAudioSystem;
class CameraShake;
class CameraDirector;

struct ScriptAnimationEvent {
    ecs::Entity entity = ecs::kNull;
    std::string name;
};

struct ScriptContext {
    ecs::Registry* registry = nullptr;
    ecs::Entity entity = ecs::kNull;
    std::vector<ecs::Entity>* destroyQueue = nullptr;
    const struct ScriptInputState* input = nullptr;
    RuntimeAudioSystem* audio = nullptr;
    CameraShake* cameraShake = nullptr;
    CameraDirector* cameraDirector = nullptr;
};

struct ScriptInputState {
    bool enabled = false;
    std::unordered_set<int> keysDown;
    std::unordered_set<int> keysPressed;
    std::unordered_set<int> mouseButtonsDown;
    std::unordered_set<int> mouseButtonsPressed;
    const std::vector<CollisionEvent>* physicsEvents = nullptr;
    const std::vector<ScriptAnimationEvent>* animationEvents = nullptr;
    float mouseDeltaX = 0.0f;
    float mouseDeltaY = 0.0f;
};

struct ScriptField {
    enum class Type {
        Float = 0,
        Int = 1,
        Bool = 2,
        String = 3
    };

    std::string name;
    Type type = Type::Float;
    std::string value = "0";
};

class Script {
public:
    virtual ~Script() = default;
    virtual void OnCreate() {}                          // once, when the script starts
    virtual void OnUpdate(float dt) { (void)dt; }       // once per rendered frame (variable dt)
    virtual void OnFixedUpdate(float dt) { (void)dt; }  // per physics step (fixed dt)
    virtual void OnDestroy() {}                          // once, before the entity/script is destroyed
    // Runtime-system hook. Games normally use SetTimer/Delay below instead.
    void TickTimers(float dt);

    void SetContext(ScriptContext context) { m_context = context; }

protected:
    ScriptContext& Context() { return m_context; }
    const ScriptContext& Context() const { return m_context; }

    ecs::Entity Self() const { return m_context.entity; }
    ecs::Registry* Registry() const { return m_context.registry; }
    ecs::Transform* Transform();
    const ecs::Transform* Transform() const;
    ecs::Entity FindObject(const std::string& name) const;
    ecs::Transform* FindTransform(const std::string& name);
    void DestroySelf();
    void Destroy(ecs::Entity entity);
    ecs::Entity SpawnEmpty(const std::string& name, const glm::vec3& position = glm::vec3(0.0f));
    ecs::Entity SpawnFromObject(const std::string& prototypeName,
                                const glm::vec3& position);
    void RequestSceneLoad(const std::string& runtimeScenePath);
    bool SaveValue(const std::string& key, const std::string& value);
    std::string LoadValue(const std::string& key,
                          const std::string& fallback = {}) const;
    bool SaveCheckpoint(const std::string& name, const glm::vec3& position);
    bool LoadCheckpoint(const std::string& name, glm::vec3* position) const;
    int SetTimer(float seconds, std::function<void()> callback, bool repeat = false);
    int Delay(float seconds, std::function<void()> callback) {
        return SetTimer(seconds, std::move(callback), false);
    }
    void ClearTimer(int timerId);
    bool IsKeyDown(int key) const;
    bool WasKeyPressed(int key) const;
    bool IsMouseButtonDown(int button) const;
    bool WasMouseButtonPressed(int button) const;
    float MouseDeltaX() const;
    float MouseDeltaY() const;
    bool IsTriggerTouching(ecs::Entity entity) const;
    bool WasTriggerEntered(ecs::Entity entity) const;
    bool WasTriggerExited(ecs::Entity entity) const;
    bool WasAnimationEvent(const std::string& name) const;
    bool WasAnimationEvent(ecs::Entity entity, const std::string& name) const;
    bool PlayAnimationAction(int clipIndex,
                             float fadeIn = 0.1f,
                             float fadeOut = 0.2f,
                             float speed = 1.0f);
    bool PlayAnimationAction(const std::string& clipName,
                             float fadeIn = 0.1f,
                             float fadeOut = 0.2f,
                             float speed = 1.0f);
    bool PlayMaskedAnimationAction(int clipIndex,
                                   const std::string& rootBone,
                                   float fadeIn = 0.1f,
                                   float fadeOut = 0.2f,
                                   float speed = 1.0f);
    bool PlayMaskedAnimationAction(const std::string& clipName,
                                   const std::string& rootBone,
                                   float fadeIn = 0.1f,
                                   float fadeOut = 0.2f,
                                   float speed = 1.0f);
    bool PlayAnimationProfile(const std::string& profileName);
    bool SetAnimationParameter(const std::string& name, float value);
    bool SetAnimationBool(const std::string& name, bool value);
    bool SetAnimationTrigger(const std::string& name);
    float GetAnimationParameter(const std::string& name, float fallback = 0.0f) const;
    bool GetAnimationBool(const std::string& name, bool fallback = false) const;
    bool IsAnimationActionPlaying() const;
    bool IsAnimationMovementLocked() const;
    bool PlayAudio(bool restart = false);
    bool PlayAudio(ecs::Entity entity, bool restart = false);
    bool PauseAudio();
    bool PauseAudio(ecs::Entity entity);
    bool ResumeAudio();
    bool ResumeAudio(ecs::Entity entity);
    bool StopAudio();
    bool StopAudio(ecs::Entity entity);
    bool SeekAudio(float seconds);
    bool SeekAudio(ecs::Entity entity, float seconds);
    bool IsAudioPlaying() const;
    bool IsAudioPlaying(ecs::Entity entity) const;
    bool IsAudioPaused() const;
    bool IsAudioPaused(ecs::Entity entity) const;
    float AudioCursorSeconds() const;
    float AudioCursorSeconds(ecs::Entity entity) const;
    bool SetAudioVolume(float volume);
    bool SetAudioVolume(ecs::Entity entity, float volume);
    bool SetAudioPitch(float pitch);
    bool SetAudioPitch(ecs::Entity entity, float pitch);
    bool SetAudioLooping(bool looping);
    bool SetAudioLooping(ecs::Entity entity, bool looping);
    bool SetAudioSpatial(bool spatial);
    bool SetAudioSpatial(ecs::Entity entity, bool spatial);
    bool SetAudioBus(AudioBus bus);
    bool SetAudioBus(ecs::Entity entity, AudioBus bus);
    bool ApplyAudioSnapshot(AudioSnapshotPreset preset, float transitionSeconds = 0.25f);
    bool SetDialogueDucking(bool enabled);
    bool PlayAudioCue(const std::string& path, bool spatial = true);
    bool LoadAdaptiveMusic(const std::string& path);
    bool SetMusicState(const std::string& stateName, bool synchronizeToBeat = true);
    bool SetAudioAttenuation(float minDistance, float maxDistance, float rolloff);
    bool SetAudioAttenuation(ecs::Entity entity, float minDistance,
                             float maxDistance, float rolloff);
    bool SetAudioDoppler(float factor);
    bool SetAudioDoppler(ecs::Entity entity, float factor);
    bool SetAudioCone(float innerDegrees, float outerDegrees, float outerGain);
    bool SetAudioCone(ecs::Entity entity, float innerDegrees,
                      float outerDegrees, float outerGain);
    bool SetAudioOcclusion(float amount);
    bool SetAudioOcclusion(ecs::Entity entity, float amount);
    bool SetAudioPriority(int priority);
    bool SetAudioPriority(ecs::Entity entity, int priority);
    bool PlayParticles(bool restart = false);
    bool PlayParticles(ecs::Entity entity, bool restart = false);
    bool StopParticles(bool clear = false);
    bool StopParticles(ecs::Entity entity, bool clear = false);
    bool RestartParticles();
    bool RestartParticles(ecs::Entity entity);
    bool BurstParticles(int count = 0);
    bool BurstParticles(ecs::Entity entity, int count = 0);
    bool ClearParticles();
    bool ClearParticles(ecs::Entity entity);
    bool SetParticlesEnabled(bool enabled);
    bool SetParticlesEnabled(ecs::Entity entity, bool enabled);
    bool SetParticleRate(float particlesPerSecond);
    bool SetParticleRate(ecs::Entity entity, float particlesPerSecond);
    bool SetParticleSpeed(float simulationSpeed);
    bool SetParticleSpeed(ecs::Entity entity, float simulationSpeed);
    bool AreParticlesPlaying() const;
    bool AreParticlesPlaying(ecs::Entity entity) const;
    int ParticleCount() const;
    int ParticleCount(ecs::Entity entity) const;
    bool ShakeCamera(float intensity = 1.0f, float duration = 0.35f,
                     float frequency = 18.0f);
    bool ShakeCameraAdvanced(float translationAmplitude, float rotationDegrees,
                             float duration = 0.35f, float frequency = 18.0f,
                             float fovAmplitude = 0.0f);
    bool PlayCameraSequence(const std::string& name, bool lockInput = true,
                            bool skippable = true);
    bool StopCameraSequence();
    bool SkipCameraSequence();
    bool IsCameraSequencePlaying(const std::string& name = {}) const;
    bool WasCameraSequenceFinished(const std::string& name) const;
    bool WasCameraSequenceSkipped(const std::string& name) const;
    bool WasCameraSequenceEvent(const std::string& sequenceName,
                                const std::string& eventName) const;
    std::string GetFieldString(const std::string& name, const std::string& fallback = {}) const;
    float GetFieldFloat(const std::string& name, float fallback = 0.0f) const;
    int GetFieldInt(const std::string& name, int fallback = 0) const;
    bool GetFieldBool(const std::string& name, bool fallback = false) const;

    template <class T>
    T* TryGet() {
        return m_context.registry ? m_context.registry->TryGet<T>(m_context.entity) : nullptr;
    }

    template <class T>
    const T* TryGet() const {
        return m_context.registry ? m_context.registry->TryGet<T>(m_context.entity) : nullptr;
    }

    template <class T>
    T* TryGet(ecs::Entity entity) {
        return m_context.registry ? m_context.registry->TryGet<T>(entity) : nullptr;
    }

    template <class T>
    const T* TryGet(ecs::Entity entity) const {
        return m_context.registry ? m_context.registry->TryGet<T>(entity) : nullptr;
    }

    template <class T>
    bool Has() const {
        return m_context.registry && m_context.registry->Has<T>(m_context.entity);
    }

    template <class T>
    bool Has(ecs::Entity entity) const {
        return m_context.registry && m_context.registry->Has<T>(entity);
    }

    template <class T>
    T& Add(T value = T{}) {
        return m_context.registry->Add<T>(m_context.entity, std::move(value));
    }

    template <class T>
    void Remove() {
        if (m_context.registry) {
            m_context.registry->Remove<T>(m_context.entity);
        }
    }

private:
    struct Timer {
        int id = 0;
        float remaining = 0.0f;
        float interval = 0.0f;
        bool repeat = false;
        bool cancelled = false;
        std::function<void()> callback;
    };
    ScriptContext m_context;
    std::vector<Timer> m_timers;
    int m_nextTimerId = 1;
};

struct NativeScriptComponent {
    bool enabled = true;
    bool created = false;
    bool missingFactory = false;
    std::string className;
    std::string sourcePath;
    std::vector<ScriptField> fields;
    std::unique_ptr<Script> instance;
};

class ScriptRegistry {
public:
    using Factory = std::function<std::unique_ptr<Script>()>;

    static ScriptRegistry& Instance();

    void Register(const std::string& className, Factory factory);
    bool Has(const std::string& className) const;
    std::unique_ptr<Script> Create(const std::string& className) const;

private:
    std::unordered_map<std::string, Factory> m_factories;
};

// Per-frame update: creates instances, calls OnCreate once, then OnUpdate(dt).
void UpdateScripts(ecs::Registry& registry, float dt, const ScriptInputState* input = nullptr,
                   RuntimeAudioSystem* audio = nullptr, CameraShake* cameraShake = nullptr,
                   CameraDirector* cameraDirector = nullptr);

// Per-physics-step update: calls OnFixedUpdate(dt) on already-created scripts. Call
// this from the fixed-timestep loop; instance creation stays in UpdateScripts.
void FixedUpdateScripts(ecs::Registry& registry, float dt, const ScriptInputState* input = nullptr,
                        RuntimeAudioSystem* audio = nullptr, CameraShake* cameraShake = nullptr,
                        CameraDirector* cameraDirector = nullptr);

// Calls OnDestroy() on every live script and releases the instances. Call when
// leaving Play mode or unloading the scene so scripts can clean up.
void ShutdownScripts(ecs::Registry& registry);

// Scene requests are queued by scripts and consumed by the runtime host after the
// current script update, avoiding registry destruction from inside a callback.
std::string ConsumeScriptSceneLoadRequest();

} // namespace engine
