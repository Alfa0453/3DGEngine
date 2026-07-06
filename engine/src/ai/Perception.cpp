#include "engine/ai/Perception.h"

#include "engine/physics/PhysicsWorld.h"
#include "engine/ecs/Registry.h"

#include <glm/glm.hpp>

namespace engine {
namespace ai {

bool CanSee(const glm::vec3& eye, const glm::vec3& forward, const VisionCone& cone,
            const glm::vec3& target, ecs::Entity targetEntity,
            PhysicsWorld& world, ecs::Registry& reg) {
    if (!InvisionCone(eye, forward, cone, target)) return false;

    const glm::vec3 d = target - eye;
    const float dist = glm::length(d);
    if (dist < 1e-4f) return true;

    Ray ray;
    ray.origin = eye;
    ray.direction = d / dist;
    const RaycastHit hit = world.Raycast(reg, ray, dist + 0.01f);

    // Blocked only if something other than the target is hit before we reach it.
    if (hit.hit && hit.entity != targetEntity && hit.distance < dist - 0.05f) return false;
    return true;
}

} // namespace ai
} // namespace engine