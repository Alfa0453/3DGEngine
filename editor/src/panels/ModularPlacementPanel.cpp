#include "ModularPlacementPanel.h"
#include "EditorPanels.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>

namespace {

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

} // namespace

void ModularPlacementPanel::Refresh(const std::string& assetRoot) {
    const std::string selectedPath = SelectedAsset() ? SelectedAsset()->path : std::string();
    m_assets.clear();
    m_scannedRoot = assetRoot;
    std::error_code ec;
    const std::filesystem::path root(assetRoot);
    if (!std::filesystem::is_directory(root, ec)) return;

    for (std::filesystem::recursive_directory_iterator it(
             root, std::filesystem::directory_options::skip_permission_denied, ec), end;
         it != end; it.increment(ec)) {
        if (ec || !it->is_regular_file(ec)) continue;
        const std::string extension = Lower(it->path().extension().string());
        AssetKind kind;
        if (extension == ".3dgmesh") kind = AssetKind::Model;
        else if (extension == ".3dgprefab") kind = AssetKind::Prefab;
        else continue;

        AssetChoice choice;
        choice.kind = kind;
        choice.path = it->path().string();
        choice.name = it->path().stem().string();
        choice.relativePath = std::filesystem::relative(it->path(), root, ec).generic_string();
        if (ec) {
            ec.clear();
            choice.relativePath = it->path().filename().string();
        }
        m_assets.push_back(std::move(choice));
    }
    std::sort(m_assets.begin(), m_assets.end(), [](const AssetChoice& a, const AssetChoice& b) {
        if (a.kind != b.kind) return a.kind < b.kind;
        return Lower(a.relativePath) < Lower(b.relativePath);
    });
    m_selected = -1;
    if (!selectedPath.empty()) {
        for (int i = 0; i < static_cast<int>(m_assets.size()); ++i) {
            if (m_assets[static_cast<std::size_t>(i)].path == selectedPath) {
                m_selected = i;
                break;
            }
        }
    }
    if (m_selected < 0) {
        m_placementActive = false;
        m_paintMode = false;
    }
}

const ModularPlacementPanel::AssetChoice* ModularPlacementPanel::SelectedAsset() const {
    if (m_selected < 0 || m_selected >= static_cast<int>(m_assets.size())) return nullptr;
    return &m_assets[static_cast<std::size_t>(m_selected)];
}

glm::vec3 ModularPlacementPanel::SnapPosition(const glm::vec3& position) const {
    glm::vec3 result = position;
    const float step = std::max(m_gridSize, 0.001f);
    auto snap = [step](float value) { return std::round(value / step) * step; };
    if (m_snapX) result.x = snap(result.x);
    if (m_snapY) result.y = snap(result.y);
    if (m_snapZ) result.z = snap(result.z);
    return result;
}

ModularPlacementPanel::Result ModularPlacementPanel::Draw(
    const std::string& assetRoot, bool* open) {
    Result result;
    if (m_scannedRoot != assetRoot) Refresh(assetRoot);

    if (!ImGui::Begin(EditorPanels::Name(EditorPanels::Panel::ModularPlacement), open)) {
        ImGui::End();
        return result;
    }

    if (ImGui::Button("Refresh")) {
        Refresh(assetRoot);
        result.refreshRequested = true;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##ModuleFilter", "Search models and prefabs...",
                             m_filter.data(), m_filter.size());

    const float listHeight = std::clamp(ImGui::GetContentRegionAvail().y * 0.43f,
                                        150.0f, 330.0f);
    if (ImGui::BeginChild("##ModulePalette", ImVec2(0.0f, listHeight), true)) {
        const std::string filter = Lower(m_filter.data());
        for (int i = 0; i < static_cast<int>(m_assets.size()); ++i) {
            const AssetChoice& asset = m_assets[static_cast<std::size_t>(i)];
            if (!filter.empty() && Lower(asset.relativePath).find(filter) == std::string::npos)
                continue;
            ImGui::PushID(i);
            const char* type = asset.kind == AssetKind::Model ? "Mesh" : "Prefab";
            const std::string label = std::string("[") + type + "] " + asset.name;
            if (ImGui::Selectable(label.c_str(), i == m_selected)) m_selected = i;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", asset.relativePath.c_str());
            ImGui::PopID();
        }
        if (m_assets.empty()) ImGui::TextDisabled("No static meshes or prefabs found.");
    }
    ImGui::EndChild();

    const AssetChoice* selected = SelectedAsset();
    ImGui::Text("Selected: %s", selected ? selected->name.c_str() : "None");
    ImGui::SeparatorText("Placement");
    if (!selected) ImGui::BeginDisabled();
    if (ImGui::Checkbox("Place in Viewport", &m_placementActive) && m_placementActive)
        m_paintMode = false;
    if (ImGui::Checkbox("Paint Continuously", &m_paintMode) && m_paintMode)
        m_placementActive = true;
    if (!selected) ImGui::EndDisabled();
    ImGui::Checkbox("Snap to Surfaces", &m_surfaceSnap);
    ImGui::Checkbox("Keep Outside Surface", &m_offsetByBounds);
    ImGui::DragFloat("Grid Size", &m_gridSize, 0.05f, 0.01f, 100.0f, "%.2f");
    m_gridSize = std::clamp(m_gridSize, 0.01f, 100.0f);
    ImGui::Checkbox("X##ModuleSnap", &m_snapX); ImGui::SameLine();
    ImGui::Checkbox("Y##ModuleSnap", &m_snapY); ImGui::SameLine();
    ImGui::Checkbox("Z##ModuleSnap", &m_snapZ);

    ImGui::DragFloat("Rotation Step", &m_rotationStep, 1.0f, 1.0f, 180.0f, "%.0f deg");
    m_rotationStep = std::clamp(m_rotationStep, 1.0f, 180.0f);
    if (ImGui::Button("Rotate Left")) m_rotationDegrees -= m_rotationStep;
    ImGui::SameLine();
    if (ImGui::Button("Rotate Right")) m_rotationDegrees += m_rotationStep;
    ImGui::SameLine();
    if (ImGui::Button("Reset##ModuleRotation")) m_rotationDegrees = 0.0f;
    m_rotationDegrees = std::remainder(m_rotationDegrees, 360.0f);
    ImGui::Text("Yaw: %.0f deg", m_rotationDegrees);
    if (m_paintMode) {
        ImGui::DragFloat("Paint Spacing", &m_paintSpacing, 0.05f, 0.05f, 100.0f, "%.2f");
        m_paintSpacing = std::clamp(m_paintSpacing, 0.05f, 100.0f);
    }

    if (!selected) ImGui::BeginDisabled();
    if (ImGui::Button("Place In Front", ImVec2(-1.0f, 0.0f)))
        result.placeInFrontRequested = true;
    if (ImGui::Button("Replace Selected", ImVec2(-1.0f, 0.0f)))
        result.replaceSelectedRequested = true;
    if (!selected) ImGui::EndDisabled();

    ImGui::Separator();
    if (m_placementActive)
        ImGui::TextColored(ImVec4(0.30f, 0.90f, 0.45f, 1.0f),
                           m_paintMode ? "Painting: hold left mouse in the Viewport"
                                       : "Placement active: click in the Viewport");
    else
        ImGui::TextDisabled("Select a piece, then enable viewport placement.");

    ImGui::End();
    return result;
}
