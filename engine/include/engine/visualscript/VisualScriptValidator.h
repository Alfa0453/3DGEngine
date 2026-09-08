#pragma once

// =============================================================================
// Visual Scripting — Pass 1 : graph validator (Phase 7 + Phase 24).
//
// Shared by the editor and the (future) cooker. Rejects malformed graphs at
// load/validation time so the runtime interpreter never has to defend against
// them. Returns structured issues rather than throwing or crashing.
// =============================================================================

#include "VisualNodeRegistry.h"
#include "VisualScriptAsset.h"
#include "VisualScriptTypes.h"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace engine::vs {

struct ValidationIssue {
    enum class Severity { Warning, Error };
    Severity    severity = Severity::Error;
    NodeId      nodeId = kInvalidNodeId;
    LinkId      linkId = kInvalidLinkId;
    std::string message;
};

struct ValidationReport {
    std::vector<ValidationIssue> issues;
    bool Ok() const {
        for (const auto& i : issues) if (i.severity == ValidationIssue::Severity::Error) return false;
        return true;
    }
    void Error(const std::string& m, NodeId n = kInvalidNodeId, LinkId l = kInvalidLinkId) {
        issues.push_back({ValidationIssue::Severity::Error, n, l, m});
    }
    void Warn(const std::string& m, NodeId n = kInvalidNodeId, LinkId l = kInvalidLinkId) {
        issues.push_back({ValidationIssue::Severity::Warning, n, l, m});
    }
};

// A single connection rule check (Phase 7), reusable by the editor before it
// commits a link. `from` must be an output pin, `to` an input pin.
inline bool ConnectionValid(const VisualPin& from, const VisualPin& to, std::string* why) {
    if (from.direction != PinDirection::Output || to.direction != PinDirection::Input) {
        if (why) *why = "A link must go from an output pin to an input pin.";
        return false;
    }
    if (from.kind != to.kind) {
        if (why) *why = "Exec pins can only connect to exec pins.";
        return false;
    }
    if (from.kind == PinKind::Exec) return true;   // exec<->exec always ok
    if (!DataTypesCompatible(from.type, to.type)) {
        if (why) *why = std::string("Incompatible data types: ")
            + ValueTypeName(from.type) + " -> " + ValueTypeName(to.type);
        return false;
    }
    return true;
}

inline ValidationReport ValidateGraph(const VisualScriptAsset& asset) {
    ValidationReport report;
    const VisualNodeRegistry& registry = VisualNodeRegistry::Instance();

    // --- duplicate node ids + missing node types + duplicate pin ids --------
    std::unordered_set<NodeId> nodeIds;
    for (const VisualNode& node : asset.nodes) {
        if (node.id == kInvalidNodeId) report.Error("Node has an invalid (zero) id.", node.id);
        if (!nodeIds.insert(node.id).second) report.Error("Duplicate node id.", node.id);
        if (!registry.Has(node.typeId)) report.Error("Missing node type: " + node.typeId, node.id);

        std::unordered_set<PinId> pinIds;
        auto checkPins = [&](const std::vector<VisualPin>& pins) {
            for (const VisualPin& p : pins) {
                if (p.id == kInvalidPinId) report.Error("Pin has an invalid (zero) id.", node.id);
                if (!pinIds.insert(p.id).second) report.Error("Duplicate pin id within node.", node.id);
            }
        };
        checkPins(node.inputs);
        checkPins(node.outputs);
    }

    // --- event nodes must be real event types (Phase 24) --------------------
    for (const VisualNode& node : asset.nodes) {
        if (const NodeDescriptor* d = registry.Find(node.typeId)) {
            if (d->IsEvent()) {
                for (const VisualPin& p : node.inputs)
                    if (p.kind == PinKind::Exec)
                        report.Error("Event node must not have an exec input.", node.id);
            }
        }
    }

    // --- links: duplicate ids, dangling endpoints, direction/type, multiplicity
    std::unordered_set<LinkId> linkIds;
    // multiplicity counters keyed by (node,pin)
    std::unordered_map<std::uint64_t, int> incomingData;   // into a data input
    std::unordered_map<std::uint64_t, int> outgoingExec;   // out of an exec output
    auto key = [](NodeId n, PinId p) { return (static_cast<std::uint64_t>(n) << 32) | p; };

    for (const VisualLink& link : asset.links) {
        if (link.id == kInvalidLinkId) report.Error("Link has an invalid (zero) id.", kInvalidNodeId, link.id);
        if (!linkIds.insert(link.id).second) report.Error("Duplicate link id.", kInvalidNodeId, link.id);

        const VisualNode* from = asset.FindNode(link.fromNode);
        const VisualNode* to = asset.FindNode(link.toNode);
        if (!from || !to) { report.Error("Dangling link: endpoint node missing.", kInvalidNodeId, link.id); continue; }
        const VisualPin* fromPin = from->FindPin(link.fromPin);
        const VisualPin* toPin = to->FindPin(link.toPin);
        if (!fromPin || !toPin) { report.Error("Dangling link: endpoint pin missing.", kInvalidNodeId, link.id); continue; }

        std::string why;
        if (!ConnectionValid(*fromPin, *toPin, &why)) { report.Error("Invalid link: " + why, kInvalidNodeId, link.id); continue; }

        if (toPin->kind == PinKind::Data)
            if (++incomingData[key(link.toNode, link.toPin)] > 1)
                report.Error("A data input pin can accept only one connection.", link.toNode, link.id);
        if (fromPin->kind == PinKind::Exec)
            if (++outgoingExec[key(link.fromNode, link.fromPin)] > 1)
                report.Error("An exec output pin can drive only one connection.", link.fromNode, link.id);
    }

    return report;
}

} // namespace engine::vs
