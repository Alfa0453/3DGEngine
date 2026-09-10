#pragma once

// =============================================================================
// Visual Scripting — Milestone 10 : automated graph testing.
//
// A headless test harness that drives a graph through a VisualScriptInstance with
// a DETERMINISTIC clock (fixed dt) and evaluates assertions — values, emitted
// events, runtime errors, and completion-within-N-ticks. Results link back to the
// failing graph and node. Includes a self-contained REGRESSION SUITE covering
// serialization, version migration, hot reload, latent cancellation, and the
// instruction-budget guard. Everything is header-only and free of ImGui / the
// editor, so it runs from the test build and the command line as well as the
// editor's Automated Test panel.
// =============================================================================

#include "VisualNodeRegistry.h"
#include "VisualScriptAsset.h"
#include "VisualScriptComponent.h"
#include "VisualScriptInstance.h"
#include "VisualScriptTypes.h"

#include <engine/ecs/Registry.h>

#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace engine::vs {

// ---- Test model ------------------------------------------------------------
struct VsAssert {
    enum class Kind { VariableEquals, EventEmitted, NoError, ErrorOccurred, CompletedWithin };
    Kind        kind = Kind::NoError;
    std::string name;        // variable name / event name / completion flag variable
    VisualValue expected;    // VariableEquals
};
struct VsTestStep {
    enum class Kind { Update, FixedUpdate, SendEvent };
    Kind        kind = Kind::Update;
    float       dt = 1.0f / 60.0f;
    int         count = 1;
    ScriptEvent event;       // SendEvent
};
struct VisualScriptTest {
    std::string name;
    std::vector<VisualScriptVariableOverride> setup;   // initial exposed-variable overrides
    std::vector<VsTestStep> steps;
    std::vector<VsAssert>   asserts;
};

// ---- Results ---------------------------------------------------------------
struct VsTestFailure {
    std::string test;
    std::string message;
    std::string expected;
    std::string actual;
    AssetHandle graph;
    NodeId      node = kInvalidNodeId;   // set when a runtime error names a node
};
struct VisualScriptTestReport {
    int passed = 0;
    int failed = 0;
    std::vector<VsTestFailure> failures;
    bool Ok() const { return failed == 0; }
    std::string Summary() const {
        std::ostringstream o;
        o << passed << " passed, " << failed << " failed";
        for (const VsTestFailure& f : failures)
            o << "\n  FAIL [" << f.test << "] " << f.message
              << (f.expected.empty() ? "" : ("  expected=" + f.expected + " actual=" + f.actual))
              << (f.node != kInvalidNodeId ? ("  node=" + std::to_string(f.node)) : "");
        return o.str();
    }
};

// ---- Runner ----------------------------------------------------------------
class VisualScriptTestRunner {
public:
    // Run every test against `graph`. `graph` must outlive the call.
    VisualScriptTestReport Run(const VisualScriptAsset& graph, const std::vector<VisualScriptTest>& tests) {
        RegisterCoreNodes();   // idempotent — ensure the core node set exists
        VisualScriptTestReport report;
        for (const VisualScriptTest& test : tests) RunOne(graph, test, report);
        return report;
    }

    // Build-in regression suite (Milestone 10) — returns a report the caller prints / asserts on.
    static VisualScriptTestReport RunRegressionSuite();

private:
    static std::string ValueStr(const VisualValue& v) {
        std::ostringstream o;
        switch (v.type) {
            case ValueType::Bool:   o << (v.AsBool() ? "true" : "false"); break;
            case ValueType::Int:    o << v.AsInt(); break;
            case ValueType::Float:  o << v.AsFloat(); break;
            case ValueType::String: o << '"' << v.AsString() << '"'; break;
            default:                o << ValueTypeName(v.type); break;
        }
        return o.str();
    }

    void RunOne(const VisualScriptAsset& graph, const VisualScriptTest& test, VisualScriptTestReport& report) {
        ecs::Registry reg;
        const ecs::Entity entity = reg.Create();
        std::vector<ScriptEvent> outbox;
        std::vector<std::function<void(ecs::Registry&)>> structural;

        VisualScriptInstance inst;
        inst.Bind(entity, graph.id, &graph, test.setup);
        inst.SetServices(nullptr, nullptr, &outbox, &structural);

        double clock = 0.0;
        auto advance = [&](float dt) {
            clock += dt;
            inst.SetPlayTime(clock);
            inst.Begin(reg);
            if (!inst.HasError()) inst.Update(reg, dt);
            if (!inst.HasError()) inst.TickStateMachines(reg, dt);
            if (!inst.HasError()) inst.TickLatent(reg, dt);
        };

        auto fail = [&](const VsAssert& a, const std::string& msg, const std::string& exp, const std::string& act) {
            (void)a;   // assertion context reserved for future per-assert detail
            VsTestFailure f; f.test = test.name; f.message = msg; f.expected = exp; f.actual = act;
            f.graph = graph.id;
            if (inst.HasError()) f.node = inst.Error().nodeId;
            report.failures.push_back(std::move(f));
            ++report.failed;
        };

        // Drive the deterministic timeline.
        for (const VsTestStep& step : test.steps) {
            for (int i = 0; i < step.count; ++i) {
                switch (step.kind) {
                    case VsTestStep::Kind::Update:      advance(step.dt); break;
                    case VsTestStep::Kind::FixedUpdate: clock += step.dt; inst.SetPlayTime(clock); inst.Begin(reg);
                                                        if (!inst.HasError()) inst.FixedUpdate(reg, step.dt);
                                                        if (!inst.HasError()) inst.TickLatentFixed(reg, step.dt, 1); break;
                    case VsTestStep::Kind::SendEvent:   inst.DispatchEvent(reg, step.event); break;
                }
                if (inst.HasError()) break;
            }
        }

        const auto snapshot = inst.VariableSnapshot();
        auto variable = [&](const std::string& n) -> const VisualValue* {
            for (const auto& kv : snapshot) if (kv.first == n) return &kv.second;
            return nullptr;
        };

        for (const VsAssert& a : test.asserts) {
            switch (a.kind) {
                case VsAssert::Kind::VariableEquals: {
                    const VisualValue* v = variable(a.name);
                    if (!v) { fail(a, "variable '" + a.name + "' not found", ValueStr(a.expected), "<missing>"); }
                    else if (!v->Equals(a.expected)) { fail(a, "variable '" + a.name + "' mismatch", ValueStr(a.expected), ValueStr(*v)); }
                    else ++report.passed;
                    break;
                }
                case VsAssert::Kind::EventEmitted: {
                    bool found = false; for (const ScriptEvent& e : outbox) if (e.name == a.name) { found = true; break; }
                    if (found) ++report.passed; else fail(a, "event '" + a.name + "' was not emitted", a.name, "<none>");
                    break;
                }
                case VsAssert::Kind::NoError:
                    if (!inst.HasError()) ++report.passed;
                    else fail(a, "unexpected runtime error: " + inst.Error().message, "no error", inst.Error().message);
                    break;
                case VsAssert::Kind::ErrorOccurred:
                    if (inst.HasError()) ++report.passed;
                    else fail(a, "expected a runtime error but none occurred", "error", "no error");
                    break;
                case VsAssert::Kind::CompletedWithin: {
                    const VisualValue* v = variable(a.name);
                    if (v && v->AsBool()) ++report.passed;
                    else fail(a, "completion flag '" + a.name + "' not set", "true", v ? ValueStr(*v) : "<missing>");
                    break;
                }
            }
        }
    }
};

// ---- Built-in regression suite --------------------------------------------
inline VisualScriptTestReport VisualScriptTestRunner::RunRegressionSuite() {
    RegisterCoreNodes();
    VisualScriptTestReport report;
    auto pass = [&]() { ++report.passed; };
    auto fail = [&](const std::string& test, const std::string& msg, const std::string& exp = "", const std::string& act = "") {
        report.failures.push_back({test, msg, exp, act, AssetHandle{}, kInvalidNodeId});
        ++report.failed;
    };

    // Helper: mint a node from the registry with a chosen id.
    auto make = [](VisualScriptAsset& a, const char* type, NodeId id, glm::vec2 pos = {}) -> VisualNode& {
        VisualNode n = VisualNodeRegistry::Instance().MakeNode(type, id);
        n.editorPosition = pos;
        a.nodes.push_back(std::move(n));
        return a.nodes.back();
    };

    // --- 1. Serialization round-trip (nodes/vars/functions/structs/events survive) ---
    {
        VisualScriptAsset a; a.id = AssetHandle::Generate(); a.graphId = 1;
        make(a, "Event.BeginPlay", 1);
        VisualVariable v; v.id = 1; v.name = "Score"; v.type = ValueType::Int; v.defaultValue = VisualValue::Int(7); v.exposed = true;
        a.variables.push_back(v);
        VisualFunction fn; fn.id = 1; fn.name = "Helper"; fn.inputs.push_back({"X", ValueType::Float});
        a.functions.push_back(fn);
        VisualStructType st; st.id = 2; st.name = "Item"; st.fields.push_back({"Count", ValueType::Int, ValueType::Float, 0});
        a.structs.push_back(st);
        VisualCustomEvent ev; ev.id = 3; ev.name = "OnHit"; ev.params.push_back({"Damage", ValueType::Float});
        a.events.push_back(ev);

        std::ostringstream out; a.Save(out);
        std::istringstream in(out.str());
        VisualScriptAsset b; std::string err;
        if (!b.Load(in, &err)) fail("serialize-roundtrip", "reload failed: " + err);
        else if (b.nodes.size() != a.nodes.size()) fail("serialize-roundtrip", "node count", std::to_string(a.nodes.size()), std::to_string(b.nodes.size()));
        else if (b.variables.size() != 1 || b.variables[0].defaultValue.AsInt() != 7) fail("serialize-roundtrip", "variable not preserved");
        else if (b.functions.size() != 1 || b.functions[0].name != "Helper") fail("serialize-roundtrip", "function not preserved");
        else if (b.structs.size() != 1 || b.structs[0].name != "Item") fail("serialize-roundtrip", "struct not preserved");
        else if (b.events.size() != 1 || b.events[0].name != "OnHit") fail("serialize-roundtrip", "event not preserved");
        else pass();
    }

    // --- 2. Version migration: a hand-written v1 asset loads (no functions/comments) ---
    {
        std::ostringstream v1;
        v1 << "3DG_VISUAL_SCRIPT 1 " << AssetHandle::Generate().ToString() << "\n";
        v1 << "graph 1\n";
        v1 << "nodes 1\n";
        v1 << "node 1 \"Event.BeginPlay\" 0 0\n";
        v1 << "  props 0\n";
        v1 << "  in 0\n";
        v1 << "  out 1\n";
        v1 << "    pin 1 1 0 Exec \"Then\" Exec\n";
        v1 << "links 0\n";
        v1 << "vars 1\n";
        v1 << "var \"Health\" Float Float 100\n";
        VisualScriptAsset a; std::string err; std::istringstream in(v1.str());
        if (!a.Load(in, &err)) fail("migrate-v1", "v1 load failed: " + err);
        else if (a.nodes.size() != 1 || a.variables.size() != 1) fail("migrate-v1", "v1 content wrong");
        else if (a.variables[0].id == kInvalidVariableId) fail("migrate-v1", "v1 variable id not minted");
        else pass();
    }

    // --- 3. Hot reload migrates a variable value by stable id ---
    {
        VisualScriptAsset a; a.id = AssetHandle::Generate(); a.graphId = 1;
        VisualVariable v; v.id = 5; v.name = "Ammo"; v.type = ValueType::Int; v.defaultValue = VisualValue::Int(0);
        a.variables.push_back(v);
        ecs::Registry reg; const ecs::Entity e = reg.Create();
        VisualScriptInstance inst; inst.Bind(e, a.id, &a);
        inst.SetVariableById(5, VisualValue::Int(42));
        VisualScriptAsset b = a;   // same variable id 5, possibly renamed
        b.variables[0].name = "Bullets";
        inst.HotReload(a.id, &b);
        if (inst.GetVariableById(5).AsInt() != 42) fail("hot-reload", "variable value not migrated", "42", std::to_string(inst.GetVariableById(5).AsInt()));
        else pass();
    }

    // --- 4. Latent cancellation: a suspended Delay is dropped on rebind (no resume) ---
    {
        VisualScriptAsset a; a.id = AssetHandle::Generate(); a.graphId = 1;
        make(a, "Event.BeginPlay", 1);
        make(a, "Flow.Delay", 2);
        // BeginPlay.Then(pin 1) -> Delay.In(pin 1); Delay.Seconds default 1.
        a.links.push_back({1, 1, 1, 2, 1});
        ecs::Registry reg; const ecs::Entity e = reg.Create();
        VisualScriptInstance inst; std::vector<ScriptEvent> ob; std::vector<std::function<void(ecs::Registry&)>> sc;
        inst.Bind(e, a.id, &a); inst.SetServices(nullptr, nullptr, &ob, &sc);
        inst.SetPlayTime(0.0); inst.Begin(reg);              // BeginPlay -> Delay suspends
        const bool suspended = inst.HasLatent();
        inst.Bind(e, a.id, &a);                              // rebind == destroy/recreate: continuations cleared
        if (!suspended) fail("latent-cancel", "Delay did not suspend");
        else if (inst.HasLatent()) fail("latent-cancel", "latent flow not cancelled on rebind");
        else pass();
    }

    // --- 5. Instruction budget: a self-looping Sequence errors instead of hanging ---
    {
        VisualScriptAsset a; a.id = AssetHandle::Generate(); a.graphId = 1;
        make(a, "Event.BeginPlay", 1);
        make(a, "Core.Sequence", 2);                     // In=1, Then0=2, Then1=3
        a.links.push_back({1, 1, 1, 2, 1});              // BeginPlay.Then -> Sequence.In
        a.links.push_back({2, 2, 2, 2, 1});              // Sequence.Then0 -> Sequence.In  (infinite loop)
        ecs::Registry reg; const ecs::Entity e = reg.Create();
        VisualScriptInstance inst; std::vector<ScriptEvent> ob; std::vector<std::function<void(ecs::Registry&)>> sc;
        inst.Bind(e, a.id, &a); inst.SetServices(nullptr, nullptr, &ob, &sc);
        inst.SetPlayTime(0.0); inst.Begin(reg);
        if (!inst.HasError()) fail("instruction-limit", "infinite loop did not trip the budget guard");
        else pass();
    }

    return report;
}

} // namespace engine::vs
