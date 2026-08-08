#pragma once

#include <engine/ecs/Components.h>

#include <array>
#include <string>
#include <vector>

class EditorScene;

class ArrayToolPanel {
public:
    enum class Layout { Linear, Grid, Radial, Spline };

    struct Result {
        bool createRequested = false;
        bool deleteGeneratedRequested = false;
    };

    Result Draw(const EditorScene& scene, bool* open);
    std::vector<engine::ecs::Transform> BuildTransforms(
        const engine::ecs::Transform& source, const EditorScene& scene) const;

    const char* GroupName() const { return m_groupName.data(); }
    bool ReplaceExisting() const { return m_replaceExisting; }
    int MaximumCopies() const { return 2000; }

private:
    Layout m_layout = Layout::Linear;
    std::array<char, 96> m_groupName{{'A','r','r','a','y','_','1','\0'}};
    bool m_replaceExisting = true;
    bool m_localSpace = false;
    int m_linearCount = 5;
    glm::vec3 m_linearSpacing{2.0f, 0.0f, 0.0f};
    float m_rotationStep = 0.0f;
    float m_scaleStep = 1.0f;
    glm::ivec3 m_gridCount{4, 1, 4};
    glm::vec3 m_gridSpacing{2.0f, 2.0f, 2.0f};
    int m_radialCount = 8;
    float m_radialRadius = 5.0f;
    float m_radialArc = 360.0f;
    float m_radialStart = 0.0f;
    int m_radialAxis = 1;
    bool m_radialFaceOutward = true;
    std::string m_splineName;
    int m_splineCount = 10;
    float m_splineStart = 0.0f;
    float m_splineEnd = 1.0f;
    glm::vec3 m_splineOffset{0.0f};
    bool m_splineAlign = true;
};
