#pragma once

// Scripting Pass 3 -- transactional hot reload: ABI handshake, compatibility validation, schema-driven
// state migration, compile-diagnostic parsing, and script manifest.
//
// ADDITIVE. The existing ScriptModule ABI (Get3DGScriptApiVersion == kScriptModuleApiVersion +
// RegisterScriptModule export, checked in ScriptModule::Load) is KEPT. This adds a richer, optional
// module info block a candidate can export, the pure validation/migration logic the transactional
// reload needs, and diagnostics -- all as free functions/structs so the editor's DLL orchestration
// (load marker, quarantine, known-good retention) can consume them without changing its safety paths.
// The known-good module stays active until ValidateModuleAbi + ValidateRequiredFactories +
// ValidateDescriptors all pass on the separately-loaded candidate.
//
// Header-only, GL-free, platform-independent (no LoadLibrary here -- that stays in ScriptModule.cpp).

#include "engine/gameplay/ScriptModule.h"       // kScriptModuleApiVersion
#include "engine/gameplay/ScriptReflection.h"   // ScriptApiRegistry, descriptors, stable IDs

#include <cstdint>
#include <string>
#include <vector>
#include <sstream>
#include <unordered_set>
#include <unordered_map>

namespace engine {
namespace script {

// Bumped when the reflection descriptor schema changes shape (not when scripts change).
inline constexpr std::uint32_t kReflectionSchemaVersion = 1;

// ---- Phase 2: module info exported across the stable C ABI boundary --------------------------
// A candidate DLL optionally exports:  extern "C" ScriptModuleInfo Get3DGScriptModuleInfo();
// (The legacy Get3DGScriptApiVersion() export is retained for backward compatibility.)
struct ScriptModuleInfo {
    std::uint32_t abiVersion             = kScriptModuleApiVersion;   // must equal host
    std::uint32_t scriptApiVersion       = kScriptModuleApiVersion;   // Script API surface version
    std::uint32_t reflectionSchemaVersion = kReflectionSchemaVersion; // descriptor schema shape
    std::uint64_t engineBuildId          = 0;   // host engine build stamp the module compiled against
    std::uint64_t moduleBuildId          = 0;   // this candidate's own build stamp (informational)
};
using GetScriptModuleInfoFn = ScriptModuleInfo (*)();

// What the running host requires of a candidate. engineBuildId is checked only in strict mode
// (packaged builds); during iterative editor reload a build-id skew is normal and tolerated.
struct ScriptModuleAbiExpectation {
    std::uint32_t abiVersion             = kScriptModuleApiVersion;
    std::uint32_t scriptApiVersion       = kScriptModuleApiVersion;
    std::uint32_t reflectionSchemaVersion = kReflectionSchemaVersion;
    std::uint64_t engineBuildId          = 0;
    bool          requireExactEngineBuild = false;
    bool          hasRegisterExport       = true;   // set from GetProcAddress result by the caller
};

struct ReloadDiagnostics {
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    bool ok() const { return errors.empty(); }
    void error(std::string m)   { errors.push_back(std::move(m)); }
    void warn(std::string m)    { warnings.push_back(std::move(m)); }
};

// ---- Phase 3: ABI validation (reject an incompatible candidate BEFORE instantiating) ---------
inline bool ValidateModuleAbi(const ScriptModuleAbiExpectation& want,
                              const ScriptModuleInfo& got, ReloadDiagnostics& diag) {
    if (!want.hasRegisterExport)
        diag.error("candidate module has no RegisterScriptModule export");
    if (got.abiVersion != want.abiVersion)
        diag.error("ABI version mismatch: module=" + std::to_string(got.abiVersion)
                   + " host=" + std::to_string(want.abiVersion));
    if (got.scriptApiVersion != want.scriptApiVersion)
        diag.error("Script API version mismatch: module=" + std::to_string(got.scriptApiVersion)
                   + " host=" + std::to_string(want.scriptApiVersion));
    if (got.reflectionSchemaVersion > want.reflectionSchemaVersion)
        diag.error("reflection schema newer than host (module=" + std::to_string(got.reflectionSchemaVersion)
                   + " host=" + std::to_string(want.reflectionSchemaVersion) + "); rebuild the editor");
    else if (got.reflectionSchemaVersion < want.reflectionSchemaVersion)
        diag.warn("module built against an older reflection schema; descriptors will be up-migrated");
    if (want.requireExactEngineBuild && got.engineBuildId != want.engineBuildId)
        diag.error("engine build id mismatch (module=" + std::to_string(got.engineBuildId)
                   + " host=" + std::to_string(want.engineBuildId) + ")");
    return diag.ok();
}

// ---- Phase 5: factory validation (every attached script type must resolve, alias-aware) ------
// `attachedClassNames` = the class names attached in the scene (scenes store the name). A candidate
// resolves a name directly, or via a descriptor alias (a safe rename lists the old name as an alias),
// so a renamed class does not fail the reload. A truly missing class yields an actionable diagnostic.
inline bool ValidateRequiredFactories(const std::vector<std::string>& attachedClassNames,
                                      const ScriptApiRegistry& candidate,
                                      ReloadDiagnostics& diag) {
    for (const std::string& name : attachedClassNames) {
        if (candidate.FindScriptByName(name) == nullptr)
            diag.error("attached script class '" + name
                       + "' has no factory in the candidate module (renamed without an alias, or removed)");
    }
    return diag.ok();
}

// ---- Phase 6: descriptor validation (duplicate/invalid metadata rejects the candidate) -------
inline bool ValidateDescriptors(const ScriptApiRegistry& reg, ReloadDiagnostics& diag) {
    std::unordered_set<ScriptTypeId> types;
    std::unordered_set<ScriptFieldId> fields;
    for (const ScriptDescriptor& d : reg.Scripts()) {
        if (d.id == 0) diag.error("script '" + d.className + "' has an unset ScriptTypeId");
        if (!types.insert(d.id).second)
            diag.error("duplicate ScriptTypeId for '" + d.className + "' (hash collision or double-register)");
        for (const ScriptFieldDescriptor& f : d.fields) {
            if (f.id == 0) diag.error("field '" + f.name + "' on '" + d.className + "' has an unset FieldId");
            if (!fields.insert(f.id).second)
                diag.error("duplicate FieldId '" + f.name + "' on '" + d.className + "'");
        }
    }
    std::unordered_set<ScriptFunctionId> fns;
    for (const ScriptFunctionDescriptor& f : reg.Functions())
        if (!fns.insert(f.id).second)
            diag.error("duplicate FunctionId for '" + f.name + "'");
    return diag.ok();
}

// ---- Phase 7/8/9: schema-driven state transfer by FieldId (+ safe conversion + alias) ---------
// State is the on-disk/string form (matching ScriptField::value). We migrate the OLD field map onto
// the NEW descriptor set: resolve by name or alias, keep the value when the type is unchanged, apply
// an explicitly-safe conversion when allowed (int->float), otherwise drop to the field default and
// log it. Custom OnSaveState/OnLoadState hooks are UNTOUCHED and still run for advanced data.
struct FieldValue { std::string value; ScriptFieldType type; };
struct StateMigrationLog {
    struct Entry { std::string field; ScriptFieldType from, to; std::string result; std::string note; };
    std::vector<Entry> entries;
};

inline bool SafeConvertFieldValue(const FieldValue& in, ScriptFieldType to, std::string& out) {
    if (in.type == to) { out = in.value; return true; }
    // int -> float (widening), and numeric string passthrough.
    if (in.type == ScriptFieldType::Int && to == ScriptFieldType::Float) { out = in.value; return true; }
    // float -> int would lose data: not silent; caller falls back.
    // Asset<->String and Tag<->String are representationally identical strings.
    const auto isStringy = [](ScriptFieldType t) {
        return t == ScriptFieldType::String || t == ScriptFieldType::Asset || t == ScriptFieldType::Tag;
    };
    if (isStringy(in.type) && isStringy(to)) { out = in.value; return true; }
    return false;   // not an explicitly-safe conversion
}

inline std::unordered_map<ScriptFieldId, std::string> MigrateFieldState(
        const ScriptDescriptor& newDesc,
        const std::unordered_map<std::string, FieldValue>& oldByName,
        StateMigrationLog* log = nullptr) {
    std::unordered_map<ScriptFieldId, std::string> migrated;
    for (const ScriptFieldDescriptor& f : newDesc.fields) {
        // find the old value under the current name or any alias (Phase 9)
        const FieldValue* src = nullptr; std::string matchedName;
        auto it = oldByName.find(f.name);
        if (it != oldByName.end()) { src = &it->second; matchedName = f.name; }
        else for (const std::string& a : f.aliases) {
            auto ai = oldByName.find(a);
            if (ai != oldByName.end()) { src = &ai->second; matchedName = a; break; }
        }
        if (!src) { migrated[f.id] = f.defaultValue; continue; }   // new field: default

        std::string result;
        if (SafeConvertFieldValue(*src, f.type, result)) {
            migrated[f.id] = result;
            if (log) log->entries.push_back({f.name, src->type, f.type, result,
                matchedName == f.name ? "kept" : ("migrated from alias '" + matchedName + "'")});
        } else {
            migrated[f.id] = f.defaultValue;                       // incompatible -> default fallback
            if (log) log->entries.push_back({f.name, src->type, f.type, f.defaultValue,
                "incompatible conversion; fell back to default"});
        }
    }
    return migrated;
}

// ---- Phase 14: compile-diagnostic parsing (MSVC first) ---------------------------------------
// MSVC:  path\file.cpp(line,col): error C2065: 'x': undeclared identifier
//        path\file.cpp(line): warning C4100: 'y': unreferenced formal parameter
// GCC/Clang:  path/file.cpp:line:col: error: message
struct CompileDiagnostic {
    std::string file;
    int         line = 0;
    int         column = 0;
    bool        isError = true;   // false == warning
    std::string code;             // e.g. "C2065" (empty if none)
    std::string message;
};

inline std::vector<CompileDiagnostic> ParseCompilerDiagnostics(const std::string& output) {
    std::vector<CompileDiagnostic> out;
    std::istringstream in(output);
    std::string ln;
    while (std::getline(in, ln)) {
        // strip trailing CR (Windows output)
        if (!ln.empty() && ln.back() == '\r') ln.pop_back();
        CompileDiagnostic d;
        const std::size_t paren = ln.find('(');
        const std::size_t colon = ln.find(':');
        if (paren != std::string::npos && ln.find(')') != std::string::npos && ln.find(')') > paren) {
            // MSVC form
            const std::size_t close = ln.find(')');
            d.file = ln.substr(0, paren);
            const std::string loc = ln.substr(paren + 1, close - paren - 1);   // "line" or "line,col"
            const std::size_t comma = loc.find(',');
            d.line = std::atoi(loc.substr(0, comma).c_str());
            if (comma != std::string::npos) d.column = std::atoi(loc.substr(comma + 1).c_str());
            std::string rest = ln.substr(close + 1);   // ": error C2065: msg"
            const std::size_t e = rest.find("error");
            const std::size_t w = rest.find("warning");
            if (e == std::string::npos && w == std::string::npos) continue;   // not a diagnostic line
            d.isError = (e != std::string::npos && (w == std::string::npos || e < w));
            std::size_t sev = d.isError ? e : w;
            std::string after = rest.substr(sev + (d.isError ? 5 : 7));   // after "error"/"warning"
            // optional code
            std::istringstream as(after);
            std::string tok; as >> tok;
            if (!tok.empty() && tok.back() == ':') tok.pop_back();
            if (!tok.empty() && (tok[0] == 'C' || tok[0] == 'D' || tok[0] == 'L')) {
                d.code = tok;
                const std::size_t m = after.find(tok);
                std::string msg = after.substr(m + tok.size());
                while (!msg.empty() && (msg.front() == ':' || msg.front() == ' ')) msg.erase(msg.begin());
                d.message = msg;
            } else {
                std::string msg = after;
                while (!msg.empty() && (msg.front() == ':' || msg.front() == ' ')) msg.erase(msg.begin());
                d.message = msg;
            }
            out.push_back(std::move(d));
        } else if (colon != std::string::npos) {
            // GCC/Clang form file:line:col: error: msg
            std::size_t c1 = ln.find(':');
            std::size_t c2 = ln.find(':', c1 + 1);
            if (c2 == std::string::npos) continue;
            std::size_t c3 = ln.find(':', c2 + 1);
            d.file = ln.substr(0, c1);
            d.line = std::atoi(ln.substr(c1 + 1, c2 - c1 - 1).c_str());
            if (c3 != std::string::npos) {
                d.column = std::atoi(ln.substr(c2 + 1, c3 - c2 - 1).c_str());
                std::string rest = ln.substr(c3 + 1);
                d.isError = rest.find("error") != std::string::npos;
                if (!d.isError && rest.find("warning") == std::string::npos) continue;
                std::size_t sev = rest.find(d.isError ? "error" : "warning");
                std::string msg = rest.substr(sev + (d.isError ? 5 : 7));
                while (!msg.empty() && (msg.front() == ':' || msg.front() == ' ')) msg.erase(msg.begin());
                d.message = msg;
                out.push_back(std::move(d));
            }
        }
    }
    return out;
}

// ---- Phase 18/19: script manifest (Player + packaging validation) ----------------------------
struct ScriptManifestEntry {
    ScriptTypeId  id = 0;
    std::string   displayName;
    std::string   module;             // e.g. "game_scripts"
    std::uint32_t schemaVersion = kReflectionSchemaVersion;
    std::uint32_t requiredApiVersion = kScriptModuleApiVersion;
};

inline std::vector<ScriptManifestEntry> BuildScriptManifest(const ScriptApiRegistry& reg,
                                                            const std::string& module) {
    std::vector<ScriptManifestEntry> entries;
    for (const ScriptDescriptor& d : reg.Scripts())
        entries.push_back({d.id, d.displayName.empty() ? d.className : d.displayName,
                           module, kReflectionSchemaVersion, kScriptModuleApiVersion});
    return entries;
}

// Packaging/Player check: every script type the scene needs must be present in the manifest.
inline bool ValidateManifestHasTypes(const std::vector<ScriptManifestEntry>& manifest,
                                     const std::vector<ScriptTypeId>& required,
                                     ReloadDiagnostics& diag) {
    std::unordered_set<ScriptTypeId> have;
    for (const auto& e : manifest) have.insert(e.id);
    for (ScriptTypeId id : required)
        if (!have.count(id))
            diag.error("manifest is missing required script type id " + std::to_string(id));
    return diag.ok();
}

} // namespace script
} // namespace engine
