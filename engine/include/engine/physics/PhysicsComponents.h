#pragma once

#include <glm/glm.hpp>

namespace engine {
namespace ecs {

// A body that participates in dynamics. invMass = 0 means infinite mass (static
// or immovable). Velocity is integrated each fixed step; accumForce is applied
// then cleared. Surface material (bounciness, friction) lives on the Collider,
// not here -- a static floor has no RigidBody yet must still be bouncy/grippy.
//
// Sleeping: a body that stays nearly still for a while is put to sleep by the
// solver (skips integration + contact response) until a moving body touches it.
// This makes settled piles almost free and removes resting jitter.
struct RigidBody {
    glm::vec3 velocity{0.0f};
    glm::vec3 accumForce{0.0f};
    float     invMass    = 1.0f;
    bool      useGravity = true;

    bool      allowSleep = true;
    bool      sleeping   = false;   // solver-managed
    float     sleepTimer = 0.0f;    // solver-managed (seconds of stillness)

    void AddForce(const glm::vec3& f) { accumForce += f; }

    static RigidBody Static() { RigidBody b; b.invMass = 0.0f; b.useGravity = false; return b; }
    static RigidBody Dynamic(float mass) { RigidBody b; b.invMass = (mass > 0.0f) ? 1.0f / mass : 0.0f; return b; }
};

enum class ColliderShape { Sphere, Plane, Box };

// The collision shape attached to an entity (positioned by its Transform). A
// Plane is an infinite half-space defined in world space by a normal and offset
// (dot(normal, x) = offset), independent of the Transform position. Material is
// a surface property (so it lives here, like Box2D fixtures / PhysX shapes):
// restitution is bounciness (0 = dead, 1 = elastic); friction is the Coulomb
// coefficient. A contact combines the two colliders' values.
struct Collider {
    ColliderShape shape = ColliderShape::Sphere;
    float         radius = 0.5f;                  // Sphere
    glm::vec3     halfExtents{0.5f};              // Box
    glm::vec3     planeNormal{0.0f, 1.0f, 0.0f};  // Plane
    float         planeOffset = 0.0f;             // Plane
    float         restitution = 0.4f;             // material: bounceness
    float         friction    = 0.5f;             // material: Coulomb coefficient
    bool          isTrigger   = false;            // overlap-only: detected but never resolved

    static Collider MakeSphere(float r) { Collider c; c.shape = ColliderShape::Sphere; c.radius = r; return c; }
    static Collider MakePlane(const glm::vec3& n, float off) {
        Collider c; c.shape = ColliderShape::Plane; c.planeNormal = glm::normalize(n); c.planeOffset = off; return c;
    }
    static Collider MakeBox(const glm::vec3& he) { Collider c; c.shape = ColliderShape::Box; c.halfExtents = he; return c; }
};

} // namespace ecs
} // namespace engine