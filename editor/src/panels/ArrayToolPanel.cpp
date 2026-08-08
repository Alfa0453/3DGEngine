#include "ArrayToolPanel.h"

#include "EditorScene.h"

#include <engine/math/Spline.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace {

glm::quat FaceDirection(const glm::vec3& direction, glm::vec3 up) {
    glm::vec3 forward = glm::dot(direction, direction) > 1.0e-8f
        ? glm::normalize(direction) : glm::vec3(0, 0, 1);
    if (std::abs(glm::dot(forward, up)) > 0.98f) up = glm::vec3(1, 0, 0);
    glm::vec3 right = glm::normalize(glm::cross(up, forward));
    up = glm::normalize(glm::cross(forward, right));
    return glm::normalize(glm::quat_cast(glm::mat3(right, up, forward)));
}

} // namespace

ArrayToolPanel::Result ArrayToolPanel::Draw(const EditorScene& scene, bool* open) {
    Result result;
    if (!ImGui::Begin("Smart Duplicate & Array", open)) { ImGui::End(); return result; }

    const EditorScene::Object* selected = scene.SelectedObject();
    ImGui::Text("Source: %s", selected ? selected->name.c_str() : "Select a scene object");
    if (selected && selected->locked)
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.2f, 1.0f), "The selected source is locked.");
    ImGui::InputText("Group Name", m_groupName.data(), m_groupName.size());
    ImGui::Checkbox("Replace Existing Group", &m_replaceExisting);

    ImGui::SeparatorText("Layout");
    const char* layouts[] = {"Linear", "Grid", "Radial", "Spline Path"};
    int layout = static_cast<int>(m_layout);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::Combo("##ArrayLayout", &layout, layouts, 4))
        m_layout = static_cast<Layout>(layout);

    if (m_layout == Layout::Linear) {
        ImGui::DragInt("Copies", &m_linearCount, 1.0f, 1, MaximumCopies());
        ImGui::DragFloat3("Spacing", &m_linearSpacing.x, 0.05f, -1000.0f, 1000.0f, "%.2f");
        ImGui::Checkbox("Use Source Local Axes", &m_localSpace);
        ImGui::DragFloat("Yaw per Copy", &m_rotationStep, 1.0f, -360.0f, 360.0f, "%.1f deg");
        ImGui::DragFloat("Scale Multiplier", &m_scaleStep, 0.01f, 0.05f, 4.0f, "%.2fx");
        m_linearCount = std::clamp(m_linearCount, 1, MaximumCopies());
        m_scaleStep = std::clamp(m_scaleStep, 0.05f, 4.0f);
    } else if (m_layout == Layout::Grid) {
        ImGui::DragInt3("Count XYZ", &m_gridCount.x, 1.0f, 1, 100);
        ImGui::DragFloat3("Cell Spacing", &m_gridSpacing.x, 0.05f, -1000.0f, 1000.0f, "%.2f");
        ImGui::Checkbox("Use Source Local Axes", &m_localSpace);
        m_gridCount = glm::clamp(m_gridCount, glm::ivec3(1), glm::ivec3(100));
        const int total = m_gridCount.x * m_gridCount.y * m_gridCount.z - 1;
        ImGui::TextDisabled("%d copies (source occupies cell 0,0,0)", std::max(total, 0));
        if (total > MaximumCopies())
            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.2f, 1.0f), "Preview and creation are capped at 2000 copies.");
    } else if (m_layout == Layout::Radial) {
        ImGui::DragInt("Copies", &m_radialCount, 1.0f, 1, MaximumCopies());
        ImGui::DragFloat("Radius", &m_radialRadius, 0.05f, 0.0f, 10000.0f, "%.2f m");
        ImGui::DragFloat("Arc", &m_radialArc, 1.0f, -360.0f, 360.0f, "%.1f deg");
        ImGui::DragFloat("Start Angle", &m_radialStart, 1.0f, -360.0f, 360.0f, "%.1f deg");
        const char* axes[] = {"X", "Y", "Z"};
        ImGui::Combo("Axis", &m_radialAxis, axes, 3);
        ImGui::Checkbox("Face Outward", &m_radialFaceOutward);
        m_radialCount = std::clamp(m_radialCount, 1, MaximumCopies());
        m_radialRadius = std::clamp(m_radialRadius, 0.0f, 10000.0f);
    } else {
        std::vector<std::string> splines;
        for (const EditorScene::Object& object : scene.Objects())
            if (object.isSpline && object.splinePoints.size() >= 2) splines.push_back(object.name);
        if (std::find(splines.begin(), splines.end(), m_splineName) == splines.end())
            m_splineName = splines.empty() ? std::string() : splines.front();
        const char* preview = m_splineName.empty() ? "No spline available" : m_splineName.c_str();
        if (ImGui::BeginCombo("Spline", preview)) {
            for (const std::string& name : splines) {
                if (ImGui::Selectable(name.c_str(), name == m_splineName)) m_splineName = name;
            }
            ImGui::EndCombo();
        }
        ImGui::DragInt("Copies", &m_splineCount, 1.0f, 1, MaximumCopies());
        ImGui::DragFloatRange2("Path Range", &m_splineStart, &m_splineEnd,
                               0.005f, 0.0f, 1.0f, "%.2f", "%.2f");
        ImGui::DragFloat3("Path Offset", &m_splineOffset.x, 0.05f, -1000.0f, 1000.0f, "%.2f");
        ImGui::Checkbox("Align to Path", &m_splineAlign);
        m_splineCount = std::clamp(m_splineCount, 1, MaximumCopies());
        m_splineStart = std::clamp(m_splineStart, 0.0f, 1.0f);
        m_splineEnd = std::clamp(m_splineEnd, m_splineStart, 1.0f);
    }

    const engine::ecs::Transform* source = selected ? scene.TryGetTransform(selected->entity) : nullptr;
    const int previewCount = source
        ? static_cast<int>(BuildTransforms(*source, scene).size()) : 0;
    ImGui::Separator();
    ImGui::Text("Preview: %d copies", previewCount);
    const bool canCreate = source && !selected->locked && previewCount > 0
        && m_groupName[0] != '\0';
    if (!canCreate) ImGui::BeginDisabled();
    if (ImGui::Button("Create Array", ImVec2(-1, 0))) result.createRequested = true;
    if (!canCreate) ImGui::EndDisabled();
    if (ImGui::Button("Delete Generated Group", ImVec2(-1, 0)))
        result.deleteGeneratedRequested = true;
    ImGui::TextDisabled("The source is preserved; generated copies remain fully editable.");

    ImGui::End();
    return result;
}

std::vector<engine::ecs::Transform> ArrayToolPanel::BuildTransforms(
    const engine::ecs::Transform& source, const EditorScene& scene) const {
    std::vector<engine::ecs::Transform> transforms;
    transforms.reserve(128);
    auto push = [&](engine::ecs::Transform transform) {
        if (static_cast<int>(transforms.size()) < MaximumCopies())
            transforms.push_back(transform);
    };

    if (m_layout == Layout::Linear) {
        for (int i = 1; i <= m_linearCount; ++i) {
            engine::ecs::Transform transform = source;
            glm::vec3 offset = m_linearSpacing * static_cast<float>(i);
            if (m_localSpace) offset = glm::mat3_cast(source.rotation) * offset;
            transform.position = source.position + offset;
            transform.rotation = glm::angleAxis(glm::radians(m_rotationStep * i), glm::vec3(0, 1, 0))
                * source.rotation;
            transform.scale = source.scale * std::pow(m_scaleStep, static_cast<float>(i));
            push(transform);
        }
    } else if (m_layout == Layout::Grid) {
        for (int z = 0; z < m_gridCount.z; ++z) {
            for (int y = 0; y < m_gridCount.y; ++y) {
                for (int x = 0; x < m_gridCount.x; ++x) {
                    if (x == 0 && y == 0 && z == 0) continue;
                    engine::ecs::Transform transform = source;
                    glm::vec3 offset = glm::vec3(x, y, z) * m_gridSpacing;
                    if (m_localSpace) offset = glm::mat3_cast(source.rotation) * offset;
                    transform.position = source.position + offset;
                    push(transform);
                    if (static_cast<int>(transforms.size()) >= MaximumCopies()) return transforms;
                }
            }
        }
    } else if (m_layout == Layout::Radial) {
        const glm::vec3 axis = m_radialAxis == 0 ? glm::vec3(1, 0, 0)
            : (m_radialAxis == 1 ? glm::vec3(0, 1, 0) : glm::vec3(0, 0, 1));
        const glm::vec3 a = m_radialAxis == 0 ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
        const glm::vec3 b = glm::normalize(glm::cross(axis, a));
        const bool closed = std::abs(m_radialArc) >= 359.999f;
        const float denominator = static_cast<float>(std::max(1, closed ? m_radialCount : m_radialCount - 1));
        for (int i = 0; i < m_radialCount; ++i) {
            const float angle = glm::radians(m_radialStart
                + m_radialArc * static_cast<float>(i) / denominator);
            const glm::vec3 outward = a * std::cos(angle) + b * std::sin(angle);
            engine::ecs::Transform transform = source;
            transform.position = source.position + outward * m_radialRadius;
            if (m_radialFaceOutward) transform.rotation = FaceDirection(outward, axis);
            push(transform);
        }
    } else {
        const EditorScene::Object* splineObject = nullptr;
        for (const EditorScene::Object& object : scene.Objects()) {
            if (object.isSpline && object.name == m_splineName
                && object.splinePoints.size() >= 2) {
                splineObject = &object;
                break;
            }
        }
        if (!splineObject) return transforms;
        const engine::Spline spline(splineObject->splinePoints, splineObject->splineClosed);
        const float length = spline.Length();
        for (int i = 0; i < m_splineCount; ++i) {
            const float fraction = m_splineCount <= 1 ? m_splineStart
                : glm::mix(m_splineStart, m_splineEnd,
                    static_cast<float>(i) / static_cast<float>(m_splineCount - 1));
            engine::ecs::Transform transform = source;
            transform.position = spline.PositionAtDistance(length * fraction) + m_splineOffset;
            if (m_splineAlign)
                transform.rotation = FaceDirection(spline.TangentAtDistance(length * fraction),
                                                    glm::vec3(0, 1, 0));
            push(transform);
        }
    }
    return transforms;
}
