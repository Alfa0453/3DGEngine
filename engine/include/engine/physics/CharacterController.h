#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>

namespace engine {
namespace ecs { class Registry; }

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

    void SetMaxSlopeDegrees(float deg) { maxSlopeCos = std::cos(glm::radians(deg)); }

    // Advance one fixed step. wishVel is the desired horizontal velocity (x,z);
    // vertical motion (gravity, landing) is handled internally. Trigger colliders
    // are ignored.
    void Move(ecs::Registry& registry, const glm::vec3& wishVel, float dt);

    // Jump helper: sets upward velocity if grounded.
    void Jump(float speed) { if (grounded) { velocity.y = speed; grounded = false; } }

private:
    // Returns true if a steeper-than-walkable surface (a wall/step) was hit.
    bool ResolvePenetrations(ecs::Registry& registry);
};

} // namespace engine