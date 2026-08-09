#include "SplineBuilderPanel.h"

#include "EditorPanels.h"
#include "EditorScene.h"

#include <engine/math/Spline.h>
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
const EditorScene::Object* FindSpline(const EditorScene& scene, const std::string& name) {
    for (const EditorScene::Object& object : scene.Objects())
        if (object.isSpline && object.name == name && object.splinePoints.size() >= 2) return &object;
    return nullptr;
}
}

void SplineBuilderPanel::RefreshAssets(const std::string& assetRoot) {
    m_assetRoot = assetRoot;
    m_materials.clear();
    m_models.clear();
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(
             assetRoot, std::filesystem::directory_options::skip_permission_denied, ec), end;
         it != end; it.increment(ec)) {
        if (ec || !it->is_regular_file(ec)) continue;
        const std::string ext = Lower(it->path().extension().string());
        AssetChoice choice{it->path().string(), it->path().stem().string()};
        if (ext == ".3dgmat") m_materials.push_back(std::move(choice));
        else if (ext == ".3dgmesh") m_models.push_back(std::move(choice));
    }
    auto sort = [](std::vector<AssetChoice>& choices) {
        std::sort(choices.begin(), choices.end(), [](const AssetChoice& a, const AssetChoice& b) {
            return Lower(a.name) < Lower(b.name);
        });
    };
    sort(m_materials);
    sort(m_models);
}

SplineBuilderPanel::Result SplineBuilderPanel::Draw(
    const EditorScene& scene, const std::string& assetRoot, bool* open) {
    Result result;
    if (m_assetRoot != assetRoot) RefreshAssets(assetRoot);
    if (!ImGui::Begin(EditorPanels::Name(EditorPanels::Panel::SplineBuilder), open)) { ImGui::End(); return result; }

    std::vector<const EditorScene::Object*> splines;
    for (const EditorScene::Object& object : scene.Objects())
        if (object.isSpline && object.splinePoints.size() >= 2) splines.push_back(&object);
    if (!FindSpline(scene, m_splineName) && !splines.empty()) m_splineName = splines.front()->name;

    ImGui::InputText("Group Name", m_groupName.data(), m_groupName.size());
    if (ImGui::BeginCombo("Spline", m_splineName.empty() ? "No spline available" : m_splineName.c_str())) {
        for (const EditorScene::Object* spline : splines)
            if (ImGui::Selectable(spline->name.c_str(), spline->name == m_splineName))
                m_splineName = spline->name;
        ImGui::EndCombo();
    }
    static const char* modes[] = {"Road", "Fence", "Prop Line"};
    ImGui::Combo("Build Type", &m_mode, modes, 3);
    ImGui::DragFloat("Segment / Item Spacing", &m_spacing, 0.05f, 0.1f, 1000.0f, "%.2f");
    m_spacing = std::clamp(m_spacing, 0.1f, 1000.0f);
    ImGui::DragFloat("Vertical Offset", &m_verticalOffset, 0.01f, -100.0f, 100.0f, "%.2f");

    if (CurrentMode() == Mode::Road) {
        ImGui::DragFloat("Road Width", &m_width, 0.05f, 0.1f, 1000.0f, "%.2f");
        ImGui::DragFloat("Road Thickness", &m_thickness, 0.01f, 0.02f, 100.0f, "%.2f");
    } else if (CurrentMode() == Mode::Fence) {
        ImGui::DragFloat("Fence Height", &m_height, 0.05f, 0.1f, 100.0f, "%.2f");
        ImGui::DragFloat("Post Size", &m_postSize, 0.01f, 0.02f, 10.0f, "%.2f");
        ImGui::DragFloat("Rail Thickness", &m_thickness, 0.01f, 0.02f, 10.0f, "%.2f");
        ImGui::SliderInt("Rails", &m_rails, 0, 6);
    } else {
        const std::string modelName = m_modelPath.empty() ? "Choose static mesh..."
            : std::filesystem::path(m_modelPath).stem().string();
        if (ImGui::BeginCombo("Prop Mesh", modelName.c_str())) {
            for (const AssetChoice& model : m_models)
                if (ImGui::Selectable(model.name.c_str(), model.path == m_modelPath))
                    m_modelPath = model.path;
            ImGui::EndCombo();
        }
        ImGui::DragFloat("Prop Scale", &m_propScale, 0.02f, 0.01f, 1000.0f, "%.2f");
        ImGui::Checkbox("Align props to spline", &m_alignProps);
    }
    m_width = std::max(m_width, 0.1f);
    m_height = std::max(m_height, 0.1f);
    m_thickness = std::max(m_thickness, 0.02f);
    m_postSize = std::max(m_postSize, 0.02f);
    m_propScale = std::max(m_propScale, 0.01f);

    if (CurrentMode() != Mode::Props) {
        const std::string materialName = m_materialPath.empty() ? "Default"
            : std::filesystem::path(m_materialPath).stem().string();
        if (ImGui::BeginCombo("Material", materialName.c_str())) {
            if (ImGui::Selectable("Default", m_materialPath.empty())) m_materialPath.clear();
            for (const AssetChoice& material : m_materials)
                if (ImGui::Selectable(material.name.c_str(), material.path == m_materialPath))
                    m_materialPath = material.path;
            ImGui::EndCombo();
        }
    }
    ImGui::Checkbox("Generate Colliders", &m_colliders);
    ImGui::Checkbox("Replace group with same name", &m_replace);

    const EditorScene::Object* spline = FindSpline(scene, m_splineName);
    if (spline) {
        const engine::Spline curve(spline->splinePoints, spline->splineClosed);
        ImGui::Text("Spline length: %.2f m | Estimated items: %d", curve.Length(),
            std::max(1, static_cast<int>(std::ceil(curve.Length() / m_spacing))));
    } else {
        ImGui::TextDisabled("Add or select a spline before generating.");
    }
    const bool valid = spline && m_groupName[0] != '\0'
        && (CurrentMode() != Mode::Props || !m_modelPath.empty());
    if (!valid) ImGui::BeginDisabled();
    if (ImGui::Button("Generate / Update", ImVec2(-1.0f, 0.0f))) result.generate = true;
    if (!valid) ImGui::EndDisabled();
    if (ImGui::Button("Delete Generated Group", ImVec2(-1.0f, 0.0f))) result.remove = true;
    ImGui::TextDisabled("Generated pieces are independent editable scene objects.");
    ImGui::End();
    return result;
}

std::vector<glm::vec3> SplineBuilderPanel::PreviewPoints(const EditorScene& scene) const {
    const EditorScene::Object* object = FindSpline(scene, m_splineName);
    if (!object) return {};
    const engine::Spline spline(object->splinePoints, object->splineClosed);
    const float length = spline.Length();
    const int count = std::clamp(static_cast<int>(std::ceil(length / std::max(m_spacing, 0.1f))), 1, 512);
    std::vector<glm::vec3> points;
    points.reserve(static_cast<std::size_t>(count + 1));
    for (int i = 0; i <= count; ++i) {
        if (object->splineClosed && i == count) break;
        const float distance = length * static_cast<float>(i) / static_cast<float>(count);
        points.push_back(spline.PositionAtDistance(distance) + glm::vec3(0, m_verticalOffset, 0));
    }
    return points;
}
