#include <engine/gameplay/Script.h>
#include <engine/gameplay/GameMode.h>
#include <engine/gameplay/GameplayComponents.h>
#include <engine/gameplay/SaveGame.h>
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
    int enabled = 0;
    int disabled = 0;
    int updated = 0;
    int fixedUpdated = 0;
    int destroyed = 0;
    float frameDt = 0.0f;
    float fixedDt = 0.0f;
    bool frameKeyDown = false;
    bool frameKeyPressed = false;
    bool fixedKeyDown = false;
    bool fixedKeyPressed = false;
    bool destroySawDisabled = false;
};

LifecycleState lifecycle;

class LifecycleScript final : public engine::Script {
public:
    void OnCreate() override {
        ++lifecycle.created;
    }

    void OnEnable() override { ++lifecycle.enabled; }
    void OnDisable() override { ++lifecycle.disabled; }

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
        lifecycle.destroySawDisabled = lifecycle.disabled == 2;
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

int hotReloadRestoredTicks = -1;
int hotReloadOldDestroyed = 0;
int debugUpdateCount = 0;

class HotReloadOldScript final : public engine::Script {
public:
    void OnUpdate(float) override { ++ticks; }
    void OnDestroy() override { ++hotReloadOldDestroyed; }
    void OnBeforeHotReload(ReloadState& state) const override {
        state["ticks"] = std::to_string(ticks);
    }
private:
    int ticks = 0;
};

class HotReloadNewScript final : public engine::Script {
public:
    void OnAfterHotReload(const ReloadState& state) override {
        const auto found = state.find("ticks");
        hotReloadRestoredTicks = found == state.end() ? -1 : std::stoi(found->second);
    }
};

class DebugCallbackScript final : public engine::Script {
public:
    void OnUpdate(float) override { ++debugUpdateCount; }
};

int eventHits = 0;
int deferredEventHits = 0;
float eventDamage = 0.0f;
engine::ecs::Entity eventSender = engine::ecs::kNull;

class EventListenerScript final : public engine::Script {
public:
    void OnCreate() override {
        ListenForEvent("combat.hit");
        ListenForEvent("combat.followup");
        ListenForEvent("health.damage");
        ListenForEvent("trigger.enter");
    }
    void OnEvent(const engine::ScriptEvent& event) override {
        if (event.name == "combat.hit") {
            ++eventHits;
            eventDamage = event.GetFloat("damage");
            eventSender = event.sender;
            PublishEvent("combat.followup", Self());
        } else if (event.name == "combat.followup") {
            ++deferredEventHits;
        } else if (event.name == "health.damage") {
            ++healthEventHits;
        } else if (event.name == "trigger.enter") {
            collisionOther = event.GetEntity("other");
        }
    }
    static int healthEventHits;
    static engine::ecs::Entity collisionOther;
};
int EventListenerScript::healthEventHits = 0;
engine::ecs::Entity EventListenerScript::collisionOther = engine::ecs::kNull;

class EventPublisherScript final : public engine::Script {
public:
    void OnUpdate(float) override {
        if (published) return;
        engine::ScriptEvent event;
        event.name = "combat.hit";
        event.floats["damage"] = 17.5f;
        PublishEvent(std::move(event));
        published = true;
    }
private:
    bool published = false;
};

std::vector<std::string> orderedCallbacks;
class OrderProviderScript final : public engine::Script {
public:
    void OnCreate() override { orderedCallbacks.push_back("provider.create"); }
    void OnUpdate(float) override { orderedCallbacks.push_back("provider.update"); }
    void OnFixedUpdate(float) override { orderedCallbacks.push_back("provider.fixed"); }
};
class OrderConsumerScript final : public engine::Script {
public:
    void OnCreate() override { orderedCallbacks.push_back("consumer.create"); }
    void OnUpdate(float) override { orderedCallbacks.push_back("consumer.update"); }
    void OnFixedUpdate(float) override { orderedCallbacks.push_back("consumer.fixed"); }
};

float scriptCallResult = 0.0f;
bool scriptHandleStayedValid = false;
class CallableTargetScript final : public engine::Script {
public:
    void OnCreate() override {
        BindScriptFunction("Double", [](const engine::ScriptEvent& arguments,
                                         engine::ScriptEvent& result) {
            result.floats["value"] = arguments.GetFloat("value") * 2.0f;
            return true;
        });
    }
};
class CallableCallerScript final : public engine::Script {
public:
    void OnUpdate(float) override {
        if (!target) target = FindScript(Self(), "CallableTargetRegressionScript");
        scriptHandleStayedValid = IsScriptValid(target);
        engine::ScriptEvent arguments;
        arguments.floats["value"] = 6.5f;
        engine::ScriptEvent result;
        if (CallScript(target, "Double", std::move(arguments), &result))
            scriptCallResult = result.GetFloat("value");
    }
private:
    engine::ScriptHandle target;
};

int activationTargetEnabled = 0;
int activationTargetDisabled = 0;
int activationTargetUpdated = 0;
bool activationHandleValidWhileDisabled = false;
bool activationControlsSucceeded = false;

class ActivationTargetScript final : public engine::Script {
public:
    void OnEnable() override { ++activationTargetEnabled; }
    void OnDisable() override { ++activationTargetDisabled; }
    void OnUpdate(float) override { ++activationTargetUpdated; }
};

class ActivationControllerScript final : public engine::Script {
public:
    void OnUpdate(float) override {
        if (!target) target = FindScript(Self(), "ActivationTargetRegressionScript");
        if (phase == 0) {
            activationControlsSucceeded = IsScriptEnabled(target)
                && SetScriptEnabled(target, false);
            ++phase;
        } else if (phase == 1) {
            activationHandleValidWhileDisabled = IsScriptValid(target)
                && !IsScriptEnabled(target);
            activationControlsSucceeded = activationControlsSucceeded
                && SetScriptEnabled(target, true);
            ++phase;
        }
    }
private:
    int phase = 0;
    engine::ScriptHandle target;
};

int sequenceFirstActions = 0;
int sequenceDelayedActions = 0;
int sequenceCancelledActions = 0;
int sequenceResumedActions = 0;

class SequenceRegressionScript final : public engine::Script {
public:
    void OnCreate() override {
        mainHandle = Sequence()
            .Do([] { ++sequenceFirstActions; })
            .Wait(0.2f)
            .Do([] { ++sequenceDelayedActions; })
            .Handle();
        const int cancelled = Sequence()
            .Wait(0.01f)
            .Do([] { ++sequenceCancelledActions; })
            .Handle();
        CancelSequence(cancelled);
        pausedHandle = Sequence()
            .Wait(0.1f)
            .Do([] { ++sequenceResumedActions; })
            .Handle();
        PauseSequence(pausedHandle);
    }

    bool ResumeHeldSequence() { return ResumeSequence(pausedHandle); }
    bool MainSequenceActive() const { return IsSequenceActive(mainHandle); }
    bool HeldSequencePaused() const { return IsSequencePaused(pausedHandle); }

private:
    int mainHandle = 0;
    int pausedHandle = 0;
};

int persistentLoadedVersion = 0;
int persistentLoadedCoins = 0;
bool persistentLoadAfterCreate = false;

class PersistentStateRegressionScript final : public engine::Script {
public:
    void OnCreate() override { created = true; }
    int PersistentStateVersion() const override { return 2; }
    void OnSaveState(StateMap& state) const override {
        state["coins"] = "37";
        state["spell"] = "arcane_bolt";
    }
    void OnLoadState(int savedVersion, const StateMap& state) override {
        persistentLoadAfterCreate = created;
        persistentLoadedVersion = savedVersion;
        const auto coins = state.find("coins");
        persistentLoadedCoins = coins == state.end() ? -1 : std::stoi(coins->second);
    }
private:
    bool created = false;
};

class ScriptTestFrameworkRegressionScript final : public engine::Script {
public:
    void DefineTests(engine::ScriptTestSuite& suite) override {
        suite.Add("01 Mutates isolated world", [this](engine::ScriptTestContext& test) {
            engine::ecs::Transform* transform = Transform();
            test.Expect(transform != nullptr, "temporary test entity has a transform");
            if (transform) transform->position.x = 9.0f;
        });
        suite.Add("02 Starts with a clean world", [this](engine::ScriptTestContext& test) {
            const engine::ecs::Transform* transform = Transform();
            test.Expect(transform != nullptr && transform->position.x == 0.0f,
                        "each test receives a fresh registry and entity");
            test.ExpectNear(1.001f, 1.0f, 0.01f,
                            "near comparison accepts values within tolerance");
        });
        suite.Add("03 Reports assertion failures", [](engine::ScriptTestContext& test) {
            test.Expect(false, "intentional native test failure");
        });
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
    scripts.Register("NamedTimerRegressionScript", [] {
        return std::make_unique<NamedTimerScript>();
    });
    scripts.Register("HotReloadRegressionScript", [] {
        return std::make_unique<HotReloadOldScript>();
    });
    scripts.Register("DebugCallbackRegressionScript", [] {
        return std::make_unique<DebugCallbackScript>();
    });
    scripts.Register("EventListenerRegressionScript", [] {
        return std::make_unique<EventListenerScript>();
    });
    scripts.Register("EventPublisherRegressionScript", [] {
        return std::make_unique<EventPublisherScript>();
    });
    scripts.Register("OrderProviderRegressionScript", [] {
        return std::make_unique<OrderProviderScript>();
    });
    scripts.Register("OrderConsumerRegressionScript", [] {
        return std::make_unique<OrderConsumerScript>();
    });
    scripts.Register("CallableTargetRegressionScript", [] {
        return std::make_unique<CallableTargetScript>();
    });
    scripts.Register("CallableCallerRegressionScript", [] {
        return std::make_unique<CallableCallerScript>();
    });
    scripts.Register("ActivationTargetRegressionScript", [] {
        return std::make_unique<ActivationTargetScript>();
    });
    scripts.Register("ActivationControllerRegressionScript", [] {
        return std::make_unique<ActivationControllerScript>();
    });
    scripts.Register("SequenceRegressionScript", [] {
        return std::make_unique<SequenceRegressionScript>();
    });
    scripts.Register("PersistentStateRegressionScript", [] {
        return std::make_unique<PersistentStateRegressionScript>();
    });
    scripts.Register("ScriptTestFrameworkRegressionScript", [] {
        return std::make_unique<ScriptTestFrameworkRegressionScript>();
    });

    {
        const std::filesystem::path luaTestPath =
            std::filesystem::current_path() / "lua_test_framework_regression.lua";
        {
            std::ofstream lua(luaTestPath, std::ios::trunc);
            lua << "ScriptTests = {\n"
                   "  ['Lua pass'] = function()\n"
                   "    Engine.TestAssert(true, 'true should pass')\n"
                   "    Engine.TestExpectNear(2.001, 2.0, 0.01, 'near should pass')\n"
                   "  end,\n"
                   "  ['Lua fail'] = function()\n"
                   "    Engine.TestAssert(false, 'intentional lua test failure')\n"
                   "  end\n"
                   "}\n";
        }

        const std::vector<engine::ScriptTestResult> results =
            engine::RunScriptTests({luaTestPath.string()});
        const auto findResult = [&results](const std::string& suite,
                                           const std::string& name) {
            return std::find_if(results.begin(), results.end(),
                [&suite, &name](const engine::ScriptTestResult& result) {
                    return result.suite == suite && result.name == name;
                });
        };
        const auto isolated = findResult(
            "ScriptTestFrameworkRegressionScript", "02 Starts with a clean world");
        Check(isolated != results.end() && isolated->passed
                && isolated->assertions == 2,
              "native script tests run with isolated worlds and assertion counts");
        const auto nativeFailure = findResult(
            "ScriptTestFrameworkRegressionScript", "03 Reports assertion failures");
        Check(nativeFailure != results.end() && !nativeFailure->passed
                && !nativeFailure->failures.empty()
                && nativeFailure->failures.front() == "intentional native test failure",
              "native script test failures retain their diagnostic message");
        const auto luaPass = findResult("lua_test_framework_regression", "Lua pass");
        Check(luaPass != results.end() && luaPass->passed
                && luaPass->assertions == 2,
              "Lua ScriptTests run through the same assertion framework");
        const auto luaFailure = findResult("lua_test_framework_regression", "Lua fail");
        Check(luaFailure != results.end() && !luaFailure->passed
                && !luaFailure->failures.empty()
                && luaFailure->failures.front() == "intentional lua test failure",
              "Lua assertion failures are reported without aborting the test run");
        std::error_code cleanupError;
        std::filesystem::remove(luaTestPath, cleanupError);
    }

    {
        engine::ScriptRegistry incoming;
        incoming.Register("CandidateOnly", [] {
            return std::make_unique<LifecycleScript>();
        });
        engine::ScriptRegistry active;
        active.Register("KeepActive", [] {
            return std::make_unique<LifecycleScript>();
        });
        active.MergeFrom(std::move(incoming));
        Check(active.Has("KeepActive") && active.Has("CandidateOnly"),
              "transactional registry merge preserves active and candidate factories");
        active.Remove("CandidateOnly");
        Check(active.Has("KeepActive") && !active.Has("CandidateOnly"),
              "project factory removal does not clear unrelated factories");
    }

    {
        engine::ecs::Registry registry;
        const engine::ecs::Entity entity = registry.Create();
        engine::NativeScriptComponent component = Component("HotReloadRegressionScript");
        component.fields.push_back(
            {"power", engine::ScriptField::Type::Float, "7.5"});
        registry.Add<engine::NativeScriptComponent>(entity, std::move(component));
        engine::UpdateScripts(registry, 0.016f);
        engine::PrepareScriptsForHotReload(registry);

        auto* saved = registry.TryGet<engine::NativeScriptComponent>(entity);
        Check(hotReloadOldDestroyed == 1 && saved && !saved->instance && !saved->created,
              "hot reload destroys the old live instance before its module unloads");
        Check(saved && saved->fields.size() == 1 && saved->fields[0].value == "7.5",
              "hot reload preserves authored and live-exposed field values");

        scripts.Register("HotReloadRegressionScript", [] {
            return std::make_unique<HotReloadNewScript>();
        });
        engine::UpdateScripts(registry, 0.016f);
        Check(hotReloadRestoredTicks == 1,
              "replacement script receives custom transient hot-reload state");
        engine::ShutdownScripts(registry);
    }

    {
        engine::ecs::Registry registry;
        const engine::ecs::Entity entity = registry.Create();
        registry.Add<engine::NativeScriptComponent>(
            entity, Component("DebugCallbackRegressionScript"));
        engine::ClearScriptExecutionStatistics();
        engine::ClearScriptCallbackBreakpoints();
        engine::SetScriptDebuggingEnabled(true);
        engine::SetScriptCallbackBreakpoint("DebugCallbackRegressionScript",
            engine::ScriptCallbackKind::OnUpdate, true);

        engine::UpdateScripts(registry, 0.016f);
        engine::ScriptDebugState state = engine::GetScriptDebugState();
        Check(debugUpdateCount == 1 && state.paused
                && state.stopReason.find("Breakpoint") != std::string::npos,
              "script callback breakpoint pauses after a safe update boundary");
        engine::UpdateScripts(registry, 0.016f);
        Check(debugUpdateCount == 1,
              "paused script debugger suppresses subsequent callbacks");
        engine::RequestScriptExecutionStep();
        engine::UpdateScripts(registry, 0.016f);
        state = engine::GetScriptDebugState();
        Check(debugUpdateCount == 2 && state.paused && !state.statistics.empty(),
              "script debugger steps one callback and records execution timing");

        engine::SetScriptExecutionPaused(false);
        engine::ClearScriptCallbackBreakpoints();
        engine::SetScriptDebuggingEnabled(false);
        engine::ShutdownScripts(registry);
    }

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
        Check(lifecycle.enabled == 1 && lifecycle.disabled == 0,
              "OnEnable runs once after initial creation");
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

        auto* liveComponent =
            registry.TryGet<engine::NativeScriptComponent>(entity);
        liveComponent->enabled = false;
        engine::UpdateScripts(registry, 0.25f, &frameInput);
        Check(lifecycle.disabled == 1 && lifecycle.updated == 2
                && !liveComponent->active,
              "disabling a live script calls OnDisable once and stops updates");
        engine::UpdateScripts(registry, 0.25f, &frameInput);
        Check(lifecycle.disabled == 1,
              "an inactive script does not repeat OnDisable every frame");
        liveComponent->enabled = true;
        engine::UpdateScripts(registry, 0.25f, &frameInput);
        Check(lifecycle.created == 1 && lifecycle.enabled == 2
                && lifecycle.updated == 3 && liveComponent->active,
              "re-enabling calls OnEnable without recreating the script instance");

        engine::ShutdownScripts(registry);
        const auto* component =
            registry.TryGet<engine::NativeScriptComponent>(entity);
        Check(lifecycle.destroyed == 1, "ShutdownScripts calls OnDestroy once");
        Check(lifecycle.disabled == 2,
              "shutdown calls OnDisable before destroying an active script");
        Check(lifecycle.destroySawDisabled,
              "OnDisable is ordered before OnDestroy during teardown");
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
        orderedCallbacks.clear();
        engine::ecs::Registry registry;
        const engine::ecs::Entity entity = registry.Create();
        engine::NativeScriptComponent scriptsOnEntity;
        scriptsOnEntity.className = "OrderConsumerRegressionScript";
        scriptsOnEntity.executionOrder = -100;
        scriptsOnEntity.dependencies = {"OrderProviderRegressionScript"};
        engine::NativeScriptSlot provider;
        provider.className = "OrderProviderRegressionScript";
        provider.executionOrder = 100;
        scriptsOnEntity.additional.push_back(std::move(provider));
        registry.Add<engine::NativeScriptComponent>(entity, std::move(scriptsOnEntity));

        const engine::ecs::Entity blockedEntity = registry.Create();
        engine::NativeScriptComponent blocked = Component("OrderConsumerRegressionScript");
        blocked.executionOrder = -200;
        blocked.dependencies = {"MissingRegressionScript"};
        registry.Add<engine::NativeScriptComponent>(blockedEntity, std::move(blocked));

        engine::UpdateScripts(registry, 0.016f);
        const std::vector<std::string> expectedFrame = {
            "provider.create", "provider.update", "consumer.create", "consumer.update"};
        Check(orderedCallbacks == expectedFrame,
              "script dependencies override numeric priority for create and update order");
        const auto& blockedRuntime = registry.Get<engine::NativeScriptComponent>(blockedEntity);
        Check(!blockedRuntime.created && !blockedRuntime.dependencyError.empty(),
              "scripts with missing dependencies stay inactive and expose a diagnostic");

        engine::FixedUpdateScripts(registry, 0.02f);
        Check(orderedCallbacks.size() == 6
                && orderedCallbacks[4] == "provider.fixed"
                && orderedCallbacks[5] == "consumer.fixed",
              "fixed script callbacks use the same deterministic dependency order");
        engine::ShutdownScripts(registry);
    }

    {
        scriptCallResult = 0.0f;
        scriptHandleStayedValid = false;
        engine::ecs::Registry registry;
        const engine::ecs::Entity entity = registry.Create();
        engine::NativeScriptComponent caller;
        caller.className = "CallableCallerRegressionScript";
        caller.dependencies = {"CallableTargetRegressionScript"};
        engine::NativeScriptSlot target;
        target.className = "CallableTargetRegressionScript";
        caller.additional.push_back(std::move(target));
        registry.Add<engine::NativeScriptComponent>(entity, std::move(caller));
        engine::UpdateScripts(registry, 0.016f);
        Check(scriptHandleStayedValid && scriptCallResult == 13.0f,
              "safe script handles resolve attached classes and return typed call results");
        engine::UpdateScripts(registry, 0.016f);
        Check(scriptHandleStayedValid && scriptCallResult == 13.0f,
              "script handles remain valid across later callback passes");
        engine::ShutdownScripts(registry);
    }

    {
        activationTargetEnabled = 0;
        activationTargetDisabled = 0;
        activationTargetUpdated = 0;
        activationHandleValidWhileDisabled = false;
        activationControlsSucceeded = false;
        engine::ecs::Registry registry;
        const engine::ecs::Entity entity = registry.Create();
        engine::NativeScriptComponent controller;
        controller.className = "ActivationControllerRegressionScript";
        controller.executionOrder = 100;
        engine::NativeScriptSlot target;
        target.className = "ActivationTargetRegressionScript";
        target.executionOrder = 0;
        controller.additional.push_back(std::move(target));
        registry.Add<engine::NativeScriptComponent>(entity, std::move(controller));

        engine::UpdateScripts(registry, 0.016f);
        engine::UpdateScripts(registry, 0.016f);
        engine::UpdateScripts(registry, 0.016f);
        Check(activationControlsSucceeded && activationHandleValidWhileDisabled,
              "script handles can inspect and re-enable a disabled attached script");
        Check(activationTargetEnabled == 2 && activationTargetDisabled == 1
                && activationTargetUpdated == 2,
              "runtime activation controls deliver balanced lifecycle transitions");
        engine::ShutdownScripts(registry);
    }

    {
        sequenceFirstActions = 0;
        sequenceDelayedActions = 0;
        sequenceCancelledActions = 0;
        sequenceResumedActions = 0;
        engine::ecs::Registry registry;
        const engine::ecs::Entity entity = registry.Create();
        registry.Add<engine::NativeScriptComponent>(
            entity, Component("SequenceRegressionScript"));

        engine::UpdateScripts(registry, 0.05f);
        auto& component = registry.Get<engine::NativeScriptComponent>(entity);
        auto* sequenceScript =
            dynamic_cast<SequenceRegressionScript*>(component.instance.get());
        Check(sequenceFirstActions == 1 && sequenceDelayedActions == 0
                && sequenceCancelledActions == 0 && sequenceScript
                && sequenceScript->MainSequenceActive()
                && sequenceScript->HeldSequencePaused(),
              "script sequences expose handles and support pause and cancellation");

        component.enabled = false;
        engine::UpdateScripts(registry, 1.0f);
        Check(sequenceDelayedActions == 0,
              "script sequences automatically suspend while their owner is disabled");
        component.enabled = true;
        engine::UpdateScripts(registry, 0.05f);
        engine::UpdateScripts(registry, 0.11f);
        Check(sequenceDelayedActions == 1 && sequenceCancelledActions == 0,
              "reactivation resumes sequence timing without running cancelled work");
        Check(sequenceScript->ResumeHeldSequence(),
              "a paused sequence can be resumed by its stable handle");
        engine::UpdateScripts(registry, 0.11f);
        Check(sequenceResumedActions == 1,
              "resumed sequence actions run after their remaining wait");
        engine::ShutdownScripts(registry);
    }

    {
        persistentLoadedVersion = 0;
        persistentLoadedCoins = 0;
        persistentLoadAfterCreate = false;
        engine::ecs::Registry source;
        const engine::ecs::Entity oldEntity = source.Create();
        source.Add<engine::ecs::RuntimeName>(
            oldEntity, engine::ecs::RuntimeName{"PersistentHero"});
        source.Add<engine::NativeScriptComponent>(
            oldEntity, Component("PersistentStateRegressionScript"));
        engine::UpdateScripts(source, 0.016f);
        std::unordered_map<std::string, std::string> savedValues;
        engine::CaptureScriptPersistentStates(source, savedValues);
        Check(!savedValues.empty(),
              "full save capture includes versioned state from live named scripts");
        engine::ShutdownScripts(source);

        engine::ecs::Registry restored;
        // Deliberately allocate a throwaway first so the restored entity id differs.
        restored.Create();
        const engine::ecs::Entity newEntity = restored.Create();
        restored.Add<engine::ecs::RuntimeName>(
            newEntity, engine::ecs::RuntimeName{"PersistentHero"});
        restored.Add<engine::NativeScriptComponent>(
            newEntity, Component("PersistentStateRegressionScript"));
        engine::RestoreScriptPersistentStates(restored, savedValues);
        engine::UpdateScripts(restored, 0.016f);
        Check(persistentLoadAfterCreate && persistentLoadedVersion == 2
                && persistentLoadedCoins == 37,
              "script state restores by stable entity name after OnCreate with its saved version");
        engine::ShutdownScripts(restored);
    }

    {
        eventHits = 0;
        deferredEventHits = 0;
        EventListenerScript::healthEventHits = 0;
        EventListenerScript::collisionOther = engine::ecs::kNull;
        engine::ecs::Registry registry;
        const engine::ecs::Entity listener = registry.Create();
        const engine::ecs::Entity publisher = registry.Create();
        registry.Add<engine::Health>(listener);
        registry.Add<engine::NativeScriptComponent>(
            listener, Component("EventListenerRegressionScript"));
        registry.Add<engine::NativeScriptComponent>(
            publisher, Component("EventPublisherRegressionScript"));

        engine::UpdateScripts(registry, 0.016f);
        Check(eventHits == 1 && eventDamage == 17.5f && eventSender == publisher,
              "queued typed events broadcast after the script update pass");
        Check(deferredEventHits == 0,
              "events published by an event handler are deferred to the next frame");

        engine::CollisionEvent collision;
        collision.a = listener;
        collision.b = publisher;
        collision.trigger = true;
        collision.phase = engine::CollisionEvent::Phase::Enter;
        engine::QueueScriptCollisionEvents(registry, {collision});
        registry.Get<engine::Health>(listener).hp -= 10.0f;
        engine::UpdateScripts(registry, 0.016f);
        Check(deferredEventHits == 1,
              "deferred handler events arrive on the following update");
        Check(EventListenerScript::healthEventHits == 1,
              "health changes emit targeted gameplay events");
        Check(EventListenerScript::collisionOther == publisher,
              "physics overlap events include the other entity");
        engine::ShutdownScripts(registry);
    }

    {
        const std::filesystem::path luaStatePath =
            std::filesystem::current_path() / "lua_state_regression.lua";
        {
            std::ofstream lua(luaStatePath, std::ios::trunc);
            lua << "local counter = 21\n"
                   "function PersistentStateVersion() return 4 end\n"
                   "function OnSaveState() return { counter = counter } end\n"
                   "function OnLoadState(version, state)\n"
                   "  Engine.SetPosition(tonumber(state.counter) or -1, version, 0)\n"
                   "end\n";
        }

        std::unordered_map<std::string, std::string> savedValues;
        engine::ecs::Registry source;
        const engine::ecs::Entity sourceEntity = source.Create();
        source.Add<engine::ecs::RuntimeName>(
            sourceEntity, engine::ecs::RuntimeName{"LuaPersistentHero"});
        source.Add<engine::ecs::Transform>(sourceEntity);
        engine::NativeScriptComponent sourceScript;
        sourceScript.className = "LuaPersistentRegression";
        sourceScript.sourcePath = luaStatePath.string();
        source.Add<engine::NativeScriptComponent>(sourceEntity, std::move(sourceScript));
        engine::UpdateScripts(source, 0.016f);
        engine::CaptureScriptPersistentStates(source, savedValues);
        engine::ShutdownScripts(source);

        engine::ecs::Registry restored;
        const engine::ecs::Entity restoredEntity = restored.Create();
        restored.Add<engine::ecs::RuntimeName>(
            restoredEntity, engine::ecs::RuntimeName{"LuaPersistentHero"});
        restored.Add<engine::ecs::Transform>(restoredEntity);
        engine::NativeScriptComponent restoredScript;
        restoredScript.className = "LuaPersistentRegression";
        restoredScript.sourcePath = luaStatePath.string();
        restored.Add<engine::NativeScriptComponent>(
            restoredEntity, std::move(restoredScript));
        engine::RestoreScriptPersistentStates(restored, savedValues);
        engine::UpdateScripts(restored, 0.016f);
        const engine::ecs::Transform& restoredTransform =
            restored.Get<engine::ecs::Transform>(restoredEntity);
        Check(restoredTransform.position.x == 21.0f
                && restoredTransform.position.y == 4.0f,
              "Lua persistent-state tables restore with the saved schema version");
        engine::ShutdownScripts(restored);
        std::error_code cleanupError;
        std::filesystem::remove(luaStatePath, cleanupError);
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
                   "  Engine.ListenForEvent(\"lua.ping\")\n"
                   "  Engine.PublishEvent(\"lua.ping\", Engine.Self(), { amount = 4 })\n"
                   "  local flow = Engine.CreateSequence()\n"
                   "  Engine.SequenceDo(flow, \"FlowStart\")\n"
                   "  Engine.SequenceWait(flow, 0.1)\n"
                   "  Engine.SequenceDo(flow, \"FlowEnd\")\n"
                   "end\n"
                   "function OnEnable()\n"
                   "  Engine.Translate(0, 1, 0)\n"
                   "end\n"
                   "function OnDisable()\n"
                   "  Engine.Translate(0, -1, 0)\n"
                   "end\n"
                   "function Pulse()\n"
                   "  Engine.Translate(1, 0, 0)\n"
                   "end\n"
                   "function FlowStart()\n"
                   "  Engine.Translate(1, 0, 0)\n"
                   "end\n"
                   "function FlowEnd()\n"
                   "  Engine.Translate(0, 0, 1)\n"
                   "end\n"
                   "function OnUpdate(dt)\n"
                   "  Engine.Translate(dt, 0, 0)\n"
                   "  local target = Engine.FindScript(Engine.Self(), \"LuaRegression\")\n"
                   "  local handled, value = Engine.CallScript(target, \"Double\", { amount = 2 })\n"
                   "  if handled then Engine.Translate(value, 0, 0) end\n"
                   "end\n"
                   "function Double(sender, target)\n"
                   "  return true, Engine.EventFloat(\"amount\", 0) * 2\n"
                   "end\n"
                   "function OnFixedUpdate(dt)\n"
                   "  Engine.Translate(0, dt, 0)\n"
                   "end\n"
                   "function OnEvent(name, sender, target)\n"
                   "  if name == \"lua.ping\" then\n"
                   "    Engine.Translate(0, 0, Engine.EventFloat(\"amount\", 0))\n"
                   "  end\n"
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
        Check(afterUpdate.position.x == 11.25f
                && afterUpdate.position.y == 3.0f
                && afterUpdate.position.z == 8.0f,
              "Lua scripts run sequences, timers, events, and safe typed script calls");
        Check(gameMode.GlobalTimeDilation() == 0.5f
                && gameMode.HitStopActive()
                && gameMode.EffectiveTimeDilation() == 0.0f,
              "Lua scripts control global dilation and hit stop");

        engine::FixedUpdateScripts(registry, 0.5f);
        Check(registry.Get<engine::ecs::Transform>(entity).position.y == 3.5f,
              "Lua scripts receive fixed updates through the native lifecycle");

        engine::ShutdownScripts(registry);
        Check(registry.Get<engine::ecs::Transform>(entity).scale.x == 2.0f
                && registry.Get<engine::ecs::Transform>(entity).position.y == 2.5f,
              "Lua scripts receive OnDisable and OnDestroy before their VM is released");
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
