#include "engine/ai/AgentCollision.h"

#include "engine/ecs/Registry.h"
#include "engine/physics/PhysicsComponents.h"
#include "engine/physics/PhysicsWorld.h"

#include <algorithm>
#include <cmath>

namespace engine::ai {
namespace {

float HorizontalRadius(const ecs::Collider* collider) {
    if (!collider) return 0.4f;
    using ecs::ColliderShape;
    switch (collider->shape) {
    case ColliderShape::Sphere:
    case ColliderShape::Capsule:
    case ColliderShape::Cylinder:
    case ColliderShape::Cone:
        return collider->radius;
    case ColliderShape::Torus:
        return collider->majorRadius + collider->minorRadius;
    case ColliderShape::Plane:
        return 0.4f;
    default:
        return std::max(collider->halfExtents.x, collider->halfExtents.z);
    }
}

} // namespace

float AgentCollisionRadius(const ecs::Registry& registry, ecs::Entity agent) {
    return std::clamp(HorizontalRadius(registry.TryGet<ecs::Collider>(agent)), 0.05f, 5.0f);
}

glm::vec3 MoveAgentWithCollision(PhysicsWorld& physics,
                                 ecs::Registry& registry,
                                 ecs::Entity agent,
                                 const glm::vec3& start,
                                 const glm::vec3& desired,
                                 int slideIterations) {
    const ecs::Collider* collider = registry.TryGet<ecs::Collider>(agent);
    const float radius = AgentCollisionRadius(registry, agent);
    const std::uint32_t mask = collider ? collider->mask
                                        : ecs::CollisionLayer::CharacterBlockers;
    const std::uint32_t layer = collider ? collider->layer
                                         : ecs::CollisionLayer::Enemy;

    glm::vec3 current = start;
    current.y = start.y;
    glm::vec3 target(desired.x, start.y, desired.z);
    constexpr float skin = 0.015f;

    for (int iteration = 0; iteration < std::max(slideIterations, 1); ++iteration) {
        glm::vec3 remaining = target - current;
        remaining.y = 0.0f;
        const float distance = glm::length(remaining);
        if (distance <= 1.0e-5f) break;

        const RaycastHit hit = physics.SphereCast(
            registry, current, current + remaining, radius, agent, mask, layer);
        if (!hit.hit) {
            current += remaining;
            break;
        }

        const glm::vec3 direction = remaining / distance;
        current += direction * std::max(hit.distance - skin, 0.0f);

        glm::vec3 wallNormal(hit.normal.x, 0.0f, hit.normal.z);
        const float normalLengthSq = glm::dot(wallNormal, wallNormal);
        if (normalLengthSq <= 1.0e-6f) break;
        wallNormal /= std::sqrt(normalLengthSq);

        remaining = target - current;
        remaining.y = 0.0f;
        const float intoWall = glm::dot(remaining, wallNormal);
        if (intoWall < 0.0f) remaining -= wallNormal * intoWall;
        target = current + remaining;
    }

    current.y = desired.y;
    return current;
}

} // namespace engine::ai
