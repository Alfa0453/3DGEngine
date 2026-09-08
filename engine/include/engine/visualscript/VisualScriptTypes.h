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
#include <string>
#include <variant>

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

inline constexpr GraphId    kInvalidGraphId    = 0;
inline constexpr NodeId     kInvalidNodeId     = 0;
inline constexpr PinId      kInvalidPinId      = 0;
inline constexpr LinkId     kInvalidLinkId     = 0;
inline constexpr VariableId kInvalidVariableId = 0;
inline constexpr CommentId  kInvalidCommentId  = 0;

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

    VisualValue() = default;
    explicit VisualValue(ValueType t) : type(t) { *this = MakeDefault(t); }

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
