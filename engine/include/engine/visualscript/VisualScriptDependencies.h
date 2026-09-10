#pragma once

// =============================================================================
// Visual Scripting — Milestone 11 : asset lifecycle + migration hardening.
//
// References between graphs and other assets are by STABLE AssetHandle id, never
// by path — so renaming or moving a .3dgvs (or any asset) never breaks a
// reference. This header adds the tooling that makes lifecycle operations safe:
//   * dependency extraction (asset refs in pin/variable/property values, incl.
//     nested array/struct/map values, plus cross-graph function calls)
//   * a project index with REVERSE dependents, so a delete can report every
//     graph that references the asset before removing it
//   * stale-override pruning (overrides whose variable was removed / re-typed /
//     un-exposed) with a report
//   * dangling-link repair and a project-wide validation command
//   * a missing-node report (types not currently registered; their pins and
//     authored data are always retained by serialization until the type returns)
//
// Header-only and editor-free (uses only std::filesystem for the project scan).
// =============================================================================

#include "VisualNodeRegistry.h"
#include "VisualScriptAsset.h"
#include "VisualScriptComponent.h"
#include "VisualScriptTypes.h"
#include "VisualScriptValidator.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::vs {

// ---- dependency extraction -------------------------------------------------
struct VsDependency {
    AssetHandle asset;
    std::string reason;   // human-readable ("pin default", "Call target", "variable default", ...)
    NodeId      node = kInvalidNodeId;
};

// Collect every AssetHandle reachable from a value (handles nested arrays/structs/maps).
inline void CollectAssetHandles(const VisualValue& v, std::vector<AssetHandle>* out) {
    switch (v.type) {
        case ValueType::Asset: { const AssetHandle h = v.AsAsset(); if (h.Valid()) out->push_back(h); break; }
        case ValueType::Array:
        case ValueType::Struct: for (const VisualValue& e : v.Elements()) CollectAssetHandles(e, out); break;
        case ValueType::Map:    for (const VisualValue& e : v.MapValuesConst()) CollectAssetHandles(e, out); break;
        default: break;
    }
}

inline std::vector<VsDependency> CollectDependencies(const VisualScriptAsset& asset) {
    std::vector<VsDependency> deps;
    auto addValue = [&](const VisualValue& val, const char* reason, NodeId node) {
        std::vector<AssetHandle> handles;
        CollectAssetHandles(val, &handles);
        for (const AssetHandle& h : handles) deps.push_back({h, reason, node});
    };
    for (const VisualNode& n : asset.nodes) {
        for (const VisualPin& p : n.inputs)  addValue(p.defaultValue, "pin default", n.id);
        for (const VisualPin& p : n.outputs) addValue(p.defaultValue, "pin default", n.id);
        for (const auto& kv : n.properties) {
            if (n.typeId == "Function.Call" && kv.first == "graph") addValue(kv.second, "function-call target graph", n.id);
            else addValue(kv.second, "node property", n.id);
        }
    }
    for (const VisualVariable& v : asset.variables) addValue(v.defaultValue, "variable default", kInvalidNodeId);
    for (const VisualFunction& fn : asset.functions)
        for (const VisualVariable& l : fn.locals) addValue(l.defaultValue, "function-local default", kInvalidNodeId);
    // Deduplicate identical (asset,reason,node) rows.
    std::sort(deps.begin(), deps.end(), [](const VsDependency& a, const VsDependency& b) {
        if (!(a.asset == b.asset)) return a.asset.low < b.asset.low;
        if (a.node != b.node) return a.node < b.node;
        return a.reason < b.reason;
    });
    deps.erase(std::unique(deps.begin(), deps.end(), [](const VsDependency& a, const VsDependency& b) {
        return a.asset == b.asset && a.node == b.node && a.reason == b.reason;
    }), deps.end());
    return deps;
}

// A missing node is one whose type is not currently registered. Its pins/properties are preserved by
// serialization, so it comes back to life if the type is registered again.
inline std::vector<NodeId> CollectMissingNodes(const VisualScriptAsset& asset) {
    std::vector<NodeId> missing;
    const VisualNodeRegistry& reg = VisualNodeRegistry::Instance();
    for (const VisualNode& n : asset.nodes) if (!reg.Has(n.typeId)) missing.push_back(n.id);
    return missing;
}

// ---- project index (reverse dependents) ------------------------------------
struct VsProjectGraph {
    AssetHandle handle;
    std::string path;
    GraphId     graphId = kInvalidGraphId;
};
struct VsDependentRef {
    AssetHandle graph;       // the graph that depends on the queried asset
    std::string path;
    std::string reason;
    NodeId      node = kInvalidNodeId;
};

class VsProjectIndex {
public:
    // Scan a content root for .3dgvs, load each, and build handle->path + reverse dependents.
    void Build(const std::string& contentRoot) {
        m_graphs.clear(); m_dependents.clear(); m_loadErrors.clear();
        namespace fs = std::filesystem;
        std::error_code ec;
        for (fs::recursive_directory_iterator it(contentRoot, fs::directory_options::skip_permission_denied, ec), end;
             it != end; it.increment(ec)) {
            if (ec || !it->is_regular_file(ec)) continue;
            std::string ext = it->path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (ext != ".3dgvs") continue;
            VisualScriptAsset asset; std::string err;
            if (!asset.Load(it->path().string(), &err)) { m_loadErrors.push_back(it->path().string() + ": " + err); continue; }
            const std::string path = it->path().string();
            m_graphs.push_back({asset.id, path, asset.graphId});
            for (const VsDependency& d : CollectDependencies(asset))
                m_dependents[Key(d.asset)].push_back({asset.id, path, d.reason, d.node});
        }
    }
    const std::vector<VsProjectGraph>& Graphs() const { return m_graphs; }
    const std::vector<std::string>& LoadErrors() const { return m_loadErrors; }
    // Every graph that references `asset` (call this before deleting it).
    std::vector<VsDependentRef> DependentsOf(const AssetHandle& asset) const {
        auto it = m_dependents.find(Key(asset));
        return it == m_dependents.end() ? std::vector<VsDependentRef>{} : it->second;
    }

private:
    static std::uint64_t Key(const AssetHandle& h) { return h.high ^ (h.low * 1099511628211ull); }
    std::vector<VsProjectGraph> m_graphs;
    std::unordered_map<std::uint64_t, std::vector<VsDependentRef>> m_dependents;
    std::vector<std::string> m_loadErrors;
};

// ---- stale override cleanup ------------------------------------------------
struct VsOverrideCleanup {
    int removed = 0;
    std::vector<std::string> report;
};
// Remove per-object overrides whose variable was removed, un-exposed, or changed type (Milestone 11).
inline VsOverrideCleanup PruneStaleOverrides(const VisualScriptAsset& graph,
                                             std::vector<VisualScriptVariableOverride>* overrides) {
    VsOverrideCleanup result;
    if (!overrides) return result;
    overrides->erase(std::remove_if(overrides->begin(), overrides->end(),
        [&](const VisualScriptVariableOverride& o) {
            const VisualVariable* var = graph.FindVariable(o.variableId);
            const char* why = nullptr;
            if (!var) why = "variable removed";
            else if (!var->exposed) why = "variable no longer exposed";
            else if (var->type != o.value.type) why = "variable type changed";
            if (why) { result.report.push_back("override id " + std::to_string(o.variableId) + ": " + why); ++result.removed; return true; }
            return false;
        }), overrides->end());
    return result;
}

// ---- repair ----------------------------------------------------------------
struct VsRepairResult {
    int danglingLinksRemoved = 0;
    std::vector<std::string> notes;
};
// Remove links whose endpoint node/pin no longer exists (e.g. after a node/pin was deleted or a
// signature changed). Non-destructive to authored nodes and their data.
inline VsRepairResult RepairGraph(VisualScriptAsset* asset) {
    VsRepairResult result;
    if (!asset) return result;
    auto pinExists = [&](NodeId n, PinId p) {
        const VisualNode* node = asset->FindNode(n);
        return node && node->FindPin(p) != nullptr;
    };
    const std::size_t before = asset->links.size();
    asset->links.erase(std::remove_if(asset->links.begin(), asset->links.end(),
        [&](const VisualLink& l) { return !pinExists(l.fromNode, l.fromPin) || !pinExists(l.toNode, l.toPin); }),
        asset->links.end());
    result.danglingLinksRemoved = static_cast<int>(before - asset->links.size());
    if (result.danglingLinksRemoved > 0)
        result.notes.push_back("Removed " + std::to_string(result.danglingLinksRemoved) + " dangling link(s).");
    return result;
}

// ---- project-wide validation ----------------------------------------------
struct VsProjectValidation {
    struct Entry { std::string path; ValidationReport report; };
    std::vector<Entry> entries;
    int errorGraphs = 0;
    int warningGraphs = 0;
};
inline VsProjectValidation ValidateProject(const std::string& contentRoot) {
    VsProjectValidation out;
    namespace fs = std::filesystem;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(contentRoot, fs::directory_options::skip_permission_denied, ec), end;
         it != end; it.increment(ec)) {
        if (ec || !it->is_regular_file(ec)) continue;
        std::string ext = it->path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ext != ".3dgvs") continue;
        VisualScriptAsset asset; std::string err;
        ValidationReport report;
        if (!asset.Load(it->path().string(), &err)) report.Error("Load failed: " + err);
        else report = ValidateGraph(asset);
        bool hasErr = false, hasWarn = false;
        for (const ValidationIssue& i : report.issues) {
            if (i.severity == ValidationIssue::Severity::Error) hasErr = true; else hasWarn = true;
        }
        if (hasErr) ++out.errorGraphs; else if (hasWarn) ++out.warningGraphs;
        out.entries.push_back({it->path().string(), std::move(report)});
    }
    return out;
}

} // namespace engine::vs
