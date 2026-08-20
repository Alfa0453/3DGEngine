#include "BiomeEditorPanel.h"
#include "EditorScene.h"

#include "EditorPanels.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>

namespace {
std::string Safe(std::string value) {
    for (char& c : value) if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-')) c = '_';
    return value.empty() ? "Biome" : value;
}
const char* FileName(const std::string& path) {
    static std::string value; value = path.empty() ? "None" : std::filesystem::path(path).filename().string(); return value.c_str();
}
}

void BiomeEditorPanel::NewBiome() {
    m_biome = {}; m_biome.layers.push_back({}); m_path.clear(); m_dirty = true;
    m_status = "New biome"; RefreshPreview();
}

bool BiomeEditorPanel::Load(const std::string& path, std::string* error) {
    engine::BiomeAssetData loaded;
    if (!engine::LoadBiomeAsset(path, &loaded, error)) return false;
    m_biome = std::move(loaded); m_path = path; m_dirty = false; RefreshPreview(); return true;
}

bool BiomeEditorPanel::Save(const std::string& root, std::string* error) {
    if (m_path.empty()) m_path = (std::filesystem::path(root) / "GameAssets" / "Biomes"
        / (Safe(m_biome.name) + ".3dgbiome")).string();
    if (!engine::SaveBiomeAsset(m_path, m_biome, error)) return false;
    m_dirty = false; return true;
}

bool BiomeEditorPanel::AssetCombo(const char* label, EditorAssets::Type type,
    std::string& path, engine::AssetHandle& id, EditorAssets& assets) {
    bool changed = false;
    if (ImGui::BeginCombo(label, FileName(path))) {
        if (ImGui::Selectable("None", path.empty())) { path.clear(); id = {}; changed = true; }
        for (const std::string& candidate : assets.ContentAssetPaths(type)) {
            const std::string name = std::filesystem::path(candidate).filename().string();
            if (ImGui::Selectable(name.c_str(), path == candidate)) {
                path = candidate; id = assets.AssetIdForPath(candidate); changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

void BiomeEditorPanel::RefreshPreview() {
    engine::BiomeAssetData preview = m_biome;
    preview.maximumInstances = std::min(preview.maximumInstances, 1800);
    m_preview = engine::EvaluateBiome(preview, [&](float x, float z) {
        engine::BiomeSurfaceSample s;
        const float size = std::max(preview.previewWorldSize, 1.0f);
        s.normalizedHeight = std::clamp(0.46f + std::sin(x / size * 10.0f) * 0.18f
            + std::cos(z / size * 7.0f) * 0.12f, 0.0f, 1.0f);
        s.height = s.normalizedHeight * 8.0f;
        s.normal = glm::normalize(glm::vec3(-0.18f * std::cos(x / size * 10.0f), 1.0f,
                                             0.12f * std::sin(z / size * 7.0f)));
        s.moisture = std::clamp(preview.moisture + std::sin(x * 0.11f) * 0.16f, 0.0f, 1.0f);
        s.temperature = std::clamp(preview.temperature + std::cos(z * 0.09f) * 0.14f, 0.0f, 1.0f);
        return s;
    });
}

void BiomeEditorPanel::DrawPreview() {
    const ImVec2 size(ImGui::GetContentRegionAvail().x, std::max(300.0f, ImGui::GetContentRegionAvail().y));
    ImGui::InvisibleButton("##BiomePreview", size);
    const ImVec2 p = ImGui::GetItemRectMin(); ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(p, {p.x + size.x, p.y + size.y}, IM_COL32(110, 150, 190, 255));
    const ImVec2 landMin(p.x + 22, p.y + 55), landMax(p.x + size.x - 22, p.y + size.y - 22);
    draw->AddRectFilled(landMin, landMax, IM_COL32(62, 98, 55, 255), 7.0f);
    const float world = std::max(m_biome.previewWorldSize, 1.0f);
    if (m_biome.waterEnabled) {
        const float water = std::clamp(0.5f - m_biome.waterLevel / 16.0f, 0.05f, 0.95f);
        const float y = landMin.y + (landMax.y - landMin.y) * water;
        draw->AddRectFilled({landMin.x, y}, landMax, IM_COL32(30, 115, 160, 170), 5.0f);
    }
    for (const auto& placement : m_preview) {
        const float u = placement.position.x / world + 0.5f;
        const float v = placement.position.z / world + 0.5f;
        const ImVec2 q(landMin.x + u * (landMax.x - landMin.x),
                       landMin.y + v * (landMax.y - landMin.y));
        const ImU32 color = IM_COL32(80 + static_cast<int>((placement.foliageRule * 61) % 100),
                                     210, 70 + static_cast<int>((placement.foliageRule * 37) % 100), 220);
        draw->AddCircleFilled(q, 1.5f, color);
    }
    draw->AddText({p.x + 10, p.y + 10}, IM_COL32_WHITE,
        (m_biome.name + " | " + std::to_string(m_preview.size()) + " preview instances").c_str());
    draw->AddText({p.x + 10, p.y + 30}, IM_COL32(225, 235, 245, 255),
        ("Moisture " + std::to_string(m_biome.moisture).substr(0, 4)
         + "  Temperature " + std::to_string(m_biome.temperature).substr(0, 4)).c_str());
}

BiomeEditorPanel::Result BiomeEditorPanel::Draw(EditorScene& scene, EditorAssets& assets,
    const std::string& root, bool* open) {
    Result result;
    if (!m_pendingOpen.empty()) { std::string error; m_status = Load(m_pendingOpen, &error) ? "Loaded biome" : error; m_pendingOpen.clear(); }
    if (!ImGui::Begin(EditorPanels::Name(EditorPanels::Panel::BiomeEditor), open)) { ImGui::End(); return result; }
    if (ImGui::Button("New")) NewBiome(); ImGui::SameLine();
    if (ImGui::Button("Save")) { std::string error; if (Save(root, &error)) { result.saved = true; result.message = "Saved biome: " + m_path; } else result.message = error; }
    ImGui::SameLine(); if (ImGui::Button("Refresh Preview")) RefreshPreview();
    ImGui::SameLine();
    const EditorScene::Object* target = scene.FindObject(m_applyTarget);
    const bool validTarget = target && target->isTerrain && !target->locked;
    ImGui::BeginDisabled(!validTarget);
    const std::string applyLabel = validTarget
        ? "Apply to \"" + target->name + "\"" : "Apply (No Landscape Target)";
    if (ImGui::Button(applyLabel.c_str())) result.applyRequested = true;
    ImGui::EndDisabled();
    ImGui::SameLine(); ImGui::TextDisabled("%s", m_dirty ? "Unsaved" : "Saved");
    ImGui::TextWrapped("%s", m_path.empty() ? "New biome asset" : m_path.c_str());
    if (target) ImGui::Text("Apply target: %s", target->name.c_str());
    else if (m_applyTarget != engine::ecs::kNull) m_applyTarget = engine::ecs::kNull;
    if (const EditorScene::Object* selected = scene.SelectedObject()) {
        if (selected->entity != m_applyTarget) {
            ImGui::BeginDisabled(!selected->isTerrain || selected->locked);
            if (ImGui::Button(("Use \"" + selected->name + "\" As Apply Target").c_str()))
                m_applyTarget = selected->entity;
            ImGui::EndDisabled();
        }
    }
    if (target && scene.SelectedObject() && scene.SelectedObject()->entity != target->entity)
        ImGui::TextColored(ImVec4(1, .7f, .2f, 1), "Current selection differs from apply target.");
    ImGui::BeginChild("BiomeSettings", {430, 0}, true);
    bool changed = false;
    changed |= ImGui::InputText("Name", &m_biome.name);
    int seed = static_cast<int>(m_biome.seed); if (ImGui::InputInt("Seed", &seed)) { m_biome.seed = static_cast<unsigned>(std::max(seed, 0)); changed = true; }
    changed |= ImGui::DragFloat("World Size", &m_biome.previewWorldSize, 1.0f, 1.0f, 100000.0f, "%.1f m");
    changed |= ImGui::DragInt("Maximum Instances", &m_biome.maximumInstances, 50, 0, 1000000);
    changed |= ImGui::DragFloat("Transition Distance", &m_biome.transitionDistance, 0.1f, 0.0f, 10000.0f);
    changed |= ImGui::SliderFloat("Moisture", &m_biome.moisture, 0, 1);
    changed |= ImGui::SliderFloat("Temperature", &m_biome.temperature, 0, 1);
    if (ImGui::CollapsingHeader("Terrain Layers", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (m_biome.layers.size() < 5 && ImGui::Button("Add Layer")) { m_biome.layers.push_back({}); changed = true; }
        for (std::size_t i = 0; i < m_biome.layers.size(); ++i) {
            auto& layer = m_biome.layers[i]; ImGui::PushID(static_cast<int>(i));
            const std::string title = layer.name.empty() ? "Layer" : layer.name;
            if (ImGui::TreeNode("Layer", "%zu: %s", i + 1, title.c_str())) {
                changed |= ImGui::InputText("Name", &layer.name);
                changed |= AssetCombo("Material", EditorAssets::Type::Material, layer.materialPath, layer.materialId, assets);
                changed |= ImGui::DragFloatRange2("Height", &layer.heightMin, &layer.heightMax, .01f, 0, 1);
                changed |= ImGui::DragFloatRange2("Slope", &layer.slopeMinDegrees, &layer.slopeMaxDegrees, .5f, 0, 90, "Min %.1f", "Max %.1f");
                changed |= ImGui::DragFloatRange2("Moisture", &layer.moistureMin, &layer.moistureMax, .01f, 0, 1);
                changed |= ImGui::DragFloatRange2("Temperature", &layer.temperatureMin, &layer.temperatureMax, .01f, 0, 1);
                if (ImGui::Button("Remove Layer")) { m_biome.layers.erase(m_biome.layers.begin() + static_cast<std::ptrdiff_t>(i)); changed = true; ImGui::TreePop(); ImGui::PopID(); break; }
                ImGui::TreePop();
            } ImGui::PopID();
        }
    }
    if (ImGui::CollapsingHeader("Weighted Foliage", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button("Add Foliage Rule")) { m_biome.foliage.push_back({}); changed = true; }
        for (std::size_t i = 0; i < m_biome.foliage.size(); ++i) {
            auto& rule = m_biome.foliage[i]; ImGui::PushID(1000 + static_cast<int>(i));
            if (ImGui::TreeNode("Rule", "%zu: %s", i + 1, rule.name.c_str())) {
                changed |= ImGui::InputText("Name", &rule.name);
                changed |= AssetCombo("Static Mesh", EditorAssets::Type::Model, rule.meshPath, rule.meshId, assets);
                changed |= ImGui::DragFloat("Weight", &rule.weight, .05f, 0, 100);
                changed |= ImGui::DragFloat("Density / m2", &rule.density, .001f, 0, 100, "%.3f");
                changed |= ImGui::DragFloatRange2("Scale", &rule.scaleRange.x, &rule.scaleRange.y, .01f, .01f, 100);
                changed |= ImGui::DragFloatRange2("Height", &rule.heightMin, &rule.heightMax, .01f, 0, 1);
                changed |= ImGui::DragFloatRange2("Slope", &rule.slopeMinDegrees, &rule.slopeMaxDegrees, .5f, 0, 90);
                changed |= ImGui::DragFloatRange2("Moisture", &rule.moistureMin, &rule.moistureMax, .01f, 0, 1);
                changed |= ImGui::DragFloatRange2("Temperature", &rule.temperatureMin, &rule.temperatureMax, .01f, 0, 1);
                changed |= ImGui::Checkbox("Align to Surface", &rule.alignToSurface);
                changed |= ImGui::Checkbox("Cast Shadows", &rule.castShadows);
                if (ImGui::Button("Remove Rule")) { m_biome.foliage.erase(m_biome.foliage.begin() + static_cast<std::ptrdiff_t>(i)); changed = true; ImGui::TreePop(); ImGui::PopID(); break; }
                ImGui::TreePop();
            } ImGui::PopID();
        }
    }
    if (ImGui::CollapsingHeader("Environment", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= AssetCombo("Weather", EditorAssets::Type::Weather, m_biome.weatherPath, m_biome.weatherId, assets);
        changed |= ImGui::Checkbox("Water Enabled", &m_biome.waterEnabled);
        if (m_biome.waterEnabled) { changed |= ImGui::DragFloat("Water Level", &m_biome.waterLevel, .1f); changed |= AssetCombo("Water Material", EditorAssets::Type::Material, m_biome.waterMaterialPath, m_biome.waterMaterialId, assets); }
        changed |= AssetCombo("Ambient Particles", EditorAssets::Type::Particle, m_biome.particlePath, m_biome.particleId, assets);
        changed |= AssetCombo("Ambient Audio", EditorAssets::Type::Audio, m_biome.ambientAudioPath, m_biome.ambientAudioId, assets);
    }
    if (changed) { engine::NormalizeBiome(m_biome); m_dirty = true; }
    ImGui::EndChild(); ImGui::SameLine();
    ImGui::BeginChild("BiomePreview", {0, 0}, true); DrawPreview(); ImGui::EndChild();
    if (!m_status.empty()) ImGui::TextWrapped("%s", m_status.c_str());
    ImGui::End(); return result;
}
