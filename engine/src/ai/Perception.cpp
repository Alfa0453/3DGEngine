#include "engine/ai/Perception.h"

#include "engine/physics/PhysicsWorld.h"
#include "engine/ecs/Registry.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>

namespace engine {
namespace ai {

bool CanSee(const glm::vec3& eye, const glm::vec3& forward, const VisionCone& cone,
            const glm::vec3& target, ecs::Entity targetEntity,
            PhysicsWorld& world, ecs::Registry& reg, ecs::Entity observerEntity) {
    const glm::vec3 d = target - eye;
    const float dist2 = glm::dot(d, d);
    const float range = std::max(cone.range, 0.0f);
    if (dist2 > range * range) return false;
    if (dist2 < 1e-10f) return true;
    const float forwardLen2 = glm::dot(forward, forward);
    if (forwardLen2 < 1e-10f) return false;
    const float dist = std::sqrt(dist2);
    const float cosToTarget = glm::dot(forward, d)
        / (std::sqrt(forwardLen2) * dist);
    if (cosToTarget < std::cos(glm::radians(cone.halfAngleDegrees))) return false;

    Ray ray;
    ray.origin = eye;
    ray.direction = d / dist;
    const RaycastHit hit = world.Raycast(
        reg, ray, dist + 0.01f, 0xFFFFFFFFu, observerEntity);

    // Blocked only if something other than the target is hit before we reach it.
    if (hit.hit && hit.entity != targetEntity && hit.distance < dist - 0.05f) return false;
    return true;
}

} // namespace ai
} // namespace engine
