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
enum class LatentKind { Delay, WaitUntil, WaitEvent, WaitFixedSteps, WaitAnimation };

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
};

using NodeExecutor = std::function<void(INodeContext&)>;

// ---- Descriptors (Phase 10) ------------------------------------------------
struct NodePinDesc {
    std::string  name;
    PinDirection direction = PinDirection::Input;
    PinKind      kind = PinKind::Data;
    ValueType    type = ValueType::Float;
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

    // ---- Debug -------------------------------------------------------------
    reg.Register({"Debug.PrintString", "Print String", "Debug", false, 1,
        { Exec("In", PinDirection::Input),
          Data("Text", PinDirection::Input, ValueType::String, VisualValue::Str("Hello")),
          Exec("Then", PinDirection::Output) },
        [](INodeContext& c) { c.Log(c.ReadInput("Text").AsString()); c.Continue("Then"); }});
}

} // namespace engine::vs
