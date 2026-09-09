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
    // Container/struct identity must also match (Milestone 4). A 0 typeId is a wildcard (a generic
    // node pin whose concrete type the editor has not specialised yet).
    if (from.type == ValueType::Array || from.type == ValueType::Map) {
        if (from.elementType != to.elementType) {
            if (why) *why = std::string("Element type mismatch: ")
                + ValueTypeName(from.elementType) + " -> " + ValueTypeName(to.elementType);
            return false;
        }
    }
    if (from.type == ValueType::Map && from.keyType != to.keyType) {
        if (why) *why = std::string("Map key type mismatch: ")
            + ValueTypeName(from.keyType) + " -> " + ValueTypeName(to.keyType);
        return false;
    }
    if ((from.type == ValueType::Struct || from.type == ValueType::Enum)
        && from.typeId != 0 && to.typeId != 0 && from.typeId != to.typeId) {
        if (why) *why = "Struct/enum type mismatch.";
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

    // --- functions / subgraphs (Milestone 3) --------------------------------
    auto isLatent = [](const std::string& typeId) {
        return typeId == "Flow.Delay" || typeId == "Flow.WaitUntil" || typeId == "Flow.WaitForEvent"
            || typeId == "Flow.WaitForFixedSteps" || typeId == "Flow.WaitForAnimation"
            || typeId == "Flow.RetriggerableDelay" || typeId == "Time.SetTimer" || typeId == "Time.Timeline"
            || typeId == "Async.StartTask" || typeId == "Async.LoadLevel" || typeId == "Async.LoadScene"
            || typeId == "Async.StartDialogue";
    };
    // Each function needs exactly one Entry; latent nodes are not allowed inside a function body.
    for (const VisualFunction& fn : asset.functions) {
        int entryCount = 0, returnCount = 0;
        for (const VisualNode& node : asset.nodes) {
            if (node.functionId != fn.id) continue;
            if (node.typeId == "Function.Entry")  ++entryCount;
            if (node.typeId == "Function.Return") ++returnCount;
            if (isLatent(node.typeId))
                report.Error("Latent node '" + node.typeId + "' is not allowed inside function '"
                             + fn.name + "' (functions run synchronously).", node.id);
        }
        if (entryCount == 0) report.Error("Function '" + fn.name + "' has no Entry node.");
        if (entryCount > 1)  report.Error("Function '" + fn.name + "' has more than one Entry node.");
        if (!fn.outputs.empty() && returnCount == 0)
            report.Warn("Function '" + fn.name + "' declares outputs but has no Return node.");
    }
    // Call nodes: same-graph targets must exist; build the same-graph call graph for cycle detection.
    std::unordered_map<FunctionId, std::unordered_set<FunctionId>> callEdges;
    for (const VisualNode& node : asset.nodes) {
        if (node.typeId != "Function.Call") continue;
        AssetHandle targetGraph;
        if (auto g = node.properties.find("graph"); g != node.properties.end()) targetGraph = g->second.AsAsset();
        FunctionId target = kInvalidFunctionId;
        if (auto f = node.properties.find("func"); f != node.properties.end())
            target = static_cast<FunctionId>(f->second.AsInt());
        const bool crossGraph = targetGraph.Valid() && !(targetGraph == asset.id);
        if (target == kInvalidFunctionId) { report.Error("Call node has no target function.", node.id); continue; }
        if (!crossGraph) {   // same-graph: the function must exist here (cross-graph is checked at load)
            if (!asset.FindFunction(target))
                report.Error("Call node targets a function that does not exist in this graph.", node.id);
            else
                callEdges[node.functionId].insert(target);   // edge: caller scope -> callee function
        }
    }
    // Depth-first cycle detection over same-graph function calls (recursion is rejected, not run).
    {
        std::unordered_map<FunctionId, int> color;   // 0=unvisited 1=on-stack 2=done
        std::function<bool(FunctionId)> dfs = [&](FunctionId f) -> bool {
            color[f] = 1;
            auto it = callEdges.find(f);
            if (it != callEdges.end())
                for (FunctionId next : it->second) {
                    if (color[next] == 1) return true;                 // back-edge => cycle
                    if (color[next] == 0 && dfs(next)) return true;
                }
            color[f] = 2;
            return false;
        };
        for (const VisualFunction& fn : asset.functions)
            if (color[fn.id] == 0 && dfs(fn.id))
                report.Error("Recursive/cyclic function call detected involving function '" + fn.name + "'.");
    }

    // --- events / interfaces (Milestone 6) ----------------------------------
    std::unordered_set<std::string> eventNames;
    for (const VisualCustomEvent& e : asset.events)
        if (!e.name.empty() && !eventNames.insert(e.name).second)
            report.Error("Duplicate event name: " + e.name);
    std::unordered_set<std::string> interfaceNames;
    for (const VisualInterface& itf : asset.interfaces) {
        if (!itf.name.empty() && !interfaceNames.insert(itf.name).second)
            report.Error("Duplicate interface name: " + itf.name);
        std::unordered_set<std::string> msgNames;
        for (const VisualCustomEvent& m : itf.messages)
            if (!m.name.empty() && !msgNames.insert(m.name).second)
                report.Error("Interface '" + itf.name + "' has duplicate message: " + m.name);
    }
    // Missing implementations: each implemented interface message needs an On Custom Event handler.
    for (std::uint32_t id : asset.implementedInterfaces) {
        const VisualInterface* itf = asset.FindInterface(id);
        if (!itf) { report.Warn("Implemented interface id not found (was it deleted?)."); continue; }
        for (const VisualCustomEvent& m : itf->messages) {
            const std::string qualified = itf->name + "." + m.name;
            bool found = false;
            for (const VisualNode& node : asset.nodes) {
                if (node.typeId != "Event.Custom") continue;
                auto it = node.properties.find("eventName");
                if (it != node.properties.end() && it->second.AsString() == qualified) { found = true; break; }
            }
            if (!found) report.Warn("Interface '" + itf->name + "': missing handler for message '" + m.name + "'.");
        }
    }

    // --- state machines (Milestone 7) ---------------------------------------
    auto hasBoolOutput = [](const VisualFunction& fn) {
        for (const VisualFunctionParam& p : fn.outputs) if (p.type == ValueType::Bool) return true;
        return false;
    };
    for (const VisualStateMachine& sm : asset.stateMachines) {
        std::unordered_set<std::uint32_t> stateIds;
        for (const VisualState& st : sm.states) stateIds.insert(st.id);
        if (!sm.states.empty() && stateIds.count(sm.entryState) == 0)
            report.Error("State machine '" + sm.name + "' has no valid entry state.");
        auto checkFn = [&](FunctionId f, const std::string& where) {
            if (f != kInvalidFunctionId && !asset.FindFunction(f))
                report.Error("State machine '" + sm.name + "': " + where + " references a missing function.");
        };
        for (const VisualState& st : sm.states) {
            checkFn(st.onEnter, "state '" + st.name + "' Enter");
            checkFn(st.onUpdate, "state '" + st.name + "' Update");
            checkFn(st.onExit, "state '" + st.name + "' Exit");
        }
        for (const VisualTransition& tr : sm.transitions) {
            if (stateIds.count(tr.from) == 0 || stateIds.count(tr.to) == 0)
                report.Error("State machine '" + sm.name + "' has a transition with a missing endpoint.");
            if (tr.condition != kInvalidFunctionId) {
                const VisualFunction* fn = asset.FindFunction(tr.condition);
                if (!fn) report.Error("State machine '" + sm.name + "': transition condition function is missing.");
                else if (!hasBoolOutput(*fn))
                    report.Warn("State machine '" + sm.name + "': a transition condition function should return a Bool.");
            }
        }
    }

    return report;
}

} // namespace engine::vs
