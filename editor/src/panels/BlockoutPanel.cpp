#include "BlockoutPanel.h"
#include "EditorPanels.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace {
std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}
}

void BlockoutPanel::RefreshMaterials(const std::string& assetRoot) {
    m_materialRoot = assetRoot;
    m_materials.clear();
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(
             assetRoot, std::filesystem::directory_options::skip_permission_denied, ec), end;
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

BlockoutPanel::Result BlockoutPanel::Draw(const std::string& assetRoot, bool* open) {
    Result result;
    if (m_materialRoot != assetRoot) RefreshMaterials(assetRoot);
    if (!ImGui::Begin(EditorPanels::Name(EditorPanels::Panel::Blockout), open)) {
        ImGui::End();
        return result;
    }

    static const char* shapes[] = {"Wall", "Floor", "Platform", "Ramp", "Doorway", "Stairs"};
    static const char* placements[] = {"Viewport Cursor", "Manual Position", "Selected Object"};
    ImGui::InputText("Group Name", m_groupName.data(), m_groupName.size());
    ImGui::Combo("Shape", &m_shape, shapes, 6);
    ImGui::Combo("Place At", &m_placement, placements, 3);
    if (PlacementMode() == Placement::Manual)
        ImGui::DragFloat3("Position", &m_position.x, 0.1f, -100000.0f, 100000.0f, "%.2f");

    ImGui::SeparatorText("Dimensions");
    const Shape shape = CurrentShape();
    const char* widthLabel = "Width";
    const char* heightLabel = shape == Shape::Floor || shape == Shape::Platform ? "Thickness" : "Height";
    const char* depthLabel = shape == Shape::Wall || shape == Shape::Doorway ? "Thickness" : "Depth / Run";
    ImGui::DragFloat(widthLabel, &m_dimensions.x, 0.05f, 0.05f, 10000.0f, "%.2f");
    ImGui::DragFloat(heightLabel, &m_dimensions.y, 0.05f, 0.05f, 10000.0f, "%.2f");
    ImGui::DragFloat(depthLabel, &m_dimensions.z, 0.05f, 0.05f, 10000.0f, "%.2f");
    m_dimensions = glm::clamp(m_dimensions, glm::vec3(0.05f), glm::vec3(10000.0f));
    ImGui::SliderFloat("Yaw", &m_yaw, -180.0f, 180.0f, "%.0f deg");

    if (shape == Shape::Doorway) {
        ImGui::DragFloat("Opening Width", &m_doorWidth, 0.05f, 0.2f,
                         std::max(0.2f, m_dimensions.x - 0.1f), "%.2f");
        ImGui::DragFloat("Opening Height", &m_doorHeight, 0.05f, 0.2f,
                         std::max(0.2f, m_dimensions.y - 0.05f), "%.2f");
        m_doorWidth = std::clamp(m_doorWidth, 0.2f, std::max(0.2f, m_dimensions.x - 0.1f));
        m_doorHeight = std::clamp(m_doorHeight, 0.2f, std::max(0.2f, m_dimensions.y - 0.05f));
    }
    if (shape == Shape::Stairs)
        ImGui::SliderInt("Steps", &m_steps, 2, 64);

    ImGui::SeparatorText("Output");
    const std::string materialName = m_materialPath.empty()
        ? "Default" : std::filesystem::path(m_materialPath).stem().string();
    if (ImGui::BeginCombo("Material", materialName.c_str())) {
        if (ImGui::Selectable("Default", m_materialPath.empty())) m_materialPath.clear();
        for (const MaterialChoice& material : m_materials)
            if (ImGui::Selectable(material.name.c_str(), material.path == m_materialPath))
                m_materialPath = material.path;
        ImGui::EndCombo();
    }
    ImGui::Checkbox("Generate Colliders", &m_collider);
    ImGui::Checkbox("Replace group with same name", &m_replace);
    ImGui::TextDisabled("The preview shows the overall occupied bounds.");

    const bool valid = m_groupName[0] != '\0';
    if (!valid) ImGui::BeginDisabled();
    if (ImGui::Button("Create / Update Blockout", ImVec2(-1.0f, 0.0f)))
        result.createRequested = true;
    if (!valid) ImGui::EndDisabled();
    if (ImGui::Button("Delete Generated Group", ImVec2(-1.0f, 0.0f)))
        result.deleteRequested = true;
    ImGui::TextDisabled("Generated pieces remain normal editable scene objects.");
    ImGui::End();
    return result;
}
