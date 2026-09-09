#include "VisualScriptEditorPanel.h"

#include <engine/visualscript/VisualScriptAsset.h>
#include <engine/visualscript/VisualScriptCompiler.h>   // Milestone 9: compiled-execution toggle
#include <engine/visualscript/VisualScriptDiagnostics.h>
#include <engine/visualscript/VisualScriptValidator.h>

#include <imgui.h>

#include <algorithm>
#include <cctype>
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
                                 "Quaternion", "Color", "ScriptHandle",
                                 "Array", "Map", "Struct", "Enum"};

ImU32 PinColor(ValueType t) {
    switch (t) {
        case ValueType::Exec:         return IM_COL32(230, 230, 230, 255);
        case ValueType::Bool:         return IM_COL32(180, 70, 70, 255);
        case ValueType::Int:          return IM_COL32(90, 180, 160, 255);
        case ValueType::Float:        return IM_COL32(120, 200, 90, 255);
        case ValueType::String:       return IM_COL32(200, 120, 190, 255);
        case ValueType::Vector2:      return IM_COL32(235, 210, 120, 255);
        case ValueType::Vector3:      return IM_COL32(230, 190, 80, 255);
        case ValueType::Entity:       return IM_COL32(90, 150, 220, 255);
        case ValueType::Asset:        return IM_COL32(200, 160, 90, 255);
        case ValueType::Quaternion:   return IM_COL32(150, 130, 220, 255);
        case ValueType::Color:        return IM_COL32(235, 130, 90, 255);
        case ValueType::ScriptHandle: return IM_COL32(120, 190, 220, 255);
        default:                      return IM_COL32(160, 160, 170, 255);
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

VisualScriptEditorPanel::Result VisualScriptEditorPanel::Draw(bool* open, const std::string& assetRoot,
                                                              const vswidgets::ValueWidgetContext& valueCtx) {
    Result result;
    // Build this frame's value-editor context: borrowed scene/asset data + this graph's type defs.
    m_localCtx = valueCtx;
    m_localCtx.structs = &m_document.Asset().structs;
    m_localCtx.enums   = &m_document.Asset().enums;
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

    // ---- Milestone 2: navigation & layout row -----------------------------
    ImGui::Separator();
    if (ImGui::Button("Frame All")) m_frameRequest = 1;
    ImGui::SameLine(); ImGui::BeginDisabled(m_document.Selection().empty());
    if (ImGui::Button("Frame Selection")) m_frameRequest = 2;
    ImGui::EndDisabled();
    ImGui::SameLine(); if (ImGui::Button("Add Comment")) {
        const glm::vec2 at = (glm::vec2(80.0f) - m_document.pan) / m_document.zoom;
        m_selectedComment = m_document.AddComment(at);
    }

    const int selCount = static_cast<int>(m_document.Selection().size());
    ImGui::SameLine(); ImGui::TextDisabled("|");
    ImGui::SameLine(); ImGui::BeginDisabled(selCount < 2);
    if (ImGui::Button("Align")) ImGui::OpenPopup("VSAlignMenu");
    ImGui::EndDisabled();
    if (ImGui::BeginPopup("VSAlignMenu")) {
        if (ImGui::MenuItem("Left"))          m_document.AlignSelected(0);
        if (ImGui::MenuItem("Center (H)"))    m_document.AlignSelected(1);
        if (ImGui::MenuItem("Right"))         m_document.AlignSelected(2);
        if (ImGui::MenuItem("Top"))           m_document.AlignSelected(3);
        if (ImGui::MenuItem("Center (V)"))    m_document.AlignSelected(4);
        if (ImGui::MenuItem("Bottom"))        m_document.AlignSelected(5);
        ImGui::Separator();
        if (ImGui::MenuItem("Distribute Horizontally")) m_document.DistributeSelected(true);
        if (ImGui::MenuItem("Distribute Vertically"))   m_document.DistributeSelected(false);
        ImGui::EndPopup();
    }
    ImGui::SameLine(); if (ImGui::Button("Auto Layout"))
        m_document.AutoLayoutSelected(240.0f, 90.0f, 8);

    ImGui::SameLine(); ImGui::TextDisabled("|");
    ImGui::SameLine(); ImGui::Checkbox("Snap", &m_snapToGrid);
    ImGui::SameLine(); ImGui::Checkbox("Minimap", &m_showMinimap);
    ImGui::SameLine(); ImGui::Checkbox("Comments carry nodes", &m_commentsCarryNodes);

    // Keyboard shortcuts (Phase 14/15) while the panel is focused.
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        const ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)) m_document.Undo();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y)) m_document.Redo();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C)) m_document.CopySelection();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V)) m_document.Paste(glm::vec2(24.0f));
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D)) m_document.Duplicate(glm::vec2(24.0f));
        // Plain-key shortcuts are suppressed while a text field is capturing input.
        if (!io.WantTextInput) {
            if (ImGui::IsKeyPressed(ImGuiKey_Delete)) m_document.DeleteSelected();
            if (ImGui::IsKeyPressed(ImGuiKey_F) && !io.KeyCtrl)
                m_frameRequest = m_document.Selection().empty() ? 1 : 2;
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A)) {
                m_document.ClearSelection();
                for (const engine::vs::VisualNode& n : m_document.Asset().nodes)
                    m_document.Select(n.id, true);
            }
        }
    }
}

// Fit the view so every node (or just the selection) is visible and centered.
void VisualScriptEditorPanel::FrameView(bool selectionOnly) {
    const VisualScriptAsset& asset = m_document.Asset();
    if (asset.nodes.empty()) return;
    const float nodeWidth = 180.0f, rowH = 20.0f, headerH = 22.0f;
    glm::vec2 lo(1e9f), hi(-1e9f);
    bool any = false;
    for (const VisualNode& n : asset.nodes) {
        if (n.functionId != m_document.CurrentScope()) continue;   // only frame the visible scope
        if (selectionOnly && !m_document.IsSelected(n.id)) continue;
        const int rows = std::max(1, std::max(static_cast<int>(n.inputs.size()), static_cast<int>(n.outputs.size())));
        const glm::vec2 a = n.editorPosition;
        const glm::vec2 b = a + glm::vec2(nodeWidth, headerH + rows * rowH + 8.0f);
        lo = glm::min(lo, a); hi = glm::max(hi, b); any = true;
    }
    if (!any) return;
    const glm::vec2 span = glm::max(hi - lo, glm::vec2(1.0f));
    const ImVec2 avail = ImVec2(std::max(m_lastCanvasSize.x, 100.0f), std::max(m_lastCanvasSize.y, 100.0f));
    const float pad = 60.0f;
    float z = std::min((avail.x - pad) / span.x, (avail.y - pad) / span.y);
    z = std::clamp(z, kZoomMin, kZoomMax);
    m_document.zoom = z;
    const glm::vec2 center = (lo + hi) * 0.5f;
    m_document.pan = glm::vec2(avail.x * 0.5f, avail.y * 0.5f) - center * z;
}

void VisualScriptEditorPanel::DrawCanvas() {
    ImGui::BeginChild("VSCanvas", ImVec2(-320.0f, 0.0f), true,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size = ImGui::GetContentRegionAvail();
    m_lastCanvasSize = size;   // Milestone 2: framing math needs the live canvas size
    // Apply a pending Frame All / Frame Selection now that the canvas size is known.
    if (m_frameRequest != 0) { FrameView(m_frameRequest == 2); m_frameRequest = 0; }
    ImGui::InvisibleButton("VSBackground", size,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();

    float& zoom = m_document.zoom;
    glm::vec2& pan = m_document.pan;
    if (hovered && ImGui::GetIO().MouseWheel != 0.0f) {
        const float old = zoom;
        zoom = std::clamp(zoom * std::pow(1.12f, ImGui::GetIO().MouseWheel), kZoomMin, kZoomMax);
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
    const float grid = m_gridSize * zoom;
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

    // ---- draw comments (behind nodes) — collapsible, colored, movable (Milestone 2) -------
    const float commentTitleH = 20.0f;
    for (const VisualComment& comment : asset.comments) {
        const glm::vec2 drawSize = comment.collapsed
            ? glm::vec2(comment.size.x, commentTitleH) : comment.size;
        const ImVec2 a = screen(comment.position);
        const ImVec2 b = screen(comment.position + drawSize);
        const ImVec2 titleB(b.x, a.y + commentTitleH * zoom);
        const ImU32 fill = IM_COL32((int)(comment.color.r * 255), (int)(comment.color.g * 255),
                                    (int)(comment.color.b * 255), 40);
        const ImU32 titleFill = IM_COL32((int)(comment.color.r * 255), (int)(comment.color.g * 255),
                                         (int)(comment.color.b * 255), 150);
        const bool selected = (m_selectedComment == comment.id);
        if (!comment.collapsed) draw->AddRectFilled(a, b, fill, 4.0f);
        draw->AddRectFilled(a, titleB, titleFill, 4.0f, ImDrawFlags_RoundCornersTop);
        draw->AddRect(a, b, selected ? IM_COL32(240, 200, 110, 220) : IM_COL32(120, 150, 190, 160),
                      4.0f, 0, selected ? 2.0f : 1.0f);
        draw->AddText({a.x + 18 * zoom, a.y + 3}, IM_COL32(225, 232, 242, 235),
                      comment.text.empty() ? "Comment" : comment.text.c_str());
        // collapse toggle triangle
        const ImVec2 tri(a.x + 6 * zoom, a.y + 6 * zoom);
        if (comment.collapsed)
            draw->AddTriangleFilled({tri.x, tri.y}, {tri.x + 7 * zoom, tri.y + 3.5f * zoom},
                                    {tri.x, tri.y + 7 * zoom}, IM_COL32(220, 228, 240, 235));
        else
            draw->AddTriangleFilled({tri.x, tri.y}, {tri.x + 7 * zoom, tri.y},
                                    {tri.x + 3.5f * zoom, tri.y + 7 * zoom}, IM_COL32(220, 228, 240, 235));

        // title-bar interaction: select + drag; the little triangle toggles collapse
        const ImVec2 m = ImGui::GetIO().MousePos;
        const bool overTitle = m.x >= a.x && m.x <= titleB.x && m.y >= a.y && m.y <= titleB.y;
        const bool overTri = m.x >= a.x && m.x <= a.x + 16 * zoom && m.y >= a.y && m.y <= titleB.y;
        if (overTitle && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !m_dragging) {
            m_selectedComment = comment.id;
            m_document.ClearSelection();
            if (overTri) {
                m_document.SetCommentCollapsed(comment.id, !comment.collapsed);
            } else {
                m_movingComment = true; m_dragComment = comment.id; m_document.PushUndo();
            }
        }
    }

    // ---- draw nodes --------------------------------------------------------
    const FunctionId scope = m_document.CurrentScope();   // Milestone 3: only this scope's nodes
    bool openNodeMenu = false;   // set when a node is right-clicked
    for (VisualNode& node : asset.nodes) {
        if (node.functionId != scope) continue;   // node belongs to a different function / the event graph
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
                const VariableId vid = static_cast<VariableId>(it->second.AsInt());
                const VisualVariable* var = asset.FindVariable(vid);
                if (!var && node.functionId != kInvalidFunctionId)   // resolve function-local names too
                    if (const VisualFunction* fn = asset.FindFunction(node.functionId))
                        for (const VisualVariable& l : fn->locals) if (l.id == vid) { var = &l; break; }
                if (var) title = (node.typeId == "Var.Set" ? "Set " : "Get ") + var->name;
            }
        }
        if (node.typeId == "Function.Call") {   // show the callee's name (Milestone 3)
            auto it = node.properties.find("func");
            if (it != node.properties.end())
                if (const VisualFunction* fn = asset.FindFunction(static_cast<FunctionId>(it->second.AsInt())))
                    title = "Call " + fn->name;
        }
        // Event nodes show the event name they carry (Milestone 6).
        if (node.typeId == "Event.Custom" || node.typeId == "Event.Broadcast" || node.typeId == "Interface.Call") {
            auto it = node.properties.find("eventName");
            if (it != node.properties.end() && !it->second.AsString().empty()) {
                const std::string& en = it->second.AsString();
                title = (node.typeId == "Event.Custom" ? "On " : node.typeId == "Interface.Call" ? "Call " : "Broadcast ") + en;
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
                // Milestone 2: while dragging a wire, ring every pin it could legally join.
                if (m_dragging && node.id != m_dragNode && output != m_dragFromOutput
                    && pin.kind == m_dragPinKind) {
                    const bool compat = (m_dragPinKind == PinKind::Exec) ||
                        (m_dragFromOutput ? DataTypesCompatible(m_dragPinType, pin.type)
                                          : DataTypesCompatible(pin.type, m_dragPinType));
                    if (compat)
                        draw->AddCircle(c, 8.0f * zoom, IM_COL32(255, 240, 150, 235), 0, 2.0f);
                }
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
            // One shared, type-aware editor covers every ValueType (Milestone 1).
            if (vswidgets::DrawVisualValueEditor("##d", pin.type, v, ValueCtx()))
                m_document.SetPinDefault(node.id, pin.id, v);
            ImGui::PopID();
            ImGui::PopID();
        }

        // node body click = select / move (drag) with undo bracketing.
        const ImVec2 m = ImGui::GetIO().MousePos;
        const bool overBody = m.x >= a.x && m.x <= b.x && m.y >= a.y && m.y <= a.y + headerH * zoom;
        if (overBody && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !m_dragging && !m_movingComment) {
            m_document.Select(node.id, ImGui::GetIO().KeyCtrl);
            m_selectedComment = kInvalidCommentId;
            m_movingNodes = true; m_document.PushUndo();
        }
        // Double-click a Call node to dive into the function it calls (breadcrumb back in the panel).
        if (overBody && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            const FunctionId target = m_document.CallTargetOf(node.id);
            if (target != kInvalidFunctionId) {
                m_movingNodes = false;
                m_document.SetScope(target);
                m_frameRequest = 1;
            }
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
        const ImU32 col = PinColor(pinType[key(link.fromNode, link.fromPin)]);
        draw->AddBezierCubic(from, {from.x + 60 * zoom, from.y}, {to.x - 60 * zoom, to.y}, to,
            col, 2.5f * zoom);
        // Direction arrowhead at the bezier midpoint (t=0.5), pointing toward the input.
        const ImVec2 p0 = from, p1{from.x + 60 * zoom, from.y}, p2{to.x - 60 * zoom, to.y}, p3 = to;
        auto bez = [&](float u) {
            const float v = 1.0f - u;
            const float w0 = v*v*v, w1 = 3*v*v*u, w2 = 3*v*u*u, w3 = u*u*u;
            return ImVec2(w0*p0.x + w1*p1.x + w2*p2.x + w3*p3.x,
                          w0*p0.y + w1*p1.y + w2*p2.y + w3*p3.y);
        };
        const ImVec2 mid = bez(0.5f), ahead = bez(0.56f);
        const float dx = ahead.x - mid.x, dy = ahead.y - mid.y;
        const float len = std::sqrt(dx*dx + dy*dy);
        if (len > 0.001f) {
            const float ux = dx/len, uy = dy/len, s = 6.0f * zoom;
            draw->AddTriangleFilled(
                {mid.x + ux*s, mid.y + uy*s},
                {mid.x - uy*s*0.6f - ux*s*0.2f, mid.y + ux*s*0.6f - uy*s*0.2f},
                {mid.x + uy*s*0.6f - ux*s*0.2f, mid.y - ux*s*0.6f - uy*s*0.2f}, col);
        }
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
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            m_movingNodes = false;
            if (m_snapToGrid) m_document.SnapSelectedToGrid(m_gridSize);   // Milestone 2
        }
    }
    // Comment drag (optionally carrying enclosed nodes).
    if (m_movingComment) {
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
            m_document.MoveComment(m_dragComment,
                glm::vec2(ImGui::GetIO().MouseDelta.x, ImGui::GetIO().MouseDelta.y) / zoom,
                m_commentsCarryNodes);
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) { m_movingComment = false; m_dragComment = kInvalidCommentId; }
    }

    // ---- box (rubber-band) selection over empty canvas (Milestone 2) -------
    if (m_boxSelecting) {
        const ImVec2 s = ImVec2(std::min(m_boxStartScreen.x, ImGui::GetIO().MousePos.x),
                                std::min(m_boxStartScreen.y, ImGui::GetIO().MousePos.y));
        const ImVec2 e = ImVec2(std::max(m_boxStartScreen.x, ImGui::GetIO().MousePos.x),
                                std::max(m_boxStartScreen.y, ImGui::GetIO().MousePos.y));
        draw->AddRectFilled(s, e, IM_COL32(120, 170, 230, 40));
        draw->AddRect(s, e, IM_COL32(150, 195, 245, 200));
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            if (!ImGui::GetIO().KeyCtrl) m_document.ClearSelection();
            const glm::vec2 lo = graph(s), hi = graph(e);
            for (const VisualNode& n : asset.nodes) {
                if (n.functionId != scope) continue;
                const int rows = std::max(1, std::max(static_cast<int>(n.inputs.size()), static_cast<int>(n.outputs.size())));
                const glm::vec2 na = n.editorPosition;
                const glm::vec2 nb = na + glm::vec2(nodeWidth, headerH + rows * rowH + 8.0f);
                const bool overlap = na.x <= hi.x && nb.x >= lo.x && na.y <= hi.y && nb.y >= lo.y;
                if (overlap) m_document.Select(n.id, true);
            }
            m_boxSelecting = false;
        }
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
    // Left-press on empty canvas arms a box selection (and clears unless Ctrl is held).
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !m_dragging && !m_movingNodes
        && !m_movingComment) {
        m_boxArmed = true;
        m_boxStartScreen = glm::vec2(ImGui::GetIO().MousePos.x, ImGui::GetIO().MousePos.y);
        if (!ImGui::GetIO().KeyCtrl) { m_document.ClearSelection(); m_selectedComment = kInvalidCommentId; }
    }
    if (m_boxArmed && !m_boxSelecting && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        m_boxSelecting = true; m_boxArmed = false;
    }
    if (m_boxArmed && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) m_boxArmed = false;

    // ---- minimap overlay (bottom-right) — click to recenter (Milestone 2) --
    if (m_showMinimap && !asset.nodes.empty()) {
        glm::vec2 lo(1e9f), hi(-1e9f);
        bool anyInScope = false;
        for (const VisualNode& n : asset.nodes) {
            if (n.functionId != scope) continue;
            const int rows = std::max(1, std::max(static_cast<int>(n.inputs.size()), static_cast<int>(n.outputs.size())));
            lo = glm::min(lo, n.editorPosition);
            hi = glm::max(hi, n.editorPosition + glm::vec2(nodeWidth, headerH + rows * rowH + 8.0f));
            anyInScope = true;
        }
        if (!anyInScope) { lo = glm::vec2(0.0f); hi = glm::vec2(1.0f); }
        const glm::vec2 gspan = glm::max(hi - lo, glm::vec2(1.0f));
        const ImVec2 mmSize(180.0f, 120.0f);
        const ImVec2 mmMax(origin.x + size.x - 12.0f, origin.y + size.y - 12.0f);
        const ImVec2 mmMin(mmMax.x - mmSize.x, mmMax.y - mmSize.y);
        const float mmScale = std::min(mmSize.x / gspan.x, mmSize.y / gspan.y) * 0.92f;
        auto mm = [&](glm::vec2 g) {
            return ImVec2(mmMin.x + 6.0f + (g.x - lo.x) * mmScale,
                          mmMin.y + 6.0f + (g.y - lo.y) * mmScale);
        };
        draw->AddRectFilled(mmMin, mmMax, IM_COL32(18, 20, 26, 220), 4.0f);
        draw->AddRect(mmMin, mmMax, IM_COL32(80, 88, 102, 220), 4.0f);
        for (const VisualNode& n : asset.nodes) {
            if (n.functionId != scope) continue;
            const int rows = std::max(1, std::max(static_cast<int>(n.inputs.size()), static_cast<int>(n.outputs.size())));
            const ImVec2 na = mm(n.editorPosition);
            const ImVec2 nb = mm(n.editorPosition + glm::vec2(nodeWidth, headerH + rows * rowH + 8.0f));
            const ImU32 c = m_document.IsSelected(n.id) ? IM_COL32(240, 200, 110, 255)
                                                        : IM_COL32(130, 150, 180, 235);
            draw->AddRectFilled(na, nb, c);
        }
        // current viewport rectangle
        const glm::vec2 vlo = graph(origin);
        const glm::vec2 vhi = graph(ImVec2(origin.x + size.x, origin.y + size.y));
        draw->AddRect(mm(vlo), mm(vhi), IM_COL32(240, 240, 250, 220));
        // click inside the minimap recenters the view there
        const ImVec2 mp = ImGui::GetIO().MousePos;
        const bool overMM = mp.x >= mmMin.x && mp.x <= mmMax.x && mp.y >= mmMin.y && mp.y <= mmMax.y;
        if (overMM && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            const glm::vec2 g(lo.x + (mp.x - mmMin.x - 6.0f) / mmScale,
                              lo.y + (mp.y - mmMin.y - 6.0f) / mmScale);
            pan = glm::vec2(size.x * 0.5f, size.y * 0.5f) - g * zoom;
            m_boxArmed = false;   // don't treat the minimap click as a canvas box-select
        }
    }

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

    // scope breadcrumb (Milestone 3): Event Graph / <function>
    {
        const FunctionId scope = m_document.CurrentScope();
        ImGui::TextDisabled("Scope:"); ImGui::SameLine();
        if (ImGui::SmallButton("Event Graph")) m_document.SetScope(kInvalidFunctionId);
        if (scope != kInvalidFunctionId) {
            const VisualFunction* fn = m_document.Asset().FindFunction(scope);
            ImGui::SameLine(); ImGui::TextDisabled("/");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.92f, 0.82f, 0.42f, 1.0f), "%s",
                               fn ? (fn->name.empty() ? "Function" : fn->name.c_str()) : "?");
        }
    }

    // ---- jump-to-node search (Milestone 2) --------------------------------
    ImGui::SeparatorText("Navigate");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##jump", "Find node...", m_jumpBuffer.data(), m_jumpBuffer.size());
    const std::string jump = m_jumpBuffer.data();
    if (!jump.empty()) {
        std::string needle = jump;
        std::transform(needle.begin(), needle.end(), needle.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        ImGui::BeginChild("VSJumpResults", ImVec2(0, 120), true);
        for (const VisualNode& n : m_document.Asset().nodes) {
            const NodeDescriptor* d = VisualNodeRegistry::Instance().Find(n.typeId);
            std::string title = d ? d->displayName : n.typeId;
            if (n.typeId == "Var.Get" || n.typeId == "Var.Set") {
                auto it = n.properties.find("var");
                if (it != n.properties.end())
                    if (const VisualVariable* var = m_document.Asset().FindVariable(
                            static_cast<VariableId>(it->second.AsInt())))
                        title = (n.typeId == "Var.Set" ? "Set " : "Get ") + var->name;
            }
            std::string hay = title;
            std::transform(hay.begin(), hay.end(), hay.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (hay.find(needle) == std::string::npos) continue;
            ImGui::PushID(static_cast<int>(n.id));
            if (ImGui::Selectable((title + "  [" + std::to_string(n.id) + "]").c_str())) {
                m_document.SetScope(n.functionId);   // jump may cross into a function scope
                m_document.Select(n.id, false);
                m_document.pan = glm::vec2(m_lastCanvasSize.x * 0.5f, m_lastCanvasSize.y * 0.5f)
                               - n.editorPosition * m_document.zoom;
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
    }

    // ---- live debugger ----------------------------------------------------
    ImGui::SeparatorText("Debugger");
    ImGui::TextColored(diagnostics.enabled ? ImVec4(0.45f, 0.9f, 0.55f, 1.0f)
                                             : ImVec4(0.65f, 0.68f, 0.72f, 1.0f),
                       diagnostics.enabled ? "Attached to Play mode" : "Start Play mode to debug");
    // Milestone 9: toggle compiled vs interpreted execution live to compare profiler timings.
    ImGui::Checkbox("Compiled execution", &engine::vs::VisualScriptUseCompiledRef());
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("On: O(1) compiled node/link lookups. Off: interpreted linear scans.\n"
                          "Toggle during Play and watch the per-node timings to compare.");
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
                const std::string frameLabel = (it->functionName.empty() ? std::string() : (it->functionName + " · "))
                                             + it->nodeType + "  [" + std::to_string(it->node) + "]";
                if (ImGui::Selectable(frameLabel.c_str())) {
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
                    ImGui::BulletText("%s%s [%u]",
                        frame.functionName.empty() ? "" : (frame.functionName + " · ").c_str(),
                        frame.nodeType.c_str(), static_cast<unsigned>(frame.node));
                ImGui::TreePop();
            }
            ImGui::Separator();
            ImGui::PopID();
        }
        if (ImGui::SmallButton("Clear Errors##vsdebug")) diagnostics.ClearErrors();
        ImGui::TreePop();
    }

    // ---- event trace (Milestone 6): sender -> receiver, name, dispatch path -----
    if (!diagnostics.EventTrace().empty() &&
        ImGui::TreeNodeEx("Event Trace", 0, "Event Trace (%d)", static_cast<int>(diagnostics.EventTrace().size()))) {
        int shown = 0;
        for (auto it = diagnostics.EventTrace().rbegin(); it != diagnostics.EventTrace().rend() && shown < 40; ++it, ++shown) {
            ImGui::BulletText("%s  E%u %s %s", it->eventName.c_str(),
                              static_cast<unsigned>(it->sender),
                              it->broadcast ? "-> (broadcast)" : "->",
                              it->broadcast ? "" : ("E" + std::to_string(static_cast<unsigned>(it->target))).c_str());
        }
        if (ImGui::SmallButton("Clear Trace##vsdebug")) diagnostics.ClearEventTrace();
        ImGui::TreePop();
    }

    // ---- active async tasks (Milestone 8): owner, elapsed, status ----------
    if (!diagnostics.Tasks().empty() &&
        ImGui::TreeNodeEx("Active Tasks", ImGuiTreeNodeFlags_DefaultOpen, "Active Tasks (%d)",
                          static_cast<int>(diagnostics.Tasks().size()))) {
        for (const engine::vs::VsTaskInfo& t : diagnostics.Tasks()) {
            if (t.timeout > 0.0f)
                ImGui::BulletText("%s #%d  E%u  %.2fs / timeout %.2fs", t.kind.c_str(), t.handle,
                                  static_cast<unsigned>(t.owner), t.elapsed, t.timeout);
            else
                ImGui::BulletText("%s #%d  E%u  %.2fs", t.kind.c_str(), t.handle,
                                  static_cast<unsigned>(t.owner), t.elapsed);
        }
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
                ImGui::TextUnformatted(pin.name.c_str());
                ImGui::SetNextItemWidth(180.0f);
                // Same shared editor as the canvas, so both views stay in sync (Milestone 1).
                const std::string label = "##pin" + std::to_string(pin.id);
                if (vswidgets::DrawVisualValueEditor(label.c_str(), pin.type, v, ValueCtx()))
                    m_document.SetPinDefault(node->id, pin.id, v);
                ImGui::PopID();
            }
            if (!any) ImGui::TextDisabled("No editable inputs.");
        }
    }

    // ---- comments (Milestone 2) -------------------------------------------
    if (!m_document.Asset().comments.empty()) {
        ImGui::SeparatorText("Comments");
        CommentId deleteComment = kInvalidCommentId;
        for (const VisualComment& comment : m_document.Asset().comments) {
            ImGui::PushID(static_cast<int>(comment.id + 100000));
            std::array<char, 96> title{};
            std::snprintf(title.data(), title.size(), "%s", comment.text.c_str());
            ImGui::SetNextItemWidth(150.0f);
            if (ImGui::InputText("##ctitle", title.data(), title.size()))
                m_document.SetCommentText(comment.id, title.data());
            ImGui::SameLine();
            glm::vec4 col = comment.color;
            if (ImGui::ColorEdit4("##ccolor", &col.x,
                    ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel))
                m_document.SetCommentColor(comment.id, col);
            ImGui::SameLine();
            bool collapsed = comment.collapsed;
            if (ImGui::Checkbox("Collapse##c", &collapsed))
                m_document.SetCommentCollapsed(comment.id, collapsed);
            ImGui::SameLine();
            if (ImGui::SmallButton("Focus")) {
                m_document.pan = glm::vec2(m_lastCanvasSize.x * 0.5f, m_lastCanvasSize.y * 0.5f)
                               - comment.position * m_document.zoom;
                m_selectedComment = comment.id;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("X##c")) deleteComment = comment.id;
            ImGui::PopID();
        }
        if (deleteComment != kInvalidCommentId) {
            m_document.DeleteComment(deleteComment);
            if (m_selectedComment == deleteComment) m_selectedComment = kInvalidCommentId;
        }
    }

    // ---- struct / enum types (Milestone 4) --------------------------------
    DrawTypesSection();

    // ---- events / interfaces (Milestone 6) --------------------------------
    DrawEventsSection();

    // ---- state machines (Milestone 7) -------------------------------------
    DrawStateMachinesSection();

    // ---- functions / subgraphs (Milestone 3) ------------------------------
    DrawFunctionsSection();

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

        bool exposed = var.exposed;
        if (ImGui::Checkbox("Exposed on Component##exposed", &exposed))
            m_document.SetVariableExposed(var.id, exposed);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Show this variable as a per-object override in the Inspector.");

        // Container/struct/enum refinement (Milestone 4): pick element/key/struct/enum type.
        if (var.type == ValueType::Array || var.type == ValueType::Map) {
            ImGui::SetNextItemWidth(90.0f);
            int et = static_cast<int>(var.defaultValue.elementType);
            if (ImGui::Combo("Elem##vc", &et, kValueTypeNames, IM_ARRAYSIZE(kValueTypeNames)))
                m_document.SetVariableContainer(var.id, static_cast<ValueType>(et),
                                                var.defaultValue.keyType, 0);
            if (var.type == ValueType::Map) {
                ImGui::SameLine(); ImGui::SetNextItemWidth(90.0f);
                int kt = static_cast<int>(var.defaultValue.keyType);
                if (ImGui::Combo("Key##vk", &kt, kValueTypeNames, IM_ARRAYSIZE(kValueTypeNames)))
                    m_document.SetVariableContainer(var.id, var.defaultValue.elementType,
                                                    static_cast<ValueType>(kt), var.defaultValue.typeId);
            }
        }
        const bool wantsStructId =
            (var.type == ValueType::Struct) ||
            ((var.type == ValueType::Array || var.type == ValueType::Map) && var.defaultValue.elementType == ValueType::Struct);
        const bool wantsEnumId =
            (var.type == ValueType::Enum) ||
            ((var.type == ValueType::Array || var.type == ValueType::Map) && var.defaultValue.elementType == ValueType::Enum);
        if (wantsStructId) {
            ImGui::SameLine(); ImGui::SetNextItemWidth(100.0f);
            const engine::vs::VisualStructType* cur = m_document.Asset().FindStruct(var.defaultValue.typeId);
            if (ImGui::BeginCombo("Type##vs", cur ? cur->name.c_str() : "(struct)")) {
                for (const engine::vs::VisualStructType& o : m_document.Asset().structs)
                    if (ImGui::Selectable(o.name.c_str(), o.id == var.defaultValue.typeId))
                        m_document.SetVariableContainer(var.id, var.defaultValue.elementType,
                                                        var.defaultValue.keyType, o.id);
                ImGui::EndCombo();
            }
        } else if (wantsEnumId) {
            ImGui::SameLine(); ImGui::SetNextItemWidth(100.0f);
            const engine::vs::VisualEnumType* cur = m_document.Asset().FindEnum(var.defaultValue.typeId);
            if (ImGui::BeginCombo("Type##ve", cur ? cur->name.c_str() : "(enum)")) {
                for (const engine::vs::VisualEnumType& o : m_document.Asset().enums)
                    if (ImGui::Selectable(o.name.c_str(), o.id == var.defaultValue.typeId))
                        m_document.SetVariableContainer(var.id, var.defaultValue.elementType,
                                                        var.defaultValue.keyType, o.id);
                ImGui::EndCombo();
            }
        }

        // Default-value editor — the shared type-aware widget, covering every ValueType (Milestone 1).
        VisualValue dv = var.defaultValue;
        ImGui::SetNextItemWidth(200.0f);
        if (vswidgets::DrawVisualValueEditor("Value##vv", var.type, dv, ValueCtx()))
            m_document.SetVariableDefault(var.id, dv);

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

// A rename field with one shared persistent buffer, so typing isn't wiped by the per-frame reseed.
bool VisualScriptEditorPanel::EditText(const char* label, const std::string& token,
                                       const std::string& current, std::string* out) {
    std::array<char, 96> buf{};
    const bool active = (m_editToken == token);
    std::snprintf(buf.data(), buf.size(), "%s", active ? m_editBuf.data() : current.c_str());
    ImGui::InputText(label, buf.data(), buf.size());
    if (ImGui::IsItemActive()) {
        m_editToken = token;
        std::snprintf(m_editBuf.data(), m_editBuf.size(), "%s", buf.data());
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        if (out) *out = buf.data();
        if (m_editToken == token) m_editToken.clear();
        return true;
    }
    return false;
}

void VisualScriptEditorPanel::DrawFunctionsSection() {
    using engine::vs::VisualFunction;
    using engine::vs::VisualFunctionParam;
    using engine::vs::VisualVariable;

    ImGui::SeparatorText("Functions");
    if (ImGui::Button("Add Function")) {
        const FunctionId id = m_document.CreateFunction("");
        m_document.SetScope(id);
        m_frameRequest = 1;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Reusable subgraphs; double-click a Call node to enter.");

    FunctionId deleteFn = kInvalidFunctionId;
    const FunctionId scope = m_document.CurrentScope();
    for (const VisualFunction& fn : m_document.Asset().functions) {
        ImGui::PushID(static_cast<int>(fn.id + 200000));
        const std::string tok = "fn" + std::to_string(fn.id);
        std::string newName;
        ImGui::SetNextItemWidth(130.0f);
        if (EditText("##fnname", tok, fn.name, &newName)) m_document.RenameFunction(fn.id, newName);
        ImGui::SameLine(); if (ImGui::SmallButton("Open")) { m_document.SetScope(fn.id); m_frameRequest = 1; }
        ImGui::SameLine();
        if (ImGui::SmallButton("Call")) {
            const glm::vec2 at = (glm::vec2(140.0f) - m_document.pan) / m_document.zoom;
            m_document.AddCallNode(fn.id, at);
        }
        ImGui::SameLine(); if (ImGui::SmallButton("X")) deleteFn = fn.id;

        // Signature + locals editors are shown only for the function currently open on the canvas.
        if (scope == fn.id) {
            ImGui::Indent(10.0f);
            auto paramEditor = [&](bool output) {
                ImGui::TextDisabled(output ? "Outputs" : "Inputs");
                const auto& params = output ? fn.outputs : fn.inputs;
                for (std::size_t i = 0; i < params.size(); ++i) {
                    ImGui::PushID(static_cast<int>((output ? 5000 : 1000) + i));
                    const std::string ptok = tok + (output ? "o" : "i") + std::to_string(i);
                    std::string pname;
                    ImGui::SetNextItemWidth(110.0f);
                    if (EditText("##pn", ptok, params[i].name, &pname))
                        m_document.SetFunctionParam(fn.id, output, i, pname, params[i].type);
                    ImGui::SameLine(); ImGui::SetNextItemWidth(90.0f);
                    int typeIdx = static_cast<int>(params[i].type);
                    if (ImGui::Combo("##pt", &typeIdx, kValueTypeNames, IM_ARRAYSIZE(kValueTypeNames)))
                        m_document.SetFunctionParam(fn.id, output, i, params[i].name,
                                                    static_cast<ValueType>(typeIdx));
                    ImGui::SameLine();
                    if (ImGui::SmallButton("X")) m_document.RemoveFunctionParam(fn.id, output, i);
                    ImGui::PopID();
                }
                if (ImGui::SmallButton(output ? "+ Output" : "+ Input"))
                    m_document.AddFunctionParam(fn.id, output, output ? "Out" : "In",
                                                static_cast<ValueType>(m_newParamType));
            };
            paramEditor(false);
            paramEditor(true);
            ImGui::SetNextItemWidth(90.0f);
            ImGui::Combo("New param type", &m_newParamType, kValueTypeNames, IM_ARRAYSIZE(kValueTypeNames));

            // Function-local variables (own scope; Get/Set nodes usable inside this function).
            ImGui::TextDisabled("Locals");
            VariableId deleteLocal = kInvalidVariableId;
            for (const VisualVariable& local : fn.locals) {
                ImGui::PushID(static_cast<int>(local.id + 700000));
                const std::string ltok = tok + "l" + std::to_string(local.id);
                std::string lname;
                ImGui::SetNextItemWidth(110.0f);
                if (EditText("##ln", ltok, local.name, &lname)) m_document.RenameFunctionLocal(fn.id, local.id, lname);
                ImGui::SameLine(); ImGui::TextDisabled("%s", kValueTypeNames[static_cast<int>(local.type)]);
                ImGui::SameLine(); if (ImGui::SmallButton("Get"))
                    m_document.AddLocalVariableNode(false, fn.id, local.id, (glm::vec2(60.0f) - m_document.pan) / m_document.zoom);
                ImGui::SameLine(); if (ImGui::SmallButton("Set"))
                    m_document.AddLocalVariableNode(true, fn.id, local.id, (glm::vec2(60.0f, 90.0f) - m_document.pan) / m_document.zoom);
                ImGui::SameLine(); if (ImGui::SmallButton("X")) deleteLocal = local.id;
                VisualValue dv = local.defaultValue;
                ImGui::SetNextItemWidth(160.0f);
                if (vswidgets::DrawVisualValueEditor("Value##lv", local.type, dv, ValueCtx()))
                    m_document.SetFunctionLocalDefault(fn.id, local.id, dv);
                ImGui::PopID();
            }
            if (deleteLocal != kInvalidVariableId) m_document.DeleteFunctionLocal(fn.id, deleteLocal);
            if (ImGui::SmallButton("+ Local"))
                m_document.AddFunctionLocal(fn.id, "Local", static_cast<ValueType>(m_newParamType));
            ImGui::Unindent(10.0f);
        }
        ImGui::Separator();
        ImGui::PopID();
    }
    if (deleteFn != kInvalidFunctionId) m_document.DeleteFunction(deleteFn);
}

void VisualScriptEditorPanel::DrawTypesSection() {
    using engine::vs::VisualStructType;
    using engine::vs::VisualStructField;
    using engine::vs::VisualEnumType;

    ImGui::SeparatorText("Structs");
    if (ImGui::Button("Add Struct")) m_document.CreateStruct("");
    std::uint32_t deleteStruct = 0;
    for (const VisualStructType& s : m_document.Asset().structs) {
        ImGui::PushID(static_cast<int>(s.id + 900000));
        const std::string tok = "st" + std::to_string(s.id);
        std::string newName;
        ImGui::SetNextItemWidth(130.0f);
        if (EditText("##sname", tok, s.name, &newName)) m_document.RenameStruct(s.id, newName);
        ImGui::SameLine(); if (ImGui::SmallButton("Make")) m_document.AddStructNode(true, s.id, (glm::vec2(120.0f) - m_document.pan) / m_document.zoom);
        ImGui::SameLine(); if (ImGui::SmallButton("Break")) m_document.AddStructNode(false, s.id, (glm::vec2(120.0f, 90.0f) - m_document.pan) / m_document.zoom);
        ImGui::SameLine(); if (ImGui::SmallButton("X")) deleteStruct = s.id;

        ImGui::Indent(10.0f);
        for (std::size_t i = 0; i < s.fields.size(); ++i) {
            const VisualStructField& f = s.fields[i];
            ImGui::PushID(static_cast<int>(i));
            std::string fname;
            ImGui::SetNextItemWidth(100.0f);
            if (EditText("##fn", tok + "f" + std::to_string(i), f.name, &fname))
                m_document.SetStructField(s.id, i, fname, f.type, f.typeId);
            ImGui::SameLine(); ImGui::SetNextItemWidth(85.0f);
            int typeIdx = static_cast<int>(f.type);
            if (ImGui::Combo("##ft", &typeIdx, kValueTypeNames, IM_ARRAYSIZE(kValueTypeNames)))
                m_document.SetStructField(s.id, i, f.name, static_cast<ValueType>(typeIdx), 0);
            // struct/enum field needs a concrete type id
            if (f.type == ValueType::Struct) {
                ImGui::SameLine(); ImGui::SetNextItemWidth(90.0f);
                const VisualStructType* cur = m_document.Asset().FindStruct(f.typeId);
                if (ImGui::BeginCombo("##fsid", cur ? cur->name.c_str() : "(struct)")) {
                    for (const VisualStructType& o : m_document.Asset().structs)
                        if (o.id != s.id && ImGui::Selectable(o.name.c_str(), o.id == f.typeId))
                            m_document.SetStructField(s.id, i, f.name, ValueType::Struct, o.id);
                    ImGui::EndCombo();
                }
            } else if (f.type == ValueType::Enum) {
                ImGui::SameLine(); ImGui::SetNextItemWidth(90.0f);
                const VisualEnumType* cur = m_document.Asset().FindEnum(f.typeId);
                if (ImGui::BeginCombo("##feid", cur ? cur->name.c_str() : "(enum)")) {
                    for (const VisualEnumType& o : m_document.Asset().enums)
                        if (ImGui::Selectable(o.name.c_str(), o.id == f.typeId))
                            m_document.SetStructField(s.id, i, f.name, ValueType::Enum, o.id);
                    ImGui::EndCombo();
                }
            }
            ImGui::SameLine(); if (ImGui::SmallButton("x")) m_document.RemoveStructField(s.id, i);
            ImGui::PopID();
        }
        if (ImGui::SmallButton("+ Field")) m_document.AddStructField(s.id, "Field", ValueType::Float);
        ImGui::Unindent(10.0f);
        ImGui::Separator();
        ImGui::PopID();
    }
    if (deleteStruct != 0) m_document.DeleteStruct(deleteStruct);

    ImGui::SeparatorText("Enums");
    if (ImGui::Button("Add Enum")) m_document.CreateEnum("");
    std::uint32_t deleteEnum = 0;
    for (const VisualEnumType& e : m_document.Asset().enums) {
        ImGui::PushID(static_cast<int>(e.id + 950000));
        const std::string tok = "en" + std::to_string(e.id);
        std::string newName;
        ImGui::SetNextItemWidth(130.0f);
        if (EditText("##ename", tok, e.name, &newName)) m_document.RenameEnum(e.id, newName);
        ImGui::SameLine(); if (ImGui::SmallButton("X")) deleteEnum = e.id;
        ImGui::Indent(10.0f);
        for (std::size_t i = 0; i < e.entries.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            std::string entryName;
            ImGui::SetNextItemWidth(140.0f);
            if (EditText("##ee", tok + "e" + std::to_string(i), e.entries[i], &entryName))
                m_document.SetEnumEntry(e.id, i, entryName);
            ImGui::SameLine(); if (ImGui::SmallButton("x")) m_document.RemoveEnumEntry(e.id, i);
            ImGui::PopID();
        }
        if (ImGui::SmallButton("+ Entry")) m_document.AddEnumEntry(e.id, "");
        ImGui::Unindent(10.0f);
        ImGui::Separator();
        ImGui::PopID();
    }
    if (deleteEnum != 0) m_document.DeleteEnum(deleteEnum);
}

void VisualScriptEditorPanel::DrawEventsSection() {
    using engine::vs::VisualCustomEvent;
    using engine::vs::VisualInterface;

    ImGui::SeparatorText("Events");
    if (ImGui::Button("Add Event")) m_document.CreateEvent("");
    std::uint32_t deleteEvent = 0;
    for (const VisualCustomEvent& e : m_document.Asset().events) {
        ImGui::PushID(static_cast<int>(e.id + 300000));
        const std::string tok = "ev" + std::to_string(e.id);
        std::string newName;
        ImGui::SetNextItemWidth(120.0f);
        if (EditText("##evn", tok, e.name, &newName)) m_document.RenameEvent(e.id, newName);
        ImGui::SameLine(); if (ImGui::SmallButton("Handler")) m_document.AddCustomEventNode(e.id, (glm::vec2(80.0f) - m_document.pan) / m_document.zoom);
        ImGui::SameLine(); if (ImGui::SmallButton("Broadcast")) m_document.AddBroadcastNode(e.id, (glm::vec2(80.0f, 90.0f) - m_document.pan) / m_document.zoom);
        ImGui::SameLine(); if (ImGui::SmallButton("X")) deleteEvent = e.id;
        ImGui::Indent(10.0f);
        for (std::size_t i = 0; i < e.params.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            std::string pname;
            ImGui::SetNextItemWidth(100.0f);
            if (EditText("##pn", tok + "p" + std::to_string(i), e.params[i].name, &pname))
                m_document.SetEventParam(e.id, i, pname, e.params[i].type);
            ImGui::SameLine(); ImGui::SetNextItemWidth(85.0f);
            int typeIdx = static_cast<int>(e.params[i].type);
            if (ImGui::Combo("##pt", &typeIdx, kValueTypeNames, IM_ARRAYSIZE(kValueTypeNames)))
                m_document.SetEventParam(e.id, i, e.params[i].name, static_cast<ValueType>(typeIdx));
            ImGui::SameLine(); if (ImGui::SmallButton("x")) m_document.RemoveEventParam(e.id, i);
            ImGui::PopID();
        }
        if (ImGui::SmallButton("+ Param")) m_document.AddEventParam(e.id, "Param", ValueType::Float);
        ImGui::Unindent(10.0f);
        ImGui::Separator();
        ImGui::PopID();
    }
    if (deleteEvent != 0) m_document.DeleteEvent(deleteEvent);

    ImGui::SeparatorText("Interfaces");
    if (ImGui::Button("Add Interface")) m_document.CreateInterface("");
    std::uint32_t deleteInterface = 0;
    for (const VisualInterface& itf : m_document.Asset().interfaces) {
        ImGui::PushID(static_cast<int>(itf.id + 400000));
        const std::string tok = "if" + std::to_string(itf.id);
        std::string newName;
        ImGui::SetNextItemWidth(120.0f);
        if (EditText("##ifn", tok, itf.name, &newName)) m_document.RenameInterface(itf.id, newName);
        bool implemented = std::find(m_document.Asset().implementedInterfaces.begin(),
                                     m_document.Asset().implementedInterfaces.end(), itf.id)
                           != m_document.Asset().implementedInterfaces.end();
        ImGui::SameLine(); if (ImGui::Checkbox("Impl##if", &implemented)) m_document.SetInterfaceImplemented(itf.id, implemented);
        ImGui::SameLine(); if (ImGui::SmallButton("Add Handlers")) m_document.ImplementInterface(itf.id);
        ImGui::SameLine(); if (ImGui::SmallButton("X")) deleteInterface = itf.id;
        ImGui::Indent(10.0f);
        for (std::size_t i = 0; i < itf.messages.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            ImGui::TextDisabled("%s", itf.messages[i].name.empty() ? "message" : itf.messages[i].name.c_str());
            ImGui::SameLine(); if (ImGui::SmallButton("Call")) m_document.AddInterfaceCallNode(itf.id, i, (glm::vec2(120.0f) - m_document.pan) / m_document.zoom);
            ImGui::PopID();
        }
        if (ImGui::SmallButton("+ Message")) m_document.AddInterfaceMessage(itf.id, "");
        ImGui::Unindent(10.0f);
        ImGui::Separator();
        ImGui::PopID();
    }
    if (deleteInterface != 0) m_document.DeleteInterface(deleteInterface);
}

void VisualScriptEditorPanel::DrawStateMachinesSection() {
    using engine::vs::VisualStateMachine;
    using engine::vs::VisualState;
    using engine::vs::VisualTransition;
    using engine::vs::FunctionId;
    using engine::vs::VisualFunction;

    // Pick a function (or none) from the graph's functions.
    auto pickFunction = [&](const char* label, FunctionId cur) -> std::pair<bool, FunctionId> {
        const VisualFunction* f = m_document.Asset().FindFunction(cur);
        bool changed = false; FunctionId result = cur;
        ImGui::SetNextItemWidth(110.0f);
        if (ImGui::BeginCombo(label, cur ? (f ? f->name.c_str() : "?") : "(none)")) {
            if (ImGui::Selectable("(none)", cur == 0)) { changed = true; result = 0; }
            for (const VisualFunction& fn : m_document.Asset().functions)
                if (ImGui::Selectable(fn.name.c_str(), fn.id == cur)) { changed = true; result = fn.id; }
            ImGui::EndCombo();
        }
        return {changed, result};
    };
    auto pickState = [&](const char* label, const VisualStateMachine& sm, std::uint32_t cur) -> std::pair<bool, std::uint32_t> {
        const char* curName = "(none)";
        for (const VisualState& s : sm.states) if (s.id == cur) curName = s.name.c_str();
        bool changed = false; std::uint32_t result = cur;
        ImGui::SetNextItemWidth(90.0f);
        if (ImGui::BeginCombo(label, curName)) {
            for (const VisualState& s : sm.states)
                if (ImGui::Selectable(s.name.c_str(), s.id == cur)) { changed = true; result = s.id; }
            ImGui::EndCombo();
        }
        return {changed, result};
    };

    ImGui::SeparatorText("State Machines");
    if (ImGui::Button("Add State Machine")) m_document.CreateStateMachine("");
    VisualScriptDiagnostics& diag = VisualScriptDiagnostics::Instance();
    std::uint32_t deleteSM = 0;
    for (const VisualStateMachine& sm : m_document.Asset().stateMachines) {
        ImGui::PushID(static_cast<int>(sm.id + 500000));
        const std::string tok = "sm" + std::to_string(sm.id);
        std::string newName;
        ImGui::SetNextItemWidth(130.0f);
        if (EditText("##smn", tok, sm.name, &newName)) m_document.RenameStateMachine(sm.id, newName);
        ImGui::SameLine(); if (ImGui::SmallButton("X")) deleteSM = sm.id;

        // Live current-state readout during Play.
        const std::uint32_t live = diag.CurrentState(m_document.Asset().id, sm.id);
        if (live) {
            const char* liveName = "?";
            for (const VisualState& s : sm.states) if (s.id == live) liveName = s.name.c_str();
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.55f, 1.0f), "[%s]", liveName);
        }

        ImGui::Indent(10.0f);
        // entry state
        auto entry = pickState("Entry##sme", sm, sm.entryState);
        if (entry.first) m_document.SetEntryState(sm.id, entry.second);

        ImGui::TextDisabled("States");
        std::uint32_t delState = 0;
        for (const VisualState& st : sm.states) {
            ImGui::PushID(static_cast<int>(st.id));
            std::string sname;
            ImGui::SetNextItemWidth(90.0f);
            if (EditText("##stn", tok + "s" + std::to_string(st.id), st.name, &sname)) m_document.RenameState(sm.id, st.id, sname);
            ImGui::SameLine(); { auto r = pickFunction("En##e", st.onEnter); if (r.first) m_document.SetStateFunction(sm.id, st.id, 0, r.second); }
            ImGui::SameLine(); { auto r = pickFunction("Up##u", st.onUpdate); if (r.first) m_document.SetStateFunction(sm.id, st.id, 1, r.second); }
            ImGui::SameLine(); { auto r = pickFunction("Ex##x", st.onExit); if (r.first) m_document.SetStateFunction(sm.id, st.id, 2, r.second); }
            ImGui::SameLine(); if (ImGui::SmallButton("x")) delState = st.id;
            ImGui::PopID();
        }
        if (delState != 0) m_document.RemoveState(sm.id, delState);
        if (ImGui::SmallButton("+ State")) m_document.AddState(sm.id, "");

        ImGui::TextDisabled("Transitions");
        std::uint32_t delTrans = 0;
        for (const VisualTransition& tr : sm.transitions) {
            ImGui::PushID(static_cast<int>(tr.id));
            std::uint32_t from = tr.from, to = tr.to; int priority = tr.priority;
            FunctionId condition = tr.condition; float cooldown = tr.cooldown; bool changed = false;
            auto fr = pickState("##tf", sm, from); if (fr.first) { from = fr.second; changed = true; }
            ImGui::SameLine(); ImGui::TextUnformatted("->"); ImGui::SameLine();
            auto tr2 = pickState("##tt", sm, to); if (tr2.first) { to = tr2.second; changed = true; }
            ImGui::SameLine(); ImGui::SetNextItemWidth(50.0f);
            if (ImGui::DragInt("P##tp", &priority)) changed = true;
            ImGui::SameLine(); { auto r = pickFunction("If##tc", condition); if (r.first) { condition = r.second; changed = true; } }
            ImGui::SameLine(); ImGui::SetNextItemWidth(55.0f);
            if (ImGui::DragFloat("CD##tcd", &cooldown, 0.05f, 0.0f, 0.0f, "%.2f")) changed = true;
            ImGui::SameLine(); if (ImGui::SmallButton("x")) delTrans = tr.id;
            if (changed) m_document.SetTransition(sm.id, tr.id, from, to, priority, condition, cooldown);
            ImGui::PopID();
        }
        if (delTrans != 0) m_document.RemoveTransition(sm.id, delTrans);
        if (ImGui::SmallButton("+ Transition")) {
            const std::uint32_t first = sm.states.empty() ? 0u : sm.states.front().id;
            m_document.AddTransition(sm.id, first, first);
        }
        ImGui::Unindent(10.0f);
        ImGui::Separator();
        ImGui::PopID();
    }
    if (deleteSM != 0) m_document.DeleteStateMachine(deleteSM);
}
