#include "engine/ai/AiMovement.h"

#include "engine/ai/AgentCollision.h"
#include "engine/animation/AnimatedModel.h"
#include "engine/ecs/Registry.h"
#include "engine/physics/PhysicsComponents.h"
#include "engine/physics/PhysicsWorld.h"

#include <algorithm>
#include <cmath>

namespace engine::ai {
namespace {

// Distance from an upright agent's transform origin to the bottom of its
// collider. AI transforms are kept upright (only yaw is authored at runtime),
// so the local vertical extent is also the world-space floor support height.
float AgentGroundSupportHeight(const ecs::Collider* collider, float sweepRadius) {
    if (!collider) return sweepRadius;
    using ecs::ColliderShape;
    switch (collider->shape) {
    case ColliderShape::Sphere:
        return collider->radius;
    case ColliderShape::Capsule:
        return collider->halfHeight + collider->radius;
    case ColliderShape::Cylinder:
    case ColliderShape::Cone:
        return collider->halfHeight;
    case ColliderShape::Torus:
        return collider->minorRadius;
    case ColliderShape::Plane:
        return sweepRadius;
    default:
        return collider->halfExtents.y;
    }
}

} // namespace

void UpdateAiAnimationParameters(AnimatedModel& animated,
                                 const AiMovementComponent& movement,
                                 const glm::vec3& worldVelocity) {
    const float horizontalSpeed = glm::length(glm::vec2(worldVelocity.x, worldVelocity.z));
    animated.controller.SetParameter("Speed", horizontalSpeed);
    animated.controller.SetParameter("VerticalSpeed", movement.verticalVelocity);
    animated.controller.SetBoolParameter("IsGrounded", movement.IsGrounded());
    animated.controller.SetBoolParameter("IsFalling", movement.IsFalling());
    animated.controller.SetBoolParameter("IsFlying", movement.IsFlying());
}

glm::vec3 MoveAiAgent(PhysicsWorld& physics,
                      ecs::Registry& registry,
                      ecs::Entity agent,
                      const glm::vec3& start,
                      const glm::vec3& navigationDesired,
                      float dt,
                      AiMovementComponent& movement,
                      bool hasTerrainGround,
                      float terrainGroundY) {
    if (movement.IsFlying()) {
        movement.verticalVelocity = 0.0f;
        movement.groundNormal = glm::vec3(0.0f, 1.0f, 0.0f);
        return MoveAgentWithCollision(
            physics, registry, agent, start, navigationDesired);
    }

    glm::vec3 horizontalDesired(navigationDesired.x, start.y, navigationDesired.z);
    glm::vec3 resolved = MoveAgentWithCollision(
        physics, registry, agent, start, horizontalDesired);

    // If a low obstruction stopped the direct sweep, retry at the authored step
    // height. A real wall still blocks the raised sweep; a curb or stair does not.
    const glm::vec2 requestedDelta(horizontalDesired.x - start.x,
                                   horizontalDesired.z - start.z);
    const glm::vec2 resolvedDelta(resolved.x - start.x, resolved.z - start.z);
    if (movement.stepHeight > 0.0f
        && glm::length(resolvedDelta) + 1.0e-4f < glm::length(requestedDelta)) {
        const glm::vec3 raisedStart = start + glm::vec3(0.0f, movement.stepHeight, 0.0f);
        const glm::vec3 raisedDesired = horizontalDesired
            + glm::vec3(0.0f, movement.stepHeight, 0.0f);
        const glm::vec3 stepped = MoveAgentWithCollision(
            physics, registry, agent, raisedStart, raisedDesired);
        const glm::vec2 steppedDelta(stepped.x - start.x, stepped.z - start.z);
        if (glm::length(steppedDelta) > glm::length(resolvedDelta) + 1.0e-4f) {
            resolved.x = stepped.x;
            resolved.z = stepped.z;
        }
    }

    const float safeDt = std::max(dt, 0.0f);
    movement.verticalVelocity = std::max(
        movement.verticalVelocity + std::min(movement.gravity, 0.0f) * safeDt,
        -std::max(movement.maxFallSpeed, 0.0f));
    const float predictedY = start.y + movement.verticalVelocity * safeDt;

    const ecs::Collider* collider = registry.TryGet<ecs::Collider>(agent);
    const float radius = AgentCollisionRadius(registry, agent);
    const float supportHeight = std::max(
        AgentGroundSupportHeight(collider, radius), 0.01f);
    // The grounding sweep uses a sphere with the agent's horizontal radius. Put
    // that sphere at the bottom of taller shapes (capsules, boxes, cylinders)
    // instead of at the transform origin.
    const float sweepCenterOffset = supportHeight - radius;
    const std::uint32_t mask = collider ? collider->mask
                                        : ecs::CollisionLayer::CharacterBlockers;
    const std::uint32_t layer = collider ? collider->layer
                                         : ecs::CollisionLayer::Enemy;
    const float step = std::max(movement.stepHeight, 0.0f);
    const float probe = std::max(movement.groundProbeDistance, 0.02f);
    const float probeTopY = std::max(start.y, predictedY) + step;
    const float probeBottomY = predictedY - probe;
    const glm::vec3 probeStart(
        resolved.x, probeTopY - sweepCenterOffset, resolved.z);
    const glm::vec3 probeEnd(
        resolved.x, probeBottomY - sweepCenterOffset, resolved.z);
    const RaycastHit floorHit = physics.SphereCast(
        registry, probeStart, probeEnd, radius, agent, mask, layer);

    bool foundGround = false;
    float groundY = -1.0e30f;
    glm::vec3 groundNormal(0.0f, 1.0f, 0.0f);
    const float slopeCos = std::cos(glm::radians(
        std::clamp(movement.maxSlopeDegrees, 0.0f, 89.0f)));
    if (floorHit.hit && floorHit.normal.y >= slopeCos) {
        foundGround = true;
        // SphereCast::point is the swept sphere centre at impact. Convert that
        // centre back to the owning object's origin; subtracting the radius here
        // incorrectly placed the origin on the floor and buried the agent.
        groundY = floorHit.point.y + sweepCenterOffset;
        groundNormal = floorHit.normal;
    }
    const float terrainOriginY = terrainGroundY + supportHeight;
    if (hasTerrainGround && terrainOriginY <= probeTopY
        && terrainOriginY >= probeBottomY && terrainOriginY > groundY) {
        foundGround = true;
        groundY = terrainOriginY;
        groundNormal = glm::vec3(0.0f, 1.0f, 0.0f);
    }

    if (foundGround && predictedY <= groundY + probe) {
        resolved.y = groundY;
        movement.verticalVelocity = 0.0f;
        movement.mode = AiMovementMode::Grounded;
        movement.groundNormal = groundNormal;
    } else {
        resolved.y = predictedY;
        movement.mode = AiMovementMode::Falling;
        movement.groundNormal = glm::vec3(0.0f, 1.0f, 0.0f);
    }
    return resolved;
}

} // namespace engine::ai
