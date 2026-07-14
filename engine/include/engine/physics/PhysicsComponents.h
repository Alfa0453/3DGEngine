#pragma once

#include <glm/glm.hpp>

#include <algorithm>

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

    // Velocity damping, applied every step (v *= 1/(1 + dt*damping)). Bleeds off
    // residual jitter left by the non-warm-started contact solver so resting bodies
    // actually settle and sleep; angular damping also stops slow rocking and lets
    // rolling spheres come to rest. Raise for floatier motion, set 0 to disable.
    float     linearDamping  = 0.05f;
    float     angularDamping = 0.6f;

    // Continuous collision: sweep this body's motion each step so it cannot
    // tunnel through thin geometry. Opt-in (costs an extra sweep) -- for bullets
    // and other fast movers. The moving body is approximated as a sphere.
    bool      ccd = false;

    // Angular dynamics. invInertiaLocal is the body-space inverse inertia tensor;
    // it is initialized from the collider on the first step (unless freezeRotation
    // is set, which locks the body against spinning). Orientation lives on the
    // entity's Transform.rotation.
    glm::vec3 angularVelocity{0.0f};
    glm::vec3 accumTorque{0.0f};
    glm::mat3 invInertiaLocal{0.0f};
    bool      freezeRotation = false;

    void AddForce(const glm::vec3& f) { accumForce += f; }
    void AddTorque(const glm::vec3& tq) { accumTorque += tq; }
    // Apply a force at a world point (com = centre of mass): adds the torque too.
    void AddForceAtPoint(const glm::vec3& f, const glm::vec3& point, const glm::vec3& com) {
        accumForce  += f;
        accumTorque += glm::cross(point - com, f);
    }

    static RigidBody Static() { RigidBody b; b.invMass = 0.0f; b.useGravity = false; return b; }
    static RigidBody Dynamic(float mass) { RigidBody b; b.invMass = (mass > 0.0f) ? 1.0f / mass : 0.0f; return b; }

    // Inverse inertia tensors (body space, diagonal) for the primitive shapes.
    static glm::mat3 SolidBoxInvInertia(float mass, const glm::vec3& half) {
        if (mass <= 0.0f) return glm::mat3(0.0f);
        const float x = half.x * 2.0f, y = half.y * 2.0f, z = half.z * 2.0f;   // full extents
        const float ix = (mass / 12.0f) * (y * y + z * z);
        const float iy = (mass / 12.0f) * (x * x + z * z);
        const float iz = (mass / 12.0f) * (x * x + y * y);
        glm::mat3 m(0.0f);
        m[0][0] = 1.0f / ix; m[1][1] = 1.0f / iy; m[2][2] = 1.0f / iz;
        return m;
    }
    static glm::mat3 SolidSphereInvInertia(float mass, float radius) {
        if (mass <= 0.0f || radius <= 0.0f) return glm::mat3(0.0f);
        const float i = (2.0f / 5.0f) * mass * radius * radius;
        glm::mat3 m(0.0f);
        m[0][0] = m[1][1] = m[2][2] = 1.0f / i;
        return m;
    }
    // Capsule aligned with the local +Y axis: radius `r`, and `halfHeight` = the
    // half-length of the central segment (total tip-to-tip height is
    // 2*(halfHeight + r)). Approximated as a solid cylinder of the full height --
    // stable and cheap, exact enough for gameplay bodies.
    static glm::mat3 CapsuleInvInertia(float mass, float r, float halfHeight) {
        if (mass <= 0.0f || r <= 0.0f) return glm::mat3(0.0f);
        const float H  = 2.0f * (halfHeight + r);                  // full height
        const float iy = 0.5f * mass * r * r;                     // about the capsule axis
        const float ix = (mass / 12.0f) * (3.0f * r * r + H * H); // perpendicular
        glm::mat3 m(0.0f);
        m[0][0] = 1.0f / ix; m[1][1] = 1.0f / iy; m[2][2] = 1.0f / ix;
        return m;
    }
};

enum class ColliderShape { Sphere, Plane, Box, Capsule };

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
    float         halfHeight  = 0.5f;             // Capsule: half-length of the central segment
    float         restitution = 0.4f;             // material: bounciness
    float         friction    = 0.5f;             // material: Coulomb coefficient
    bool          isTrigger   = false;            // overlap-only: detected but never resolved

    static Collider MakeSphere(float r) { Collider c; c.shape = ColliderShape::Sphere; c.radius = r; return c; }
    static Collider MakePlane(const glm::vec3& n, float off) {
        Collider c; c.shape = ColliderShape::Plane; c.planeNormal = glm::normalize(n); c.planeOffset = off; return c;
    }
    static Collider MakeBox(const glm::vec3& he) { Collider c; c.shape = ColliderShape::Box; c.halfExtents = he; return c; }
    // Capsule along local +Y. `r` = radius, `halfHeight` = half-length of the
    // central segment (total height = 2*(halfHeight + r)). Convenience overload
    // takes the total height instead.
    static Collider MakeCapsule(float r, float halfHeight) {
        Collider c; c.shape = ColliderShape::Capsule; c.radius = r; c.halfHeight = halfHeight; return c;
    }
    static Collider MakeCapsuleFromHeight(float r, float totalHeight) {
        return MakeCapsule(r, std::max(0.0f, totalHeight * 0.5f - r));
    }
};

} // namespace ecs
} // namespace engine
