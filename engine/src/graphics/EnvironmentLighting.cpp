#include "engine/graphics/EnvironmentLighting.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace engine {
namespace {
constexpr float kPi = 3.14159265358979323846f;

float SafePhase(float cosineTheta, float g) {
    g = std::clamp(g, -0.94f, 0.94f);
    const float gg = g * g;
    return (1.0f - gg) /
        std::max(4.0f * kPi * std::pow(1.0f + gg - 2.0f * g * cosineTheta, 1.5f), 1e-4f);
}

glm::vec3 PhysicalSky(const EnvironmentLightingState& s, glm::vec3 d) {
    d = glm::normalize(d);
    const float up = std::clamp(d.y, -0.08f, 1.0f);
    const float mu = std::clamp(glm::dot(d, s.sunDirection), -1.0f, 1.0f);
    const float airMass = 1.0f / std::max(0.08f, up + 0.12f);
    const glm::vec3 betaR(5.8e-3f, 13.5e-3f, 33.1e-3f);
    const glm::vec3 betaM(4.0e-3f);
    const glm::vec3 ozoneAbsorb(0.65e-3f, 1.15e-3f, 0.08e-3f);
    const glm::vec3 extinction =
        betaR * s.atmosphere.rayleighDensity
        + betaM * s.atmosphere.mieDensity
        + ozoneAbsorb * s.atmosphere.ozone;
    const glm::vec3 transmittance = glm::exp(-extinction * airMass * 22.0f);
    const float rayleighPhase = 3.0f * (1.0f + mu * mu) / (16.0f * kPi);
    const float miePhase = SafePhase(mu, s.atmosphere.mieAnisotropy);
    glm::vec3 scatter = (betaR * rayleighPhase * s.atmosphere.rayleighDensity
                       + betaM * miePhase * s.atmosphere.mieDensity)
                      * (glm::vec3(1.0f) - transmittance) * 38.0f;
    scatter *= s.sunRadiance * s.atmosphere.intensity;
    const float below = std::clamp(-d.y * 5.0f, 0.0f, 1.0f);
    scatter = glm::mix(scatter, s.ambientRadiance * 0.12f, below);

    const float angularRadius = glm::radians(s.atmosphere.sunAngularDiameterDegrees * 0.5f);
    const float disk = glm::smoothstep(std::cos(angularRadius * 1.12f),
                                      std::cos(angularRadius * 0.88f), mu);
    scatter += s.sunRadiance * disk * s.atmosphere.sunDiskIntensity;

    const float night = s.nightFactor;
    // Sky visibility and lighting energy are intentionally separate. Exposure
    // may reveal this signal without turning it into a daylight-strength fill.
    scatter += glm::vec3(0.055f, 0.080f, 0.18f) * night;
    if (s.night.moon) {
        const float moonMu = glm::dot(d, glm::normalize(s.night.moonDirection));
        const float moonRadius = glm::radians(s.night.moonAngularDiameterDegrees * 0.5f);
        const float moonDisk = glm::smoothstep(std::cos(moonRadius * 1.15f),
                                              std::cos(moonRadius * 0.85f), moonMu);
        scatter += s.night.moonRadiance * moonDisk * s.night.moonPhase * night;
    }
    return glm::max(scatter, glm::vec3(0.0f)) * s.environmentIntensity;
}
}

void EnvironmentEnergyParameters::Normalize() {
    dayIntensity = std::clamp(dayIntensity, 0.0f, 20.0f);
    twilightIntensity = std::clamp(twilightIntensity, 0.0f, 20.0f);
    nightIntensity = std::clamp(nightIntensity, 0.0f, 20.0f);
    nightReflectionIntensity = std::clamp(nightReflectionIntensity, 0.0f, 4.0f);
    nightFogScattering = std::clamp(nightFogScattering, 0.0f, 4.0f);
    nightCloudAmbient = std::clamp(nightCloudAmbient, 0.0f, 4.0f);
}

void AtmosphereParameters::Normalize() {
    rayleighDensity = std::clamp(rayleighDensity, 0.0f, 8.0f);
    rayleighScaleHeightKm = std::clamp(rayleighScaleHeightKm, 1.0f, 32.0f);
    mieDensity = std::clamp(mieDensity, 0.0f, 8.0f);
    mieScaleHeightKm = std::clamp(mieScaleHeightKm, 0.1f, 8.0f);
    mieAnisotropy = std::clamp(mieAnisotropy, -0.94f, 0.94f);
    ozone = std::clamp(ozone, 0.0f, 4.0f);
    planetRadiusKm = std::clamp(planetRadiusKm, 100.0f, 100000.0f);
    atmosphereHeightKm = std::clamp(atmosphereHeightKm, 1.0f, 1000.0f);
    intensity = std::clamp(intensity, 0.0f, 20.0f);
    sunAngularDiameterDegrees = std::clamp(sunAngularDiameterDegrees, 0.05f, 30.0f);
    sunDiskIntensity = std::clamp(sunDiskIntensity, 0.0f, 100.0f);
}

glm::vec3 EnvironmentLightingState::SampleEnvironmentRadiance(
    const glm::vec3& direction) const {
    return PhysicalSky(*this, direction);
}

DirectionalSkyRadiance EnvironmentLightingState::ToDirectionalSkyRadiance(
    float intensity) const {
    DirectionalSkyRadiance result;
    result.zenith = SampleEnvironmentRadiance(glm::vec3(0.0f, 1.0f, 0.0f)) * intensity;
    result.horizon = SampleEnvironmentRadiance(glm::normalize(glm::vec3(1.0f, 0.03f, 0.0f))) * intensity;
    result.ground = SampleEnvironmentRadiance(glm::vec3(0.0f, -1.0f, 0.0f)) * intensity;
    result.sunDirection = -keyLightDirection;
    result.sunRadiance = keyLightRadiance * intensity * 0.35f;
    return result;
}

EnvironmentLightingState ResolveEnvironmentLighting(
    float timeOfDay, const DayNightCycle::Sample& dayNight,
    const AtmosphereParameters& authoredAtmosphere,
    const EnvironmentCloudParameters& clouds,
    const NightEnvironment& authoredNight,
    const EnvironmentEnergyParameters& authoredEnergy,
    EnvironmentQuality quality) {
    EnvironmentLightingState state;
    state.timeOfDay = timeOfDay - std::floor(timeOfDay);
    state.dayFactor = std::clamp(dayNight.dayFactor, 0.0f, 1.0f);
    state.twilightFactor = std::clamp(dayNight.twilightFactor, 0.0f, 1.0f);
    state.nightFactor = std::clamp(dayNight.nightFactor, 0.0f, 1.0f);
    state.solarElevation = std::clamp(dayNight.solarElevation, -1.0f, 1.0f);
    state.sunDirection = glm::normalize(dayNight.sunToward);
    state.sunRadiance = glm::max(dayNight.sunRadiance, glm::vec3(0.0f));
    state.keyLightDirection = dayNight.keyLightDirection;
    const bool moonDominant = glm::dot(dayNight.moonRadiance, dayNight.moonRadiance)
        > glm::dot(dayNight.sunRadiance, dayNight.sunRadiance);
    state.keyLightRadiance = moonDominant
        ? glm::max(dayNight.moonRadiance, glm::vec3(0.0f))
            * std::clamp(authoredNight.moonGiContribution, 0.0f, 1.0f)
        : state.sunRadiance;
    state.ambientRadiance = glm::max(dayNight.ambient, glm::vec3(0.0f));
    state.atmosphere = authoredAtmosphere;
    state.atmosphere.Normalize();
    state.clouds = clouds;
    state.clouds.coverage = std::clamp(state.clouds.coverage, 0.0f, 1.0f);
    state.clouds.density = std::clamp(state.clouds.density, 0.0f, 4.0f);
    state.clouds.opticalDepth = std::clamp(state.clouds.opticalDepth, 0.01f, 8.0f);
    state.clouds.forwardScattering = std::clamp(state.clouds.forwardScattering, 0.0f, 0.95f);
    state.clouds.silverLining = std::clamp(state.clouds.silverLining, 0.0f, 2.0f);
    state.night = authoredNight;
    state.night.moonDirection = glm::normalize(dayNight.moonToward);
    state.night.starIntensity = std::clamp(state.night.starIntensity, 0.0f, 10.0f);
    state.night.moonPhase = std::clamp(state.night.moonPhase, 0.0f, 1.0f);
    state.night.moonGiContribution = std::clamp(
        state.night.moonGiContribution, 0.0f, 1.0f);
    state.energy = authoredEnergy;
    state.energy.Normalize();
    const float factorSum = std::max(
        state.dayFactor + state.twilightFactor + state.nightFactor, 1e-5f);
    state.environmentIntensity = (state.dayFactor * state.energy.dayIntensity
        + state.twilightFactor * state.energy.twilightIntensity
        + state.nightFactor * state.energy.nightIntensity) / factorSum;
    state.ambientRadiance *= state.environmentIntensity;
    state.quality = quality;
    return state;
}

float ResolveNightExposureMaxEv(float dayMaxEv, float nightLimitEv,
                                float nightFactor, bool preserveNightDarkness) {
    if (!preserveNightDarkness) return dayMaxEv;
    return glm::mix(dayMaxEv, std::min(dayMaxEv, nightLimitEv),
                    std::clamp(nightFactor, 0.0f, 1.0f));
}

} // namespace engine
