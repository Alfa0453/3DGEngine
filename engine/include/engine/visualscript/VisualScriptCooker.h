#pragma once

// =============================================================================
// Visual Scripting — Pass 5 : cooker validation, dependencies, docs, hashing.
//
//   * CookValidate  — the packaging gate: fails clearly for invalid graphs,
//                     missing node types / properties, and (optionally) missing
//                     assets (Phase 20/28).
//   * CollectAssetDependencies — graph -> assets, for the Asset Dependency
//                     Viewer and the cooker (Phase 19).
//   * GraphContentHash — derived-data invalidation key: changes when the graph
//                     or the node-registry fingerprint changes (Phase 3/4).
//   * GenerateNodeDocsMarkdown — node/API reference generated from the LIVE
//                     registry, never a hand-maintained second list (Phase 29).
//
// Header-only, GL/ImGui-free — usable from the editor project command and a
// command-line cooker alike.
// =============================================================================

#include "VisualNodeRegistry.h"
#include "VisualScriptAsset.h"
#include "VisualScriptValidator.h"

#include <engine/reflect/Reflection.h>

#include <cstdint>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace engine::vs {

struct CookResult {
    bool ok = true;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    void Error(const std::string& m) { ok = false; errors.push_back(m); }
    void Warn(const std::string& m) { warnings.push_back(m); }
};

namespace detail {
// Parse "Prop.Get.<cid>.<pid>" / "Prop.Set.<cid>.<pid>" -> (cid,pid). Returns false if not a prop node.
inline bool ParsePropNode(const std::string& typeId, std::uint32_t* cid, std::uint32_t* pid) {
    const bool get = typeId.rfind("Prop.Get.", 0) == 0;
    const bool set = typeId.rfind("Prop.Set.", 0) == 0;
    if (!get && !set) return false;
    const std::size_t dot = typeId.find('.', 9);
    if (dot == std::string::npos) return false;
    try {
        *cid = static_cast<std::uint32_t>(std::stoul(typeId.substr(9, dot - 9)));
        *pid = static_cast<std::uint32_t>(std::stoul(typeId.substr(dot + 1)));
    } catch (...) { return false; }
    return true;
}
} // namespace detail

// Optional callback so the cooker can confirm referenced assets resolve (Phase 20). Return true if
// the handle exists in the project asset registry.
using AssetExistsFn = std::function<bool(const AssetHandle&)>;

inline std::vector<AssetHandle> CollectAssetDependencies(const VisualScriptAsset& asset);   // fwd

inline CookResult CookValidate(const VisualScriptAsset& asset, const AssetExistsFn& assetExists = {}) {
    CookResult result;

    // 1) structural validation (duplicate ids, dangling links, type mismatch, event schema...).
    const ValidationReport report = ValidateGraph(asset);
    for (const ValidationIssue& i : report.issues) {
        if (i.severity == ValidationIssue::Severity::Error) result.Error(i.message);
        else result.Warn(i.message);
    }

    const VisualNodeRegistry& reg = VisualNodeRegistry::Instance();
    for (const VisualNode& node : asset.nodes) {
        const NodeDescriptor* desc = reg.Find(node.typeId);
        if (!desc) { result.Error("Missing node type '" + node.typeId + "' (node " + std::to_string(node.id) + ")"); continue; }

        // 2) reflected property node -> the component + property must still exist (Phase 28).
        std::uint32_t cid = 0, pid = 0;
        if (detail::ParsePropNode(node.typeId, &cid, &pid)) {
            const auto* cd = reflect::TypeRegistry::Get().Find(static_cast<reflect::ComponentTypeId>(cid));
            if (!cd) result.Error("Property node references unknown component id " + std::to_string(cid));
            else if (!cd->FindProperty(static_cast<reflect::PropertyId>(pid)))
                result.Error("Property node references unknown property " + std::to_string(pid) + " on " + cd->name);
        }
    }

    // 3) referenced assets resolve (Phase 20), when the caller supplied a checker.
    if (assetExists)
        for (const AssetHandle& dep : CollectAssetDependencies(asset))
            if (dep.Valid() && !assetExists(dep))
                result.Error("Missing referenced asset " + dep.ToString());

    return result;
}

// Phase 19: every AssetHandle referenced by the graph (node properties + pin defaults).
inline std::vector<AssetHandle> CollectAssetDependencies(const VisualScriptAsset& asset) {
    std::vector<AssetHandle> deps;
    auto add = [&](const VisualValue& v) {
        if (v.type == ValueType::Asset && v.AsAsset().Valid()) {
            const AssetHandle h = v.AsAsset();
            for (const AssetHandle& e : deps) if (e == h) return;
            deps.push_back(h);
        }
    };
    for (const VisualNode& n : asset.nodes) {
        for (const auto& kv : n.properties) add(kv.second);
        for (const VisualPin& p : n.inputs) add(p.defaultValue);
        for (const VisualPin& p : n.outputs) add(p.defaultValue);
    }
    for (const VisualVariable& v : asset.variables) add(v.defaultValue);
    return deps;
}

// Fingerprint of the installed node set (a proxy for "node registry version"): changes when node
// types are added/removed, invalidating derived data that assumed the old set (Phase 4).
inline std::uint64_t NodeRegistryFingerprint() {
    std::uint64_t h = 1469598103934665603ull;
    for (const std::string& id : VisualNodeRegistry::Instance().TypeIds())
        for (char c : id) { h ^= static_cast<std::uint8_t>(c); h *= 1099511628211ull; }
    return h ? h : 1ull;
}

// Content hash for the derived/compiled cache (Phase 3/4). Combines the authored graph with the node
// registry fingerprint so either changing invalidates the cache.
inline std::uint64_t GraphContentHash(const VisualScriptAsset& asset) {
    std::ostringstream os;
    os << asset.version << '|' << asset.graphId;
    for (const VisualNode& n : asset.nodes) {
        os << "|N" << n.id << ':' << n.typeId;
        for (const VisualPin& p : n.inputs)  os << ",i" << p.id << '=' << static_cast<int>(p.type);
        for (const VisualPin& p : n.outputs) os << ",o" << p.id << '=' << static_cast<int>(p.type);
        for (const auto& kv : n.properties)  os << ",p" << kv.first << '=' << static_cast<int>(kv.second.type);
    }
    for (const VisualLink& l : asset.links)
        os << "|L" << l.id << ':' << l.fromNode << '.' << l.fromPin << ">" << l.toNode << '.' << l.toPin;
    for (const VisualVariable& v : asset.variables)
        os << "|V" << v.id << ':' << static_cast<int>(v.type);
    const std::string s = os.str();
    std::uint64_t h = 1469598103934665603ull;
    for (char c : s) { h ^= static_cast<std::uint8_t>(c); h *= 1099511628211ull; }
    h ^= NodeRegistryFingerprint();
    return h ? h : 1ull;
}

// Phase 29: generate a node/API reference straight from the live registry.
inline std::string GenerateNodeDocsMarkdown() {
    const VisualNodeRegistry& reg = VisualNodeRegistry::Instance();
    std::ostringstream md;
    md << "# Visual Script Node Reference\n\n";
    std::string currentCategory;
    for (const std::string& typeId : reg.TypeIds()) {
        const NodeDescriptor* d = reg.Find(typeId);
        if (!d) continue;
        if (d->category != currentCategory) { currentCategory = d->category; md << "\n## " << currentCategory << "\n\n"; }
        md << "### " << d->displayName << "  `" << d->typeId << "`\n";
        md << "- Kind: " << (d->pure ? "pure (data)" : d->IsEvent() ? "event" : "impure (exec)") << "\n";
        auto emitPins = [&](const char* label, PinDirection dir) {
            bool any = false;
            for (const NodePinDesc& p : d->pins) {
                if (p.direction != dir) continue;
                if (!any) { md << "- " << label << ": "; any = true; } else md << ", ";
                md << p.name << " (" << (p.kind == PinKind::Exec ? "Exec" : ValueTypeName(p.type)) << ")";
            }
            if (any) md << "\n";
        };
        emitPins("Inputs", PinDirection::Input);
        emitPins("Outputs", PinDirection::Output);
        if (!d->eventName.empty()) md << "- Event: `" << d->eventName << "`\n";
        md << "\n";
    }
    return md.str();
}

} // namespace engine::vs
