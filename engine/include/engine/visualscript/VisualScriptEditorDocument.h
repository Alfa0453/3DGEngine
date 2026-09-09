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
#include <cmath>
#include <cstddef>
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
        m_scope = kInvalidFunctionId;   // always open on the event graph (Milestone 3)
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
        node.functionId = m_scope;   // Milestone 3: new nodes belong to the scope on screen
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
            for (VisualPin& p : pins) if (p.name == "Value") {
                p.type = var->type;
                p.elementType = var->defaultValue.elementType;   // Milestone 4 container metadata
                p.keyType = var->defaultValue.keyType;
                p.typeId = var->defaultValue.typeId;
                p.defaultValue = var->defaultValue;
            }
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
    void SetCommentColor(CommentId id, const glm::vec4& color) {
        for (VisualComment& c : m_asset.comments) if (c.id == id) { PushUndo(); c.color = color; m_dirty = true; return; }
    }
    void SetCommentCollapsed(CommentId id, bool collapsed) {
        for (VisualComment& c : m_asset.comments) if (c.id == id) { PushUndo(); c.collapsed = collapsed; m_dirty = true; return; }
    }
    void SetCommentSize(CommentId id, const glm::vec2& size) {
        for (VisualComment& c : m_asset.comments) if (c.id == id) { c.size = glm::max(size, glm::vec2(60.0f, 32.0f)); m_dirty = true; return; }
    }
    // Move a comment; optionally drag every node whose top-left sits within its rect (Milestone 2).
    void MoveComment(CommentId id, const glm::vec2& delta, bool moveEnclosed) {
        VisualComment* comment = nullptr;
        for (VisualComment& c : m_asset.comments) if (c.id == id) { comment = &c; break; }
        if (!comment) return;
        if (moveEnclosed) {
            const glm::vec2 lo = comment->position;
            const glm::vec2 hi = comment->position + comment->size;
            for (VisualNode& n : m_asset.nodes)
                if (n.editorPosition.x >= lo.x && n.editorPosition.x <= hi.x &&
                    n.editorPosition.y >= lo.y && n.editorPosition.y <= hi.y)
                    n.editorPosition += delta;
        }
        comment->position += delta;
        m_dirty = true;
    }

    // ---- layout / alignment (Milestone 2) ---------------------------------
    // axis: 0=left 1=h-center 2=right 3=top 4=v-center 5=bottom. Aligns by top-left position
    // (approximate but stable — a node's on-screen size is editor-derived).
    void AlignSelected(int axis) {
        std::vector<VisualNode*> sel = SelectedNodes();
        if (sel.size() < 2) return;
        PushUndo();
        float acc = 0.0f;
        for (VisualNode* n : sel) acc += (axis < 3 ? n->editorPosition.x : n->editorPosition.y);
        const float mean = acc / static_cast<float>(sel.size());
        float lo = 1e9f, hi = -1e9f;
        for (VisualNode* n : sel) {
            const float v = (axis < 3 ? n->editorPosition.x : n->editorPosition.y);
            lo = std::min(lo, v); hi = std::max(hi, v);
        }
        for (VisualNode* n : sel) {
            switch (axis) {
                case 0: n->editorPosition.x = lo;   break;
                case 1: n->editorPosition.x = mean; break;
                case 2: n->editorPosition.x = hi;   break;
                case 3: n->editorPosition.y = lo;   break;
                case 4: n->editorPosition.y = mean; break;
                case 5: n->editorPosition.y = hi;   break;
                default: break;
            }
        }
        m_dirty = true;
    }
    // Even spacing between the min and max selected node along one axis.
    void DistributeSelected(bool horizontal) {
        std::vector<VisualNode*> sel = SelectedNodes();
        if (sel.size() < 3) return;
        PushUndo();
        std::sort(sel.begin(), sel.end(), [&](const VisualNode* a, const VisualNode* b) {
            return horizontal ? a->editorPosition.x < b->editorPosition.x
                              : a->editorPosition.y < b->editorPosition.y;
        });
        const float lo = horizontal ? sel.front()->editorPosition.x : sel.front()->editorPosition.y;
        const float hi = horizontal ? sel.back()->editorPosition.x  : sel.back()->editorPosition.y;
        const float step = (hi - lo) / static_cast<float>(sel.size() - 1);
        for (std::size_t i = 0; i < sel.size(); ++i) {
            const float v = lo + step * static_cast<float>(i);
            if (horizontal) sel[i]->editorPosition.x = v; else sel[i]->editorPosition.y = v;
        }
        m_dirty = true;
    }
    void SnapSelectedToGrid(float grid) {
        if (grid <= 0.0f) return;
        std::vector<VisualNode*> sel = SelectedNodes();
        if (sel.empty()) return;
        PushUndo();
        for (VisualNode* n : sel) {
            n->editorPosition.x = std::round(n->editorPosition.x / grid) * grid;
            n->editorPosition.y = std::round(n->editorPosition.y / grid) * grid;
        }
        m_dirty = true;
    }
    // Simple layered auto-layout: order by current x, then stack in columns of a fixed height.
    void AutoLayoutSelected(float colWidth, float rowHeight, int perColumn) {
        std::vector<VisualNode*> sel = SelectedNodes();
        if (sel.empty()) { for (VisualNode& n : m_asset.nodes) sel.push_back(&n); }
        if (sel.size() < 2) return;
        PushUndo();
        std::sort(sel.begin(), sel.end(), [](const VisualNode* a, const VisualNode* b) {
            return a->editorPosition.x < b->editorPosition.x;
        });
        float baseX = 1e9f, baseY = 1e9f;
        for (VisualNode* n : sel) { baseX = std::min(baseX, n->editorPosition.x); baseY = std::min(baseY, n->editorPosition.y); }
        const int per = std::max(1, perColumn);
        for (std::size_t i = 0; i < sel.size(); ++i) {
            const int col = static_cast<int>(i) / per;
            const int row = static_cast<int>(i) % per;
            sel[i]->editorPosition = glm::vec2(baseX + col * colWidth, baseY + row * rowHeight);
        }
        m_dirty = true;
    }

    // ---- functions / subgraphs (Milestone 3) ------------------------------
    // The canvas shows ONE scope at a time: 0 == the event graph, else a function's nodes.
    FunctionId CurrentScope() const { return m_scope; }
    void SetScope(FunctionId scope) { m_scope = scope; m_selection.clear(); }

    FunctionId CreateFunction(const std::string& name) {
        PushUndo();
        VisualFunction fn;
        fn.id = ++m_nextFunctionId;
        fn.name = name.empty() ? ("Function_" + std::to_string(fn.id)) : name;
        m_asset.functions.push_back(fn);
        // Seed an Entry and a Return node in the new function's scope.
        MintNode("Function.Entry", glm::vec2(-220.0f, 0.0f), fn.id);
        MintNode("Function.Return", glm::vec2(220.0f, 0.0f), fn.id);
        SyncFunctionInterfaceNodes(fn.id);
        m_dirty = true;
        return fn.id;
    }

    void DeleteFunction(FunctionId id) {
        auto fit = std::find_if(m_asset.functions.begin(), m_asset.functions.end(),
            [&](const VisualFunction& f) { return f.id == id; });
        if (fit == m_asset.functions.end()) return;
        PushUndo();
        std::unordered_set<NodeId> inScope;
        for (const VisualNode& n : m_asset.nodes) if (n.functionId == id) inScope.insert(n.id);
        auto& links = m_asset.links;
        links.erase(std::remove_if(links.begin(), links.end(), [&](const VisualLink& l) {
            return inScope.count(l.fromNode) || inScope.count(l.toNode); }), links.end());
        auto& nodes = m_asset.nodes;
        nodes.erase(std::remove_if(nodes.begin(), nodes.end(), [&](const VisualNode& n) {
            return n.functionId == id; }), nodes.end());
        m_asset.functions.erase(fit);
        if (m_scope == id) m_scope = kInvalidFunctionId;
        m_selection.clear();
        m_dirty = true;
    }

    void RenameFunction(FunctionId id, const std::string& name) {
        for (VisualFunction& f : m_asset.functions) if (f.id == id) { PushUndo(); f.name = name; m_dirty = true; return; }
    }

    void AddFunctionParam(FunctionId id, bool output, const std::string& name, ValueType type) {
        VisualFunction* fn = FindFunctionMutable(id);
        if (!fn) return;
        PushUndo();
        (output ? fn->outputs : fn->inputs).push_back({name.empty() ? "Param" : name, type});
        SyncFunctionInterfaceNodes(id);
        m_dirty = true;
    }
    void RemoveFunctionParam(FunctionId id, bool output, std::size_t index) {
        VisualFunction* fn = FindFunctionMutable(id);
        if (!fn) return;
        auto& params = output ? fn->outputs : fn->inputs;
        if (index >= params.size()) return;
        PushUndo();
        params.erase(params.begin() + static_cast<std::ptrdiff_t>(index));
        SyncFunctionInterfaceNodes(id);
        m_dirty = true;
    }
    void SetFunctionParam(FunctionId id, bool output, std::size_t index,
                          const std::string& name, ValueType type) {
        VisualFunction* fn = FindFunctionMutable(id);
        if (!fn) return;
        auto& params = output ? fn->outputs : fn->inputs;
        if (index >= params.size()) return;
        PushUndo();
        params[index].name = name;
        params[index].type = type;
        SyncFunctionInterfaceNodes(id);
        m_dirty = true;
    }

    VariableId AddFunctionLocal(FunctionId id, const std::string& name, ValueType type) {
        VisualFunction* fn = FindFunctionMutable(id);
        if (!fn) return kInvalidVariableId;
        PushUndo();
        VisualVariable v;
        v.id = ++m_nextVariableId;   // locals share the graph variable id space (unique across the asset)
        v.name = name.empty() ? ("Local_" + std::to_string(v.id)) : name;
        v.type = type;
        v.defaultValue = VisualValue::MakeDefault(type);
        fn->locals.push_back(std::move(v));
        m_dirty = true;
        return m_nextVariableId;
    }
    void DeleteFunctionLocal(FunctionId id, VariableId localId) {
        VisualFunction* fn = FindFunctionMutable(id);
        if (!fn) return;
        auto it = std::find_if(fn->locals.begin(), fn->locals.end(),
            [&](const VisualVariable& v) { return v.id == localId; });
        if (it == fn->locals.end()) return;
        PushUndo(); fn->locals.erase(it); m_dirty = true;
    }
    void RenameFunctionLocal(FunctionId id, VariableId localId, const std::string& name) {
        VisualFunction* fn = FindFunctionMutable(id);
        if (!fn) return;
        for (VisualVariable& v : fn->locals) if (v.id == localId) { PushUndo(); v.name = name; m_dirty = true; return; }
    }
    void SetFunctionLocalDefault(FunctionId id, VariableId localId, const VisualValue& value) {
        VisualFunction* fn = FindFunctionMutable(id);
        if (!fn) return;
        for (VisualVariable& v : fn->locals) if (v.id == localId) { PushUndo(); v.defaultValue = value; m_dirty = true; return; }
    }
    // A Get/Set node for a function-local variable (same node types as graph variables).
    NodeId AddLocalVariableNode(bool setter, FunctionId funcId, VariableId localId, const glm::vec2& position) {
        VisualFunction* fn = FindFunctionMutable(funcId);
        if (!fn) return kInvalidNodeId;
        const VisualVariable* var = nullptr;
        for (const VisualVariable& v : fn->locals) if (v.id == localId) { var = &v; break; }
        if (!var) return kInvalidNodeId;
        const NodeId id = AddNode(setter ? "Var.Set" : "Var.Get", position);   // stamps current scope
        VisualNode* node = FindNodeMutable(id);
        if (!node) return id;
        node->properties["var"] = VisualValue::Int(static_cast<int>(localId));
        const VisualVariable local = *var;
        auto typePins = [&](std::vector<VisualPin>& pins) {
            for (VisualPin& p : pins) if (p.name == "Value") {
                p.type = local.type;
                p.elementType = local.defaultValue.elementType;
                p.keyType = local.defaultValue.keyType;
                p.typeId = local.defaultValue.typeId;
                p.defaultValue = local.defaultValue;
            }
        };
        typePins(node->inputs); typePins(node->outputs);
        m_dirty = true;
        return id;
    }

    // Insert a node that calls `target` (a function in THIS graph) into the current scope.
    NodeId AddCallNode(FunctionId target, const glm::vec2& position) {
        if (!m_asset.FindFunction(target)) return kInvalidNodeId;
        PushUndo();
        const NodeId id = MintNode("Function.Call", position, m_scope);
        VisualNode* node = FindNodeMutable(id);
        if (node) {
            node->properties["graph"] = VisualValue::Asset(AssetHandle{});    // self graph
            node->properties["func"] = VisualValue::Int(static_cast<int>(target));
        }
        SyncFunctionInterfaceNodes(target);   // builds the call node's data pins from the signature
        Select(id, false);
        m_dirty = true;
        return id;
    }

    // If `node` is a same-graph Call, the function it targets (else kInvalidFunctionId). Editor uses
    // this for double-click navigation.
    FunctionId CallTargetOf(NodeId nodeId) const {
        const VisualNode* node = m_asset.FindNode(nodeId);
        if (!node || node->typeId != "Function.Call") return kInvalidFunctionId;
        AssetHandle g;
        if (auto it = node->properties.find("graph"); it != node->properties.end()) g = it->second.AsAsset();
        if (g.Valid() && !(g == m_asset.id)) return kInvalidFunctionId;   // cross-graph — not navigable here
        if (auto it = node->properties.find("func"); it != node->properties.end())
            return static_cast<FunctionId>(it->second.AsInt());
        return kInvalidFunctionId;
    }

    // ---- structs / enums (Milestone 4) ------------------------------------
    std::uint32_t CreateStruct(const std::string& name) {
        PushUndo();
        VisualStructType s; s.id = ++m_nextTypeId;
        s.name = name.empty() ? ("Struct_" + std::to_string(s.id)) : name;
        m_asset.structs.push_back(std::move(s));
        m_dirty = true;
        return m_nextTypeId;
    }
    void RenameStruct(std::uint32_t id, const std::string& name) {
        for (VisualStructType& s : m_asset.structs) if (s.id == id) { PushUndo(); s.name = name; m_dirty = true; return; }
    }
    void DeleteStruct(std::uint32_t id) {
        auto it = std::find_if(m_asset.structs.begin(), m_asset.structs.end(),
            [&](const VisualStructType& s) { return s.id == id; });
        if (it == m_asset.structs.end()) return;
        PushUndo(); m_asset.structs.erase(it); m_dirty = true;   // dependent nodes become validator diagnostics
    }
    void AddStructField(std::uint32_t id, const std::string& name, ValueType type, std::uint32_t fieldTypeId = 0) {
        VisualStructType* s = FindStructMutable(id);
        if (!s) return;
        PushUndo();
        s->fields.push_back({name.empty() ? "Field" : name, type, ValueType::Float, fieldTypeId});
        SyncStructNodes(id); m_dirty = true;
    }
    void RemoveStructField(std::uint32_t id, std::size_t index) {
        VisualStructType* s = FindStructMutable(id);
        if (!s || index >= s->fields.size()) return;
        PushUndo();
        s->fields.erase(s->fields.begin() + static_cast<std::ptrdiff_t>(index));
        SyncStructNodes(id); m_dirty = true;
    }
    void SetStructField(std::uint32_t id, std::size_t index, const std::string& name,
                        ValueType type, std::uint32_t fieldTypeId) {
        VisualStructType* s = FindStructMutable(id);
        if (!s || index >= s->fields.size()) return;
        PushUndo();
        s->fields[index].name = name;
        s->fields[index].type = type;
        s->fields[index].typeId = fieldTypeId;
        SyncStructNodes(id); m_dirty = true;
    }
    NodeId AddStructNode(bool isMake, std::uint32_t structId, const glm::vec2& position) {
        const VisualStructType* def = m_asset.FindStruct(structId);
        if (!def) return kInvalidNodeId;
        PushUndo();
        const NodeId id = MintNode(isMake ? "Struct.Make" : "Struct.Break", position, m_scope);
        VisualNode* node = FindNodeMutable(id);
        if (node) { node->properties["struct"] = VisualValue::Int(static_cast<int>(structId));
                    RebuildStructNode(*node, isMake, *def); }
        Select(id, false); m_dirty = true;
        return id;
    }

    std::uint32_t CreateEnum(const std::string& name) {
        PushUndo();
        VisualEnumType e; e.id = ++m_nextTypeId;
        e.name = name.empty() ? ("Enum_" + std::to_string(e.id)) : name;
        e.entries.push_back("Option0");
        m_asset.enums.push_back(std::move(e));
        m_dirty = true;
        return m_nextTypeId;
    }
    void RenameEnum(std::uint32_t id, const std::string& name) {
        for (VisualEnumType& e : m_asset.enums) if (e.id == id) { PushUndo(); e.name = name; m_dirty = true; return; }
    }
    void DeleteEnum(std::uint32_t id) {
        auto it = std::find_if(m_asset.enums.begin(), m_asset.enums.end(),
            [&](const VisualEnumType& e) { return e.id == id; });
        if (it == m_asset.enums.end()) return;
        PushUndo(); m_asset.enums.erase(it); m_dirty = true;
    }
    void AddEnumEntry(std::uint32_t id, const std::string& name) {
        for (VisualEnumType& e : m_asset.enums) if (e.id == id) {
            PushUndo(); e.entries.push_back(name.empty() ? ("Option" + std::to_string(e.entries.size())) : name);
            m_dirty = true; return;
        }
    }
    void SetEnumEntry(std::uint32_t id, std::size_t index, const std::string& name) {
        for (VisualEnumType& e : m_asset.enums) if (e.id == id) {
            if (index < e.entries.size()) { PushUndo(); e.entries[index] = name; m_dirty = true; }
            return;
        }
    }
    void RemoveEnumEntry(std::uint32_t id, std::size_t index) {
        for (VisualEnumType& e : m_asset.enums) if (e.id == id) {
            if (index < e.entries.size()) { PushUndo(); e.entries.erase(e.entries.begin() + static_cast<std::ptrdiff_t>(index)); m_dirty = true; }
            return;
        }
    }

    // Refine a container/struct/enum variable's default (element/key/struct-enum type) — Milestone 4.
    void SetVariableContainer(VariableId id, ValueType elementType, ValueType keyType, std::uint32_t typeId) {
        for (VisualVariable& v : m_asset.variables) if (v.id == id) {
            PushUndo();
            switch (v.type) {
                case ValueType::Array:  v.defaultValue = VisualValue::MakeArray(elementType); v.defaultValue.typeId = typeId; break;
                case ValueType::Map:    v.defaultValue = VisualValue::MakeMap(keyType, elementType); v.defaultValue.typeId = typeId; break;
                case ValueType::Struct: v.defaultValue = VisualValue::MakeStruct(typeId); break;
                case ValueType::Enum:   v.defaultValue = VisualValue::MakeEnum(typeId, 0); break;
                default: break;
            }
            m_dirty = true; return;
        }
    }

    // ---- events / interfaces (Milestone 6) --------------------------------
    static std::string QualifiedMessage(const VisualInterface& itf, const VisualCustomEvent& msg) {
        return itf.name + "." + msg.name;
    }
    std::uint32_t CreateEvent(const std::string& name) {
        PushUndo();
        VisualCustomEvent e; e.id = ++m_nextTypeId;
        e.name = name.empty() ? ("Event_" + std::to_string(e.id)) : name;
        m_asset.events.push_back(std::move(e));
        m_dirty = true; return m_nextTypeId;
    }
    void RenameEvent(std::uint32_t id, const std::string& name) {
        for (VisualCustomEvent& e : m_asset.events) if (e.id == id) { PushUndo(); e.name = name; SyncEventNodesForEvent(id); m_dirty = true; return; }
    }
    void DeleteEvent(std::uint32_t id) {
        auto it = std::find_if(m_asset.events.begin(), m_asset.events.end(), [&](const VisualCustomEvent& e) { return e.id == id; });
        if (it == m_asset.events.end()) return;
        PushUndo(); m_asset.events.erase(it); m_dirty = true;
    }
    void AddEventParam(std::uint32_t id, const std::string& name, ValueType type) {
        for (VisualCustomEvent& e : m_asset.events) if (e.id == id) {
            PushUndo(); e.params.push_back({name.empty() ? "Param" : name, type}); SyncEventNodesForEvent(id); m_dirty = true; return;
        }
    }
    void RemoveEventParam(std::uint32_t id, std::size_t index) {
        for (VisualCustomEvent& e : m_asset.events) if (e.id == id) {
            if (index < e.params.size()) { PushUndo(); e.params.erase(e.params.begin() + static_cast<std::ptrdiff_t>(index)); SyncEventNodesForEvent(id); m_dirty = true; }
            return;
        }
    }
    void SetEventParam(std::uint32_t id, std::size_t index, const std::string& name, ValueType type) {
        for (VisualCustomEvent& e : m_asset.events) if (e.id == id) {
            if (index < e.params.size()) { PushUndo(); e.params[index] = {name, type}; SyncEventNodesForEvent(id); m_dirty = true; }
            return;
        }
    }
    NodeId AddCustomEventNode(std::uint32_t eventId, const glm::vec2& position) {
        const VisualCustomEvent* e = m_asset.FindEvent(eventId);
        if (!e) return kInvalidNodeId;
        PushUndo();
        const NodeId id = MintNode("Event.Custom", position, m_scope);
        VisualNode* node = FindNodeMutable(id);
        if (node) { node->properties["event"] = VisualValue::Int(static_cast<int>(eventId));
                    node->properties["eventName"] = VisualValue::Str(e->name);
                    RebuildEventNode(*node, e->params, "custom"); }
        Select(id, false); m_dirty = true; return id;
    }
    NodeId AddBroadcastNode(std::uint32_t eventId, const glm::vec2& position) {
        const VisualCustomEvent* e = m_asset.FindEvent(eventId);
        if (!e) return kInvalidNodeId;
        PushUndo();
        const NodeId id = MintNode("Event.Broadcast", position, m_scope);
        VisualNode* node = FindNodeMutable(id);
        if (node) { node->properties["event"] = VisualValue::Int(static_cast<int>(eventId));
                    node->properties["eventName"] = VisualValue::Str(e->name);
                    RebuildEventNode(*node, e->params, "broadcast"); }
        Select(id, false); m_dirty = true; return id;
    }

    std::uint32_t CreateInterface(const std::string& name) {
        PushUndo();
        VisualInterface itf; itf.id = ++m_nextTypeId;
        itf.name = name.empty() ? ("Interface_" + std::to_string(itf.id)) : name;
        m_asset.interfaces.push_back(std::move(itf));
        m_dirty = true; return m_nextTypeId;
    }
    void RenameInterface(std::uint32_t id, const std::string& name) {
        for (VisualInterface& i : m_asset.interfaces) if (i.id == id) { PushUndo(); i.name = name; m_dirty = true; return; }
    }
    void DeleteInterface(std::uint32_t id) {
        auto it = std::find_if(m_asset.interfaces.begin(), m_asset.interfaces.end(), [&](const VisualInterface& i) { return i.id == id; });
        if (it == m_asset.interfaces.end()) return;
        PushUndo(); m_asset.interfaces.erase(it);
        auto& impl = m_asset.implementedInterfaces;
        impl.erase(std::remove(impl.begin(), impl.end(), id), impl.end());
        m_dirty = true;
    }
    void AddInterfaceMessage(std::uint32_t interfaceId, const std::string& name) {
        for (VisualInterface& i : m_asset.interfaces) if (i.id == interfaceId) {
            PushUndo(); VisualCustomEvent m; m.id = ++m_nextTypeId; m.name = name.empty() ? ("Message_" + std::to_string(m.id)) : name;
            i.messages.push_back(std::move(m)); m_dirty = true; return;
        }
    }
    void SetInterfaceImplemented(std::uint32_t interfaceId, bool implemented) {
        auto& impl = m_asset.implementedInterfaces;
        const bool has = std::find(impl.begin(), impl.end(), interfaceId) != impl.end();
        if (implemented && !has) { PushUndo(); impl.push_back(interfaceId); m_dirty = true; }
        else if (!implemented && has) { PushUndo(); impl.erase(std::remove(impl.begin(), impl.end(), interfaceId), impl.end()); m_dirty = true; }
    }
    // Create On Custom Event handlers for every message of an interface (and mark it implemented).
    void ImplementInterface(std::uint32_t interfaceId) {
        const VisualInterface* itf = m_asset.FindInterface(interfaceId);
        if (!itf) return;
        PushUndo();
        SetInterfaceImplemented(interfaceId, true);
        float y = 0.0f;
        for (const VisualCustomEvent& msg : itf->messages) {
            const NodeId id = MintNode("Event.Custom", glm::vec2(-260.0f, y), m_scope);
            VisualNode* node = FindNodeMutable(id);
            if (node) { node->properties["eventName"] = VisualValue::Str(QualifiedMessage(*itf, msg));
                        RebuildEventNode(*node, msg.params, "custom"); }
            y += 140.0f;
        }
        m_dirty = true;
    }
    NodeId AddInterfaceCallNode(std::uint32_t interfaceId, std::size_t messageIndex, const glm::vec2& position) {
        const VisualInterface* itf = m_asset.FindInterface(interfaceId);
        if (!itf || messageIndex >= itf->messages.size()) return kInvalidNodeId;
        const VisualCustomEvent& msg = itf->messages[messageIndex];
        PushUndo();
        const NodeId id = MintNode("Interface.Call", position, m_scope);
        VisualNode* node = FindNodeMutable(id);
        if (node) { node->properties["eventName"] = VisualValue::Str(QualifiedMessage(*itf, msg));
                    RebuildEventNode(*node, msg.params, "call"); }
        Select(id, false); m_dirty = true; return id;
    }

    // ---- state machines (Milestone 7) -------------------------------------
    std::uint32_t CreateStateMachine(const std::string& name) {
        PushUndo();
        VisualStateMachine sm; sm.id = ++m_nextTypeId;
        sm.name = name.empty() ? ("StateMachine_" + std::to_string(sm.id)) : name;
        m_asset.stateMachines.push_back(std::move(sm));
        m_dirty = true; return m_nextTypeId;
    }
    void RenameStateMachine(std::uint32_t id, const std::string& name) {
        for (VisualStateMachine& sm : m_asset.stateMachines) if (sm.id == id) { PushUndo(); sm.name = name; m_dirty = true; return; }
    }
    void DeleteStateMachine(std::uint32_t id) {
        auto it = std::find_if(m_asset.stateMachines.begin(), m_asset.stateMachines.end(),
            [&](const VisualStateMachine& s) { return s.id == id; });
        if (it == m_asset.stateMachines.end()) return;
        PushUndo(); m_asset.stateMachines.erase(it); m_dirty = true;
    }
    void SetEntryState(std::uint32_t smId, std::uint32_t stateId) {
        if (VisualStateMachine* sm = FindStateMachineMutable(smId)) { PushUndo(); sm->entryState = stateId; m_dirty = true; }
    }
    std::uint32_t AddState(std::uint32_t smId, const std::string& name) {
        VisualStateMachine* sm = FindStateMachineMutable(smId);
        if (!sm) return 0;
        PushUndo();
        VisualState st; st.id = ++m_nextTypeId; st.name = name.empty() ? ("State_" + std::to_string(st.id)) : name;
        st.editorPosition = glm::vec2(40.0f + 180.0f * static_cast<float>(sm->states.size()), 60.0f);
        sm->states.push_back(st);
        if (sm->entryState == 0) sm->entryState = st.id;
        m_dirty = true; return st.id;
    }
    void RemoveState(std::uint32_t smId, std::uint32_t stateId) {
        VisualStateMachine* sm = FindStateMachineMutable(smId);
        if (!sm) return;
        PushUndo();
        sm->states.erase(std::remove_if(sm->states.begin(), sm->states.end(),
            [&](const VisualState& s) { return s.id == stateId; }), sm->states.end());
        sm->transitions.erase(std::remove_if(sm->transitions.begin(), sm->transitions.end(),
            [&](const VisualTransition& t) { return t.from == stateId || t.to == stateId; }), sm->transitions.end());
        if (sm->entryState == stateId) sm->entryState = sm->states.empty() ? 0 : sm->states.front().id;
        m_dirty = true;
    }
    void RenameState(std::uint32_t smId, std::uint32_t stateId, const std::string& name) {
        if (VisualStateMachine* sm = FindStateMachineMutable(smId))
            for (VisualState& s : sm->states) if (s.id == stateId) { PushUndo(); s.name = name; m_dirty = true; return; }
    }
    // which: 0=Enter 1=Update 2=Exit
    void SetStateFunction(std::uint32_t smId, std::uint32_t stateId, int which, FunctionId func) {
        if (VisualStateMachine* sm = FindStateMachineMutable(smId))
            for (VisualState& s : sm->states) if (s.id == stateId) {
                PushUndo();
                if (which == 0) s.onEnter = func; else if (which == 1) s.onUpdate = func; else s.onExit = func;
                m_dirty = true; return;
            }
    }
    std::uint32_t AddTransition(std::uint32_t smId, std::uint32_t from, std::uint32_t to) {
        VisualStateMachine* sm = FindStateMachineMutable(smId);
        if (!sm) return 0;
        PushUndo();
        VisualTransition t; t.id = ++m_nextTypeId; t.from = from; t.to = to;
        sm->transitions.push_back(t); m_dirty = true; return t.id;
    }
    void RemoveTransition(std::uint32_t smId, std::uint32_t transId) {
        if (VisualStateMachine* sm = FindStateMachineMutable(smId)) {
            PushUndo();
            sm->transitions.erase(std::remove_if(sm->transitions.begin(), sm->transitions.end(),
                [&](const VisualTransition& t) { return t.id == transId; }), sm->transitions.end());
            m_dirty = true;
        }
    }
    void SetTransition(std::uint32_t smId, std::uint32_t transId, std::uint32_t from, std::uint32_t to,
                       int priority, FunctionId condition, float cooldown) {
        if (VisualStateMachine* sm = FindStateMachineMutable(smId))
            for (VisualTransition& t : sm->transitions) if (t.id == transId) {
                PushUndo();
                t.from = from; t.to = to; t.priority = priority; t.condition = condition; t.cooldown = cooldown;
                m_dirty = true; return;
            }
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
    std::vector<VisualNode*> SelectedNodes() {
        std::vector<VisualNode*> out;
        for (VisualNode& n : m_asset.nodes) if (m_selection.count(n.id)) out.push_back(&n);
        return out;
    }
    VisualFunction* FindFunctionMutable(FunctionId id) {
        for (VisualFunction& f : m_asset.functions) if (f.id == id) return &f;
        return nullptr;
    }
    VisualStructType* FindStructMutable(std::uint32_t id) {
        for (VisualStructType& s : m_asset.structs) if (s.id == id) return &s;
        return nullptr;
    }
    VisualStateMachine* FindStateMachineMutable(std::uint32_t id) {
        for (VisualStateMachine& s : m_asset.stateMachines) if (s.id == id) return &s;
        return nullptr;
    }
    // Rebuild a Struct.Make/Break node's data pins from a struct definition (Milestone 4).
    void RebuildStructNode(VisualNode& node, bool isMake, const VisualStructType& def) {
        const std::vector<VisualPin> oldIn = node.inputs, oldOut = node.outputs;
        PinId maxId = 0;
        for (const VisualPin& p : oldIn)  maxId = std::max(maxId, p.id);
        for (const VisualPin& p : oldOut) maxId = std::max(maxId, p.id);
        auto reuse = [&](const std::vector<VisualPin>& old, const std::string& name) {
            for (const VisualPin& p : old) if (p.name == name) return p.id;
            return ++maxId;
        };
        auto fieldPin = [&](const VisualStructField& f, PinDirection dir, const std::vector<VisualPin>& old) {
            VisualPin p; p.id = reuse(old, f.name); p.direction = dir; p.kind = PinKind::Data;
            p.type = f.type; p.elementType = f.elementType; p.typeId = f.typeId; p.name = f.name;
            p.defaultValue = VisualValue::MakeDefault(f.type);
            return p;
        };
        auto structPin = [&](PinDirection dir, const std::vector<VisualPin>& old) {
            VisualPin p; p.id = reuse(old, "Value"); p.direction = dir; p.kind = PinKind::Data;
            p.type = ValueType::Struct; p.typeId = def.id; p.name = "Value";
            p.defaultValue = VisualValue::MakeStruct(def.id);
            return p;
        };
        node.inputs.clear(); node.outputs.clear();
        if (isMake) {
            for (const VisualStructField& f : def.fields) node.inputs.push_back(fieldPin(f, PinDirection::Input, oldIn));
            node.outputs.push_back(structPin(PinDirection::Output, oldOut));
        } else {
            node.inputs.push_back(structPin(PinDirection::Input, oldIn));
            for (const VisualStructField& f : def.fields) node.outputs.push_back(fieldPin(f, PinDirection::Output, oldOut));
        }
        std::unordered_set<PinId> alive;
        for (const VisualPin& p : node.inputs)  alive.insert(p.id);
        for (const VisualPin& p : node.outputs) alive.insert(p.id);
        auto& links = m_asset.links;
        links.erase(std::remove_if(links.begin(), links.end(), [&](const VisualLink& l) {
            if (l.fromNode == node.id && !alive.count(l.fromPin)) return true;
            if (l.toNode == node.id && !alive.count(l.toPin)) return true;
            return false;
        }), links.end());
    }
    void SyncStructNodes(std::uint32_t structId) {
        const VisualStructType* def = m_asset.FindStruct(structId);
        if (!def) return;
        for (VisualNode& node : m_asset.nodes) {
            const bool isMake = node.typeId == "Struct.Make";
            if (!isMake && node.typeId != "Struct.Break") continue;
            auto it = node.properties.find("struct");
            if (it != node.properties.end() && static_cast<std::uint32_t>(it->second.AsInt()) == structId)
                RebuildStructNode(node, isMake, *def);
        }
    }
    // Rebuild an event node's pins from a signature. kind: "custom" | "broadcast" | "call" (Milestone 6).
    void RebuildEventNode(VisualNode& node, const std::vector<VisualFunctionParam>& params, const std::string& kind) {
        const std::vector<VisualPin> oldIn = node.inputs, oldOut = node.outputs;
        PinId maxId = 0;
        for (const VisualPin& p : oldIn)  maxId = std::max(maxId, p.id);
        for (const VisualPin& p : oldOut) maxId = std::max(maxId, p.id);
        auto reuse = [&](const std::vector<VisualPin>& old, const std::string& name) {
            for (const VisualPin& p : old) if (p.name == name) return p.id;
            return ++maxId;
        };
        auto execPin = [&](const std::vector<VisualPin>& old, const char* nm, PinDirection d) {
            VisualPin p; p.id = reuse(old, nm); p.direction = d; p.kind = PinKind::Exec;
            p.type = ValueType::Exec; p.name = nm; p.defaultValue = VisualValue::Exec(); return p;
        };
        auto dataPin = [&](const std::vector<VisualPin>& old, const std::string& nm, PinDirection d, ValueType t, VisualValue def) {
            VisualPin p; p.id = reuse(old, nm); p.direction = d; p.kind = PinKind::Data;
            p.type = t; p.name = nm; p.defaultValue = std::move(def); return p;
        };
        node.inputs.clear(); node.outputs.clear();
        if (kind == "custom") {
            node.outputs.push_back(execPin(oldOut, "Then", PinDirection::Output));
            for (const VisualFunctionParam& p : params)
                node.outputs.push_back(dataPin(oldOut, p.name, PinDirection::Output, p.type, VisualValue::MakeDefault(p.type)));
        } else {
            node.inputs.push_back(execPin(oldIn, "In", PinDirection::Input));
            if (kind == "broadcast")
                node.inputs.push_back(dataPin(oldIn, "Broadcast", PinDirection::Input, ValueType::Bool, VisualValue::Bool(true)));
            node.inputs.push_back(dataPin(oldIn, "Target", PinDirection::Input, ValueType::Entity, VisualValue::Ent(engine::ecs::kNull)));
            for (const VisualFunctionParam& p : params)
                node.inputs.push_back(dataPin(oldIn, p.name, PinDirection::Input, p.type, VisualValue::MakeDefault(p.type)));
            node.outputs.push_back(execPin(oldOut, "Then", PinDirection::Output));
        }
        std::unordered_set<PinId> alive;
        for (const VisualPin& p : node.inputs)  alive.insert(p.id);
        for (const VisualPin& p : node.outputs) alive.insert(p.id);
        auto& links = m_asset.links;
        links.erase(std::remove_if(links.begin(), links.end(), [&](const VisualLink& l) {
            if (l.fromNode == node.id && !alive.count(l.fromPin)) return true;
            if (l.toNode == node.id && !alive.count(l.toPin)) return true;
            return false;
        }), links.end());
    }
    void SyncEventNodesForEvent(std::uint32_t eventId) {
        const VisualCustomEvent* e = m_asset.FindEvent(eventId);
        if (!e) return;
        for (VisualNode& node : m_asset.nodes) {
            const bool custom = node.typeId == "Event.Custom";
            const bool broadcast = node.typeId == "Event.Broadcast";
            if (!custom && !broadcast) continue;
            auto it = node.properties.find("event");
            if (it == node.properties.end() || static_cast<std::uint32_t>(it->second.AsInt()) != eventId) continue;
            node.properties["eventName"] = VisualValue::Str(e->name);
            RebuildEventNode(node, e->params, custom ? "custom" : "broadcast");
        }
    }
    // Add a node WITHOUT its own undo entry (callers bracket the whole op) — Milestone 3.
    NodeId MintNode(const std::string& typeId, const glm::vec2& position, FunctionId scope) {
        VisualNode node = VisualNodeRegistry::Instance().MakeNode(typeId, ++m_nextNodeId);
        node.editorPosition = position;
        node.functionId = scope;
        m_asset.nodes.push_back(std::move(node));
        return m_nextNodeId;
    }
    // Rebuild an interface node's pins from a signature, preserving pin ids (and thus links) for pins
    // whose name is unchanged, minting fresh ids for new ones, and dropping links to removed pins.
    void RebuildInterfacePins(VisualNode& node, const char* execIn, const char* execOut,
                              const std::vector<VisualFunctionParam>& dataIn,
                              const std::vector<VisualFunctionParam>& dataOut) {
        const std::vector<VisualPin> oldIn = node.inputs, oldOut = node.outputs;
        PinId maxId = 0;
        for (const VisualPin& p : oldIn)  maxId = std::max(maxId, p.id);
        for (const VisualPin& p : oldOut) maxId = std::max(maxId, p.id);
        auto reuseOrMint = [&](const std::vector<VisualPin>& old, const std::string& name) {
            for (const VisualPin& p : old) if (p.name == name) return p.id;
            return ++maxId;
        };
        node.inputs.clear(); node.outputs.clear();
        if (execIn && *execIn) {
            VisualPin p; p.id = reuseOrMint(oldIn, execIn); p.direction = PinDirection::Input;
            p.kind = PinKind::Exec; p.type = ValueType::Exec; p.name = execIn; p.defaultValue = VisualValue::Exec();
            node.inputs.push_back(p);
        }
        for (const VisualFunctionParam& param : dataIn) {
            VisualPin p; p.id = reuseOrMint(oldIn, param.name); p.direction = PinDirection::Input;
            p.kind = PinKind::Data; p.type = param.type; p.name = param.name;
            p.defaultValue = VisualValue::MakeDefault(param.type);
            node.inputs.push_back(p);
        }
        if (execOut && *execOut) {
            VisualPin p; p.id = reuseOrMint(oldOut, execOut); p.direction = PinDirection::Output;
            p.kind = PinKind::Exec; p.type = ValueType::Exec; p.name = execOut; p.defaultValue = VisualValue::Exec();
            node.outputs.push_back(p);
        }
        for (const VisualFunctionParam& param : dataOut) {
            VisualPin p; p.id = reuseOrMint(oldOut, param.name); p.direction = PinDirection::Output;
            p.kind = PinKind::Data; p.type = param.type; p.name = param.name;
            p.defaultValue = VisualValue::MakeDefault(param.type);
            node.outputs.push_back(p);
        }
        std::unordered_set<PinId> alive;
        for (const VisualPin& p : node.inputs)  alive.insert(p.id);
        for (const VisualPin& p : node.outputs) alive.insert(p.id);
        auto& links = m_asset.links;
        links.erase(std::remove_if(links.begin(), links.end(), [&](const VisualLink& l) {
            if (l.fromNode == node.id && !alive.count(l.fromPin)) return true;
            if (l.toNode == node.id && !alive.count(l.toPin)) return true;
            return false;
        }), links.end());
    }
    // Re-derive Entry/Return pins for a function, and every same-graph Call node targeting it.
    void SyncFunctionInterfaceNodes(FunctionId fid) {
        const VisualFunction* fn = m_asset.FindFunction(fid);
        if (!fn) return;
        const std::vector<VisualFunctionParam> none;
        for (VisualNode& node : m_asset.nodes) {
            if (node.typeId == "Function.Entry" && node.functionId == fid)
                RebuildInterfacePins(node, "", "Then", none, fn->inputs);
            else if (node.typeId == "Function.Return" && node.functionId == fid)
                RebuildInterfacePins(node, "In", "", fn->outputs, none);
            else if (node.typeId == "Function.Call") {
                AssetHandle g;
                if (auto it = node.properties.find("graph"); it != node.properties.end()) g = it->second.AsAsset();
                const bool self = !(g.Valid() && !(g == m_asset.id));
                FunctionId target = kInvalidFunctionId;
                if (auto it = node.properties.find("func"); it != node.properties.end())
                    target = static_cast<FunctionId>(it->second.AsInt());
                if (self && target == fid)
                    RebuildInterfacePins(node, "In", "Then", fn->inputs, fn->outputs);
            }
        }
    }
    void RefreshMinters() {
        m_nextNodeId = m_asset.MaxNodeId();
        m_nextLinkId = m_asset.MaxLinkId();
        m_nextVariableId = m_asset.MaxVariableId();
        m_nextCommentId = m_asset.MaxCommentId();
        m_nextFunctionId = m_asset.MaxFunctionId();
        m_nextTypeId = m_asset.MaxTypeId();
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
    FunctionId m_nextFunctionId = 0;
    std::uint32_t m_nextTypeId = 0;            // struct/enum id minter (Milestone 4)
    FunctionId m_scope = kInvalidFunctionId;   // which function's nodes the canvas is editing

    inline static Clipboard s_clipboard;   // shared across documents (cross-graph paste)
};

} // namespace engine::vs
