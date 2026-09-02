#pragma once

// Scripting Pass 2 -- reflection, stable IDs, unified script API registry & event schemas.
//
// A purely ADDITIVE metadata layer. It does NOT replace ScriptField, the Script.h helper methods,
// the Lua bindings, the serializers, or the editor Script API panel. Existing string-keyed identity
// (className / field name / event name) keeps working everywhere; this layer sits alongside so the
// editor, the Lua binder, the serializer and (future) visual scripting can consume descriptors and
// stable IDs gradually.
//
// Stable IDs (Phase 2/4/10/15): a compiler- and platform-independent 64-bit FNV-1a hash of the
// human-readable name. We NEVER persist typeid().hash_code(), std::type_index, or pointer addresses.
// The name stays the authoritative human identity; the numeric ID is derived from it deterministically,
// so it is identical across launches, recompiles, and machines, and old name-keyed data maps to it by
// re-hashing. Renames are handled by aliases (Phase 5), not by changing an existing ID.
//
// GL-free, header-only.

#include "engine/gameplay/Script.h"     // ScriptField, ScriptEvent, ScriptHandle, ecs types
#include "engine/ecs/Registry.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace engine {
namespace script {

// ---- stable identity (FNV-1a 64, deterministic function of the name) -------------------------
using ScriptTypeId     = std::uint64_t;
using ScriptFieldId    = std::uint64_t;
using ScriptFunctionId = std::uint64_t;
using ScriptEventId    = std::uint64_t;

inline constexpr std::uint64_t StableId(std::string_view s) {
    std::uint64_t h = 1469598103934665603ull;          // FNV offset basis
    for (char c : s) { h ^= static_cast<std::uint8_t>(c); h *= 1099511628211ull; } // FNV prime
    return h ? h : 1ull;                                 // never 0 (0 == "unset")
}
// A field's stable id is scoped by its owning script so two scripts may reuse a field name.
inline std::uint64_t StableFieldId(std::string_view className, std::string_view fieldName) {
    std::string key; key.reserve(className.size() + 1 + fieldName.size());
    key.append(className); key.push_back('.'); key.append(fieldName);
    return StableId(key);
}

// ---- typed field / value model (Phase 6) -----------------------------------------------------
enum class ScriptFieldType : std::uint8_t {
    Bool, Int, Float, String, Vec2, Vec3, Quat, Color,
    Entity,       // validated against the registry (Phase 7)
    Asset,        // AssetHandle / asset path (Phase 8)
    ScriptRef,    // ScriptHandle (Phase 9)
    Tag           // GameplayTag string
};

// Map to/from the existing ScriptField::Type so old authored fields flow into descriptors unchanged.
inline ScriptFieldType FromLegacyFieldType(ScriptField::Type t) {
    switch (t) {
        case ScriptField::Type::Float:  return ScriptFieldType::Float;
        case ScriptField::Type::Int:    return ScriptFieldType::Int;
        case ScriptField::Type::Bool:   return ScriptFieldType::Bool;
        case ScriptField::Type::String: return ScriptFieldType::String;
        case ScriptField::Type::Vec3:   return ScriptFieldType::Vec3;
        case ScriptField::Type::Color:  return ScriptFieldType::Color;
        case ScriptField::Type::Entity: return ScriptFieldType::Entity;
        case ScriptField::Type::Asset:  return ScriptFieldType::Asset;
    }
    return ScriptFieldType::String;
}

// ---- field descriptor (Phase 4/5/6) ----------------------------------------------------------
struct ScriptFieldDescriptor {
    ScriptFieldId   id = 0;
    std::string     name;                 // storage identity (human)
    std::string     displayName;          // editor label
    ScriptFieldType type = ScriptFieldType::Float;
    std::string     defaultValue = "0";   // string-encoded, matching ScriptField::value
    bool            editorVisible = true;
    bool            serializable = true;
    bool            visualScriptVisible = false;   // opt-in
    std::string     assetType;            // Asset fields: e.g. ".3dgmat"
    std::vector<std::string> aliases;     // Phase 5: former names that resolve to this field
    float           minValue = 0.0f, maxValue = 0.0f;
    std::string     tooltip;
    std::string     group;

    bool MatchesNameOrAlias(std::string_view stored) const {
        if (stored == name) return true;
        for (const std::string& a : aliases) if (stored == a) return true;
        return false;
    }
};

// ---- function descriptor (Phase 10/12) -------------------------------------------------------
enum ScriptFunctionFlags : std::uint32_t {
    SFF_None               = 0,
    SFF_NativeVisible      = 1u << 0,
    SFF_LuaVisible         = 1u << 1,
    SFF_VisualScriptVisible= 1u << 2,
    SFF_RuntimeOnly        = 1u << 3,
    SFF_EditorOnly         = 1u << 4,
    SFF_MainThreadOnly     = 1u << 5,
    SFF_FixedStepSafe      = 1u << 6,
    SFF_StructuralCommand  = 1u << 7,
    SFF_Deprecated         = 1u << 8,
};

struct ScriptParam {
    std::string     name;
    ScriptFieldType type = ScriptFieldType::Float;
};

struct ScriptFunctionDescriptor {
    ScriptFunctionId id = 0;
    std::string      name;
    std::string      category;
    ScriptFieldType  returnType = ScriptFieldType::Bool;
    bool             hasReturn = true;
    std::vector<ScriptParam> params;
    std::uint32_t    flags = SFF_NativeVisible;   // bitmask of ScriptFunctionFlags
    std::string      deprecationNote;

    bool Has(ScriptFunctionFlags f) const { return (flags & f) != 0; }
};

// ---- event descriptor + typed payload schema (Phase 15/16/17) --------------------------------
struct EventFieldSchema {
    std::string     name;
    ScriptFieldType type = ScriptFieldType::Float;
    bool            required = true;
};

struct EventDescriptor {
    ScriptEventId id = 0;
    std::string   name;                 // human "Combat.Hit"
    std::vector<EventFieldSchema> fields;

    // Development-build validation: check the payload carries each required field with the right
    // type. Returns true when valid (or when no schema fields are declared -- dynamic events pass).
    // Never used to gate release gameplay; a missing schema simply means "unvalidated / dynamic".
    bool ValidatePayload(const ScriptEvent& e, std::vector<std::string>* problems = nullptr) const {
        bool ok = true;
        for (const EventFieldSchema& f : fields) {
            bool present = false;
            switch (f.type) {
                case ScriptFieldType::Bool:   present = e.bools.count(f.name);   break;
                case ScriptFieldType::Int:    present = e.ints.count(f.name);    break;
                case ScriptFieldType::Float:  present = e.floats.count(f.name);  break;
                case ScriptFieldType::String:
                case ScriptFieldType::Asset:
                case ScriptFieldType::Tag:    present = e.strings.count(f.name); break;
                case ScriptFieldType::Vec2:
                case ScriptFieldType::Vec3:
                case ScriptFieldType::Quat:
                case ScriptFieldType::Color:  present = e.vectors.count(f.name); break;
                case ScriptFieldType::Entity:
                case ScriptFieldType::ScriptRef: present = e.entities.count(f.name); break;
            }
            if (f.required && !present) {
                ok = false;
                if (problems) problems->push_back("event '" + name + "' missing required field '" + f.name + "'");
            }
        }
        return ok;
    }
};

// ---- script descriptor (Phase 3) -------------------------------------------------------------
struct ScriptDescriptor {
    ScriptTypeId  id = 0;
    std::string   className;             // authoritative human identity (unchanged from today)
    std::string   displayName;
    std::string   category;
    std::function<std::unique_ptr<Script>()> createFactory;   // optional; ScriptRegistry still owns creation
    std::vector<ScriptFieldDescriptor> fields;
    std::vector<ScriptFunctionId>      callableFunctions;
    std::vector<ScriptEventId>         supportedEvents;
    std::vector<std::string>           aliases;   // former class names (Phase 2 compat mapping)

    // Resolve a stored field name (possibly an old alias) to its current descriptor -- so a safe
    // rename never drops the old serialized value (Phase 5).
    const ScriptFieldDescriptor* ResolveField(std::string_view storedName) const {
        for (const ScriptFieldDescriptor& f : fields) if (f.MatchesNameOrAlias(storedName)) return &f;
        return nullptr;
    }
    bool MatchesNameOrAlias(std::string_view stored) const {
        if (stored == className) return true;
        for (const std::string& a : aliases) if (stored == a) return true;
        return false;
    }
};

// ---- entity / asset / script reference safety helpers (Phase 7/8/9) --------------------------
// Resolve an entity-valued field against the live registry: a dead/stale handle -> kNull, never an
// unchecked raw id.
inline ecs::Entity ResolveEntityField(const ecs::Registry& reg, ecs::Entity stored) {
    return reg.Valid(stored) ? stored : ecs::kNull;
}
// A script-reference field only resolves if the target entity is alive AND still carries the class.
inline bool ResolveScriptField(const ecs::Registry& reg, const ScriptHandle& h) {
    return h.entity != ecs::kNull && !h.className.empty() && reg.Valid(h.entity);
}

// ---- unified Script API registry (Phase 10/14) -----------------------------------------------
struct ApiParityReport {
    std::vector<std::string> nativeOnly;
    std::vector<std::string> luaOnly;
    std::vector<std::string> shared;
    std::vector<std::string> visualScriptVisible;
    std::vector<std::string> deprecated;
};

class ScriptApiRegistry {
public:
    static ScriptApiRegistry& Get() { static ScriptApiRegistry r; return r; }

    // Scripts -----------------------------------------------------------------------------------
    void RegisterScript(ScriptDescriptor d) {
        if (d.id == 0) d.id = StableId(d.className);
        for (ScriptFieldDescriptor& f : d.fields)
            if (f.id == 0) f.id = StableFieldId(d.className, f.name);
        for (auto& e : m_scripts) if (e.id == d.id) { e = std::move(d); return; }
        m_scripts.push_back(std::move(d));
    }
    const ScriptDescriptor* FindScript(ScriptTypeId id) const {
        for (const auto& d : m_scripts) if (d.id == id) return &d;
        return nullptr;
    }
    const ScriptDescriptor* FindScriptByName(std::string_view name) const {
        for (const auto& d : m_scripts) if (d.MatchesNameOrAlias(name)) return &d;
        return nullptr;
    }
    const std::vector<ScriptDescriptor>& Scripts() const { return m_scripts; }

    // Functions ---------------------------------------------------------------------------------
    void RegisterFunction(ScriptFunctionDescriptor d) {
        if (d.id == 0) d.id = StableId(d.name);
        for (auto& e : m_functions) if (e.id == d.id) { e = std::move(d); return; }
        m_functions.push_back(std::move(d));
    }
    const ScriptFunctionDescriptor* FindFunction(ScriptFunctionId id) const {
        for (const auto& d : m_functions) if (d.id == id) return &d;
        return nullptr;
    }
    const ScriptFunctionDescriptor* FindFunctionByName(std::string_view name) const {
        return FindFunction(StableId(name));
    }
    const std::vector<ScriptFunctionDescriptor>& Functions() const { return m_functions; }

    // Events ------------------------------------------------------------------------------------
    ScriptEventId RegisterEvent(EventDescriptor d) {
        if (d.id == 0) d.id = StableId(d.name);
        for (auto& e : m_events) if (e.id == d.id) { e = std::move(d); return e.id; }
        const ScriptEventId id = d.id;
        m_events.push_back(std::move(d));
        return id;
    }
    // Phase 15: resolve a human event name to its stable id (registered or not -- ids are derivable).
    ScriptEventId EventId(std::string_view name) const { return StableId(name); }
    const EventDescriptor* FindEvent(ScriptEventId id) const {
        for (const auto& d : m_events) if (d.id == id) return &d;
        return nullptr;
    }
    const EventDescriptor* FindEventByName(std::string_view name) const { return FindEvent(StableId(name)); }
    const std::vector<EventDescriptor>& Events() const { return m_events; }

    void Clear() { m_scripts.clear(); m_functions.clear(); m_events.clear(); }

    // Phase 14: API parity report over the registered functions and their exposure flags.
    ApiParityReport BuildParityReport() const {
        ApiParityReport r;
        for (const ScriptFunctionDescriptor& f : m_functions) {
            const bool nat = f.Has(SFF_NativeVisible);
            const bool lua = f.Has(SFF_LuaVisible);
            if (nat && lua) r.shared.push_back(f.name);
            else if (nat)   r.nativeOnly.push_back(f.name);
            else if (lua)   r.luaOnly.push_back(f.name);
            if (f.Has(SFF_VisualScriptVisible)) r.visualScriptVisible.push_back(f.name);
            if (f.Has(SFF_Deprecated))          r.deprecated.push_back(f.name);
        }
        return r;
    }

private:
    std::vector<ScriptDescriptor>          m_scripts;
    std::vector<ScriptFunctionDescriptor>  m_functions;
    std::vector<EventDescriptor>           m_events;
};

// ---- descriptor builder from existing authored fields (Phase 20 normalize) -------------------
// Turn a live NativeScriptSlot's authored ScriptField list into a descriptor without changing the
// on-disk format: names stay the storage identity, ids are derived, types map from the legacy enum.
inline ScriptDescriptor DescribeFromSlot(const std::string& className,
                                         const std::vector<ScriptField>& authored) {
    ScriptDescriptor d;
    d.className = className; d.displayName = className; d.id = StableId(className);
    for (const ScriptField& f : authored) {
        ScriptFieldDescriptor fd;
        fd.name = f.name; fd.displayName = f.name.empty() ? f.name : f.name;
        fd.type = FromLegacyFieldType(f.type);
        fd.defaultValue = f.value;
        fd.minValue = f.minValue; fd.maxValue = f.maxValue;
        fd.tooltip = f.tooltip; fd.group = f.group;
        fd.id = StableFieldId(className, f.name);
        d.fields.push_back(std::move(fd));
    }
    return d;
}

} // namespace script
} // namespace engine
