#pragma once

// =============================================================================
// Visual Scripting — Pass 1 : type system, runtime value, and stable identity.
//
// This header is DEPENDENCY-LIGHT and EDITOR-FREE (Phase 23): it lives under
// engine/ and pulls in only glm, the ECS Entity id, and the AssetHandle. It is
// header-only so it adds no new translation unit / CMake reconfigure.
//
// It defines:
//   * stable id aliases (GraphId / NodeId / PinId / LinkId)   — Phase 2
//   * ValueType                                                — Phase 4
//   * VisualValue (a bounded, typed runtime value)             — Phase 5
//   * PinDirection / PinKind                                   — Phase 6
//   * explicit conversion rules                                — Phase 5
// =============================================================================

#include <engine/assets/AssetIdentity.h>
#include <engine/ecs/Entity.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace engine::vs {

// ---- Stable identity (Phase 2) ---------------------------------------------
// These are AUTHORED identities. They must survive save/load, node reorder,
// editor restart and recompilation. They are NEVER a vector index, pointer,
// ImGui id, or runtime execution index.
using GraphId    = std::uint64_t;
using NodeId     = std::uint32_t;
using PinId      = std::uint32_t;   // unique within its owning node
using LinkId     = std::uint32_t;
using VariableId = std::uint32_t;   // graph-scoped; stable across rename (Pass 3, Phase 11/12)
using CommentId  = std::uint32_t;
using FunctionId = std::uint32_t;   // graph-scoped function/subgraph identity (Milestone 3)

inline constexpr GraphId    kInvalidGraphId    = 0;
inline constexpr NodeId     kInvalidNodeId     = 0;
inline constexpr PinId      kInvalidPinId      = 0;
inline constexpr LinkId     kInvalidLinkId     = 0;
inline constexpr VariableId kInvalidVariableId = 0;
inline constexpr CommentId  kInvalidCommentId  = 0;
inline constexpr FunctionId kInvalidFunctionId = 0;   // 0 == the event graph scope

// ---- Value types (Phase 4) -------------------------------------------------
enum class ValueType : std::uint8_t {
    Exec = 0,     // control flow only — carries no data
    Bool,
    Int,
    Float,
    String,
    Vector2,
    Vector3,
    Entity,
    Asset,        // AssetHandle
    Quaternion,
    Color,        // rgba stored as vec4
    ScriptHandle, // reserved (entity + class name); Pass 1 stores the entity only
    Array,        // typed, ordered list (elementType) — Milestone 4
    Map,          // typed key->value dictionary (keyType/elementType) — Milestone 4
    Struct,       // user-authored record (typeId -> ordered fields) — Milestone 4
    Enum,         // named integer options (typeId) — Milestone 4
    Count
};

inline const char* ValueTypeName(ValueType type) {
    switch (type) {
        case ValueType::Exec:         return "Exec";
        case ValueType::Bool:         return "Bool";
        case ValueType::Int:          return "Int";
        case ValueType::Float:        return "Float";
        case ValueType::String:       return "String";
        case ValueType::Vector2:      return "Vector2";
        case ValueType::Vector3:      return "Vector3";
        case ValueType::Entity:       return "Entity";
        case ValueType::Asset:        return "Asset";
        case ValueType::Quaternion:   return "Quaternion";
        case ValueType::Color:        return "Color";
        case ValueType::ScriptHandle: return "ScriptHandle";
        case ValueType::Array:        return "Array";
        case ValueType::Map:          return "Map";
        case ValueType::Struct:       return "Struct";
        case ValueType::Enum:         return "Enum";
        default:                      return "Unknown";
    }
}

inline bool ParseValueType(const std::string& name, ValueType* out) {
    for (std::uint8_t i = 0; i < static_cast<std::uint8_t>(ValueType::Count); ++i) {
        const auto candidate = static_cast<ValueType>(i);
        if (name == ValueTypeName(candidate)) { if (out) *out = candidate; return true; }
    }
    return false;
}

// ---- Pin classification (Phase 6) ------------------------------------------
enum class PinDirection : std::uint8_t { Input = 0, Output = 1 };
enum class PinKind      : std::uint8_t { Exec = 0, Data = 1 };

// ---- Runtime value (Phase 5) -----------------------------------------------
// A bounded, typed value. NOT arbitrary C++ type erasure: the variant only ever
// holds one of the supported ValueType payloads. Exec pins never carry a value.
struct VisualValue {
    using Storage = std::variant<
        std::monostate,   // Exec / empty
        bool,             // Bool
        int,              // Int
        float,            // Float
        std::string,      // String
        glm::vec2,        // Vector2
        glm::vec3,        // Vector3
        ecs::Entity,      // Entity
        AssetHandle,      // Asset
        glm::quat,        // Quaternion
        glm::vec4>;       // Color

    ValueType type = ValueType::Float;
    Storage   data = 0.0f;

    // ---- Container payloads (Milestone 4) ----------------------------------
    // Only populated for the matching `type`. Arrays/structs share `elements`; maps use the two
    // parallel key/value vectors. Copies are shallow (shared_ptr) — mutation nodes CloneDeep() first,
    // so authored/runtime values keep value semantics without per-copy deep clones.
    std::shared_ptr<std::vector<VisualValue>> elements;   // Array items OR Struct fields (ordered)
    std::shared_ptr<std::vector<VisualValue>> mapKeys;    // Map keys (parallel to mapValues)
    std::shared_ptr<std::vector<VisualValue>> mapValues;  // Map values
    ValueType     elementType = ValueType::Float;         // Array element / Map value type
    ValueType     keyType     = ValueType::String;        // Map key type
    std::uint32_t typeId      = 0;                         // Struct / Enum definition id

    VisualValue() = default;
    explicit VisualValue(ValueType t) : type(t) { *this = MakeDefault(t); }

    // Value semantics with DEEP container copies (Milestone 4): copying a VisualValue clones its
    // array/map/struct payload recursively, so authored snapshots (undo), variable storage, and
    // interpreter reads never alias each other's containers. Moves stay cheap.
    VisualValue(const VisualValue& o)
        : type(o.type), data(o.data), elementType(o.elementType), keyType(o.keyType), typeId(o.typeId) {
        if (o.elements)  elements  = std::make_shared<std::vector<VisualValue>>(*o.elements);
        if (o.mapKeys)   mapKeys   = std::make_shared<std::vector<VisualValue>>(*o.mapKeys);
        if (o.mapValues) mapValues = std::make_shared<std::vector<VisualValue>>(*o.mapValues);
    }
    VisualValue& operator=(const VisualValue& o) {
        if (this != &o) { VisualValue tmp(o); *this = std::move(tmp); }
        return *this;
    }
    VisualValue(VisualValue&&) noexcept = default;
    VisualValue& operator=(VisualValue&&) noexcept = default;

    // ---- Constructors for each supported type ------------------------------
    static VisualValue Exec()                       { VisualValue v; v.type = ValueType::Exec; v.data = std::monostate{}; return v; }
    static VisualValue Bool(bool b)                 { VisualValue v; v.type = ValueType::Bool; v.data = b; return v; }
    static VisualValue Int(int i)                   { VisualValue v; v.type = ValueType::Int; v.data = i; return v; }
    static VisualValue Float(float f)               { VisualValue v; v.type = ValueType::Float; v.data = f; return v; }
    static VisualValue Str(std::string s)           { VisualValue v; v.type = ValueType::String; v.data = std::move(s); return v; }
    static VisualValue Vec2(const glm::vec2& p)     { VisualValue v; v.type = ValueType::Vector2; v.data = p; return v; }
    static VisualValue Vec3(const glm::vec3& p)     { VisualValue v; v.type = ValueType::Vector3; v.data = p; return v; }
    static VisualValue Ent(ecs::Entity e)           { VisualValue v; v.type = ValueType::Entity; v.data = e; return v; }
    static VisualValue Asset(const AssetHandle& h)  { VisualValue v; v.type = ValueType::Asset; v.data = h; return v; }
    static VisualValue Quat(const glm::quat& q)     { VisualValue v; v.type = ValueType::Quaternion; v.data = q; return v; }
    static VisualValue Col(const glm::vec4& c)      { VisualValue v; v.type = ValueType::Color; v.data = c; return v; }

    // ---- Container constructors (Milestone 4) ------------------------------
    static VisualValue MakeArray(ValueType elementType) {
        VisualValue v; v.type = ValueType::Array; v.data = std::monostate{};
        v.elementType = elementType;
        v.elements = std::make_shared<std::vector<VisualValue>>();
        return v;
    }
    static VisualValue MakeMap(ValueType keyType, ValueType valueType) {
        VisualValue v; v.type = ValueType::Map; v.data = std::monostate{};
        v.keyType = keyType; v.elementType = valueType;
        v.mapKeys = std::make_shared<std::vector<VisualValue>>();
        v.mapValues = std::make_shared<std::vector<VisualValue>>();
        return v;
    }
    static VisualValue MakeStruct(std::uint32_t structTypeId, std::vector<VisualValue> fields = {}) {
        VisualValue v; v.type = ValueType::Struct; v.data = std::monostate{};
        v.typeId = structTypeId;
        v.elements = std::make_shared<std::vector<VisualValue>>(std::move(fields));
        return v;
    }
    static VisualValue MakeEnum(std::uint32_t enumTypeId, int value) {
        VisualValue v; v.type = ValueType::Enum; v.data = value; v.typeId = enumTypeId; return v;
    }

    // ---- Container access ---------------------------------------------------
    std::vector<VisualValue>& Elements() {
        if (!elements) elements = std::make_shared<std::vector<VisualValue>>();
        return *elements;
    }
    const std::vector<VisualValue>& Elements() const {
        static const std::vector<VisualValue> kEmpty; return elements ? *elements : kEmpty;
    }
    std::vector<VisualValue>& MapKeys() {
        if (!mapKeys) mapKeys = std::make_shared<std::vector<VisualValue>>();
        return *mapKeys;
    }
    std::vector<VisualValue>& MapValues() {
        if (!mapValues) mapValues = std::make_shared<std::vector<VisualValue>>();
        return *mapValues;
    }
    const std::vector<VisualValue>& MapKeysConst() const {
        static const std::vector<VisualValue> kEmpty; return mapKeys ? *mapKeys : kEmpty;
    }
    const std::vector<VisualValue>& MapValuesConst() const {
        static const std::vector<VisualValue> kEmpty; return mapValues ? *mapValues : kEmpty;
    }

    // A deep copy: clones the owned container vectors (recursively) so mutation nodes can edit a
    // returned value without aliasing the source array/struct/map.
    VisualValue CloneDeep() const {
        VisualValue v = *this;
        if (elements)  { v.elements  = std::make_shared<std::vector<VisualValue>>(*elements);
                         for (VisualValue& e : *v.elements) e = e.CloneDeep(); }
        if (mapKeys)   { v.mapKeys   = std::make_shared<std::vector<VisualValue>>(*mapKeys);
                         for (VisualValue& e : *v.mapKeys) e = e.CloneDeep(); }
        if (mapValues) { v.mapValues = std::make_shared<std::vector<VisualValue>>(*mapValues);
                         for (VisualValue& e : *v.mapValues) e = e.CloneDeep(); }
        return v;
    }

    static VisualValue MakeDefault(ValueType t) {
        switch (t) {
            case ValueType::Exec:         return Exec();
            case ValueType::Bool:         return Bool(false);
            case ValueType::Int:          return Int(0);
            case ValueType::Float:        return Float(0.0f);
            case ValueType::String:       return Str(std::string());
            case ValueType::Vector2:      return Vec2(glm::vec2(0.0f));
            case ValueType::Vector3:      return Vec3(glm::vec3(0.0f));
            case ValueType::Entity:       return Ent(ecs::kNull);
            case ValueType::Asset:        return Asset(AssetHandle{});
            case ValueType::Quaternion:   return Quat(glm::quat(1, 0, 0, 0));
            case ValueType::Color:        return Col(glm::vec4(1.0f));
            case ValueType::ScriptHandle: return Ent(ecs::kNull);
            case ValueType::Array:        return MakeArray(ValueType::Float);   // element type refined by caller
            case ValueType::Map:          return MakeMap(ValueType::String, ValueType::Float);
            case ValueType::Struct:       return MakeStruct(0);
            case ValueType::Enum:         return MakeEnum(0, 0);
            default:                      return Float(0.0f);
        }
    }

    // ---- Typed getters with default fallbacks ------------------------------
    bool  AsBool(bool fallback = false) const { auto* p = std::get_if<bool>(&data); return p ? *p : fallback; }
    int   AsInt(int fallback = 0) const { auto* p = std::get_if<int>(&data); return p ? *p : fallback; }
    float AsFloat(float fallback = 0.0f) const { auto* p = std::get_if<float>(&data); return p ? *p : fallback; }
    const std::string& AsString() const {
        static const std::string kEmpty;
        auto* p = std::get_if<std::string>(&data); return p ? *p : kEmpty;
    }
    glm::vec2 AsVec2(const glm::vec2& fallback = glm::vec2(0.0f)) const { auto* p = std::get_if<glm::vec2>(&data); return p ? *p : fallback; }
    glm::vec3 AsVec3(const glm::vec3& fallback = glm::vec3(0.0f)) const { auto* p = std::get_if<glm::vec3>(&data); return p ? *p : fallback; }
    ecs::Entity AsEntity() const { auto* p = std::get_if<ecs::Entity>(&data); return p ? *p : ecs::kNull; }
    AssetHandle AsAsset() const { auto* p = std::get_if<AssetHandle>(&data); return p ? *p : AssetHandle{}; }
    glm::quat AsQuat() const { auto* p = std::get_if<glm::quat>(&data); return p ? *p : glm::quat(1, 0, 0, 0); }
    glm::vec4 AsColor(const glm::vec4& fallback = glm::vec4(1.0f)) const { auto* p = std::get_if<glm::vec4>(&data); return p ? *p : fallback; }

    // Value equality (Milestone 4) — used by Contains/Find/Map lookups. Containers compare
    // element-wise; maps compare as unordered key->value sets.
    bool Equals(const VisualValue& o) const {
        if (type != o.type) return false;
        switch (type) {
            case ValueType::Exec:         return true;
            case ValueType::Bool:         return AsBool() == o.AsBool();
            case ValueType::Int:
            case ValueType::Enum:         return AsInt() == o.AsInt();
            case ValueType::Float:        return AsFloat() == o.AsFloat();
            case ValueType::String:       return AsString() == o.AsString();
            case ValueType::Vector2:      return AsVec2() == o.AsVec2();
            case ValueType::Vector3:      return AsVec3() == o.AsVec3();
            case ValueType::Entity:
            case ValueType::ScriptHandle: return AsEntity() == o.AsEntity();
            case ValueType::Asset:        return AsAsset() == o.AsAsset();
            case ValueType::Quaternion:   return AsQuat() == o.AsQuat();
            case ValueType::Color:        return AsColor() == o.AsColor();
            case ValueType::Array:
            case ValueType::Struct: {
                const auto& a = Elements(); const auto& b = o.Elements();
                if (a.size() != b.size()) return false;
                for (std::size_t i = 0; i < a.size(); ++i) if (!a[i].Equals(b[i])) return false;
                return true;
            }
            case ValueType::Map: {
                const auto& ak = MapKeysConst(); const auto& av = MapValuesConst();
                const auto& bk = o.MapKeysConst(); const auto& bv = o.MapValuesConst();
                if (ak.size() != bk.size()) return false;
                for (std::size_t i = 0; i < ak.size(); ++i) {
                    bool found = false;
                    for (std::size_t j = 0; j < bk.size(); ++j)
                        if (ak[i].Equals(bk[j])) { found = av[i].Equals(bv[j]); break; }
                    if (!found) return false;
                }
                return true;
            }
            default: return false;
        }
    }

    // ---- Explicit conversion (Phase 5) -------------------------------------
    // Widening Int -> Float is allowed. Float -> Int is allowed only as an
    // *explicit* conversion (truncation). Vector3 -> Float is NOT allowed. No
    // ambiguous automatic casts are ever performed. Returns false if disallowed.
    bool ConvertTo(ValueType target, VisualValue* out) const {
        if (target == type) { if (out) *out = *this; return true; }
        switch (target) {
            case ValueType::Float:
                if (type == ValueType::Int)  { if (out) *out = Float(static_cast<float>(AsInt())); return true; }
                if (type == ValueType::Bool) { if (out) *out = Float(AsBool() ? 1.0f : 0.0f); return true; }
                return false;
            case ValueType::Int:
                if (type == ValueType::Float) { if (out) *out = Int(static_cast<int>(AsFloat())); return true; } // explicit truncation
                if (type == ValueType::Bool)  { if (out) *out = Int(AsBool() ? 1 : 0); return true; }
                return false;
            case ValueType::Bool:
                if (type == ValueType::Int)   { if (out) *out = Bool(AsInt() != 0); return true; }
                if (type == ValueType::Float) { if (out) *out = Bool(AsFloat() != 0.0f); return true; }
                return false;
            case ValueType::Color:
                if (type == ValueType::Vector3) { const glm::vec3 v = AsVec3(); if (out) *out = Col(glm::vec4(v, 1.0f)); return true; }
                return false;
            case ValueType::Vector3:
                if (type == ValueType::Color) { const glm::vec4 c = AsColor(); if (out) *out = Vec3(glm::vec3(c)); return true; }
                return false;
            default:
                return false;
        }
    }
};

// Whether a data pin of type `from` may feed a data pin of type `to`. STRICT: only the exact same
// type connects — a link never silently converts between types. To cross types, the user inserts an
// explicit Convert.* node. (Runtime pin-default coercion still uses VisualValue::ConvertTo; this rule
// governs authoring links + the pin-drag search filter only.)
inline bool DataTypesCompatible(ValueType from, ValueType to) {
    if (from == ValueType::Exec || to == ValueType::Exec) return false;
    return from == to;
}

} // namespace engine::vs
