#include "LightingAnalysis.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace editor::lighting {
namespace {

float Luminance(const glm::vec3& color) {
    return glm::dot(glm::max(color, glm::vec3(0.0f)),
                    glm::vec3(0.2126f, 0.7152f, 0.0722f));
}

float Contribution(const LightInput& light, const glm::vec3& point) {
    const float power = std::max(0.0f, light.intensity) * Luminance(light.color);
    if (power <= 0.00001f) return 0.0f;
    if (light.type == LightType::Directional) return power;

    const glm::vec3 delta = point - light.position;
    const float distance = glm::length(delta);
    const float range = std::max(0.01f, light.range);
    if (distance >= range) return 0.0f;
    if (light.type == LightType::Spot) {
        const glm::vec3 direction = glm::dot(light.direction, light.direction) > 0.00001f
            ? glm::normalize(light.direction) : glm::vec3(0.0f, -1.0f, 0.0f);
        const glm::vec3 toPoint = distance > 0.00001f
            ? delta / distance : direction;
        const float cutoff = std::cos(glm::radians(
            std::clamp(light.outerAngleDegrees, 1.0f, 89.0f)));
        if (glm::dot(direction, toPoint) < cutoff) return 0.0f;
    }
    const float normalized = distance / range;
    const float window = std::max(0.0f, 1.0f - normalized * normalized);
    return power * window * window / (1.0f + distance * distance * 0.12f);
}

void AddFinding(Report& report, Severity severity, const LightInput& light,
                std::string category, std::string message,
                std::string recommendation) {
    report.findings.push_back({severity, light.objectIndex, light.name,
        std::move(category), std::move(message), std::move(recommendation)});
}

} // namespace

Report Analyze(const std::vector<LightInput>& lights, const Settings& inputSettings,
               float ambientIlluminance) {
    Settings settings = inputSettings;
    settings.gridResolution = std::clamp(settings.gridResolution, 4, 128);
    settings.boundsMax = glm::max(settings.boundsMax, settings.boundsMin + glm::vec2(0.1f));
    settings.unlitThreshold = std::max(0.0f, settings.unlitThreshold);
    settings.overexposedThreshold = std::max(settings.unlitThreshold,
                                              settings.overexposedThreshold);
    settings.overlapWarning = std::max(1, settings.overlapWarning);
    settings.overlapCritical = std::max(settings.overlapWarning,
                                         settings.overlapCritical);
    settings.shadowWarning = std::max(1, settings.shadowWarning);

    Report report;
    report.lightCount = static_cast<int>(lights.size());
    report.shadowLightCount = static_cast<int>(std::count_if(
        lights.begin(), lights.end(), [](const LightInput& light) {
            return light.castsShadow && light.intensity > 0.0f;
        }));
    const int resolution = settings.gridResolution;
    report.cells.reserve(static_cast<std::size_t>(resolution * resolution));
    float totalIlluminance = 0.0f;
    report.minimumIlluminance = std::numeric_limits<float>::max();

    for (int z = 0; z < resolution; ++z) {
        for (int x = 0; x < resolution; ++x) {
            const glm::vec2 uv((static_cast<float>(x) + 0.5f) / resolution,
                               (static_cast<float>(z) + 0.5f) / resolution);
            Cell cell;
            const glm::vec2 xz = glm::mix(settings.boundsMin, settings.boundsMax, uv);
            cell.position = glm::vec3(xz.x, settings.sampleHeight, xz.y);
            cell.illuminance = std::max(0.0f, ambientIlluminance);
            for (const LightInput& light : lights) {
                const float contribution = Contribution(light, cell.position);
                if (contribution <= 0.001f) continue;
                cell.illuminance += contribution;
                ++cell.lightOverlap;
                if (light.castsShadow) ++cell.shadowOverlap;
            }
            report.maximumOverlap = std::max(report.maximumOverlap, cell.lightOverlap);
            report.maximumShadowOverlap = std::max(report.maximumShadowOverlap,
                                                    cell.shadowOverlap);
            report.minimumIlluminance = std::min(report.minimumIlluminance,
                                                  cell.illuminance);
            report.maximumIlluminance = std::max(report.maximumIlluminance,
                                                  cell.illuminance);
            totalIlluminance += cell.illuminance;
            if (cell.illuminance < settings.unlitThreshold) ++report.unlitCellCount;
            if (cell.illuminance > settings.overexposedThreshold)
                ++report.overexposedCellCount;
            report.cells.push_back(cell);
        }
    }
    if (report.cells.empty()) report.minimumIlluminance = 0.0f;
    else report.averageIlluminance = totalIlluminance / report.cells.size();

    for (const LightInput& light : lights) {
        if (light.intensity <= 0.0f)
            AddFinding(report, Severity::Warning, light, "Inactive light",
                "The light has no intensity.", "Disable or remove it, or restore a useful intensity.");
        if (light.type != LightType::Directional && light.range > 100.0f)
            AddFinding(report, Severity::Warning, light, "Oversized influence",
                "The local light range exceeds 100 world units.",
                "Reduce its range or split the lighting into focused fixtures.");
        if (light.castsShadow && light.type != LightType::Directional
            && light.range > 60.0f)
            AddFinding(report, Severity::Warning, light, "Shadow pressure",
                "A large local light is rendering shadows across a wide area.",
                "Reduce the range, disable shadows, or use a cheaper fill light.");
    }

    const int cells = static_cast<int>(report.cells.size());
    if (cells > 0 && report.unlitCellCount * 5 > cells)
        report.findings.push_back({Severity::Warning, -1, "Level", "Unlit areas",
            "More than 20% of analyzed samples are below the unlit threshold.",
            "Add intentional fill, reflection/sky lighting, or confirm these areas should be dark."});
    if (cells > 0 && report.overexposedCellCount * 10 > cells)
        report.findings.push_back({Severity::Warning, -1, "Level", "Exposure",
            "More than 10% of analyzed samples exceed the exposure threshold.",
            "Reduce overlapping intensities or rebalance exposure and emissive materials."});
    if (report.maximumOverlap >= settings.overlapCritical)
        report.findings.push_back({Severity::Critical, -1, "Level", "Light complexity",
            "Local light overlap reaches " + std::to_string(report.maximumOverlap) + ".",
            "Tighten influence ranges and remove redundant fill lights."});
    else if (report.maximumOverlap >= settings.overlapWarning)
        report.findings.push_back({Severity::Warning, -1, "Level", "Light complexity",
            "Local light overlap reaches " + std::to_string(report.maximumOverlap) + ".",
            "Inspect the highlighted overlap cells and reduce redundant coverage."});
    if (report.maximumShadowOverlap >= settings.shadowWarning)
        report.findings.push_back({Severity::Warning, -1, "Level", "Shadow overlap",
            "Up to " + std::to_string(report.maximumShadowOverlap)
                + " shadow-casting lights affect the same sample.",
            "Keep one important shadow light and convert supporting lights to non-shadowing fills."});
    return report;
}

} // namespace editor::lighting
