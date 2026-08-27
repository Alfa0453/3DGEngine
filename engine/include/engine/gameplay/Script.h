#pragma once

#include "engine/ecs/Components.h"
#include "engine/ecs/Entity.h"
#include "engine/ecs/Registry.h"
#include "engine/physics/PhysicsWorld.h"

#include <functional>
#include <algorithm>
#include <cmath>
#include <cstdint>
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
class GameMode;
class ScriptInputApi;
class ScriptCameraApi;
class ScriptParticlesApi;
class ScriptAudioApi;
class ScriptAnimApi;
class LuaScript;

struct ScriptAnimationEvent {
    ecs::Entity entity = ecs::kNull;
    std::string name;
};

// A typed, forwards-compatible payload for queued gameplay messages.
struct ScriptEvent {
    std::string name;
    ecs::Entity sender = ecs::kNull;
    ecs::Entity target = ecs::kNull; // kNull broadcasts to every listening script
    std::unordered_map<std::string, bool> bools;
    std::unordered_map<std::string, int> ints;
    std::unordered_map<std::string, float> floats;
    std::unordered_map<std::string, std::string> strings;
    std::unordered_map<std::string, glm::vec3> vectors;
    std::unordered_map<std::string, ecs::Entity> entities;

    bool GetBool(const std::string& key, bool fallback = false) const;
    int GetInt(const std::string& key, int fallback = 0) const;
    float GetFloat(const std::string& key, float fallback = 0.0f) const;
    std::string GetString(const std::string& key,
                          const std::string& fallback = {}) const;
    glm::vec3 GetVector(const std::string& key,
                        const glm::vec3& fallback = glm::vec3(0.0f)) const;
    ecs::Entity GetEntity(const std::string& key,
                          ecs::Entity fallback = ecs::kNull) const;
};

// Stable script reference: no DLL object pointer is retained. Every operation
// resolves entity + class against the current registry, so hot reload and scene
// streaming cannot leave a dangling Script pointer in gameplay code.
struct ScriptHandle {
    ecs::Entity entity = ecs::kNull;
    std::string className;
    explicit operator bool() const {
        return entity != ecs::kNull && !className.empty();
    }
};

struct ScriptContext {
    ecs::Registry* registry = nullptr;
    ecs::Entity entity = ecs::kNull;
    std::vector<ecs::Entity>* destroyQueue = nullptr;
    const struct ScriptInputState* input = nullptr;
    RuntimeAudioSystem* audio = nullptr;
    CameraShake* cameraShake = nullptr;
    CameraDirector* cameraDirector = nullptr;
    const std::vector<struct ScriptField>* fields = nullptr;
    GameMode* gameMode = nullptr;              // host-owned; scripts reach it via Game()
    std::string* sceneLoadRequest = nullptr;   // where RequestSceneLoad writes when the host sets it
    PhysicsWorld* physics = nullptr;           // optional runtime query/debug world
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
        String = 3,
        Vec3 = 4,     // "x y z"      -> GetFieldVec3
        Color = 5,    // "r g b"      -> GetFieldColor
        Entity = 6,   // object name  -> GetFieldEntity
        Asset = 7     // asset path   -> GetFieldAsset
    };

    std::string name;
    Type type = Type::Float;
    std::string value = "0";
    // Editor-only inspector metadata (ignored by the runtime): a Float/Int slider range
    // (min >= max means no slider, plain drag/input) and a hover tooltip.
    float minValue = 0.0f;
    float maxValue = 0.0f;
    std::string tooltip;
    std::string group;   // editor-only: inspector collapsible-section name ("" = ungrouped)
};

// A lightweight scripted sequence: chain steps that run top-to-bottom across frames
// instead of nesting timer callbacks. Build one with Script::Sequence():
//   Sequence().Do([&]{ OpenDoor(); }).Wait(2.0f).Do([&]{ CloseDoor(); });
//   Sequence().WaitUntil([&]{ return PlayerNear(); }).Do([&]{ Trigger(); });
class ScriptSequence {
public:
    explicit ScriptSequence(int handle = 0) : m_handle(handle) {}

    int Handle() const { return m_handle; }
    ScriptSequence& Do(std::function<void()> action) {
        m_steps.push_back(Step{Step::Kind::Action, std::move(action), 0.0f, {}});
        return *this;
    }
    ScriptSequence& Wait(float seconds) {
        m_steps.push_back(Step{Step::Kind::WaitTime, {}, std::max(seconds, 0.0f), {}});
        return *this;
    }
    ScriptSequence& WaitUntil(std::function<bool()> condition) {
        m_steps.push_back(Step{Step::Kind::WaitUntil, {}, 0.0f, std::move(condition)});
        return *this;
    }
    // Advance by dt; driven by the owning Script every update. Actions run and advance
    // immediately; waits hold until their time elapses or their condition is true.
    void Tick(float dt) {
        if (m_paused || m_cancelled) return;
        float remainingDt = std::max(dt, 0.0f);
        while (m_current < m_steps.size()) {
            Step& step = m_steps[m_current];
            if (step.kind == Step::Kind::Action) {
                if (step.action) step.action();
                ++m_current;
                if (m_paused || m_cancelled) return;
                continue;
            }
            if (step.kind == Step::Kind::WaitTime) {
                const float timeLeft = std::max(step.seconds - m_timer, 0.0f);
                if (remainingDt < timeLeft) {
                    m_timer += remainingDt;
                    return;
                }
                remainingDt -= timeLeft;
                m_timer = 0.0f;
                ++m_current;
                continue;
            }
            if (step.condition && !step.condition()) return;   // WaitUntil
            ++m_current;
        }
    }
    void Pause() { if (!Done()) m_paused = true; }
    void Resume() { if (!m_cancelled) m_paused = false; }
    void Cancel() { m_cancelled = true; m_paused = false; }
    bool Paused() const { return m_paused && !m_cancelled; }
    bool Cancelled() const { return m_cancelled; }
    bool Done() const { return m_cancelled || m_current >= m_steps.size(); }
    bool Running() const { return !Done() && !m_paused; }

private:
    struct Step {
        enum class Kind { Action, WaitTime, WaitUntil };
        Kind kind = Kind::Action;
        std::function<void()> action;
        float seconds = 0.0f;
        std::function<bool()> condition;
    };
    std::vector<Step> m_steps;
    int m_handle = 0;
    std::size_t m_current = 0;
    float m_timer = 0.0f;
    bool m_paused = false;
    bool m_cancelled = false;
};

class ScriptTestContext {
public:
    void Expect(bool condition, std::string message) {
        ++m_assertions;
        if (!condition) m_failures.push_back(std::move(message));
    }
    void ExpectNear(float actual, float expected, float tolerance,
                    std::string message) {
        Expect(std::abs(actual - expected) <= std::max(tolerance, 0.0f),
               std::move(message));
    }
    int AssertionCount() const { return m_assertions; }
    bool Passed() const { return m_failures.empty(); }
    const std::vector<std::string>& Failures() const { return m_failures; }
private:
    int m_assertions = 0;
    std::vector<std::string> m_failures;
};

class ScriptTestSuite {
public:
    using TestFunction = std::function<void(ScriptTestContext&)>;
    struct TestCase {
        std::string name;
        TestFunction function;
    };
    void Add(std::string name, TestFunction function) {
        if (!name.empty() && function)
            m_tests.push_back({std::move(name), std::move(function)});
    }
    const std::vector<TestCase>& Tests() const { return m_tests; }
private:
    std::vector<TestCase> m_tests;
};

struct ScriptTestResult {
    std::string suite;
    std::string name;
    bool passed = false;
    int assertions = 0;
    double milliseconds = 0.0;
    std::vector<std::string> failures;
};

class Script {
public:
    using StateMap = std::unordered_map<std::string, std::string>;
    using ReloadState = StateMap;
    using CallableFunction =
        std::function<bool(const ScriptEvent& arguments, ScriptEvent& result)>;

    virtual ~Script() = default;
    virtual void OnCreate() {}                          // once, when the script starts
    virtual void OnEnable() {}                          // after create and each reactivation
    virtual void OnDisable() {}                         // before deactivation or destruction
    virtual void OnUpdate(float dt) { (void)dt; }       // once per rendered frame (variable dt)
    virtual void OnFixedUpdate(float dt) { (void)dt; }  // per physics step (fixed dt)
    virtual void OnEvent(const ScriptEvent& event) { (void)event; }
    virtual bool OnScriptCall(const std::string& functionName,
                              const ScriptEvent& arguments,
                              ScriptEvent& result) {
        (void)functionName; (void)arguments; (void)result; return false;
    }
    virtual void OnDestroy() {}                          // once, before the entity/script is destroyed
    // Versioned save-slot state. Named entities automatically capture this map
    // into full game saves and restore it after OnCreate. Increment the version
    // when the schema changes; OnLoadState receives the version stored in the save.
    // Return 0 (the default) to opt out of per-script persistence.
    virtual int PersistentStateVersion() const { return 0; }
    virtual void OnSaveState(StateMap& state) const { (void)state; }
    virtual void OnLoadState(int savedVersion, const StateMap& state) {
        (void)savedVersion; (void)state;
    }
    virtual void DefineTests(ScriptTestSuite& suite) { (void)suite; }
    // Optional hot-reload bridge for transient values that are not exposed ScriptFields.
    // Store compact, version-tolerant strings here; the replacement instance receives them
    // after OnCreate. Authored/exposed fields are preserved by the engine automatically.
    virtual void OnBeforeHotReload(ReloadState& state) const { (void)state; }
    virtual void OnAfterHotReload(const ReloadState& state) { (void)state; }
    // Runtime-system hook. Games normally use SetTimer/Delay below instead.
    void TickTimers(float dt);

    void SetContext(ScriptContext context) { m_context = context; }

    friend class ScriptInputApi;
    friend class ScriptCameraApi;
    friend class ScriptParticlesApi;
    friend class ScriptAudioApi;
    friend class ScriptAnimApi;
    friend class ScriptEventDispatcher;
    friend class ScriptCallDispatcher;
    friend class LuaScript;

protected:
    ScriptContext& Context() { return m_context; }
    const ScriptContext& Context() const { return m_context; }

    // Grouped, discoverable sugar over the flat helpers below (fully backward-compatible;
    // the flat methods stay): Input().KeyDown(k), Camera().Shake(), Particles().Burst().
    ScriptInputApi Input();
    ScriptCameraApi Camera();
    ScriptParticlesApi Particles();
    ScriptAudioApi Audio();
    ScriptAnimApi Anim();

    ecs::Entity Self() const { return m_context.entity; }
    ecs::Registry* Registry() const { return m_context.registry; }
    GameMode* Game() const { return m_context.gameMode; }   // scene score/state (host-owned)
    ecs::Transform* Transform();
    const ecs::Transform* Transform() const;
    ecs::Entity FindObject(const std::string& name) const;
    ecs::Transform* FindTransform(const std::string& name);
    bool SocketTransform(const std::string& name, glm::mat4* world) const;
    bool SocketPosition(const std::string& name, glm::vec3* position) const;
    bool ActivateRagdoll();
    bool RecoverFromRagdoll();
    bool GrantAbility(const std::string& assetPath);
    bool ActivateAbility(const std::string& abilityName,
                         ecs::Entity target = ecs::kNull);
    bool CancelAbility();
    bool IsAbilityActive(const std::string& abilityName = {}) const;
    float AbilityCooldown(const std::string& abilityName) const;
    bool SetAbilityResources(float mana, float stamina);
    bool WasAbilityEvent(const std::string& eventName);
    bool ConfigureDestructible(const std::string& assetPath);
    bool DamageDestructible(float damage,
                            const glm::vec3& hitPoint = glm::vec3(0.0f),
                            const glm::vec3& impulse = glm::vec3(0.0f));
    bool ImpactDestructible(float impact,const glm::vec3& hitPoint,
                            const glm::vec3& direction);
    float DestructibleHealth() const;
    bool IsDestructibleBroken() const;
    bool WasDestructionEvent(const std::string& eventName);
    int SplinePointCount(ecs::Entity spline) const;
    bool IsSplineClosed(ecs::Entity spline) const;
    bool SetSplineClosed(ecs::Entity spline, bool closed);
    glm::vec3 GetSplinePoint(ecs::Entity spline, int index,
                             const glm::vec3& fallback = glm::vec3(0.0f)) const;
    bool SetSplinePoint(ecs::Entity spline, int index, const glm::vec3& point);
    glm::vec3 GetSplinePointRotation(ecs::Entity spline, int index,
                                     const glm::vec3& fallback = glm::vec3(0.0f)) const;
    bool SetSplinePointRotation(ecs::Entity spline, int index,
                                const glm::vec3& degrees);
    int AddSplinePoint(ecs::Entity spline, const glm::vec3& point,
                       const glm::vec3& rotationDegrees = glm::vec3(0.0f));
    bool InsertSplinePoint(ecs::Entity spline, int index, const glm::vec3& point,
                           const glm::vec3& rotationDegrees = glm::vec3(0.0f));
    bool RemoveSplinePoint(ecs::Entity spline, int index);
    bool TranslateSpline(ecs::Entity spline, const glm::vec3& delta);
    float SplineLength(ecs::Entity spline) const;
    glm::vec3 SplinePositionAt(ecs::Entity spline, float normalizedDistance,
                               const glm::vec3& fallback = glm::vec3(0.0f)) const;
    glm::vec3 SplineTangentAt(ecs::Entity spline, float normalizedDistance,
                              const glm::vec3& fallback = glm::vec3(0.0f, 0.0f, 1.0f)) const;
    glm::vec3 SplineClosestPoint(ecs::Entity spline, const glm::vec3& world,
                                 const glm::vec3& fallback = glm::vec3(0.0f)) const;
    float SplineClosestDistance(ecs::Entity spline, const glm::vec3& world,
                                float fallback = 0.0f) const;
    void DestroySelf();
    void Destroy(ecs::Entity entity);
    ecs::Entity SpawnEmpty(const std::string& name, const glm::vec3& position = glm::vec3(0.0f));
    ecs::Entity SpawnFromObject(const std::string& prototypeName,
                                const glm::vec3& position);
    // Evaluates an engine-owned .3dgscatter asset on a flat runtime plane and
    // creates model entities. Returns the number generated. Terrain-aware
    // authored results should be baked in the editor.
    int GenerateScatterGraph(const std::string& assetPath,
                             const glm::vec3& worldOffset = glm::vec3(0.0f),
                             std::uint32_t seedOverride = 0);
    // Evaluates an engine-owned .3dgbiome population on a flat runtime plane.
    // Editor application is terrain-aware and also applies materials/environment.
    int GenerateBiome(const std::string& assetPath,
                      const glm::vec3& worldOffset = glm::vec3(0.0f),
                      std::uint32_t seedOverride = 0);
    // Spawns the baked interior mesh referenced by a native .3dgcave asset.
    // Editor-authored collision/navigation pieces are normally saved with the level.
    ecs::Entity SpawnCave(const std::string& assetPath,
                          const glm::vec3& worldOffset = glm::vec3(0.0f));
    bool LoadDayNightTimeline(const std::string& assetPath, bool play = true);
    void PlayDayNightTimeline();
    void PauseDayNightTimeline();
    void StopDayNightTimeline();
    void SetDayNightTime(float normalizedTime);
    float DayNightTime() const;
    void SetDayNightPlaybackRate(float rate);
    bool WasDayNightEvent(const std::string& eventName);
    void RequestSceneLoad(const std::string& runtimeScenePath);
    // Manual level-streaming requests. The identifier may be the level's
    // manifest path, file name, or file stem.
    void RequestLevelLoad(const std::string& level);
    void RequestLevelUnload(const std::string& level);
    bool SaveValue(const std::string& key, const std::string& value);
    std::string LoadValue(const std::string& key,
                          const std::string& fallback = {}) const;
    // Typed convenience over the flat key/value store (rides along in save slots).
    bool SaveInt(const std::string& key, int value);
    bool SaveFloat(const std::string& key, float value);
    bool SaveBool(const std::string& key, bool value);
    bool SaveVec3(const std::string& key, const glm::vec3& value);
    int GetSavedInt(const std::string& key, int fallback = 0) const;
    float GetSavedFloat(const std::string& key, float fallback = 0.0f) const;
    bool GetSavedBool(const std::string& key, bool fallback = false) const;
    glm::vec3 GetSavedVec3(const std::string& key,
                           const glm::vec3& fallback = glm::vec3(0.0f)) const;
    bool SaveCheckpoint(const std::string& name, const glm::vec3& position);
    bool LoadCheckpoint(const std::string& name, glm::vec3* position) const;
    // Request a full game save/load to a numbered slot. Queued and processed by the
    // host after this script update (a load reloads the saved scene, then restores state).
    void SaveGameToSlot(int slot, const std::string& displayName = {});
    void LoadGameFromSlot(int slot);
    int SetTimer(float seconds, std::function<void()> callback, bool repeat = false);
    int SetTimerByEvent(float seconds, std::function<void()> event,
                        bool repeat = false) {
        return SetTimer(seconds, std::move(event), repeat);
    }
    // Native C++ has no reflection metadata, so expose a small per-script
    // function table. Register once (normally in OnCreate), then timers can use
    // the same string-based workflow as visual scripting and Lua.
    bool BindTimerFunction(const std::string& functionName,
                           std::function<void()> function);
    bool UnbindTimerFunction(const std::string& functionName);
    int SetTimerByFunctionName(const std::string& functionName, float seconds,
                               bool repeat = false);
    int Delay(float seconds, std::function<void()> callback) {
        return SetTimer(seconds, std::move(callback), false);
    }
    void ClearTimer(int timerId);
    int ClearTimerByFunctionName(const std::string& functionName);
    bool IsTimerActive(int timerId) const;
    bool IsTimerActive(const std::string& functionName) const;

    void SetGlobalTimeDilation(float dilation);
    float GlobalTimeDilation() const;
    float EffectiveTimeDilation() const;
    void HitStop(float unscaledSeconds, float dilation = 0.0f);
    bool IsHitStopActive() const;
    // Start a multi-step sequence (Do / Wait / WaitUntil), ticked automatically each update.
    ScriptSequence& Sequence();
    bool CancelSequence(int handle);
    bool PauseSequence(int handle);
    bool ResumeSequence(int handle);
    bool IsSequenceActive(int handle) const;
    bool IsSequencePaused(int handle) const;
    void CancelAllSequences();
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
    // Events are always queued. A broadcast has target == kNull; a targeted event
    // is delivered only to scripts attached to that entity.
    void PublishEvent(ScriptEvent event);
    void PublishEvent(const std::string& name, ecs::Entity target = ecs::kNull);
    bool ListenForEvent(const std::string& name);
    bool StopListeningForEvent(const std::string& name);
    int SubscribeEvent(const std::string& name,
                       std::function<void(const ScriptEvent&)> callback);
    bool UnsubscribeEvent(int subscriptionId);
    void UnsubscribeAllEvents();
    ScriptHandle FindScript(ecs::Entity entity,
                            const std::string& className = {}) const;
    ScriptHandle FindScript(const std::string& objectName,
                            const std::string& className = {}) const;
    bool IsScriptValid(const ScriptHandle& handle) const;
    bool IsScriptEnabled(const ScriptHandle& handle) const;
    bool SetScriptEnabled(const ScriptHandle& handle, bool enabled);
    bool SetSelfEnabled(bool enabled);
    bool BindScriptFunction(const std::string& functionName,
                            CallableFunction function);
    bool UnbindScriptFunction(const std::string& functionName);
    bool CallScript(const ScriptHandle& handle, const std::string& functionName,
                    ScriptEvent arguments = {}, ScriptEvent* result = nullptr);
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
    // Plays a standalone Action Clip attached to the character. The name is the
    // Action Name authored in the Clip Editor, not a locomotion graph state.
    bool PlayActionClip(const std::string& actionName);
    bool PlayAnimationProfile(const std::string& profileName);
    bool SetAnimationParameter(const std::string& name, float value);
    bool SetAnimationBool(const std::string& name, bool value);
    bool SetAnimationTrigger(const std::string& name);
    RaycastHit TraceLine(const glm::vec3& start, const glm::vec3& end,
                         std::uint32_t layerMask = 0xFFFFFFFFu) const;
    RaycastHit TraceSphere(const glm::vec3& start, const glm::vec3& end, float radius,
                           std::uint32_t layerMask = 0xFFFFFFFFu) const;
    std::vector<ecs::Entity> TraceOverlapSphere(const glm::vec3& center, float radius,
                                                 std::uint32_t layerMask = 0xFFFFFFFFu) const;
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
    glm::vec3 GetFieldVec3(const std::string& name, const glm::vec3& fallback = glm::vec3(0.0f)) const;
    glm::vec3 GetFieldColor(const std::string& name, const glm::vec3& fallback = glm::vec3(1.0f)) const;
    ecs::Entity GetFieldEntity(const std::string& name) const;   // resolves the named object
    std::string GetFieldAsset(const std::string& name, const std::string& fallback = {}) const;

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
    virtual bool HasTimerFunction(const std::string& functionName) const;
    virtual bool InvokeTimerFunction(const std::string& functionName);

    struct Timer {
        int id = 0;
        float remaining = 0.0f;
        float interval = 0.0f;
        bool repeat = false;
        bool cancelled = false;
        std::string functionName;
        std::function<void()> callback;
    };
    struct EventSubscription {
        int id = 0;
        std::string name;
        std::function<void(const ScriptEvent&)> callback;
    };
    bool WantsEvent(const std::string& name) const;
    void DispatchEvent(const ScriptEvent& event);
    bool InvokeScriptFunction(const std::string& functionName,
                              const ScriptEvent& arguments, ScriptEvent& result);
    ScriptSequence* FindSequence(int handle);
    const ScriptSequence* FindSequence(int handle) const;
    ScriptContext m_context;
    std::vector<Timer> m_timers;
    std::vector<std::function<void()>> m_timerCallbacks;
    std::unordered_map<std::string, std::function<void()>> m_timerFunctions;
    int m_nextTimerId = 1;
    int m_nextSequenceId = 1;
    std::vector<std::unique_ptr<ScriptSequence>> m_sequences;
    std::unordered_set<std::string> m_listenedEvents;
    std::vector<EventSubscription> m_eventSubscriptions;
    int m_nextEventSubscriptionId = 1;
    std::unordered_map<std::string, CallableFunction> m_scriptFunctions;
};

// ---- Grouped script API proxies -------------------------------------------------
// Thin, zero-overhead sugar that forwards to Script's flat helpers. They exist only to
// make the large Script surface discoverable (`Camera().Shake()` reads better than a
// wall of Shake*/*Camera* methods). Every original method still works unchanged.
class ScriptInputApi {
public:
    explicit ScriptInputApi(Script* s) : m_s(s) {}
    bool KeyDown(int key) const { return m_s->IsKeyDown(key); }
    bool KeyPressed(int key) const { return m_s->WasKeyPressed(key); }
    bool MouseDown(int button) const { return m_s->IsMouseButtonDown(button); }
    bool MousePressed(int button) const { return m_s->WasMouseButtonPressed(button); }
    float MouseDeltaX() const { return m_s->MouseDeltaX(); }
    float MouseDeltaY() const { return m_s->MouseDeltaY(); }
private:
    Script* m_s;
};

class ScriptCameraApi {
public:
    explicit ScriptCameraApi(Script* s) : m_s(s) {}
    bool Shake(float intensity = 1.0f, float duration = 0.35f, float frequency = 18.0f) {
        return m_s->ShakeCamera(intensity, duration, frequency);
    }
    bool ShakeAdvanced(float translationAmplitude, float rotationDegrees,
                       float duration = 0.35f, float frequency = 18.0f, float fovAmplitude = 0.0f) {
        return m_s->ShakeCameraAdvanced(translationAmplitude, rotationDegrees, duration,
                                        frequency, fovAmplitude);
    }
    bool PlaySequence(const std::string& name, bool lockInput = true, bool skippable = true) {
        return m_s->PlayCameraSequence(name, lockInput, skippable);
    }
    bool StopSequence() { return m_s->StopCameraSequence(); }
    bool SkipSequence() { return m_s->SkipCameraSequence(); }
    bool IsSequencePlaying(const std::string& name = {}) const {
        return m_s->IsCameraSequencePlaying(name);
    }
    bool WasSequenceFinished(const std::string& name) const {
        return m_s->WasCameraSequenceFinished(name);
    }
    bool WasSequenceSkipped(const std::string& name) const {
        return m_s->WasCameraSequenceSkipped(name);
    }
    bool WasSequenceEvent(const std::string& sequenceName, const std::string& eventName) const {
        return m_s->WasCameraSequenceEvent(sequenceName, eventName);
    }
private:
    Script* m_s;
};

class ScriptParticlesApi {
public:
    explicit ScriptParticlesApi(Script* s) : m_s(s) {}
    bool Play(bool restart = false) { return m_s->PlayParticles(restart); }
    bool Play(ecs::Entity e, bool restart = false) { return m_s->PlayParticles(e, restart); }
    bool Stop(bool clear = false) { return m_s->StopParticles(clear); }
    bool Stop(ecs::Entity e, bool clear = false) { return m_s->StopParticles(e, clear); }
    bool Restart() { return m_s->RestartParticles(); }
    bool Restart(ecs::Entity e) { return m_s->RestartParticles(e); }
    bool Burst(int count = 0) { return m_s->BurstParticles(count); }
    bool Burst(ecs::Entity e, int count = 0) { return m_s->BurstParticles(e, count); }
    bool Clear() { return m_s->ClearParticles(); }
    bool Clear(ecs::Entity e) { return m_s->ClearParticles(e); }
    bool SetEnabled(bool enabled) { return m_s->SetParticlesEnabled(enabled); }
    bool SetEnabled(ecs::Entity e, bool enabled) { return m_s->SetParticlesEnabled(e, enabled); }
    bool SetRate(float perSecond) { return m_s->SetParticleRate(perSecond); }
    bool SetRate(ecs::Entity e, float perSecond) { return m_s->SetParticleRate(e, perSecond); }
    bool SetSpeed(float simulationSpeed) { return m_s->SetParticleSpeed(simulationSpeed); }
    bool SetSpeed(ecs::Entity e, float simulationSpeed) { return m_s->SetParticleSpeed(e, simulationSpeed); }
    bool IsPlaying() const { return m_s->AreParticlesPlaying(); }
    bool IsPlaying(ecs::Entity e) const { return m_s->AreParticlesPlaying(e); }
    int Count() const { return m_s->ParticleCount(); }
    int Count(ecs::Entity e) const { return m_s->ParticleCount(e); }
private:
    Script* m_s;
};

inline ScriptInputApi Script::Input() { return ScriptInputApi(this); }
inline ScriptCameraApi Script::Camera() { return ScriptCameraApi(this); }
inline ScriptParticlesApi Script::Particles() { return ScriptParticlesApi(this); }

class ScriptAudioApi {
public:
    explicit ScriptAudioApi(Script* s) : m_s(s) {}
    bool Play(bool restart = false) { return m_s->PlayAudio(restart); }
    bool Play(ecs::Entity e, bool restart = false) { return m_s->PlayAudio(e, restart); }
    bool Pause() { return m_s->PauseAudio(); }
    bool Pause(ecs::Entity e) { return m_s->PauseAudio(e); }
    bool Resume() { return m_s->ResumeAudio(); }
    bool Resume(ecs::Entity e) { return m_s->ResumeAudio(e); }
    bool Stop() { return m_s->StopAudio(); }
    bool Stop(ecs::Entity e) { return m_s->StopAudio(e); }
    bool Seek(float seconds) { return m_s->SeekAudio(seconds); }
    bool Seek(ecs::Entity e, float seconds) { return m_s->SeekAudio(e, seconds); }
    bool IsPlaying() const { return m_s->IsAudioPlaying(); }
    bool IsPlaying(ecs::Entity e) const { return m_s->IsAudioPlaying(e); }
    bool IsPaused() const { return m_s->IsAudioPaused(); }
    bool IsPaused(ecs::Entity e) const { return m_s->IsAudioPaused(e); }
    float CursorSeconds() const { return m_s->AudioCursorSeconds(); }
    float CursorSeconds(ecs::Entity e) const { return m_s->AudioCursorSeconds(e); }
    bool SetVolume(float volume) { return m_s->SetAudioVolume(volume); }
    bool SetVolume(ecs::Entity e, float volume) { return m_s->SetAudioVolume(e, volume); }
    bool SetPitch(float pitch) { return m_s->SetAudioPitch(pitch); }
    bool SetPitch(ecs::Entity e, float pitch) { return m_s->SetAudioPitch(e, pitch); }
    bool SetLooping(bool looping) { return m_s->SetAudioLooping(looping); }
    bool SetLooping(ecs::Entity e, bool looping) { return m_s->SetAudioLooping(e, looping); }
    bool SetSpatial(bool spatial) { return m_s->SetAudioSpatial(spatial); }
    bool SetSpatial(ecs::Entity e, bool spatial) { return m_s->SetAudioSpatial(e, spatial); }
    bool SetBus(AudioBus bus) { return m_s->SetAudioBus(bus); }
    bool SetBus(ecs::Entity e, AudioBus bus) { return m_s->SetAudioBus(e, bus); }
    bool ApplySnapshot(AudioSnapshotPreset preset, float transitionSeconds = 0.25f) {
        return m_s->ApplyAudioSnapshot(preset, transitionSeconds);
    }
    bool SetDialogueDucking(bool enabled) { return m_s->SetDialogueDucking(enabled); }
    bool PlayCue(const std::string& path, bool spatial = true) { return m_s->PlayAudioCue(path, spatial); }
    bool LoadMusic(const std::string& path) { return m_s->LoadAdaptiveMusic(path); }
    bool SetMusicState(const std::string& stateName, bool synchronizeToBeat = true) {
        return m_s->SetMusicState(stateName, synchronizeToBeat);
    }
    bool SetAttenuation(float minDistance, float maxDistance, float rolloff) {
        return m_s->SetAudioAttenuation(minDistance, maxDistance, rolloff);
    }
    bool SetAttenuation(ecs::Entity e, float minDistance, float maxDistance, float rolloff) {
        return m_s->SetAudioAttenuation(e, minDistance, maxDistance, rolloff);
    }
    bool SetDoppler(float factor) { return m_s->SetAudioDoppler(factor); }
    bool SetDoppler(ecs::Entity e, float factor) { return m_s->SetAudioDoppler(e, factor); }
    bool SetCone(float innerDegrees, float outerDegrees, float outerGain) {
        return m_s->SetAudioCone(innerDegrees, outerDegrees, outerGain);
    }
    bool SetCone(ecs::Entity e, float innerDegrees, float outerDegrees, float outerGain) {
        return m_s->SetAudioCone(e, innerDegrees, outerDegrees, outerGain);
    }
    bool SetOcclusion(float amount) { return m_s->SetAudioOcclusion(amount); }
    bool SetOcclusion(ecs::Entity e, float amount) { return m_s->SetAudioOcclusion(e, amount); }
    bool SetPriority(int priority) { return m_s->SetAudioPriority(priority); }
    bool SetPriority(ecs::Entity e, int priority) { return m_s->SetAudioPriority(e, priority); }
private:
    Script* m_s;
};

class ScriptAnimApi {
public:
    explicit ScriptAnimApi(Script* s) : m_s(s) {}
    bool PlayAction(int clipIndex, float fadeIn = 0.1f, float fadeOut = 0.2f, float speed = 1.0f) {
        return m_s->PlayAnimationAction(clipIndex, fadeIn, fadeOut, speed);
    }
    bool PlayAction(const std::string& clipName, float fadeIn = 0.1f, float fadeOut = 0.2f, float speed = 1.0f) {
        return m_s->PlayAnimationAction(clipName, fadeIn, fadeOut, speed);
    }
    bool PlayMaskedAction(int clipIndex, const std::string& rootBone,
                          float fadeIn = 0.1f, float fadeOut = 0.2f, float speed = 1.0f) {
        return m_s->PlayMaskedAnimationAction(clipIndex, rootBone, fadeIn, fadeOut, speed);
    }
    bool PlayMaskedAction(const std::string& clipName, const std::string& rootBone,
                          float fadeIn = 0.1f, float fadeOut = 0.2f, float speed = 1.0f) {
        return m_s->PlayMaskedAnimationAction(clipName, rootBone, fadeIn, fadeOut, speed);
    }
    bool PlayActionClip(const std::string& actionName) { return m_s->PlayActionClip(actionName); }
    bool PlayProfile(const std::string& profileName) { return m_s->PlayAnimationProfile(profileName); }
    bool SetParameter(const std::string& name, float value) { return m_s->SetAnimationParameter(name, value); }
    bool SetBool(const std::string& name, bool value) { return m_s->SetAnimationBool(name, value); }
    bool SetTrigger(const std::string& name) { return m_s->SetAnimationTrigger(name); }
    float GetParameter(const std::string& name, float fallback = 0.0f) const {
        return m_s->GetAnimationParameter(name, fallback);
    }
    bool GetBool(const std::string& name, bool fallback = false) const {
        return m_s->GetAnimationBool(name, fallback);
    }
    bool IsActionPlaying() const { return m_s->IsAnimationActionPlaying(); }
    bool IsMovementLocked() const { return m_s->IsAnimationMovementLocked(); }
private:
    Script* m_s;
};

inline ScriptAudioApi Script::Audio() { return ScriptAudioApi(this); }
inline ScriptAnimApi Script::Anim() { return ScriptAnimApi(this); }

struct NativeScriptSlot {
    bool enabled = true;
    bool created = false;
    bool active = false;
    bool missingFactory = false;
    int executionOrder = 0; // lower values initialize/update first
    std::vector<std::string> dependencies; // required script class names
    std::string dependencyError; // runtime diagnostic; not authored
    std::string reportedDependencyError;
    std::string className;
    std::string sourcePath;
    std::vector<ScriptField> fields;
    std::unique_ptr<Script> instance;
    Script::ReloadState reloadState;
    bool restoreReloadState = false;
    Script::StateMap persistentState;
    int persistentStateVersion = 1;
    bool restorePersistentState = false;
};

struct NativeScriptComponent : NativeScriptSlot {
    std::vector<NativeScriptSlot> additional;
};

class ScriptRegistry {
public:
    using Factory = std::function<std::unique_ptr<Script>()>;

    static ScriptRegistry& Instance();

    void Register(const std::string& className, Factory factory);
    bool Has(const std::string& className) const;
    std::unique_ptr<Script> Create(const std::string& className) const;
    std::vector<std::string> Names() const;
    void Remove(const std::string& className);
    ScriptRegistry Extract(const std::vector<std::string>& classNames);
    void MergeFrom(ScriptRegistry&& other);
    void Clear() { m_factories.clear(); m_registrationErrors.clear(); }
    void SetStrictValidation(bool strict) { m_strictValidation = strict; }
    bool Valid(std::string* error = nullptr) const;

private:
    std::unordered_map<std::string, Factory> m_factories;
    std::vector<std::string> m_registrationErrors;
    bool m_strictValidation = false;
};

enum class ScriptCallbackKind {
    OnCreate = 0,
    OnEnable,
    OnDisable,
    OnUpdate,
    OnFixedUpdate,
    OnEvent,
    OnScriptCall
};

struct ScriptExecutionStat {
    ecs::Entity entity = ecs::kNull;
    std::string className;
    ScriptCallbackKind callback = ScriptCallbackKind::OnUpdate;
    double lastMilliseconds = 0.0;
    double averageMilliseconds = 0.0;
    double maximumMilliseconds = 0.0;
    std::uint64_t callCount = 0;
};

struct ScriptDebugState {
    bool enabled = false;
    bool paused = false;
    std::string stopReason;
    std::vector<ScriptExecutionStat> statistics;
};

// Runtime callback debugger. Breakpoints stop after the matching callback completes,
// leaving the registry in a valid state. A step executes exactly one pending lifecycle
// callback while paused. This complements source-level breakpoints in VS/Rider/VS Code.
void SetScriptDebuggingEnabled(bool enabled);
void SetScriptExecutionPaused(bool paused);
void RequestScriptExecutionStep();
void SetScriptCallbackBreakpoint(const std::string& className,
                                 ScriptCallbackKind callback, bool enabled);
bool HasScriptCallbackBreakpoint(const std::string& className,
                                 ScriptCallbackKind callback);
void ClearScriptCallbackBreakpoints();
void ClearScriptExecutionStatistics();
ScriptDebugState GetScriptDebugState();
const char* ScriptCallbackKindName(ScriptCallbackKind callback);

// Runtime-system bridges. They enqueue only; game code runs later at the safe
// dispatch point inside UpdateScripts.
void QueueScriptEvent(ecs::Registry& registry, ScriptEvent event);
void QueueScriptAnimationEvent(ecs::Registry& registry, ecs::Entity entity,
                               const std::string& eventName);
void QueueScriptCollisionEvents(ecs::Registry& registry,
                                const std::vector<CollisionEvent>& events);

// Per-frame update: creates instances, calls OnCreate once, then OnUpdate(dt).
void UpdateScripts(ecs::Registry& registry, float dt, const ScriptInputState* input = nullptr,
                   RuntimeAudioSystem* audio = nullptr, CameraShake* cameraShake = nullptr,
                   CameraDirector* cameraDirector = nullptr, GameMode* gameMode = nullptr,
                   PhysicsWorld* physics = nullptr);

// Per-physics-step update: calls OnFixedUpdate(dt) on already-created scripts. Call
// this from the fixed-timestep loop; instance creation stays in UpdateScripts.
void FixedUpdateScripts(ecs::Registry& registry, float dt, const ScriptInputState* input = nullptr,
                        RuntimeAudioSystem* audio = nullptr, CameraShake* cameraShake = nullptr,
                        CameraDirector* cameraDirector = nullptr, GameMode* gameMode = nullptr,
                        PhysicsWorld* physics = nullptr);

// Calls OnDestroy() on every live script and releases the instances. Call when
// leaving Play mode or unloading the scene so scripts can clean up.
void ShutdownScripts(ecs::Registry& registry);
// Level streaming variant: invokes OnDestroy and releases scripts only for the
// entities being unloaded, leaving scripts in other resident levels untouched.
void ShutdownScripts(ecs::Registry& registry, const std::vector<ecs::Entity>& entities);

// Captures optional transient reload state, invokes OnDestroy, and releases every live
// native instance while preserving its authored fields. New instances consume the saved
// state after OnCreate on their next UpdateScripts call.
void PrepareScriptsForHotReload(ecs::Registry& registry);

// Recreates prepared native instances and delivers OnCreate/OnEnable without running an
// OnUpdate frame. Used by the editor while a candidate DLL is still protected by its
// persistent load marker. Returns false if an enabled attachment cannot be recreated.
bool RecreateScriptsAfterHotReload(ecs::Registry& registry,
                                   RuntimeAudioSystem* audio = nullptr,
                                   CameraShake* cameraShake = nullptr,
                                   CameraDirector* cameraDirector = nullptr,
                                   GameMode* gameMode = nullptr,
                                   PhysicsWorld* physics = nullptr,
                                   std::string* error = nullptr);

// Optional sink for script errors (a script that threw and was disabled). The editor
// wires this to its console so failures are visible instead of only hitting stderr.
// Pass nullptr to clear. Not thread-safe; set it once during startup.
void SetScriptErrorHandler(std::function<void(const std::string&)> handler);

// Scene requests are queued by scripts and consumed by the runtime host after the
// current script update, avoiding registry destruction from inside a callback.
std::string ConsumeScriptSceneLoadRequest();

struct ScriptLevelStreamRequest {
    std::string level;
    bool load = true;
};
std::vector<ScriptLevelStreamRequest> ConsumeScriptLevelStreamRequests();

// A queued full save/load to a numbered slot. The host captures/restores world state.
struct ScriptSaveGameRequest {
    int slot = 0;
    bool load = false;               // false = save, true = load
    std::string displayName;         // save only: shown in the load menu
};
std::vector<ScriptSaveGameRequest> ConsumeScriptSaveGameRequests();

// Discovers native registered suites plus Lua ScriptTests tables and executes
// every case in a fresh temporary registry/entity.
std::vector<ScriptTestResult> RunScriptTests(
    const std::vector<std::string>& luaSourcePaths = {});

// SaveGame and level-streaming hosts use these to preserve versioned state for
// scripts attached to named entities. Values are encoded into the existing save
// key/value map, keeping the on-disk format backward compatible.
void CaptureScriptPersistentStates(
    ecs::Registry& registry,
    std::unordered_map<std::string, std::string>& values);
void CaptureScriptPersistentStates(
    ecs::Registry& registry, const std::vector<ecs::Entity>& entities,
    std::unordered_map<std::string, std::string>& values);
void RestoreScriptPersistentStates(
    ecs::Registry& registry,
    const std::unordered_map<std::string, std::string>& values);
void RestoreScriptPersistentStates(
    ecs::Registry& registry, const std::vector<ecs::Entity>& entities,
    const std::unordered_map<std::string, std::string>& values);

} // namespace engine
