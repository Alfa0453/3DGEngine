#pragma once

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace editor::lighting {

enum class LightType { Directional = 0, Point = 1, Spot = 2, Area = 3 };
enum class Severity { Info = 0, Warning = 1, Critical = 2 };

struct LightInput {
    std::string name;
    int objectIndex = -1;
    LightType type = LightType::Point;
    glm::vec3 position{0.0f};
    glm::vec3 direction{0.0f, -1.0f, 0.0f};
    glm::vec3 color{1.0f};
    float intensity = 1.0f;
    float range = 10.0f;
    float outerAngleDegrees = 30.0f;
    bool castsShadow = true;
};

struct Settings {
    glm::vec2 boundsMin{-20.0f};
    glm::vec2 boundsMax{20.0f};
    float sampleHeight = 0.1f;
    int gridResolution = 24;
    float unlitThreshold = 0.12f;
    float overexposedThreshold = 4.0f;
    int overlapWarning = 4;
    int overlapCritical = 7;
    int shadowWarning = 3;
};

struct Cell {
    glm::vec3 position{0.0f};
    float illuminance = 0.0f;
    int lightOverlap = 0;
    int shadowOverlap = 0;
};

struct Finding {
    Severity severity = Severity::Info;
    int objectIndex = -1;
    std::string objectName;
    std::string category;
    std::string message;
    std::string recommendation;
};

struct Report {
    std::vector<Cell> cells;
    std::vector<Finding> findings;
    int lightCount = 0;
    int shadowLightCount = 0;
    int unlitCellCount = 0;
    int overexposedCellCount = 0;
    int maximumOverlap = 0;
    int maximumShadowOverlap = 0;
    float minimumIlluminance = 0.0f;
    float averageIlluminance = 0.0f;
    float maximumIlluminance = 0.0f;
};

Report Analyze(const std::vector<LightInput>& lights, const Settings& settings,
               float ambientIlluminance);

} // namespace editor::lighting
