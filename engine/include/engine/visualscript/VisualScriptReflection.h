#pragma once

// =============================================================================
// Visual Scripting — Pass 2 : reflection bridge + engine-API node library.
//
// Visual scripting consumes the SAME metadata native C++ / Lua / the docs use:
//   * engine::script::ScriptApiRegistry — functions + typed events (stable FNV ids)
//   * engine::reflect::TypeRegistry     — component properties (stable Component/PropertyId)
// It does NOT hand-write a second copy of the API. Node SHAPE (pins, category,
// safety flags, ids) is generated from those registries; the thin runtime CALL
// for reflected functions is supplied by a host-owned invoker binding keyed by
// the same stable FunctionId (the metadata layer intentionally stores no
// std::function). Property get/set and structural/physics/input/event nodes run
// directly on the existing public engine APIs.
//
// Header-only, editor-free. Call RegisterVisualScriptPass2() once at startup
// (after the host has populated the two reflection registries).
// =============================================================================

#include "VisualNodeRegistry.h"

#include <engine/gameplay/Script.h>            // ScriptEvent, ScriptInputState
#include <engine/gameplay/ScriptReflection.h>  // script::ScriptApiRegistry, descriptors, flags
#include <engine/reflect/Reflection.h>         // reflect::TypeRegistry, Get/SetProperty (tracked)
#include <engine/physics/PhysicsWorld.h>       // Raycast / SphereCast / OverlapSphere
#include <engine/ecs/Components.h>             // ecs::Transform

#include <cmath>
#include <functional>
#include <string>
#include <unordered_map>

namespace engine::vs {

// ---- type mapping (Phase 2/4/13/14) ----------------------------------------
inline ValueType FromScriptFieldType(script::ScriptFieldType t) {
    using S = script::ScriptFieldType;
    switch (t) {
        case S::Bool:      return ValueType::Bool;
        case S::Int:       return ValueType::Int;
        case S::Float:     return ValueType::Float;
        case S::String:    return ValueType::String;
        case S::Vec2:      return ValueType::Vector2;
        case S::Vec3:      return ValueType::Vector3;
        case S::Quat:      return ValueType::Quaternion;
        case S::Color:     return ValueType::Color;
        case S::Entity:    return ValueType::Entity;
        case S::Asset:     return ValueType::Asset;
        case S::ScriptRef: return ValueType::ScriptHandle;
        case S::Tag:       return ValueType::String;
    }
    return ValueType::Float;
}

inline ValueType FromPropertyType(reflect::PropertyType t) {
    using P = reflect::PropertyType;
    switch (t) {
        case P::Bool:      return ValueType::Bool;
        case P::Int:       return ValueType::Int;
        case P::Float:     return ValueType::Float;
        case P::Vec2:      return ValueType::Vector2;
        case P::Vec3:      return ValueType::Vector3;
        case P::Vec4:      return ValueType::Color;
        case P::Quat:      return ValueType::Quaternion;
        case P::Color:     return ValueType::Color;   // reflect stores the member as vec3
        case P::String:    return ValueType::String;
        case P::EntityRef: return ValueType::Entity;
        case P::Enum:      return ValueType::Int;      // Phase 14: stable numeric enum value
    }
    return ValueType::Float;
}

// reflect::PropertyValue (variant) -> VisualValue, coerced to the declared VS type.
inline VisualValue FromReflectValue(const reflect::PropertyValue& pv, ValueType t) {
    switch (t) {
        case ValueType::Bool:    if (auto* p = std::get_if<bool>(&pv))        return VisualValue::Bool(*p); break;
        case ValueType::Int:     if (auto* p = std::get_if<int>(&pv))         return VisualValue::Int(*p); break;
        case ValueType::Float:   if (auto* p = std::get_if<float>(&pv))       return VisualValue::Float(*p); break;
        case ValueType::String:  if (auto* p = std::get_if<std::string>(&pv)) return VisualValue::Str(*p); break;
        case ValueType::Vector2: if (auto* p = std::get_if<glm::vec2>(&pv))   return VisualValue::Vec2(*p); break;
        case ValueType::Vector3: if (auto* p = std::get_if<glm::vec3>(&pv))   return VisualValue::Vec3(*p); break;
        case ValueType::Quaternion: if (auto* p = std::get_if<glm::quat>(&pv)) return VisualValue::Quat(*p); break;
        case ValueType::Color:
            if (auto* p = std::get_if<glm::vec4>(&pv)) return VisualValue::Col(*p);
            if (auto* p = std::get_if<glm::vec3>(&pv)) return VisualValue::Col(glm::vec4(*p, 1.0f));
            break;
        case ValueType::Entity:  if (auto* p = std::get_if<ecs::Entity>(&pv)) return VisualValue::Ent(*p); break;
        default: break;
    }
    return VisualValue::MakeDefault(t);
}

// VisualValue -> reflect::PropertyValue, shaped to the property's declared type.
inline reflect::PropertyValue ToReflectValue(const VisualValue& v, reflect::PropertyType pt) {
    using P = reflect::PropertyType;
    switch (pt) {
        case P::Bool:      return v.AsBool();
        case P::Int:
        case P::Enum:      return v.AsInt();
        case P::Float:     return v.AsFloat();
        case P::Vec2:      return v.AsVec2();
        case P::Vec3:      return v.AsVec3();
        case P::Vec4:      return v.AsColor();
        case P::Quat:      return v.AsQuat();
        case P::Color:     return glm::vec3(v.AsColor());   // reflect Color member is vec3
        case P::String:    return v.AsString();
        case P::EntityRef: return v.AsEntity();
    }
    return reflect::PropertyValue{};
}

// ---- host-supplied invokers for reflected functions (Phase 2/3) ------------
class VisualScriptFunctionBindings {
public:
    static VisualScriptFunctionBindings& Get() { static VisualScriptFunctionBindings b; return b; }
    void Bind(script::ScriptFunctionId id, NodeExecutor invoker) { m_invokers[id] = std::move(invoker); }
    const NodeExecutor* Find(script::ScriptFunctionId id) const {
        auto it = m_invokers.find(id);
        return it == m_invokers.end() ? nullptr : &it->second;
    }
private:
    std::unordered_map<script::ScriptFunctionId, NodeExecutor> m_invokers;
};

// ---- generators -------------------------------------------------------------
namespace detail {

inline NodePinDesc ExecPin(const char* name, PinDirection dir) {
    NodePinDesc p; p.name = name; p.direction = dir; p.kind = PinKind::Exec;
    p.type = ValueType::Exec; p.defaultValue = VisualValue::Exec(); return p;
}
inline NodePinDesc DataPin(const std::string& name, PinDirection dir, ValueType type) {
    NodePinDesc p; p.name = name; p.direction = dir; p.kind = PinKind::Data;
    p.type = type; p.defaultValue = VisualValue::MakeDefault(type); return p;
}
inline NodePinDesc EntitySelfPin(const char* name) {
    // default kNull == "Self" (Phase 6); executors resolve+validate (Phase 7).
    NodePinDesc p = DataPin(name, PinDirection::Input, ValueType::Entity);
    p.defaultValue = VisualValue::Ent(ecs::kNull); return p;
}

} // namespace detail

// Phase 4/5/6/7: component property get/set nodes from ECS reflection.
inline void GenerateComponentPropertyNodes() {
    using namespace detail;
    VisualNodeRegistry& reg = VisualNodeRegistry::Instance();
    for (const reflect::ComponentDescriptor& cd : reflect::TypeRegistry::Get().All()) {
        for (const reflect::PropertyDescriptor& pd : cd.properties) {
            if (!pd.flags.visualScriptVisible) continue;   // opt-in only
            const ValueType vt = FromPropertyType(pd.type);
            const reflect::ComponentTypeId cid = cd.id;
            const reflect::PropertyId pid = pd.id;
            const reflect::PropertyType ptype = pd.type;
            const std::string base = std::string(cd.name) + "." + pd.name;

            if (pd.flags.readable) {
                NodeDescriptor get;
                get.typeId = "Prop.Get." + std::to_string(cid) + "." + std::to_string(pid);
                get.displayName = std::string("Get ") + base;
                get.category = std::string("Component/") + cd.name;
                get.pure = true;
                get.pins = { EntitySelfPin("Entity"), DataPin("Value", PinDirection::Output, vt) };
                get.execute = [cid, pid, vt](INodeContext& c) {
                    const ecs::Entity e = detail::ResolveEntityInput(c, "Entity");
                    reflect::PropertyValue pv = c.Registry()
                        ? reflect::GetProperty(*c.Registry(), e, cid, pid) : reflect::PropertyValue{};
                    c.WriteOutput("Value", FromReflectValue(pv, vt));
                };
                reg.Register(std::move(get));
            }
            if (pd.flags.writable) {
                NodeDescriptor set;
                set.typeId = "Prop.Set." + std::to_string(cid) + "." + std::to_string(pid);
                set.displayName = std::string("Set ") + base;
                set.category = std::string("Component/") + cd.name;
                set.pure = false;
                set.pins = { ExecPin("In", PinDirection::Input), EntitySelfPin("Entity"),
                             DataPin("Value", PinDirection::Input, vt), ExecPin("Then", PinDirection::Output) };
                set.execute = [cid, pid, ptype](INodeContext& c) {
                    const ecs::Entity e = detail::ResolveEntityInput(c, "Entity");
                    // Tracked write via reflect::SetProperty -> Registry::Patch<T> (Phase 5).
                    const bool ok = c.Registry() && reflect::SetProperty(
                        *c.Registry(), e, cid, pid, ToReflectValue(c.ReadInput("Value"), ptype));
                    if (!ok) c.Fail("Set " + std::to_string(cid) + "." + std::to_string(pid)
                                    + " failed (dead entity, missing component, or type mismatch)");
                    c.Continue("Then");
                };
                reg.Register(std::move(set));
            }
        }
    }
}

// Phase 2/3/20/21: reflected function-call nodes from the Script API registry.
inline void GenerateFunctionNodes() {
    using namespace detail;
    VisualNodeRegistry& reg = VisualNodeRegistry::Instance();
    for (const script::ScriptFunctionDescriptor& fd : script::ScriptApiRegistry::Get().Functions()) {
        if (!fd.Has(script::SFF_VisualScriptVisible)) continue;
        if (fd.Has(script::SFF_EditorOnly)) continue;   // never runs in the game/runtime graph

        NodeDescriptor node;
        node.typeId = "Fn." + std::to_string(fd.id);         // stable FunctionId (Phase 21)
        node.displayName = fd.name;
        node.category = fd.category.empty() ? std::string("Function") : fd.category;
        node.pure = false;                                    // functions may have side effects
        node.version = 1;
        node.pins.push_back(ExecPin("In", PinDirection::Input));
        node.pins.push_back(EntitySelfPin("Target"));         // Phase 6: optional target, default Self
        for (const script::ScriptParam& p : fd.params)
            node.pins.push_back(DataPin(p.name, PinDirection::Input, FromScriptFieldType(p.type)));
        node.pins.push_back(ExecPin("Then", PinDirection::Output));
        if (fd.hasReturn)
            node.pins.push_back(DataPin("Return", PinDirection::Output, FromScriptFieldType(fd.returnType)));

        const script::ScriptFunctionId id = fd.id;
        const bool deprecated = fd.Has(script::SFF_Deprecated);
        const std::string name = fd.name;
        const std::string depNote = fd.deprecationNote;
        node.execute = [id, deprecated, name, depNote](INodeContext& c) {
            if (deprecated) c.Log("[deprecated] " + name + (depNote.empty() ? "" : ": " + depNote));
            const NodeExecutor* invoker = VisualScriptFunctionBindings::Get().Find(id);
            if (!invoker) c.Fail("Function node '" + name + "' has no runtime binding");  // Phase 21 diagnostic
            else (*invoker)(c);   // binding reads inputs, performs the call, writes "Return"
            c.Continue("Then");
        };
        reg.Register(std::move(node));
    }
}

// Phase 10: event-entry nodes from the typed event schema.
inline void GenerateEventNodes() {
    using namespace detail;
    VisualNodeRegistry& reg = VisualNodeRegistry::Instance();
    for (const script::EventDescriptor& ed : script::ScriptApiRegistry::Get().Events()) {
        NodeDescriptor node;
        node.typeId = "Event." + std::to_string(ed.id);   // stable EventId
        node.displayName = ed.name;
        node.category = "Event";
        node.eventName = ed.name;                         // matched at DispatchEvent
        node.pins.push_back(ExecPin("Then", PinDirection::Output));
        for (const script::EventFieldSchema& f : ed.fields)
            node.pins.push_back(DataPin(f.name, PinDirection::Output, FromScriptFieldType(f.type)));
        node.execute = [](INodeContext& c) { c.Continue("Then"); };   // outputs seeded from payload
        reg.Register(std::move(node));
    }
}

// ---- concrete Pass 2 nodes (public engine APIs) -----------------------------
inline void RegisterConversionNodes() {   // Phase 13 — explicit, never hidden
    using namespace detail;
    VisualNodeRegistry& reg = VisualNodeRegistry::Instance();
    auto conv = [&](const char* id, const char* label, ValueType in, ValueType out, NodeExecutor fn) {
        NodeDescriptor d; d.typeId = id; d.displayName = label; d.category = "Convert"; d.pure = true;
        d.pins = { DataPin("In", PinDirection::Input, in), DataPin("Out", PinDirection::Output, out) };
        d.execute = std::move(fn); reg.Register(std::move(d));
    };
    conv("Convert.IntToFloat", "Int -> Float", ValueType::Int, ValueType::Float,
         [](INodeContext& c) { c.WriteOutput("Out", VisualValue::Float(static_cast<float>(c.ReadInput("In").AsInt()))); });
    conv("Convert.FloatToInt", "Float -> Int (truncate)", ValueType::Float, ValueType::Int,
         [](INodeContext& c) { c.WriteOutput("Out", VisualValue::Int(static_cast<int>(c.ReadInput("In").AsFloat()))); });
    conv("Convert.BoolToFloat", "Bool -> Float", ValueType::Bool, ValueType::Float,
         [](INodeContext& c) { c.WriteOutput("Out", VisualValue::Float(c.ReadInput("In").AsBool() ? 1.0f : 0.0f)); });
    conv("Convert.IntToString", "Int -> String", ValueType::Int, ValueType::String,
         [](INodeContext& c) { c.WriteOutput("Out", VisualValue::Str(std::to_string(c.ReadInput("In").AsInt()))); });
    conv("Convert.FloatToString", "Float -> String", ValueType::Float, ValueType::String,
         [](INodeContext& c) { c.WriteOutput("Out", VisualValue::Str(std::to_string(c.ReadInput("In").AsFloat()))); });
    conv("Convert.BoolToString", "Bool -> String", ValueType::Bool, ValueType::String,
         [](INodeContext& c) { c.WriteOutput("Out", VisualValue::Str(c.ReadInput("In").AsBool() ? "true" : "false")); });
    conv("Convert.Vector3ToString", "Vector3 -> String", ValueType::Vector3, ValueType::String,
         [](INodeContext& c) { const glm::vec3 v = c.ReadInput("In").AsVec3();
             c.WriteOutput("Out", VisualValue::Str("(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", " + std::to_string(v.z) + ")")); });
    conv("Convert.Vector2ToString", "Vector2 -> String", ValueType::Vector2, ValueType::String,
         [](INodeContext& c) { const glm::vec2 v = c.ReadInput("In").AsVec2();
             c.WriteOutput("Out", VisualValue::Str("(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ")")); });
}

inline void RegisterPhysicsNodes() {   // Phase 15 — reuse accelerated PhysicsWorld only
    using namespace detail;
    VisualNodeRegistry& reg = VisualNodeRegistry::Instance();

    NodeDescriptor ray;
    ray.typeId = "Physics.Raycast"; ray.displayName = "Raycast"; ray.category = "Physics";
    ray.pins = { ExecPin("In", PinDirection::Input),
                 DataPin("Start", PinDirection::Input, ValueType::Vector3),
                 DataPin("End", PinDirection::Input, ValueType::Vector3),
                 ExecPin("Then", PinDirection::Output),
                 DataPin("Hit", PinDirection::Output, ValueType::Bool),
                 DataPin("Point", PinDirection::Output, ValueType::Vector3),
                 DataPin("Normal", PinDirection::Output, ValueType::Vector3),
                 DataPin("HitEntity", PinDirection::Output, ValueType::Entity),
                 DataPin("Distance", PinDirection::Output, ValueType::Float) };
    ray.execute = [](INodeContext& c) {
        PhysicsWorld* pw = c.Physics();
        if (!pw || !c.Registry()) { c.Fail("Raycast: physics unavailable"); c.Continue("Then"); return; }
        const glm::vec3 s = c.ReadInput("Start").AsVec3();
        const glm::vec3 e = c.ReadInput("End").AsVec3();
        const glm::vec3 d = e - s;
        const float len = glm::length(d);
        Ray r; r.origin = s; r.direction = len > 1e-6f ? d / len : glm::vec3(0, 0, -1);
        const RaycastHit hit = pw->Raycast(*c.Registry(), r, len > 1e-6f ? len : 1.0e30f);
        c.WriteOutput("Hit", VisualValue::Bool(hit.hit));
        c.WriteOutput("Point", VisualValue::Vec3(hit.point));
        c.WriteOutput("Normal", VisualValue::Vec3(hit.normal));
        c.WriteOutput("HitEntity", VisualValue::Ent(hit.entity));
        c.WriteOutput("Distance", VisualValue::Float(hit.distance));
        c.Continue("Then");
    };
    reg.Register(std::move(ray));

    NodeDescriptor overlap;
    overlap.typeId = "Physics.OverlapSphere"; overlap.displayName = "Overlap Sphere"; overlap.category = "Physics";
    overlap.pins = { ExecPin("In", PinDirection::Input),
                     DataPin("Center", PinDirection::Input, ValueType::Vector3),
                     DataPin("Radius", PinDirection::Input, ValueType::Float),
                     ExecPin("Then", PinDirection::Output),
                     DataPin("Count", PinDirection::Output, ValueType::Int),
                     DataPin("First", PinDirection::Output, ValueType::Entity) };
    overlap.execute = [](INodeContext& c) {
        PhysicsWorld* pw = c.Physics();
        if (!pw || !c.Registry()) { c.Fail("OverlapSphere: physics unavailable"); c.Continue("Then"); return; }
        const std::vector<ecs::Entity> hits = pw->OverlapSphere(
            *c.Registry(), c.ReadInput("Center").AsVec3(), c.ReadInput("Radius").AsFloat());
        c.WriteOutput("Count", VisualValue::Int(static_cast<int>(hits.size())));
        c.WriteOutput("First", VisualValue::Ent(hits.empty() ? ecs::kNull : hits.front()));
        c.Continue("Then");
    };
    reg.Register(std::move(overlap));
}

inline void RegisterStructuralNodes() {   // Phase 16 — deferred / post-iteration only
    using namespace detail;
    VisualNodeRegistry& reg = VisualNodeRegistry::Instance();

    NodeDescriptor destroy;
    destroy.typeId = "Structural.DestroyEntity"; destroy.displayName = "Destroy Entity"; destroy.category = "Structural";
    destroy.pins = { ExecPin("In", PinDirection::Input), EntitySelfPin("Entity"), ExecPin("Then", PinDirection::Output) };
    destroy.execute = [](INodeContext& c) {
        const ecs::Entity e = detail::ResolveEntityInput(c, "Entity");
        c.EnqueueStructural([e](ecs::Registry& r) { if (r.Valid(e)) r.DestroyDeferred(e); });
        c.Continue("Then");
    };
    reg.Register(std::move(destroy));

    NodeDescriptor addc;
    addc.typeId = "Structural.AddComponent"; addc.displayName = "Add Component"; addc.category = "Structural";
    addc.pins = { ExecPin("In", PinDirection::Input), EntitySelfPin("Entity"),
                  DataPin("ComponentTypeId", PinDirection::Input, ValueType::Int), ExecPin("Then", PinDirection::Output) };
    addc.execute = [](INodeContext& c) {
        const ecs::Entity e = detail::ResolveEntityInput(c, "Entity");
        const int cid = c.ReadInput("ComponentTypeId").AsInt();
        c.EnqueueStructural([e, cid](ecs::Registry& r) {
            if (const auto* cd = reflect::TypeRegistry::Get().Find(static_cast<reflect::ComponentTypeId>(cid)))
                if (cd->construct) cd->construct(r, e);
        });
        c.Continue("Then");
    };
    reg.Register(std::move(addc));

    NodeDescriptor remc;
    remc.typeId = "Structural.RemoveComponent"; remc.displayName = "Remove Component"; remc.category = "Structural";
    remc.pins = { ExecPin("In", PinDirection::Input), EntitySelfPin("Entity"),
                  DataPin("ComponentTypeId", PinDirection::Input, ValueType::Int), ExecPin("Then", PinDirection::Output) };
    remc.execute = [](INodeContext& c) {
        const ecs::Entity e = detail::ResolveEntityInput(c, "Entity");
        const int cid = c.ReadInput("ComponentTypeId").AsInt();
        c.EnqueueStructural([e, cid](ecs::Registry& r) {
            if (const auto* cd = reflect::TypeRegistry::Get().Find(static_cast<reflect::ComponentTypeId>(cid)))
                if (cd->remove) cd->remove(r, e);
        });
        c.Continue("Then");
    };
    reg.Register(std::move(remc));
}

inline void RegisterInputNodes() {   // Phase 17 — reuse the existing ScriptInputState
    using namespace detail;
    VisualNodeRegistry& reg = VisualNodeRegistry::Instance();

    NodeDescriptor down;
    down.typeId = "Input.IsKeyDown"; down.displayName = "Is Key Down"; down.category = "Input"; down.pure = true;
    down.pins = { DataPin("Key", PinDirection::Input, ValueType::Int), DataPin("Down", PinDirection::Output, ValueType::Bool) };
    down.execute = [](INodeContext& c) {
        const ScriptInputState* in = c.Input();
        const int key = c.ReadInput("Key").AsInt();
        c.WriteOutput("Down", VisualValue::Bool(in && in->keysDown.count(key) != 0));
    };
    reg.Register(std::move(down));

    NodeDescriptor pressed;
    pressed.typeId = "Input.WasKeyPressed"; pressed.displayName = "Was Key Pressed"; pressed.category = "Input"; pressed.pure = true;
    pressed.pins = { DataPin("Key", PinDirection::Input, ValueType::Int), DataPin("Pressed", PinDirection::Output, ValueType::Bool) };
    pressed.execute = [](INodeContext& c) {
        const ScriptInputState* in = c.Input();
        const int key = c.ReadInput("Key").AsInt();
        c.WriteOutput("Pressed", VisualValue::Bool(in && in->keysPressed.count(key) != 0));
    };
    reg.Register(std::move(pressed));

    NodeDescriptor mouse;
    mouse.typeId = "Input.MouseDelta"; mouse.displayName = "Mouse Delta"; mouse.category = "Input"; mouse.pure = true;
    mouse.pins = { DataPin("Delta", PinDirection::Output, ValueType::Vector2) };
    mouse.execute = [](INodeContext& c) {
        const ScriptInputState* in = c.Input();
        c.WriteOutput("Delta", VisualValue::Vec2(in ? glm::vec2(in->mouseDeltaX, in->mouseDeltaY) : glm::vec2(0.0f)));
    };
    reg.Register(std::move(mouse));
}

inline void RegisterEventPublishNode() {   // Phase 12 — publish onto the shared ScriptEvent bus
    using namespace detail;
    VisualNodeRegistry& reg = VisualNodeRegistry::Instance();
    NodeDescriptor pub;
    pub.typeId = "Event.Publish"; pub.displayName = "Publish Event"; pub.category = "Event";
    pub.pins = { ExecPin("In", PinDirection::Input),
                 DataPin("Name", PinDirection::Input, ValueType::String),
                 EntitySelfPin("Target"),       // kNull broadcasts to every listener
                 DataPin("Value", PinDirection::Input, ValueType::Float),
                 DataPin("Vector", PinDirection::Input, ValueType::Vector3),
                 DataPin("Text", PinDirection::Input, ValueType::String),
                 DataPin("Flag", PinDirection::Input, ValueType::Bool),
                 ExecPin("Then", PinDirection::Output) };
    pub.execute = [](INodeContext& c) {
        ScriptEvent ev;
        ev.name = c.ReadInput("Name").AsString();
        ev.sender = c.Entity();
        ev.target = c.ReadInput("Target").AsEntity();   // kNull == broadcast
        ev.floats["value"] = c.ReadInput("Value").AsFloat();
        ev.vectors["vector"] = c.ReadInput("Vector").AsVec3();
        ev.strings["text"] = c.ReadInput("Text").AsString();
        ev.bools["flag"] = c.ReadInput("Flag").AsBool();
        c.PublishEvent(ev);   // queued; the host forwards to QueueScriptEvent at the safe phase
        c.Continue("Then");
    };
    reg.Register(std::move(pub));
}

// ---- master registration ----------------------------------------------------
// Call once at startup, AFTER the host has populated script::ScriptApiRegistry and
// reflect::TypeRegistry (RegisterCoreComponents / RegisterScriptApi seeds).
inline void RegisterVisualScriptPass2() {
    RegisterCoreNodes();               // Pass 1 core (idempotent)
    GenerateComponentPropertyNodes();  // Phase 4/5/6/7
    GenerateFunctionNodes();           // Phase 2/3/20/21
    GenerateEventNodes();              // Phase 10
    RegisterConversionNodes();         // Phase 13
    RegisterPhysicsNodes();            // Phase 15
    RegisterStructuralNodes();         // Phase 16
    RegisterInputNodes();              // Phase 17
    RegisterEventPublishNode();        // Phase 12
}

} // namespace engine::vs
