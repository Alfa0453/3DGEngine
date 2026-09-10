#pragma once

// =============================================================================
// Visual Scripting — Milestone 12 : starter-graph templates.
//
// Each template builds a small, VALID, runnable graph from the registered node
// set so a new user can start from a working example instead of a blank canvas.
// Nodes are wired by PIN NAME (not hard-coded pin ids) so templates stay correct
// if a descriptor's pin order ever changes. Header-only and editor-free.
// =============================================================================

#include "VisualNodeRegistry.h"
#include "VisualScriptAsset.h"
#include "VisualScriptTypes.h"

#include <string>
#include <vector>

namespace engine::vs {

enum class VsTemplate {
    Blank, Interaction, Trigger, Pickup, MovingPlatform, Door, Ability, Projectile, UIEvent, AIHelper, LevelStreaming
};

inline const char* VsTemplateName(VsTemplate t) {
    switch (t) {
        case VsTemplate::Blank:          return "Blank";
        case VsTemplate::Interaction:    return "Interaction";
        case VsTemplate::Trigger:        return "Trigger";
        case VsTemplate::Pickup:         return "Pickup";
        case VsTemplate::MovingPlatform: return "Moving Platform";
        case VsTemplate::Door:           return "Door";
        case VsTemplate::Ability:        return "Ability (Cooldown)";
        case VsTemplate::Projectile:     return "Projectile";
        case VsTemplate::UIEvent:        return "UI Event";
        case VsTemplate::AIHelper:       return "AI Helper";
        case VsTemplate::LevelStreaming: return "Level Streaming";
        default:                         return "Template";
    }
}

namespace detail {

// A tiny builder that mints nodes and wires them by pin name.
struct TemplateBuilder {
    VisualScriptAsset asset;
    NodeId nextNode = 0;
    LinkId nextLink = 0;
    VariableId nextVar = 0;

    explicit TemplateBuilder(const char* /*name*/) {
        asset.id = AssetHandle::Generate();
        asset.graphId = 1;
    }
    NodeId add(const char* typeId, glm::vec2 pos) {
        VisualNode n = VisualNodeRegistry::Instance().MakeNode(typeId, ++nextNode);
        n.editorPosition = pos;
        asset.nodes.push_back(std::move(n));
        return nextNode;
    }
    VisualNode* node(NodeId id) {
        for (VisualNode& n : asset.nodes) if (n.id == id) return &n;
        return nullptr;
    }
    static PinId pinByName(const VisualNode& n, const std::string& name) {
        for (const VisualPin& p : n.inputs)  if (p.name == name) return p.id;
        for (const VisualPin& p : n.outputs) if (p.name == name) return p.id;
        return kInvalidPinId;
    }
    void link(NodeId from, const char* fromPin, NodeId to, const char* toPin) {
        const VisualNode* a = node(from); const VisualNode* b = node(to);
        if (!a || !b) return;
        const PinId fp = pinByName(*a, fromPin), tp = pinByName(*b, toPin);
        if (fp == kInvalidPinId || tp == kInvalidPinId) return;
        asset.links.push_back({++nextLink, from, fp, to, tp});
    }
    void setDefault(NodeId n, const char* pin, const VisualValue& v) {
        if (VisualNode* nd = node(n))
            for (VisualPin& p : nd->inputs) if (p.name == pin) p.defaultValue = v;
    }
    void setProp(NodeId n, const char* key, const VisualValue& v) {
        if (VisualNode* nd = node(n)) nd->properties[key] = v;
    }
    VariableId var(const char* name, ValueType type, const VisualValue& def, bool exposed = false) {
        VisualVariable v; v.id = ++nextVar; v.name = name; v.type = type; v.defaultValue = def; v.exposed = exposed;
        asset.variables.push_back(v);
        return nextVar;
    }
    // Add an "On Custom Event" handler for a named event with a single exec output "Then".
    NodeId customEvent(const char* eventName, glm::vec2 pos) {
        const NodeId n = add("Event.Custom", pos);
        setProp(n, "eventName", VisualValue::Str(eventName));
        return n;
    }
    void print(NodeId fromNode, const char* fromPin, const char* text, glm::vec2 pos) {
        const NodeId p = add("Debug.PrintString", pos);
        setDefault(p, "Text", VisualValue::Str(text));
        link(fromNode, fromPin, p, "In");
    }
};

} // namespace detail

// Build a starter graph for the given template.
inline VisualScriptAsset MakeTemplate(VsTemplate kind) {
    using detail::TemplateBuilder;
    RegisterCoreNodes();
    TemplateBuilder b(VsTemplateName(kind));

    switch (kind) {
        case VsTemplate::Blank: {
            b.add("Event.BeginPlay", {40, 40});
            break;
        }
        case VsTemplate::Interaction: {
            const NodeId e = b.customEvent("Interact", {40, 40});
            b.print(e, "Then", "Interacted", {260, 40});
            break;
        }
        case VsTemplate::Trigger: {
            const NodeId e = b.customEvent("TriggerEnter", {40, 40});
            b.print(e, "Then", "Trigger entered", {260, 40});
            break;
        }
        case VsTemplate::Pickup: {
            const NodeId e = b.customEvent("TriggerEnter", {40, 40});
            b.print(e, "Then", "Picked up item", {260, 40});
            break;
        }
        case VsTemplate::MovingPlatform: {
            // BeginPlay -> Timeline(loop) : Update -> Lerp(A..B by Alpha) -> Set Position(Self)
            const NodeId begin = b.add("Event.BeginPlay", {40, 40});
            const NodeId tl = b.add("Time.Timeline", {240, 40});
            b.setDefault(tl, "Duration", VisualValue::Float(3.0f));
            b.setDefault(tl, "Loop", VisualValue::Bool(true));
            b.link(begin, "Then", tl, "Play");
            const NodeId lerp = b.add("Math.LerpVector3", {460, 120});
            b.setDefault(lerp, "A", VisualValue::Vec3(glm::vec3(0, 0, 0)));
            b.setDefault(lerp, "B", VisualValue::Vec3(glm::vec3(0, 3, 0)));
            b.link(tl, "Alpha", lerp, "Alpha");
            const NodeId setPos = b.add("Transform.SetPosition", {680, 40});
            b.link(tl, "Update", setPos, "In");
            b.link(lerp, "Result", setPos, "Position");
            break;
        }
        case VsTemplate::Door: {
            // On "Interact" -> Timeline once : Update -> Lerp(closed..open) -> Set Position
            const NodeId e = b.customEvent("Interact", {40, 40});
            const NodeId tl = b.add("Time.Timeline", {240, 40});
            b.setDefault(tl, "Duration", VisualValue::Float(1.0f));
            b.link(e, "Then", tl, "Play");
            const NodeId lerp = b.add("Math.LerpVector3", {460, 120});
            b.setDefault(lerp, "A", VisualValue::Vec3(glm::vec3(0, 0, 0)));
            b.setDefault(lerp, "B", VisualValue::Vec3(glm::vec3(0, 4, 0)));
            b.link(tl, "Alpha", lerp, "Alpha");
            const NodeId setPos = b.add("Transform.SetPosition", {680, 40});
            b.link(tl, "Update", setPos, "In");
            b.link(lerp, "Result", setPos, "Position");
            break;
        }
        case VsTemplate::Ability: {
            // On "Activate" -> Cooldown Gate : Ready -> Print ; On Cooldown -> Print
            const NodeId e = b.customEvent("Activate", {40, 40});
            const NodeId cd = b.add("Time.Cooldown", {240, 40});
            b.setDefault(cd, "Cooldown", VisualValue::Float(2.0f));
            b.link(e, "Then", cd, "In");
            b.print(cd, "Ready", "Ability fired", {460, 20});
            b.print(cd, "On Cooldown", "Still on cooldown", {460, 120});
            break;
        }
        case VsTemplate::Projectile: {
            // Update -> Get Position + (forward*speed*dt) -> Set Position
            const NodeId upd = b.add("Event.Update", {40, 40});
            const NodeId getPos = b.add("Transform.GetPosition", {240, 140});
            const NodeId scale = b.add("Math.ScaleVector3", {240, 220});
            b.setDefault(scale, "V", VisualValue::Vec3(glm::vec3(0, 0, 1)));
            b.setDefault(scale, "S", VisualValue::Float(10.0f));
            const NodeId addv = b.add("Math.AddVector3", {460, 160});
            b.link(getPos, "Position", addv, "A");
            b.link(scale, "Result", addv, "B");
            const NodeId setPos = b.add("Transform.SetPosition", {680, 40});
            b.link(upd, "Then", setPos, "In");
            b.link(addv, "Result", setPos, "Position");
            break;
        }
        case VsTemplate::UIEvent: {
            const NodeId e = b.customEvent("ButtonClicked", {40, 40});
            b.print(e, "Then", "Button clicked", {260, 40});
            break;
        }
        case VsTemplate::AIHelper: {
            // A skeleton with an exposed "TargetRange" variable and an Update tick.
            b.var("TargetRange", ValueType::Float, VisualValue::Float(8.0f), true);
            const NodeId upd = b.add("Event.Update", {40, 40});
            b.print(upd, "Then", "AI tick", {260, 40});
            break;
        }
        case VsTemplate::LevelStreaming: {
            // On "EnterZone" -> Load Level (Async) : Completed -> Print ; Failed -> Print
            const NodeId e = b.customEvent("EnterZone", {40, 40});
            const NodeId load = b.add("Async.LoadLevel", {240, 40});
            b.setDefault(load, "Level", VisualValue::Str("Zone2"));
            b.setDefault(load, "Timeout", VisualValue::Float(10.0f));
            b.link(e, "Then", load, "In");
            b.print(load, "Completed", "Level loaded", {460, 20});
            b.print(load, "Failed", "Load failed", {460, 120});
            break;
        }
    }
    return std::move(b.asset);
}

// The full ordered template menu.
inline std::vector<VsTemplate> AllTemplates() {
    return { VsTemplate::Blank, VsTemplate::Interaction, VsTemplate::Trigger, VsTemplate::Pickup,
             VsTemplate::MovingPlatform, VsTemplate::Door, VsTemplate::Ability, VsTemplate::Projectile,
             VsTemplate::UIEvent, VsTemplate::AIHelper, VsTemplate::LevelStreaming };
}

} // namespace engine::vs
