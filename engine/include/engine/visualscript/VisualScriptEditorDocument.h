#pragma once

// =============================================================================
// Visual Scripting — Pass 3 : editor document model (GL/ImGui-free).
//
// The authoring backbone the Visual Script Editor panel renders. It owns the
// AUTHORED graph document plus editor-only state (selection, pan/zoom, dirty)
// and every mutating operation with undo/redo (Phase 14), typed connection
// validation (Phase 7), id-safe copy/paste (Phase 15), and variable ops that
// preserve VariableId across rename (Phase 12). It contains NO runtime instance
// state — the runtime stays entirely under the engine VS runtime (critical rule).
//
// Keeping this pure means the panel is a thin view and the logic is testable
// without a window.
// =============================================================================

#include "VisualNodeRegistry.h"
#include "VisualScriptAsset.h"
#include "VisualScriptTypes.h"
#include "VisualScriptValidator.h"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace engine::vs {

class VisualScriptEditorDocument {
public:
    // ---- lifecycle ---------------------------------------------------------
    void Open(const AssetHandle& handle, const VisualScriptAsset& asset) {
        m_handle = handle;
        m_asset = asset;
        m_selection.clear();
        m_undo.clear();
        m_redo.clear();
        m_dirty = false;
        RefreshMinters();
    }
    const AssetHandle& Handle() const { return m_handle; }
    const VisualScriptAsset& Asset() const { return m_asset; }
    VisualScriptAsset& MutableAsset() { return m_asset; }   // for the panel's transient reads

    bool Dirty() const { return m_dirty; }
    void MarkSaved() { m_dirty = false; }

    // editor-only view state (Phase 4)
    glm::vec2 pan{0.0f};
    float     zoom = 1.0f;

    // ---- selection (Phases 8/9/10) ----------------------------------------
    const std::unordered_set<NodeId>& Selection() const { return m_selection; }
    bool IsSelected(NodeId n) const { return m_selection.count(n) != 0; }
    void ClearSelection() { m_selection.clear(); }
    void Select(NodeId n, bool additive) { if (!additive) m_selection.clear(); if (n != kInvalidNodeId) m_selection.insert(n); }
    void ToggleSelect(NodeId n) { if (m_selection.count(n)) m_selection.erase(n); else m_selection.insert(n); }

    // ---- undo / redo (Phase 14) -------------------------------------------
    // Snapshot the whole (small) authored document. Simple and correct; a graph
    // is tiny relative to a scene.
    void PushUndo() {
        m_undo.push_back(m_asset);
        if (m_undo.size() > kMaxUndo) m_undo.erase(m_undo.begin());
        m_redo.clear();
    }
    bool CanUndo() const { return !m_undo.empty(); }
    bool CanRedo() const { return !m_redo.empty(); }
    void Undo() {
        if (m_undo.empty()) return;
        m_redo.push_back(m_asset);
        m_asset = m_undo.back(); m_undo.pop_back();
        PruneSelection(); RefreshMinters(); m_dirty = true;
    }
    void Redo() {
        if (m_redo.empty()) return;
        m_undo.push_back(m_asset);
        m_asset = m_redo.back(); m_redo.pop_back();
        PruneSelection(); RefreshMinters(); m_dirty = true;
    }

    // ---- node ops (Phases 5/11/12) ----------------------------------------
    NodeId AddNode(const std::string& typeId, const glm::vec2& position) {
        PushUndo();
        VisualNode node = VisualNodeRegistry::Instance().MakeNode(typeId, ++m_nextNodeId);
        node.editorPosition = position;
        m_asset.nodes.push_back(std::move(node));
        m_dirty = true;
        Select(m_nextNodeId, false);
        return m_nextNodeId;
    }

    // A variable Get/Set node carries the VariableId in its "var" property and its data pin typed to
    // the variable (Phase 11). Convenience used by the palette's variable drag.
    NodeId AddVariableNode(bool setter, VariableId variableId, const glm::vec2& position) {
        const VisualVariable* var = m_asset.FindVariable(variableId);
        if (!var) return kInvalidNodeId;
        const NodeId id = AddNode(setter ? "Var.Set" : "Var.Get", position);
        VisualNode* node = FindNodeMutable(id);
        if (!node) return id;
        node->properties["var"] = VisualValue::Int(static_cast<int>(variableId));
        // Type every "Value" pin to the variable's type — Set has both a Value input and a
        // pass-through Value output; Get has a Value output.
        auto typePins = [&](std::vector<VisualPin>& pins) {
            for (VisualPin& p : pins)
                if (p.name == "Value") { p.type = var->type; p.defaultValue = VisualValue::MakeDefault(var->type); }
        };
        typePins(node->inputs);
        typePins(node->outputs);
        m_dirty = true;
        return id;
    }

    void MoveSelected(const glm::vec2& delta) {
        for (VisualNode& n : m_asset.nodes) if (m_selection.count(n.id)) n.editorPosition += delta;
        m_dirty = true;   // panel calls PushUndo() once at drag start
    }

    void DeleteSelected() {
        if (m_selection.empty()) return;
        PushUndo();
        auto& links = m_asset.links;
        links.erase(std::remove_if(links.begin(), links.end(), [&](const VisualLink& l) {
            return m_selection.count(l.fromNode) || m_selection.count(l.toNode); }), links.end());
        auto& nodes = m_asset.nodes;
        nodes.erase(std::remove_if(nodes.begin(), nodes.end(), [&](const VisualNode& n) {
            return m_selection.count(n.id) != 0; }), nodes.end());
        m_selection.clear();
        m_dirty = true;
    }

    void SetPinDefault(NodeId nodeId, PinId pinId, const VisualValue& value) {  // Phase 8/9/10
        VisualNode* node = FindNodeMutable(nodeId);
        if (!node) return;
        for (VisualPin& p : node->inputs) if (p.id == pinId) { PushUndo(); p.defaultValue = value; m_dirty = true; return; }
    }

    // ---- linking (Phase 7) -------------------------------------------------
    // Reject invalid connections up front with a reason; never store a bad link.
    bool Connect(NodeId fromNode, PinId fromPin, NodeId toNode, PinId toPin, std::string* reason) {
        const VisualNode* a = m_asset.FindNode(fromNode);
        const VisualNode* b = m_asset.FindNode(toNode);
        if (!a || !b) { if (reason) *reason = "Missing endpoint node."; return false; }
        const VisualPin* pa = a->FindPin(fromPin);
        const VisualPin* pb = b->FindPin(toPin);
        if (!pa || !pb) { if (reason) *reason = "Missing endpoint pin."; return false; }
        // Normalize so `from` is the output side.
        if (pa->direction == PinDirection::Input && pb->direction == PinDirection::Output) {
            std::swap(fromNode, toNode); std::swap(fromPin, toPin); std::swap(pa, pb);
        }
        if (!ConnectionValid(*pa, *pb, reason)) return false;
        if (fromNode == toNode) { if (reason) *reason = "Cannot connect a node to itself."; return false; }

        PushUndo();
        // Multiplicity: a data input takes one source; an exec output drives one target — replace.
        auto& links = m_asset.links;
        links.erase(std::remove_if(links.begin(), links.end(), [&](const VisualLink& l) {
            if (pb->kind == PinKind::Data && l.toNode == toNode && l.toPin == toPin) return true;
            if (pa->kind == PinKind::Exec && l.fromNode == fromNode && l.fromPin == fromPin) return true;
            return false;
        }), links.end());
        m_asset.links.push_back({++m_nextLinkId, fromNode, fromPin, toNode, toPin});
        m_dirty = true;
        return true;
    }

    void Disconnect(LinkId linkId) {
        auto& links = m_asset.links;
        auto it = std::find_if(links.begin(), links.end(), [&](const VisualLink& l) { return l.id == linkId; });
        if (it == links.end()) return;
        PushUndo(); links.erase(it); m_dirty = true;
    }

    // ---- copy / paste (Phase 15) ------------------------------------------
    void CopySelection() {
        s_clipboard = Clipboard{};
        std::unordered_set<NodeId> set = m_selection;
        for (const VisualNode& n : m_asset.nodes) if (set.count(n.id)) s_clipboard.nodes.push_back(n);
        for (const VisualLink& l : m_asset.links)
            if (set.count(l.fromNode) && set.count(l.toNode)) s_clipboard.links.push_back(l);
    }

    void Paste(const glm::vec2& offset) {
        if (s_clipboard.nodes.empty()) return;
        PushUndo();
        std::unordered_map<NodeId, NodeId> nodeRemap;
        // (node,oldPin) -> newPin, so links relink to freshly minted pin ids.
        std::unordered_map<std::uint64_t, PinId> pinRemap;
        auto key = [](NodeId n, PinId p) { return (static_cast<std::uint64_t>(n) << 32) | p; };

        m_selection.clear();
        for (const VisualNode& src : s_clipboard.nodes) {
            VisualNode copy = src;
            copy.id = ++m_nextNodeId;
            nodeRemap[src.id] = copy.id;
            copy.editorPosition += offset;
            PinId nextPin = 1;   // fresh, unique-within-node pin ids
            for (VisualPin& p : copy.inputs)  { pinRemap[key(src.id, p.id)] = nextPin; p.id = nextPin++; }
            for (VisualPin& p : copy.outputs) { pinRemap[key(src.id, p.id)] = nextPin; p.id = nextPin++; }
            m_asset.nodes.push_back(std::move(copy));
            m_selection.insert(m_nextNodeId);
        }
        for (const VisualLink& src : s_clipboard.links) {
            auto fn = nodeRemap.find(src.fromNode), tn = nodeRemap.find(src.toNode);
            if (fn == nodeRemap.end() || tn == nodeRemap.end()) continue;
            VisualLink link;
            link.id = ++m_nextLinkId;
            link.fromNode = fn->second; link.fromPin = pinRemap[key(src.fromNode, src.fromPin)];
            link.toNode = tn->second;   link.toPin = pinRemap[key(src.toNode, src.toPin)];
            m_asset.links.push_back(link);
        }
        m_dirty = true;
    }
    void Duplicate(const glm::vec2& offset) { CopySelection(); Paste(offset); }
    static bool ClipboardHasContent() { return !s_clipboard.nodes.empty(); }

    // ---- variables (Phases 11/12) -----------------------------------------
    VariableId CreateVariable(const std::string& name, ValueType type) {
        PushUndo();
        VisualVariable var;
        var.id = ++m_nextVariableId;
        var.name = name;
        var.type = type;
        var.defaultValue = VisualValue::MakeDefault(type);
        m_asset.variables.push_back(std::move(var));
        m_dirty = true;
        return m_nextVariableId;
    }
    void DeleteVariable(VariableId id) {
        auto& vars = m_asset.variables;
        auto it = std::find_if(vars.begin(), vars.end(), [&](const VisualVariable& v) { return v.id == id; });
        if (it == vars.end()) return;
        PushUndo(); vars.erase(it); m_dirty = true;   // referencing Get/Set nodes become validator diagnostics
    }
    // Rename preserves the VariableId, so every Get/Set node keeps resolving (Phase 12).
    void RenameVariable(VariableId id, const std::string& newName) {
        for (VisualVariable& v : m_asset.variables) if (v.id == id) { PushUndo(); v.name = newName; m_dirty = true; return; }
    }
    void SetVariableDefault(VariableId id, const VisualValue& value) {
        for (VisualVariable& v : m_asset.variables) if (v.id == id) { PushUndo(); v.defaultValue = value; m_dirty = true; return; }
    }
    void SetVariableExposed(VariableId id, bool exposed) {
        for (VisualVariable& v : m_asset.variables) if (v.id == id) { PushUndo(); v.exposed = exposed; m_dirty = true; return; }
    }

    // ---- comments (Phase 13) ----------------------------------------------
    CommentId AddComment(const glm::vec2& position) {
        PushUndo();
        VisualComment c; c.id = ++m_nextCommentId; c.position = position;
        m_asset.comments.push_back(std::move(c));
        m_dirty = true;
        return m_nextCommentId;
    }
    void SetCommentText(CommentId id, const std::string& text) {
        for (VisualComment& c : m_asset.comments) if (c.id == id) { PushUndo(); c.text = text; m_dirty = true; return; }
    }
    void DeleteComment(CommentId id) {
        auto& cs = m_asset.comments;
        auto it = std::find_if(cs.begin(), cs.end(), [&](const VisualComment& c) { return c.id == id; });
        if (it == cs.end()) return;
        PushUndo(); cs.erase(it); m_dirty = true;
    }

    // ---- validation (Phases 16/17) ----------------------------------------
    ValidationReport Validate() const { return ValidateGraph(m_asset); }

private:
    struct Clipboard {
        std::vector<VisualNode> nodes;
        std::vector<VisualLink> links;
    };

    VisualNode* FindNodeMutable(NodeId id) {
        for (VisualNode& n : m_asset.nodes) if (n.id == id) return &n;
        return nullptr;
    }
    void RefreshMinters() {
        m_nextNodeId = m_asset.MaxNodeId();
        m_nextLinkId = m_asset.MaxLinkId();
        m_nextVariableId = m_asset.MaxVariableId();
        m_nextCommentId = m_asset.MaxCommentId();
    }
    void PruneSelection() {
        std::unordered_set<NodeId> alive;
        for (const VisualNode& n : m_asset.nodes) alive.insert(n.id);
        for (auto it = m_selection.begin(); it != m_selection.end();)
            it = alive.count(*it) ? std::next(it) : m_selection.erase(it);
    }

    static constexpr std::size_t kMaxUndo = 128;

    AssetHandle m_handle;
    VisualScriptAsset m_asset;
    std::unordered_set<NodeId> m_selection;
    std::vector<VisualScriptAsset> m_undo;
    std::vector<VisualScriptAsset> m_redo;
    bool m_dirty = false;

    NodeId     m_nextNodeId = 0;
    LinkId     m_nextLinkId = 0;
    VariableId m_nextVariableId = 0;
    CommentId  m_nextCommentId = 0;

    inline static Clipboard s_clipboard;   // shared across documents (cross-graph paste)
};

} // namespace engine::vs
