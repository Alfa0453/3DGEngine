#include "VisualScriptEditorPanel.h"

#include <engine/visualscript/VisualScriptAsset.h>
#include <engine/visualscript/VisualScriptDiagnostics.h>
#include <engine/visualscript/VisualScriptValidator.h>

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <string>
#include <unordered_map>

using namespace engine::vs;

namespace {
const char* kValueTypeNames[] = {"Exec", "Bool", "Int", "Float", "String",
                                 "Vector2", "Vector3", "Entity", "Asset",
                                 "Quaternion", "Color", "ScriptHandle"};

ImU32 PinColor(ValueType t) {
    switch (t) {
        case ValueType::Exec:    return IM_COL32(230, 230, 230, 255);
        case ValueType::Bool:    return IM_COL32(180, 70, 70, 255);
        case ValueType::Int:     return IM_COL32(90, 180, 160, 255);
        case ValueType::Float:   return IM_COL32(120, 200, 90, 255);
        case ValueType::String:  return IM_COL32(200, 120, 190, 255);
        case ValueType::Vector2:
        case ValueType::Vector3: return IM_COL32(230, 190, 80, 255);
        case ValueType::Entity:  return IM_COL32(90, 150, 220, 255);
        case ValueType::Asset:   return IM_COL32(200, 160, 90, 255);
        default:                 return IM_COL32(160, 160, 170, 255);
    }
}

std::string ValueText(const VisualValue& value) {
    std::ostringstream out;
    switch (value.type) {
        case ValueType::Bool: out << (value.AsBool() ? "true" : "false"); break;
        case ValueType::Int: out << value.AsInt(); break;
        case ValueType::Float: out << value.AsFloat(); break;
        case ValueType::String: out << '"' << value.AsString() << '"'; break;
        case ValueType::Vector2: { const auto v = value.AsVec2(); out << '(' << v.x << ", " << v.y << ')'; break; }
        case ValueType::Vector3: { const auto v = value.AsVec3(); out << '(' << v.x << ", " << v.y << ", " << v.z << ')'; break; }
        case ValueType::Entity: out << "Entity " << value.AsEntity(); break;
        case ValueType::Asset: out << value.AsAsset().ToString(); break;
        case ValueType::Quaternion: { const auto q = value.AsQuat(); out << '(' << q.w << ", " << q.x << ", " << q.y << ", " << q.z << ')'; break; }
        case ValueType::Color: { const auto c = value.AsColor(); out << '(' << c.r << ", " << c.g << ", " << c.b << ", " << c.a << ')'; break; }
        case ValueType::ScriptHandle: out << "Script on Entity " << value.AsEntity(); break;
        default: out << "-"; break;
    }
    return out.str();
}
} // namespace

bool VisualScriptEditorPanel::Load(const std::string& path, std::string* error) {
    VisualScriptAsset asset;
    if (!asset.Load(path, error)) return false;
    m_document.Open(asset.id, asset);
    m_path = path;
    m_hasAsset = true;
    return true;
}

bool VisualScriptEditorPanel::Save(std::string* error) {
    if (!m_hasAsset) { if (error) *error = "No visual script open."; return false; }
    // Phase 17: validate before save; errors are still saved for recovery but reported.
    const ValidationReport report = m_document.Validate();
    if (!m_document.Asset().Save(m_path, error)) return false;
    m_document.MarkSaved();
    m_status = report.Ok() ? "Saved" : "Saved (graph has validation errors)";
    return true;
}

VisualScriptEditorPanel::Result VisualScriptEditorPanel::Draw(bool* open, const std::string& assetRoot) {
    Result result;
    VisualScriptNodeCatalog::Instance().EnsureBuilt();

    if (!m_pendingOpen.empty()) {
        std::string error;
        m_status = Load(m_pendingOpen, &error) ? ("Opened " + m_pendingOpen) : error;
        m_pendingOpen.clear();
    }

    if (!ImGui::Begin("Visual Script Editor", open)) { ImGui::End(); return result; }

    if (!m_hasAsset) {
        ImGui::TextUnformatted("Open a .3dgvs from the Content browser, or create one.");
        if (ImGui::Button("New Visual Script")) {
            VisualScriptAsset asset;
            asset.id = engine::AssetHandle::Generate();
            asset.graphId = 1;
            std::filesystem::path dir = std::filesystem::path(assetRoot) / "GameAssets" / "VisualScripts";
            std::error_code ec; std::filesystem::create_directories(dir, ec);
            m_path = (dir / "NewVisualScript.3dgvs").string();
            std::string error;
            if (asset.Save(m_path, &error)) {
                m_document.Open(asset.id, asset);
                m_hasAsset = true;
                result.assetsChanged = true;
                m_status = "Created " + m_path;
            } else { m_status = error; }
        }
        if (!m_status.empty()) ImGui::TextWrapped("%s", m_status.c_str());
        ImGui::End();
        return result;
    }

    DrawToolbar(result, assetRoot);
    DrawCanvas();
    ImGui::SameLine();
    DrawSidePanel();
    ImGui::End();
    return result;
}

void VisualScriptEditorPanel::DrawToolbar(Result& result, const std::string& /*assetRoot*/) {
    if (ImGui::Button("Save")) { std::string error; if (Save(&error)) result.assetsChanged = true; else m_status = error; }
    ImGui::SameLine(); ImGui::BeginDisabled(!m_document.CanUndo()); if (ImGui::Button("Undo")) m_document.Undo(); ImGui::EndDisabled();
    ImGui::SameLine(); ImGui::BeginDisabled(!m_document.CanRedo()); if (ImGui::Button("Redo")) m_document.Redo(); ImGui::EndDisabled();
    ImGui::SameLine(); if (ImGui::Button("Copy")) m_document.CopySelection();
    ImGui::SameLine(); ImGui::BeginDisabled(!VisualScriptEditorDocument::ClipboardHasContent());
    if (ImGui::Button("Paste")) m_document.Paste(glm::vec2(24.0f)); ImGui::EndDisabled();
    ImGui::SameLine(); if (ImGui::Button("Duplicate")) m_document.Duplicate(glm::vec2(24.0f));
    ImGui::SameLine(); if (ImGui::Button("Delete")) m_document.DeleteSelected();
    ImGui::SameLine();
    ImGui::TextColored(m_document.Dirty() ? ImVec4(1, 0.7f, 0.2f, 1) : ImVec4(0.5f, 0.8f, 0.5f, 1),
        m_document.Dirty() ? "Unsaved" : "Saved");
    if (!m_status.empty()) { ImGui::SameLine(); ImGui::TextDisabled("| %s", m_status.c_str()); }

    // Keyboard shortcuts (Phase 14/15) while the panel is focused.
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        const ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)) m_document.Undo();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y)) m_document.Redo();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C)) m_document.CopySelection();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V)) m_document.Paste(glm::vec2(24.0f));
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D)) m_document.Duplicate(glm::vec2(24.0f));
        if (ImGui::IsKeyPressed(ImGuiKey_Delete)) m_document.DeleteSelected();
    }
}

void VisualScriptEditorPanel::DrawCanvas() {
    ImGui::BeginChild("VSCanvas", ImVec2(-320.0f, 0.0f), true,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size = ImGui::GetContentRegionAvail();
    ImGui::InvisibleButton("VSBackground", size,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();

    float& zoom = m_document.zoom;
    glm::vec2& pan = m_document.pan;
    if (hovered && ImGui::GetIO().MouseWheel != 0.0f) {
        const float old = zoom;
        zoom = std::clamp(zoom * std::pow(1.12f, ImGui::GetIO().MouseWheel), 0.35f, 2.5f);
        const ImVec2 m = ImGui::GetIO().MousePos;
        pan.x = m.x - origin.x - (m.x - origin.x - pan.x) * (zoom / old);
        pan.y = m.y - origin.y - (m.y - origin.y - pan.y) * (zoom / old);
    }
    if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        pan.x += ImGui::GetIO().MouseDelta.x; pan.y += ImGui::GetIO().MouseDelta.y;
    }

    auto screen = [&](glm::vec2 p) { return ImVec2(origin.x + pan.x + p.x * zoom, origin.y + pan.y + p.y * zoom); };
    auto graph = [&](ImVec2 s) { return glm::vec2((s.x - origin.x - pan.x) / zoom, (s.y - origin.y - pan.y) / zoom); };

    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, {origin.x + size.x, origin.y + size.y}, IM_COL32(24, 27, 33, 255));
    const float grid = 28.0f * zoom;
    for (float x = std::fmod(pan.x, grid); x < size.x; x += grid)
        draw->AddLine({origin.x + x, origin.y}, {origin.x + x, origin.y + size.y}, IM_COL32(44, 49, 59, 110));
    for (float y = std::fmod(pan.y, grid); y < size.y; y += grid)
        draw->AddLine({origin.x, origin.y + y}, {origin.x + size.x, origin.y + y}, IM_COL32(44, 49, 59, 110));

    VisualScriptAsset& asset = m_document.MutableAsset();
    VisualScriptDiagnostics& diagnostics = VisualScriptDiagnostics::Instance();
    const PausedExecution& pausedExecution = diagnostics.Paused();
    NodeId latestNode = kInvalidNodeId;
    for (auto it = diagnostics.Highlights().rbegin(); it != diagnostics.Highlights().rend(); ++it) {
        if (it->graph == asset.id) { latestNode = it->node; break; }
    }
    const float nodeWidth = 180.0f;
    const float headerH = 22.0f;
    const float rowH = 20.0f;

    // Frame-scoped pin screen positions for link routing + drag completion.
    std::unordered_map<std::uint64_t, ImVec2> pinPos;
    std::unordered_map<std::uint64_t, ValueType> pinType;
    std::unordered_map<std::uint64_t, PinKind> pinKind;
    std::unordered_map<std::uint64_t, bool> pinIsOutput;
    auto key = [](NodeId n, PinId p) { return (static_cast<std::uint64_t>(n) << 32) | p; };

    // ---- draw comments (behind nodes) --------------------------------------
    for (const VisualComment& comment : asset.comments) {
        const ImVec2 a = screen(comment.position);
        const ImVec2 b = screen(comment.position + comment.size);
        draw->AddRectFilled(a, b, IM_COL32(60, 90, 120, 40), 4.0f);
        draw->AddRect(a, b, IM_COL32(90, 140, 190, 160), 4.0f);
        draw->AddText({a.x + 6, a.y + 4}, IM_COL32(200, 220, 240, 220),
                      comment.text.empty() ? "Comment" : comment.text.c_str());
    }

    // ---- draw nodes --------------------------------------------------------
    bool openNodeMenu = false;   // set when a node is right-clicked
    for (VisualNode& node : asset.nodes) {
        const int rows = std::max(1, std::max(static_cast<int>(node.inputs.size()),
                                               static_cast<int>(node.outputs.size())));
        const float h = headerH + rows * rowH + 8.0f;
        const ImVec2 a = screen(node.editorPosition);
        const ImVec2 b = ImVec2(a.x + nodeWidth * zoom, a.y + h * zoom);
        const bool selected = m_document.IsSelected(node.id);
        const bool breakpoint = diagnostics.HasBreakpoint(asset.id, node.id);
        const bool pausedHere = pausedExecution.active && pausedExecution.graph == asset.id
                             && pausedExecution.node == node.id;
        const bool recentlyExecuted = latestNode == node.id;
        draw->AddRectFilled(a, b, IM_COL32(38, 42, 51, 245), 5.0f);
        const ImU32 border = pausedHere ? IM_COL32(255, 205, 55, 255)
                           : recentlyExecuted ? IM_COL32(80, 225, 125, 255)
                           : selected ? IM_COL32(240, 190, 90, 255) : IM_COL32(80, 86, 98, 255);
        draw->AddRect(a, b, border, 5.0f, 0, (pausedHere || recentlyExecuted || selected) ? 2.5f : 1.0f);
        draw->AddRectFilled(a, {b.x, a.y + headerH * zoom}, IM_COL32(52, 58, 70, 255), 5.0f, ImDrawFlags_RoundCornersTop);
        if (breakpoint) {
            draw->AddCircleFilled({a.x + 8.0f * zoom, a.y + 11.0f * zoom}, 5.0f * zoom,
                                  IM_COL32(235, 65, 65, 255));
        }
        const NodeDescriptor* desc = VisualNodeRegistry::Instance().Find(node.typeId);
        // Variable Get/Set nodes show the bound variable's name (e.g. "Get Score" / "Set Score").
        std::string title = desc ? desc->displayName : node.typeId;
        if (node.typeId == "Var.Get" || node.typeId == "Var.Set") {
            auto it = node.properties.find("var");
            if (it != node.properties.end()) {
                const VisualVariable* var = asset.FindVariable(static_cast<VariableId>(it->second.AsInt()));
                if (var) title = (node.typeId == "Var.Set" ? "Set " : "Get ") + var->name;
            }
        }
        draw->AddText({a.x + (breakpoint ? 17.0f : 6.0f) * zoom, a.y + 4},
                      IM_COL32(235, 238, 245, 255), title.c_str());
        if (!desc)   // Phase 16: missing node type is visible on the node itself
            draw->AddText({a.x + 6, b.y - 16}, IM_COL32(230, 110, 110, 255), "missing type");

        auto drawPins = [&](std::vector<VisualPin>& pins, bool output) {
            for (std::size_t i = 0; i < pins.size(); ++i) {
                VisualPin& pin = pins[i];
                const float py = a.y + (headerH + i * rowH + rowH * 0.5f) * zoom;
                const float px = output ? b.x : a.x;
                const ImVec2 c(px, py);
                draw->AddCircleFilled(c, 4.5f * zoom, PinColor(pin.type));
                const std::uint64_t k = key(node.id, pin.id);
                pinPos[k] = c; pinType[k] = pin.type; pinKind[k] = pin.kind; pinIsOutput[k] = output;
                const ImVec2 label(output ? px - 8 - ImGui::CalcTextSize(pin.name.c_str()).x : px + 8, py - 7);
                draw->AddText(label, IM_COL32(200, 205, 214, 255), pin.name.c_str());

                // pin hit test for link authoring
                const ImVec2 m = ImGui::GetIO().MousePos;
                const bool overPin = std::fabs(m.x - c.x) < 7 && std::fabs(m.y - c.y) < 7;
                if (overPin && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    m_dragging = true; m_dragNode = node.id; m_dragPin = pin.id; m_dragFromOutput = output;
                    m_dragPinType = pin.type; m_dragPinKind = pin.kind; m_linkError.clear();
                }
                if (m_dragging && overPin && ImGui::IsMouseReleased(ImGuiMouseButton_Left)
                    && node.id != m_dragNode) {
                    NodeId fn = m_dragFromOutput ? m_dragNode : node.id;
                    PinId  fp = m_dragFromOutput ? m_dragPin : pin.id;
                    NodeId tn = m_dragFromOutput ? node.id : m_dragNode;
                    PinId  tp = m_dragFromOutput ? pin.id : m_dragPin;
                    std::string why;
                    if (!m_document.Connect(fn, fp, tn, tp, &why)) m_linkError = why;   // Phase 7 feedback
                    m_dragging = false;   // consume the release so it doesn't also open the search
                }
            }
        };
        drawPins(node.inputs, false);
        drawPins(node.outputs, true);

        // Inline default-value editors for UNCONNECTED data inputs (Phases 8/9/10).
        for (std::size_t i = 0; i < node.inputs.size(); ++i) {
            VisualPin& pin = node.inputs[i];
            if (pin.kind != PinKind::Data) continue;
            const bool connected = std::any_of(asset.links.begin(), asset.links.end(),
                [&](const VisualLink& l) { return l.toNode == node.id && l.toPin == pin.id; });
            if (connected) continue;
            const float py = a.y + (headerH + i * rowH) * zoom;
            ImGui::SetCursorScreenPos({a.x + 6, py});
            // Unique ImGui id per (node, pin) — using only the pin id collides across nodes and
            // steals input, so pin editors on other nodes appear uneditable.
            ImGui::PushID(static_cast<int>(node.id));
            ImGui::PushID(static_cast<int>(pin.id));
            ImGui::SetNextItemWidth((nodeWidth - 12.0f) * zoom);
            VisualValue v = pin.defaultValue;
            bool changed = false;
            switch (pin.type) {
                case ValueType::Bool:   { bool dv = v.AsBool(); if (ImGui::Checkbox("##d", &dv)) { v = VisualValue::Bool(dv); changed = true; } break; }
                case ValueType::Int:    { int dv = v.AsInt(); if (ImGui::DragInt("##d", &dv)) { v = VisualValue::Int(dv); changed = true; } break; }
                case ValueType::Float:  { float dv = v.AsFloat(); if (ImGui::DragFloat("##d", &dv, 0.05f)) { v = VisualValue::Float(dv); changed = true; } break; }
                case ValueType::Vector3:{ glm::vec3 dv = v.AsVec3(); if (ImGui::DragFloat3("##d", &dv.x, 0.05f)) { v = VisualValue::Vec3(dv); changed = true; } break; }
                case ValueType::String: { std::array<char,128> buf{}; std::snprintf(buf.data(), buf.size(), "%s", v.AsString().c_str());
                                          if (ImGui::InputText("##d", buf.data(), buf.size())) { v = VisualValue::Str(buf.data()); changed = true; } break; }
                case ValueType::Entity: { int dv = static_cast<int>(v.AsEntity()); if (ImGui::DragInt("##d", &dv)) { v = VisualValue::Ent(static_cast<engine::ecs::Entity>(dv)); changed = true; } break; }
                default: break;
            }
            if (changed) m_document.SetPinDefault(node.id, pin.id, v);
            ImGui::PopID();
            ImGui::PopID();
        }

        // node body click = select / move (drag) with undo bracketing.
        const ImVec2 m = ImGui::GetIO().MousePos;
        const bool overBody = m.x >= a.x && m.x <= b.x && m.y >= a.y && m.y <= a.y + headerH * zoom;
        if (overBody && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !m_dragging) {
            m_document.Select(node.id, ImGui::GetIO().KeyCtrl);
            m_movingNodes = true; m_document.PushUndo();
        }
        // Right-click anywhere on the node opens its context menu (Delete/Copy/Duplicate/...).
        const bool overNode = m.x >= a.x && m.x <= b.x && m.y >= a.y && m.y <= b.y;
        if (overNode && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            if (!m_document.IsSelected(node.id)) m_document.Select(node.id, false);
            openNodeMenu = true;
        }
    }

    // ---- draw links --------------------------------------------------------
    for (const VisualLink& link : asset.links) {
        auto f = pinPos.find(key(link.fromNode, link.fromPin));
        auto t = pinPos.find(key(link.toNode, link.toPin));
        if (f == pinPos.end() || t == pinPos.end()) continue;
        const ImVec2 from = f->second, to = t->second;
        draw->AddBezierCubic(from, {from.x + 60 * zoom, from.y}, {to.x - 60 * zoom, to.y}, to,
            PinColor(pinType[key(link.fromNode, link.fromPin)]), 2.5f * zoom);
    }

    // ---- in-progress drag preview -----------------------------------------
    if (m_dragging) {
        auto it = pinPos.find(key(m_dragNode, m_dragPin));
        if (it != pinPos.end()) {
            const ImVec2 from = it->second, to = ImGui::GetIO().MousePos;
            draw->AddBezierCubic(from, {from.x + 60 * zoom, from.y}, {to.x - 60 * zoom, to.y}, to, IM_COL32(220, 220, 120, 200), 2.0f);
        }
        // Released over empty canvas: open a compatible-node search and auto-connect (Phase 6).
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            m_dragging = false;
            m_contextFromPin = true;
            m_contextGraphPos = graph(ImGui::GetIO().MousePos);
            m_contextOpen = true;
            m_searchBuffer[0] = '\0';
            ImGui::OpenPopup("VSContextSearch");
        }
    }
    if (m_movingNodes) {
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
            m_document.MoveSelected(glm::vec2(ImGui::GetIO().MouseDelta.x, ImGui::GetIO().MouseDelta.y) / zoom);
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) m_movingNodes = false;
    }
    if (!m_linkError.empty())
        draw->AddText({origin.x + 8, origin.y + size.y - 20}, IM_COL32(240, 120, 120, 255), m_linkError.c_str());

    // Node context menu takes priority over the empty-canvas add-search.
    if (openNodeMenu) ImGui::OpenPopup("VSNodeMenu");
    if (ImGui::BeginPopup("VSNodeMenu")) {
        const int selCount = static_cast<int>(m_document.Selection().size());
        ImGui::TextDisabled("%d node(s)", selCount);
        ImGui::Separator();
        if (ImGui::MenuItem("Copy", "Ctrl+C")) m_document.CopySelection();
        if (ImGui::MenuItem("Duplicate", "Ctrl+D")) m_document.Duplicate(glm::vec2(24.0f));
        if (ImGui::MenuItem("Cut", "Ctrl+X")) { m_document.CopySelection(); m_document.DeleteSelected(); }
        if (m_document.Selection().size() == 1) {
            const NodeId selectedNode = *m_document.Selection().begin();
            const bool hasBreakpoint = diagnostics.HasBreakpoint(asset.id, selectedNode);
            if (ImGui::MenuItem(hasBreakpoint ? "Remove Breakpoint" : "Add Breakpoint", "F9"))
                diagnostics.SetBreakpoint(asset.id, selectedNode, !hasBreakpoint);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Delete", "Del")) m_document.DeleteSelected();
        ImGui::EndPopup();
    }

    // empty-canvas interactions: right-click search, left-click clears selection.
    if (hovered && !openNodeMenu && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        m_contextGraphPos = graph(ImGui::GetIO().MousePos);
        m_contextOpen = true;
        m_contextFromPin = false;   // plain add (unfiltered)
        m_searchBuffer[0] = '\0';
        ImGui::OpenPopup("VSContextSearch");
    }
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !m_dragging && !m_movingNodes
        && !ImGui::GetIO().KeyCtrl)
        m_document.ClearSelection();

    DrawContextSearch(m_contextGraphPos);
    ImGui::EndChild();
}

void VisualScriptEditorPanel::DrawContextSearch(const glm::vec2& graphPos) {
    if (ImGui::BeginPopup("VSContextSearch")) {
        VisualScriptNodeCatalog& catalog = VisualScriptNodeCatalog::Instance();
        ImGui::TextUnformatted(m_contextFromPin ? "Add Node (compatible)" : "Add Node");
        ImGui::SetNextItemWidth(240.0f);
        if (m_contextOpen) { ImGui::SetKeyboardFocusHere(); m_contextOpen = false; }
        ImGui::InputTextWithHint("##vssearch", "Search nodes...", m_searchBuffer.data(), m_searchBuffer.size());
        ImGui::BeginChild("VSResults", ImVec2(300, 320), true);

        // When dragged from a pin, only show nodes that have a pin able to join it (Phase 6).
        const PinDirection sourceDir = m_dragFromOutput ? PinDirection::Output : PinDirection::Input;
        const std::vector<const CatalogEntry*> results = m_contextFromPin
            ? catalog.SearchForPin(m_searchBuffer.data(), m_dragPinType, m_dragPinKind, sourceDir)
            : catalog.Search(m_searchBuffer.data());

        // Results are sorted by category then name, so a header per category groups them.
        std::string currentCategory;
        for (const CatalogEntry* e : results) {
            if (e->category != currentCategory) {
                currentCategory = e->category;
                ImGui::SeparatorText(currentCategory.c_str());
            }
            ImGui::Indent(8.0f);
            if (ImGui::Selectable(e->displayName.c_str())) {
                const NodeId newNode = m_document.AddNode(e->typeId, graphPos);
                if (m_contextFromPin) AutoConnectDraggedPin(newNode);
                m_contextFromPin = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::Unindent(8.0f);
        }
        ImGui::EndChild();
        ImGui::EndPopup();
    } else {
        m_contextFromPin = false;   // popup dismissed without a pick
    }
}

// Connect the pin the user dragged from to the first compatible pin on the freshly added node.
void VisualScriptEditorPanel::AutoConnectDraggedPin(NodeId newNode) {
    const VisualNode* node = m_document.Asset().FindNode(newNode);
    if (!node) return;
    // The dragged pin is an output -> we need an input on the new node (and vice-versa).
    const std::vector<VisualPin>& candidates = m_dragFromOutput ? node->inputs : node->outputs;
    for (const VisualPin& p : candidates) {
        if (p.kind != m_dragPinKind) continue;
        bool compatible = (m_dragPinKind == PinKind::Exec);
        if (!compatible) {
            compatible = m_dragFromOutput ? DataTypesCompatible(m_dragPinType, p.type)
                                          : DataTypesCompatible(p.type, m_dragPinType);
        }
        if (!compatible) continue;
        std::string why;
        const NodeId fromNode = m_dragFromOutput ? m_dragNode : newNode;
        const PinId  fromPin  = m_dragFromOutput ? m_dragPin : p.id;
        const NodeId toNode   = m_dragFromOutput ? newNode : m_dragNode;
        const PinId  toPin    = m_dragFromOutput ? p.id : m_dragPin;
        if (m_document.Connect(fromNode, fromPin, toNode, toPin, &why)) return;   // first that connects wins
    }
}

void VisualScriptEditorPanel::DrawSidePanel() {
    ImGui::BeginChild("VSDetails", ImVec2(0, 0), true);

    VisualScriptDiagnostics& diagnostics = VisualScriptDiagnostics::Instance();
    const engine::AssetHandle graphId = m_document.Asset().id;

    ImGui::SeparatorText("Graph");
    ImGui::Text("Nodes: %d   Links: %d",
        static_cast<int>(m_document.Asset().nodes.size()),
        static_cast<int>(m_document.Asset().links.size()));

    // ---- live debugger ----------------------------------------------------
    ImGui::SeparatorText("Debugger");
    ImGui::TextColored(diagnostics.enabled ? ImVec4(0.45f, 0.9f, 0.55f, 1.0f)
                                             : ImVec4(0.65f, 0.68f, 0.72f, 1.0f),
                       diagnostics.enabled ? "Attached to Play mode" : "Start Play mode to debug");
    const PausedExecution& paused = diagnostics.Paused();
    if (paused.active) {
        const bool thisGraph = paused.graph == graphId;
        ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.2f, 1.0f),
                           thisGraph ? "Paused at node %u (Entity %u)" : "Another visual graph is paused",
                           static_cast<unsigned>(paused.node), static_cast<unsigned>(paused.entity));
        if (ImGui::Button("Continue##vsdebug")) diagnostics.ContinueExecution();
        ImGui::SameLine();
        if (ImGui::Button("Step Node##vsdebug")) diagnostics.StepExecution();
        if (thisGraph && !paused.callStack.empty() && ImGui::TreeNode("Call Stack")) {
            for (auto it = paused.callStack.rbegin(); it != paused.callStack.rend(); ++it) {
                ImGui::PushID(static_cast<int>(it->node));
                if (ImGui::Selectable((it->nodeType + "  [" + std::to_string(it->node) + "]").c_str())) {
                    m_document.Select(it->node, false);
                    if (const VisualNode* n = m_document.Asset().FindNode(it->node))
                        m_document.pan = glm::vec2(60.0f) - n->editorPosition * m_document.zoom;
                }
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
    }

    int breakpointCount = 0;
    for (const VisualNode& node : m_document.Asset().nodes)
        if (diagnostics.HasBreakpoint(graphId, node.id)) ++breakpointCount;
    ImGui::Text("Breakpoints: %d", breakpointCount);
    ImGui::SameLine();
    ImGui::BeginDisabled(breakpointCount == 0);
    if (ImGui::SmallButton("Clear##vsbreakpoints"))
        for (const VisualNode& node : m_document.Asset().nodes)
            diagnostics.SetBreakpoint(graphId, node.id, false);
    ImGui::EndDisabled();
    ImGui::TextDisabled("Right-click a node or press F9 to toggle a breakpoint.");

    if (m_document.Selection().size() == 1) {
        const NodeId selectedNode = *m_document.Selection().begin();
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_F9)) {
            const bool on = diagnostics.HasBreakpoint(graphId, selectedNode);
            diagnostics.SetBreakpoint(graphId, selectedNode, !on);
        }
        const GraphProfile* graphProfile = nullptr;
        for (const auto& entry : diagnostics.Graphs())
            if (entry.second.graph == graphId) { graphProfile = &entry.second; break; }
        if (graphProfile) {
            auto profile = graphProfile->nodes.find(selectedNode);
            if (profile != graphProfile->nodes.end()) {
                const NodeProfile& p = profile->second;
                ImGui::Text("Selected timing: %.4f ms last", p.lastMs);
                ImGui::TextDisabled("%.4f ms avg | %.4f ms max | %llu calls", p.AverageMs(), p.maxMs,
                                    static_cast<unsigned long long>(p.calls));
            }
        }
        const NodeValueSnapshot& values = diagnostics.Values();
        if (values.graph == graphId && values.node == selectedNode) {
            ImGui::Text("Last outputs (Entity %u)", static_cast<unsigned>(values.entity));
            if (values.outputs.empty()) ImGui::TextDisabled("No data outputs.");
            for (const auto& value : values.outputs)
                ImGui::BulletText("%s = %s", value.first.c_str(), ValueText(value.second).c_str());
            if (!values.variables.empty() && ImGui::TreeNode("Live Variables")) {
                for (const auto& value : values.variables)
                    ImGui::BulletText("%s = %s", value.first.c_str(), ValueText(value.second).c_str());
                ImGui::TreePop();
            }
        }
    }

    int graphErrors = 0;
    for (const VsErrorRecord& error : diagnostics.Errors()) if (error.graph == graphId) ++graphErrors;
    if (graphErrors > 0 && ImGui::TreeNodeEx("Runtime Errors", ImGuiTreeNodeFlags_DefaultOpen,
                                            "Runtime Errors (%d)", graphErrors)) {
        for (std::size_t i = 0; i < diagnostics.Errors().size(); ++i) {
            const VsErrorRecord& error = diagnostics.Errors()[i];
            if (error.graph != graphId) continue;
            ImGui::PushID(static_cast<int>(i));
            ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.35f, 1.0f), "%s", error.message.c_str());
            ImGui::TextDisabled("%s | node %u | entity %u", error.nodeType.c_str(),
                                static_cast<unsigned>(error.nodeId), static_cast<unsigned>(error.entity));
            if (ImGui::IsItemClicked() && error.nodeId != kInvalidNodeId) {
                m_document.Select(error.nodeId, false);
                if (const VisualNode* n = m_document.Asset().FindNode(error.nodeId))
                    m_document.pan = glm::vec2(60.0f) - n->editorPosition * m_document.zoom;
            }
            if (!error.callStack.empty() && ImGui::TreeNode("Error Call Stack")) {
                for (const CallFrame& frame : error.callStack)
                    ImGui::BulletText("%s [%u]", frame.nodeType.c_str(), static_cast<unsigned>(frame.node));
                ImGui::TreePop();
            }
            ImGui::Separator();
            ImGui::PopID();
        }
        if (ImGui::SmallButton("Clear Errors##vsdebug")) diagnostics.ClearErrors();
        ImGui::TreePop();
    }

    // ---- selected node inspector (edits the SAME pin data as the canvas, so both stay in sync) ----
    if (m_document.Selection().size() == 1) {
        const NodeId sel = *m_document.Selection().begin();
        if (const VisualNode* node = m_document.Asset().FindNode(sel)) {
            const NodeDescriptor* d = VisualNodeRegistry::Instance().Find(node->typeId);
            ImGui::SeparatorText(d ? d->displayName.c_str() : node->typeId.c_str());
            bool any = false;
            for (const VisualPin& pin : node->inputs) {
                if (pin.kind != PinKind::Data) continue;
                const bool connected = std::any_of(m_document.Asset().links.begin(), m_document.Asset().links.end(),
                    [&](const VisualLink& l) { return l.toNode == node->id && l.toPin == pin.id; });
                any = true;
                ImGui::PushID(static_cast<int>(pin.id));
                if (connected) { ImGui::TextDisabled("%s (linked)", pin.name.c_str()); ImGui::PopID(); continue; }
                VisualValue v = pin.defaultValue;
                bool changed = false;
                ImGui::SetNextItemWidth(180.0f);
                switch (pin.type) {
                    case ValueType::Bool:    { bool b = v.AsBool(); if (ImGui::Checkbox(pin.name.c_str(), &b)) { v = VisualValue::Bool(b); changed = true; } break; }
                    case ValueType::Int:     { int b = v.AsInt(); if (ImGui::DragInt(pin.name.c_str(), &b)) { v = VisualValue::Int(b); changed = true; } break; }
                    case ValueType::Float:   { float b = v.AsFloat(); if (ImGui::DragFloat(pin.name.c_str(), &b, 0.05f)) { v = VisualValue::Float(b); changed = true; } break; }
                    case ValueType::Vector2: { glm::vec2 b = v.AsVec2(); if (ImGui::DragFloat2(pin.name.c_str(), &b.x, 0.05f)) { v = VisualValue::Vec2(b); changed = true; } break; }
                    case ValueType::Vector3: { glm::vec3 b = v.AsVec3(); if (ImGui::DragFloat3(pin.name.c_str(), &b.x, 0.05f)) { v = VisualValue::Vec3(b); changed = true; } break; }
                    case ValueType::Color:   { glm::vec4 b = v.AsColor(); if (ImGui::ColorEdit4(pin.name.c_str(), &b.x)) { v = VisualValue::Col(b); changed = true; } break; }
                    case ValueType::String:  { std::array<char,128> b{}; std::snprintf(b.data(), b.size(), "%s", v.AsString().c_str());
                                               if (ImGui::InputText(pin.name.c_str(), b.data(), b.size(), ImGuiInputTextFlags_EnterReturnsTrue)) { v = VisualValue::Str(b.data()); changed = true; } break; }
                    default: ImGui::TextDisabled("%s (%s)", pin.name.c_str(), kValueTypeNames[static_cast<int>(pin.type)]); break;
                }
                if (changed) m_document.SetPinDefault(node->id, pin.id, v);
                ImGui::PopID();
            }
            if (!any) ImGui::TextDisabled("No editable inputs.");
        }
    }

    // ---- variables (Phases 11/12) -----------------------------------------
    ImGui::SeparatorText("Variables");
    ImGui::SetNextItemWidth(120.0f);
    ImGui::InputText("##varname", m_newVariableName.data(), m_newVariableName.size());
    ImGui::SameLine(); ImGui::SetNextItemWidth(90.0f);
    ImGui::Combo("##vartype", &m_newVariableType, kValueTypeNames, IM_ARRAYSIZE(kValueTypeNames));
    ImGui::SameLine();
    if (ImGui::Button("Add##var"))
        m_document.CreateVariable(m_newVariableName.data(), static_cast<ValueType>(m_newVariableType));

    VariableId deleteVar = kInvalidVariableId;
    for (const VisualVariable& var : m_document.Asset().variables) {
        ImGui::PushID(static_cast<int>(var.id));

        // Rename field: keep the in-progress text in a persistent buffer so typing isn't wiped by the
        // per-frame reseed; commit only when the field loses focus (Phase 12 — id preserved).
        std::array<char, 64> name{};
        if (m_editingVarId == var.id) std::snprintf(name.data(), name.size(), "%s", m_varEditBuffer.data());
        else                          std::snprintf(name.data(), name.size(), "%s", var.name.c_str());
        ImGui::SetNextItemWidth(120.0f);
        ImGui::InputText("##rn", name.data(), name.size());
        if (ImGui::IsItemActive()) {
            m_editingVarId = var.id;
            std::snprintf(m_varEditBuffer.data(), m_varEditBuffer.size(), "%s", name.data());
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            m_document.RenameVariable(var.id, name.data());
            m_editingVarId = kInvalidVariableId;
        }
        ImGui::SameLine(); ImGui::TextDisabled("%s", kValueTypeNames[static_cast<int>(var.type)]);
        ImGui::SameLine(); if (ImGui::SmallButton("Get")) m_document.AddVariableNode(false, var.id, m_document.pan * -1.0f + glm::vec2(40.0f));
        ImGui::SameLine(); if (ImGui::SmallButton("Set")) m_document.AddVariableNode(true, var.id, m_document.pan * -1.0f + glm::vec2(40.0f, 80.0f));
        ImGui::SameLine(); if (ImGui::SmallButton("X")) deleteVar = var.id;

        // Default-value editor (immediate widgets — no reset bug). Sets the variable's initial value.
        VisualValue dv = var.defaultValue;
        bool changed = false;
        ImGui::SetNextItemWidth(200.0f);
        switch (var.type) {
            case ValueType::Bool:    { bool b = dv.AsBool(); if (ImGui::Checkbox("Value##vv", &b)) { dv = VisualValue::Bool(b); changed = true; } break; }
            case ValueType::Int:     { int b = dv.AsInt(); if (ImGui::DragInt("Value##vv", &b)) { dv = VisualValue::Int(b); changed = true; } break; }
            case ValueType::Float:   { float b = dv.AsFloat(); if (ImGui::DragFloat("Value##vv", &b, 0.05f)) { dv = VisualValue::Float(b); changed = true; } break; }
            case ValueType::Vector2: { glm::vec2 b = dv.AsVec2(); if (ImGui::DragFloat2("Value##vv", &b.x, 0.05f)) { dv = VisualValue::Vec2(b); changed = true; } break; }
            case ValueType::Vector3: { glm::vec3 b = dv.AsVec3(); if (ImGui::DragFloat3("Value##vv", &b.x, 0.05f)) { dv = VisualValue::Vec3(b); changed = true; } break; }
            case ValueType::Color:   { glm::vec4 b = dv.AsColor(); if (ImGui::ColorEdit4("Value##vv", &b.x)) { dv = VisualValue::Col(b); changed = true; } break; }
            case ValueType::String:  { std::array<char,128> b{}; std::snprintf(b.data(), b.size(), "%s", dv.AsString().c_str());
                                       if (ImGui::InputText("Value##vv", b.data(), b.size(), ImGuiInputTextFlags_EnterReturnsTrue)) { dv = VisualValue::Str(b.data()); changed = true; } break; }
            default: break;
        }
        if (changed) m_document.SetVariableDefault(var.id, dv);

        ImGui::Separator();
        ImGui::PopID();
    }
    if (deleteVar != kInvalidVariableId) m_document.DeleteVariable(deleteVar);

    // ---- validation (Phase 16) --------------------------------------------
    ImGui::SeparatorText("Validation");
    const ValidationReport report = m_document.Validate();
    if (report.Ok()) ImGui::TextColored(ImVec4(0.5f, 0.85f, 0.5f, 1), "No errors");
    for (const ValidationIssue& issue : report.issues) {
        const bool err = issue.severity == ValidationIssue::Severity::Error;
        ImGui::TextColored(err ? ImVec4(0.95f, 0.45f, 0.4f, 1) : ImVec4(0.95f, 0.8f, 0.35f, 1),
            "%s", issue.message.c_str());
        if (issue.nodeId != kInvalidNodeId && ImGui::IsItemClicked()) {
            m_document.Select(issue.nodeId, false);   // double-click-to-focus foundation (Phase 16)
            if (const VisualNode* n = m_document.Asset().FindNode(issue.nodeId))
                m_document.pan = glm::vec2(60.0f) - n->editorPosition * m_document.zoom;
        }
    }

    ImGui::EndChild();
}
