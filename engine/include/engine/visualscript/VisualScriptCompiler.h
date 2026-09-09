#pragma once

// =============================================================================
// Visual Scripting — Milestone 9 : compiled graph execution.
//
// The authored .3dgvs stays the source of truth; this derives a compact, cached
// acceleration structure (an IR) so the interpreter never re-scans the node or
// link vectors at runtime. It resolves, once per compile:
//   * NodeId  -> const VisualNode*        (no linear FindNode scan)
//   * NodeId  -> const NodeDescriptor*    (no per-node registry hash lookup)
//   * (node,pin) -> outgoing / incoming link  (no linear link scan)
//
// It preserves authored NodeIds, so errors, breakpoints, profiling and execution
// highlights still map back to the exact source node (Phase requirement).
//
// A content hash (over node/link/variable identity + the format version) keys the
// cache: unchanged graphs are not recompiled, and any authored change or a graph
// reload invalidates it. If compiled data is absent/stale the interpreter falls
// back to direct asset scans, so correctness never depends on the cache.
// =============================================================================

#include "VisualNodeRegistry.h"
#include "VisualScriptAsset.h"
#include "VisualScriptTypes.h"

#include <cstdint>
#include <unordered_map>

namespace engine::vs {

struct CompiledGraph {
    const VisualScriptAsset* source = nullptr;   // the exact asset these pointers index into
    std::uint64_t contentHash = 0;
    std::unordered_map<NodeId, const VisualNode*>       nodeById;
    std::unordered_map<NodeId, const NodeDescriptor*>   descById;   // resolved once at compile time
    std::unordered_map<std::uint64_t, const VisualLink*> linkFrom;  // key(fromNode,fromPin) -> link
    std::unordered_map<std::uint64_t, const VisualLink*> linkTo;    // key(toNode,toPin)   -> link
    std::size_t nodeCount = 0;
    std::size_t linkCount = 0;
    std::size_t missingDescriptors = 0;   // unresolved node types (graph will error at run)

    static std::uint64_t Key(NodeId n, PinId p) {
        return (static_cast<std::uint64_t>(n) << 32) | static_cast<std::uint64_t>(p);
    }

    // A stable hash over the parts that affect execution mapping. Mixing the format version means a
    // schema bump invalidates every cache; callers may also mix a registry epoch for reflected APIs.
    static std::uint64_t ContentHash(const VisualScriptAsset* asset) {
        std::uint64_t h = 1469598103934665603ull;   // FNV-1a offset basis
        auto mix = [&](std::uint64_t v) { h ^= v; h *= 1099511628211ull; };
        auto mixStr = [&](const std::string& s) { for (unsigned char c : s) mix(c); mix(0x1234u); };
        mix(kVisualScriptVersion);
        if (!asset) return h;
        for (const VisualNode& n : asset->nodes) {
            mix(n.id); mix(n.functionId); mixStr(n.typeId);
            for (const VisualPin& p : n.inputs)  { mix(p.id); mix(static_cast<std::uint64_t>(p.type)); }
            for (const VisualPin& p : n.outputs) { mix(p.id); mix(static_cast<std::uint64_t>(p.type)); }
        }
        for (const VisualLink& l : asset->links) { mix(l.fromNode); mix(l.fromPin); mix(l.toNode); mix(l.toPin); }
        for (const VisualVariable& v : asset->variables) { mix(v.id); mix(static_cast<std::uint64_t>(v.type)); }
        return h;
    }

    void Compile(const VisualScriptAsset* asset) {
        nodeById.clear(); descById.clear(); linkFrom.clear(); linkTo.clear();
        missingDescriptors = 0;
        source = asset;
        contentHash = ContentHash(asset);
        if (!asset) { nodeCount = linkCount = 0; return; }
        const VisualNodeRegistry& reg = VisualNodeRegistry::Instance();
        nodeById.reserve(asset->nodes.size());
        descById.reserve(asset->nodes.size());
        for (const VisualNode& n : asset->nodes) {
            nodeById[n.id] = &n;
            const NodeDescriptor* d = reg.Find(n.typeId);
            descById[n.id] = d;
            if (!d) ++missingDescriptors;
        }
        linkFrom.reserve(asset->links.size());
        linkTo.reserve(asset->links.size());
        for (const VisualLink& l : asset->links) {
            linkFrom[Key(l.fromNode, l.fromPin)] = &l;
            linkTo[Key(l.toNode, l.toPin)] = &l;
        }
        nodeCount = asset->nodes.size();
        linkCount = asset->links.size();
    }

    bool MatchesCurrent(const VisualScriptAsset* asset) const {
        return source == asset && contentHash == ContentHash(asset);
    }
};

// Global toggle so the editor can compare interpreted vs compiled execution timings (the profiler
// times the same executors either way; only the lookups differ). Default on.
inline bool& VisualScriptUseCompiledRef() { static bool useCompiled = true; return useCompiled; }

} // namespace engine::vs
