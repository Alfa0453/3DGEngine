#include <engine/gameplay/Script.h>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

struct LifecycleState {
    int created = 0;
    int updated = 0;
    int fixedUpdated = 0;
    int destroyed = 0;
    float frameDt = 0.0f;
    float fixedDt = 0.0f;
    bool frameKeyDown = false;
    bool frameKeyPressed = false;
    bool fixedKeyDown = false;
    bool fixedKeyPressed = false;
};

LifecycleState lifecycle;

class LifecycleScript final : public engine::Script {
public:
    void OnCreate() override {
        ++lifecycle.created;
    }

    void OnUpdate(float dt) override {
        ++lifecycle.updated;
        lifecycle.frameDt = dt;
        lifecycle.frameKeyDown = IsKeyDown(7);
        lifecycle.frameKeyPressed = WasKeyPressed(7);
    }

    void OnFixedUpdate(float dt) override {
        ++lifecycle.fixedUpdated;
        lifecycle.fixedDt = dt;
        lifecycle.fixedKeyDown = IsKeyDown(7);
        lifecycle.fixedKeyPressed = WasKeyPressed(7);
    }

    void OnDestroy() override {
        ++lifecycle.destroyed;
    }
};

int destroyCreated = 0;
int destroyUpdated = 0;
int destroyDestroyed = 0;

class DestroySelfScript final : public engine::Script {
public:
    void OnCreate() override {
        ++destroyCreated;
    }

    void OnUpdate(float) override {
        ++destroyUpdated;
        DestroySelf();
    }

    void OnDestroy() override {
        ++destroyDestroyed;
    }
};

int throwingDestroyed = 0;
int timerCallbacks = 0;
engine::ecs::Entity spawnedEntity = engine::ecs::kNull;

class GameplayServicesScript final : public engine::Script {
public:
    void OnCreate() override {
        spawnedEntity = SpawnFromObject("ProjectilePrototype", {4.0f, 2.0f, 1.0f});
        Delay(0.1f, [] { ++timerCallbacks; });
    }
    void OnUpdate(float) override {
        if (timerCallbacks > 0) RequestSceneLoad("Levels/Next.3dgscene");
    }
};

class ThrowingScript final : public engine::Script {
public:
    void OnUpdate(float) override {
        throw std::runtime_error("expected regression-test exception");
    }

    void OnDestroy() override {
        ++throwingDestroyed;
    }
};

engine::NativeScriptComponent Component(const std::string& className) {
    engine::NativeScriptComponent component;
    component.className = className;
    return component;
}

} // namespace

int main() {
    engine::ScriptRegistry& scripts = engine::ScriptRegistry::Instance();
    scripts.Register("LifecycleRegressionScript", [] {
        return std::make_unique<LifecycleScript>();
    });
    scripts.Register("DestroySelfRegressionScript", [] {
        return std::make_unique<DestroySelfScript>();
    });
    scripts.Register("ThrowingRegressionScript", [] {
        return std::make_unique<ThrowingScript>();
    });
    scripts.Register("GameplayServicesRegressionScript", [] {
        return std::make_unique<GameplayServicesScript>();
    });

    {
        engine::ecs::Registry registry;
        const engine::ecs::Entity prototype = registry.Create();
        registry.Add<engine::ecs::RuntimeName>(
            prototype, engine::ecs::RuntimeName{"ProjectilePrototype"});
        registry.Add<engine::ecs::Transform>(prototype);
        const engine::ecs::Entity scriptEntity = registry.Create();
        registry.Add<engine::NativeScriptComponent>(
            scriptEntity, Component("GameplayServicesRegressionScript"));

        engine::UpdateScripts(registry, 0.05f);
        Check(spawnedEntity != engine::ecs::kNull && registry.Valid(spawnedEntity),
              "scripts can spawn from a named runtime prototype");
        Check(registry.Get<engine::ecs::Transform>(spawnedEntity).position.x == 4.0f,
              "spawned prototype is placed at the requested position");
        engine::UpdateScripts(registry, 0.06f);
        Check(timerCallbacks == 1, "delayed script callback fires once");
        Check(engine::ConsumeScriptSceneLoadRequest() == "Levels/Next.3dgscene",
              "scene changes are safely queued for the runtime host");
    }

    {
        engine::ecs::Registry registry;
        const engine::ecs::Entity entity = registry.Create();
        registry.Add<engine::NativeScriptComponent>(
            entity, Component("LifecycleRegressionScript"));

        engine::ScriptInputState frameInput;
        frameInput.enabled = true;
        frameInput.keysDown.insert(7);
        frameInput.keysPressed.insert(7);
        engine::UpdateScripts(registry, 0.125f, &frameInput);

        Check(lifecycle.created == 1, "OnCreate runs before the first frame update");
        Check(lifecycle.updated == 1, "OnUpdate runs during UpdateScripts");
        Check(lifecycle.fixedUpdated == 0,
              "OnFixedUpdate does not run during the frame update");
        Check(lifecycle.frameDt == 0.125f, "OnUpdate receives the frame delta");
        Check(lifecycle.frameKeyDown && lifecycle.frameKeyPressed,
              "frame scripts receive held and pressed input");

        engine::ScriptInputState fixedInput;
        fixedInput.enabled = true;
        fixedInput.keysDown.insert(7);
        engine::FixedUpdateScripts(registry, 0.02f, &fixedInput);

        Check(lifecycle.created == 1, "OnCreate runs only once");
        Check(lifecycle.updated == 1,
              "OnUpdate is not called by FixedUpdateScripts");
        Check(lifecycle.fixedUpdated == 1,
              "OnFixedUpdate runs during FixedUpdateScripts");
        Check(lifecycle.fixedDt == 0.02f, "OnFixedUpdate receives the fixed delta");
        Check(lifecycle.fixedKeyDown && !lifecycle.fixedKeyPressed,
              "fixed scripts receive held input without replaying frame edges");

        engine::UpdateScripts(registry, 0.25f, &frameInput);
        Check(lifecycle.created == 1 && lifecycle.updated == 2,
              "subsequent frame updates reuse the script instance");

        engine::ShutdownScripts(registry);
        const auto* component =
            registry.TryGet<engine::NativeScriptComponent>(entity);
        Check(lifecycle.destroyed == 1, "ShutdownScripts calls OnDestroy once");
        Check(component && !component->instance && !component->created,
              "ShutdownScripts releases and resets the instance");
    }

    {
        engine::ecs::Registry registry;
        const engine::ecs::Entity entity = registry.Create();
        registry.Add<engine::NativeScriptComponent>(
            entity, Component("DestroySelfRegressionScript"));

        engine::UpdateScripts(registry, 0.016f);
        Check(destroyCreated == 1 && destroyUpdated == 1,
              "destroying script receives create and update callbacks");
        Check(destroyDestroyed == 1,
              "DestroySelf invokes OnDestroy before removing the entity");
        Check(!registry.Valid(entity), "DestroySelf removes the entity after iteration");
    }

    {
        engine::ecs::Registry registry;
        const engine::ecs::Entity entity = registry.Create();
        registry.Add<engine::NativeScriptComponent>(
            entity, Component("ThrowingRegressionScript"));

        engine::UpdateScripts(registry, 0.016f);
        auto* component = registry.TryGet<engine::NativeScriptComponent>(entity);
        Check(component && !component->enabled,
              "a throwing frame callback disables only that script");
        Check(registry.Valid(entity),
              "a throwing script does not destroy its entity or stop the update loop");

        engine::ShutdownScripts(registry);
        Check(throwingDestroyed == 1,
              "disabled scripts still receive OnDestroy during shutdown");
    }

    if (failures != 0) {
        std::cerr << failures << " scripting regression check(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All scripting regression checks passed\n";
    return EXIT_SUCCESS;
}
