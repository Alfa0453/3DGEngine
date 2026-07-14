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

struct ScriptAnimationEvent {
    ecs::Entity entity = ecs::kNull;
    std::string name;
};

struct ScriptContext {
    ecs::Registry* registry = nullptr;
    ecs::Entity entity = ecs::kNull;
    std::vector<ecs::Entity>* destroyQueue = nullptr;
    const struct ScriptInputState* input = nullptr;
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
    ScriptContext m_context;
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
void UpdateScripts(ecs::Registry& registry, float dt, const ScriptInputState* input = nullptr);

// Per-physics-step update: calls OnFixedUpdate(dt) on already-created scripts. Call
// this from the fixed-timestep loop; instance creation stays in UpdateScripts.
void FixedUpdateScripts(ecs::Registry& registry, float dt, const ScriptInputState* input = nullptr);

// Calls OnDestroy() on every live script and releases the instances. Call when
// leaving Play mode or unloading the scene so scripts can clean up.
void ShutdownScripts(ecs::Registry& registry);

} // namespace engine