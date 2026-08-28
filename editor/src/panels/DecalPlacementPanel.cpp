#include "DecalPlacementPanel.h"
#include "EditorPanels.h"

#include <imgui.h>

#include <algorithm>
#include <filesystem>

void DecalPlacementPanel::SetHover(const glm::vec3& position,
                                   const glm::vec3& normal, bool valid) {
    m_hoverPosition = position;
    m_hoverNormal = glm::length(normal) > 0.0001f
        ? glm::normalize(normal) : glm::vec3(0, 1, 0);
    m_hoverValid = valid;
}

void DecalPlacementPanel::Draw(bool* open,
                               const std::vector<std::string>& materials,
                               const std::vector<std::string>& textures,
                               const std::string& selectedAssetPath) {
    if (!ImGui::Begin(EditorPanels::Name(EditorPanels::Panel::DecalPlacement), open)) {
        ImGui::End();
        return;
    }

    ImGui::TextWrapped("Place surface-aligned dirt, cracks, signs, puddles, scorch "
                       "marks and other transparent overlays.");
    ImGui::Separator();

    const std::string preview = m_settings.materialPath.empty()
        ? std::string("Choose a material or texture...")
        : std::filesystem::path(m_settings.materialPath).filename().string();
    if (ImGui::BeginCombo("Decal Asset", preview.c_str())) {
        if (ImGui::Selectable("None", m_settings.materialPath.empty()))
            m_settings.materialPath.clear();
        if (!materials.empty()) ImGui::SeparatorText("Materials");
        for (const std::string& path : materials) {
            const std::string label = std::filesystem::path(path).filename().string();
            ImGui::PushID(path.c_str());
            if (ImGui::Selectable(label.c_str(), m_settings.materialPath == path))
                m_settings.materialPath = path;
            ImGui::PopID();
        }
        if (!textures.empty()) ImGui::SeparatorText("Textures");
        for (const std::string& path : textures) {
            const std::string label = std::filesystem::path(path).filename().string();
            ImGui::PushID(path.c_str());
            if (ImGui::Selectable(label.c_str(), m_settings.materialPath == path))
                m_settings.materialPath = path;
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    const std::string extension = std::filesystem::path(selectedAssetPath).extension().string();
    const bool selectedUsable = extension == ".3dgmat" || extension == ".3dgtex"
        || extension == ".png" || extension == ".jpg" || extension == ".jpeg"
        || extension == ".tga";
    if (!selectedUsable) ImGui::BeginDisabled();
    if (ImGui::Button("Use Selected Content Asset", ImVec2(-1.0f, 0.0f)))
        m_settings.materialPath = selectedAssetPath;
    if (!selectedUsable) ImGui::EndDisabled();

    ImGui::DragFloat2("Size", &m_settings.size.x, 0.05f, 0.05f, 100.0f, "%.2f m");
    m_settings.size = glm::clamp(m_settings.size, glm::vec2(0.05f), glm::vec2(100.0f));
    ImGui::SliderFloat("Rotation", &m_settings.rotationDegrees, -180.0f, 180.0f, "%.0f deg");
    ImGui::SliderFloat("Surface Offset", &m_settings.surfaceOffset, 0.001f, 0.08f, "%.3f m");
    ImGui::SliderFloat("Opacity", &m_settings.opacity, 0.0f, 1.0f, "%.2f");
    ImGui::Separator();

    const bool missingAsset = m_settings.materialPath.empty();
    if (missingAsset) ImGui::BeginDisabled();
    if (ImGui::Button(m_placementActive ? "Stop Placing" : "Place Decals",
                      ImVec2(-1.0f, 0.0f)))
        m_placementActive = !m_placementActive;
    if (missingAsset) ImGui::EndDisabled();
    if (missingAsset) ImGui::TextDisabled("Choose a decal material or texture first.");
    else if (m_placementActive)
        ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f),
                           "Click a visible surface in the viewport to place.");
    ImGui::TextDisabled("Placed decals are ordinary selectable scene actors. Transform "
                        "them with W/E/R and assign a different material in Inspector.");
    ImGui::End();
}
