#pragma once

// =============================================================================
// Visual Scripting — Pass 5 : diagnostics (profiler + debugger data layer).
//
// A single, bounded, STRIPPABLE diagnostics singleton the interpreter reports to
// only while `enabled` is true, so a shipping build pays nothing (Phase 27) and
// disconnecting the debugger never changes gameplay state. Covers:
//   * per-graph + per-node profiler                              (Phase 13/14)
//   * breakpoints by (graph, node) + pause/step budget           (Phase 6/7/8)
//   * execution highlight ring (bounded, editor reads it)        (Phase 9/23)
//   * call-stack capture + runtime error records with path       (Phase 11/12)
//   * execution-budget accounting                                (Phase 15)
//
// Editor-facing DATA only — the panel renders it; no runtime state is written
// into the authored graph asset (Phase 9).
// =============================================================================

#include "VisualScriptTypes.h"

#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace engine::vs {

struct NodeProfile {
    NodeId      nodeId = kInvalidNodeId;
    std::string typeId;
    std::uint64_t calls = 0;
    double totalMs = 0.0;
    double maxMs = 0.0;
    double lastMs = 0.0;
    double AverageMs() const { return calls ? totalMs / static_cast<double>(calls) : 0.0; }
};

struct GraphProfile {                    // Phase 13
    AssetHandle graph;
    int         instances = 0;
    std::uint64_t nodesExecuted = 0;
    double      updateMs = 0.0;
    double      fixedMs = 0.0;
    double      eventMs = 0.0;
    std::uint64_t latentActive = 0;
    std::unordered_map<NodeId, NodeProfile> nodes;
};

struct CallFrame {                       // Phase 11
    AssetHandle graph;
    NodeId      node = kInvalidNodeId;
    std::string nodeType;
    ecs::Entity entity = ecs::kNull;
    std::string functionName;            // owning function (empty = event graph) — Milestone 3
};

struct VsErrorRecord {                   // Phase 12
    ecs::Entity entity = ecs::kNull;
    AssetHandle graph;
    NodeId      nodeId = kInvalidNodeId;
    std::string nodeType;
    std::string message;
    std::vector<CallFrame> callStack;
};

struct HighlightEntry {                  // Phase 9
    AssetHandle graph;
    NodeId      node = kInvalidNodeId;
    ecs::Entity entity = ecs::kNull;
    double      timeSeconds = 0.0;
};

enum class PauseReason : std::uint8_t { None, Breakpoint, Step };

struct PausedExecution {
    bool active = false;
    PauseReason reason = PauseReason::None;
    AssetHandle graph;
    NodeId node = kInvalidNodeId;
    ecs::Entity entity = ecs::kNull;
    std::vector<CallFrame> callStack;
};

struct NodeValueSnapshot {
    AssetHandle graph;
    NodeId node = kInvalidNodeId;
    ecs::Entity entity = ecs::kNull;
    std::unordered_map<std::string, VisualValue> outputs;
    std::unordered_map<std::string, VisualValue> variables;
};

struct VsEventTraceEntry {              // Milestone 6: event dispatch path
    ecs::Entity sender = ecs::kNull;
    ecs::Entity target = ecs::kNull;    // kNull == broadcast to the whole bus
    std::string eventName;
    bool        broadcast = true;
    double      timeSeconds = 0.0;
};

struct VsStateChange {                  // Milestone 7: state-machine transition history
    AssetHandle   graph;
    std::uint32_t stateMachine = 0;
    ecs::Entity   entity = ecs::kNull;
    std::uint32_t fromState = 0;
    std::uint32_t toState = 0;
    double        timeSeconds = 0.0;
};

struct VsTaskInfo {                     // Milestone 8: active async task (rebuilt each frame)
    AssetHandle graph;
    ecs::Entity owner = ecs::kNull;
    int         handle = 0;
    std::string kind;
    float       elapsed = 0.0f;
    float       timeout = 0.0f;         // <= 0 => none
};

class VisualScriptDiagnostics {
public:
    static VisualScriptDiagnostics& Instance() { static VisualScriptDiagnostics d; return d; }

    // Master switch. False in shipping strips all capture cost (Phase 27).
    bool enabled = false;
    void SetEnabled(bool on) {
        enabled = on;
        if (!on) { paused = false; stepBudget = -1; m_paused = {}; m_suppressOnce = 0; }
    }
    // Clears one Play session's live data while deliberately preserving authored editor breakpoints.
    void BeginSession() {
        m_graphs.clear(); m_highlights.clear(); m_errors.clear(); m_values = {};
        m_eventTrace.clear(); m_stateTrace.clear();
        m_budgetHits = 0; paused = false; stepBudget = -1; m_paused = {}; m_suppressOnce = 0;
    }

    // ---- profiler (Phase 13) ----------------------------------------------
    void FrameReset() {                  // call once per frame before ticking graphs
        for (auto& kv : m_graphs) {
            kv.second.instances = 0; kv.second.nodesExecuted = 0;
            kv.second.updateMs = kv.second.fixedMs = kv.second.eventMs = 0.0;
            kv.second.latentActive = 0;
        }
        m_tasks.clear();   // Milestone 8: active-task list is rebuilt each frame
    }
    GraphProfile& Graph(const AssetHandle& g) {
        GraphProfile& p = m_graphs[Key(g)];
        if (!p.graph.Valid()) p.graph = g;
        return p;
    }
    void RecordNode(const AssetHandle& g, NodeId nodeId, const std::string& typeId, double ms) {
        if (!enabled) return;
        GraphProfile& gp = Graph(g);
        ++gp.nodesExecuted;
        NodeProfile& np = gp.nodes[nodeId];
        np.nodeId = nodeId; np.typeId = typeId; ++np.calls; np.totalMs += ms; np.lastMs = ms;
        if (ms > np.maxMs) np.maxMs = ms;
    }
    const std::unordered_map<std::uint64_t, GraphProfile>& Graphs() const { return m_graphs; }

    // ---- breakpoints + stepping (Phase 6/7/8) -----------------------------
    void SetBreakpoint(const AssetHandle& g, NodeId node, bool on) {
        const std::uint64_t k = Key(g) ^ (static_cast<std::uint64_t>(node) * 0x9e3779b97f4a7c15ull);
        if (on) m_breakpoints.insert(k); else m_breakpoints.erase(k);
    }
    bool HasBreakpoint(const AssetHandle& g, NodeId node) const {
        if (m_breakpoints.empty()) return false;
        return m_breakpoints.count(Key(g) ^ (static_cast<std::uint64_t>(node) * 0x9e3779b97f4a7c15ull)) != 0;
    }
    bool paused = false;         // set when a breakpoint is hit; controlled through Continue/Step
    int  stepBudget = -1;        // -1 = run freely; >=0 = execute this many nodes then pause
    // Called by the interpreter when it enters a node. Returns true if execution should STOP here
    // (breakpoint hit or step budget exhausted) — a safe, deterministic pause at a node boundary.
    bool ShouldPauseAt(const AssetHandle& g, NodeId node, ecs::Entity entity,
                       const std::vector<CallFrame>& callStack) {
        if (!enabled) return false;
        const std::uint64_t key = BreakpointKey(g, node);
        if (m_suppressOnce == key) {
            m_suppressOnce = 0;
            if (stepBudget > 0) --stepBudget;
            return false;
        }
        if (HasBreakpoint(g, node)) { SetPaused(g, node, entity, callStack, PauseReason::Breakpoint); return true; }
        if (stepBudget == 0) { SetPaused(g, node, entity, callStack, PauseReason::Step); return true; }
        if (stepBudget > 0) --stepBudget;
        return false;
    }

    void ContinueExecution() {
        if (m_paused.active) m_suppressOnce = BreakpointKey(m_paused.graph, m_paused.node);
        paused = false; stepBudget = -1; m_paused.active = false;
    }
    void StepExecution() {
        if (m_paused.active) m_suppressOnce = BreakpointKey(m_paused.graph, m_paused.node);
        paused = false; stepBudget = 1; m_paused.active = false;
    }
    const PausedExecution& Paused() const { return m_paused; }

    // ---- execution highlight (Phase 9, bounded — Phase 23) ----------------
    void RecordHighlight(const AssetHandle& g, NodeId node, ecs::Entity e, double t) {
        if (!enabled) return;
        m_highlights.push_back({g, node, e, t});
        while (m_highlights.size() > kMaxHighlights) m_highlights.pop_front();
    }
    const std::deque<HighlightEntry>& Highlights() const { return m_highlights; }

    void RecordValues(const AssetHandle& g, NodeId node, ecs::Entity entity,
                      std::unordered_map<std::string, VisualValue> outputs,
                      std::unordered_map<std::string, VisualValue> variables) {
        if (!enabled) return;
        m_values = {g, node, entity, std::move(outputs), std::move(variables)};
    }
    const NodeValueSnapshot& Values() const { return m_values; }

    // ---- runtime errors (Phase 12, bounded) -------------------------------
    void RecordError(VsErrorRecord rec) {
        m_errors.push_back(std::move(rec));
        while (m_errors.size() > kMaxErrors) m_errors.erase(m_errors.begin());
    }
    const std::vector<VsErrorRecord>& Errors() const { return m_errors; }
    void ClearErrors() { m_errors.clear(); }

    // ---- execution budget (Phase 15) --------------------------------------
    void RecordBudgetHit() { ++m_budgetHits; }
    std::uint64_t BudgetHits() const { return m_budgetHits; }

    // ---- event trace (Milestone 6, bounded) -------------------------------
    void RecordEventDispatch(ecs::Entity sender, ecs::Entity target, const std::string& name,
                             bool broadcast, double t) {
        if (!enabled) return;
        m_eventTrace.push_back({sender, target, name, broadcast, t});
        while (m_eventTrace.size() > kMaxEventTrace) m_eventTrace.pop_front();
    }
    const std::deque<VsEventTraceEntry>& EventTrace() const { return m_eventTrace; }
    void ClearEventTrace() { m_eventTrace.clear(); }

    // ---- state-machine trace (Milestone 7, bounded) -----------------------
    void RecordStateChange(const AssetHandle& g, std::uint32_t sm, ecs::Entity e,
                           std::uint32_t from, std::uint32_t to, double t) {
        if (!enabled) return;
        m_stateTrace.push_back({g, sm, e, from, to, t});
        while (m_stateTrace.size() > kMaxStateTrace) m_stateTrace.pop_front();
    }
    const std::deque<VsStateChange>& StateTrace() const { return m_stateTrace; }
    void ClearStateTrace() { m_stateTrace.clear(); }

    // ---- active async tasks (Milestone 8, rebuilt each frame) --------------
    void ReportTask(const VsTaskInfo& t) { if (enabled) m_tasks.push_back(t); }
    const std::vector<VsTaskInfo>& Tasks() const { return m_tasks; }
    // Latest state id for a (graph, state machine) across any entity — for the editor's live view.
    std::uint32_t CurrentState(const AssetHandle& g, std::uint32_t sm) const {
        for (auto it = m_stateTrace.rbegin(); it != m_stateTrace.rend(); ++it)
            if (it->graph == g && it->stateMachine == sm) return it->toState;
        return 0;
    }

    void Clear() {
        m_graphs.clear(); m_highlights.clear(); m_errors.clear(); m_eventTrace.clear(); m_stateTrace.clear();
        m_budgetHits = 0; paused = false; stepBudget = -1;
        m_paused = {}; m_values = {}; m_suppressOnce = 0;
    }

private:
    static std::uint64_t Key(const AssetHandle& g) { return g.high ^ (g.low * 1099511628211ull); }
    static std::uint64_t BreakpointKey(const AssetHandle& g, NodeId node) {
        return Key(g) ^ (static_cast<std::uint64_t>(node) * 0x9e3779b97f4a7c15ull);
    }
    void SetPaused(const AssetHandle& g, NodeId node, ecs::Entity entity,
                   const std::vector<CallFrame>& callStack, PauseReason reason) {
        paused = true;
        m_paused.active = true; m_paused.reason = reason; m_paused.graph = g;
        m_paused.node = node; m_paused.entity = entity; m_paused.callStack = callStack;
    }

    static constexpr std::size_t kMaxHighlights = 512;   // no unbounded debug-history growth
    static constexpr std::size_t kMaxErrors = 128;
    static constexpr std::size_t kMaxEventTrace = 256;
    static constexpr std::size_t kMaxStateTrace = 256;

    std::unordered_map<std::uint64_t, GraphProfile> m_graphs;
    std::unordered_set<std::uint64_t> m_breakpoints;
    std::deque<HighlightEntry> m_highlights;
    std::deque<VsEventTraceEntry> m_eventTrace;
    std::deque<VsStateChange> m_stateTrace;
    std::vector<VsTaskInfo> m_tasks;
    std::vector<VsErrorRecord> m_errors;
    std::uint64_t m_budgetHits = 0;
    PausedExecution m_paused;
    NodeValueSnapshot m_values;
    std::uint64_t m_suppressOnce = 0;
};

} // namespace engine::vs
