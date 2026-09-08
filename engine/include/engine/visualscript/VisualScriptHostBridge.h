#pragma once

// =============================================================================
// Visual Scripting — host bridge (ties Passes 1–5 into the play loop).
//
// One object the editor Play loop and the standalone Player own. It bundles the
// VisualScriptRuntime, a graph asset cache + resolver, node registration, and
// the per-frame service plumbing, so the host integration is a handful of calls:
//
//   bridge.Startup();                                  // once at init
//   bridge.SetPathResolver([&](AssetHandle h){...});   // handle -> ".3dgvs" path
//   ... each render frame while playing ...
//   bridge.Update(reg, dt, physics, input, audio, shake, director, gameMode);
//   bridge.DeliverPhysicsEvents(reg, physics.Events());
//   ... each fixed step ...
//   bridge.FixedUpdate(reg, dt, physics, input, audio, shake, director, gameMode);
//   ... on stop ...
//   bridge.Shutdown(reg);
//
// It is engine-side and GL/ImGui-free, so the Player links it without any editor
// graph code (Phase 21).
// =============================================================================

#include "VisualScriptRuntime.h"
#include "VisualScriptReflection.h"      // RegisterVisualScriptPass2
#include "VisualScriptGameplayNodes.h"   // RegisterVisualScriptGameplay
#include "VisualScriptDiagnostics.h"

#include <engine/gameplay/Script.h>      // ScriptEvent, CollisionEvent, QueueScriptEvent

#include <functional>
#include <string>
#include <unordered_map>

namespace engine::vs {

class VisualScriptHostBridge {
public:
    // Register the node library (gameplay descriptors BEFORE the function-node generator) and wire
    // the asset resolver. Idempotent — safe to call once per editor/player launch.
    void Startup() {
        if (m_started) return;
        m_started = true;
        RegisterVisualScriptGameplay();   // gameplay descriptors + invoker bindings
        RegisterVisualScriptPass2();      // core + reflected + generated function nodes
        m_runtime.SetAssetProvider([this](const AssetHandle& h) { return Resolve(h); });
    }

    // Host supplies AssetHandle -> ".3dgvs" absolute path (via its AssetRegistry). The bridge loads
    // and caches the parsed graph; cache node pointers are stable for the runtime.
    void SetPathResolver(std::function<std::string(const AssetHandle&)> resolver) {
        m_pathResolver = std::move(resolver);
    }

    VisualScriptRuntime& Runtime() { return m_runtime; }

    void SetServices(PhysicsWorld* physics, const ScriptInputState* input,
                     RuntimeAudioSystem* audio, CameraShake* shake,
                     CameraDirector* director, GameMode* gameMode) {
        m_runtime.SetServices(physics, input);
        m_runtime.SetGameplayServices(audio, shake, director, gameMode);
    }

    void Update(ecs::Registry& reg, float dt, PhysicsWorld* physics, const ScriptInputState* input,
                RuntimeAudioSystem* audio, CameraShake* shake, CameraDirector* director, GameMode* gameMode) {
        SetServices(physics, input, audio, shake, director, gameMode);
        m_runtime.Update(reg, dt);
        // Custom events published by graphs this frame: deliver to graphs (VS<->VS) and forward to the
        // shared ScriptEvent bus so native/Lua listeners also receive them (Phase 4/12).
        for (const ScriptEvent& ev : m_runtime.PublishedEvents()) {
            m_runtime.DeliverEventTo(reg, ev.target, ev);
            QueueScriptEvent(reg, ev);
        }
        m_runtime.ClearPublishedEvents();
    }

    void FixedUpdate(ecs::Registry& reg, float dt, PhysicsWorld* physics, const ScriptInputState* input,
                     RuntimeAudioSystem* audio, CameraShake* shake, CameraDirector* director, GameMode* gameMode) {
        SetServices(physics, input, audio, shake, director, gameMode);
        m_runtime.FixedUpdate(reg, dt);
    }

    // Stage physics collision/trigger events to the touched entities' graphs (Phase 9/11). Event names
    // match the schema-authored entry nodes: (Collision|Trigger)(Enter|Stay|Exit) with Other/Point/Normal.
    void DeliverPhysicsEvents(ecs::Registry& reg, const std::vector<CollisionEvent>& events) {
        for (const CollisionEvent& e : events) {
            const char* name =
                e.trigger ? (e.phase == CollisionEvent::Phase::Enter ? "TriggerEnter"
                           : e.phase == CollisionEvent::Phase::Exit  ? "TriggerExit" : "TriggerStay")
                          : (e.phase == CollisionEvent::Phase::Enter ? "CollisionEnter"
                           : e.phase == CollisionEvent::Phase::Exit  ? "CollisionExit" : "CollisionStay");
            ScriptEvent forA; forA.name = name; forA.sender = e.a; forA.target = e.a;
            forA.entities["Other"] = e.b; forA.vectors["Point"] = e.point;
            forA.vectors["Normal"] = e.normal; forA.floats["Impulse"] = e.impulse;
            m_runtime.DeliverEventTo(reg, e.a, forA);
            ScriptEvent forB = forA; forB.sender = e.b; forB.target = e.b; forB.entities["Other"] = e.a;
            m_runtime.DeliverEventTo(reg, e.b, forB);
        }
    }

    void Shutdown(ecs::Registry& reg) {
        m_runtime.Shutdown(reg);
        m_cache.clear();
    }

    // Transactional hot reload from disk (Pass 5). Loads + validates the candidate; only swaps if valid.
    bool HotReloadFromFile(ecs::Registry& reg, const AssetHandle& handle, const std::string& path, std::string* error) {
        VisualScriptAsset candidate;
        if (!candidate.Load(path, error)) return false;
        VisualScriptAsset& slot = m_cache[handle];
        VisualScriptAsset previous = slot;   // keep known-good for rollback
        slot = candidate;
        if (!m_runtime.ReloadGraph(reg, handle, &slot, error)) { slot = previous; return false; }
        return true;
    }

private:
    const VisualScriptAsset* Resolve(const AssetHandle& handle) {
        auto it = m_cache.find(handle);
        if (it != m_cache.end()) return &it->second;
        if (!m_pathResolver) return nullptr;
        const std::string path = m_pathResolver(handle);
        if (path.empty()) return nullptr;
        VisualScriptAsset asset; std::string error;
        if (!asset.Load(path, &error)) return nullptr;
        auto res = m_cache.emplace(handle, std::move(asset));
        return &res.first->second;
    }

    bool m_started = false;
    VisualScriptRuntime m_runtime;
    std::unordered_map<AssetHandle, VisualScriptAsset, AssetHandleHash> m_cache;
    std::function<std::string(const AssetHandle&)> m_pathResolver;
};

} // namespace engine::vs
