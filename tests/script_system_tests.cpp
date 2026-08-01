#include <engine/gameplay/Script.h>
#include <engine/gameplay/GameMode.h>
#include <engine/physics/PhysicsComponents.h>
#include <engine/physics/PhysicsWorld.h>
#include <engine/ui/Hud.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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
int namedTimerCallbacks = 0;
engine::ecs::Entity spawnedEntity = engine::ecs::kNull;

class GameplayServicesScript final : public engine::Script {
public:
    void OnCreate() override {
        spawnedEntity = SpawnFromObject("ProjectilePrototype", {4.0f, 2.0f, 1.0f});
        Delay(0.1f, [] { ++timerCallbacks; });
    }
    void OnUpdate(float) override {
        if (timerCallbacks > 0) {
            RequestSceneLoad("Levels/Next.3dgscene");
            RequestLevelLoad("Dungeon");
            RequestLevelUnload("Courtyard.runtime.scene");
        }
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

class NamedTimerScript final : public engine::Script {
public:
    void OnCreate() override {
        BindTimerFunction("Pulse", [this] {
            ++namedTimerCallbacks;
            if (namedTimerCallbacks == 2)
                ClearTimerByFunctionName("Pulse");
        });
        timerHandle = SetTimerByFunctionName("Pulse", 0.1f, true);
    }

    void OnUpdate(float) override {
        if (namedTimerCallbacks == 1)
            activeAfterFirstPulse = IsTimerActive(timerHandle)
                && IsTimerActive("Pulse");
    }

    bool TimerStillActive() const {
        return IsTimerActive(timerHandle);
    }

    int timerHandle = 0;
    bool activeAfterFirstPulse = false;
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
    scripts.Register("NamedTimerRegressionScript", [] {
        return std::make_unique<NamedTimerScript>();
    });

    {
        engine::ecs::Registry registry;
        const engine::ecs::Entity player = registry.Create();
        registry.Add<engine::ecs::Transform>(player, engine::ecs::Transform{});
        engine::ecs::Collider playerCollider =
            engine::ecs::Collider::MakeCapsuleFromHeight(0.4f, 1.8f);
        playerCollider.isTrigger = true;
        registry.Add<engine::ecs::Collider>(player, playerCollider);

        const engine::ecs::Entity coin = registry.Create();
        registry.Add<engine::ecs::Transform>(coin, engine::ecs::Transform{});
        engine::ecs::Collider coinCollider = engine::ecs::Collider::MakeCylinder(0.25f, 0.05f);
        coinCollider.isTrigger = true;
        registry.Add<engine::ecs::Collider>(coin, coinCollider);

        engine::PhysicsWorld physics;
        physics.gravity = glm::vec3(0.0f);
        physics.Step(registry, 1.0f / 60.0f);
        const auto entered = std::find_if(
            physics.Events().begin(), physics.Events().end(),
            [player, coin](const engine::CollisionEvent& event) {
                return event.trigger
                    && event.phase == engine::CollisionEvent::Phase::Enter
                    && ((event.a == player && event.b == coin)
                        || (event.a == coin && event.b == player));
            });
        Check(entered != physics.Events().end(),
              "trigger-only player and coin emit an overlap event without rigid bodies");
    }

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
        const auto levelRequests =
            engine::ConsumeScriptLevelStreamRequests();
        Check(levelRequests.size() == 2
                && levelRequests[0].load
                && levelRequests[0].level == "Dungeon"
                && !levelRequests[1].load
                && levelRequests[1].level == "Courtyard.runtime.scene",
              "manual level streaming requests are safely queued for the runtime host");
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

    {
        engine::ecs::Registry registry;
        const engine::ecs::Entity entity = registry.Create();
        registry.Add<engine::NativeScriptComponent>(
            entity, Component("NamedTimerRegressionScript"));
        engine::UpdateScripts(registry, 0.1f);
        auto* component =
            registry.TryGet<engine::NativeScriptComponent>(entity);
        auto* named = component
            ? dynamic_cast<NamedTimerScript*>(component->instance.get()) : nullptr;
        Check(named && named->timerHandle != 0
                && named->activeAfterFirstPulse,
              "named repeating timer can be queried by handle and function name");
        engine::UpdateScripts(registry, 0.1f);
        engine::UpdateScripts(registry, 0.2f);
        Check(namedTimerCallbacks == 2
                && named && !named->TimerStillActive(),
              "named timer invokes the bound C++ event and can clear itself by name");
        engine::ShutdownScripts(registry);
    }

    {
        const std::filesystem::path luaPath =
            std::filesystem::current_path() / "lua_script_regression.lua";
        {
            std::ofstream lua(luaPath, std::ios::trunc);
            lua << "function OnCreate()\n"
                   "  local x = Engine.GetFieldFloat(\"startX\", 1)\n"
                   "  Engine.SetPosition(x, 2, 3)\n"
                   "  Engine.SetTimerByFunctionName(\"Pulse\", 0.1, false)\n"
                   "  Engine.SetGlobalTimeDilation(0.5)\n"
                   "  Engine.HitStop(0.1, 0.0)\n"
                   "end\n"
                   "function Pulse()\n"
                   "  Engine.Translate(1, 0, 0)\n"
                   "end\n"
                   "function OnUpdate(dt)\n"
                   "  Engine.Translate(dt, 0, 0)\n"
                   "end\n"
                   "function OnFixedUpdate(dt)\n"
                   "  Engine.Translate(0, dt, 0)\n"
                   "end\n"
                   "function OnDestroy()\n"
                   "  Engine.SetScale(2, 2, 2)\n"
                   "end\n";
        }

        engine::ecs::Registry registry;
        const engine::ecs::Entity entity = registry.Create();
        registry.Add<engine::ecs::Transform>(entity);
        engine::NativeScriptComponent component;
        component.className = "LuaRegression";
        component.sourcePath = luaPath.string();
        component.fields.push_back(
            {"startX", engine::ScriptField::Type::Float, "5"});
        registry.Add<engine::NativeScriptComponent>(entity, std::move(component));

        engine::GameMode& gameMode = engine::GameMode::Instance();
        gameMode.Reset();
        engine::UpdateScripts(
            registry, 0.25f, nullptr, nullptr, nullptr, nullptr, &gameMode);
        const engine::ecs::Transform& afterUpdate =
            registry.Get<engine::ecs::Transform>(entity);
        Check(afterUpdate.position.x == 6.25f
                && afterUpdate.position.y == 2.0f
                && afterUpdate.position.z == 3.0f,
              "Lua scripts run named function timers and normal lifecycle callbacks");
        Check(gameMode.GlobalTimeDilation() == 0.5f
                && gameMode.HitStopActive()
                && gameMode.EffectiveTimeDilation() == 0.0f,
              "Lua scripts control global dilation and hit stop");

        engine::FixedUpdateScripts(registry, 0.5f);
        Check(registry.Get<engine::ecs::Transform>(entity).position.y == 2.5f,
              "Lua scripts receive fixed updates through the native lifecycle");

        engine::ShutdownScripts(registry);
        Check(registry.Get<engine::ecs::Transform>(entity).scale.x == 2.0f,
              "Lua scripts receive OnDestroy before their VM is released");
        std::error_code cleanupError;
        std::filesystem::remove(luaPath, cleanupError);
        gameMode.Reset();
    }

    {
        engine::GameMode& gameMode = engine::GameMode::Instance();
        engine::ecs::Registry registry;
        gameMode.Reset();
        gameMode.SetGlobalTimeDilation(0.25f);
        Check(gameMode.ScaleDelta(2.0f) == 0.5f,
              "global time dilation scales gameplay delta time");
        gameMode.HitStop(0.1f);
        Check(gameMode.HitStopActive()
                && gameMode.ScaleDelta(2.0f) == 0.0f,
              "zero-dilation hit stop freezes gameplay");
        gameMode.UpdateUnscaledTime(0.05f);
        Check(gameMode.HitStopActive(),
              "hit stop duration advances using unscaled time");
        gameMode.UpdateUnscaledTime(0.06f);
        Check(!gameMode.HitStopActive()
                && gameMode.ScaleDelta(2.0f) == 0.5f,
              "hit stop restores the configured global dilation");
        gameMode.AddScore(30);
        gameMode.Update(registry, engine::ecs::kNull, 1.5f);

        engine::HudContext context;
        context.floats["score"] = static_cast<float>(gameMode.Score());
        context.floats["time"] = gameMode.Elapsed();
        context.strings["gamestate"] = engine::GameMode::StateName(gameMode.State());
        engine::HudWidget score;
        score.type = engine::HudWidgetType::Text;
        score.binding = engine::HudBinding::NamedFloat;
        score.bindKey = "score";
        score.text = "Score: {}";
        engine::HudWidget state = score;
        state.binding = engine::HudBinding::NamedString;
        state.bindKey = "gamestate";
        state.text = "{}";
        Check(engine::ResolveHudText(score, context) == "Score: 30",
              "HUD resolves the live GameMode score binding");
        Check(engine::ResolveHudText(state, context) == "Playing",
              "HUD resolves the live GameMode state binding");
        Check(context.floats["time"] > 1.49f,
              "GameMode play clock advances for the HUD time binding");
    }

    if (failures != 0) {
        std::cerr << failures << " scripting regression check(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All scripting regression checks passed\n";
    return EXIT_SUCCESS;
}
