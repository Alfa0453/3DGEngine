#pragma once

#include "engine/ecs/Components.h"
#include "engine/ecs/Registry.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/geometric.hpp>

namespace engine {
namespace ecs {

inline void UpdateRuntimeMotion(Registry& registry, float dt) {
    registry.view<Transform, LinearVelocity>().each(
        [dt](Entity, Transform& transform, LinearVelocity& motion) {
            transform.position += motion.velocity * dt;
        });

    registry.view<Transform, AngularVelocity>().each(
        [dt](Entity, Transform& transform, AngularVelocity& motion) {
            if (motion.radiansPerSecond == 0.0f || glm::dot(motion.axis, motion.axis) == 0.0f) {
                return;
            }

            const glm::quat rotation = glm::angleAxis(
                motion.radiansPerSecond * dt,
                glm::normalize(motion.axis));
                transform.rotation = glm::normalize(rotation * transform.rotation);
        }
    );
}

} // namespace ecs
} // namespace engine