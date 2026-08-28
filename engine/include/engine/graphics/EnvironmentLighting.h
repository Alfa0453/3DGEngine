#pragma once

#include "engine/graphics/DayNightCycle.h"
#include "engine/graphics/LightingBuildData.h"

#include <glm/glm.hpp>

#include <cstdint>

namespace engine {

enum class EnvironmentQuality : std::uint8_t { Low = 0, Medium, High, Ultra };

// Artist-authored atmosphere controls. Distances are kilometres; the renderer
// maps them to its unitless scattering model so ordinary scene coordinates stay
// numerically well behaved.
struct AtmosphereParameters {
    float rayleighDensity = 1.0f;
    float rayleighScaleHeightKm = 8.0f;
    float mieDensity = 1.0f;
    float mieScaleHeightKm = 1.2f;
    float mieAnisotropy = 0.76f;
    float ozone = 1.0f;
    float planetRadiusKm = 6360.0f;
    float atmosphereHeightKm = 100.0f;
    float intensity = 1.0f;
    float sunAngularDiameterDegrees = 0.53f;
    float sunDiskIntensity = 10.0f;

    void Normalize();
};

struct EnvironmentCloudParameters {
    bool enabled = true;
    float coverage = 0.45f;
    float density = 0.75f;
    float opticalDepth = 1.35f;
    float forwardScattering = 0.72f;
    float silverLining = 0.35f;
    glm::vec3 albedo{1.0f, 0.98f, 0.94f};
};

struct NightEnvironment {
    bool stars = true;
    float starIntensity = 0.65f;
    bool moon = true;
    glm::vec3 moonDirection{0.0f, 1.0f, 0.0f};
    glm::vec3 moonRadiance{0.035f, 0.045f, 0.070f};
    float moonAngularDiameterDegrees = 0.52f;
    float moonPhase = 1.0f;
};

// Resolved, authoritative frame state shared by the sky, direct light, baked
// and dynamic GI, reflection captures, clouds and volumetrics.
struct EnvironmentLightingState {
    float timeOfDay = 0.5f;
    float dayFactor = 1.0f;
    glm::vec3 sunDirection{0.0f, 1.0f, 0.0f}; // points toward the sun
    glm::vec3 sunRadiance{1.0f};
    glm::vec3 ambientRadiance{0.03f};
    AtmosphereParameters atmosphere;
    EnvironmentCloudParameters clouds;
    NightEnvironment night;
    EnvironmentQuality quality = EnvironmentQuality::High;
    std::uint64_t atmosphereRevision = 1;
    std::uint64_t skyRevision = 1;

    glm::vec3 SampleEnvironmentRadiance(const glm::vec3& direction) const;
    DirectionalSkyRadiance ToDirectionalSkyRadiance(float intensity = 1.0f) const;
};

EnvironmentLightingState ResolveEnvironmentLighting(
    float timeOfDay, const DayNightCycle::Sample& dayNight,
    const AtmosphereParameters& atmosphere = {},
    const EnvironmentCloudParameters& clouds = {},
    const NightEnvironment& night = {},
    EnvironmentQuality quality = EnvironmentQuality::High);

} // namespace engine
