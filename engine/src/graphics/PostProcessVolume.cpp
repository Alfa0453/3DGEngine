#include "engine/graphics/PostProcessVolume.h"

#include "engine/ecs/Components.h"
#include "engine/ecs/Registry.h"
#include "engine/graphics/PostProcess.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace engine {
namespace {
struct Candidate {
    ecs::Entity entity = ecs::kNull;
    ecs::PostProcessVolume* volume = nullptr;
    ecs::Transform* transform = nullptr;
    float weight = 0.0f;
};

float VolumeWeight(const ecs::PostProcessVolume& volume,
                   const ecs::Transform& transform,
                   const glm::vec3& cameraPosition) {
    if (!volume.enabled) return 0.0f;
    if (volume.unbound) return std::clamp(volume.blendWeight, 0.0f, 1.0f);
    const glm::vec3 extents = glm::max(
        volume.boxExtents * glm::abs(transform.scale), glm::vec3(0.001f));
    const glm::vec3 q = glm::abs(cameraPosition - transform.position) - extents;
    const float outsideDistance = glm::length(glm::max(q, glm::vec3(0.0f)));
    const float boundaryWeight = volume.blendDistance <= 0.0f
        ? (outsideDistance <= 0.0f ? 1.0f : 0.0f)
        : 1.0f - std::clamp(outsideDistance / volume.blendDistance, 0.0f, 1.0f);
    return boundaryWeight * std::clamp(volume.blendWeight, 0.0f, 1.0f);
}
}

void ApplyPostProcessVolumes(ecs::Registry& registry,
                             const glm::vec3& cameraPosition,
                             PostProcess& postProcess) {
    std::vector<Candidate> candidates;
    registry.view<ecs::Transform, ecs::PostProcessVolume>().each(
        [&](ecs::Entity entity, ecs::Transform& transform, ecs::PostProcessVolume& volume) {
            const float weight = VolumeWeight(volume, transform, cameraPosition);
            if (weight > 0.0f) candidates.push_back({entity, &volume, &transform, weight});
        });
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        if (a.volume->priority != b.volume->priority)
            return a.volume->priority < b.volume->priority;
        return a.entity < b.entity;
    });
    for (const Candidate& candidate : candidates) {
        const ecs::PostProcessVolume& volume = *candidate.volume;
        const float weight = candidate.weight;
        if (volume.overrideExposure)
            postProcess.settings.exposureCompensationEV = glm::mix(
                postProcess.settings.exposureCompensationEV,
                volume.exposureCompensationEV, weight);
        if (volume.overrideBloom)
            postProcess.settings.bloomStrength = glm::mix(
                postProcess.settings.bloomStrength, volume.bloomStrength, weight);
        if (volume.overrideColorGrading) {
            postProcess.settings.temperature = glm::mix(
                postProcess.settings.temperature, volume.temperature, weight);
            postProcess.settings.tint = glm::mix(postProcess.settings.tint, volume.tint, weight);
            postProcess.settings.saturation = glm::mix(
                postProcess.settings.saturation, volume.saturation, weight);
            postProcess.settings.contrast = glm::mix(
                postProcess.settings.contrast, volume.contrast, weight);
        }
        if (volume.overrideFogDensity)
            postProcess.volumetrics.density = glm::mix(
                postProcess.volumetrics.density, volume.fogDensity, weight);
    }
}
}
