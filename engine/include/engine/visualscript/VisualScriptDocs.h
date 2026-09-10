#pragma once

// =============================================================================
// Visual Scripting — Milestone 12 : documentation + release hardening.
//
//   * GenerateNodeReference()  — a markdown reference of every registered node,
//     grouped by category, for the engine docs.
//   * CookForShipping()        — strip EDITOR-ONLY data (comments, editor node
//     positions) from a graph so packaged builds carry only runtime data.
//   * PackageAudit()           — per-graph report of validation, missing node
//     types, unresolved-descriptor (compiled-cache) status, and dependencies.
//   * VsBudgets / CheckBudgets — perf/memory budgets over the live profiler
//     counters (graph instances, active tasks, instructions per frame).
//
// Header-only and editor-free.
// =============================================================================

#include "VisualNodeRegistry.h"
#include "VisualScriptAsset.h"
#include "VisualScriptCompiler.h"
#include "VisualScriptDependencies.h"
#include "VisualScriptDiagnostics.h"
#include "VisualScriptValidator.h"

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace engine::vs {

// ---- generated node reference ----------------------------------------------
inline std::string GenerateNodeReference() {
    const VisualNodeRegistry& reg = VisualNodeRegistry::Instance();
    std::vector<std::string> ids = reg.TypeIds();   // sorted
    std::ostringstream out;
    out << "# 3DGEngine Visual Scripting — Node Reference\n\n";
    out << "_Generated from the live node registry (" << ids.size() << " nodes)._\n\n";
    std::string currentCategory;
    // ids are sorted by typeId; regroup by category for readability.
    std::vector<const NodeDescriptor*> descs;
    for (const std::string& id : ids) if (const NodeDescriptor* d = reg.Find(id)) descs.push_back(d);
    std::sort(descs.begin(), descs.end(), [](const NodeDescriptor* a, const NodeDescriptor* b) {
        if (a->category != b->category) return a->category < b->category;
        return a->displayName < b->displayName;
    });
    for (const NodeDescriptor* d : descs) {
        if (d->category != currentCategory) { currentCategory = d->category; out << "\n## " << currentCategory << "\n\n"; }
        out << "### " << d->displayName << "  `" << d->typeId << "`\n\n";
        out << (d->pure ? "_Pure (data-only, no side effects)._" : "_Impure (has exec pins)._") << "\n\n";
        auto emitPins = [&](PinDirection dir, const char* label) {
            bool any = false;
            for (const NodePinDesc& p : d->pins) if (p.direction == dir) {
                if (!any) { out << "- **" << label << ":** "; any = true; } else out << ", ";
                out << p.name << " (" << (p.kind == PinKind::Exec ? "exec" : ValueTypeName(p.type)) << ")";
            }
            if (any) out << "\n";
        };
        emitPins(PinDirection::Input, "Inputs");
        emitPins(PinDirection::Output, "Outputs");
        out << "\n";
    }
    return out.str();
}

// ---- cook for shipping (strip editor-only data) ----------------------------
struct VsCookResult {
    VisualScriptAsset cooked;
    std::size_t commentsStripped = 0;
    std::size_t positionsCleared = 0;
};
inline VsCookResult CookForShipping(const VisualScriptAsset& asset) {
    VsCookResult r;
    r.cooked = asset;
    r.commentsStripped = r.cooked.comments.size();
    r.cooked.comments.clear();                       // comments are editor-only (no runtime cost anyway)
    for (VisualNode& n : r.cooked.nodes) {
        if (n.editorPosition != glm::vec2(0.0f)) ++r.positionsCleared;
        n.editorPosition = glm::vec2(0.0f);          // layout is editor-only
    }
    for (VisualStateMachine& sm : r.cooked.stateMachines)
        for (VisualState& s : sm.states) s.editorPosition = glm::vec2(0.0f);
    return r;
}
// Verification that a shipping build has diagnostics OFF (no debugger/profiler capture cost).
inline bool ShippingDiagnosticsDisabled() { return !VisualScriptDiagnostics::Instance().enabled; }

// ---- packaging audit -------------------------------------------------------
struct VsPackageAuditEntry {
    std::string path;
    AssetHandle handle;
    int  nodes = 0;
    int  errors = 0;
    int  warnings = 0;
    int  missingNodeTypes = 0;      // unresolved descriptors -> would error at run
    int  dependencies = 0;
    bool compileClean = true;       // all node descriptors resolved
};
struct VsPackageAudit {
    std::vector<VsPackageAuditEntry> entries;
    int totalGraphs = 0;
    int graphsWithErrors = 0;
    std::string Summary() const {
        std::ostringstream o;
        o << "Packaging audit: " << totalGraphs << " graph(s), " << graphsWithErrors << " with errors.\n";
        for (const VsPackageAuditEntry& e : entries) {
            o << "  " << std::filesystem::path(e.path).filename().string()
              << "  nodes=" << e.nodes << " deps=" << e.dependencies
              << " errors=" << e.errors << " warnings=" << e.warnings;
            if (e.missingNodeTypes > 0) o << " MISSING-TYPES=" << e.missingNodeTypes;
            if (!e.compileClean) o << " [will not compile clean]";
            o << "\n";
        }
        return o.str();
    }
};
inline VsPackageAudit PackageAudit(const std::string& contentRoot) {
    VsPackageAudit audit;
    namespace fs = std::filesystem;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(contentRoot, fs::directory_options::skip_permission_denied, ec), end;
         it != end; it.increment(ec)) {
        if (ec || !it->is_regular_file(ec)) continue;
        std::string ext = it->path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ext != ".3dgvs") continue;
        VisualScriptAsset asset; std::string err;
        if (!asset.Load(it->path().string(), &err)) {
            audit.entries.push_back({it->path().string(), {}, 0, 1, 0, 0, 0, false});
            ++audit.totalGraphs; ++audit.graphsWithErrors;
            continue;
        }
        VsPackageAuditEntry e;
        e.path = it->path().string();
        e.handle = asset.id;
        e.nodes = static_cast<int>(asset.nodes.size());
        const ValidationReport rep = ValidateGraph(asset);
        for (const ValidationIssue& i : rep.issues) {
            if (i.severity == ValidationIssue::Severity::Error) ++e.errors; else ++e.warnings;
        }
        e.missingNodeTypes = static_cast<int>(CollectMissingNodes(asset).size());
        e.dependencies = static_cast<int>(CollectDependencies(asset).size());
        CompiledGraph cg; cg.Compile(&asset);
        e.compileClean = (cg.missingDescriptors == 0);
        ++audit.totalGraphs;
        if (e.errors > 0) ++audit.graphsWithErrors;
        audit.entries.push_back(std::move(e));
    }
    return audit;
}

// ---- perf / memory budgets -------------------------------------------------
struct VsBudgets {
    int           maxInstances = 512;
    int           maxActiveTasks = 256;
    std::uint64_t maxInstructionsPerFrame = 200000;
};
struct VsBudgetStatus {
    int           instances = 0;
    int           activeTasks = 0;
    std::uint64_t instructions = 0;
    bool          instancesOver = false;
    bool          tasksOver = false;
    bool          instructionsOver = false;
    bool AnyOver() const { return instancesOver || tasksOver || instructionsOver; }
};
// Reads the live profiler counters (only meaningful while diagnostics are enabled during Play).
inline VsBudgetStatus CheckBudgets(const VsBudgets& budgets) {
    VsBudgetStatus s;
    const VisualScriptDiagnostics& diag = VisualScriptDiagnostics::Instance();
    for (const auto& kv : diag.Graphs()) {
        s.instances += kv.second.instances;
        s.instructions += kv.second.nodesExecuted;
    }
    s.activeTasks = static_cast<int>(diag.Tasks().size());
    s.instancesOver = s.instances > budgets.maxInstances;
    s.tasksOver = s.activeTasks > budgets.maxActiveTasks;
    s.instructionsOver = s.instructions > budgets.maxInstructionsPerFrame;
    return s;
}

} // namespace engine::vs
