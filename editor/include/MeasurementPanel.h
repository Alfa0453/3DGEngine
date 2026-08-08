#pragma once

#include <glm/glm.hpp>

#include <array>
#include <string>
#include <vector>

class MeasurementPanel {
public:
    enum class Type { Distance, Box };

    struct Measurement {
        std::array<char, 64> name{};
        Type type = Type::Distance;
        glm::vec3 a{0.0f};
        glm::vec3 b{0.0f};
        bool visible = true;
    };

    void Draw(bool* open);
    bool Capturing() const { return m_capturing; }
    bool HasFirstPoint() const { return m_hasFirst; }
    void SetHoverPoint(const glm::vec3& point);
    void CapturePoint(const glm::vec3& point);
    void CancelCapture();

    const std::vector<Measurement>& Measurements() const { return m_measurements; }
    glm::vec3 FirstPoint() const { return m_first; }
    glm::vec3 PreviewPoint() const { return m_hover; }
    Type CaptureType() const { return m_type; }
    int SelectedIndex() const { return m_selected; }

private:
    glm::vec3 Snap(const glm::vec3& point) const;
    std::string BuildReport(const Measurement& measurement) const;

    std::vector<Measurement> m_measurements;
    Type m_type = Type::Distance;
    bool m_capturing = false;
    bool m_hasFirst = false;
    bool m_snap = false;
    bool m_centimeters = false;
    float m_gridSize = 0.5f;
    glm::vec3 m_first{0.0f};
    glm::vec3 m_hover{0.0f};
    int m_selected = -1;
    int m_nextId = 1;
};
