#include "AlignmentPanel.h"

#include "EditorPanels.h"
#include "EditorScene.h"

#include <imgui.h>

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

namespace {

using Transform = engine::ecs::Transform;

struct Entry {
    int index = -1;
    Transform transform;
    glm::vec3 extent{0.5f};
};

std::vector<Entry> EditableSelection(const EditorScene& scene) {
    std::vector<Entry> entries;
    for (int index : scene.SelectedIndices()) {
        if (index < 0 || index >= static_cast<int>(scene.Objects().size())) continue;
        const EditorScene::Object& object = scene.Objects()[static_cast<std::size_t>(index)];
        if (object.locked) continue;
        const Transform* transform = scene.TryGetTransform(object.entity);
        if (!transform) continue;
        const glm::mat3 rotation = glm::mat3_cast(transform->rotation);
        const glm::vec3 half = glm::abs(transform->scale) * 0.5f;
        glm::vec3 extent(0.0f);
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 3; ++col)
                extent[row] += std::abs(rotation[col][row]) * half[col];
        entries.push_back({index, *transform, glm::max(extent, glm::vec3(0.001f))});
    }
    return entries;
}

bool Apply(EditorScene& scene, const std::vector<Entry>& entries) {
    std::vector<int> indices;
    std::vector<Transform> transforms;
    indices.reserve(entries.size());
    transforms.reserve(entries.size());
    for (const Entry& entry : entries) {
        indices.push_back(entry.index);
        transforms.push_back(entry.transform);
    }
    return scene.SetObjectTransformsUndoable(indices, transforms);
}

float Minimum(const Entry& entry, int axis) {
    return entry.transform.position[axis] - entry.extent[axis];
}
float Maximum(const Entry& entry, int axis) {
    return entry.transform.position[axis] + entry.extent[axis];
}

} // namespace

void AlignmentPanel::Draw(EditorScene& scene, bool* open) {
    if (!ImGui::Begin(EditorPanels::Name(EditorPanels::Panel::Alignment), open)) {
        ImGui::End();
        return;
    }

    std::vector<Entry> entries = EditableSelection(scene);
    static const char* axes[] = {"X", "Y", "Z"};
    ImGui::Combo("Axis", &m_axis, axes, 3);
    ImGui::Text("Selected: %zu editable object(s)", entries.size());
    if (entries.size() < 2) {
        ImGui::TextDisabled("Multi-select two or more unlocked objects in the Hierarchy.");
        ImGui::End();
        return;
    }

    ImGui::SeparatorText("Align Bounds");
    if (ImGui::Button("Minimum", ImVec2(100.0f, 0.0f))) {
        float target = Minimum(entries.front(), m_axis);
        for (const Entry& entry : entries) target = std::min(target, Minimum(entry, m_axis));
        for (Entry& entry : entries)
            entry.transform.position[m_axis] += target - Minimum(entry, m_axis);
        Apply(scene, entries);
    }
    ImGui::SameLine();
    if (ImGui::Button("Centers", ImVec2(100.0f, 0.0f))) {
        float minimum = Minimum(entries.front(), m_axis);
        float maximum = Maximum(entries.front(), m_axis);
        for (const Entry& entry : entries) {
            minimum = std::min(minimum, Minimum(entry, m_axis));
            maximum = std::max(maximum, Maximum(entry, m_axis));
        }
        const float target = (minimum + maximum) * 0.5f;
        for (Entry& entry : entries) entry.transform.position[m_axis] = target;
        Apply(scene, entries);
    }
    ImGui::SameLine();
    if (ImGui::Button("Maximum", ImVec2(100.0f, 0.0f))) {
        float target = Maximum(entries.front(), m_axis);
        for (const Entry& entry : entries) target = std::max(target, Maximum(entry, m_axis));
        for (Entry& entry : entries)
            entry.transform.position[m_axis] += target - Maximum(entry, m_axis);
        Apply(scene, entries);
    }

    ImGui::SeparatorText("Distribute");
    if (ImGui::Button("Even Centers", ImVec2(150.0f, 0.0f))) {
        std::sort(entries.begin(), entries.end(), [&](const Entry& a, const Entry& b) {
            return a.transform.position[m_axis] < b.transform.position[m_axis];
        });
        const float first = entries.front().transform.position[m_axis];
        const float last = entries.back().transform.position[m_axis];
        const float spacing = (last - first) / static_cast<float>(entries.size() - 1);
        for (std::size_t i = 1; i + 1 < entries.size(); ++i)
            entries[i].transform.position[m_axis] = first + spacing * static_cast<float>(i);
        Apply(scene, entries);
    }
    ImGui::SameLine();
    if (ImGui::Button("Even Gaps", ImVec2(150.0f, 0.0f))) {
        std::sort(entries.begin(), entries.end(), [&](const Entry& a, const Entry& b) {
            return Minimum(a, m_axis) < Minimum(b, m_axis);
        });
        const float rangeStart = Minimum(entries.front(), m_axis);
        const float rangeEnd = Maximum(entries.back(), m_axis);
        float occupied = 0.0f;
        for (const Entry& entry : entries) occupied += entry.extent[m_axis] * 2.0f;
        const float gap = (rangeEnd - rangeStart - occupied)
            / static_cast<float>(entries.size() - 1);
        float cursor = rangeStart;
        for (Entry& entry : entries) {
            entry.transform.position[m_axis] = cursor + entry.extent[m_axis];
            cursor += entry.extent[m_axis] * 2.0f + gap;
        }
        Apply(scene, entries);
    }

    ImGui::SeparatorText("Match Primary Selection");
    const int primaryIndex = scene.SelectedIndex();
    const auto primary = std::find_if(entries.begin(), entries.end(),
        [&](const Entry& entry) { return entry.index == primaryIndex; });
    if (primary == entries.end()) ImGui::BeginDisabled();
    if (ImGui::Button("Position", ImVec2(96.0f, 0.0f)) && primary != entries.end()) {
        for (Entry& entry : entries)
            if (entry.index != primaryIndex) entry.transform.position = primary->transform.position;
        Apply(scene, entries);
    }
    ImGui::SameLine();
    if (ImGui::Button("Rotation", ImVec2(96.0f, 0.0f)) && primary != entries.end()) {
        for (Entry& entry : entries)
            if (entry.index != primaryIndex) entry.transform.rotation = primary->transform.rotation;
        Apply(scene, entries);
    }
    ImGui::SameLine();
    if (ImGui::Button("Scale", ImVec2(96.0f, 0.0f)) && primary != entries.end()) {
        for (Entry& entry : entries)
            if (entry.index != primaryIndex) entry.transform.scale = primary->transform.scale;
        Apply(scene, entries);
    }
    if (primary == entries.end()) ImGui::EndDisabled();

    ImGui::SeparatorText("Arrange");
    ImGui::DragFloat("Line Spacing", &m_lineSpacing, 0.05f, -1000.0f, 1000.0f, "%.2f");
    ImGui::Checkbox("Keep primary at its position", &m_preservePrimary);
    if (ImGui::Button("Arrange on Line", ImVec2(-1.0f, 0.0f))) {
        glm::vec3 origin = entries.front().transform.position;
        if (m_preservePrimary && primary != entries.end()) origin = primary->transform.position;
        std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
            return a.index < b.index;
        });
        if (m_preservePrimary && primaryIndex >= 0) {
            const auto selectedPrimary = std::find_if(entries.begin(), entries.end(),
                [&](const Entry& entry) { return entry.index == primaryIndex; });
            if (selectedPrimary != entries.end())
                std::rotate(entries.begin(), selectedPrimary, selectedPrimary + 1);
        }
        for (std::size_t i = 0; i < entries.size(); ++i) {
            entries[i].transform.position = origin;
            entries[i].transform.position[m_axis] += m_lineSpacing * static_cast<float>(i);
        }
        Apply(scene, entries);
    }

    ImGui::SliderInt("Grid Columns", &m_gridColumns, 1, 32);
    ImGui::DragFloat("Grid X Spacing", &m_gridSpacingX, 0.05f, -1000.0f, 1000.0f, "%.2f");
    ImGui::DragFloat("Grid Z Spacing", &m_gridSpacingZ, 0.05f, -1000.0f, 1000.0f, "%.2f");
    if (ImGui::Button("Arrange on XZ Grid", ImVec2(-1.0f, 0.0f))) {
        glm::vec3 origin = entries.front().transform.position;
        if (m_preservePrimary && primary != entries.end()) origin = primary->transform.position;
        std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
            return a.index < b.index;
        });
        if (m_preservePrimary && primaryIndex >= 0) {
            const auto selectedPrimary = std::find_if(entries.begin(), entries.end(),
                [&](const Entry& entry) { return entry.index == primaryIndex; });
            if (selectedPrimary != entries.end())
                std::rotate(entries.begin(), selectedPrimary, selectedPrimary + 1);
        }
        for (std::size_t i = 0; i < entries.size(); ++i) {
            const int column = static_cast<int>(i) % m_gridColumns;
            const int row = static_cast<int>(i) / m_gridColumns;
            entries[i].transform.position = origin
                + glm::vec3(m_gridSpacingX * static_cast<float>(column), 0.0f,
                            m_gridSpacingZ * static_cast<float>(row));
        }
        Apply(scene, entries);
    }
    ImGui::TextDisabled("Locked objects are excluded. Every operation uses one Undo step.");
    ImGui::End();
}
