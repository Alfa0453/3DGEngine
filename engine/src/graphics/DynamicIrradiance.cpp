#include "engine/graphics/DynamicIrradiance.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace engine {
namespace {

constexpr float kPi = 3.14159265358979323846f;

std::array<float, 4> Sh4Basis(const glm::vec3& direction) {
    return {0.2820947918f, 0.4886025119f * direction.y,
            0.4886025119f * direction.z, 0.4886025119f * direction.x};
}

glm::vec3 SphereDirection(std::uint32_t sample, std::uint32_t count,
                          std::uint32_t frame) {
    constexpr float golden = 2.39996322972865332f;
    const std::uint32_t rotated = sample + frame * 17u;
    const float u = (static_cast<float>(sample) + 0.5f) / static_cast<float>(count);
    const float y = 1.0f - 2.0f * u;
    const float radius = std::sqrt(std::max(0.0f, 1.0f - y * y));
    const float angle = golden * static_cast<float>(rotated);
    return glm::normalize(glm::vec3(std::cos(angle) * radius, y,
                                    std::sin(angle) * radius));
}

float Luminance(const glm::vec3& value) {
    return glm::dot(value, glm::vec3(0.2126f, 0.7152f, 0.0722f));
}

float FiniteAttenuation(float distanceSquared, float range) {
    const float safeRange = std::max(range, 0.01f);
    const float normalized = std::sqrt(std::max(distanceSquared, 0.0f)) / safeRange;
    const float window = std::clamp(1.0f - std::pow(normalized, 4.0f), 0.0f, 1.0f);
    return window * window / std::max(distanceSquared, 0.01f);
}

} // namespace

void DynamicIrradianceSettings::Normalize() {
    probeSpacing = std::clamp(probeSpacing, 0.5f, 50.0f);
    boundsMax = glm::max(boundsMax, boundsMin + glm::vec3(probeSpacing));
    raysPerProbe = std::clamp(raysPerProbe, 8u, 512u);
    probesPerFrame = std::clamp(probesPerFrame, 1u, 1024u);
    maxGiRaysPerFrame = std::clamp(maxGiRaysPerFrame, 8u, 262144u);
    maxRayDistance = std::clamp(maxRayDistance, probeSpacing, 2000.0f);
    hysteresis = std::clamp(hysteresis, 0.0f, 0.995f);
    intensity = std::clamp(intensity, 0.0f, 2.0f);
    activeDistance = std::clamp(activeDistance, probeSpacing * 2.0f, 10000.0f);
}

void DynamicIrradianceSettings::ApplyQualityPreset(DynamicGiQuality preset) {
    quality = preset;
    switch (preset) {
    case DynamicGiQuality::Low:
        enabled = false; raysPerProbe = 8; probesPerFrame = 1; maxGiRaysPerFrame = 8; break;
    case DynamicGiQuality::Medium:
        raysPerProbe = 24; probesPerFrame = 6; maxGiRaysPerFrame = 192; break;
    case DynamicGiQuality::High:
        raysPerProbe = 48; probesPerFrame = 12; maxGiRaysPerFrame = 768; break;
    case DynamicGiQuality::Ultra:
        raysPerProbe = 96; probesPerFrame = 24; maxGiRaysPerFrame = 3072; break;
    }
    Normalize();
}

bool DynamicIrradianceSystem::Configure(const DynamicIrradianceSettings& input,
                                        const DirectionalSkyRadiance& initialEnvironment,
                                        const LightingBuildData* bakedBaseline,
                                        std::string* error) {
    Reset();
    m_settings = input;
    m_settings.Normalize();
    if (bakedBaseline && bakedBaseline->IsValid()) {
        m_baked = *bakedBaseline;
        m_composed = *bakedBaseline;
        m_settings.boundsMin = m_baked.boundsMin;
        m_settings.boundsMax = m_baked.boundsMax;
        m_settings.probeSpacing = m_baked.spacing;
        m_hasBakedBaseline = true;
    } else {
        m_composed.version = LightingBuildData::kVersion;
        m_composed.boundsMin = m_settings.boundsMin;
        glm::ivec3 dimensions = glm::max(glm::ivec3(2),
            glm::ivec3(glm::ceil((m_settings.boundsMax - m_settings.boundsMin)
                                 / m_settings.probeSpacing)) + 1);
        constexpr std::size_t kMaximumDynamicProbes = 65536;
        while (static_cast<std::size_t>(dimensions.x) * dimensions.y * dimensions.z
               > kMaximumDynamicProbes) {
            m_settings.probeSpacing *= 1.2f;
            dimensions = glm::max(glm::ivec3(2),
                glm::ivec3(glm::ceil((m_settings.boundsMax - m_settings.boundsMin)
                                     / m_settings.probeSpacing)) + 1);
        }
        m_composed.dimensions = dimensions;
        m_composed.spacing = m_settings.probeSpacing;
        m_composed.boundsMax = m_composed.boundsMin
            + glm::vec3(dimensions - 1) * m_settings.probeSpacing;
        m_settings.boundsMax = m_composed.boundsMax;
        m_composed.settings.probeSpacing = m_settings.probeSpacing;
        m_composed.settings.maxRayDistance = m_settings.maxRayDistance;
        const std::size_t count = static_cast<std::size_t>(dimensions.x)
            * dimensions.y * dimensions.z;
        m_composed.probes.resize(count);
    }
    if (!m_composed.IsValid()) {
        if (error) *error = "Dynamic irradiance volume produced an invalid probe grid.";
        Reset(); return false;
    }
    m_probes.resize(m_composed.probes.size());
    m_dirty.assign(m_probes.size(), 1u);
    const std::uint32_t seedRays = std::max(16u, std::min(m_settings.raysPerProbe, 64u));
    std::array<glm::vec3, 4> environmentSh{};
    for (std::uint32_t ray = 0; ray < seedRays; ++ray) {
        const glm::vec3 direction = SphereDirection(ray, seedRays, 0);
        const auto basis = Sh4Basis(direction);
        const glm::vec3 radiance = initialEnvironment.Sample(direction);
        for (std::size_t coefficient = 0; coefficient < 4; ++coefficient)
            environmentSh[coefficient] += radiance * basis[coefficient];
    }
    const float projection = 4.0f * kPi / static_cast<float>(seedRays);
    environmentSh[0] *= projection * kPi;
    for (std::size_t coefficient = 1; coefficient < 4; ++coefficient)
        environmentSh[coefficient] *= projection * (2.0f * kPi / 3.0f);
    for (std::size_t index = 0; index < m_probes.size(); ++index) {
        DynamicIrradianceProbe& probe = m_probes[index];
        probe.irradianceSH = m_hasBakedBaseline
            ? m_baked.probes[index].irradianceSH : environmentSh;
        probe.validity = 1.0f;
        probe.confidence = m_hasBakedBaseline ? 0.0f : 0.15f;
        ComposeProbe(index);
    }
    RefreshStats();
    if (error) error->clear();
    return true;
}

void DynamicIrradianceSystem::SetSceneGeometry(
    const std::vector<LightingTriangle>& triangles) {
    m_scene.Build(triangles);
    InvalidateAll();
}

void DynamicIrradianceSystem::Reset() {
    m_grid.Reset();
    m_scene.Reset();
    m_baked = {};
    m_composed = {};
    m_probes.clear();
    m_dirty.clear();
    m_scheduleCursor = 0;
    m_hasBakedBaseline = false;
    m_hasPreviousLighting = false;
    m_previousLights.clear();
    m_stats = {};
}

bool DynamicIrradianceSystem::InitializeGpu(std::string* error) {
    return m_composed.IsValid() && m_grid.Upload(m_composed, error);
}

glm::vec3 DynamicIrradianceSystem::ProbeGridPosition(std::size_t index) const {
    if (index >= m_probes.size()) return glm::vec3(0.0f);
    const int x = static_cast<int>(index % static_cast<std::size_t>(m_composed.dimensions.x));
    const std::size_t yz = index / static_cast<std::size_t>(m_composed.dimensions.x);
    const int y = static_cast<int>(yz % static_cast<std::size_t>(m_composed.dimensions.y));
    const int z = static_cast<int>(yz / static_cast<std::size_t>(m_composed.dimensions.y));
    return m_composed.boundsMin + glm::vec3(x, y, z) * m_composed.spacing;
}

glm::vec3 DynamicIrradianceSystem::ProbeSamplePosition(std::size_t index) const {
    return ProbeGridPosition(index) + (index < m_probes.size()
        ? m_probes[index].relocationOffset : glm::vec3(0.0f));
}

void DynamicIrradianceSystem::ClassifyAndRelocate(
    std::size_t index, const glm::vec3& cameraPosition) {
    DynamicIrradianceProbe& probe = m_probes[index];
    const glm::vec3 original = ProbeGridPosition(index);
    if (!m_scene.Empty()) {
        const glm::vec3 margin(m_composed.spacing * 2.0f);
        if (glm::any(glm::lessThan(original, m_scene.BoundsMin() - margin))
            || glm::any(glm::greaterThan(original, m_scene.BoundsMax() + margin))) {
            probe.state = DynamicProbeState::OutsideGeometry;
            probe.validity = 0.0f;
            return;
        }
    }
    if (glm::distance(original, cameraPosition) > m_settings.activeDistance) {
        probe.state = DynamicProbeState::Sleeping;
        probe.validity = std::max(probe.validity, 0.25f);
        return;
    }
    if (!m_settings.classification || m_scene.Empty()) {
        probe.state = DynamicProbeState::Active;
        probe.validity = 1.0f;
        return;
    }
    static constexpr glm::vec3 directions[6] = {
        {1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    const float searchDistance = m_composed.spacing * 0.55f;
    const float dangerDistance = m_composed.spacing * 0.16f;
    glm::vec3 correction(0.0f);
    int nearCount = 0;
    int hitCount = 0;
    for (const glm::vec3& direction : directions) {
        LightingRayHit hit;
        if (!m_scene.Trace(original + direction * 0.002f, direction,
                           searchDistance, &hit)) continue;
        ++hitCount;
        if (hit.distance < dangerDistance) {
            ++nearCount;
            correction -= direction * (dangerDistance - hit.distance);
        }
    }
    if (nearCount >= 6) {
        probe.state = DynamicProbeState::InsideGeometry;
        probe.validity = 0.0f;
        return;
    }
    if (m_settings.relocation && nearCount > 0) {
        const float maximumOffset = m_composed.spacing * 0.45f;
        if (glm::dot(correction, correction) > 1e-8f) {
            correction = glm::normalize(correction)
                * std::min(glm::length(correction), maximumOffset);
            probe.relocationOffset = glm::mix(probe.relocationOffset, correction, 0.35f);
        }
        probe.state = DynamicProbeState::Relocated;
        probe.validity = 1.0f;
    } else {
        probe.relocationOffset *= 0.9f;
        probe.state = hitCount == 6 && nearCount >= 4
            ? DynamicProbeState::Invalid : DynamicProbeState::Active;
        probe.validity = probe.state == DynamicProbeState::Invalid ? 0.0f : 1.0f;
    }
}

glm::vec3 DynamicIrradianceSystem::EvaluateHitRadiance(
    const LightingRayHit& hit, const DirectionalSkyRadiance& environment,
    const std::vector<DynamicGiLight>& lights) const {
    glm::vec3 incoming(0.0f);
    const glm::vec3 sunDirection = glm::normalize(environment.sunDirection);
    const float sunNdotL = std::max(glm::dot(hit.normal, sunDirection), 0.0f);
    if (sunNdotL > 0.0f
        && !m_scene.Occluded(hit.position + hit.normal * 0.025f,
                             sunDirection, m_settings.maxRayDistance)) {
        incoming += glm::max(environment.sunRadiance, glm::vec3(0.0f)) * sunNdotL;
    }
    for (const DynamicGiLight& light : lights) {
        if (!light.affectDynamicGi || light.type == DynamicGiLight::Type::Directional) continue;
        const glm::vec3 delta = light.position - hit.position;
        const float distanceSquared = glm::dot(delta, delta);
        if (distanceSquared >= light.range * light.range || distanceSquared < 1e-8f) continue;
        const float distance = std::sqrt(distanceSquared);
        const glm::vec3 L = delta / distance;
        const float nDotL = std::max(glm::dot(hit.normal, L), 0.0f);
        if (nDotL <= 0.0f || m_scene.Occluded(hit.position + hit.normal * 0.025f,
                                              L, distance - 0.025f)) continue;
        float attenuation = FiniteAttenuation(distanceSquared, light.range);
        if (light.type == DynamicGiLight::Type::Spot) {
            const float cone = glm::dot(glm::normalize(-light.direction), L);
            attenuation *= glm::smoothstep(light.outerCos, light.innerCos, cone);
        }
        incoming += glm::max(light.radiance, glm::vec3(0.0f)) * attenuation * nDotL;
    }
    const glm::vec3 diffuse = glm::max(hit.albedo, glm::vec3(0.0f)) * incoming / kPi;
    return diffuse + glm::max(hit.emissive, glm::vec3(0.0f));
}

DynamicIrradianceSystem::ProbeSample DynamicIrradianceSystem::TraceProbe(
    std::size_t index, const DirectionalSkyRadiance& environment,
    const std::vector<DynamicGiLight>& lights, std::uint32_t frameIndex) const {
    ProbeSample result;
    const std::uint32_t rays = std::min(m_settings.raysPerProbe,
        std::max(8u, m_settings.maxGiRaysPerFrame));
    const glm::vec3 origin = ProbeSamplePosition(index);
    float depthSum = 0.0f, depthSquaredSum = 0.0f;
    std::uint32_t skyRays = 0, skyVisible = 0;
    for (std::uint32_t ray = 0; ray < rays; ++ray) {
        const glm::vec3 direction = SphereDirection(ray, rays,
            frameIndex + static_cast<std::uint32_t>(index));
        if (direction.y > 0.0f) ++skyRays;
        LightingRayHit hit;
        glm::vec3 radiance;
        if (m_scene.Trace(origin, direction, m_settings.maxRayDistance, &hit)) {
            radiance = EvaluateHitRadiance(hit, environment, lights);
            const float normalizedDepth = std::clamp(
                hit.distance / m_settings.maxRayDistance, 0.0f, 1.0f);
            depthSum += normalizedDepth;
            depthSquaredSum += normalizedDepth * normalizedDepth;
        } else {
            radiance = environment.Sample(direction);
            depthSum += 1.0f;
            depthSquaredSum += 1.0f;
            if (direction.y > 0.0f) ++skyVisible;
        }
        const auto basis = Sh4Basis(direction);
        for (std::size_t coefficient = 0; coefficient < 4; ++coefficient)
            result.sh[coefficient] += radiance * basis[coefficient];
    }
    const float projection = 4.0f * kPi / static_cast<float>(rays);
    result.sh[0] *= projection * kPi;
    for (std::size_t coefficient = 1; coefficient < 4; ++coefficient)
        result.sh[coefficient] *= projection * (2.0f * kPi / 3.0f);
    result.skyVisibility = static_cast<float>(skyVisible) / std::max(skyRays, 1u);
    result.depthMean = depthSum / static_cast<float>(rays);
    result.depthSecondMoment = depthSquaredSum / static_cast<float>(rays);
    result.rays = rays;
    return result;
}

void DynamicIrradianceSystem::ComposeProbe(std::size_t index) {
    DynamicIrradianceProbe& dynamic = m_probes[index];
    LightingProbe& output = m_composed.probes[index];
    const float dynamicWeight = std::clamp(dynamic.confidence * m_settings.intensity,
                                           0.0f, 1.0f);
    for (std::size_t coefficient = 0; coefficient < 4; ++coefficient) {
        const glm::vec3 baseline = m_hasBakedBaseline
            ? m_baked.probes[index].irradianceSH[coefficient]
            : dynamic.irradianceSH[coefficient];
        output.irradianceSH[coefficient] = glm::mix(
            baseline, dynamic.irradianceSH[coefficient], dynamicWeight);
    }
    output.skyVisibility = m_hasBakedBaseline
        ? glm::mix(m_baked.probes[index].skyVisibility,
                   dynamic.skyVisibility, dynamicWeight)
        : dynamic.skyVisibility;
    output.depthMean = dynamic.depthMean;
    output.depthSecondMoment = dynamic.depthSecondMoment;
    output.valid = dynamic.validity > 0.01f
        && dynamic.state != DynamicProbeState::InsideGeometry
        && dynamic.state != DynamicProbeState::Invalid
        && dynamic.state != DynamicProbeState::OutsideGeometry;
    const auto up = Sh4Basis(glm::vec3(0, 1, 0));
    output.ambient = glm::max(output.irradianceSH[0] * up[0]
        + output.irradianceSH[1] * up[1] + output.irradianceSH[2] * up[2]
        + output.irradianceSH[3] * up[3], glm::vec3(0.0f));
}

void DynamicIrradianceSystem::Update(
    const glm::vec3& cameraPosition, const DirectionalSkyRadiance& environment,
    const std::vector<DynamicGiLight>& lights, std::uint32_t frameIndex) {
    if (!m_settings.enabled || m_probes.empty()) return;
    const auto begin = std::chrono::steady_clock::now();
    m_stats.raysCast = 0;
    m_stats.probesUpdated = 0;

    // Lighting edits must reach nearby probes immediately rather than waiting
    // for the round-robin cursor to revisit them.  Sun/sky changes affect the
    // complete volume; local light edits only invalidate their influence area.
    const auto changedVec3 = [](const glm::vec3& a, const glm::vec3& b, float epsilon) {
        const glm::vec3 delta = a - b;
        return glm::dot(delta, delta) > epsilon * epsilon;
    };
    if (m_hasPreviousLighting) {
        if (changedVec3(environment.sunDirection, m_previousEnvironment.sunDirection, 0.002f)
            || changedVec3(environment.sunRadiance, m_previousEnvironment.sunRadiance, 0.01f)
            || changedVec3(environment.zenith, m_previousEnvironment.zenith, 0.01f)
            || changedVec3(environment.horizon, m_previousEnvironment.horizon, 0.01f)
            || changedVec3(environment.ground, m_previousEnvironment.ground, 0.01f)) {
            InvalidateAll();
        }
        const std::size_t common = std::min(lights.size(), m_previousLights.size());
        for (std::size_t i = 0; i < common; ++i) {
            const DynamicGiLight& current = lights[i];
            const DynamicGiLight& previous = m_previousLights[i];
            if (current.affectDynamicGi != previous.affectDynamicGi
                || current.type != previous.type
                || changedVec3(current.position, previous.position, 0.01f)
                || changedVec3(current.direction, previous.direction, 0.002f)
                || changedVec3(current.radiance, previous.radiance, 0.01f)
                || std::abs(current.range - previous.range) > 0.01f) {
                InvalidateSphere(previous.position, previous.range + m_settings.probeSpacing);
                InvalidateSphere(current.position, current.range + m_settings.probeSpacing);
            }
        }
        for (std::size_t i = common; i < m_previousLights.size(); ++i)
            InvalidateSphere(m_previousLights[i].position,
                             m_previousLights[i].range + m_settings.probeSpacing);
        for (std::size_t i = common; i < lights.size(); ++i)
            InvalidateSphere(lights[i].position, lights[i].range + m_settings.probeSpacing);
    } else {
        InvalidateAll();
        m_hasPreviousLighting = true;
    }
    m_previousEnvironment = environment;
    m_previousLights = lights;
    const std::uint32_t rayLimited = std::max(1u,
        m_settings.maxGiRaysPerFrame / std::max(m_settings.raysPerProbe, 1u));
    const std::size_t budget = std::min<std::size_t>(
        std::min(m_settings.probesPerFrame, rayLimited), m_probes.size());
    const std::size_t candidateCount = std::min<std::size_t>(
        m_probes.size(), std::max<std::size_t>(budget * 8u, 64u));
    struct Candidate { std::size_t index; float score; };
    std::vector<Candidate> candidates;
    candidates.reserve(candidateCount);
    for (std::size_t offset = 0; offset < candidateCount; ++offset) {
        const std::size_t index = (m_scheduleCursor + offset) % m_probes.size();
        const DynamicIrradianceProbe& probe = m_probes[index];
        const float distance = glm::distance(ProbeGridPosition(index), cameraPosition);
        const float age = static_cast<float>(frameIndex - probe.lastUpdatedFrame);
        const float dirtyBonus = m_dirty[index] ? 100000.0f : 0.0f;
        float localLightBonus = 0.0f;
        const glm::vec3 probePosition = ProbeGridPosition(index);
        for (const DynamicGiLight& light : lights) {
            if (!light.affectDynamicGi || light.type == DynamicGiLight::Type::Directional) continue;
            const float normalizedDistance = glm::distance(probePosition, light.position)
                / std::max(light.range, 0.01f);
            if (normalizedDistance < 1.25f)
                localLightBonus = std::max(localLightBonus, (1.25f - normalizedDistance) * 40.0f);
        }
        candidates.push_back({index, dirtyBonus + age * 2.0f + localLightBonus - distance});
    }
    std::partial_sort(candidates.begin(), candidates.begin() + budget, candidates.end(),
        [](const Candidate& a, const Candidate& b) { return a.score > b.score; });
    std::vector<std::size_t> changed;
    changed.reserve(budget);
    for (std::size_t selection = 0; selection < budget; ++selection) {
        const std::size_t index = candidates[selection].index;
        ClassifyAndRelocate(index, cameraPosition);
        DynamicIrradianceProbe& probe = m_probes[index];
        if (probe.state == DynamicProbeState::Sleeping
            || probe.state == DynamicProbeState::OutsideGeometry
            || probe.state == DynamicProbeState::InsideGeometry
            || probe.state == DynamicProbeState::Invalid) {
            ComposeProbe(index); changed.push_back(index); m_dirty[index] = 0; continue;
        }
        // Cull local lights once per probe, not once per ray/hit.  Keep the
        // nearest sixteen overlapping lights to bound CPU work in dense scenes.
        std::vector<std::pair<float, const DynamicGiLight*>> nearby;
        nearby.reserve(lights.size());
        const glm::vec3 samplePosition = ProbeSamplePosition(index);
        for (const DynamicGiLight& light : lights) {
            if (!light.affectDynamicGi || light.type == DynamicGiLight::Type::Directional) continue;
            const glm::vec3 delta = light.position - samplePosition;
            const float distanceSquared = glm::dot(delta, delta);
            const float reach = light.range + m_settings.maxRayDistance;
            if (distanceSquared <= reach * reach) nearby.emplace_back(distanceSquared, &light);
        }
        constexpr std::size_t kMaximumRelevantLights = 16;
        if (nearby.size() > kMaximumRelevantLights) {
            std::partial_sort(nearby.begin(), nearby.begin() + kMaximumRelevantLights, nearby.end(),
                [](const auto& a, const auto& b) { return a.first < b.first; });
            nearby.resize(kMaximumRelevantLights);
        }
        std::vector<DynamicGiLight> relevantLights;
        relevantLights.reserve(nearby.size());
        for (const auto& entry : nearby) relevantLights.push_back(*entry.second);
        const ProbeSample sample = TraceProbe(index, environment, relevantLights, frameIndex);
        const float oldLuminance = Luminance(probe.irradianceSH[0]);
        const float newLuminance = Luminance(sample.sh[0]);
        const float relativeChange = std::abs(newLuminance - oldLuminance)
            / std::max(std::max(oldLuminance, newLuminance), 0.05f);
        const float adaptiveHysteresis = relativeChange > 0.35f
            ? std::min(m_settings.hysteresis, 0.55f) : m_settings.hysteresis;
        for (std::size_t coefficient = 0; coefficient < 4; ++coefficient)
            probe.irradianceSH[coefficient] = glm::mix(
                sample.sh[coefficient], probe.irradianceSH[coefficient],
                adaptiveHysteresis);
        probe.skyVisibility = glm::mix(sample.skyVisibility, probe.skyVisibility,
                                       adaptiveHysteresis);
        probe.depthMean = glm::mix(sample.depthMean, probe.depthMean, adaptiveHysteresis);
        probe.depthSecondMoment = glm::mix(sample.depthSecondMoment,
                                           probe.depthSecondMoment,
                                           adaptiveHysteresis);
        probe.confidence = std::min(1.0f, probe.confidence + 0.12f);
        probe.validity = 1.0f;
        probe.lastUpdatedFrame = frameIndex;
        ++probe.revision;
        m_dirty[index] = 0;
        m_stats.raysCast += sample.rays;
        ++m_stats.probesUpdated;
        ComposeProbe(index);
        changed.push_back(index);
    }
    m_scheduleCursor = (m_scheduleCursor + candidateCount) % m_probes.size();
    m_stats.updateMilliseconds = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - begin).count();
    const auto uploadBegin = std::chrono::steady_clock::now();
    if (m_grid.Valid() && !changed.empty()) m_grid.UpdateCombined(m_composed, changed);
    m_stats.uploadMilliseconds = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - uploadBegin).count();
    RefreshStats();
}

void DynamicIrradianceSystem::InvalidateAll() {
    std::fill(m_dirty.begin(), m_dirty.end(), std::uint8_t{1});
    for (DynamicIrradianceProbe& probe : m_probes) ++probe.revision;
}

void DynamicIrradianceSystem::InvalidateSphere(const glm::vec3& center, float radius) {
    const float safeRadius = std::max(radius, m_composed.spacing);
    for (std::size_t index = 0; index < m_probes.size(); ++index) {
        if (glm::distance(ProbeGridPosition(index), center) <= safeRadius) {
            m_dirty[index] = 1;
            ++m_probes[index].revision;
        }
    }
}

void DynamicIrradianceSystem::RefreshStats() {
    m_stats.activeProbes = m_stats.sleepingProbes = m_stats.relocatedProbes
        = m_stats.invalidProbes = 0;
    for (const DynamicIrradianceProbe& probe : m_probes) {
        switch (probe.state) {
        case DynamicProbeState::Active: ++m_stats.activeProbes; break;
        case DynamicProbeState::Sleeping: ++m_stats.sleepingProbes; break;
        case DynamicProbeState::Relocated: ++m_stats.relocatedProbes; break;
        case DynamicProbeState::Invalid:
        case DynamicProbeState::InsideGeometry:
        case DynamicProbeState::OutsideGeometry: ++m_stats.invalidProbes; break;
        }
    }
    m_stats.memoryBytes = static_cast<std::uint64_t>(m_probes.size())
        * (sizeof(DynamicIrradianceProbe) + sizeof(LightingProbe))
        + m_grid.MemoryBytes();
}

} // namespace engine
