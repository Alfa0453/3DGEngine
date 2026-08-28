#include "RoomBuilderPanel.h"
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
}

glm::vec3 RoomBuilderPanel::Snap(const glm::vec3& value) const {
    const float step = std::max(m_gridSize, 0.01f);
    return glm::round(value / step) * step;
}

void RoomBuilderPanel::SetHoverPoint(const glm::vec3& point) {
    if (m_captureOutline) m_hover = Snap(point);
}

void RoomBuilderPanel::CapturePoint(const glm::vec3& point) {
    const glm::vec3 snapped = Snap(point);
    if (!m_hasFirst || !m_captureOutline) {
        m_first = snapped;
        m_second = snapped;
        m_hover = snapped;
        m_hasFirst = true;
        m_hasSecond = false;
        m_captureOutline = true;
        return;
    }
    m_second = snapped;
    if (std::abs(m_second.x - m_first.x) < m_gridSize)
        m_second.x = m_first.x + m_gridSize;
    if (std::abs(m_second.z - m_first.z) < m_gridSize)
        m_second.z = m_first.z + m_gridSize;
    m_second.y = m_first.y;
    m_hasSecond = true;
    m_captureOutline = false;
}

void RoomBuilderPanel::CancelCapture() {
    m_captureOutline = false;
    if (!m_hasSecond) m_hasFirst = false;
}

void RoomBuilderPanel::RefreshMaterials(const std::string& assetRoot) {
    m_materialRoot = assetRoot;
    m_materials.clear();
    std::error_code ec;
    const std::filesystem::path root(assetRoot);
    for (std::filesystem::recursive_directory_iterator it(
             root, std::filesystem::directory_options::skip_permission_denied, ec), end;
         it != end; it.increment(ec)) {
        if (ec || !it->is_regular_file(ec)
            || Lower(it->path().extension().string()) != ".3dgmat") continue;
        m_materials.push_back({it->path().string(), it->path().stem().string()});
    }
    std::sort(m_materials.begin(), m_materials.end(),
        [](const MaterialChoice& a, const MaterialChoice& b) {
            return Lower(a.name) < Lower(b.name);
        });
}

RoomBuilderPanel::Result RoomBuilderPanel::Draw(const std::string& assetRoot, bool* open) {
    Result result;
    if (m_materialRoot != assetRoot) RefreshMaterials(assetRoot);
    if (!ImGui::Begin(EditorPanels::Name(EditorPanels::Panel::RoomBuilder), open)) { ImGui::End(); return result; }

    ImGui::InputText("Room Name", m_roomName.data(), m_roomName.size());
    ImGui::DragFloat("Grid Size", &m_gridSize, 0.05f, 0.05f, 10.0f, "%.2f");
    m_gridSize = std::clamp(m_gridSize, 0.05f, 10.0f);
    if (ImGui::Button("Draw Room Outline", ImVec2(-1.0f, 0.0f))) {
        m_hasFirst = false;
        m_hasSecond = false;
        m_captureOutline = true;
    }
    if (m_captureOutline) {
        ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.45f, 1.0f),
            m_hasFirst ? "Click the opposite corner in the Viewport"
                       : "Click the first corner in the Viewport");
        if (ImGui::Button("Cancel Outline")) CancelCapture();
    }

    float first[3] = {m_first.x, m_first.y, m_first.z};
    float second[3] = {m_second.x, m_second.y, m_second.z};
    if (ImGui::DragFloat3("Corner A", first, 0.1f, 0.0f, 0.0f, "%.2f")) {
        m_first = Snap({first[0], first[1], first[2]}); m_hasFirst = true;
    }
    if (ImGui::DragFloat3("Corner B", second, 0.1f, 0.0f, 0.0f, "%.2f")) {
        m_second = Snap({second[0], m_first.y, second[2]});
        m_hasFirst = m_hasSecond = true;
    }
    if (m_hasFirst) {
        const glm::vec3 b = m_hasSecond ? m_second : m_hover;
        ImGui::Text("Room size: %.2f x %.2f", std::abs(b.x - m_first.x),
                    std::abs(b.z - m_first.z));
    }

    ImGui::SeparatorText("Structure");
    ImGui::DragFloat("Wall Height", &m_wallHeight, 0.1f, 0.2f, 100.0f, "%.2f");
    ImGui::DragFloat("Wall Thickness", &m_wallThickness, 0.02f, 0.05f, 10.0f, "%.2f");
    ImGui::DragFloat("Floor Thickness", &m_floorThickness, 0.02f, 0.05f, 10.0f, "%.2f");
    m_wallHeight = std::clamp(m_wallHeight, 0.2f, 100.0f);
    m_wallThickness = std::clamp(m_wallThickness, 0.05f, 10.0f);
    m_floorThickness = std::clamp(m_floorThickness, 0.05f, 10.0f);
    ImGui::Checkbox("Floor", &m_createFloor); ImGui::SameLine();
    ImGui::Checkbox("Ceiling", &m_createCeiling); ImGui::SameLine();
    ImGui::Checkbox("Corner Posts", &m_cornerPosts);
    ImGui::Checkbox("Generate Colliders", &m_colliders);

    ImGui::SeparatorText("Door Opening");
    ImGui::Checkbox("Add Doorway", &m_doorEnabled);
    if (m_doorEnabled) {
        static const char* walls[] = {"North (+Z)", "South (-Z)", "East (+X)", "West (-X)"};
        ImGui::Combo("Door Wall", &m_doorWall, walls, 4);
        ImGui::DragFloat("Door Width", &m_doorWidth, 0.05f, 0.4f, 20.0f, "%.2f");
        ImGui::DragFloat("Door Height", &m_doorHeight, 0.05f, 0.4f, 20.0f, "%.2f");
        ImGui::DragFloat("Door Offset", &m_doorOffset, 0.05f, -50.0f, 50.0f, "%.2f");
        m_doorWidth = std::max(m_doorWidth, 0.4f);
        m_doorHeight = std::clamp(
            m_doorHeight, 0.1f, std::max(0.1f, m_wallHeight - 0.05f));
    }

    ImGui::SeparatorText("Appearance");
    const std::string selectedMaterialName = m_materialPath.empty() ? "Default"
        : std::filesystem::path(m_materialPath).stem().string();
    if (ImGui::BeginCombo("Material", selectedMaterialName.c_str())) {
        if (ImGui::Selectable("Default", m_materialPath.empty())) m_materialPath.clear();
        for (const MaterialChoice& material : m_materials) {
            ImGui::PushID(material.path.c_str());
            if (ImGui::Selectable(material.name.c_str(), material.path == m_materialPath))
                m_materialPath = material.path;
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::Checkbox("Replace room with same name", &m_replaceExisting);

    ImGui::Separator();
    if (!HasRoom() || m_roomName[0] == '\0') ImGui::BeginDisabled();
    if (ImGui::Button("Generate Room", ImVec2(-1.0f, 0.0f))) result.generateRequested = true;
    if (!HasRoom() || m_roomName[0] == '\0') ImGui::EndDisabled();
    if (ImGui::Button("Delete Generated Room", ImVec2(-1.0f, 0.0f)))
        result.deleteExistingRequested = true;
    ImGui::TextDisabled("Generated pieces remain normal editable scene objects.");

    ImGui::End();
    return result;
}
