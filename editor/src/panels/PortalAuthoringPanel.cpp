#include "PortalAuthoringPanel.h"
#include "EditorPanels.h"

#include <imgui.h>

#include <cstdio>
#include <filesystem>

namespace {
bool Text(const char* label, std::string& value) {
    char buffer[512]{}; std::snprintf(buffer, sizeof(buffer), "%s", value.c_str());
    if (!ImGui::InputText(label, buffer, sizeof(buffer))) return false;
    value = buffer; return true;
}
}

void PortalAuthoringPanel::New(const std::string& root) {
    m_asset = {}; m_asset.header.id = engine::AssetHandle::Generate();
    m_asset.name = "NewPortal";
    m_path = (std::filesystem::path(root) / "GameAssets" / "Portals" /
              "NewPortal.3dgportal").string();
    m_status.clear(); m_dirty = true;
}

bool PortalAuthoringPanel::SaveForShutdown(const std::string& root, std::string* error) {
    if (m_path.empty()) New(root);
    if (!engine::SavePortalAsset(m_path, m_asset, error)) return false;
    m_dirty = false; return true;
}

bool PortalAuthoringPanel::AssetCombo(const char* label, EditorAssets::Type type,
                                      std::string& path, engine::AssetHandle& id,
                                      EditorAssets& assets, const char* empty) {
    bool changed = false;
    const std::string preview = path.empty() ? empty : std::filesystem::path(path).filename().string();
    if (ImGui::BeginCombo(label, preview.c_str())) {
        if (ImGui::Selectable(empty, path.empty())) { path.clear(); id = {}; changed = true; }
        for (const auto& candidate : assets.ContentAssetPaths(type)) {
            ImGui::PushID(candidate.c_str());
            const std::string name = std::filesystem::path(candidate).filename().string();
            if (ImGui::Selectable(name.c_str(), candidate == path)) {
                path = candidate; id = assets.AssetIdForPath(candidate); changed = true;
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    return changed;
}

void PortalAuthoringPanel::DrawPreview() const {
    const ImVec2 size(ImGui::GetContentRegionAvail().x, 230.0f);
    ImGui::InvisibleButton("##PortalPreview", size);
    auto* draw = ImGui::GetWindowDrawList(); const ImVec2 p = ImGui::GetItemRectMin();
    draw->AddRectFilled(p, {p.x + size.x, p.y + size.y}, IM_COL32(18, 20, 25, 255));
    const ImVec2 a{p.x + size.x * .28f, p.y + size.y * .55f};
    const ImVec2 b{p.x + size.x * .72f, p.y + size.y * .55f};
    draw->AddCircle(a, 45.0f, IM_COL32(90, 190, 255, 255), 0, 7.0f);
    draw->AddCircle(b, 45.0f, IM_COL32(190, 100, 255, 255), 0, 7.0f);
    draw->AddLine({a.x + 55, a.y}, {b.x - 55, b.y}, IM_COL32(245, 190, 70, 255), 3.0f);
    draw->AddTriangleFilled({b.x - 55, b.y}, {b.x - 70, b.y - 9},
                            {b.x - 70, b.y + 9}, IM_COL32(245, 190, 70, 255));
    draw->AddText({a.x - 30, a.y + 58}, IM_COL32_WHITE, "ENTRY");
    const std::string destination = m_asset.mode == engine::PortalMode::SameLevel
        ? (m_asset.destinationObject.empty() ? "DESTINATION" : m_asset.destinationObject)
        : (m_asset.destinationLevel.empty() ? "LEVEL" : std::filesystem::path(m_asset.destinationLevel).stem().string());
    draw->AddText({b.x - 42, b.y + 58}, IM_COL32_WHITE, destination.c_str());
}

PortalAuthoringPanel::Result PortalAuthoringPanel::Draw(
    EditorAssets& assets, const std::vector<std::string>& objects,
    const std::string& root, bool* open) {
    Result result;
    if (!m_asset.header.id.Valid()) New(root);
    if (!m_pendingOpen.empty()) {
        std::string error;
        if (engine::LoadPortalAsset(m_pendingOpen, &m_asset, &error)) {
            m_path = m_pendingOpen; m_dirty = false; m_status = "Loaded " + m_path;
        } else m_status = error;
        m_pendingOpen.clear();
    }
    if (!ImGui::Begin(EditorPanels::Name(EditorPanels::Panel::PortalAuthoring), open)) {
        ImGui::End(); return result;
    }
    if (ImGui::Button("New")) New(root);
    ImGui::SameLine(); if (ImGui::Button("Save")) {
        std::string error;
        if (engine::SavePortalAsset(m_path, m_asset, &error)) {
            m_dirty = false; result.saved = true; result.message = "Saved portal asset: " + m_path;
        } else result.message = error;
    }
    ImGui::SameLine(); if (ImGui::Button("Apply to Selected")) result.applySelected = true;
    ImGui::SameLine(); ImGui::TextUnformatted(m_path.c_str());
    m_dirty |= Text("Name", m_asset.name);
    if (ImGui::BeginCombo("Portal Mode", engine::PortalModeName(m_asset.mode))) {
        for (int i = 0; i < 3; ++i) { const auto mode = static_cast<engine::PortalMode>(i);
            if (ImGui::Selectable(engine::PortalModeName(mode), mode == m_asset.mode)) { m_asset.mode = mode; m_dirty = true; }
        } ImGui::EndCombo();
    }
    ImGui::SeparatorText("Destination");
    if (m_asset.mode == engine::PortalMode::SameLevel) {
        const char* preview = m_asset.destinationObject.empty() ? "Choose scene object..." : m_asset.destinationObject.c_str();
        if (ImGui::BeginCombo("Destination Object", preview)) {
            for (const auto& object : objects) { ImGui::PushID(object.c_str());
                if (ImGui::Selectable(object.c_str(), object == m_asset.destinationObject)) { m_asset.destinationObject = object; m_dirty = true; }
                ImGui::PopID();
            } ImGui::EndCombo();
        }
    } else m_dirty |= AssetCombo("Destination Level", EditorAssets::Type::Scene,
        m_asset.destinationLevel, m_asset.destinationLevelId, assets, "Choose saved level...");
    m_dirty |= ImGui::DragFloat3("Arrival Offset", &m_asset.arrivalOffset.x, .05f, -10000.f, 10000.f, "%.2f m");
    m_dirty |= ImGui::DragFloat3("Arrival Rotation", &m_asset.arrivalRotationDegrees.x, 1.f, -360.f, 360.f, "%.1f deg");
    m_dirty |= ImGui::Checkbox("Align Facing", &m_asset.alignFacing); ImGui::SameLine();
    m_dirty |= ImGui::Checkbox("Preserve Velocity", &m_asset.preserveVelocity); ImGui::SameLine();
    m_dirty |= ImGui::Checkbox("Safe Arrival", &m_asset.safeArrival);
    ImGui::SeparatorText("Activation");
    m_dirty |= ImGui::DragFloat("Activation Radius", &m_asset.activationRadius, .05f, .05f, 10000.f, "%.2f m");
    m_dirty |= ImGui::DragFloat("Cooldown", &m_asset.cooldown, .05f, 0.f, 3600.f, "%.2f s");
    m_dirty |= ImGui::Checkbox("Automatic", &m_asset.automatic); ImGui::SameLine();
    m_dirty |= ImGui::Checkbox("One Way", &m_asset.oneWay);
    m_dirty |= Text("Required Access Tag", m_asset.requiredAccessTag);
    m_dirty |= Text("Prompt", m_asset.prompt);
    ImGui::SeparatorText("Presentation");
    m_dirty |= AssetCombo("Enter Sound", EditorAssets::Type::Audio, m_asset.enterAudioPath, m_asset.enterAudioId, assets, "None");
    m_dirty |= AssetCombo("Exit Sound", EditorAssets::Type::Audio, m_asset.exitAudioPath, m_asset.exitAudioId, assets, "None");
    m_dirty |= AssetCombo("Transition Effect", EditorAssets::Type::ParticleEffect, m_asset.transitionEffectPath, m_asset.transitionEffectId, assets, "None");
    engine::NormalizePortalAsset(m_asset);
    ImGui::SeparatorText("Destination Preview"); DrawPreview();
    std::string validation;
    if (!engine::ValidatePortalAsset(m_asset, &validation)) ImGui::TextColored({1,.45f,.3f,1}, "%s", validation.c_str());
    if (m_dirty) ImGui::TextColored({1,.7f,.2f,1}, "Unsaved changes");
    if (!m_status.empty()) ImGui::TextWrapped("%s", m_status.c_str());
    ImGui::End(); return result;
}
