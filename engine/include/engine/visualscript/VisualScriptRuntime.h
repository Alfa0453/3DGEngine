#pragma once

// =============================================================================
// Visual Scripting — Pass 1 : the runtime manager (Phase 12).
//
// A tightly-integrated visual-script runtime intended to be OWNED BY THE HOST
// alongside the existing native/Lua ScriptWorld — NOT a separate lifecycle
// manager, ECS, or event bus. The host (editor Play loop / standalone Player)
// creates one, gives it an asset provider (RuntimeAssetManager), and calls:
//     Begin/Update  right after UpdateScripts(...)          — render frame
//     FixedUpdate   right after FixedUpdateScripts(...)      — fixed step
//     Shutdown      when leaving Play / unloading the scene
//
// It resolves .3dgvs graphs through the provider only — no editor objects, no
// ImGui, no source-project paths (Phase 22/23).
// =============================================================================

#include "VisualScriptComponent.h"
#include "VisualScriptInstance.h"
#include "VisualScriptAsset.h"
#include "VisualScriptCompiler.h"   // Milestone 9: compiled graph cache
#include "VisualScriptValidator.h"

#include <engine/ecs/Registry.h>

#include <algorithm>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace engine::vs {

class VisualScriptRuntime {
public:
    // Resolves a graph AssetHandle to a loaded, cached VisualScriptAsset. The
    // host wires this to RuntimeAssetManager. Returning nullptr is reported as a
    // recoverable "missing asset" error, never a crash.
    using AssetProvider = std::function<const VisualScriptAsset*(const AssetHandle&)>;

    void SetAssetProvider(AssetProvider provider) { m_provider = std::move(provider); }

    // Pass 2: the host supplies the engine services visual-script nodes may use. Physics query and
    // input nodes borrow these; they are re-pointed each tick because they are frame-scoped.
    void SetServices(PhysicsWorld* physics, const ScriptInputState* input) {
        m_physics = physics; m_input = input;
    }
    // Pass 4: forward the same gameplay hosts native/Lua scripts get, so Script-host gameplay nodes
    // drive the real combat/ability/inventory/dialogue/audio/... runtime.
    void SetGameplayServices(RuntimeAudioSystem* audio, CameraShake* cameraShake,
                             CameraDirector* cameraDirector, GameMode* gameMode) {
        m_audio = audio; m_cameraShake = cameraShake; m_cameraDirector = cameraDirector; m_gameMode = gameMode;
    }

    // Render-frame tick: creates/binds instances, runs Begin once, then Update.
    void Update(ecs::Registry& registry, float dt) {
        PruneDeadInstances(registry);
        VisualScriptDiagnostics& diag = VisualScriptDiagnostics::Instance();
        if (diag.enabled) diag.FrameReset();
        if (!diag.paused) m_elapsed += dt;   // Milestone 5: play clock (respects pause + dt dilation)
        registry.view<VisualScriptComponent>().each(
            [&](ecs::Entity entity, VisualScriptComponent& component) {
                if (!component.enabled || !component.graph.Valid()) return;
                VisualScriptInstance* instance = EnsureBound(registry, entity, component);
                if (!instance || !instance->Valid()) return;
                instance->SetServices(m_physics, m_input, &m_outbox, &m_structural);
                instance->SetPlayTime(m_elapsed);
                instance->SetGameplayHosts(m_audio, m_cameraShake, m_cameraDirector, m_gameMode);
                instance->SetTargetedOutbox(&m_targeted);   // Milestone 6
                if (diag.enabled) {   // Phase 13/14 instance accounting
                    GraphProfile& gp = diag.Graph(component.graph);
                    ++gp.instances;
                    gp.latentActive += instance->ActiveContinuations();
                }
                if (diag.paused) return;   // Phase 7: breakpoint pause halts advancement safely
                instance->ResumeExecution(registry, dt);
                if (diag.paused || instance->HasError()) { CaptureError(*instance); return; }
                instance->Begin(registry);
                if (!instance->HasError() && !diag.paused) instance->Update(registry, dt);
                if (!instance->HasError() && !diag.paused) instance->TickStateMachines(registry, dt);   // Milestone 7
                if (!instance->HasError() && !diag.paused) instance->TickLatent(registry, dt);   // Pass 4 latent resume
                CaptureError(*instance);
            });
        FlushStructural(registry);
        DrainTargeted(registry);
    }

    // Fixed-step tick: only fully-created, non-errored instances run.
    void FixedUpdate(ecs::Registry& registry, float dt) {
        registry.view<VisualScriptComponent>().each(
            [&](ecs::Entity entity, VisualScriptComponent& component) {
                if (!component.enabled || !component.graph.Valid()) return;
                auto it = m_instances.find(entity);
                if (it == m_instances.end() || !it->second.Valid() || it->second.HasError()) return;
                it->second.SetServices(m_physics, m_input, &m_outbox, &m_structural);
                it->second.SetGameplayHosts(m_audio, m_cameraShake, m_cameraDirector, m_gameMode);
                it->second.SetTargetedOutbox(&m_targeted);   // Milestone 6
                VisualScriptDiagnostics& diag = VisualScriptDiagnostics::Instance();
                if (diag.paused) return;
                it->second.ResumeExecution(registry, dt);
                if (!it->second.HasError() && !diag.paused) it->second.FixedUpdate(registry, dt);
                if (!it->second.HasError() && !diag.paused) it->second.TickLatentFixed(registry, dt, 1);
                CaptureError(it->second);
            });
        FlushStructural(registry);
        DrainTargeted(registry);
    }

    // Phase 11/12: deliver one received ScriptEvent to every bound graph at the SAFE script-event
    // phase. The host calls this from the same place native/Lua scripts receive events — never from
    // a physics worker. Collision events arrive here after the engine stages them.
    void DeliverEvent(ecs::Registry& registry, const ScriptEvent& event) {
        for (auto& kv : m_instances) {
            if (!kv.second.Valid() || kv.second.HasError()) continue;
            kv.second.SetServices(m_physics, m_input, &m_outbox, &m_structural);
            kv.second.SetGameplayHosts(m_audio, m_cameraShake, m_cameraDirector, m_gameMode);
            kv.second.SetTargetedOutbox(&m_targeted);   // Milestone 6
            VisualScriptDiagnostics& diag = VisualScriptDiagnostics::Instance();
            if (diag.paused) continue;
            kv.second.ResumeExecution(registry, 0.0f);
            if (!diag.paused) kv.second.DispatchEvent(registry, event);
            CaptureError(kv.second);
        }
        FlushStructural(registry);
        DrainTargeted(registry);
    }

    // Targeted delivery — only the graph on `entity` receives it (kNull broadcasts). Used for physics
    // collision/trigger events so a TriggerEnter fires on the touched entity's graph, not every graph.
    void DeliverEventTo(ecs::Registry& registry, ecs::Entity entity, const ScriptEvent& event) {
        if (entity == ecs::kNull) { DeliverEvent(registry, event); return; }
        auto it = m_instances.find(entity);
        if (it == m_instances.end() || !it->second.Valid() || it->second.HasError()) return;
        it->second.SetServices(m_physics, m_input, &m_outbox, &m_structural);
        it->second.SetGameplayHosts(m_audio, m_cameraShake, m_cameraDirector, m_gameMode);
        it->second.SetTargetedOutbox(&m_targeted);   // Milestone 6
        VisualScriptDiagnostics& diag = VisualScriptDiagnostics::Instance();
        if (!diag.paused) it->second.ResumeExecution(registry, 0.0f);
        if (!diag.paused) it->second.DispatchEvent(registry, event);
        CaptureError(it->second);
        FlushStructural(registry);
        DrainTargeted(registry);
    }

    // Events a graph published this frame (via the Publish Event node). The host drains these and
    // forwards them to the existing QueueScriptEvent queue so native/Lua/visual all share one bus.
    std::vector<ScriptEvent>& PublishedEvents() { return m_outbox; }
    void ClearPublishedEvents() { m_outbox.clear(); }

    // Transactional hot reload (Phase 16/17/18): validate the candidate first; only migrate live
    // instances if it is valid, so a broken edit never destroys the known-good running graph.
    bool ReloadGraph(ecs::Registry& /*registry*/, const AssetHandle& handle,
                     const VisualScriptAsset* newAsset, std::string* error) {
        if (!newAsset) { if (error) *error = "Hot reload: candidate graph is null."; return false; }
        const ValidationReport report = ValidateGraph(*newAsset);
        if (!report.Ok()) {
            if (error) {
                *error = "Hot reload rejected: graph has validation errors";
                for (const ValidationIssue& i : report.issues)
                    if (i.severity == ValidationIssue::Severity::Error) { *error += " — " + i.message; break; }
            }
            return false;   // keep the known-good running graph
        }
        // Milestone 9: recompile the acceleration structure in place (its address is stable, so live
        // instances that point at it stay valid) before migrating instances onto the new asset.
        CompiledGraph& cg = m_compiledCache[handle];
        cg.Compile(newAsset);
        for (auto& kv : m_instances)
            if (kv.second.Graph() == handle) { kv.second.HotReload(handle, newAsset); kv.second.SetCompiled(&cg); }
        return true;
    }

    // OnDestroy equivalent — drop every instance (called when leaving Play). This also releases every
    // latent continuation, event linkage and graph reference held by those instances (Phase 24).
    void Shutdown(ecs::Registry&) {
        m_instances.clear();
        m_missingAssetReported.clear();
        m_targeted.clear();
        m_compiledCache.clear();   // Milestone 9
        m_elapsed = 0.0;
    }

    // Milestone 9: compiled-graph stats for the editor (nodes/links resolved, missing descriptors).
    const CompiledGraph* CompiledFor(const AssetHandle& handle) const {
        auto it = m_compiledCache.find(handle);
        return it == m_compiledCache.end() ? nullptr : &it->second;
    }

    const std::vector<VisualScriptError>& RecentErrors() const { return m_errors; }
    void ClearErrors() { m_errors.clear(); }
    std::size_t InstanceCount() const { return m_instances.size(); }

private:
    VisualScriptInstance* EnsureBound(ecs::Registry& /*registry*/, ecs::Entity entity,
                                      const VisualScriptComponent& component) {
        VisualScriptInstance& instance = m_instances[entity];
        // (Re)bind only when the referenced graph changes — avoids re-resolving
        // (and error-spamming) the same handle every frame.
        if (instance.Graph() != component.graph || (!instance.Valid() && !m_missingAssetReported.count(entity))) {
            const VisualScriptAsset* asset = m_provider ? m_provider(component.graph) : nullptr;
            instance.Bind(entity, component.graph, asset, component.variableOverrides);
            // Milestone 3: cross-graph function-library calls resolve other .3dgvs through the same
            // provider that resolves this graph.
            instance.SetFunctionResolver(m_provider);
            if (!asset) {
                if (m_missingAssetReported.insert(entity).second) {
                    VisualScriptError error;
                    error.active = true;
                    error.entity = entity;
                    error.asset = component.graph;
                    error.message = "Visual script graph asset could not be resolved.";
                    m_errors.push_back(error);
                    if (auto& handler = VisualScriptErrorHandlerRef()) handler(error);
                }
                return &instance;
            }
            m_missingAssetReported.erase(entity);
            // Milestone 9: compile (or reuse cached) the acceleration structure for this graph.
            CompiledGraph& cg = m_compiledCache[component.graph];
            if (!cg.MatchesCurrent(asset)) cg.Compile(asset);
            instance.SetCompiled(&cg);
        }
        return &instance;
    }

    void CaptureError(const VisualScriptInstance& instance) {
        if (!instance.HasError()) return;
        // Record once per (entity,node,message).
        const VisualScriptError& e = instance.Error();
        for (const VisualScriptError& seen : m_errors)
            if (seen.entity == e.entity && seen.nodeId == e.nodeId && seen.message == e.message) return;
        m_errors.push_back(e);
    }

    // Apply queued structural changes AFTER view iteration, then flush the registry's own deferred
    // structural command buffer so spawns/adds/removes/destroys take effect at a safe point.
    void FlushStructural(ecs::Registry& registry) {
        if (m_structural.empty()) return;
        std::vector<std::function<void(ecs::Registry&)>> pending;
        pending.swap(m_structural);
        for (auto& op : pending) op(registry);
        registry.FlushStructuralCommands();
    }

    void PruneDeadInstances(ecs::Registry& registry) {
        for (auto it = m_instances.begin(); it != m_instances.end();) {
            if (!registry.Valid(it->first) || !registry.TryGet<VisualScriptComponent>(it->first)) {
                m_missingAssetReported.erase(it->first);
                it = m_instances.erase(it);
            } else {
                ++it;
            }
        }
    }

    AssetProvider m_provider;
    std::unordered_map<ecs::Entity, VisualScriptInstance> m_instances;
    std::unordered_map<AssetHandle, CompiledGraph, AssetHandleHash> m_compiledCache;   // Milestone 9
    std::unordered_set<ecs::Entity> m_missingAssetReported;
    std::vector<VisualScriptError> m_errors;
    PhysicsWorld* m_physics = nullptr;
    const ScriptInputState* m_input = nullptr;
    RuntimeAudioSystem* m_audio = nullptr;
    CameraShake* m_cameraShake = nullptr;
    CameraDirector* m_cameraDirector = nullptr;
    GameMode* m_gameMode = nullptr;
    // Milestone 6: VS->VS targeted events, drained inside the runtime (never through a worker thread).
    void DrainTargeted(ecs::Registry& registry) {
        VisualScriptDiagnostics& diag = VisualScriptDiagnostics::Instance();
        int guard = 0;
        while (!m_targeted.empty() && guard < 8192) {
            std::vector<std::pair<ecs::Entity, ScriptEvent>> pending;
            pending.swap(m_targeted);
            for (auto& item : pending) {
                if (++guard >= 8192) break;
                auto it = m_instances.find(item.first);
                if (it == m_instances.end() || !it->second.Valid() || it->second.HasError()) continue;
                it->second.SetServices(m_physics, m_input, &m_outbox, &m_structural);
                it->second.SetGameplayHosts(m_audio, m_cameraShake, m_cameraDirector, m_gameMode);
                it->second.SetTargetedOutbox(&m_targeted);
                if (diag.paused) continue;
                it->second.ResumeExecution(registry, 0.0f);
                if (!diag.paused) it->second.DispatchEvent(registry, item.second);
                CaptureError(it->second);
            }
        }
        FlushStructural(registry);
    }

    std::vector<ScriptEvent> m_outbox;
    std::vector<std::function<void(ecs::Registry&)>> m_structural;
    std::vector<std::pair<ecs::Entity, ScriptEvent>> m_targeted;   // Milestone 6
    double m_elapsed = 0.0;   // Milestone 5: monotonic play clock for Now()/cooldown nodes
};

} // namespace engine::vs
