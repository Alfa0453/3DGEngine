#pragma once

#include <glm/glm.hpp>

#include <array>
#include <string>
#include <vector>

class BlockoutPanel {
public:
    enum class Shape { Wall = 0, Floor, Platform, Ramp, Doorway, Stairs };
    enum class Placement { ViewportCursor = 0, Manual, SelectedObject };
    struct Result {
        bool createRequested = false;
        bool deleteRequested = false;
    };

    Result Draw(const std::string& assetRoot, bool* open);
    void RefreshMaterials(const std::string& assetRoot);

    Shape CurrentShape() const { return static_cast<Shape>(m_shape); }
    Placement PlacementMode() const { return static_cast<Placement>(m_placement); }
    const char* GroupName() const { return m_groupName.data(); }
    glm::vec3 Dimensions() const { return m_dimensions; }
    glm::vec3 ManualPosition() const { return m_position; }
    float Yaw() const { return m_yaw; }
    int StepCount() const { return m_steps; }
    float DoorWidth() const { return m_doorWidth; }
    float DoorHeight() const { return m_doorHeight; }
    bool CreateCollider() const { return m_collider; }
    bool ReplaceExisting() const { return m_replace; }
    const std::string& MaterialPath() const { return m_materialPath; }

private:
    struct MaterialChoice { std::string path; std::string name; };
    std::array<char, 96> m_groupName{{'B','l','o','c','k','o','u','t','_','1','\0'}};
    int m_shape = 0;
    int m_placement = 0;
    glm::vec3 m_dimensions{4.0f, 3.0f, 0.25f};
    glm::vec3 m_position{0.0f};
    float m_yaw = 0.0f;
    int m_steps = 8;
    float m_doorWidth = 1.2f;
    float m_doorHeight = 2.2f;
    bool m_collider = true;
    bool m_replace = true;
    std::vector<MaterialChoice> m_materials;
    std::string m_materialRoot;
    std::string m_materialPath;
};
