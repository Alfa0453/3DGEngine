#pragma once

// =============================================================================
// Visual Scripting — Pass 1 : authored graph data model + .3dgvs serialization.
//
//   * VisualScriptAsset { version; graphId; nodes; links; variables; }  — Phase 3
//   * versioned, migration-friendly text format "3DG_VISUAL_SCRIPT <v>" — Phase 8
//   * value (de)serialization for every ValueType                        — Phase 5
//
// Editor-free (Phase 23): lives under engine/, depends only on the value/type
// layer + std. Runtime execution state does NOT live here (see
// VisualScriptInstance) — this is the immutable authored document.
// =============================================================================

#include "VisualScriptTypes.h"

#include <cstdint>
#include <fstream>
#include <iomanip>
#include <istream>
#include <ostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::vs {

// v1 = Pass 1 (nodes/links/vars). v2 = Pass 3 adds a stable VariableId to each variable and an
// editor-only comment block. v3 = Milestone 2 adds per-comment color + collapsed flag (navigation
// & layout). v4 = Milestone 3 adds per-node functionId + a functions block (functions/subgraphs).
// v5 = Milestone 4 adds struct/enum type definitions, pin container metadata, and recursive
// array/map/struct/enum values. v6 = Milestone 6 adds user events, interfaces, and the implemented-
// interface list. v7 = Milestone 7 adds state machines. Old v1..v6 files load unchanged.
inline constexpr std::uint32_t kVisualScriptVersion = 7;   // Phase 8
inline constexpr const char* kVisualScriptMagic = "3DG_VISUAL_SCRIPT";
inline constexpr const char* kVisualScriptExtension = ".3dgvs";

// ---- Authored pin (Phase 6) ------------------------------------------------
struct VisualPin {
    PinId        id = kInvalidPinId;       // unique within its node
    PinDirection direction = PinDirection::Input;
    PinKind      kind = PinKind::Data;
    ValueType    type = ValueType::Float;  // Exec for control pins
    // Container/struct identity (Milestone 4): element type for Array/Map, key type for Map, and the
    // Struct/Enum definition id. Ignored for scalar types; drives connection type-checking.
    ValueType    elementType = ValueType::Float;
    ValueType    keyType = ValueType::String;
    std::uint32_t typeId = 0;
    std::string  name;
    VisualValue  defaultValue;             // used when a data input is unconnected
};

// ---- Authored node (Phase 3) -----------------------------------------------
struct VisualNode {
    NodeId      id = kInvalidNodeId;
    std::string typeId;                     // stable NodeTypeId, e.g. "Core.Branch"
    FunctionId  functionId = kInvalidFunctionId;  // 0 = event graph; else the owning function (M3)
    glm::vec2   editorPosition{0.0f};       // editor layout only (kept out of runtime)
    std::unordered_map<std::string, VisualValue> properties;  // authored constants
    std::vector<VisualPin> inputs;
    std::vector<VisualPin> outputs;

    const VisualPin* FindPin(PinId pin) const {
        for (const VisualPin& p : inputs)  if (p.id == pin) return &p;
        for (const VisualPin& p : outputs) if (p.id == pin) return &p;
        return nullptr;
    }
};

// ---- Authored link (Phase 3) -----------------------------------------------
// Stores BOTH endpoints as (node, pin) so it resolves unambiguously even though
// PinId is only unique within a node.
struct VisualLink {
    LinkId id = kInvalidLinkId;
    NodeId fromNode = kInvalidNodeId;
    PinId  fromPin = kInvalidPinId;   // an OUTPUT pin
    NodeId toNode = kInvalidNodeId;
    PinId  toPin = kInvalidPinId;     // an INPUT pin
};

// ---- Graph variable --------------------------------------------------------
struct VisualVariable {
    VariableId  id = kInvalidVariableId;   // stable identity; Get/Set Variable nodes resolve by this
    std::string name;                      // display/authoring identity (may be renamed; id survives)
    ValueType   type = ValueType::Float;
    VisualValue defaultValue;
    bool        exposed = false;           // editable/exposed on the component instance (Phase 11)
};

// ---- Function / reusable subgraph (Milestone 3) ----------------------------
// A function is a named subgraph inside the asset: its nodes carry functionId == this id, an
// Entry node produces the typed inputs, a Return node consumes the typed outputs. Functions are
// SYNCHRONOUS (no latent nodes) so a call runs to completion within the caller's frame.
struct VisualFunctionParam {
    std::string name;
    ValueType   type = ValueType::Float;
};

struct VisualFunction {
    FunctionId  id = kInvalidFunctionId;
    std::string name;
    std::vector<VisualFunctionParam> inputs;    // exposed as Entry node data outputs
    std::vector<VisualFunctionParam> outputs;   // consumed as Return node data inputs
    std::vector<VisualVariable>      locals;    // function-local variables (own scope)
    bool        pure = false;                    // reserved: pure functions (no exec pins)
};

// ---- User-authored struct + enum types (Milestone 4) -----------------------
// Struct/Enum values reference these by a stable typeId. Field/entry order is the on-wire order.
struct VisualStructField {
    std::string name;
    ValueType   type = ValueType::Float;
    ValueType   elementType = ValueType::Float;   // when the field is itself an Array/Map
    std::uint32_t typeId = 0;                      // when the field is itself a Struct/Enum
};
struct VisualStructType {
    std::uint32_t id = 0;
    std::string   name;
    std::vector<VisualStructField> fields;
};
struct VisualEnumType {
    std::uint32_t id = 0;
    std::string   name;
    std::vector<std::string> entries;   // index == integer value
};

// ---- User-defined events + interfaces (Milestone 6) ------------------------
// An event is a named, typed message on the shared script-event bus. An interface is a named set of
// message signatures a graph can declare it implements. Both reference stable ids in the type-id space.
struct VisualCustomEvent {
    std::uint32_t id = 0;
    std::string   name;                         // the wire name on the ScriptEvent bus
    std::vector<VisualFunctionParam> params;    // typed payload
};
struct VisualInterface {
    std::uint32_t id = 0;
    std::string   name;
    std::vector<VisualCustomEvent> messages;    // required messages (name + params)
};

// ---- Visual state machine (Milestone 7) ------------------------------------
// A high-level gameplay state model. Each state's Enter/Update/Exit logic and each transition's
// Condition are authored as regular functions (Milestone 3) referenced by id — so the synchronous
// function-call engine runs them and no second graph interpreter is needed.
struct VisualState {
    std::uint32_t id = 0;
    std::string   name;
    FunctionId    onEnter = kInvalidFunctionId;
    FunctionId    onUpdate = kInvalidFunctionId;
    FunctionId    onExit = kInvalidFunctionId;
    glm::vec2     editorPosition{0.0f};
};
struct VisualTransition {
    std::uint32_t id = 0;
    std::uint32_t from = 0;
    std::uint32_t to = 0;
    int           priority = 0;             // higher wins when several conditions are true
    FunctionId    condition = kInvalidFunctionId;   // Bool-returning function (empty = always true)
    float         cooldown = 0.0f;          // seconds this transition is disabled after firing
    bool          interruptible = true;     // may fire mid-state (controlled interruption)
};
struct VisualStateMachine {
    std::uint32_t id = 0;
    std::string   name;
    std::uint32_t entryState = 0;
    std::vector<VisualState> states;
    std::vector<VisualTransition> transitions;
};

// ---- Editor-only comment box (Phase 13; no runtime cost) -------------------
struct VisualComment {
    CommentId   id = kInvalidCommentId;
    glm::vec2   position{0.0f};
    glm::vec2   size{240.0f, 120.0f};
    std::string text;                       // used as the comment's title
    glm::vec4   color{0.24f, 0.35f, 0.47f, 1.0f};   // editor tint (Milestone 2)
    bool        collapsed = false;          // draws as a compact title bar (Milestone 2)
};

// ---- Value (de)serialization ----------------------------------------------
inline void WriteVisualValue(std::ostream& out, const VisualValue& value) {
    out << ValueTypeName(value.type);
    switch (value.type) {
        case ValueType::Exec:                                      break;
        case ValueType::Bool:    out << ' ' << (value.AsBool() ? 1 : 0); break;
        case ValueType::Int:     out << ' ' << value.AsInt();       break;
        case ValueType::Float:   out << ' ' << value.AsFloat();     break;
        case ValueType::String:  out << ' ' << std::quoted(value.AsString()); break;
        case ValueType::Vector2: { glm::vec2 v = value.AsVec2(); out << ' ' << v.x << ' ' << v.y; break; }
        case ValueType::Vector3: { glm::vec3 v = value.AsVec3(); out << ' ' << v.x << ' ' << v.y << ' ' << v.z; break; }
        case ValueType::Entity:  out << ' ' << static_cast<std::uint32_t>(value.AsEntity()); break;
        case ValueType::Asset:   out << ' ' << std::quoted(value.AsAsset().Valid() ? value.AsAsset().ToString() : std::string("-")); break;
        case ValueType::Quaternion: { glm::quat q = value.AsQuat(); out << ' ' << q.w << ' ' << q.x << ' ' << q.y << ' ' << q.z; break; }
        case ValueType::Color:   { glm::vec4 c = value.AsColor(); out << ' ' << c.r << ' ' << c.g << ' ' << c.b << ' ' << c.a; break; }
        case ValueType::ScriptHandle: out << ' ' << static_cast<std::uint32_t>(value.AsEntity()); break;
        // ---- containers (Milestone 4) — recursive, self-describing elements ----
        case ValueType::Array: {
            out << ' ' << ValueTypeName(value.elementType) << ' ' << value.Elements().size();
            for (const VisualValue& e : value.Elements()) { out << ' '; WriteVisualValue(out, e); }
            break;
        }
        case ValueType::Map: {
            out << ' ' << ValueTypeName(value.keyType) << ' ' << ValueTypeName(value.elementType)
                << ' ' << value.MapKeysConst().size();
            for (std::size_t i = 0; i < value.MapKeysConst().size(); ++i) {
                out << ' '; WriteVisualValue(out, value.MapKeysConst()[i]);
                out << ' '; WriteVisualValue(out, value.MapValuesConst()[i]);
            }
            break;
        }
        case ValueType::Struct: {
            out << ' ' << value.typeId << ' ' << value.Elements().size();
            for (const VisualValue& f : value.Elements()) { out << ' '; WriteVisualValue(out, f); }
            break;
        }
        case ValueType::Enum: out << ' ' << value.typeId << ' ' << value.AsInt(); break;
        default: break;
    }
}

inline bool ReadVisualValue(std::istream& in, VisualValue* out) {
    std::string typeName;
    if (!(in >> typeName)) return false;
    ValueType type = ValueType::Float;
    if (!ParseValueType(typeName, &type)) return false;
    switch (type) {
        case ValueType::Exec:    *out = VisualValue::Exec(); return true;
        case ValueType::Bool:    { int b = 0; in >> b; *out = VisualValue::Bool(b != 0); return true; }
        case ValueType::Int:     { int i = 0; in >> i; *out = VisualValue::Int(i); return true; }
        case ValueType::Float:   { float f = 0.0f; in >> f; *out = VisualValue::Float(f); return true; }
        case ValueType::String:  { std::string s; in >> std::quoted(s); *out = VisualValue::Str(s); return true; }
        case ValueType::Vector2: { glm::vec2 v; in >> v.x >> v.y; *out = VisualValue::Vec2(v); return true; }
        case ValueType::Vector3: { glm::vec3 v; in >> v.x >> v.y >> v.z; *out = VisualValue::Vec3(v); return true; }
        case ValueType::Entity:  { std::uint32_t e = 0; in >> e; *out = VisualValue::Ent(static_cast<ecs::Entity>(e)); return true; }
        case ValueType::Asset:   { std::string s; in >> std::quoted(s); AssetHandle h; if (s != "-") AssetHandle::Parse(s, &h); *out = VisualValue::Asset(h); return true; }
        case ValueType::Quaternion: { glm::quat q; in >> q.w >> q.x >> q.y >> q.z; *out = VisualValue::Quat(q); return true; }
        case ValueType::Color:   { glm::vec4 c; in >> c.r >> c.g >> c.b >> c.a; *out = VisualValue::Col(c); return true; }
        case ValueType::ScriptHandle: { std::uint32_t e = 0; in >> e; *out = VisualValue::Ent(static_cast<ecs::Entity>(e)); return true; }
        // ---- containers (Milestone 4) ----
        case ValueType::Array: {
            std::string elemType; std::size_t count = 0; in >> elemType >> count;
            if (count > 1000000u) return false;   // corrupt-file guard
            VisualValue arr = VisualValue::MakeArray(ValueType::Float);
            ParseValueType(elemType, &arr.elementType);
            arr.Elements().reserve(count);
            for (std::size_t i = 0; i < count; ++i) {
                VisualValue e; if (!ReadVisualValue(in, &e)) return false;
                arr.Elements().push_back(std::move(e));
            }
            *out = arr; return true;
        }
        case ValueType::Map: {
            std::string kt, vt; std::size_t count = 0; in >> kt >> vt >> count;
            if (count > 1000000u) return false;
            VisualValue m = VisualValue::MakeMap(ValueType::String, ValueType::Float);
            ParseValueType(kt, &m.keyType); ParseValueType(vt, &m.elementType);
            for (std::size_t i = 0; i < count; ++i) {
                VisualValue k, v;
                if (!ReadVisualValue(in, &k) || !ReadVisualValue(in, &v)) return false;
                m.MapKeys().push_back(std::move(k)); m.MapValues().push_back(std::move(v));
            }
            *out = m; return true;
        }
        case ValueType::Struct: {
            std::uint32_t tid = 0; std::size_t count = 0; in >> tid >> count;
            if (count > 100000u) return false;
            VisualValue s = VisualValue::MakeStruct(tid);
            for (std::size_t i = 0; i < count; ++i) {
                VisualValue f; if (!ReadVisualValue(in, &f)) return false;
                s.Elements().push_back(std::move(f));
            }
            *out = s; return true;
        }
        case ValueType::Enum: { std::uint32_t tid = 0; int val = 0; in >> tid >> val; *out = VisualValue::MakeEnum(tid, val); return true; }
        default: return false;
    }
}

// ---- The authored asset (Phase 3) ------------------------------------------
struct VisualScriptAsset {
    std::uint32_t version = kVisualScriptVersion;
    AssetHandle   id;
    GraphId       graphId = kInvalidGraphId;
    std::vector<VisualNode>     nodes;
    std::vector<VisualLink>     links;
    std::vector<VisualVariable> variables;
    std::vector<VisualComment>  comments;   // editor-only (Phase 13)
    std::vector<VisualFunction> functions;  // functions / subgraphs (Milestone 3)
    std::vector<VisualStructType> structs;  // user-authored struct types (Milestone 4)
    std::vector<VisualEnumType>   enums;    // user-authored enum types (Milestone 4)
    std::vector<VisualCustomEvent> events;  // user-defined events (Milestone 6)
    std::vector<VisualInterface>  interfaces;            // interface definitions (Milestone 6)
    std::vector<std::uint32_t>    implementedInterfaces; // interface ids this graph implements
    std::vector<VisualStateMachine> stateMachines;       // state machines (Milestone 7)

    const VisualNode* FindNode(NodeId node) const {
        for (const VisualNode& n : nodes) if (n.id == node) return &n;
        return nullptr;
    }
    const VisualVariable* FindVariable(VariableId variableId) const {
        for (const VisualVariable& v : variables) if (v.id == variableId) return &v;
        return nullptr;
    }
    const VisualFunction* FindFunction(FunctionId functionId) const {
        for (const VisualFunction& f : functions) if (f.id == functionId) return &f;
        return nullptr;
    }
    const VisualStructType* FindStruct(std::uint32_t typeId) const {
        for (const VisualStructType& s : structs) if (s.id == typeId) return &s;
        return nullptr;
    }
    const VisualEnumType* FindEnum(std::uint32_t typeId) const {
        for (const VisualEnumType& e : enums) if (e.id == typeId) return &e;
        return nullptr;
    }
    const VisualCustomEvent* FindEvent(std::uint32_t eventId) const {
        for (const VisualCustomEvent& e : events) if (e.id == eventId) return &e;
        return nullptr;
    }
    const VisualInterface* FindInterface(std::uint32_t interfaceId) const {
        for (const VisualInterface& i : interfaces) if (i.id == interfaceId) return &i;
        return nullptr;
    }
    const VisualStateMachine* FindStateMachine(std::uint32_t stateMachineId) const {
        for (const VisualStateMachine& s : stateMachines) if (s.id == stateMachineId) return &s;
        return nullptr;
    }

    // Highest ids currently in use — an editor uses these to mint fresh ids.
    NodeId MaxNodeId() const { NodeId m = 0; for (auto& n : nodes) m = n.id > m ? n.id : m; return m; }
    LinkId MaxLinkId() const { LinkId m = 0; for (auto& l : links) m = l.id > m ? l.id : m; return m; }
    VariableId MaxVariableId() const {
        VariableId m = 0; for (auto& v : variables) m = v.id > m ? v.id : m;
        for (auto& f : functions) for (auto& l : f.locals) m = l.id > m ? l.id : m;   // locals share the id space
        return m;
    }
    CommentId MaxCommentId() const { CommentId m = 0; for (auto& c : comments) m = c.id > m ? c.id : m; return m; }
    FunctionId MaxFunctionId() const { FunctionId m = 0; for (auto& f : functions) m = f.id > m ? f.id : m; return m; }
    std::uint32_t MaxTypeId() const {   // structs/enums/events/interfaces share one id space (M4/M6)
        std::uint32_t m = 0;
        for (auto& s : structs) m = s.id > m ? s.id : m;
        for (auto& e : enums)   m = e.id > m ? e.id : m;
        for (auto& e : events)  m = e.id > m ? e.id : m;
        for (auto& i : interfaces) {
            m = i.id > m ? i.id : m;
            for (auto& msg : i.messages) m = msg.id > m ? msg.id : m;
        }
        for (auto& sm : stateMachines) {
            m = sm.id > m ? sm.id : m;
            for (auto& st : sm.states) m = st.id > m ? st.id : m;
            for (auto& tr : sm.transitions) m = tr.id > m ? tr.id : m;
        }
        return m;
    }

    // ---- Serialization (Phase 8; text "MAGIC <version>" like the graph assets) --
    bool Save(std::ostream& out) const {
        if (!id.Valid()) return false;
        out << kVisualScriptMagic << ' ' << version << ' ' << id.ToString() << '\n';
        out << "graph " << graphId << '\n';
        out << "nodes " << nodes.size() << '\n';
        for (const VisualNode& node : nodes) {
            out << "node " << node.id << ' ' << std::quoted(node.typeId)
                << ' ' << node.editorPosition.x << ' ' << node.editorPosition.y
                << ' ' << node.functionId << '\n';   // functionId appended in v4
            out << "  props " << node.properties.size() << '\n';
            for (const auto& kv : node.properties) {
                out << "    " << std::quoted(kv.first) << ' ';
                WriteVisualValue(out, kv.second);
                out << '\n';
            }
            WritePins(out, "  in ", node.inputs);
            WritePins(out, "  out ", node.outputs);
        }
        out << "links " << links.size() << '\n';
        for (const VisualLink& link : links) {
            out << "link " << link.id << ' ' << link.fromNode << ' ' << link.fromPin
                << ' ' << link.toNode << ' ' << link.toPin << '\n';
        }
        out << "vars " << variables.size() << '\n';
        for (const VisualVariable& var : variables) {
            out << "var " << var.id << ' ' << std::quoted(var.name) << ' ' << ValueTypeName(var.type)
                << ' ' << (var.exposed ? 1 : 0) << ' ';
            WriteVisualValue(out, var.defaultValue);
            out << '\n';
        }
        out << "comments " << comments.size() << '\n';
        for (const VisualComment& comment : comments) {
            out << "comment " << comment.id << ' ' << comment.position.x << ' ' << comment.position.y
                << ' ' << comment.size.x << ' ' << comment.size.y << ' '
                << std::quoted(comment.text.empty() ? std::string("-") : comment.text)
                << ' ' << comment.color.r << ' ' << comment.color.g << ' ' << comment.color.b
                << ' ' << comment.color.a << ' ' << (comment.collapsed ? 1 : 0) << '\n';   // v3
        }
        // Functions block (v4). Params store name+type; locals are full variables.
        out << "functions " << functions.size() << '\n';
        for (const VisualFunction& fn : functions) {
            out << "func " << fn.id << ' ' << std::quoted(fn.name.empty() ? std::string("-") : fn.name)
                << ' ' << (fn.pure ? 1 : 0) << '\n';
            out << "  fin " << fn.inputs.size() << '\n';
            for (const VisualFunctionParam& p : fn.inputs)
                out << "    param " << std::quoted(p.name.empty() ? std::string("-") : p.name)
                    << ' ' << ValueTypeName(p.type) << '\n';
            out << "  fout " << fn.outputs.size() << '\n';
            for (const VisualFunctionParam& p : fn.outputs)
                out << "    param " << std::quoted(p.name.empty() ? std::string("-") : p.name)
                    << ' ' << ValueTypeName(p.type) << '\n';
            out << "  flocals " << fn.locals.size() << '\n';
            for (const VisualVariable& v : fn.locals) {
                out << "    local " << v.id << ' ' << std::quoted(v.name.empty() ? std::string("-") : v.name)
                    << ' ' << ValueTypeName(v.type) << ' ' << (v.exposed ? 1 : 0) << ' ';
                WriteVisualValue(out, v.defaultValue);
                out << '\n';
            }
        }
        // Struct + enum type definitions (v5).
        out << "structs " << structs.size() << '\n';
        for (const VisualStructType& s : structs) {
            out << "struct " << s.id << ' ' << std::quoted(s.name.empty() ? std::string("-") : s.name)
                << ' ' << s.fields.size() << '\n';
            for (const VisualStructField& f : s.fields)
                out << "  field " << std::quoted(f.name.empty() ? std::string("-") : f.name)
                    << ' ' << ValueTypeName(f.type) << ' ' << ValueTypeName(f.elementType)
                    << ' ' << f.typeId << '\n';
        }
        out << "enums " << enums.size() << '\n';
        for (const VisualEnumType& e : enums) {
            out << "enum " << e.id << ' ' << std::quoted(e.name.empty() ? std::string("-") : e.name)
                << ' ' << e.entries.size() << '\n';
            for (const std::string& entry : e.entries)
                out << "  entry " << std::quoted(entry.empty() ? std::string("-") : entry) << '\n';
        }
        // User events + interfaces (v6).
        auto writeParams = [&](const char* tag, const std::vector<VisualFunctionParam>& params) {
            for (const VisualFunctionParam& p : params)
                out << tag << std::quoted(p.name.empty() ? std::string("-") : p.name)
                    << ' ' << ValueTypeName(p.type) << '\n';
        };
        out << "events " << events.size() << '\n';
        for (const VisualCustomEvent& e : events) {
            out << "event " << e.id << ' ' << std::quoted(e.name.empty() ? std::string("-") : e.name)
                << ' ' << e.params.size() << '\n';
            writeParams("  eparam ", e.params);
        }
        out << "interfaces " << interfaces.size() << '\n';
        for (const VisualInterface& itf : interfaces) {
            out << "interface " << itf.id << ' ' << std::quoted(itf.name.empty() ? std::string("-") : itf.name)
                << ' ' << itf.messages.size() << '\n';
            for (const VisualCustomEvent& m : itf.messages) {
                out << "  msg " << m.id << ' ' << std::quoted(m.name.empty() ? std::string("-") : m.name)
                    << ' ' << m.params.size() << '\n';
                writeParams("    mparam ", m.params);
            }
        }
        out << "implements " << implementedInterfaces.size();
        for (std::uint32_t interfaceId : implementedInterfaces) out << ' ' << interfaceId;
        out << '\n';
        // State machines (v7).
        out << "statemachines " << stateMachines.size() << '\n';
        for (const VisualStateMachine& sm : stateMachines) {
            out << "sm " << sm.id << ' ' << std::quoted(sm.name.empty() ? std::string("-") : sm.name)
                << ' ' << sm.entryState << ' ' << sm.states.size() << ' ' << sm.transitions.size() << '\n';
            for (const VisualState& st : sm.states)
                out << "  state " << st.id << ' ' << std::quoted(st.name.empty() ? std::string("-") : st.name)
                    << ' ' << st.onEnter << ' ' << st.onUpdate << ' ' << st.onExit
                    << ' ' << st.editorPosition.x << ' ' << st.editorPosition.y << '\n';
            for (const VisualTransition& tr : sm.transitions)
                out << "  trans " << tr.id << ' ' << tr.from << ' ' << tr.to << ' ' << tr.priority
                    << ' ' << tr.condition << ' ' << tr.cooldown << ' ' << (tr.interruptible ? 1 : 0) << '\n';
        }
        return out.good();
    }

    bool Save(const std::string& path, std::string* error) const {
        std::ofstream file(path, std::ios::trunc);
        if (!file) { if (error) *error = "Could not open visual script for writing."; return false; }
        if (!Save(file)) { if (error) *error = "Failed while writing visual script."; return false; }
        return true;
    }

    bool Load(std::istream& in, std::string* error) {
        *this = VisualScriptAsset{};
        std::string magic, idText;
        if (!(in >> magic >> version >> idText) || magic != kVisualScriptMagic) {
            if (error) *error = "Not a visual script asset.";
            return false;
        }
        if (version < 1 || version > kVisualScriptVersion) {
            if (error) *error = "Unsupported visual script version.";
            return false;
        }
        if (!AssetHandle::Parse(idText, &id)) { if (error) *error = "Invalid visual script id."; return false; }

        std::string key;
        std::size_t nodeCount = 0, linkCount = 0, varCount = 0;
        if (!(in >> key >> graphId) || key != "graph") { if (error) *error = "Missing graph id."; return false; }
        if (!(in >> key >> nodeCount) || key != "nodes") { if (error) *error = "Missing node block."; return false; }
        nodes.reserve(nodeCount);
        for (std::size_t i = 0; i < nodeCount; ++i) {
            VisualNode node;
            if (!(in >> key >> node.id) || key != "node") { if (error) *error = "Malformed node."; return false; }
            in >> std::quoted(node.typeId) >> node.editorPosition.x >> node.editorPosition.y;
            if (version >= 4) in >> node.functionId;   // owning function scope (0 = event graph)
            std::size_t propCount = 0;
            if (!(in >> key >> propCount) || key != "props") { if (error) *error = "Malformed node props."; return false; }
            for (std::size_t p = 0; p < propCount; ++p) {
                std::string name; VisualValue value;
                in >> std::quoted(name);
                if (!ReadVisualValue(in, &value)) { if (error) *error = "Malformed node property value."; return false; }
                node.properties[name] = value;
            }
            if (!ReadPins(in, "in", &node.inputs, error, version)) return false;
            if (!ReadPins(in, "out", &node.outputs, error, version)) return false;
            nodes.push_back(std::move(node));
        }
        if (!(in >> key >> linkCount) || key != "links") { if (error) *error = "Missing link block."; return false; }
        links.reserve(linkCount);
        for (std::size_t i = 0; i < linkCount; ++i) {
            VisualLink link;
            if (!(in >> key >> link.id >> link.fromNode >> link.fromPin >> link.toNode >> link.toPin)
                || key != "link") { if (error) *error = "Malformed link."; return false; }
            links.push_back(link);
        }
        if (!(in >> key >> varCount) || key != "vars") { if (error) *error = "Missing variable block."; return false; }
        variables.reserve(varCount);
        for (std::size_t i = 0; i < varCount; ++i) {
            VisualVariable var; std::string typeName; int exposed = 0;
            if (!(in >> key) || key != "var") { if (error) *error = "Malformed variable."; return false; }
            if (version >= 2) {
                in >> var.id >> std::quoted(var.name) >> typeName >> exposed;
                var.exposed = exposed != 0;
            } else {
                // v1 migration: no stored id -> mint a sequential one, unexposed.
                var.id = static_cast<VariableId>(i + 1);
                in >> std::quoted(var.name) >> typeName;
            }
            ParseValueType(typeName, &var.type);
            if (!ReadVisualValue(in, &var.defaultValue)) { if (error) *error = "Malformed variable value."; return false; }
            variables.push_back(std::move(var));
        }
        // Comments block only exists from v2 (Phase 8 migration hook).
        if (version >= 2) {
            std::size_t commentCount = 0;
            if (!(in >> key >> commentCount) || key != "comments") { if (error) *error = "Missing comment block."; return false; }
            comments.reserve(commentCount);
            for (std::size_t i = 0; i < commentCount; ++i) {
                VisualComment comment; std::string text;
                if (!(in >> key >> comment.id >> comment.position.x >> comment.position.y
                        >> comment.size.x >> comment.size.y) || key != "comment") {
                    if (error) *error = "Malformed comment."; return false;
                }
                in >> std::quoted(text);
                comment.text = (text == "-") ? std::string() : text;
                if (version >= 3) {   // color + collapsed added in Milestone 2
                    int collapsed = 0;
                    in >> comment.color.r >> comment.color.g >> comment.color.b >> comment.color.a
                       >> collapsed;
                    comment.collapsed = collapsed != 0;
                }
                comments.push_back(std::move(comment));
            }
        }
        // Functions block only exists from v4 (Milestone 3).
        if (version >= 4) {
            std::size_t fnCount = 0;
            if (!(in >> key >> fnCount) || key != "functions") { if (error) *error = "Missing functions block."; return false; }
            functions.reserve(fnCount);
            auto readParams = [&](const char* tag, std::vector<VisualFunctionParam>* params) -> bool {
                std::size_t count = 0;
                if (!(in >> key >> count) || key != tag) { if (error) *error = "Malformed function param block."; return false; }
                params->reserve(count);
                for (std::size_t p = 0; p < count; ++p) {
                    VisualFunctionParam param; std::string name, typeName;
                    if (!(in >> key >> std::quoted(name) >> typeName) || key != "param") {
                        if (error) *error = "Malformed function param."; return false;
                    }
                    param.name = (name == "-") ? std::string() : name;
                    ParseValueType(typeName, &param.type);
                    params->push_back(std::move(param));
                }
                return true;
            };
            for (std::size_t i = 0; i < fnCount; ++i) {
                VisualFunction fn; std::string name; int pure = 0;
                if (!(in >> key >> fn.id >> std::quoted(name) >> pure) || key != "func") {
                    if (error) *error = "Malformed function."; return false;
                }
                fn.name = (name == "-") ? std::string() : name;
                fn.pure = pure != 0;
                if (!readParams("fin", &fn.inputs)) return false;
                if (!readParams("fout", &fn.outputs)) return false;
                std::size_t localCount = 0;
                if (!(in >> key >> localCount) || key != "flocals") { if (error) *error = "Malformed function locals."; return false; }
                fn.locals.reserve(localCount);
                for (std::size_t l = 0; l < localCount; ++l) {
                    VisualVariable v; std::string vname, typeName; int exposed = 0;
                    if (!(in >> key >> v.id >> std::quoted(vname) >> typeName >> exposed) || key != "local") {
                        if (error) *error = "Malformed function local."; return false;
                    }
                    v.name = (vname == "-") ? std::string() : vname;
                    v.exposed = exposed != 0;
                    ParseValueType(typeName, &v.type);
                    if (!ReadVisualValue(in, &v.defaultValue)) { if (error) *error = "Malformed function local value."; return false; }
                    fn.locals.push_back(std::move(v));
                }
                functions.push_back(std::move(fn));
            }
        }
        // Struct + enum type definitions (v5).
        if (version >= 5) {
            std::size_t structCount = 0;
            if (!(in >> key >> structCount) || key != "structs") { if (error) *error = "Missing structs block."; return false; }
            structs.reserve(structCount);
            for (std::size_t i = 0; i < structCount; ++i) {
                VisualStructType s; std::string name; std::size_t fieldCount = 0;
                if (!(in >> key >> s.id >> std::quoted(name) >> fieldCount) || key != "struct") {
                    if (error) *error = "Malformed struct."; return false;
                }
                s.name = (name == "-") ? std::string() : name;
                for (std::size_t f = 0; f < fieldCount; ++f) {
                    VisualStructField field; std::string fname, typeName, elemName;
                    if (!(in >> key >> std::quoted(fname) >> typeName >> elemName >> field.typeId) || key != "field") {
                        if (error) *error = "Malformed struct field."; return false;
                    }
                    field.name = (fname == "-") ? std::string() : fname;
                    ParseValueType(typeName, &field.type);
                    ParseValueType(elemName, &field.elementType);
                    s.fields.push_back(std::move(field));
                }
                structs.push_back(std::move(s));
            }
            std::size_t enumCount = 0;
            if (!(in >> key >> enumCount) || key != "enums") { if (error) *error = "Missing enums block."; return false; }
            enums.reserve(enumCount);
            for (std::size_t i = 0; i < enumCount; ++i) {
                VisualEnumType e; std::string name; std::size_t entryCount = 0;
                if (!(in >> key >> e.id >> std::quoted(name) >> entryCount) || key != "enum") {
                    if (error) *error = "Malformed enum."; return false;
                }
                e.name = (name == "-") ? std::string() : name;
                for (std::size_t j = 0; j < entryCount; ++j) {
                    std::string entry;
                    if (!(in >> key >> std::quoted(entry)) || key != "entry") {
                        if (error) *error = "Malformed enum entry."; return false;
                    }
                    e.entries.push_back(entry == "-" ? std::string() : entry);
                }
                enums.push_back(std::move(e));
            }
        }
        // User events + interfaces (v6).
        if (version >= 6) {
            auto readParams = [&](const char* tag, std::size_t count,
                                  std::vector<VisualFunctionParam>* params) -> bool {
                params->reserve(count);
                for (std::size_t p = 0; p < count; ++p) {
                    VisualFunctionParam param; std::string name, typeName;
                    if (!(in >> key >> std::quoted(name) >> typeName) || key != tag) return false;
                    param.name = (name == "-") ? std::string() : name;
                    ParseValueType(typeName, &param.type);
                    params->push_back(std::move(param));
                }
                return true;
            };
            std::size_t eventCount = 0;
            if (!(in >> key >> eventCount) || key != "events") { if (error) *error = "Missing events block."; return false; }
            events.reserve(eventCount);
            for (std::size_t i = 0; i < eventCount; ++i) {
                VisualCustomEvent e; std::string name; std::size_t pc = 0;
                if (!(in >> key >> e.id >> std::quoted(name) >> pc) || key != "event") { if (error) *error = "Malformed event."; return false; }
                e.name = (name == "-") ? std::string() : name;
                if (!readParams("eparam", pc, &e.params)) { if (error) *error = "Malformed event param."; return false; }
                events.push_back(std::move(e));
            }
            std::size_t itfCount = 0;
            if (!(in >> key >> itfCount) || key != "interfaces") { if (error) *error = "Missing interfaces block."; return false; }
            interfaces.reserve(itfCount);
            for (std::size_t i = 0; i < itfCount; ++i) {
                VisualInterface itf; std::string name; std::size_t mc = 0;
                if (!(in >> key >> itf.id >> std::quoted(name) >> mc) || key != "interface") { if (error) *error = "Malformed interface."; return false; }
                itf.name = (name == "-") ? std::string() : name;
                for (std::size_t m = 0; m < mc; ++m) {
                    VisualCustomEvent msg; std::string mname; std::size_t mp = 0;
                    if (!(in >> key >> msg.id >> std::quoted(mname) >> mp) || key != "msg") { if (error) *error = "Malformed interface message."; return false; }
                    msg.name = (mname == "-") ? std::string() : mname;
                    if (!readParams("mparam", mp, &msg.params)) { if (error) *error = "Malformed interface message param."; return false; }
                    itf.messages.push_back(std::move(msg));
                }
                interfaces.push_back(std::move(itf));
            }
            std::size_t implCount = 0;
            if (!(in >> key >> implCount) || key != "implements") { if (error) *error = "Missing implements block."; return false; }
            implementedInterfaces.reserve(implCount);
            for (std::size_t i = 0; i < implCount; ++i) { std::uint32_t interfaceId = 0; in >> interfaceId; implementedInterfaces.push_back(interfaceId); }
        }
        // State machines (v7).
        if (version >= 7) {
            std::size_t smCount = 0;
            if (!(in >> key >> smCount) || key != "statemachines") { if (error) *error = "Missing state machines block."; return false; }
            stateMachines.reserve(smCount);
            for (std::size_t i = 0; i < smCount; ++i) {
                VisualStateMachine sm; std::string name; std::size_t stateCount = 0, transCount = 0;
                if (!(in >> key >> sm.id >> std::quoted(name) >> sm.entryState >> stateCount >> transCount) || key != "sm") {
                    if (error) *error = "Malformed state machine."; return false;
                }
                sm.name = (name == "-") ? std::string() : name;
                for (std::size_t s = 0; s < stateCount; ++s) {
                    VisualState st; std::string sname;
                    if (!(in >> key >> st.id >> std::quoted(sname) >> st.onEnter >> st.onUpdate >> st.onExit
                            >> st.editorPosition.x >> st.editorPosition.y) || key != "state") {
                        if (error) *error = "Malformed state."; return false;
                    }
                    st.name = (sname == "-") ? std::string() : sname;
                    sm.states.push_back(std::move(st));
                }
                for (std::size_t t = 0; t < transCount; ++t) {
                    VisualTransition tr; int interruptible = 1;
                    if (!(in >> key >> tr.id >> tr.from >> tr.to >> tr.priority >> tr.condition
                            >> tr.cooldown >> interruptible) || key != "trans") {
                        if (error) *error = "Malformed transition."; return false;
                    }
                    tr.interruptible = interruptible != 0;
                    sm.transitions.push_back(std::move(tr));
                }
                stateMachines.push_back(std::move(sm));
            }
        }
        return true;
    }

    bool Load(const std::string& path, std::string* error) {
        std::ifstream file(path);
        if (!file) { if (error) *error = "Could not open visual script."; return false; }
        return Load(file, error);
    }

private:
    static void WritePins(std::ostream& out, const char* tag, const std::vector<VisualPin>& pins) {
        out << tag << pins.size() << '\n';
        for (const VisualPin& pin : pins) {
            out << "    pin " << pin.id << ' ' << static_cast<int>(pin.direction) << ' '
                << static_cast<int>(pin.kind) << ' ' << ValueTypeName(pin.type) << ' '
                // v5: container metadata (element/key type + struct/enum id)
                << ValueTypeName(pin.elementType) << ' ' << ValueTypeName(pin.keyType) << ' '
                << pin.typeId << ' '
                << std::quoted(pin.name) << ' ';
            WriteVisualValue(out, pin.defaultValue);
            out << '\n';
        }
    }
    static bool ReadPins(std::istream& in, const char* expectTag,
                         std::vector<VisualPin>* pins, std::string* error, std::uint32_t version) {
        std::string key; std::size_t count = 0;
        if (!(in >> key >> count) || key != expectTag) { if (error) *error = "Malformed pin block."; return false; }
        pins->reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            VisualPin pin; std::string typeName; int dir = 0, kind = 0;
            if (!(in >> key >> pin.id >> dir >> kind >> typeName) || key != "pin") {
                if (error) *error = "Malformed pin."; return false;
            }
            pin.direction = static_cast<PinDirection>(dir);
            pin.kind = static_cast<PinKind>(kind);
            ParseValueType(typeName, &pin.type);
            if (version >= 5) {   // container metadata (Milestone 4)
                std::string elemName, keyName;
                in >> elemName >> keyName >> pin.typeId;
                ParseValueType(elemName, &pin.elementType);
                ParseValueType(keyName, &pin.keyType);
            }
            in >> std::quoted(pin.name);
            if (!ReadVisualValue(in, &pin.defaultValue)) { if (error) *error = "Malformed pin default."; return false; }
            pins->push_back(std::move(pin));
        }
        return true;
    }
};

} // namespace engine::vs
