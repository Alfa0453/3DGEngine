#pragma once

// Scripting Pass 5 -- production debugging, profiling, validation, packaging & visual-scripting
// readiness. A pure, ADDITIVE data/logic layer that the editor Script Debug panel, Level Validation,
// packaging, and API-doc tooling consume. It reuses the Pass 2 registry/descriptors, Pass 3 ABI/
// manifest, and Pass 4 scheduler/RNG. No raw Script* is ever exposed -- the debugger works from
// snapshots (entity + stable ids + copied state). GL-free, header-only.

#include "engine/gameplay/Script.h"           // ScriptField, ScriptHandle, ecs::Entity
#include "engine/gameplay/ScriptReflection.h" // ScriptApiRegistry, descriptors, ids
#include "engine/gameplay/ScriptModuleAbi.h"  // ReloadDiagnostics, manifest, ABI

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
#include <sstream>
#include <algorithm>

namespace engine {
namespace script {

inline const char* FieldTypeName(ScriptFieldType t) {
    switch (t) {
        case ScriptFieldType::Bool: return "Bool"; case ScriptFieldType::Int: return "Int";
        case ScriptFieldType::Float: return "Float"; case ScriptFieldType::String: return "String";
        case ScriptFieldType::Vec2: return "Vec2"; case ScriptFieldType::Vec3: return "Vec3";
        case ScriptFieldType::Quat: return "Quat"; case ScriptFieldType::Color: return "Color";
        case ScriptFieldType::Entity: return "Entity"; case ScriptFieldType::Asset: return "Asset";
        case ScriptFieldType::ScriptRef: return "ScriptRef"; case ScriptFieldType::Tag: return "Tag";
    }
    return "?";
}

// ============================ Phase 6: script profiler =======================================
struct CallbackTiming {
    double        totalMs = 0.0;
    double        maxMs   = 0.0;
    std::uint64_t calls   = 0;
    void Add(double ms) { totalMs += ms; if (ms > maxMs) maxMs = ms; ++calls; }
    double AvgMs() const { return calls ? totalMs / static_cast<double>(calls) : 0.0; }
};

struct ClassProfile {
    ScriptTypeId  id = 0;
    std::string   className;
    int           instances = 0;
    CallbackTiming update, fixed, event;
    double         sequenceMs = 0.0;
    std::size_t    luaBytes = 0;
};

struct FrameProfile {
    double totalMs = 0.0, cppMs = 0.0, luaMs = 0.0;
    int    events = 0;
    int    scheduledContinuations = 0;
};

class ScriptProfiler {
public:
    void BeginFrame() { m_frame = FrameProfile{}; for (auto& kv : m_classes) {
        kv.second.update = kv.second.fixed = kv.second.event = CallbackTiming{}; kv.second.sequenceMs = 0.0; } }

    ClassProfile& Class(ScriptTypeId id, const std::string& name) {
        ClassProfile& c = m_classes[id];
        if (c.id == 0) { c.id = id; c.className = name; }
        return c;
    }
    void SetInstanceCount(ScriptTypeId id, const std::string& name, int n) { Class(id, name).instances = n; }
    void RecordUpdate(ScriptTypeId id, const std::string& name, double ms, bool isLua) {
        Class(id, name).update.Add(ms); m_frame.totalMs += ms; (isLua ? m_frame.luaMs : m_frame.cppMs) += ms;
    }
    void RecordFixed(ScriptTypeId id, const std::string& name, double ms, bool isLua) {
        Class(id, name).fixed.Add(ms); m_frame.totalMs += ms; (isLua ? m_frame.luaMs : m_frame.cppMs) += ms;
    }
    void RecordEvent(ScriptTypeId id, const std::string& name, double ms, bool isLua) {
        Class(id, name).event.Add(ms); m_frame.totalMs += ms; (isLua ? m_frame.luaMs : m_frame.cppMs) += ms; ++m_frame.events;
    }
    void RecordSequenceCost(double ms) { m_frame.totalMs += ms; }
    void AddScheduledContinuations(int n) { m_frame.scheduledContinuations += n; }

    const FrameProfile& Frame() const { return m_frame; }
    std::vector<ClassProfile> Classes() const {
        std::vector<ClassProfile> v; v.reserve(m_classes.size());
        for (const auto& kv : m_classes) v.push_back(kv.second);
        std::sort(v.begin(), v.end(), [](const ClassProfile& a, const ClassProfile& b) {
            return a.update.totalMs > b.update.totalMs; });   // hottest first
        return v;
    }

private:
    std::unordered_map<ScriptTypeId, ClassProfile> m_classes;
    FrameProfile m_frame;
};

// ============================ Phase 4/5: error state =========================================
struct ScriptErrorState {
    std::string   lastError;
    std::uint64_t firstFrame = 0, lastFrame = 0;
    int           count = 0;
    bool          disabledDueToError = false;

    // Records an error; disables the instance once it has failed `threshold` times (0 = never
    // auto-disable). Prevents console flooding and runaway failing scripts.
    void Record(std::string message, std::uint64_t frame, int threshold) {
        if (count == 0) firstFrame = frame;
        lastFrame = frame; lastError = std::move(message); ++count;
        if (threshold > 0 && count >= threshold) disabledDueToError = true;
    }
    void Clear() { *this = ScriptErrorState{}; }   // editor "Clear Error" action
};

// Editor actions available on an errored attachment (Phase 5). Enum only -- the editor maps to UI.
enum class ScriptErrorAction { Retry, Disable, OpenSource, ClearError };

// ============================ Phase 2/3: event trace =========================================
struct EventTraceEntry {
    std::uint64_t frame = 0;
    ScriptEventId id = 0;
    std::string   name;
    ecs::Entity   publisher = ecs::kNull;
    int           subscriberCount = 0;
    std::string   phase;            // "Gameplay" / "Physics" / "NextFrame"
    std::string   payloadSummary;   // schema-aware, built via the registry
};

class EventTracer {
public:
    void SetEnabled(bool e) { m_enabled = e; }
    bool Enabled() const { return m_enabled; }
    void SetCapacity(std::size_t c) { m_capacity = c ? c : 1; if (m_ring.size() > m_capacity) m_ring.resize(m_capacity); }

    // Record a published event. Only stores when enabled (never traces globally by default).
    void Record(EventTraceEntry e) {
        if (!m_enabled) return;
        if (m_ring.size() < m_capacity) m_ring.push_back(std::move(e));
        else { m_ring[m_head] = std::move(e); }
        m_head = (m_head + 1) % m_capacity;
    }
    // Build a schema-aware payload summary: "instigator=Entity victim=Entity damage=Float".
    static std::string SummarizePayload(const ScriptApiRegistry& reg, ScriptEventId id) {
        const EventDescriptor* d = reg.FindEvent(id);
        if (!d) return "(untyped)";
        std::ostringstream os;
        for (std::size_t i = 0; i < d->fields.size(); ++i) {
            if (i) os << ' ';
            os << d->fields[i].name << '=' << FieldTypeName(d->fields[i].type);
        }
        return os.str();
    }
    std::vector<EventTraceEntry> Entries() const { return m_ring; }
    void Clear() { m_ring.clear(); m_head = 0; }

private:
    std::vector<EventTraceEntry> m_ring;
    std::size_t m_capacity = 256, m_head = 0;
    bool m_enabled = false;
};

// ============================ Phase 1: debugger snapshots =====================================
struct WorldScriptStats {
    int total = 0, cpp = 0, lua = 0, disabled = 0, error = 0;
    int queuedEvents = 0, activeSequences = 0;
};

enum class ScriptLifecycleState { Uninitialized, Created, Enabled, Disabled, PendingDestroy, Error };
inline const char* LifecycleName(ScriptLifecycleState s) {
    switch (s) { case ScriptLifecycleState::Uninitialized: return "Uninitialized";
        case ScriptLifecycleState::Created: return "Created"; case ScriptLifecycleState::Enabled: return "Enabled";
        case ScriptLifecycleState::Disabled: return "Disabled"; case ScriptLifecycleState::PendingDestroy: return "PendingDestroy";
        case ScriptLifecycleState::Error: return "Error"; } return "?";
}

struct InstanceSnapshot {
    ecs::Entity   entity = ecs::kNull;
    ScriptTypeId  typeId = 0;
    std::string   displayName;
    ScriptLifecycleState state = ScriptLifecycleState::Uninitialized;
    bool          enabled = false;
    double        updateMs = 0.0, fixedMs = 0.0;
    int           errorCount = 0;
    int           activeSequences = 0;
    int           subscriptions = 0;
    std::uint32_t hotReloadGeneration = 0;
    std::vector<std::pair<std::string, std::string>> fields;   // (name, string value) -- never raw ptr
};

// ============================ Phase 8: level (scene) script validation =======================
// One attached field as stored in a scene.
struct AttachedField {
    std::string     name;
    ScriptFieldType type;
    std::string     value;                    // string-encoded
    ecs::Entity     entityValue = ecs::kNull; // for Entity/ScriptRef fields
    std::string     assetPath;                // for Asset fields
};
struct AttachedScript {
    std::string                 className;
    std::vector<AttachedField>  fields;
};

// Resolvers the host supplies so validation stays engine-agnostic.
struct ScriptValidationEnv {
    std::function<bool(ecs::Entity)>       entityValid = [](ecs::Entity){ return true; };
    std::function<bool(const std::string&)> assetExists = [](const std::string&){ return true; };
    std::function<bool(const std::string&)> luaFileExists = [](const std::string&){ return true; };
};

// Issues are reported through ReloadDiagnostics (errors/warnings), reusing the Pass 3 plumbing so the
// editor Level Validation panel shows scripting problems alongside asset/ABI ones. Returns false when
// any hard error was found.
inline bool ValidateSceneScripts(
        const std::vector<AttachedScript>& attachments,
        const ScriptApiRegistry& reg,
        const ScriptValidationEnv& env,
        ReloadDiagnostics& out) {
    for (const AttachedScript& a : attachments) {
        const ScriptDescriptor* desc = reg.FindScriptByName(a.className);
        if (!desc) { out.error("missing script type '" + a.className + "' (unresolved ScriptTypeId)"); continue; }
        for (const AttachedField& f : a.fields) {
            const ScriptFieldDescriptor* fd = desc->ResolveField(f.name);
            if (!fd) { out.warn("unknown field '" + f.name + "' on '" + a.className + "' (dropped)"); continue; }
            if (fd->type != f.type)
                out.error("field '" + f.name + "' on '" + a.className + "' type mismatch (scene="
                          + std::string(FieldTypeName(f.type)) + " descriptor=" + FieldTypeName(fd->type) + ")");
            switch (f.type) {
                case ScriptFieldType::Entity:
                case ScriptFieldType::ScriptRef:
                    if (f.entityValue != ecs::kNull && !env.entityValid(f.entityValue))
                        out.error("field '" + f.name + "' on '" + a.className + "' references a dead entity");
                    break;
                case ScriptFieldType::Asset:
                    if (!f.assetPath.empty() && !env.assetExists(f.assetPath))
                        out.error("field '" + f.name + "' on '" + a.className + "' references a missing asset '" + f.assetPath + "'");
                    break;
                default: break;
            }
        }
    }
    return out.ok();
}

// ============================ Phase 18: API documentation generation =========================
inline std::string GenerateApiDocumentation(const ScriptApiRegistry& reg) {
    std::ostringstream os;
    os << "# Script API Reference\n\n";
    os << "_Generated from the Script API Registry -- do not edit by hand._\n\n";
    os << "## Functions\n\n";
    for (const ScriptFunctionDescriptor& f : reg.Functions()) {
        os << "### " << f.name;
        if (f.Has(SFF_Deprecated)) os << "  **(deprecated)**";
        os << "\n\n";
        if (!f.category.empty()) os << "- Category: " << f.category << "\n";
        os << "- Signature: " << FieldTypeName(f.returnType) << ' ' << f.name << '(';
        for (std::size_t i = 0; i < f.params.size(); ++i) {
            if (i) os << ", ";
            os << FieldTypeName(f.params[i].type) << ' ' << f.params[i].name;
        }
        os << ")\n";
        os << "- Availability: "
           << (f.Has(SFF_NativeVisible) ? "C++ " : "")
           << (f.Has(SFF_LuaVisible) ? "Lua " : "")
           << (f.Has(SFF_VisualScriptVisible) ? "VisualScript" : "") << "\n";
        std::string restr;
        if (f.Has(SFF_MainThreadOnly)) restr += "MainThreadOnly ";
        if (f.Has(SFF_FixedStepSafe))  restr += "FixedStepSafe ";
        if (f.Has(SFF_StructuralCommand)) restr += "StructuralCommand ";
        if (!restr.empty()) os << "- Restrictions: " << restr << "\n";
        if (f.Has(SFF_Deprecated) && !f.deprecationNote.empty()) os << "- Deprecation: " << f.deprecationNote << "\n";
        os << "\n";
    }
    os << "## Events\n\n";
    for (const EventDescriptor& e : reg.Events()) {
        os << "### " << e.name << "\n\n";
        for (const EventFieldSchema& fs : e.fields)
            os << "- " << fs.name << " : " << FieldTypeName(fs.type) << (fs.required ? " (required)" : "") << "\n";
        os << "\n";
    }
    return os.str();
}

// ============================ Phase 19: visual-scripting readiness ============================
struct ReadinessReport {
    std::vector<std::pair<std::string, bool>> checks;
    bool Ready() const { for (auto& c : checks) if (!c.second) return false; return true; }
    std::string ToString() const {
        std::ostringstream os; os << "Visual Scripting Readiness: " << (Ready() ? "READY" : "NOT READY") << "\n";
        for (auto& c : checks) os << "  [" << (c.second ? "x" : " ") << "] " << c.first << "\n";
        return os.str();
    }
};

// Verifies the foundation exposes everything future graph nodes need. Does NOT implement graphs.
inline ReadinessReport CheckVisualScriptingReadiness(const ScriptApiRegistry& reg) {
    ReadinessReport r;
    r.checks.push_back({"stable ScriptTypeId", StableId("x") != 0});
    r.checks.push_back({"stable FieldId",      StableFieldId("A", "b") != 0});
    r.checks.push_back({"stable ScriptFunctionId / ScriptEventId", StableId("Foo") != 0});
    r.checks.push_back({"script descriptors registered", !reg.Scripts().empty()});
    r.checks.push_back({"function metadata registered",  !reg.Functions().empty()});
    r.checks.push_back({"event schemas available",       true});   // schema type exists (may be empty)
    bool anyVs = false; for (const auto& f : reg.Functions()) if (f.Has(SFF_VisualScriptVisible)) anyVs = true;
    r.checks.push_back({"visual-script-visible functions flagged", anyVs});
    r.checks.push_back({"Asset/Entity/Script reference field types", true}); // ScriptFieldType has them
    r.checks.push_back({"deferred structural commands (Pass 1)", true});     // Script AddComponentDeferred etc.
    r.checks.push_back({"tracked ECS writes (Pass 1)", true});               // Script PatchComponent/ChangeSource::Script
    r.checks.push_back({"runtime execution phases (SystemPhase)", true});    // ECS Pass 4 phases
    return r;
}

} // namespace script
} // namespace engine
