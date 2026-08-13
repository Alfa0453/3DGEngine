#include "LightingAnalysis.h"

#include <cmath>
#include <iostream>

namespace {
bool Require(bool value, const char* message) {
    if (!value) std::cerr << "FAILED: " << message << '\n';
    return value;
}
}

int main() {
    using namespace editor::lighting;
    Settings settings;
    settings.boundsMin = {-4.0f, -4.0f};
    settings.boundsMax = {4.0f, 4.0f};
    settings.gridResolution = 8;
    settings.unlitThreshold = 0.1f;
    settings.overlapWarning = 2;
    settings.overlapCritical = 3;
    settings.shadowWarning = 2;

    Report dark = Analyze({}, settings, 0.0f);
    if (!Require(dark.cells.size() == 64, "grid size is not deterministic")) return 1;
    if (!Require(dark.unlitCellCount == 64, "empty level should be unlit")) return 1;

    LightInput sun;
    sun.name = "Sun";
    sun.type = LightType::Directional;
    sun.intensity = 1.0f;
    Report day = Analyze({sun}, settings, 0.05f);
    if (!Require(day.unlitCellCount == 0, "directional light did not cover the grid")) return 1;
    if (!Require(std::abs(day.minimumIlluminance - day.maximumIlluminance) < 0.0001f,
                 "directional contribution should be uniform")) return 1;

    LightInput point;
    point.name = "Point";
    point.type = LightType::Point;
    point.position = {0.0f, 0.1f, 0.0f};
    point.range = 3.0f;
    point.intensity = 4.0f;
    Report local = Analyze({point}, settings, 0.0f);
    if (!Require(local.maximumIlluminance > local.minimumIlluminance,
                 "local light falloff was not sampled")) return 1;

    LightInput a = sun, b = sun, c = sun;
    a.name = "A"; b.name = "B"; c.name = "C";
    Report overlap = Analyze({a, b, c}, settings, 0.0f);
    if (!Require(overlap.maximumOverlap == 3, "light overlap count is wrong")) return 1;
    if (!Require(overlap.maximumShadowOverlap == 3, "shadow overlap count is wrong")) return 1;
    bool foundCritical = false;
    for (const Finding& finding : overlap.findings)
        foundCritical |= finding.severity == Severity::Critical
            && finding.category == "Light complexity";
    if (!Require(foundCritical, "critical overlap finding is missing")) return 1;

    Settings invalid = settings;
    invalid.gridResolution = 1;
    invalid.boundsMax = invalid.boundsMin;
    Report clamped = Analyze({}, invalid, 0.0f);
    if (!Require(clamped.cells.size() == 16, "unsafe settings were not clamped")) return 1;
    return 0;
}
