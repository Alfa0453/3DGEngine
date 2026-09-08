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
// editor-only comment block. Old v1 files load unchanged (ids minted, no comments).
inline constexpr std::uint32_t kVisualScriptVersion = 2;   // Phase 8
inline constexpr const char* kVisualScriptMagic = "3DG_VISUAL_SCRIPT";
inline constexpr const char* kVisualScriptExtension = ".3dgvs";

// ---- Authored pin (Phase 6) ------------------------------------------------
struct VisualPin {
    PinId        id = kInvalidPinId;       // unique within its node
    PinDirection direction = PinDirection::Input;
    PinKind      kind = PinKind::Data;
    ValueType    type = ValueType::Float;  // Exec for control pins
    std::string  name;
    VisualValue  defaultValue;             // used when a data input is unconnected
};

// ---- Authored node (Phase 3) -----------------------------------------------
struct VisualNode {
    NodeId      id = kInvalidNodeId;
    std::string typeId;                     // stable NodeTypeId, e.g. "Core.Branch"
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

// ---- Editor-only comment box (Phase 13; no runtime cost) -------------------
struct VisualComment {
    CommentId   id = kInvalidCommentId;
    glm::vec2   position{0.0f};
    glm::vec2   size{240.0f, 120.0f};
    std::string text;
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

    const VisualNode* FindNode(NodeId node) const {
        for (const VisualNode& n : nodes) if (n.id == node) return &n;
        return nullptr;
    }
    const VisualVariable* FindVariable(VariableId variableId) const {
        for (const VisualVariable& v : variables) if (v.id == variableId) return &v;
        return nullptr;
    }

    // Highest ids currently in use — an editor uses these to mint fresh ids.
    NodeId MaxNodeId() const { NodeId m = 0; for (auto& n : nodes) m = n.id > m ? n.id : m; return m; }
    LinkId MaxLinkId() const { LinkId m = 0; for (auto& l : links) m = l.id > m ? l.id : m; return m; }
    VariableId MaxVariableId() const { VariableId m = 0; for (auto& v : variables) m = v.id > m ? v.id : m; return m; }
    CommentId MaxCommentId() const { CommentId m = 0; for (auto& c : comments) m = c.id > m ? c.id : m; return m; }

    // ---- Serialization (Phase 8; text "MAGIC <version>" like the graph assets) --
    bool Save(std::ostream& out) const {
        if (!id.Valid()) return false;
        out << kVisualScriptMagic << ' ' << version << ' ' << id.ToString() << '\n';
        out << "graph " << graphId << '\n';
        out << "nodes " << nodes.size() << '\n';
        for (const VisualNode& node : nodes) {
            out << "node " << node.id << ' ' << std::quoted(node.typeId)
                << ' ' << node.editorPosition.x << ' ' << node.editorPosition.y << '\n';
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
                << std::quoted(comment.text.empty() ? std::string("-") : comment.text) << '\n';
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
            std::size_t propCount = 0;
            if (!(in >> key >> propCount) || key != "props") { if (error) *error = "Malformed node props."; return false; }
            for (std::size_t p = 0; p < propCount; ++p) {
                std::string name; VisualValue value;
                in >> std::quoted(name);
                if (!ReadVisualValue(in, &value)) { if (error) *error = "Malformed node property value."; return false; }
                node.properties[name] = value;
            }
            if (!ReadPins(in, "in", &node.inputs, error)) return false;
            if (!ReadPins(in, "out", &node.outputs, error)) return false;
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
                comments.push_back(std::move(comment));
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
                << std::quoted(pin.name) << ' ';
            WriteVisualValue(out, pin.defaultValue);
            out << '\n';
        }
    }
    static bool ReadPins(std::istream& in, const char* expectTag,
                         std::vector<VisualPin>* pins, std::string* error) {
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
            in >> std::quoted(pin.name);
            if (!ReadVisualValue(in, &pin.defaultValue)) { if (error) *error = "Malformed pin default."; return false; }
            pins->push_back(std::move(pin));
        }
        return true;
    }
};

} // namespace engine::vs
