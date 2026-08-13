#pragma once

#include <glm/glm.hpp>

#include <string>
#include <vector>

class DecalPlacementPanel {
public:
    struct Settings {
        std::string materialPath;
        glm::vec2 size{1.5f, 1.5f};
        float rotationDegrees = 0.0f;
        float surfaceOffset = 0.012f;
        float opacity = 1.0f;
    };

    void Draw(bool* open, const std::vector<std::string>& materials,
              const std::vector<std::string>& textures,
              const std::string& selectedAssetPath);
    bool PlacementActive() const { return m_placementActive; }
    const Settings& Current() const { return m_settings; }
    void SetHover(const glm::vec3& position, const glm::vec3& normal, bool valid);
    bool HoverValid() const { return m_hoverValid; }
    const glm::vec3& HoverPosition() const { return m_hoverPosition; }
    const glm::vec3& HoverNormal() const { return m_hoverNormal; }

private:
    Settings m_settings;
    bool m_placementActive = false;
    bool m_hoverValid = false;
    glm::vec3 m_hoverPosition{0.0f};
    glm::vec3 m_hoverNormal{0.0f, 1.0f, 0.0f};
};
