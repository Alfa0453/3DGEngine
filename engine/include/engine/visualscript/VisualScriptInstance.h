#pragma once

// =============================================================================
// Visual Scripting — Pass 1 : runtime instance + bounded interpreter.
//
//   * VisualScriptInstance holds ONLY transient runtime state (Phase 11):
//       owning entity, graph handle, resolved asset ptr, variable values,
//       per-callback node outputs, error state. No serialized pointers.
//   * Deterministic exec flow with an instruction budget + depth guard so a
//     bad graph can never hang the editor or overflow the stack (Phase 17/19).
//   * Pure data pins evaluated on demand and memoized per callback (Phase 18).
//   * Rich runtime error record: entity / asset / node id / type / message (Phase 20).
// =============================================================================

#include "VisualNodeRegistry.h"
#include "VisualScriptComponent.h"
#include "VisualScriptAsset.h"
#include "VisualScriptCompiler.h"      // Milestone 9: compiled acceleration structure
#include "VisualScriptDiagnostics.h"   // Pass 5: strippable profiler/debugger
#include "VisualScriptTypes.h"

#include <engine/ecs/Registry.h>
#include <engine/gameplay/Script.h>   // Pass 2: ScriptEvent / ScriptInputState / PhysicsWorld

#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace engine::vs {

// A concrete Script used purely as a call surface for gameplay nodes. Script's gameplay helpers are
// protected, so we expose thin PUBLIC wrappers (a subclass may access its base's protected members).
// Every wrapper forwards to the existing runtime — no gameplay logic is reimplemented here.
struct VisualScriptHost final : engine::Script {
    // Ability / combat
    bool VsGrantAbility(const std::string& p) { return GrantAbility(p); }
    bool VsActivateAbility(const std::string& n, ecs::Entity t = ecs::kNull) { return ActivateAbility(n, t); }
    bool VsCancelAbility() { return CancelAbility(); }
    bool VsIsAbilityActive(const std::string& n) const { return IsAbilityActive(n); }
    bool VsStartCombat(ecs::Entity t = ecs::kNull) { return StartCombat(t); }
    std::string VsDealCombatDamage(ecs::Entity t, float d, const std::string& type) { return DealCombatDamage(t, d, type); }
    // Inventory
    bool VsAddItem(const std::string& p, int c) { return AddItem(p, c); }
    int  VsRemoveItem(const std::string& n, int c) { return RemoveItem(n, c); }
    bool VsHasItem(const std::string& n, int c) const { return HasItem(n, c); }
    int  VsItemCount(const std::string& n) const { return ItemCount(n); }
    // Quest / dialogue
    bool VsStartQuest(const std::string& n) { return StartQuest(n); }
    bool VsAdvanceQuest(const std::string& n, const std::string& o, int a) { return AdvanceQuest(n, o, a); }
    bool VsStartDialogue(const std::string& p) { return StartDialogue(p); }
    bool VsChooseDialogue(int i) { return ChooseDialogue(i); }
    bool VsContinueDialogue() { return ContinueDialogue(); }
    // Audio / particles / camera
    bool VsPlayAudio() { return PlayAudio(false); }
    bool VsStopAudio() { return StopAudio(); }
    bool VsPlayAudioCue(const std::string& p) { return PlayAudioCue(p, true); }
    bool VsPlayParticles() { return PlayParticles(false); }
    bool VsStopParticles() { return StopParticles(false); }
    bool VsBurstParticles(int n) { return BurstParticles(n); }
    bool VsShakeCamera(float i, float d, float f) { return ShakeCamera(i, d, f); }
    // Animation
    bool VsPlayAnimationAction(const std::string& clip) { return PlayAnimationAction(clip); }
    bool VsSetAnimationParameter(const std::string& n, float v) { return SetAnimationParameter(n, v); }
    bool VsSetAnimationTrigger(const std::string& n) { return SetAnimationTrigger(n); }
    bool VsIsAnimationActionPlaying() const { return IsAnimationActionPlaying(); }
    // Scene / level / save
    void VsRequestSceneLoad(const std::string& p) { RequestSceneLoad(p); }
    void VsRequestLevelLoad(const std::string& l) { RequestLevelLoad(l); }
    void VsSaveGameToSlot(int s) { SaveGameToSlot(s); }
    bool VsSaveCheckpoint(const std::string& n, const glm::vec3& p) { return SaveCheckpoint(n, p); }
};

// ---- Runtime error (Phase 20) ----------------------------------------------
struct VisualScriptError {
    bool        active = false;
    ecs::Entity entity = ecs::kNull;
    AssetHandle asset;
    NodeId      nodeId = kInvalidNodeId;
    std::string nodeTypeId;
    std::string message;
};

inline std::function<void(const VisualScriptError&)>& VisualScriptErrorHandlerRef() {
    static std::function<void(const VisualScriptError&)> handler;
    return handler;
}
inline void SetVisualScriptErrorHandler(std::function<void(const VisualScriptError&)> handler) {
    VisualScriptErrorHandlerRef() = std::move(handler);
}

// ---- Runtime safeguards ----------------------------------------------------
inline constexpr int kMaxOpsPerCallback = 100000; // Phase 19: infinite-loop guard
inline constexpr int kMaxExecDepth      = 4096;    // stack-overflow guard

class VisualScriptInstance final : public INodeContext {
public:
    void Bind(ecs::Entity entity, AssetHandle handle, const VisualScriptAsset* asset,
              const std::vector<VisualScriptVariableOverride>& overrides = {}) {
        m_entity = entity;
        m_handle = handle;
        m_asset = asset;
        m_compiled = nullptr;   // Milestone 9: runtime re-attaches the compiled graph after Bind
        m_began = false;
        m_error = VisualScriptError{};
        m_variables.clear();
        m_variableNamesById.clear();
        m_continuations.clear();
        m_execStack.clear();
        m_activeFunctions.clear();
        m_functionReturnValues.clear();
        m_localScopes.clear();
        m_nodeState.clear();
        m_unboundEvents.clear();
        m_smCurrentState.clear();
        m_smTransitionReady.clear();
        m_functionDepth = 0;
        if (asset) for (const VisualVariable& v : asset->variables) {
            m_variables[v.name] = v.defaultValue;
            m_variableNamesById[v.id] = v.name;   // stable id -> current name (Phase 12)
        }
        if (asset) for (const VisualScriptVariableOverride& overrideValue : overrides) {
            const VisualVariable* variable = asset->FindVariable(overrideValue.variableId);
            if (!variable || !variable->exposed || overrideValue.value.type != variable->type) continue;
            m_variables[variable->name] = overrideValue.value;
        }
    }

    // Pass 5 (Phase 16/17/18): swap to a validated new graph in place, migrating runtime variable
    // values by stable VariableId + type compatibility, and cancelling active latent flows. Keeps
    // m_began so BeginPlay does not re-run. The caller must have validated `newAsset` first.
    void HotReload(const AssetHandle& handle, const VisualScriptAsset* newAsset) {
        // Snapshot old values keyed by VariableId (id -> current value).
        std::unordered_map<std::uint32_t, VisualValue> oldById;
        for (const auto& idName : m_variableNamesById) {
            auto v = m_variables.find(idName.second);
            if (v != m_variables.end()) oldById[idName.first] = v->second;
        }
        m_handle = handle;
        m_asset = newAsset;
        m_variables.clear();
        m_variableNamesById.clear();
        if (newAsset) for (const VisualVariable& var : newAsset->variables) {
            m_variableNamesById[var.id] = var.name;
            auto old = oldById.find(var.id);
            if (old == oldById.end()) { m_variables[var.name] = var.defaultValue; continue; }   // new var -> default
            if (old->second.type == var.type) m_variables[var.name] = old->second;               // compatible -> keep
            else {                                                                                // incompatible -> default + warn
                m_variables[var.name] = var.defaultValue;
                if (auto& log = VisualScriptLogHandlerRef())
                    log("Hot reload: variable '" + var.name + "' type changed; reset to default");
            }
        }
        // Removed variables are simply not re-seeded (discarded). Cancel active latent flows (Phase 18).
        m_continuations.clear();
        m_execStack.clear();
        m_activeFunctions.clear();
        m_functionReturnValues.clear();
        m_localScopes.clear();
        m_nodeState.clear();
        m_smCurrentState.clear();
        m_smTransitionReady.clear();
        m_functionDepth = 0;
        m_error = VisualScriptError{};
    }

    bool Valid() const { return m_asset != nullptr; }
    bool HasError() const { return m_error.active; }
    const VisualScriptError& Error() const { return m_error; }
    ecs::Entity OwningEntity() const { return m_entity; }
    AssetHandle Graph() const { return m_handle; }
    void ClearError() { m_error = VisualScriptError{}; }

    // ---- instance inspection (Pass 5, Phase 10/14 — copied values, no live refs) ----
    bool Began() const { return m_began; }
    std::size_t ActiveContinuations() const { return m_continuations.size(); }
    std::vector<std::pair<std::string, VisualValue>> VariableSnapshot() const {
        std::vector<std::pair<std::string, VisualValue>> out;
        out.reserve(m_variables.size());
        for (const auto& kv : m_variables) out.push_back(kv);
        return out;
    }
    bool HasPendingExecution() const { return !m_execStack.empty(); }
    void ResumeExecution(ecs::Registry& registry, float dt) {
        if (m_execStack.empty() || m_error.active) return;
        m_registry = &registry;
        m_deltaTime = dt;
        m_opsRemaining = kMaxOpsPerCallback;
        m_evaluating.clear();
        DrainExecution();
    }

    // ---- Pass 2 services (set by the runtime each tick; may be null) -------
    void SetServices(PhysicsWorld* physics, const ScriptInputState* input,
                     std::vector<ScriptEvent>* eventOutbox,
                     std::vector<std::function<void(ecs::Registry&)>>* structuralQueue) {
        m_physics = physics; m_input = input;
        m_eventOutbox = eventOutbox; m_structuralQueue = structuralQueue;
    }
    // Gameplay hosts forwarded from the host's ScriptContext (audio / camera / game mode), so the
    // Script host can drive the full gameplay API. All optional.
    void SetGameplayHosts(RuntimeAudioSystem* audio, CameraShake* cameraShake,
                          CameraDirector* cameraDirector, GameMode* gameMode) {
        m_audio = audio; m_cameraShake = cameraShake; m_cameraDirector = cameraDirector; m_gameMode = gameMode;
    }

    // ---- Lifecycle entry points (Phase 14) ---------------------------------
    void Begin(ecs::Registry& registry) {
        if (m_began) return;
        m_began = true;
        RunEvent(registry, 0.0f, "Event.BeginPlay");
    }
    void Update(ecs::Registry& registry, float dt) { RunEvent(registry, dt, "Event.Update"); }
    void FixedUpdate(ecs::Registry& registry, float dt) { RunEvent(registry, dt, "Event.FixedUpdate"); }

    // Deliver a received ScriptEvent to matching event-entry nodes (Phase 10/11/12). Called by the
    // runtime at the SAFE script-event phase — never from a physics worker.
    void DispatchEvent(ecs::Registry& registry, const ScriptEvent& event) {
        if (!m_asset || m_error.active) return;
        // Milestone 6: an unbound event name is ignored by this instance (dispatcher lifetime control).
        if (!m_unboundEvents.empty() && m_unboundEvents.count(event.name)) return;
        m_registry = &registry;
        m_deltaTime = 0.0f;
        m_opsRemaining = kMaxOpsPerCallback;
        m_depth = 0;
        m_nodeOutputs.clear();
        m_evaluating.clear();
        VisualScriptDiagnostics& diag = VisualScriptDiagnostics::Instance();
        for (const VisualNode& node : m_asset->nodes) {
            const NodeDescriptor* desc = DescriptorFor(&node);
            if (!desc) continue;
            // Reflected event nodes match on descriptor name; user "On Custom Event" nodes match on
            // the authored event name stored in the node's "eventName" property (Milestone 6).
            bool match = !desc->eventName.empty() && desc->eventName == event.name;
            if (!match && node.typeId == "Event.Custom") {
                auto it = node.properties.find("eventName");
                match = it != node.properties.end() && it->second.AsString() == event.name;
            }
            if (!match) continue;
            if (diag.enabled) diag.RecordEventDispatch(event.sender, m_entity, event.name, false, NowSeconds());
            SeedEventOutputs(node, event);
            ExecuteNode(node.id);
            if (m_error.active || diag.paused) return;
        }
        // Resume any Wait-For-Event continuations matching this event (Phase 5/12).
        std::vector<Continuation> ready;
        for (auto it = m_continuations.begin(); it != m_continuations.end();) {
            if (it->kind == LatentKind::WaitEvent && it->eventName == event.name) {
                ready.push_back(*it); it = m_continuations.erase(it);
            } else ++it;
        }
        for (const Continuation& c : ready) { ResumeFrom(registry, 0.0f, c.waitNode, c.resumePin); if (m_error.active) return; }
    }

    // ---- INodeContext ------------------------------------------------------
    const VisualNode& Node() const override { return *m_currentNode; }
    ecs::Registry* Registry() const override { return m_registry; }
    ecs::Entity Entity() const override { return m_entity; }
    float DeltaTime() const override { return m_deltaTime; }

    VisualValue ReadInput(const std::string& pinName) override {
        if (!m_currentNode) return VisualValue::Float(0.0f);
        const VisualPin* pin = FindPin(m_currentNode->inputs, pinName);
        if (!pin) return VisualValue::Float(0.0f);
        VisualValue raw;
        if (const VisualLink* link = FindLinkTo(m_currentNode->id, pin->id))
            raw = EvaluateOutput(link->fromNode, link->fromPin);
        else
            raw = pin->defaultValue;
        if (raw.type == pin->type) return raw;
        VisualValue converted;
        if (raw.ConvertTo(pin->type, &converted)) return converted;
        return pin->defaultValue;   // incompatible: fall back rather than crash
    }

    void WriteOutput(const std::string& pinName, VisualValue value) override {
        if (!m_currentNode) return;
        if (const VisualPin* pin = FindPin(m_currentNode->outputs, pinName))
            m_nodeOutputs[m_currentNode->id][pin->id] = std::move(value);
    }

    void Continue(const std::string& execPinName) override {
        if (!m_currentNode || !m_currentTriggered) return;   // pure nodes cannot branch flow
        if (const VisualPin* pin = FindPin(m_currentNode->outputs, execPinName))
            if (pin->kind == PinKind::Exec) m_currentTriggered->push_back(pin->id);
    }

    VisualValue GetVariable(const std::string& name) override {
        auto it = m_variables.find(name);
        return it == m_variables.end() ? VisualValue::Float(0.0f) : it->second;
    }
    void SetVariable(const std::string& name, VisualValue value) override {
        m_variables[name] = std::move(value);
    }
    VisualValue GetVariableById(std::uint32_t variableId) override {
        // Function-local scope (Milestone 3) shadows member variables while a function runs.
        if (!m_localScopes.empty()) {
            auto l = m_localScopes.back().find(variableId);
            if (l != m_localScopes.back().end()) return l->second;
        }
        auto n = m_variableNamesById.find(variableId);
        if (n == m_variableNamesById.end()) return VisualValue::Float(0.0f);
        auto v = m_variables.find(n->second);
        return v == m_variables.end() ? VisualValue::Float(0.0f) : v->second;
    }
    void SetVariableById(std::uint32_t variableId, VisualValue value) override {
        if (!m_localScopes.empty()) {
            auto& scope = m_localScopes.back();
            auto l = scope.find(variableId);
            if (l != scope.end()) { l->second = std::move(value); return; }
        }
        auto n = m_variableNamesById.find(variableId);
        if (n != m_variableNamesById.end()) m_variables[n->second] = std::move(value);
    }

    VisualValue Property(const std::string& name, const VisualValue& fallback) override {
        if (!m_currentNode) return fallback;
        auto it = m_currentNode->properties.find(name);
        return it == m_currentNode->properties.end() ? fallback : it->second;
    }

    void Log(const std::string& message) override {
        if (auto& handler = VisualScriptLogHandlerRef()) handler(message);
        else std::cout << "[VisualScript] " << message << '\n';
    }

    void Fail(const std::string& message) override {
        SetError(m_currentNode, message);
    }

    PhysicsWorld* Physics() const override { return m_physics; }
    const ScriptInputState* Input() const override { return m_input; }
    void PublishEvent(const ScriptEvent& event) override {
        if (m_eventOutbox) m_eventOutbox->push_back(event);   // drained by the host to the safe queue
        VisualScriptDiagnostics& diag = VisualScriptDiagnostics::Instance();
        if (diag.enabled) diag.RecordEventDispatch(m_entity, ecs::kNull, event.name, true, NowSeconds());
    }
    void PublishEventTo(ecs::Entity target, const ScriptEvent& event) override {
        if (target == ecs::kNull) { PublishEvent(event); return; }   // no target => broadcast
        if (m_targetedOutbox) m_targetedOutbox->push_back({target, event});
        VisualScriptDiagnostics& diag = VisualScriptDiagnostics::Instance();
        if (diag.enabled) diag.RecordEventDispatch(m_entity, target, event.name, false, NowSeconds());
    }
    void BindEvent(const std::string& eventName) override { m_unboundEvents.erase(eventName); }
    void UnbindEvent(const std::string& eventName) override { m_unboundEvents.insert(eventName); }
    void SetTargetedOutbox(std::vector<std::pair<ecs::Entity, ScriptEvent>>* box) { m_targetedOutbox = box; }

    // Event builder (keeps ScriptEvent out of the node executors).
    void BeginEvent(const std::string& name) override {
        m_buildingEvent = ScriptEvent{};
        m_buildingEvent.name = name;
        m_buildingEvent.sender = m_entity;
    }
    void EventArg(const std::string& key, const VisualValue& value) override {
        switch (value.type) {
            case ValueType::Bool:         m_buildingEvent.bools[key] = value.AsBool(); break;
            case ValueType::Int:
            case ValueType::Enum:         m_buildingEvent.ints[key] = value.AsInt(); break;
            case ValueType::Float:        m_buildingEvent.floats[key] = value.AsFloat(); break;
            case ValueType::String:       m_buildingEvent.strings[key] = value.AsString(); break;
            case ValueType::Vector3:      m_buildingEvent.vectors[key] = value.AsVec3(); break;
            case ValueType::Vector2:      m_buildingEvent.vectors[key] = glm::vec3(value.AsVec2(), 0.0f); break;
            case ValueType::Entity:
            case ValueType::ScriptHandle: m_buildingEvent.entities[key] = value.AsEntity(); break;
            default: break;   // containers/quaternion/color/asset are not carried on the event bus
        }
    }
    void SendEvent(ecs::Entity target, bool broadcast) override {
        m_buildingEvent.target = target;
        if (broadcast || target == ecs::kNull) PublishEvent(m_buildingEvent);
        else PublishEventTo(target, m_buildingEvent);
    }

    // Cast/validity: does an entity have a given component? (kNull => Self.)
    bool HasComponent(ecs::Entity entity, const std::string& componentName) override {
        if (!m_registry) return false;
        const ecs::Entity e = (entity == ecs::kNull) ? m_entity : entity;
        if (!m_registry->Valid(e)) return false;
        if (componentName == "Transform")    return m_registry->TryGet<ecs::Transform>(e) != nullptr;
        if (componentName == "RigidBody")    return m_registry->TryGet<ecs::RigidBody>(e) != nullptr;
        if (componentName == "MeshRenderer") return m_registry->TryGet<ecs::MeshRenderer>(e) != nullptr;
        if (componentName == "Light")        return m_registry->TryGet<ecs::Light>(e) != nullptr;
        if (componentName == "Collider")     return m_registry->TryGet<ecs::Collider>(e) != nullptr;
        return false;
    }

    // ---- Milestone 7: state-machine script API -----------------------------
    std::uint32_t GetStateMachineState(std::uint32_t smId) override {
        auto it = m_smCurrentState.find(smId);
        return it == m_smCurrentState.end() ? 0u : it->second;
    }
    void RequestStateChange(std::uint32_t smId, std::uint32_t stateId) override {
        const VisualStateMachine* sm = m_asset ? m_asset->FindStateMachine(smId) : nullptr;
        if (!sm) return;
        std::uint32_t& cur = m_smCurrentState[smId];
        if (const VisualState* from = FindState(*sm, cur)) CallVoidFunction(from->onExit);
        const std::uint32_t prev = cur;
        cur = stateId;
        if (const VisualState* to = FindState(*sm, cur)) CallVoidFunction(to->onEnter);
        VisualScriptDiagnostics& diag = VisualScriptDiagnostics::Instance();
        if (diag.enabled) diag.RecordStateChange(m_handle, smId, m_entity, prev, cur, m_playTime);
    }

    // Per-entity state-machine tick: run entry once, evaluate outgoing transitions by priority
    // (respecting cooldown), then run the current state's Update. Reuses the function-call engine.
    void TickStateMachines(ecs::Registry& registry, float dt) {
        if (!m_asset || m_error.active || m_asset->stateMachines.empty() || !m_execStack.empty()) return;
        m_registry = &registry; m_deltaTime = dt;
        m_opsRemaining = kMaxOpsPerCallback; m_depth = 0;
        m_nodeOutputs.clear(); m_evaluating.clear();
        VisualScriptDiagnostics& diag = VisualScriptDiagnostics::Instance();
        for (const VisualStateMachine& sm : m_asset->stateMachines) {
            std::uint32_t& cur = m_smCurrentState[sm.id];
            if (cur == 0) {
                cur = sm.entryState;
                if (const VisualState* s = FindState(sm, cur)) CallVoidFunction(s->onEnter);
                if (diag.enabled) diag.RecordStateChange(m_handle, sm.id, m_entity, 0, cur, m_playTime);
                if (m_error.active) return;
            }
            std::vector<const VisualTransition*> outs;
            for (const VisualTransition& t : sm.transitions) if (t.from == cur) outs.push_back(&t);
            std::sort(outs.begin(), outs.end(),
                      [](const VisualTransition* a, const VisualTransition* b) { return a->priority > b->priority; });
            for (const VisualTransition* t : outs) {
                const std::uint64_t key = (static_cast<std::uint64_t>(sm.id) << 32) | t->id;
                auto rit = m_smTransitionReady.find(key);
                if (rit != m_smTransitionReady.end() && m_playTime < rit->second) continue;   // cooldown
                if (CallBoolFunction(t->condition)) {
                    if (const VisualState* from = FindState(sm, cur)) CallVoidFunction(from->onExit);
                    const std::uint32_t prev = cur;
                    cur = t->to;
                    if (const VisualState* to = FindState(sm, cur)) CallVoidFunction(to->onEnter);
                    if (t->cooldown > 0.0f) m_smTransitionReady[key] = m_playTime + t->cooldown;
                    if (diag.enabled) diag.RecordStateChange(m_handle, sm.id, m_entity, prev, cur, m_playTime);
                    if (m_error.active) return;
                    break;   // one transition per tick
                }
                if (m_error.active) return;
            }
            if (const VisualState* s = FindState(sm, cur)) CallVoidFunction(s->onUpdate);
            if (m_error.active) return;
        }
    }
    void EnqueueStructural(std::function<void(ecs::Registry&)> op) override {
        if (m_structuralQueue && op) m_structuralQueue->push_back(std::move(op));
    }

    VisualScriptHost* ScriptHost() override {
        if (!m_registry) return nullptr;
        if (!m_scriptHost) m_scriptHost = std::make_unique<VisualScriptHost>();
        ScriptContext ctx;
        ctx.registry = m_registry;
        ctx.entity = m_entity;
        ctx.input = m_input;
        ctx.audio = m_audio;
        ctx.cameraShake = m_cameraShake;
        ctx.cameraDirector = m_cameraDirector;
        ctx.fields = &m_hostFields;
        ctx.gameMode = m_gameMode;
        ctx.physics = m_physics;
        m_scriptHost->SetContext(ctx);
        return m_scriptHost.get();
    }

    // ---- Milestone 3: function calls ---------------------------------------
    // The host supplies this so cross-graph (function-library) calls can resolve another .3dgvs.
    // Same-graph calls never need it. Never called from a worker thread.
    void SetFunctionResolver(std::function<const VisualScriptAsset*(const AssetHandle&)> resolver) {
        m_functionResolver = std::move(resolver);
    }

    void SetFunctionOutput(const std::string& name, VisualValue value) override {
        m_functionReturnValues[name] = std::move(value);
    }

    bool CallFunction(const AssetHandle& graph, FunctionId functionId,
                      const std::unordered_map<std::string, VisualValue>& inputs,
                      std::unordered_map<std::string, VisualValue>* outputs) override {
        const bool crossGraph = graph.Valid() && !(graph == m_handle);
        const VisualScriptAsset* callee = crossGraph
            ? (m_functionResolver ? m_functionResolver(graph) : nullptr) : m_asset;
        if (!callee) { Fail("Call Function: target graph not found"); return false; }
        const VisualFunction* fn = callee->FindFunction(functionId);
        if (!fn) { Fail("Call Function: function not found"); return false; }

        // Recursion + depth guards (a graph can never hang or overflow the stack).
        const std::uint64_t sig =
            (crossGraph ? graph.low : m_handle.low) ^
            (static_cast<std::uint64_t>(functionId) * 0x9e3779b97f4a7c15ull);
        if (m_activeFunctions.count(sig)) { Fail("Recursive function call: " + fn->name); return false; }
        if (m_functionDepth >= kMaxFunctionDepth) { Fail("Function call depth exceeded"); return false; }

        const VisualNode* entry = nullptr;
        for (const VisualNode& n : callee->nodes)
            if (n.functionId == functionId && n.typeId == "Function.Entry") { entry = &n; break; }
        if (!entry) { Fail("Function '" + fn->name + "' has no Entry node"); return false; }

        // ---- save the outer callback's interpreter scratch ----
        const VisualScriptAsset* savedAsset = m_asset;
        const AssetHandle savedHandle = m_handle;
        std::vector<ExecutionFrame> savedStack = std::move(m_execStack);
        auto savedOutputs = std::move(m_nodeOutputs);
        auto savedEval = std::move(m_evaluating);
        const VisualNode* savedCurrent = m_currentNode;
        std::vector<PinId>* savedTriggered = m_currentTriggered;
        auto savedReturns = std::move(m_functionReturnValues);
        const int savedDepth = m_depth;
        std::unordered_map<std::string, VisualValue> savedVars;
        std::unordered_map<std::uint32_t, std::string> savedNamesById;
        if (crossGraph) { savedVars = std::move(m_variables); savedNamesById = std::move(m_variableNamesById); }

        // ---- build the callee scope ----
        m_asset = callee;
        if (crossGraph) m_handle = graph;
        m_execStack.clear();
        m_nodeOutputs.clear();
        m_evaluating.clear();
        m_functionReturnValues.clear();
        m_depth = 0;
        if (crossGraph) {   // a library graph runs against its own member vars, not the caller's
            m_variables.clear(); m_variableNamesById.clear();
            for (const VisualVariable& v : callee->variables) {
                m_variables[v.name] = v.defaultValue; m_variableNamesById[v.id] = v.name;
            }
        }
        // Push a fresh function-local scope (id-keyed) — shadows member vars, never leaks into them.
        std::unordered_map<VariableId, VisualValue> localScope;
        for (const VisualVariable& v : fn->locals) localScope[v.id] = v.defaultValue;
        m_localScopes.push_back(std::move(localScope));
        // Seed the Entry node's data outputs from the call inputs (typed by pin).
        for (const VisualPin& pin : entry->outputs) {
            if (pin.kind != PinKind::Data) continue;
            auto it = inputs.find(pin.name);
            VisualValue value = (it != inputs.end()) ? it->second : pin.defaultValue;
            if (value.type != pin.type) { VisualValue conv; if (value.ConvertTo(pin.type, &conv)) value = conv; }
            m_nodeOutputs[entry->id][pin.id] = value;
        }

        m_activeFunctions.insert(sig);
        ++m_functionDepth;
        ExecuteNode(entry->id);   // synchronous — functions may not contain latent nodes
        --m_functionDepth;
        m_activeFunctions.erase(sig);
        m_localScopes.pop_back();   // drop the function-local scope
        const bool ok = !m_error.active;

        if (outputs) {
            outputs->clear();
            for (const VisualFunctionParam& p : fn->outputs) {
                auto it = m_functionReturnValues.find(p.name);
                (*outputs)[p.name] = (it != m_functionReturnValues.end())
                    ? it->second : VisualValue::MakeDefault(p.type);
            }
        }

        // ---- restore the outer scratch ----
        m_asset = savedAsset;
        m_handle = savedHandle;
        m_execStack = std::move(savedStack);
        m_nodeOutputs = std::move(savedOutputs);
        m_evaluating = std::move(savedEval);
        m_currentNode = savedCurrent;
        m_currentTriggered = savedTriggered;
        m_functionReturnValues = std::move(savedReturns);
        m_depth = savedDepth;
        if (crossGraph) { m_variables = std::move(savedVars); m_variableNamesById = std::move(savedNamesById); }
        return ok;
    }

    // Milestone 4: run a loop node's body branch synchronously, once, with a fresh evaluation memo
    // (so pure nodes recompute for this iteration) while preserving the loop node's own outputs
    // (Element/Index). Bounded by the shared op budget.
    bool RunLoopBody(const std::string& execPinName) override {
        if (!m_currentNode) return true;
        const NodeId loopNode = m_currentNode->id;
        const VisualPin* pin = FindPin(m_currentNode->outputs, execPinName);
        if (!pin) return true;
        const VisualLink* link = FindLinkFrom(loopNode, pin->id);
        if (!link) return true;   // empty loop body is fine

        std::vector<ExecutionFrame> savedStack = std::move(m_execStack);
        auto savedOutputs = std::move(m_nodeOutputs);   // cheap move; restored after the body runs
        auto savedEval = std::move(m_evaluating);
        const VisualNode* savedCurrent = m_currentNode;
        std::vector<PinId>* savedTriggered = m_currentTriggered;
        const int savedDepth = m_depth;

        // Copy only the loop node's own outputs (Element/Index) forward into the fresh body memo.
        std::unordered_map<PinId, VisualValue> loopOutputs;
        if (auto it = savedOutputs.find(loopNode); it != savedOutputs.end()) loopOutputs = it->second;
        m_execStack.clear();
        m_nodeOutputs.clear();
        m_nodeOutputs[loopNode] = std::move(loopOutputs);
        m_evaluating.clear();
        m_depth = 0;
        ExecuteNode(link->toNode);
        const bool ok = !m_error.active;

        m_execStack = std::move(savedStack);
        m_nodeOutputs = std::move(savedOutputs);
        m_evaluating = std::move(savedEval);
        m_currentNode = savedCurrent;
        m_currentTriggered = savedTriggered;
        m_depth = savedDepth;
        return ok;
    }

    // ---- Milestone 5: flow control + time ----------------------------------
    bool EnteredVia(const std::string& execInputName) override {
        if (!m_currentNode) return false;
        if (m_enteredPin == kInvalidPinId) return true;   // event/loop entry — treat as the default input
        const VisualPin* p = FindPin(m_currentNode->inputs, execInputName);
        return p && p->id == m_enteredPin;
    }
    VisualValue GetNodeState(const std::string& key, const VisualValue& fallback) override {
        if (!m_currentNode) return fallback;
        auto n = m_nodeState.find(m_currentNode->id);
        if (n == m_nodeState.end()) return fallback;
        auto k = n->second.find(key);
        return k == n->second.end() ? fallback : k->second;
    }
    void SetNodeState(const std::string& key, VisualValue value) override {
        if (m_currentNode) m_nodeState[m_currentNode->id][key] = std::move(value);
    }
    VisualValue ReadInputFresh(const std::string& pinName) override {
        // Force pure sources to recompute (loop conditions) while keeping THIS node's outputs.
        std::unordered_map<PinId, VisualValue> selfOut;
        if (m_currentNode) if (auto it = m_nodeOutputs.find(m_currentNode->id); it != m_nodeOutputs.end()) selfOut = it->second;
        m_nodeOutputs.clear(); m_evaluating.clear();
        if (m_currentNode) m_nodeOutputs[m_currentNode->id] = std::move(selfOut);
        return ReadInput(pinName);
    }
    double Now() const override { return m_playTime; }
    void SuspendTimer(float period, bool looping, const std::string& resumePin) override {
        if (!m_currentNode) return;
        const VisualPin* pin = FindPin(m_currentNode->outputs, resumePin);
        Continuation c;
        c.id = m_nextContinuationId++; c.kind = LatentKind::Timer;
        c.waitNode = m_currentNode->id; c.resumePin = pin ? pin->id : kInvalidPinId;
        c.remaining = period < 0.0001f ? 0.0001f : period; c.period = c.remaining; c.looping = looping;
        m_continuations.push_back(std::move(c));
    }
    void StartTimeline(float duration, bool loop, const std::string& alphaOutput,
                       const std::string& updatePin, const std::string& finishedPin) override {
        if (!m_currentNode || duration <= 0.0f) return;
        const VisualPin* up = FindPin(m_currentNode->outputs, updatePin);
        const VisualPin* fp = FindPin(m_currentNode->outputs, finishedPin);
        const VisualPin* ap = FindPin(m_currentNode->outputs, alphaOutput);
        // A node runs one timeline at a time — cancel any existing before starting.
        const NodeId n = m_currentNode->id;
        m_continuations.erase(std::remove_if(m_continuations.begin(), m_continuations.end(),
            [&](const Continuation& c) { return c.waitNode == n && c.kind == LatentKind::Timeline; }), m_continuations.end());
        Continuation c;
        c.id = m_nextContinuationId++; c.kind = LatentKind::Timeline;
        c.waitNode = n; c.duration = duration; c.elapsed = 0.0f; c.looping = loop;
        c.updatePin = up ? up->id : kInvalidPinId;
        c.finishedPin = fp ? fp->id : kInvalidPinId;
        c.alphaPin = ap ? ap->id : kInvalidPinId;
        m_continuations.push_back(std::move(c));
    }
    void CancelNodeLatent() override {
        if (!m_currentNode) return;
        const NodeId n = m_currentNode->id;
        m_continuations.erase(std::remove_if(m_continuations.begin(), m_continuations.end(),
            [&](const Continuation& c) { return c.waitNode == n; }), m_continuations.end());
    }
    void SetPlayTime(double seconds) { m_playTime = seconds; }

    // Milestone 9: the runtime hands the instance a compiled acceleration structure for its graph.
    void SetCompiled(const CompiledGraph* compiled) { m_compiled = compiled; }

    // ---- Milestone 8: async tasks ------------------------------------------
    int StartAsyncTask(const std::string& kind, float workSeconds, float timeout,
                       const std::string& completedPin, const std::string& failedPin,
                       const std::string& cancelledPin, const std::string& timedOutPin) override {
        if (!m_currentNode) return 0;
        auto pin = [&](const std::string& n) {
            const VisualPin* p = FindPin(m_currentNode->outputs, n); return p ? p->id : kInvalidPinId;
        };
        Continuation c;
        c.id = m_nextContinuationId++;
        c.kind = LatentKind::Task;
        c.waitNode = m_currentNode->id;
        c.resumePin = pin(completedPin);
        c.failedPin = pin(failedPin);
        c.cancelledPin = pin(cancelledPin);
        c.timedOutPin = pin(timedOutPin);
        c.remaining = workSeconds < 0.0f ? 0.0f : workSeconds;   // remaining work
        c.duration = c.remaining;                                 // original, for elapsed display
        c.timeoutRemaining = timeout > 0.0f ? timeout : -1.0f;
        c.eventName = kind;
        m_continuations.push_back(std::move(c));
        return m_continuations.back().id;
    }
    void CancelAsyncTask(int handle) override {
        for (Continuation& c : m_continuations) if (c.id == handle && c.kind == LatentKind::Task) { c.forceComplete = 3; return; }
    }
    void CompleteAsyncTask(int handle, bool success) override {
        for (Continuation& c : m_continuations) if (c.id == handle && c.kind == LatentKind::Task) { c.forceComplete = success ? 1 : 2; return; }
    }
    bool IsAsyncTaskActive(int handle) override {
        for (const Continuation& c : m_continuations) if (c.id == handle && c.kind == LatentKind::Task) return true;
        return false;
    }

    void SuspendLatent(LatentKind kind, float seconds, int fixedSteps,
                       const std::string& eventName, const std::string& resumePin) override {
        if (!m_currentNode) return;
        const VisualPin* pin = FindPin(m_currentNode->outputs, resumePin);
        Continuation cont;
        cont.id = m_nextContinuationId++;
        cont.kind = kind;
        cont.waitNode = m_currentNode->id;
        cont.resumePin = pin ? pin->id : kInvalidPinId;
        cont.remaining = seconds;
        cont.fixedStepsRemaining = fixedSteps;
        cont.eventName = eventName;
        m_continuations.push_back(std::move(cont));
    }

    // ---- Latent flow ticking (Pass 4, Phase 5/6) ---------------------------
    bool HasLatent() const { return !m_continuations.empty(); }

    // Render-frame tick: advance Delay/Timer timers, timelines, and re-test WaitUntil conditions.
    void TickLatent(ecs::Registry& registry, float dt) {
        if (m_continuations.empty() || m_error.active) return;
        VisualScriptDiagnostics& diag = VisualScriptDiagnostics::Instance();
        // A resume to run after we finish scanning (so we never mutate the list mid-iteration).
        struct Resume { NodeId node; PinId pin; bool timeline; PinId alphaPin; float alpha; };
        std::vector<Resume> resumes;
        std::vector<int> eraseIds;
        for (Continuation& c : m_continuations) {
            switch (c.kind) {
                case LatentKind::Delay:
                    c.remaining -= dt;
                    if (c.remaining <= 0.0f) { resumes.push_back({c.waitNode, c.resumePin, false, kInvalidPinId, 0.0f}); eraseIds.push_back(c.id); }
                    break;
                case LatentKind::WaitUntil:
                    if (EvaluateBoolInput(registry, c.waitNode, "Condition")) { resumes.push_back({c.waitNode, c.resumePin, false, kInvalidPinId, 0.0f}); eraseIds.push_back(c.id); }
                    break;
                case LatentKind::WaitAnimation: {
                    VisualScriptHost* h = ScriptHost();
                    if (!h || !h->VsIsAnimationActionPlaying()) { resumes.push_back({c.waitNode, c.resumePin, false, kInvalidPinId, 0.0f}); eraseIds.push_back(c.id); }
                    break;
                }
                case LatentKind::Timer:
                    c.remaining -= dt;
                    if (c.remaining <= 0.0f) {
                        resumes.push_back({c.waitNode, c.resumePin, false, kInvalidPinId, 0.0f});
                        if (c.looping) c.remaining += c.period; else eraseIds.push_back(c.id);
                    }
                    break;
                case LatentKind::Timeline: {
                    c.elapsed += dt;
                    const float alpha = c.duration > 0.0f ? std::min(c.elapsed / c.duration, 1.0f) : 1.0f;
                    resumes.push_back({c.waitNode, c.updatePin, true, c.alphaPin, alpha});
                    if (c.elapsed >= c.duration) {
                        resumes.push_back({c.waitNode, c.finishedPin, true, c.alphaPin, 1.0f});
                        if (c.looping) c.elapsed = 0.0f; else eraseIds.push_back(c.id);
                    }
                    break;
                }
                case LatentKind::Task: {
                    // Explicit completion/failure/cancel wins over timing (Milestone 8).
                    if (c.forceComplete == 1)      { resumes.push_back({c.waitNode, c.resumePin, false, kInvalidPinId, 0.0f}); eraseIds.push_back(c.id); break; }
                    if (c.forceComplete == 2)      { resumes.push_back({c.waitNode, c.failedPin, false, kInvalidPinId, 0.0f}); eraseIds.push_back(c.id); break; }
                    if (c.forceComplete == 3)      { resumes.push_back({c.waitNode, c.cancelledPin, false, kInvalidPinId, 0.0f}); eraseIds.push_back(c.id); break; }
                    c.elapsed += dt;
                    if (c.timeoutRemaining >= 0.0f) {
                        c.timeoutRemaining -= dt;
                        if (c.timeoutRemaining <= 0.0f) { resumes.push_back({c.waitNode, c.timedOutPin, false, kInvalidPinId, 0.0f}); eraseIds.push_back(c.id); break; }
                    }
                    c.remaining -= dt;
                    if (c.remaining <= 0.0f) { resumes.push_back({c.waitNode, c.resumePin, false, kInvalidPinId, 0.0f}); eraseIds.push_back(c.id); }
                    break;
                }
                default: break;   // WaitEvent handled in DispatchEvent; WaitFixedSteps in TickLatentFixed
            }
        }
        // Report still-active tasks to the debugger (rebuilt each frame when diagnostics are on).
        if (diag.enabled) {
            std::unordered_set<int> dead(eraseIds.begin(), eraseIds.end());
            for (const Continuation& c : m_continuations)
                if (c.kind == LatentKind::Task && !dead.count(c.id))
                    diag.ReportTask({m_handle, m_entity, c.id, c.eventName, c.elapsed,
                                     c.timeoutRemaining >= 0.0f ? (c.elapsed + c.timeoutRemaining) : 0.0f});
        }
        if (!eraseIds.empty()) {
            std::unordered_set<int> dead(eraseIds.begin(), eraseIds.end());
            m_continuations.erase(std::remove_if(m_continuations.begin(), m_continuations.end(),
                [&](const Continuation& c) { return dead.count(c.id) != 0; }), m_continuations.end());
        }
        for (const Resume& r : resumes) {
            if (r.timeline) ResumeTimeline(registry, dt, r.node, r.pin, r.alphaPin, r.alpha);
            else            ResumeFrom(registry, dt, r.node, r.pin);
            if (m_error.active) return;
        }
    }

    // Fixed-step tick: advance WaitForFixedSteps counters.
    void TickLatentFixed(ecs::Registry& registry, float dt, int steps) {
        if (m_continuations.empty() || m_error.active) return;
        std::vector<Continuation> ready;
        for (auto it = m_continuations.begin(); it != m_continuations.end();) {
            if (it->kind == LatentKind::WaitFixedSteps) {
                it->fixedStepsRemaining -= steps;
                if (it->fixedStepsRemaining <= 0) { ready.push_back(*it); it = m_continuations.erase(it); continue; }
            }
            ++it;
        }
        for (const Continuation& c : ready) { ResumeFrom(registry, dt, c.waitNode, c.resumePin); if (m_error.active) return; }
    }

private:
    struct Continuation {
        int         id = 0;
        LatentKind  kind = LatentKind::Delay;
        NodeId      waitNode = kInvalidNodeId;
        PinId       resumePin = kInvalidPinId;
        float       remaining = 0.0f;
        int         fixedStepsRemaining = 0;
        std::string eventName;
        // Milestone 5: timers + timelines
        bool        looping = false;
        float       period = 0.0f;
        float       duration = 0.0f;
        float       elapsed = 0.0f;
        PinId       updatePin = kInvalidPinId;
        PinId       finishedPin = kInvalidPinId;
        PinId       alphaPin = kInvalidPinId;
        // Milestone 8: async task outcome pins + control
        PinId       failedPin = kInvalidPinId;
        PinId       cancelledPin = kInvalidPinId;
        PinId       timedOutPin = kInvalidPinId;
        float       timeoutRemaining = -1.0f;   // <0 => no timeout
        int         forceComplete = 0;          // 0 none, 1 success, 2 fail, 3 cancel (set by API)
    };

    struct ExecutionFrame {
        NodeId node = kInvalidNodeId;
        std::vector<CallFrame> callStack;
        PinId  enteredPin = kInvalidPinId;   // which exec INPUT pin triggered this node (Milestone 5)
    };

    // Resume execution from a suspended node's exec output (fresh evaluation pass).
    void ResumeFrom(ecs::Registry& registry, float dt, NodeId waitNode, PinId resumePin) {
        m_registry = &registry;
        m_deltaTime = dt;
        m_opsRemaining = kMaxOpsPerCallback;
        m_depth = 0;
        m_nodeOutputs.clear();
        m_evaluating.clear();
        if (const VisualLink* link = FindLinkFrom(waitNode, resumePin)) ExecuteNode(link->toNode);
    }

    // Resume a timeline: seed the node's Alpha output, then run its Update/Finished branch (Milestone 5).
    void ResumeTimeline(ecs::Registry& registry, float dt, NodeId node, PinId resumePin, PinId alphaPin, float alpha) {
        m_registry = &registry;
        m_deltaTime = dt;
        m_opsRemaining = kMaxOpsPerCallback;
        m_depth = 0;
        m_nodeOutputs.clear();
        m_evaluating.clear();
        if (alphaPin != kInvalidPinId) m_nodeOutputs[node][alphaPin] = VisualValue::Float(alpha);
        if (const VisualLink* link = FindLinkFrom(node, resumePin)) ExecuteNode(link->toNode);
    }

    // ---- state-machine helpers (Milestone 7) -------------------------------
    static const VisualState* FindState(const VisualStateMachine& sm, std::uint32_t stateId) {
        for (const VisualState& s : sm.states) if (s.id == stateId) return &s;
        return nullptr;
    }
    void CallVoidFunction(FunctionId f) {
        if (f == kInvalidFunctionId) return;
        std::unordered_map<std::string, VisualValue> in, out;
        CallFunction(m_handle, f, in, &out);
    }
    bool CallBoolFunction(FunctionId f) {
        if (f == kInvalidFunctionId) return true;   // no condition => always eligible
        std::unordered_map<std::string, VisualValue> in, out;
        if (!CallFunction(m_handle, f, in, &out)) return false;
        const VisualFunction* fn = m_asset ? m_asset->FindFunction(f) : nullptr;
        if (fn) for (const VisualFunctionParam& p : fn->outputs) {
            auto it = out.find(p.name);
            if (it != out.end()) return it->second.AsBool();
        }
        return false;
    }

    bool EvaluateBoolInput(ecs::Registry& registry, NodeId node, const std::string& pinName) {
        const VisualNode* n = FindNode(node);
        if (!n) return false;
        m_registry = &registry;
        m_opsRemaining = kMaxOpsPerCallback;
        m_depth = 0;
        m_nodeOutputs.clear();
        m_evaluating.clear();
        const VisualNode* prev = m_currentNode;
        m_currentNode = n;
        const bool result = ReadInput(pinName).AsBool();
        m_currentNode = prev;
        return result;
    }

    // Seed an event-entry node's data outputs from the incoming payload (typed by pin).
    void SeedEventOutputs(const VisualNode& node, const ScriptEvent& event) {
        for (const VisualPin& pin : node.outputs) {
            if (pin.kind != PinKind::Data) continue;
            switch (pin.type) {
                case ValueType::Bool:   if (auto it = event.bools.find(pin.name);   it != event.bools.end())   m_nodeOutputs[node.id][pin.id] = VisualValue::Bool(it->second); break;
                case ValueType::Int:    if (auto it = event.ints.find(pin.name);    it != event.ints.end())    m_nodeOutputs[node.id][pin.id] = VisualValue::Int(it->second); break;
                case ValueType::Float:  if (auto it = event.floats.find(pin.name);  it != event.floats.end())  m_nodeOutputs[node.id][pin.id] = VisualValue::Float(it->second); break;
                case ValueType::String: if (auto it = event.strings.find(pin.name); it != event.strings.end()) m_nodeOutputs[node.id][pin.id] = VisualValue::Str(it->second); break;
                case ValueType::Vector3:if (auto it = event.vectors.find(pin.name); it != event.vectors.end()) m_nodeOutputs[node.id][pin.id] = VisualValue::Vec3(it->second); break;
                case ValueType::Vector2:if (auto it = event.vectors.find(pin.name); it != event.vectors.end()) m_nodeOutputs[node.id][pin.id] = VisualValue::Vec2(glm::vec2(it->second)); break;
                case ValueType::Entity:
                case ValueType::ScriptHandle: if (auto it = event.entities.find(pin.name); it != event.entities.end()) m_nodeOutputs[node.id][pin.id] = VisualValue::Ent(it->second); break;
                default: break;
            }
        }
    }

    // ---- Interpreter -------------------------------------------------------
    void RunEvent(ecs::Registry& registry, float dt, const char* eventTypeId) {
        if (!m_asset || m_error.active || !m_execStack.empty()) return;
        m_registry = &registry;
        m_deltaTime = dt;
        m_opsRemaining = kMaxOpsPerCallback;
        m_depth = 0;
        m_nodeOutputs.clear();
        m_evaluating.clear();
        const VisualNode* event = FindEventNode(eventTypeId);
        if (!event) return;   // graph simply doesn't handle this event
        ExecuteNode(event->id);
    }

    void ExecuteNode(NodeId id) {
        if (m_error.active || id == kInvalidNodeId) return;
        m_execStack.push_back({id, {}});
        DrainExecution();
    }

    // Exec flow is an explicit LIFO work stack instead of C++ recursion. Besides avoiding a native
    // stack overflow, this preserves every outstanding Sequence branch when the debugger pauses.
    void DrainExecution() {
        VisualScriptDiagnostics& diag = VisualScriptDiagnostics::Instance();
        while (!m_execStack.empty() && !m_error.active && !diag.paused) {
            ExecutionFrame& pending = m_execStack.back();
            const VisualNode* node = FindNode(pending.node);
            if (!node) { m_execStack.pop_back(); continue; }
            const NodeDescriptor* desc = DescriptorFor(node);
            if (!desc) { SetError(node, "Missing node type: " + node->typeId); return; }
            if (--m_opsRemaining < 0) {
                if (diag.enabled) diag.RecordBudgetHit();
                SetError(node, "Execution limit exceeded"); return;
            }
            if (pending.callStack.size() >= static_cast<std::size_t>(kMaxExecDepth)) {
                SetError(node, "Execution depth limit exceeded"); return;
            }

            m_callStack = pending.callStack;
            std::string functionName;   // Milestone 3: label the frame with its owning function
            if (node->functionId != kInvalidFunctionId && m_asset)
                if (const VisualFunction* fn = m_asset->FindFunction(node->functionId)) functionName = fn->name;
            m_callStack.push_back({m_handle, node->id, node->typeId, m_entity, functionName});
            if (diag.enabled && diag.ShouldPauseAt(m_handle, node->id, m_entity, m_callStack)) {
                m_callStack.clear();
                return; // leave this exact frame on the stack for Continue/Step
            }

            ExecutionFrame frame = std::move(pending);
            m_execStack.pop_back();
            m_enteredPin = frame.enteredPin;   // Milestone 5: which exec input triggered this node
            const double t0 = diag.enabled ? NowSeconds() : 0.0;
            if (diag.enabled) diag.RecordHighlight(m_handle, node->id, m_entity, t0);

            std::vector<PinId> triggered;
            RunExecutor(node, *desc, &triggered);

            if (diag.enabled) {
                const double elapsedMs = (NowSeconds() - t0) * 1000.0;
                diag.RecordNode(m_handle, node->id, node->typeId, elapsedMs);
                std::unordered_map<std::string, VisualValue> values;
                if (auto found = m_nodeOutputs.find(node->id); found != m_nodeOutputs.end())
                    for (const VisualPin& pin : node->outputs)
                        if (auto value = found->second.find(pin.id); value != found->second.end())
                            values.emplace(pin.name, value->second);
                diag.RecordValues(m_handle, node->id, m_entity, std::move(values), m_variables);
            }
            if (m_error.active) { m_callStack.clear(); return; }

            // Reverse insertion keeps authored output order while using the vector's back as top.
            for (auto it = triggered.rbegin(); it != triggered.rend(); ++it) {
                if (const VisualLink* link = FindLinkFrom(node->id, *it))
                    m_execStack.push_back({link->toNode, m_callStack, link->toPin});   // track entered input
            }
            m_callStack.clear();
        }
    }

    // Evaluate a data output value of a node, on demand, with per-callback memo.
    VisualValue EvaluateOutput(NodeId nodeId, PinId pinId) {
        if (auto nit = m_nodeOutputs.find(nodeId); nit != m_nodeOutputs.end()) {
            if (auto pit = nit->second.find(pinId); pit != nit->second.end()) return pit->second;
        }
        const VisualNode* node = FindNode(nodeId);
        if (!node) return VisualValue::Float(0.0f);
        const NodeDescriptor* desc = DescriptorFor(node);
        if (!desc) { SetError(node, "Missing node type: " + node->typeId); return DefaultForPin(node, pinId); }

        if (desc->pure) {
            if (m_evaluating.count(nodeId)) { SetError(node, "Cyclic data evaluation"); return DefaultForPin(node, pinId); }
            if (--m_opsRemaining < 0) { SetError(node, "Execution limit exceeded"); return DefaultForPin(node, pinId); }
            m_evaluating.insert(nodeId);
            std::vector<PinId> discard;
            ++m_depth;
            if (m_depth < kMaxExecDepth) RunExecutor(node, *desc, &discard);
            else SetError(node, "Execution depth limit exceeded");
            --m_depth;
            m_evaluating.erase(nodeId);
            if (auto nit = m_nodeOutputs.find(nodeId); nit != m_nodeOutputs.end()) {
                if (auto pit = nit->second.find(pinId); pit != nit->second.end()) return pit->second;
            }
            return DefaultForPin(node, pinId);
        }
        // Impure node data output: use the value it produced when it last executed
        // this callback, otherwise the pin default (Pass 1 limitation, documented).
        return DefaultForPin(node, pinId);
    }

    void RunExecutor(const VisualNode* node, const NodeDescriptor& desc, std::vector<PinId>* triggered) {
        const VisualNode* prevNode = m_currentNode;
        std::vector<PinId>* prevTriggered = m_currentTriggered;
        m_currentNode = node;
        m_currentTriggered = triggered;
        if (desc.execute) desc.execute(*this);
        m_currentNode = prevNode;
        m_currentTriggered = prevTriggered;
    }

    // ---- Lookups -----------------------------------------------------------
    static const VisualPin* FindPin(const std::vector<VisualPin>& pins, const std::string& name) {
        for (const VisualPin& p : pins) if (p.name == name) return &p;
        return nullptr;
    }
    VisualValue DefaultForPin(const VisualNode* node, PinId pinId) const {
        if (node) if (const VisualPin* p = node->FindPin(pinId)) return p->defaultValue;
        return VisualValue::Float(0.0f);
    }
    const VisualNode* FindEventNode(const std::string& typeId) const {
        for (const VisualNode& n : m_asset->nodes) if (n.typeId == typeId) return &n;
        return nullptr;
    }
    // Milestone 9: compiled-accelerated lookups (O(1) maps) with a safe linear fallback. The compiled
    // structure is only used when it indexes the CURRENT asset (guards cross-graph function calls) and
    // the global toggle is on.
    bool UseCompiled() const { return m_compiled && m_compiled->source == m_asset && VisualScriptUseCompiledRef(); }
    const VisualNode* FindNode(NodeId id) const {
        if (UseCompiled()) { auto it = m_compiled->nodeById.find(id); return it == m_compiled->nodeById.end() ? nullptr : it->second; }
        return m_asset ? m_asset->FindNode(id) : nullptr;
    }
    const NodeDescriptor* DescriptorFor(const VisualNode* node) const {
        if (!node) return nullptr;
        if (UseCompiled()) { auto it = m_compiled->descById.find(node->id); if (it != m_compiled->descById.end()) return it->second; }
        return VisualNodeRegistry::Instance().Find(node->typeId);
    }
    const VisualLink* FindLinkFrom(NodeId node, PinId pin) const {
        if (UseCompiled()) { auto it = m_compiled->linkFrom.find(CompiledGraph::Key(node, pin)); return it == m_compiled->linkFrom.end() ? nullptr : it->second; }
        for (const VisualLink& l : m_asset->links) if (l.fromNode == node && l.fromPin == pin) return &l;
        return nullptr;
    }
    const VisualLink* FindLinkTo(NodeId node, PinId pin) const {
        if (UseCompiled()) { auto it = m_compiled->linkTo.find(CompiledGraph::Key(node, pin)); return it == m_compiled->linkTo.end() ? nullptr : it->second; }
        for (const VisualLink& l : m_asset->links) if (l.toNode == node && l.toPin == pin) return &l;
        return nullptr;
    }

    static double NowSeconds() {
        using clock = std::chrono::steady_clock;
        return std::chrono::duration<double>(clock::now().time_since_epoch()).count();
    }

    void SetError(const VisualNode* node, const std::string& message) {
        if (m_error.active) return;   // keep the first error
        m_error.active = true;
        m_error.entity = m_entity;
        m_error.asset = m_handle;
        m_error.nodeId = node ? node->id : kInvalidNodeId;
        m_error.nodeTypeId = node ? node->typeId : std::string();
        m_error.message = message;
        if (auto& handler = VisualScriptErrorHandlerRef()) handler(m_error);
        // Pass 5: record the error with its call-stack path for the editor (Phase 12).
        VisualScriptDiagnostics& diag = VisualScriptDiagnostics::Instance();
        if (diag.enabled) {
            VsErrorRecord rec;
            rec.entity = m_entity; rec.graph = m_handle;
            rec.nodeId = m_error.nodeId; rec.nodeType = m_error.nodeTypeId;
            rec.message = message; rec.callStack = m_callStack;
            diag.RecordError(std::move(rec));
        }
    }

    // ---- State (all transient; never serialized) ---------------------------
    ecs::Entity m_entity = ecs::kNull;
    AssetHandle m_handle;
    const VisualScriptAsset* m_asset = nullptr;
    const CompiledGraph* m_compiled = nullptr;   // Milestone 9 (borrowed from the runtime cache)
    bool m_began = false;
    VisualScriptError m_error;
    std::unordered_map<std::string, VisualValue> m_variables;
    std::unordered_map<std::uint32_t, std::string> m_variableNamesById;
    std::vector<Continuation> m_continuations;   // Pass 4 latent flow (owned by this instance)
    std::vector<ExecutionFrame> m_execStack;      // paused/resumable exec flow; back is next node
    int m_nextContinuationId = 1;
    std::vector<CallFrame> m_callStack;          // Pass 5 diagnostics (only used when enabled)

    // Milestone 3: function-call machinery (synchronous; fully saved/restored around each call).
    std::function<const VisualScriptAsset*(const AssetHandle&)> m_functionResolver;
    std::unordered_map<std::string, VisualValue> m_functionReturnValues;   // Return node -> Call outputs
    std::vector<std::unordered_map<VariableId, VisualValue>> m_localScopes; // function-local variable scopes
    // Milestone 5: per-node persistent state (Do Once / Gate / Flip-Flop), entered exec input, clock.
    std::unordered_map<NodeId, std::unordered_map<std::string, VisualValue>> m_nodeState;
    PinId  m_enteredPin = kInvalidPinId;
    double m_playTime = 0.0;
    // Milestone 7: state-machine runtime state (per state machine id).
    std::unordered_map<std::uint32_t, std::uint32_t> m_smCurrentState;   // smId -> current stateId
    std::unordered_map<std::uint64_t, double> m_smTransitionReady;       // (smId<<32|transId) -> readyAt
    std::unordered_set<std::uint64_t> m_activeFunctions;                   // recursion guard by (graph,func)
    int m_functionDepth = 0;
    static constexpr int kMaxFunctionDepth = 64;

    // Pass 2 engine services (owned by the host; instance only borrows).
    PhysicsWorld* m_physics = nullptr;
    const ScriptInputState* m_input = nullptr;
    std::vector<ScriptEvent>* m_eventOutbox = nullptr;
    std::vector<std::function<void(ecs::Registry&)>>* m_structuralQueue = nullptr;
    // Milestone 6: targeted event outbox (VS→VS direct delivery) + per-instance unbound event names.
    std::vector<std::pair<ecs::Entity, ScriptEvent>>* m_targetedOutbox = nullptr;
    std::unordered_set<std::string> m_unboundEvents;
    ScriptEvent m_buildingEvent;   // scratch for the BeginEvent/EventArg/SendEvent builder

    // Pass 4 gameplay host: a reusable Script whose context we retarget per call.
    std::unique_ptr<VisualScriptHost> m_scriptHost;
    std::vector<ScriptField> m_hostFields;
    RuntimeAudioSystem* m_audio = nullptr;
    CameraShake* m_cameraShake = nullptr;
    CameraDirector* m_cameraDirector = nullptr;
    GameMode* m_gameMode = nullptr;

    // per-callback interpreter scratch
    ecs::Registry* m_registry = nullptr;
    float m_deltaTime = 0.0f;
    int m_opsRemaining = 0;
    int m_depth = 0;
    const VisualNode* m_currentNode = nullptr;
    std::vector<PinId>* m_currentTriggered = nullptr;
    std::unordered_map<NodeId, std::unordered_map<PinId, VisualValue>> m_nodeOutputs;
    std::unordered_set<NodeId> m_evaluating;
};

} // namespace engine::vs
