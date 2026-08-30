#pragma once

#include "engine/physics/PhysicsComponents.h"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>
#include <utility>
#include <vector>

namespace engine {
class PhysicsWorld;
namespace ecs { class Registry; struct Transform; struct Collider; }

// A kinematic capsule character controller. It does NOT participate in the rigid-
// body solver: instead each Move() sweeps its capsule against the scene colliders
// and "collides and slides" -- sliding along walls, walking up shallow slopes, and
// stepping over small ledges. Position is the capsule's centre; the capsule stands
// upright along +Y with total height 'height' (>= 2*radius) and the given radius.
class CharacterController {
public:
    glm::vec3 position{0.0f};               // Capsule centre position
    float     radius = 0.4f;
    float     height = 1.8f;                // total capsule height (>= 2*radius)

    glm::vec3 velocity{0.0f};               // controller-managed (gravity/jump on Y)
    glm::vec3 gravity{0.0f, -9.81f, 0.0f};
     
    bool      grounded = false;             // set by Move()
    glm::vec3 groundNormal{0.0f, 1.0f, 0.0f};

    float     maxSlopeCos = std::cos(glm::radians(50.0f));  // steeper = wall
    float     stepHeight  = 0.35f;          // ledges up to this are stepped over
    int       depenetrationIters = 4;
    std::uint32_t collisionMask = ecs::CollisionLayer::CharacterBlockers;

    void SetMaxSlopeDegrees(float deg) { maxSlopeCos = std::cos(glm::radians(deg)); }

    // Advance one fixed step. wishVel is the desired horizontal velocity (x,z);
    // vertical motion (gravity, landing) is handled internally. Trigger colliders
    // and object channels excluded by collisionMask are ignored. Pass the PhysicsWorld
    // to reuse its Pass-1 broad phase (nearby candidates only); omit it (nullptr) to keep
    // the exact full scan. Either way collision uses the canonical world collider.
    void Move(ecs::Registry& registry, const glm::vec3& wishVel, float dt,
              const PhysicsWorld* world = nullptr);

    // Move freely in three dimensions without gravity, stepping, or a ground
    // probe. This is used by swimming/flying movement modes while retaining the
    // same capsule collision and slide behaviour as grounded movement.
    void MoveFree(ecs::Registry& registry, const glm::vec3& wishVel, float dt,
                  const PhysicsWorld* world = nullptr);

    // Resize the capsule while keeping its feet at the same world height. A
    // smaller height is always accepted; growing back to standing height is
    // rejected when a blocking collider occupies the added head room.
    bool TrySetHeight(ecs::Registry& registry, float newHeight,
                      const PhysicsWorld* world = nullptr);

    // Jump helper: sets upward velocity if grounded.
    void Jump(float speed) { if (grounded) { velocity.y = speed; grounded = false; } }

private:
    // Returns true if a steeper-than-walkable surface (a wall/step) was hit.
    bool ResolvePenetrations(ecs::Registry& registry);
    // Fill `out` with the CANONICAL world colliders (Pass-1 BuildWorldCollider) near the
    // capsule segment [p0,p1], filtered by trigger/collisionMask. Uses the broad phase when
    // m_world is set and valid, otherwise the exact full ECS scan.
    void GatherCandidates(ecs::Registry& registry, const glm::vec3& p0, const glm::vec3& p1,
                          std::vector<std::pair<ecs::Transform, ecs::Collider>>& out) const;
    const PhysicsWorld* m_world = nullptr;   // set for the duration of a Move/TrySetHeight call
};

} // namespace engine
