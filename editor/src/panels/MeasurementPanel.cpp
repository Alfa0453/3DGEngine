#include "MeasurementPanel.h"
#include "EditorPanels.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

glm::vec3 MeasurementPanel::Snap(const glm::vec3& point) const {
    if (!m_snap) return point;
    const float step = std::max(m_gridSize, 0.001f);
    return glm::round(point / step) * step;
}

void MeasurementPanel::SetHoverPoint(const glm::vec3& point) {
    m_hover = Snap(point);
}

void MeasurementPanel::CapturePoint(const glm::vec3& point) {
    const glm::vec3 snapped = Snap(point);
    if (!m_capturing) return;
    if (!m_hasFirst) {
        m_first = snapped;
        m_hover = snapped;
        m_hasFirst = true;
        return;
    }
    Measurement measurement;
    measurement.type = m_type;
    measurement.a = m_first;
    measurement.b = snapped;
    std::snprintf(measurement.name.data(), measurement.name.size(),
                  "Measure_%d", m_nextId++);
    m_measurements.push_back(measurement);
    m_selected = static_cast<int>(m_measurements.size()) - 1;
    m_capturing = false;
    m_hasFirst = false;
}

void MeasurementPanel::CancelCapture() {
    m_capturing = false;
    m_hasFirst = false;
}

std::string MeasurementPanel::BuildReport(const Measurement& measurement) const {
    const glm::vec3 delta = measurement.b - measurement.a;
    const float distance = glm::length(delta);
    const float horizontal = glm::length(glm::vec2(delta.x, delta.z));
    const float slope = glm::degrees(std::atan2(std::abs(delta.y), std::max(horizontal, 1.0e-6f)));
    const float multiplier = m_centimeters ? 100.0f : 1.0f;
    const char* unit = m_centimeters ? "cm" : "m";
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(2);
    out << measurement.name.data() << ": distance " << distance * multiplier << ' ' << unit
        << ", delta (" << delta.x * multiplier << ", " << delta.y * multiplier
        << ", " << delta.z * multiplier << ") " << unit
        << ", horizontal " << horizontal * multiplier << ' ' << unit
        << ", slope " << slope << " deg";
    if (measurement.type == Type::Box) {
        const glm::vec3 dimensions = glm::abs(delta) * multiplier;
        out << ", box " << dimensions.x << " x " << dimensions.y << " x "
            << dimensions.z << ' ' << unit
            << ", volume " << std::abs(delta.x * delta.y * delta.z)
               * multiplier * multiplier * multiplier << ' ' << unit << "3";
    }
    return out.str();
}

void MeasurementPanel::Draw(bool* open) {
    if (!ImGui::Begin(EditorPanels::Name(EditorPanels::Panel::Measurement), open)) { ImGui::End(); return; }

    ImGui::SeparatorText("New Measurement");
    if (ImGui::RadioButton("Distance", m_type == Type::Distance)) m_type = Type::Distance;
    ImGui::SameLine();
    if (ImGui::RadioButton("Box Dimensions", m_type == Type::Box)) m_type = Type::Box;
    ImGui::Checkbox("Snap to Grid", &m_snap);
    if (m_snap) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        ImGui::DragFloat("Grid", &m_gridSize, 0.05f, 0.001f, 1000.0f, "%.2f m");
        m_gridSize = std::clamp(m_gridSize, 0.001f, 1000.0f);
    }
    ImGui::Checkbox("Display Centimeters", &m_centimeters);
    if (!m_capturing) {
        if (ImGui::Button("Measure in Viewport", ImVec2(-1, 0))) {
            m_capturing = true;
            m_hasFirst = false;
        }
    } else {
        if (ImGui::Button("Cancel Measurement", ImVec2(-1, 0))) CancelCapture();
        ImGui::TextColored(ImVec4(0.25f, 0.9f, 1.0f, 1.0f),
            m_hasFirst ? "Click the second point" : "Click the first point");
    }

    ImGui::SeparatorText("Saved Measurements");
    const float listHeight = std::clamp(ImGui::GetContentRegionAvail().y * 0.35f,
                                        100.0f, 260.0f);
    if (ImGui::BeginChild("##MeasurementList", ImVec2(0, listHeight), true)) {
        for (int i = 0; i < static_cast<int>(m_measurements.size()); ++i) {
            Measurement& measurement = m_measurements[static_cast<std::size_t>(i)];
            ImGui::PushID(i);
            ImGui::Checkbox("##Visible", &measurement.visible);
            ImGui::SameLine();
            const char* type = measurement.type == Type::Distance ? "[Line]" : "[Box]";
            const std::string label = std::string(type) + " " + measurement.name.data();
            if (ImGui::Selectable(label.c_str(), i == m_selected)) m_selected = i;
            ImGui::PopID();
        }
        if (m_measurements.empty()) ImGui::TextDisabled("No measurements created yet.");
    }
    ImGui::EndChild();

    if (m_selected >= 0 && m_selected < static_cast<int>(m_measurements.size())) {
        Measurement& measurement = m_measurements[static_cast<std::size_t>(m_selected)];
        ImGui::SeparatorText("Selected Measurement");
        ImGui::InputText("Name", measurement.name.data(), measurement.name.size());
        ImGui::DragFloat3("Point A", &measurement.a.x, 0.01f, -100000.0f, 100000.0f, "%.3f");
        ImGui::DragFloat3("Point B", &measurement.b.x, 0.01f, -100000.0f, 100000.0f, "%.3f");
        const glm::vec3 delta = measurement.b - measurement.a;
        const float distance = glm::length(delta);
        const float horizontal = glm::length(glm::vec2(delta.x, delta.z));
        const float slope = glm::degrees(std::atan2(std::abs(delta.y), std::max(horizontal, 1.0e-6f)));
        const float multiplier = m_centimeters ? 100.0f : 1.0f;
        const char* unit = m_centimeters ? "cm" : "m";
        ImGui::Text("Distance: %.3f %s", distance * multiplier, unit);
        ImGui::Text("Delta: X %.3f  Y %.3f  Z %.3f %s",
                    delta.x * multiplier, delta.y * multiplier,
                    delta.z * multiplier, unit);
        ImGui::Text("Horizontal: %.3f %s  |  Slope: %.2f deg",
                    horizontal * multiplier, unit, slope);
        if (measurement.type == Type::Box) {
            const glm::vec3 dimensions = glm::abs(delta) * multiplier;
            ImGui::Text("Box: %.3f x %.3f x %.3f %s",
                        dimensions.x, dimensions.y, dimensions.z, unit);
            ImGui::Text("Volume: %.3f %s3",
                std::abs(delta.x * delta.y * delta.z)
                    * multiplier * multiplier * multiplier, unit);
        }
        if (ImGui::Button("Copy Report"))
            ImGui::SetClipboardText(BuildReport(measurement).c_str());
        ImGui::SameLine();
        if (ImGui::Button("Delete")) {
            m_measurements.erase(m_measurements.begin() + m_selected);
            m_selected = std::min(m_selected, static_cast<int>(m_measurements.size()) - 1);
        }
    }
    if (!m_measurements.empty() && ImGui::Button("Clear All", ImVec2(-1, 0))) {
        m_measurements.clear();
        m_selected = -1;
    }

    ImGui::End();
}
