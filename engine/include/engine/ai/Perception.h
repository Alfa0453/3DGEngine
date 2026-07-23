#pragma once

#include "engine/ecs/Entity.h"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <cmath>

namespace engine {
class PhysicsWorld;
namespace ecs { class Registry; }

namespace ai {

// A vision cone: how far and how wide an agent can see.
struct VisionCone {
    float range            = 20.0f;
    float halfAngleDegrees = 45.0f;     // half of the total field of view
};

// Geometry only: is 'target' within the cone's range and angle from an eye at
// 'eye' facing 'forward'? (No occlusion -- pure maths, no physics needed.)
inline bool InvisionCone(const glm::vec3& eye, const glm::vec3& forward,
                         const VisionCone& cone, const glm::vec3& target) {
    const glm::vec3 d = target - eye;
    const float dist = glm::length(d);
    if (dist > cone.range) return false;
    if (dist < 1e-5f) return true;
    const float cosToTarget = glm::dot(glm::normalize(forward), d / dist);
    return cosToTarget >= std::cos(glm::radians(cone.halfAngleDegrees));
}

// Full check: in the cone AND with clear line of sight. Casts a ray from eye to
// target; if a collider other than the target is hit first, the view is blocked.
bool CanSee(const glm::vec3& eye, const glm::vec3& forward, const VisionCone& cone,
            const glm::vec3& target, ecs::Entity targetEntity,
            PhysicsWorld& world, ecs::Registry& registry,
            ecs::Entity observerEntity = ecs::kNull);

} // namespace ai
} // namespace engine
