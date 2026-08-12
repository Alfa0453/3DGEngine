#pragma once

#include "engine/ecs/Components.h"
#include "engine/ecs/Registry.h"
#include "engine/physics/PhysicsComponents.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/geometric.hpp>
#include <cmath>

namespace engine {
namespace ecs {

inline void ApplyRotatorToTransform(Transform& transform, const Rotator& rotator, float dt) {
    const glm::quat rotation = glm::angleAxis(
        rotator.radiansPerSecond * dt,
        glm::normalize(rotator.axis));
    transform.rotation = glm::normalize(rotation * transform.rotation);
}
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

inline void UpdateGameplay(Registry& registry, float dt) {
    registry.view<Transform, Mover>().each(
        [&registry, dt](Entity entity, Transform& transform, Mover& mover) {
            if (mover.speed == 0.0f || mover.distance == 0.0f || glm::dot(mover.axis, mover.axis) == 0.0f) {
                return;
            }
            if (!mover.initialized) {
                mover.origin = transform.position;
                mover.initialized = true;
            }

            mover.phase += mover.speed * dt;
            const glm::vec3 axis = glm::normalize(mover.axis);
            const glm::vec3 target = mover.origin + axis * std::sin(mover.phase) * mover.distance;

            if (RigidBody* body = registry.TryGet<RigidBody>(entity)) {
                if (body->invMass > 0.0f) {
                    const glm::vec3 desiredVelocity = axis * std::cos(mover.phase) * mover.distance * mover.speed;
                    const glm::vec3 correctionVelocity = (target - transform.position) * 6.0f;
                    body->velocity = desiredVelocity + correctionVelocity;
                    body->sleeping = false;
                    body->sleepTimer = 0.0f;
                    return;
                }
            }

            transform.position = target;
        }
    );

    registry.view<Transform, Rotator>().each(
        [&registry, dt](Entity entity, Transform& transform, Rotator& rotator) {
            if (rotator.radiansPerSecond == 0.0f || glm::dot(rotator.axis, rotator.axis) == 0.0f) {
                return;
            }

            const glm::vec3 axis = glm::normalize(rotator.axis);
            if (RigidBody* body = registry.TryGet<RigidBody>(entity)) {
                const Collider* collider = registry.TryGet<Collider>(entity);
                const bool canUsePhysicsRotation = collider && collider->shape != ColliderShape::Plane;
                if (body->invMass > 0.0f && !body->freezeRotation && canUsePhysicsRotation) {
                    body->angularVelocity = axis * rotator.radiansPerSecond;
                    body->sleeping = false;
                    body->sleepTimer = 0.0f;
                    return;
                }
            }

            ApplyRotatorToTransform(transform, rotator, dt);
        }
    );
}

} // namespace ecs
} // namespace engine