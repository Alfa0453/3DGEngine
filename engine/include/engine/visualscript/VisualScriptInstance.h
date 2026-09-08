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
#include "VisualScriptAsset.h"
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
    void Bind(ecs::Entity entity, AssetHandle handle, const VisualScriptAsset* asset) {
        m_entity = entity;
        m_handle = handle;
        m_asset = asset;
        m_began = false;
        m_error = VisualScriptError{};
        m_variables.clear();
        m_variableNamesById.clear();
        m_continuations.clear();
        m_execStack.clear();
        if (asset) for (const VisualVariable& v : asset->variables) {
            m_variables[v.name] = v.defaultValue;
            m_variableNamesById[v.id] = v.name;   // stable id -> current name (Phase 12)
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
        m_registry = &registry;
        m_deltaTime = 0.0f;
        m_opsRemaining = kMaxOpsPerCallback;
        m_depth = 0;
        m_nodeOutputs.clear();
        m_evaluating.clear();
        for (const VisualNode& node : m_asset->nodes) {
            const NodeDescriptor* desc = VisualNodeRegistry::Instance().Find(node.typeId);
            if (!desc || desc->eventName.empty() || desc->eventName != event.name) continue;
            SeedEventOutputs(node, event);
            ExecuteNode(node.id);
            if (m_error.active || VisualScriptDiagnostics::Instance().paused) return;
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
        auto n = m_variableNamesById.find(variableId);
        if (n == m_variableNamesById.end()) return VisualValue::Float(0.0f);
        auto v = m_variables.find(n->second);
        return v == m_variables.end() ? VisualValue::Float(0.0f) : v->second;
    }
    void SetVariableById(std::uint32_t variableId, VisualValue value) override {
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

    // Render-frame tick: advance Delay timers and re-test WaitUntil conditions.
    void TickLatent(ecs::Registry& registry, float dt) {
        if (m_continuations.empty() || m_error.active) return;
        std::vector<Continuation> ready;
        for (auto it = m_continuations.begin(); it != m_continuations.end();) {
            bool fire = false;
            if (it->kind == LatentKind::Delay) { it->remaining -= dt; fire = it->remaining <= 0.0f; }
            else if (it->kind == LatentKind::WaitUntil) fire = EvaluateBoolInput(registry, it->waitNode, "Condition");
            else if (it->kind == LatentKind::WaitAnimation) { VisualScriptHost* h = ScriptHost(); fire = !h || !h->VsIsAnimationActionPlaying(); }
            if (fire) { ready.push_back(*it); it = m_continuations.erase(it); }
            else ++it;
        }
        for (const Continuation& c : ready) { ResumeFrom(registry, dt, c.waitNode, c.resumePin); if (m_error.active) return; }
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
    };

    struct ExecutionFrame {
        NodeId node = kInvalidNodeId;
        std::vector<CallFrame> callStack;
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

    bool EvaluateBoolInput(ecs::Registry& registry, NodeId node, const std::string& pinName) {
        const VisualNode* n = m_asset->FindNode(node);
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
            const VisualNode* node = m_asset ? m_asset->FindNode(pending.node) : nullptr;
            if (!node) { m_execStack.pop_back(); continue; }
            const NodeDescriptor* desc = VisualNodeRegistry::Instance().Find(node->typeId);
            if (!desc) { SetError(node, "Missing node type: " + node->typeId); return; }
            if (--m_opsRemaining < 0) {
                if (diag.enabled) diag.RecordBudgetHit();
                SetError(node, "Execution limit exceeded"); return;
            }
            if (pending.callStack.size() >= static_cast<std::size_t>(kMaxExecDepth)) {
                SetError(node, "Execution depth limit exceeded"); return;
            }

            m_callStack = pending.callStack;
            m_callStack.push_back({m_handle, node->id, node->typeId, m_entity});
            if (diag.enabled && diag.ShouldPauseAt(m_handle, node->id, m_entity, m_callStack)) {
                m_callStack.clear();
                return; // leave this exact frame on the stack for Continue/Step
            }

            ExecutionFrame frame = std::move(pending);
            m_execStack.pop_back();
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
                    m_execStack.push_back({link->toNode, m_callStack});
            }
            m_callStack.clear();
        }
    }

    // Evaluate a data output value of a node, on demand, with per-callback memo.
    VisualValue EvaluateOutput(NodeId nodeId, PinId pinId) {
        if (auto nit = m_nodeOutputs.find(nodeId); nit != m_nodeOutputs.end()) {
            if (auto pit = nit->second.find(pinId); pit != nit->second.end()) return pit->second;
        }
        const VisualNode* node = m_asset->FindNode(nodeId);
        if (!node) return VisualValue::Float(0.0f);
        const NodeDescriptor* desc = VisualNodeRegistry::Instance().Find(node->typeId);
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
    const VisualLink* FindLinkFrom(NodeId node, PinId pin) const {
        for (const VisualLink& l : m_asset->links) if (l.fromNode == node && l.fromPin == pin) return &l;
        return nullptr;
    }
    const VisualLink* FindLinkTo(NodeId node, PinId pin) const {
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
    bool m_began = false;
    VisualScriptError m_error;
    std::unordered_map<std::string, VisualValue> m_variables;
    std::unordered_map<std::uint32_t, std::string> m_variableNamesById;
    std::vector<Continuation> m_continuations;   // Pass 4 latent flow (owned by this instance)
    std::vector<ExecutionFrame> m_execStack;      // paused/resumable exec flow; back is next node
    int m_nextContinuationId = 1;
    std::vector<CallFrame> m_callStack;          // Pass 5 diagnostics (only used when enabled)

    // Pass 2 engine services (owned by the host; instance only borrows).
    PhysicsWorld* m_physics = nullptr;
    const ScriptInputState* m_input = nullptr;
    std::vector<ScriptEvent>* m_eventOutbox = nullptr;
    std::vector<std::function<void(ecs::Registry&)>>* m_structuralQueue = nullptr;

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
