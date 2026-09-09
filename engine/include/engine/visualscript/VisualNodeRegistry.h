#pragma once

// =============================================================================
// Visual Scripting — Pass 1 : node type registry + runtime node interface.
//
//   * INodeContext        — the ONLY surface a node executor may touch at runtime
//   * NodeDescriptor      — stable NodeTypeId, pins, pure/impure, executor       (Phase 10)
//   * VisualNodeRegistry  — data-driven; nodes are NOT hard-coded in editor code  (Phase 10)
//   * RegisterCoreNodes() — the minimum node set to prove the architecture        (Phase 15)
//
// Node executors are plain functions taking an INodeContext&, so they carry no
// dependency on the interpreter internals, the editor, or ImGui (Phase 23).
// =============================================================================

#include "VisualScriptTypes.h"
#include "VisualScriptAsset.h"

#include <engine/ecs/Components.h>
#include <engine/ecs/Registry.h>

#include <algorithm>
#include <cstddef>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

// Pass 2 service handles are passed through the context as opaque pointers so the
// Pass-1 core keeps no hard dependency on physics / input / the event payload type.
namespace engine {
class PhysicsWorld;
struct ScriptInputState;
struct ScriptEvent;
}

namespace engine::vs {

// Pass 4: gameplay nodes call the existing Script API through this host (defined in
// VisualScriptInstance.h). It exposes public wrappers over Script's protected helpers.
struct VisualScriptHost;

// Latent flow kinds (Pass 4, Phase 5). A latent node suspends the exec thread; the VS runtime
// resumes it later at the safe script phase — never a separate global scheduler. Declared before
// INodeContext because SuspendLatent takes it.
enum class LatentKind { Delay, WaitUntil, WaitEvent, WaitFixedSteps, WaitAnimation, Timer, Timeline, Task };

// ---- Runtime node interface (Phase 17/18) ----------------------------------
// A node executor reads typed inputs, writes typed outputs, and — for impure
// nodes — triggers exec outputs. Everything is routed through the interpreter,
// so nodes never see raw runtime state, pointers, or the graph document.
class INodeContext {
public:
    virtual ~INodeContext() = default;

    virtual const VisualNode& Node() const = 0;
    virtual ecs::Registry*    Registry() const = 0;
    virtual ecs::Entity       Entity() const = 0;       // the owning entity (Self)
    virtual float             DeltaTime() const = 0;

    // Reads an input pin: follows its link (or uses the pin default) and converts
    // the result to the pin's declared type. Never throws.
    virtual VisualValue ReadInput(const std::string& pinName) = 0;
    // Publishes a data output for this node (readable by downstream pure pulls).
    virtual void WriteOutput(const std::string& pinName, VisualValue value) = 0;
    // Triggers an exec output (impure nodes only); ignored for pure nodes.
    virtual void Continue(const std::string& execPinName) = 0;

    virtual VisualValue GetVariable(const std::string& name) = 0;
    virtual void        SetVariable(const std::string& name, VisualValue value) = 0;
    // Graph variables resolved by stable id (survives rename — Pass 3 Phase 11/12).
    virtual VisualValue GetVariableById(std::uint32_t variableId) = 0;
    virtual void        SetVariableById(std::uint32_t variableId, VisualValue value) = 0;

    // Authored constant on the node (e.g. a Const node's "value" property).
    virtual VisualValue Property(const std::string& name, const VisualValue& fallback) = 0;

    virtual void Log(const std::string& message) = 0;   // Debug.PrintString sink
    virtual void Fail(const std::string& message) = 0;  // records a runtime error (Phase 20)

    // ---- Pass 2 engine services (may be null when the host did not supply them) ----
    virtual PhysicsWorld* Physics() const = 0;                  // Phase 15
    virtual const ScriptInputState* Input() const = 0;          // Phase 17
    virtual void PublishEvent(const ScriptEvent& event) = 0;    // Phase 12: queued to the safe phase
    // Phase 16: queue a structural change (spawn / add / remove component) applied by the runtime
    // AFTER the current view iteration completes — never a structural mutation mid-execution.
    virtual void EnqueueStructural(std::function<void(ecs::Registry&)> op) = 0;

    // Pass 4 (Phase 5/6): suspend the current exec thread. The runtime resumes from the current
    // node's `resumePin` when the wait completes; a destroyed/disabled owner cancels it.
    virtual void SuspendLatent(LatentKind kind, float seconds, int fixedSteps,
                               const std::string& eventName, const std::string& resumePin) = 0;

    // Pass 4 (Phase 8/10-19): a host whose ScriptContext targets THIS entity + services, so gameplay
    // nodes reuse the existing combat/ability/inventory/dialogue/audio/... API instead of
    // reimplementing it. Null if no registry is bound.
    virtual VisualScriptHost* ScriptHost() = 0;

    // ---- Milestone 3: functions / reusable subgraphs -----------------------
    // Synchronously call a function by (graph, functionId). An empty/invalid graph means "this
    // graph". Inputs/outputs are keyed by the function's parameter names. Returns false (and
    // records a runtime error) on missing target, recursion, or depth overflow. Default no-op so
    // non-interpreter contexts (tests, tools) still satisfy the interface.
    virtual bool CallFunction(const AssetHandle& /*graph*/, FunctionId /*functionId*/,
                              const std::unordered_map<std::string, VisualValue>& /*inputs*/,
                              std::unordered_map<std::string, VisualValue>* /*outputs*/) { return false; }
    // Called by a Function.Return node executor for each of its data inputs.
    virtual void SetFunctionOutput(const std::string& /*name*/, VisualValue /*value*/) {}

    // Milestone 4: run the exec branch on this node's `execPinName` output to completion, synchronously
    // (used by loop nodes for each iteration). Returns false if execution errored / hit the budget so
    // the loop stops. Default no-op returns true (an empty body).
    virtual bool RunLoopBody(const std::string& /*execPinName*/) { return true; }

    // ---- Milestone 5: flow control + time ----------------------------------
    // Which exec INPUT pin triggered this execution (Gate/Do Once/Flip-Flop need to distinguish
    // In vs Reset vs Open...). Default true so single-input nodes are unaffected.
    virtual bool EnteredVia(const std::string& /*execInputName*/) { return true; }
    // Per-node persistent runtime state (survives across callbacks; cleared on Bind/HotReload). Keyed
    // by an arbitrary string on the CURRENT node.
    virtual VisualValue GetNodeState(const std::string& /*key*/, const VisualValue& fallback) { return fallback; }
    virtual void SetNodeState(const std::string& /*key*/, VisualValue /*value*/) {}
    // Read an input while forcing its pure source subtree to re-evaluate (loop conditions).
    virtual VisualValue ReadInputFresh(const std::string& pinName) { return ReadInput(pinName); }
    // Monotonic play-time seconds (advances with dt, so it respects pause + time dilation).
    virtual double Now() const { return 0.0; }
    // Start a repeating (or one-shot) timer that resumes `resumePin` every `period` seconds.
    virtual void SuspendTimer(float /*period*/, bool /*looping*/, const std::string& /*resumePin*/) {}
    // Start a timeline: each frame for `duration` seconds, write `alphaOutput` in [0,1] and resume
    // `updatePin`; on completion resume `finishedPin`. `loop` restarts instead of finishing.
    virtual void StartTimeline(float /*duration*/, bool /*loop*/, const std::string& /*alphaOutput*/,
                               const std::string& /*updatePin*/, const std::string& /*finishedPin*/) {}
    // Cancel every latent continuation owned by the CURRENT node (retriggerable delay, stop timer).
    virtual void CancelNodeLatent() {}

    // ---- Milestone 6: events / interfaces ----------------------------------
    // Send a typed event to ONE entity's graph (kNull => broadcast to the whole bus). Safe no-op if
    // the target has no matching handler (decoupled interface calls).
    virtual void PublishEventTo(ecs::Entity /*target*/, const ScriptEvent& /*event*/) {}
    // Bind/unbind THIS graph's response to an event name at runtime (dispatcher lifetime control).
    virtual void BindEvent(const std::string& /*eventName*/) {}
    virtual void UnbindEvent(const std::string& /*eventName*/) {}

    // Event builder (so nodes never touch the ScriptEvent type, which stays forward-declared here):
    // BeginEvent(name) -> EventArg(key,value)* -> SendEvent(target, broadcast).
    virtual void BeginEvent(const std::string& /*name*/) {}
    virtual void EventArg(const std::string& /*key*/, const VisualValue& /*value*/) {}
    virtual void SendEvent(ecs::Entity /*target*/, bool /*broadcast*/) {}

    // ---- Milestone 7: state-machine script API -----------------------------
    virtual std::uint32_t GetStateMachineState(std::uint32_t /*stateMachineId*/) { return 0; }
    virtual void RequestStateChange(std::uint32_t /*stateMachineId*/, std::uint32_t /*stateId*/) {}

    // ---- Milestone 8: async tasks ------------------------------------------
    // Start an async task on the CURRENT node and suspend this exec thread. `workSeconds` is the
    // expected work time (a task completes then unless timed out / cancelled / failed first);
    // `timeout` <= 0 means none. The pins name this node's exec outputs to resume per outcome.
    // Returns a task handle (0 on failure). Ownership is this instance/entity — destruction cancels it.
    virtual int StartAsyncTask(const std::string& /*kind*/, float /*workSeconds*/, float /*timeout*/,
                               const std::string& /*completedPin*/, const std::string& /*failedPin*/,
                               const std::string& /*cancelledPin*/, const std::string& /*timedOutPin*/) { return 0; }
    virtual void CancelAsyncTask(int /*handle*/) {}
    virtual void CompleteAsyncTask(int /*handle*/, bool /*success*/) {}
    virtual bool IsAsyncTaskActive(int /*handle*/) { return false; }
};

using NodeExecutor = std::function<void(INodeContext&)>;

// ---- Descriptors (Phase 10) ------------------------------------------------
struct NodePinDesc {
    std::string  name;
    PinDirection direction = PinDirection::Input;
    PinKind      kind = PinKind::Data;
    ValueType    type = ValueType::Float;
    ValueType    elementType = ValueType::Float;   // Array/Map element (Milestone 4)
    ValueType    keyType = ValueType::String;      // Map key
    std::uint32_t typeId = 0;                       // Struct/Enum def id
    VisualValue  defaultValue;
};

struct NodeDescriptor {
    std::string   typeId;        // stable, e.g. "Core.Branch"
    std::string   displayName;
    std::string   category;
    bool          pure = false;  // pure = data-only, no exec pins, side-effect free
    std::uint32_t version = 1;
    std::vector<NodePinDesc> pins;
    NodeExecutor  execute;
    std::string   eventName;     // set only on reflected event-entry nodes (matches ScriptEvent::name)

    bool IsEvent() const { return category == "Event"; }
    const NodePinDesc* FindPin(const std::string& name, PinDirection dir) const {
        for (const NodePinDesc& p : pins) if (p.name == name && p.direction == dir) return &p;
        return nullptr;
    }
};

// ---- Log sink (Phase 20 / Debug.PrintString) -------------------------------
inline std::function<void(const std::string&)>& VisualScriptLogHandlerRef() {
    static std::function<void(const std::string&)> handler;
    return handler;
}
inline void SetVisualScriptLogHandler(std::function<void(const std::string&)> handler) {
    VisualScriptLogHandlerRef() = std::move(handler);
}

// ---- Registry (Phase 10) ---------------------------------------------------
class VisualNodeRegistry {
public:
    static VisualNodeRegistry& Instance() {
        static VisualNodeRegistry registry;
        return registry;
    }

    void Register(NodeDescriptor descriptor) {
        m_descriptors[descriptor.typeId] = std::move(descriptor);
    }
    const NodeDescriptor* Find(const std::string& typeId) const {
        auto it = m_descriptors.find(typeId);
        return it == m_descriptors.end() ? nullptr : &it->second;
    }
    bool Has(const std::string& typeId) const { return m_descriptors.count(typeId) != 0; }
    std::vector<std::string> TypeIds() const {
        std::vector<std::string> ids;
        ids.reserve(m_descriptors.size());
        for (const auto& kv : m_descriptors) ids.push_back(kv.first);
        std::sort(ids.begin(), ids.end());
        return ids;
    }

    // Build an authored node pre-populated from a descriptor's pins (editor helper).
    VisualNode MakeNode(const std::string& typeId, NodeId id) const {
        VisualNode node; node.id = id; node.typeId = typeId;
        const NodeDescriptor* desc = Find(typeId);
        if (!desc) return node;
        PinId nextPin = 1;
        for (const NodePinDesc& pin : desc->pins) {
            VisualPin authored;
            authored.id = nextPin++;
            authored.direction = pin.direction;
            authored.kind = pin.kind;
            authored.type = pin.type;
            authored.elementType = pin.elementType;   // Milestone 4 container metadata
            authored.keyType = pin.keyType;
            authored.typeId = pin.typeId;
            authored.name = pin.name;
            authored.defaultValue = pin.defaultValue;
            (pin.direction == PinDirection::Input ? node.inputs : node.outputs).push_back(authored);
        }
        return node;
    }

private:
    std::unordered_map<std::string, NodeDescriptor> m_descriptors;
};

// ---- Core node set (Phase 15) ----------------------------------------------
namespace detail {

inline NodePinDesc Exec(const char* name, PinDirection dir) {
    NodePinDesc p; p.name = name; p.direction = dir; p.kind = PinKind::Exec;
    p.type = ValueType::Exec; p.defaultValue = VisualValue::Exec(); return p;
}
inline NodePinDesc Data(const char* name, PinDirection dir, ValueType type, VisualValue def) {
    NodePinDesc p; p.name = name; p.direction = dir; p.kind = PinKind::Data;
    p.type = type; p.defaultValue = std::move(def); return p;
}

// If an Entity input is left at kNull it means "Self".
inline ecs::Entity ResolveEntityInput(INodeContext& ctx, const char* pin) {
    const ecs::Entity e = ctx.ReadInput(pin).AsEntity();
    return e == ecs::kNull ? ctx.Entity() : e;
}

} // namespace detail

inline void RegisterCoreNodes() {
    using detail::Exec;
    using detail::Data;
    VisualNodeRegistry& reg = VisualNodeRegistry::Instance();
    if (reg.Has("Event.BeginPlay")) return;   // idempotent

    // ---- Events ------------------------------------------------------------
    reg.Register({"Event.BeginPlay", "Begin Play", "Event", false, 1,
        { Exec("Then", PinDirection::Output) },
        [](INodeContext& c) { c.Continue("Then"); }});

    reg.Register({"Event.Update", "Update", "Event", false, 1,
        { Exec("Then", PinDirection::Output),
          Data("DeltaTime", PinDirection::Output, ValueType::Float, VisualValue::Float(0.0f)) },
        [](INodeContext& c) { c.WriteOutput("DeltaTime", VisualValue::Float(c.DeltaTime())); c.Continue("Then"); }});

    reg.Register({"Event.FixedUpdate", "Fixed Update", "Event", false, 1,
        { Exec("Then", PinDirection::Output),
          Data("DeltaTime", PinDirection::Output, ValueType::Float, VisualValue::Float(0.0f)) },
        [](INodeContext& c) { c.WriteOutput("DeltaTime", VisualValue::Float(c.DeltaTime())); c.Continue("Then"); }});

    // ---- Flow --------------------------------------------------------------
    reg.Register({"Core.Branch", "Branch", "Flow", false, 1,
        { Exec("In", PinDirection::Input),
          Data("Condition", PinDirection::Input, ValueType::Bool, VisualValue::Bool(false)),
          Exec("True", PinDirection::Output),
          Exec("False", PinDirection::Output) },
        [](INodeContext& c) { c.Continue(c.ReadInput("Condition").AsBool() ? "True" : "False"); }});

    reg.Register({"Core.Sequence", "Sequence", "Flow", false, 1,
        { Exec("In", PinDirection::Input),
          Exec("Then0", PinDirection::Output),
          Exec("Then1", PinDirection::Output) },
        [](INodeContext& c) { c.Continue("Then0"); c.Continue("Then1"); }});

    // ---- Latent flow (Pass 4, Phase 5) — suspend + resume via the VS runtime -----
    reg.Register({"Flow.Delay", "Delay", "Flow", false, 1,
        { Exec("In", PinDirection::Input),
          Data("Seconds", PinDirection::Input, ValueType::Float, VisualValue::Float(1.0f)),
          Exec("Then", PinDirection::Output) },
        [](INodeContext& c) { c.SuspendLatent(LatentKind::Delay, c.ReadInput("Seconds").AsFloat(), 0, "", "Then"); }});

    reg.Register({"Flow.WaitUntil", "Wait Until", "Flow", false, 1,
        { Exec("In", PinDirection::Input),
          Data("Condition", PinDirection::Input, ValueType::Bool, VisualValue::Bool(false)),
          Exec("Then", PinDirection::Output) },
        [](INodeContext& c) {
            if (c.ReadInput("Condition").AsBool()) c.Continue("Then");       // already satisfied
            else c.SuspendLatent(LatentKind::WaitUntil, 0.0f, 0, "", "Then");
        }});

    reg.Register({"Flow.WaitForEvent", "Wait For Event", "Flow", false, 1,
        { Exec("In", PinDirection::Input),
          Data("Event Name", PinDirection::Input, ValueType::String, VisualValue::Str("")),
          Exec("Then", PinDirection::Output) },
        [](INodeContext& c) { c.SuspendLatent(LatentKind::WaitEvent, 0.0f, 0, c.ReadInput("Event Name").AsString(), "Then"); }});

    reg.Register({"Flow.WaitForFixedSteps", "Wait For Fixed Steps", "Flow", false, 1,
        { Exec("In", PinDirection::Input),
          Data("Steps", PinDirection::Input, ValueType::Int, VisualValue::Int(1)),
          Exec("Then", PinDirection::Output) },
        [](INodeContext& c) { c.SuspendLatent(LatentKind::WaitFixedSteps, 0.0f, c.ReadInput("Steps").AsInt(), "", "Then"); }});

    // Phase 7/9: suspend until the current animation action finishes (via the runtime animation API).
    reg.Register({"Flow.WaitForAnimation", "Wait For Animation", "Flow", false, 1,
        { Exec("In", PinDirection::Input), Exec("Then", PinDirection::Output) },
        [](INodeContext& c) { c.SuspendLatent(LatentKind::WaitAnimation, 0.0f, 0, "", "Then"); }});

    // ---- Constants (pure) --------------------------------------------------
    reg.Register({"Const.Bool", "Bool", "Constant", true, 1,
        { Data("Value", PinDirection::Output, ValueType::Bool, VisualValue::Bool(false)) },
        [](INodeContext& c) { c.WriteOutput("Value", c.Property("value", VisualValue::Bool(false))); }});
    reg.Register({"Const.Int", "Int", "Constant", true, 1,
        { Data("Value", PinDirection::Output, ValueType::Int, VisualValue::Int(0)) },
        [](INodeContext& c) { c.WriteOutput("Value", c.Property("value", VisualValue::Int(0))); }});
    reg.Register({"Const.Float", "Float", "Constant", true, 1,
        { Data("Value", PinDirection::Output, ValueType::Float, VisualValue::Float(0.0f)) },
        [](INodeContext& c) { c.WriteOutput("Value", c.Property("value", VisualValue::Float(0.0f))); }});
    reg.Register({"Const.String", "String", "Constant", true, 1,
        { Data("Value", PinDirection::Output, ValueType::String, VisualValue::Str("")) },
        [](INodeContext& c) { c.WriteOutput("Value", c.Property("value", VisualValue::Str(""))); }});
    reg.Register({"Const.Vector2", "Vector2", "Constant", true, 1,
        { Data("Value", PinDirection::Output, ValueType::Vector2, VisualValue::Vec2(glm::vec2(0.0f))) },
        [](INodeContext& c) { c.WriteOutput("Value", c.Property("value", VisualValue::Vec2(glm::vec2(0.0f)))); }});
    reg.Register({"Const.Vector3", "Vector3", "Constant", true, 1,
        { Data("Value", PinDirection::Output, ValueType::Vector3, VisualValue::Vec3(glm::vec3(0.0f))) },
        [](INodeContext& c) { c.WriteOutput("Value", c.Property("value", VisualValue::Vec3(glm::vec3(0.0f)))); }});

    // ---- Math (pure) -------------------------------------------------------
    auto binaryFloat = [](const char* id, const char* label,
                          std::function<float(float, float, INodeContext&)> op) {
        VisualNodeRegistry::Instance().Register({id, label, "Math", true, 1,
            { Data("A", PinDirection::Input, ValueType::Float, VisualValue::Float(0.0f)),
              Data("B", PinDirection::Input, ValueType::Float, VisualValue::Float(0.0f)),
              Data("Result", PinDirection::Output, ValueType::Float, VisualValue::Float(0.0f)) },
            [op](INodeContext& c) {
                const float a = c.ReadInput("A").AsFloat();
                const float b = c.ReadInput("B").AsFloat();
                c.WriteOutput("Result", VisualValue::Float(op(a, b, c)));
            }});
    };
    binaryFloat("Math.AddFloat", "Add (Float)", [](float a, float b, INodeContext&) { return a + b; });
    binaryFloat("Math.SubtractFloat", "Subtract (Float)", [](float a, float b, INodeContext&) { return a - b; });
    binaryFloat("Math.MultiplyFloat", "Multiply (Float)", [](float a, float b, INodeContext&) { return a * b; });
    binaryFloat("Math.DivideFloat", "Divide (Float)", [](float a, float b, INodeContext& c) {
        if (b == 0.0f) { c.Fail("Divide by zero"); return 0.0f; }   // Phase 20: recoverable
        return a / b;
    });

    // Vector helpers needed by the movement test graph (Phase 26).
    reg.Register({"Math.AddVector3", "Add (Vector3)", "Math", true, 1,
        { Data("A", PinDirection::Input, ValueType::Vector3, VisualValue::Vec3(glm::vec3(0.0f))),
          Data("B", PinDirection::Input, ValueType::Vector3, VisualValue::Vec3(glm::vec3(0.0f))),
          Data("Result", PinDirection::Output, ValueType::Vector3, VisualValue::Vec3(glm::vec3(0.0f))) },
        [](INodeContext& c) { c.WriteOutput("Result", VisualValue::Vec3(c.ReadInput("A").AsVec3() + c.ReadInput("B").AsVec3())); }});

    reg.Register({"Math.ScaleVector3", "Scale (Vector3)", "Math", true, 1,
        { Data("V", PinDirection::Input, ValueType::Vector3, VisualValue::Vec3(glm::vec3(0.0f))),
          Data("S", PinDirection::Input, ValueType::Float, VisualValue::Float(1.0f)),
          Data("Result", PinDirection::Output, ValueType::Vector3, VisualValue::Vec3(glm::vec3(0.0f))) },
        [](INodeContext& c) { c.WriteOutput("Result", VisualValue::Vec3(c.ReadInput("V").AsVec3() * c.ReadInput("S").AsFloat())); }});

    // Lerp helpers (Milestone 5) — pair with a Timeline's Alpha to animate a door / value.
    reg.Register({"Math.LerpFloat", "Lerp (Float)", "Math", true, 1,
        { Data("A", PinDirection::Input, ValueType::Float, VisualValue::Float(0.0f)),
          Data("B", PinDirection::Input, ValueType::Float, VisualValue::Float(1.0f)),
          Data("Alpha", PinDirection::Input, ValueType::Float, VisualValue::Float(0.0f)),
          Data("Result", PinDirection::Output, ValueType::Float, VisualValue::Float(0.0f)) },
        [](INodeContext& c) {
            const float a = c.ReadInput("A").AsFloat(), b = c.ReadInput("B").AsFloat(), t = c.ReadInput("Alpha").AsFloat();
            c.WriteOutput("Result", VisualValue::Float(a + (b - a) * t));
        }});
    reg.Register({"Math.LerpVector3", "Lerp (Vector3)", "Math", true, 1,
        { Data("A", PinDirection::Input, ValueType::Vector3, VisualValue::Vec3(glm::vec3(0.0f))),
          Data("B", PinDirection::Input, ValueType::Vector3, VisualValue::Vec3(glm::vec3(0.0f))),
          Data("Alpha", PinDirection::Input, ValueType::Float, VisualValue::Float(0.0f)),
          Data("Result", PinDirection::Output, ValueType::Vector3, VisualValue::Vec3(glm::vec3(0.0f))) },
        [](INodeContext& c) {
            const glm::vec3 a = c.ReadInput("A").AsVec3(), b = c.ReadInput("B").AsVec3();
            const float t = c.ReadInput("Alpha").AsFloat();
            c.WriteOutput("Result", VisualValue::Vec3(a + (b - a) * t));
        }});

    // ---- Comparison (pure; two same-type inputs -> Bool) --------------------
    auto compareFloat = [](const char* id, const char* label,
                           std::function<bool(float, float)> op) {
        VisualNodeRegistry::Instance().Register({id, label, "Compare", true, 1,
            { Data("A", PinDirection::Input, ValueType::Float, VisualValue::Float(0.0f)),
              Data("B", PinDirection::Input, ValueType::Float, VisualValue::Float(0.0f)),
              Data("Result", PinDirection::Output, ValueType::Bool, VisualValue::Bool(false)) },
            [op](INodeContext& c) { c.WriteOutput("Result", VisualValue::Bool(op(c.ReadInput("A").AsFloat(), c.ReadInput("B").AsFloat()))); }});
    };
    compareFloat("Compare.EqualFloat", "Equal (Float)", [](float a, float b) { return a == b; });
    compareFloat("Compare.NotEqualFloat", "Not Equal (Float)", [](float a, float b) { return a != b; });
    compareFloat("Compare.Greater", "Greater (Float)", [](float a, float b) { return a > b; });
    compareFloat("Compare.GreaterEqual", "Greater or Equal (Float)", [](float a, float b) { return a >= b; });
    compareFloat("Compare.Less", "Less (Float)", [](float a, float b) { return a < b; });
    compareFloat("Compare.LessEqual", "Less or Equal (Float)", [](float a, float b) { return a <= b; });

    auto compareInt = [](const char* id, const char* label,
                         std::function<bool(int, int)> op) {
        VisualNodeRegistry::Instance().Register({id, label, "Compare", true, 1,
            { Data("A", PinDirection::Input, ValueType::Int, VisualValue::Int(0)),
              Data("B", PinDirection::Input, ValueType::Int, VisualValue::Int(0)),
              Data("Result", PinDirection::Output, ValueType::Bool, VisualValue::Bool(false)) },
            [op](INodeContext& c) { c.WriteOutput("Result", VisualValue::Bool(op(c.ReadInput("A").AsInt(), c.ReadInput("B").AsInt()))); }});
    };
    compareInt("Compare.EqualInt", "Equal (Int)", [](int a, int b) { return a == b; });
    compareInt("Compare.NotEqualInt", "Not Equal (Int)", [](int a, int b) { return a != b; });
    compareInt("Compare.GreaterInt", "Greater (Int)", [](int a, int b) { return a > b; });
    compareInt("Compare.LessInt", "Less (Int)", [](int a, int b) { return a < b; });

    reg.Register({"Compare.EqualBool", "Equal (Bool)", "Compare", true, 1,
        { Data("A", PinDirection::Input, ValueType::Bool, VisualValue::Bool(false)),
          Data("B", PinDirection::Input, ValueType::Bool, VisualValue::Bool(false)),
          Data("Result", PinDirection::Output, ValueType::Bool, VisualValue::Bool(false)) },
        [](INodeContext& c) { c.WriteOutput("Result", VisualValue::Bool(c.ReadInput("A").AsBool() == c.ReadInput("B").AsBool())); }});
    reg.Register({"Compare.EqualString", "Equal (String)", "Compare", true, 1,
        { Data("A", PinDirection::Input, ValueType::String, VisualValue::Str("")),
          Data("B", PinDirection::Input, ValueType::String, VisualValue::Str("")),
          Data("Result", PinDirection::Output, ValueType::Bool, VisualValue::Bool(false)) },
        [](INodeContext& c) { c.WriteOutput("Result", VisualValue::Bool(c.ReadInput("A").AsString() == c.ReadInput("B").AsString())); }});
    reg.Register({"Compare.EqualEntity", "Equal (Entity)", "Compare", true, 1,
        { Data("A", PinDirection::Input, ValueType::Entity, VisualValue::Ent(ecs::kNull)),
          Data("B", PinDirection::Input, ValueType::Entity, VisualValue::Ent(ecs::kNull)),
          Data("Result", PinDirection::Output, ValueType::Bool, VisualValue::Bool(false)) },
        [](INodeContext& c) { c.WriteOutput("Result", VisualValue::Bool(c.ReadInput("A").AsEntity() == c.ReadInput("B").AsEntity())); }});

    // ---- Boolean logic (pure) ----------------------------------------------
    reg.Register({"Logic.And", "AND", "Logic", true, 1,
        { Data("A", PinDirection::Input, ValueType::Bool, VisualValue::Bool(false)),
          Data("B", PinDirection::Input, ValueType::Bool, VisualValue::Bool(false)),
          Data("Result", PinDirection::Output, ValueType::Bool, VisualValue::Bool(false)) },
        [](INodeContext& c) { c.WriteOutput("Result", VisualValue::Bool(c.ReadInput("A").AsBool() && c.ReadInput("B").AsBool())); }});
    reg.Register({"Logic.Or", "OR", "Logic", true, 1,
        { Data("A", PinDirection::Input, ValueType::Bool, VisualValue::Bool(false)),
          Data("B", PinDirection::Input, ValueType::Bool, VisualValue::Bool(false)),
          Data("Result", PinDirection::Output, ValueType::Bool, VisualValue::Bool(false)) },
        [](INodeContext& c) { c.WriteOutput("Result", VisualValue::Bool(c.ReadInput("A").AsBool() || c.ReadInput("B").AsBool())); }});
    reg.Register({"Logic.Not", "NOT", "Logic", true, 1,
        { Data("In", PinDirection::Input, ValueType::Bool, VisualValue::Bool(false)),
          Data("Result", PinDirection::Output, ValueType::Bool, VisualValue::Bool(false)) },
        [](INodeContext& c) { c.WriteOutput("Result", VisualValue::Bool(!c.ReadInput("In").AsBool())); }});

    // Compare: property "op" in { ==, !=, <, <=, >, >= }; default ">".
    reg.Register({"Math.CompareFloat", "Compare (Float)", "Math", true, 1,
        { Data("A", PinDirection::Input, ValueType::Float, VisualValue::Float(0.0f)),
          Data("B", PinDirection::Input, ValueType::Float, VisualValue::Float(0.0f)),
          Data("Result", PinDirection::Output, ValueType::Bool, VisualValue::Bool(false)) },
        [](INodeContext& c) {
            const float a = c.ReadInput("A").AsFloat();
            const float b = c.ReadInput("B").AsFloat();
            const std::string op = c.Property("op", VisualValue::Str(">")).AsString();
            bool r = false;
            if (op == "==") r = a == b; else if (op == "!=") r = a != b;
            else if (op == "<") r = a < b; else if (op == "<=") r = a <= b;
            else if (op == ">=") r = a >= b; else r = a > b;
            c.WriteOutput("Result", VisualValue::Bool(r));
        }});

    // ---- Entity (pure) -----------------------------------------------------
    reg.Register({"Entity.Self", "Self", "Entity", true, 1,
        { Data("Self", PinDirection::Output, ValueType::Entity, VisualValue::Ent(ecs::kNull)) },
        [](INodeContext& c) { c.WriteOutput("Self", VisualValue::Ent(c.Entity())); }});

    reg.Register({"Entity.IsValid", "Is Valid", "Entity", true, 1,
        { Data("Entity", PinDirection::Input, ValueType::Entity, VisualValue::Ent(ecs::kNull)),
          Data("Valid", PinDirection::Output, ValueType::Bool, VisualValue::Bool(false)) },
        [](INodeContext& c) {
            const ecs::Entity e = detail::ResolveEntityInput(c, "Entity");
            const bool valid = c.Registry() && c.Registry()->Valid(e);
            c.WriteOutput("Valid", VisualValue::Bool(valid));
        }});

    // ---- Transform (pure get / ECS-safe set) — Phase 16 --------------------
    reg.Register({"Transform.GetPosition", "Get Position", "Transform", true, 1,
        { Data("Entity", PinDirection::Input, ValueType::Entity, VisualValue::Ent(ecs::kNull)),
          Data("Position", PinDirection::Output, ValueType::Vector3, VisualValue::Vec3(glm::vec3(0.0f))) },
        [](INodeContext& c) {
            const ecs::Entity e = detail::ResolveEntityInput(c, "Entity");
            glm::vec3 pos(0.0f);
            if (c.Registry()) if (auto* t = c.Registry()->TryGet<ecs::Transform>(e)) pos = t->position;
            c.WriteOutput("Position", VisualValue::Vec3(pos));
        }});

    reg.Register({"Transform.SetPosition", "Set Position", "Transform", false, 1,
        { Exec("In", PinDirection::Input),
          Data("Entity", PinDirection::Input, ValueType::Entity, VisualValue::Ent(ecs::kNull)),
          Data("Position", PinDirection::Input, ValueType::Vector3, VisualValue::Vec3(glm::vec3(0.0f))),
          Exec("Then", PinDirection::Output) },
        [](INodeContext& c) {
            const ecs::Entity e = detail::ResolveEntityInput(c, "Entity");
            const glm::vec3 pos = c.ReadInput("Position").AsVec3();
            // ECS-safe write: Patch<Transform> bumps the component revision so
            // RenderScene / PhysicsScene see the change (never a raw pointer poke).
            if (!c.Registry() || !c.Registry()->Patch<ecs::Transform>(
                    e, [&](ecs::Transform& t) { t.position = pos; })) {
                c.Fail("Set Position: entity has no Transform");
            }
            c.Continue("Then");
        }});

    // ---- Variables (resolved by stable VariableId in the node's "var" property) ----
    reg.Register({"Var.Get", "Get Variable", "Variable", true, 1,
        { Data("Value", PinDirection::Output, ValueType::Float, VisualValue::Float(0.0f)) },
        [](INodeContext& c) {
            c.WriteOutput("Value", c.GetVariableById(
                static_cast<std::uint32_t>(c.Property("var", VisualValue::Int(0)).AsInt())));
        }});
    reg.Register({"Var.Set", "Set Variable", "Variable", false, 1,
        { Exec("In", PinDirection::Input),
          Data("Value", PinDirection::Input, ValueType::Float, VisualValue::Float(0.0f)),
          Exec("Then", PinDirection::Output),
          Data("Value", PinDirection::Output, ValueType::Float, VisualValue::Float(0.0f)) },  // pass-through
        [](INodeContext& c) {
            const VisualValue v = c.ReadInput("Value");
            c.SetVariableById(static_cast<std::uint32_t>(c.Property("var", VisualValue::Int(0)).AsInt()), v);
            c.WriteOutput("Value", v);
            c.Continue("Then");
        }});

    // ---- Functions / subgraphs (Milestone 3) -------------------------------
    // Entry/Return carry dynamic data pins the editor builds from the function signature; the
    // executors iterate the AUTHORED pins so they work for any signature. Call resolves the target
    // function and runs it synchronously through the interpreter (INodeContext::CallFunction).
    reg.Register({"Function.Entry", "Function Entry", "Function", false, 1,
        { Exec("Then", PinDirection::Output) },
        [](INodeContext& c) { c.Continue("Then"); }});   // data outputs are pre-seeded by the caller

    reg.Register({"Function.Return", "Return", "Function", false, 1,
        { Exec("In", PinDirection::Input) },
        [](INodeContext& c) {
            for (const VisualPin& p : c.Node().inputs)
                if (p.kind == PinKind::Data) c.SetFunctionOutput(p.name, c.ReadInput(p.name));
        }});

    reg.Register({"Function.Call", "Call Function", "Function", false, 1,
        { Exec("In", PinDirection::Input), Exec("Then", PinDirection::Output) },
        [](INodeContext& c) {
            std::unordered_map<std::string, VisualValue> inputs;
            for (const VisualPin& p : c.Node().inputs)
                if (p.kind == PinKind::Data) inputs[p.name] = c.ReadInput(p.name);
            const AssetHandle target = c.Property("graph", VisualValue::Asset(AssetHandle{})).AsAsset();
            const FunctionId fid = static_cast<FunctionId>(c.Property("func", VisualValue::Int(0)).AsInt());
            std::unordered_map<std::string, VisualValue> outputs;
            if (c.CallFunction(target, fid, inputs, &outputs)) {
                for (const auto& kv : outputs) c.WriteOutput(kv.first, kv.second);
                c.Continue("Then");
            }
        }});

    // ---- Collections: Arrays (Milestone 4) ---------------------------------
    // Element pins are typed Float as a placeholder; the editor specialises them to the array's
    // element type. Executors are element-type-agnostic and operate on the VisualValue directly.
    // Mutation nodes CloneDeep() so the input array is never aliased/mutated in place.
    {
        auto arrIn  = [](const char* n) { NodePinDesc p; p.name=n; p.direction=PinDirection::Input;  p.kind=PinKind::Data; p.type=ValueType::Array; p.defaultValue=VisualValue::MakeArray(ValueType::Float); return p; };
        auto arrOut = [](const char* n) { NodePinDesc p; p.name=n; p.direction=PinDirection::Output; p.kind=PinKind::Data; p.type=ValueType::Array; p.defaultValue=VisualValue::MakeArray(ValueType::Float); return p; };

        reg.Register({"Array.Make", "Make Array", "Array", true, 1,
            { arrOut("Array") },
            [](INodeContext& c) {
                ValueType et = ValueType::Float;
                for (const VisualPin& p : c.Node().outputs) if (p.name == "Array") et = p.elementType;
                c.WriteOutput("Array", VisualValue::MakeArray(et));
            }});
        reg.Register({"Array.Length", "Array Length", "Array", true, 1,
            { arrIn("Array"), Data("Length", PinDirection::Output, ValueType::Int, VisualValue::Int(0)) },
            [](INodeContext& c) { c.WriteOutput("Length", VisualValue::Int(static_cast<int>(c.ReadInput("Array").Elements().size()))); }});
        reg.Register({"Array.Get", "Array Get", "Array", true, 1,
            { arrIn("Array"), Data("Index", PinDirection::Input, ValueType::Int, VisualValue::Int(0)),
              Data("Element", PinDirection::Output, ValueType::Float, VisualValue::Float(0.0f)) },
            [](INodeContext& c) {
                const VisualValue arr = c.ReadInput("Array");
                const int i = c.ReadInput("Index").AsInt();
                if (i < 0 || i >= static_cast<int>(arr.Elements().size())) { c.Log("Array.Get: index out of range"); return; }
                c.WriteOutput("Element", arr.Elements()[static_cast<std::size_t>(i)]);
            }});
        reg.Register({"Array.Contains", "Array Contains", "Array", true, 1,
            { arrIn("Array"), Data("Item", PinDirection::Input, ValueType::Float, VisualValue::Float(0.0f)),
              Data("Found", PinDirection::Output, ValueType::Bool, VisualValue::Bool(false)) },
            [](INodeContext& c) {
                const VisualValue arr = c.ReadInput("Array"); const VisualValue item = c.ReadInput("Item");
                bool found = false; for (const VisualValue& e : arr.Elements()) if (e.Equals(item)) { found = true; break; }
                c.WriteOutput("Found", VisualValue::Bool(found));
            }});
        reg.Register({"Array.Find", "Array Find", "Array", true, 1,
            { arrIn("Array"), Data("Item", PinDirection::Input, ValueType::Float, VisualValue::Float(0.0f)),
              Data("Index", PinDirection::Output, ValueType::Int, VisualValue::Int(-1)) },
            [](INodeContext& c) {
                const VisualValue arr = c.ReadInput("Array"); const VisualValue item = c.ReadInput("Item");
                int idx = -1; for (std::size_t i = 0; i < arr.Elements().size(); ++i) if (arr.Elements()[i].Equals(item)) { idx = static_cast<int>(i); break; }
                c.WriteOutput("Index", VisualValue::Int(idx));
            }});
        reg.Register({"Array.Add", "Array Add", "Array", false, 1,
            { Exec("In", PinDirection::Input), arrIn("Array"),
              Data("Item", PinDirection::Input, ValueType::Float, VisualValue::Float(0.0f)),
              Exec("Then", PinDirection::Output), arrOut("Array") },
            [](INodeContext& c) {
                VisualValue arr = c.ReadInput("Array").CloneDeep();
                arr.Elements().push_back(c.ReadInput("Item"));
                c.WriteOutput("Array", arr); c.Continue("Then");
            }});
        reg.Register({"Array.Set", "Array Set", "Array", false, 1,
            { Exec("In", PinDirection::Input), arrIn("Array"),
              Data("Index", PinDirection::Input, ValueType::Int, VisualValue::Int(0)),
              Data("Item", PinDirection::Input, ValueType::Float, VisualValue::Float(0.0f)),
              Exec("Then", PinDirection::Output), arrOut("Array") },
            [](INodeContext& c) {
                VisualValue arr = c.ReadInput("Array").CloneDeep();
                const int i = c.ReadInput("Index").AsInt();
                if (i >= 0 && i < static_cast<int>(arr.Elements().size())) arr.Elements()[static_cast<std::size_t>(i)] = c.ReadInput("Item");
                else c.Log("Array.Set: index out of range");
                c.WriteOutput("Array", arr); c.Continue("Then");
            }});
        reg.Register({"Array.Insert", "Array Insert", "Array", false, 1,
            { Exec("In", PinDirection::Input), arrIn("Array"),
              Data("Index", PinDirection::Input, ValueType::Int, VisualValue::Int(0)),
              Data("Item", PinDirection::Input, ValueType::Float, VisualValue::Float(0.0f)),
              Exec("Then", PinDirection::Output), arrOut("Array") },
            [](INodeContext& c) {
                VisualValue arr = c.ReadInput("Array").CloneDeep();
                int i = c.ReadInput("Index").AsInt();
                i = i < 0 ? 0 : (i > static_cast<int>(arr.Elements().size()) ? static_cast<int>(arr.Elements().size()) : i);
                arr.Elements().insert(arr.Elements().begin() + i, c.ReadInput("Item"));
                c.WriteOutput("Array", arr); c.Continue("Then");
            }});
        reg.Register({"Array.RemoveAt", "Array Remove At", "Array", false, 1,
            { Exec("In", PinDirection::Input), arrIn("Array"),
              Data("Index", PinDirection::Input, ValueType::Int, VisualValue::Int(0)),
              Exec("Then", PinDirection::Output), arrOut("Array") },
            [](INodeContext& c) {
                VisualValue arr = c.ReadInput("Array").CloneDeep();
                const int i = c.ReadInput("Index").AsInt();
                if (i >= 0 && i < static_cast<int>(arr.Elements().size())) arr.Elements().erase(arr.Elements().begin() + i);
                else c.Log("Array.RemoveAt: index out of range");
                c.WriteOutput("Array", arr); c.Continue("Then");
            }});
        reg.Register({"Array.Clear", "Array Clear", "Array", false, 1,
            { Exec("In", PinDirection::Input), arrIn("Array"), Exec("Then", PinDirection::Output), arrOut("Array") },
            [](INodeContext& c) {
                VisualValue arr = c.ReadInput("Array").CloneDeep();
                arr.Elements().clear();
                c.WriteOutput("Array", arr); c.Continue("Then");
            }});
        reg.Register({"Array.ForEach", "For Each", "Array", false, 1,
            { Exec("In", PinDirection::Input), arrIn("Array"),
              Exec("Loop Body", PinDirection::Output),
              Data("Element", PinDirection::Output, ValueType::Float, VisualValue::Float(0.0f)),
              Data("Index", PinDirection::Output, ValueType::Int, VisualValue::Int(0)),
              Exec("Completed", PinDirection::Output) },
            [](INodeContext& c) {
                const VisualValue arr = c.ReadInput("Array");
                const std::size_t n = arr.Elements().size();
                for (std::size_t i = 0; i < n; ++i) {
                    c.WriteOutput("Element", arr.Elements()[i]);
                    c.WriteOutput("Index", VisualValue::Int(static_cast<int>(i)));
                    if (!c.RunLoopBody("Loop Body")) return;   // error / budget stops the loop
                }
                c.Continue("Completed");
            }});
    }

    // ---- Collections: Maps (Milestone 4) -----------------------------------
    {
        auto mapIn  = [](const char* n) { NodePinDesc p; p.name=n; p.direction=PinDirection::Input;  p.kind=PinKind::Data; p.type=ValueType::Map; p.defaultValue=VisualValue::MakeMap(ValueType::String, ValueType::Float); return p; };
        auto mapOut = [](const char* n) { NodePinDesc p; p.name=n; p.direction=PinDirection::Output; p.kind=PinKind::Data; p.type=ValueType::Map; p.defaultValue=VisualValue::MakeMap(ValueType::String, ValueType::Float); return p; };

        reg.Register({"Map.Make", "Make Map", "Map", true, 1,
            { mapOut("Map") },
            [](INodeContext& c) {
                ValueType kt = ValueType::String, vt = ValueType::Float;
                for (const VisualPin& p : c.Node().outputs) if (p.name == "Map") { kt = p.keyType; vt = p.elementType; }
                c.WriteOutput("Map", VisualValue::MakeMap(kt, vt));
            }});
        reg.Register({"Map.Add", "Map Add", "Map", false, 1,
            { Exec("In", PinDirection::Input), mapIn("Map"),
              Data("Key", PinDirection::Input, ValueType::String, VisualValue::Str("")),
              Data("Value", PinDirection::Input, ValueType::Float, VisualValue::Float(0.0f)),
              Exec("Then", PinDirection::Output), mapOut("Map") },
            [](INodeContext& c) {
                VisualValue m = c.ReadInput("Map").CloneDeep();
                const VisualValue k = c.ReadInput("Key"); const VisualValue v = c.ReadInput("Value");
                bool set = false;
                for (std::size_t i = 0; i < m.MapKeys().size(); ++i) if (m.MapKeys()[i].Equals(k)) { m.MapValues()[i] = v; set = true; break; }
                if (!set) { m.MapKeys().push_back(k); m.MapValues().push_back(v); }
                c.WriteOutput("Map", m); c.Continue("Then");
            }});
        reg.Register({"Map.Find", "Map Find", "Map", true, 1,
            { mapIn("Map"), Data("Key", PinDirection::Input, ValueType::String, VisualValue::Str("")),
              Data("Value", PinDirection::Output, ValueType::Float, VisualValue::Float(0.0f)),
              Data("Found", PinDirection::Output, ValueType::Bool, VisualValue::Bool(false)) },
            [](INodeContext& c) {
                const VisualValue m = c.ReadInput("Map"); const VisualValue k = c.ReadInput("Key");
                for (std::size_t i = 0; i < m.MapKeysConst().size(); ++i)
                    if (m.MapKeysConst()[i].Equals(k)) { c.WriteOutput("Value", m.MapValuesConst()[i]); c.WriteOutput("Found", VisualValue::Bool(true)); return; }
                c.WriteOutput("Found", VisualValue::Bool(false));
            }});
        reg.Register({"Map.Contains", "Map Contains", "Map", true, 1,
            { mapIn("Map"), Data("Key", PinDirection::Input, ValueType::String, VisualValue::Str("")),
              Data("Found", PinDirection::Output, ValueType::Bool, VisualValue::Bool(false)) },
            [](INodeContext& c) {
                const VisualValue m = c.ReadInput("Map"); const VisualValue k = c.ReadInput("Key");
                bool found = false; for (const VisualValue& mk : m.MapKeysConst()) if (mk.Equals(k)) { found = true; break; }
                c.WriteOutput("Found", VisualValue::Bool(found));
            }});
        reg.Register({"Map.Remove", "Map Remove", "Map", false, 1,
            { Exec("In", PinDirection::Input), mapIn("Map"),
              Data("Key", PinDirection::Input, ValueType::String, VisualValue::Str("")),
              Exec("Then", PinDirection::Output), mapOut("Map") },
            [](INodeContext& c) {
                VisualValue m = c.ReadInput("Map").CloneDeep(); const VisualValue k = c.ReadInput("Key");
                for (std::size_t i = 0; i < m.MapKeys().size(); ++i) if (m.MapKeys()[i].Equals(k)) {
                    m.MapKeys().erase(m.MapKeys().begin() + i); m.MapValues().erase(m.MapValues().begin() + i); break;
                }
                c.WriteOutput("Map", m); c.Continue("Then");
            }});
        reg.Register({"Map.Keys", "Map Keys", "Map", true, 1,
            { mapIn("Map"), Data("Keys", PinDirection::Output, ValueType::Array, VisualValue::MakeArray(ValueType::String)) },
            [](INodeContext& c) {
                const VisualValue m = c.ReadInput("Map");
                VisualValue arr = VisualValue::MakeArray(m.keyType);
                for (const VisualValue& k : m.MapKeysConst()) arr.Elements().push_back(k);
                c.WriteOutput("Keys", arr);
            }});
        reg.Register({"Map.Values", "Map Values", "Map", true, 1,
            { mapIn("Map"), Data("Values", PinDirection::Output, ValueType::Array, VisualValue::MakeArray(ValueType::Float)) },
            [](INodeContext& c) {
                const VisualValue m = c.ReadInput("Map");
                VisualValue arr = VisualValue::MakeArray(m.elementType);
                for (const VisualValue& v : m.MapValuesConst()) arr.Elements().push_back(v);
                c.WriteOutput("Values", arr);
            }});
    }

    // ---- Collections: Structs (Milestone 4) --------------------------------
    // Make/Break carry dynamic data pins the editor builds from the struct definition; the executors
    // walk the AUTHORED pins so they work for any struct signature. "struct" property = struct type id.
    reg.Register({"Struct.Make", "Make Struct", "Struct", true, 1,
        { Data("Value", PinDirection::Output, ValueType::Struct, VisualValue::MakeStruct(0)) },
        [](INodeContext& c) {
            VisualValue s = VisualValue::MakeStruct(static_cast<std::uint32_t>(c.Property("struct", VisualValue::Int(0)).AsInt()));
            for (const VisualPin& p : c.Node().inputs) if (p.kind == PinKind::Data) s.Elements().push_back(c.ReadInput(p.name));
            c.WriteOutput("Value", s);
        }});
    reg.Register({"Struct.Break", "Break Struct", "Struct", true, 1,
        { Data("Value", PinDirection::Input, ValueType::Struct, VisualValue::MakeStruct(0)) },
        [](INodeContext& c) {
            const VisualValue s = c.ReadInput("Value");
            std::size_t i = 0;
            for (const VisualPin& p : c.Node().outputs)
                if (p.kind == PinKind::Data) { if (i < s.Elements().size()) c.WriteOutput(p.name, s.Elements()[i]); ++i; }
        }});

    // ---- Advanced flow control (Milestone 5) -------------------------------
    // Nodes with several exec INPUTS use EnteredVia() to tell which one fired; stateful nodes keep a
    // tiny per-node state (survives across callbacks) via Get/SetNodeState.
    reg.Register({"Flow.DoOnce", "Do Once", "Flow", false, 1,
        { Exec("In", PinDirection::Input), Exec("Reset", PinDirection::Input), Exec("Then", PinDirection::Output) },
        [](INodeContext& c) {
            if (c.EnteredVia("Reset")) { c.SetNodeState("fired", VisualValue::Bool(false)); return; }
            if (!c.GetNodeState("fired", VisualValue::Bool(false)).AsBool()) {
                c.SetNodeState("fired", VisualValue::Bool(true)); c.Continue("Then");
            }
        }});
    reg.Register({"Flow.DoN", "Do N", "Flow", false, 1,
        { Exec("In", PinDirection::Input), Exec("Reset", PinDirection::Input),
          Data("N", PinDirection::Input, ValueType::Int, VisualValue::Int(1)),
          Exec("Then", PinDirection::Output),
          Data("Counter", PinDirection::Output, ValueType::Int, VisualValue::Int(0)) },
        [](INodeContext& c) {
            if (c.EnteredVia("Reset")) { c.SetNodeState("count", VisualValue::Int(0)); return; }
            int count = c.GetNodeState("count", VisualValue::Int(0)).AsInt();
            if (count < c.ReadInput("N").AsInt()) {
                ++count; c.SetNodeState("count", VisualValue::Int(count));
                c.WriteOutput("Counter", VisualValue::Int(count)); c.Continue("Then");
            }
        }});
    reg.Register({"Flow.Gate", "Gate", "Flow", false, 1,
        { Exec("Enter", PinDirection::Input), Exec("Open", PinDirection::Input),
          Exec("Close", PinDirection::Input), Exec("Toggle", PinDirection::Input),
          Data("Start Open", PinDirection::Input, ValueType::Bool, VisualValue::Bool(false)),
          Exec("Exit", PinDirection::Output) },
        [](INodeContext& c) {
            bool open = c.GetNodeState("open", c.ReadInput("Start Open")).AsBool();
            if (c.EnteredVia("Open")) open = true;
            else if (c.EnteredVia("Close")) open = false;
            else if (c.EnteredVia("Toggle")) open = !open;
            else if (c.EnteredVia("Enter") && open) c.Continue("Exit");
            c.SetNodeState("open", VisualValue::Bool(open));
        }});
    reg.Register({"Flow.FlipFlop", "Flip Flop", "Flow", false, 1,
        { Exec("In", PinDirection::Input), Exec("A", PinDirection::Output), Exec("B", PinDirection::Output),
          Data("Is A", PinDirection::Output, ValueType::Bool, VisualValue::Bool(true)) },
        [](INodeContext& c) {
            const bool isA = c.GetNodeState("isA", VisualValue::Bool(true)).AsBool();
            c.WriteOutput("Is A", VisualValue::Bool(isA));
            c.Continue(isA ? "A" : "B");
            c.SetNodeState("isA", VisualValue::Bool(!isA));
        }});
    reg.Register({"Flow.MultiGate", "Multi Gate", "Flow", false, 1,
        { Exec("In", PinDirection::Input), Exec("Reset", PinDirection::Input),
          Data("Loop", PinDirection::Input, ValueType::Bool, VisualValue::Bool(true)),
          Exec("Out 0", PinDirection::Output), Exec("Out 1", PinDirection::Output),
          Exec("Out 2", PinDirection::Output), Exec("Out 3", PinDirection::Output) },
        [](INodeContext& c) {
            if (c.EnteredVia("Reset")) { c.SetNodeState("idx", VisualValue::Int(0)); return; }
            int idx = c.GetNodeState("idx", VisualValue::Int(0)).AsInt();
            const int count = 4;
            if (idx >= count) { if (c.ReadInput("Loop").AsBool()) idx = 0; else return; }
            const char* pins[count] = {"Out 0", "Out 1", "Out 2", "Out 3"};
            c.Continue(pins[idx]);
            c.SetNodeState("idx", VisualValue::Int(idx + 1));
        }});
    reg.Register({"Flow.Select", "Select", "Flow", true, 1,
        { Data("Pick", PinDirection::Input, ValueType::Bool, VisualValue::Bool(false)),
          Data("True", PinDirection::Input, ValueType::Float, VisualValue::Float(0.0f)),
          Data("False", PinDirection::Input, ValueType::Float, VisualValue::Float(0.0f)),
          Data("Result", PinDirection::Output, ValueType::Float, VisualValue::Float(0.0f)) },
        [](INodeContext& c) { c.WriteOutput("Result", c.ReadInput(c.ReadInput("Pick").AsBool() ? "True" : "False")); }});
    reg.Register({"Flow.SelectInt", "Select By Index", "Flow", true, 1,
        { Data("Index", PinDirection::Input, ValueType::Int, VisualValue::Int(0)),
          Data("Option 0", PinDirection::Input, ValueType::Float, VisualValue::Float(0.0f)),
          Data("Option 1", PinDirection::Input, ValueType::Float, VisualValue::Float(0.0f)),
          Data("Option 2", PinDirection::Input, ValueType::Float, VisualValue::Float(0.0f)),
          Data("Option 3", PinDirection::Input, ValueType::Float, VisualValue::Float(0.0f)),
          Data("Result", PinDirection::Output, ValueType::Float, VisualValue::Float(0.0f)) },
        [](INodeContext& c) {
            int i = c.ReadInput("Index").AsInt(); i = i < 0 ? 0 : (i > 3 ? 3 : i);
            c.WriteOutput("Result", c.ReadInput("Option " + std::to_string(i)));
        }});
    reg.Register({"Core.ForLoop", "For Loop", "Flow", false, 1,
        { Exec("In", PinDirection::Input),
          Data("First", PinDirection::Input, ValueType::Int, VisualValue::Int(0)),
          Data("Last", PinDirection::Input, ValueType::Int, VisualValue::Int(0)),
          Exec("Loop Body", PinDirection::Output),
          Data("Index", PinDirection::Output, ValueType::Int, VisualValue::Int(0)),
          Exec("Completed", PinDirection::Output) },
        [](INodeContext& c) {
            const int first = c.ReadInput("First").AsInt();
            const int last = c.ReadInput("Last").AsInt();
            for (int i = first; i <= last; ++i) {
                c.WriteOutput("Index", VisualValue::Int(i));
                if (!c.RunLoopBody("Loop Body")) return;
            }
            c.Continue("Completed");
        }});
    reg.Register({"Core.While", "While Loop", "Flow", false, 1,
        { Exec("In", PinDirection::Input),
          Data("Condition", PinDirection::Input, ValueType::Bool, VisualValue::Bool(false)),
          Exec("Loop Body", PinDirection::Output), Exec("Completed", PinDirection::Output) },
        [](INodeContext& c) {
            int guard = 0;
            while (c.ReadInputFresh("Condition").AsBool()) {
                if (++guard > 100000) { c.Fail("While Loop: exceeded max iterations"); return; }
                if (!c.RunLoopBody("Loop Body")) return;
            }
            c.Continue("Completed");
        }});

    // ---- Time / timers / timeline (Milestone 5) ----------------------------
    reg.Register({"Flow.RetriggerableDelay", "Retriggerable Delay", "Time", false, 1,
        { Exec("In", PinDirection::Input), Exec("Reset", PinDirection::Input),
          Data("Seconds", PinDirection::Input, ValueType::Float, VisualValue::Float(1.0f)),
          Exec("Then", PinDirection::Output) },
        [](INodeContext& c) {
            c.CancelNodeLatent();   // restart / cancel any pending delay for this node
            if (c.EnteredVia("Reset")) return;
            c.SuspendLatent(LatentKind::Delay, c.ReadInput("Seconds").AsFloat(), 0, "", "Then");
        }});
    reg.Register({"Time.SetTimer", "Set Timer", "Time", false, 1,
        { Exec("In", PinDirection::Input), Exec("Stop", PinDirection::Input),
          Data("Time", PinDirection::Input, ValueType::Float, VisualValue::Float(1.0f)),
          Data("Looping", PinDirection::Input, ValueType::Bool, VisualValue::Bool(true)),
          Exec("On Timer", PinDirection::Output) },
        [](INodeContext& c) {
            if (c.EnteredVia("Stop")) { c.CancelNodeLatent(); return; }
            c.CancelNodeLatent();   // restart cleanly if re-entered
            c.SuspendTimer(c.ReadInput("Time").AsFloat(), c.ReadInput("Looping").AsBool(), "On Timer");
        }});
    reg.Register({"Time.Cooldown", "Cooldown Gate", "Time", false, 1,
        { Exec("In", PinDirection::Input),
          Data("Cooldown", PinDirection::Input, ValueType::Float, VisualValue::Float(1.0f)),
          Exec("Ready", PinDirection::Output), Exec("On Cooldown", PinDirection::Output),
          Data("Remaining", PinDirection::Output, ValueType::Float, VisualValue::Float(0.0f)) },
        [](INodeContext& c) {
            const double now = c.Now();
            const double readyAt = static_cast<double>(c.GetNodeState("readyAt", VisualValue::Float(0.0f)).AsFloat());
            if (now >= readyAt) {
                c.SetNodeState("readyAt", VisualValue::Float(static_cast<float>(now + c.ReadInput("Cooldown").AsFloat())));
                c.WriteOutput("Remaining", VisualValue::Float(0.0f)); c.Continue("Ready");
            } else {
                c.WriteOutput("Remaining", VisualValue::Float(static_cast<float>(readyAt - now))); c.Continue("On Cooldown");
            }
        }});
    reg.Register({"Time.Throttle", "Throttle", "Time", false, 1,
        { Exec("In", PinDirection::Input),
          Data("Interval", PinDirection::Input, ValueType::Float, VisualValue::Float(0.25f)),
          Exec("Then", PinDirection::Output) },
        [](INodeContext& c) {
            const double now = c.Now();
            const double last = static_cast<double>(c.GetNodeState("last", VisualValue::Float(-1000000.0f)).AsFloat());
            if (now - last >= c.ReadInput("Interval").AsFloat()) {
                c.SetNodeState("last", VisualValue::Float(static_cast<float>(now))); c.Continue("Then");
            }
        }});
    reg.Register({"Time.Timeline", "Timeline", "Time", false, 1,
        { Exec("Play", PinDirection::Input), Exec("Stop", PinDirection::Input),
          Data("Duration", PinDirection::Input, ValueType::Float, VisualValue::Float(1.0f)),
          Data("Loop", PinDirection::Input, ValueType::Bool, VisualValue::Bool(false)),
          Exec("Update", PinDirection::Output),
          Data("Alpha", PinDirection::Output, ValueType::Float, VisualValue::Float(0.0f)),
          Exec("Finished", PinDirection::Output) },
        [](INodeContext& c) {
            if (c.EnteredVia("Stop")) { c.CancelNodeLatent(); return; }
            c.StartTimeline(c.ReadInput("Duration").AsFloat(), c.ReadInput("Loop").AsBool(),
                            "Alpha", "Update", "Finished");
        }});

    // ---- Events / interfaces (Milestone 6) ---------------------------------
    // On Custom Event: an event-handler entry. Payload data outputs are added by the editor from the
    // event signature and seeded by the runtime; the "eventName" property is the bus match key.
    reg.Register({"Event.Custom", "On Custom Event", "Event", false, 1,
        { Exec("Then", PinDirection::Output) },
        [](INodeContext& c) { c.Continue("Then"); }});

    // Shared send executor for Broadcast + Interface Call: builds the event from the node's data
    // inputs (skipping Target/Broadcast control pins), then routes it.
    auto sendExecutor = [](INodeContext& c) {
        c.BeginEvent(c.Property("eventName", VisualValue::Str("")).AsString());
        ecs::Entity target = ecs::kNull;
        bool forceBroadcast = false;
        for (const VisualPin& p : c.Node().inputs) {
            if (p.kind != PinKind::Data) continue;
            if (p.name == "Target")    { target = c.ReadInput("Target").AsEntity(); continue; }
            if (p.name == "Broadcast") { forceBroadcast = c.ReadInput("Broadcast").AsBool(); continue; }
            c.EventArg(p.name, c.ReadInput(p.name));
        }
        c.SendEvent(target, forceBroadcast || target == ecs::kNull);
        c.Continue("Then");
    };
    reg.Register({"Event.Broadcast", "Broadcast Event", "Events", false, 1,
        { Exec("In", PinDirection::Input),
          Data("Broadcast", PinDirection::Input, ValueType::Bool, VisualValue::Bool(true)),
          Data("Target", PinDirection::Input, ValueType::Entity, VisualValue::Ent(ecs::kNull)),
          Exec("Then", PinDirection::Output) },
        sendExecutor});
    reg.Register({"Interface.Call", "Call Interface Message", "Interface", false, 1,
        { Exec("In", PinDirection::Input),
          Data("Target", PinDirection::Input, ValueType::Entity, VisualValue::Ent(ecs::kNull)),
          Exec("Then", PinDirection::Output) },
        sendExecutor});

    reg.Register({"Event.Bind", "Bind Event", "Events", false, 1,
        { Exec("In", PinDirection::Input),
          Data("Event Name", PinDirection::Input, ValueType::String, VisualValue::Str("")),
          Exec("Then", PinDirection::Output) },
        [](INodeContext& c) { c.BindEvent(c.ReadInput("Event Name").AsString()); c.Continue("Then"); }});
    reg.Register({"Event.Unbind", "Unbind Event", "Events", false, 1,
        { Exec("In", PinDirection::Input),
          Data("Event Name", PinDirection::Input, ValueType::String, VisualValue::Str("")),
          Exec("Then", PinDirection::Output) },
        [](INodeContext& c) { c.UnbindEvent(c.ReadInput("Event Name").AsString()); c.Continue("Then"); }});

    // ---- State machine script API (Milestone 7) ----------------------------
    // The state machine id is authored into the node's "sm" property by the editor.
    reg.Register({"StateMachine.GetState", "Get Current State", "State Machine", true, 1,
        { Data("State Id", PinDirection::Output, ValueType::Int, VisualValue::Int(0)) },
        [](INodeContext& c) {
            const std::uint32_t sm = static_cast<std::uint32_t>(c.Property("sm", VisualValue::Int(0)).AsInt());
            c.WriteOutput("State Id", VisualValue::Int(static_cast<int>(c.GetStateMachineState(sm))));
        }});
    reg.Register({"StateMachine.RequestState", "Request State", "State Machine", false, 1,
        { Exec("In", PinDirection::Input),
          Data("State Id", PinDirection::Input, ValueType::Int, VisualValue::Int(0)),
          Exec("Then", PinDirection::Output) },
        [](INodeContext& c) {
            const std::uint32_t sm = static_cast<std::uint32_t>(c.Property("sm", VisualValue::Int(0)).AsInt());
            c.RequestStateChange(sm, static_cast<std::uint32_t>(c.ReadInput("State Id").AsInt()));
            c.Continue("Then");
        }});

    // ---- Async tasks (Milestone 8) -----------------------------------------
    // A task suspends the exec thread and resumes on exactly one outcome pin. Ownership is the graph
    // instance/entity: destroying it drops the task safely (no leak, no dead-instance resume).
    reg.Register({"Async.StartTask", "Start Task", "Async", false, 1,
        { Exec("In", PinDirection::Input),
          Data("Work Seconds", PinDirection::Input, ValueType::Float, VisualValue::Float(1.0f)),
          Data("Timeout", PinDirection::Input, ValueType::Float, VisualValue::Float(0.0f)),
          Exec("Started", PinDirection::Output),
          Exec("Completed", PinDirection::Output), Exec("Failed", PinDirection::Output),
          Exec("Cancelled", PinDirection::Output), Exec("Timed Out", PinDirection::Output),
          Data("Task", PinDirection::Output, ValueType::Int, VisualValue::Int(0)) },
        [](INodeContext& c) {
            const int h = c.StartAsyncTask("Task", c.ReadInput("Work Seconds").AsFloat(),
                                           c.ReadInput("Timeout").AsFloat(),
                                           "Completed", "Failed", "Cancelled", "Timed Out");
            c.WriteOutput("Task", VisualValue::Int(h));
            c.Continue("Started");   // fire immediately so the handle can be captured; outcomes resume later
        }});
    reg.Register({"Async.CancelTask", "Cancel Task", "Async", false, 1,
        { Exec("In", PinDirection::Input),
          Data("Task", PinDirection::Input, ValueType::Int, VisualValue::Int(0)),
          Exec("Then", PinDirection::Output) },
        [](INodeContext& c) { c.CancelAsyncTask(c.ReadInput("Task").AsInt()); c.Continue("Then"); }});
    reg.Register({"Async.CompleteTask", "Complete Task", "Async", false, 1,
        { Exec("In", PinDirection::Input),
          Data("Task", PinDirection::Input, ValueType::Int, VisualValue::Int(0)),
          Data("Success", PinDirection::Input, ValueType::Bool, VisualValue::Bool(true)),
          Exec("Then", PinDirection::Output) },
        [](INodeContext& c) { c.CompleteAsyncTask(c.ReadInput("Task").AsInt(), c.ReadInput("Success").AsBool()); c.Continue("Then"); }});
    reg.Register({"Async.IsTaskActive", "Is Task Active", "Async", true, 1,
        { Data("Task", PinDirection::Input, ValueType::Int, VisualValue::Int(0)),
          Data("Active", PinDirection::Output, ValueType::Bool, VisualValue::Bool(false)) },
        [](INodeContext& c) { c.WriteOutput("Active", VisualValue::Bool(c.IsAsyncTaskActive(c.ReadInput("Task").AsInt()))); }});

    // (Async service wrappers that call the Script host live in VisualScriptGameplayNodes.h, where the
    //  full VisualScriptHost type is available.)

    // ---- Debug -------------------------------------------------------------
    reg.Register({"Debug.PrintString", "Print String", "Debug", false, 1,
        { Exec("In", PinDirection::Input),
          Data("Text", PinDirection::Input, ValueType::String, VisualValue::Str("Hello")),
          Exec("Then", PinDirection::Output) },
        [](INodeContext& c) { c.Log(c.ReadInput("Text").AsString()); c.Continue("Then"); }});
}

} // namespace engine::vs
