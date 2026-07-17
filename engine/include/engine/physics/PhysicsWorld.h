#pragma once

#include <glm/glm.hpp>

#include "engine/ecs/Entity.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace engine {
namespace ecs { class Registry; struct Transform; struct Collider; struct RigidBody; }

// A collider paired with its transform and (optional) body, gathered once per
// step. A null body means a static, immovable collider. Reused across steps.
struct SolverBody {
    ecs::Entity     e  = ecs::kNull;
    ecs::Transform* t  = nullptr;
    ecs::Collider*  c  = nullptr;
    ecs::RigidBody* rb = nullptr;
};

// A contact detected once per step and cached: the velocity solver re-applies its
// impulse each iteration from this (no re-detection), and one positional pass
// uses the penetration. a/b index into the step's SolverBody list.
struct ContactManifold {
    int       a = 0, b = 0;
    glm::vec3 normal{0.0f};
    float     penetration = 0.0f;
    int       count = 0;            // number of contact points (1..4)
    glm::vec3 points[4]{};          // world-space contact points (for torque)

    // Solver state (accumulated over the step's iterations; warm-started from the
    // previous step so stacks settle without relying on damping).
    glm::vec3 tangent1{0.0f}, tangent2{0.0f};   // friction basis (shared by all points)
    float     normalImpulse[4]{};               // accumulated normal impulse per point
    float     tangentImpulse[4][2]{};           // accumulated friction impulse per point/axis
    float     restBias[4]{};                    // restitution velocity target per point
    std::uint64_t key = 0;                      // entity-pair key (warm-start lookup)

    // Constants precomputed ONCE per step (positions/inertia are frozen during the
    // velocity solve), so the per-iteration loop does only dot products + impulse
    // accumulation -- no mat3 rebuilds. This is the bulk of the solver cost.
    glm::vec3 rA[4]{}, rB[4]{};                  // contact offsets from each centre
    float     normalMass[4]{};                   // 1 / effective mass along the normal
    float     tangentMass[4][2]{};               // 1 / effective mass along each tangent
    float     invMassA = 0.0f, invMassB = 0.0f;  // cached inverse masses
    glm::mat3 invIA{0.0f}, invIB{0.0f};          // cached world inverse inertia
    float     friction = 0.0f;                   // combined Coulomb coefficient
};

// Persistent per-pair contact impulses, matched to this step's points by nearest
// position, so the solver can warm-start (seed) the accumulated impulses.
struct ContactCachePoint {
    glm::vec3 point{0.0f};
    float     normalImpulse = 0.0f;
    float     tangentImpulse[2] = {0.0f, 0.0f};
};
struct ContactCache {
    int              count = 0;
    ContactCachePoint pts[4]{};
};

// A ray for scene queries. direction is normalized by Raycast if needed.
struct Ray {
    glm::vec3 origin{0.0f};
    glm::vec3 direction{0.0f, 0.0f, -1.0f};
};

// Result of a raycast. On a miss, hit == false and entity == ecs::kNull.
struct RaycastHit {
    bool        hit      = false;
    ecs::Entity entity   = ecs::kNull;
    float       distance = 0.0f;
    glm::vec3   point{0.0f};
    glm::vec3   normal{0.0f};
};

// A collision/overlap event emitted by a step. Enter on the first step a pair
// touches, Stay while they keep touching, Exit on the step they separate.
// trigger is true when either collider is a trigger (overlap-only) volume.
//
// For solid (non-trigger) Enter/Stay events, point/normal/impulse describe the
// contact: 'point' is the average world contact point, 'normal' points from a
// toward b, and 'impulse' is the total normal impulse applied this step (a proxy
// for impact strength -- use it to scale hit sounds, damage, or effects). These
// are zero for trigger events and Exit events.
struct CollisionEvent {
    enum class Phase { Enter, Stay, Exit };
    ecs::Entity a = ecs::kNull;
    ecs::Entity b = ecs::kNull;
    Phase       phase = Phase::Enter;
    bool        trigger = false;
    glm::vec3   point{0.0f};
    glm::vec3   normal{0.0f};
    float       impulse = 0.0f;
};

// A constraint between two bodies' centres, or between one body and a fixed world
// anchor (when b == ecs::kNull, the 'anchor' point is used). Distance keeps them
// exactly restLength apart (or, as a rope, only stops them stretching past it);
// Spring pulls softly toward restLength with stiffness + damping. With no angular
// dynamics yet, joints attach at body centres (rods, pendulums, ropes, springs).
struct Joint {
    enum class Type { Distance, Spring, Ball, Hinge };
    Type        type = Type::Distance;
    ecs::Entity a = ecs::kNull;
    ecs::Entity b = ecs::kNull;        // kNull => attached to the world 'anchor' point
    glm::vec3   anchor{0.0f};          // world attach point when b == kNull
    glm::vec3   localA{0.0f};          // attach point in A's local frame (offset from COM)
    glm::vec3   localB{0.0f};          // attach point in B's local frame
    float       restLength = 1.0f;
    bool        rope = false;          // Distance only: resist stretch, allow slack
    float       stiffness = 100.0f;    // Spring only
    float       damping   = 1.0f;      // Spring only
    glm::vec3   axisA{0.0f, 1.0f, 0.0f};   // Hinge: rotation axis in A's local frame
    glm::vec3   axisB{0.0f, 1.0f, 0.0f};   // Hinge: rotation axis in B's local frame
};

// The physics solver. Step() integrates every RigidBody under gravity, detects
// contacts between Collider entities, and resolves them with impulses plus
// positional correction. Call it from the fixed-timestep OnFixedUpdate so the
// simulation is deterministic and frame-rate independent.
class PhysicsWorld {
public:
    glm::vec3 gravity{0.0f, -9.81f, 0.0f};
    int       solverIterations = 4;      // sequential-impulse passes per step

    // Broad phase: a uniform spatial hash culls the O(n^2) pair test. Disable to
    // fall back to brute-force all-pairs (identical results; used for testing).
    bool      broadPhase = true;
    float     cellSize   = 2.0f;         // grid cell edge length (world units)

    // Below this closing speed a contact does not bounce (restitution slop) --
    // stops resting stacks from micro-bouncing so they can settle and sleep.
    float     restitutionThreshold = 0.5f;

    // Sleeping: a body moving slower than sleepLinearVelocity for timeToSleep
    // seconds is put to sleep; a fast contact wakes it.
    bool      allowSleeping        = true;
    float     sleepLinearVelocity  = 0.06f;
    float     sleepAngularVelocity = 0.15f;   // rad/s below which a body may sleep
    float     timeToSleep          = 0.5f;

    void Step(ecs::Registry& registry, float dt);

    // Cast a ray through all colliders and return the closest hit within
    // maxDistance (sphere / plane / box). direction need not be pre-normalized.
    RaycastHit Raycast(ecs::Registry& registry, const Ray& ray,
                       float maxDistance = 1.0e30f,
                       std::uint32_t layerMask = 0xFFFFFFFFu) const;

    // Sweep a sphere from start to end and return the earliest solid collider.
    // Trigger volumes and the optional ignored entity are skipped. This is useful
    // for camera booms and other obstruction tests that need volume, not a thin ray.
    RaycastHit SphereCast(ecs::Registry& registry,
                          const glm::vec3& start,
                          const glm::vec3& end,
                          float radius,
                          ecs::Entity ignored = ecs::kNull,
                          std::uint32_t layerMask = 0xFFFFFFFFu) const;

    // Collect every collider overlapping a world-space sphere (AoE queries:
    // explosions, detection). Trigger volumes are included. Filtered by layerMask.
    std::vector<ecs::Entity> OverlapSphere(ecs::Registry& registry,
                                           const glm::vec3& center, float radius,
                                           std::uint32_t layerMask = 0xFFFFFFFFu) const;

    // Apply a radial impulse to dynamic bodies within 'radius' of 'center',
    // scaled by 1 - dist/radius (linear falloff). Wakes sleeping bodies. Handy for
    // explosions. strength is the peak impulse magnitude at the centre.
    void ApplyRadialImpulse(ecs::Registry& registry, const glm::vec3& center,
                            float radius, float strength,
                            std::uint32_t layerMask = 0xFFFFFFFFu) const;

    // Enter/Stay/Exit events generated by the most recent Step (both solid
    // contacts and trigger overlaps). Valid until the next Step.
    const std::vector<CollisionEvent>& Events() const { return m_events; }

    // --- Joints -----------------------------------------------------------
    void AddJoint(const Joint& j) { m_joints.push_back(j); }
    void AddDistanceJoint(ecs::Entity a, ecs::Entity b, float restLength, bool rope = false) {
        Joint j; j.type = Joint::Type::Distance; j.a = a; j.b = b; j.restLength = restLength; j.rope = rope;
        m_joints.push_back(j);
    }
    void AddDistanceJointToWorld(ecs::Entity a, const glm::vec3& anchor, float restLength, bool rope = false) {
        Joint j; j.type = Joint::Type::Distance; j.a = a; j.anchor = anchor; j.restLength = restLength; j.rope = rope;
        m_joints.push_back(j);
    }
    void AddSpringJoint(ecs::Entity a, ecs::Entity b, float restLength, float stiffness, float damping) {
        Joint j; j.type = Joint::Type::Spring; j.a = a; j.b = b; j.restLength = restLength;
        j.stiffness = stiffness; j.damping = damping; m_joints.push_back(j);
    }
    void AddSpringJointToWorld(ecs::Entity a, const glm::vec3& anchor, float restLength, float stiffness, float damping) {
        Joint j; j.type = Joint::Type::Spring; j.a = a; j.anchor = anchor; j.restLength = restLength;
        j.stiffness = stiffness; j.damping = damping; m_joints.push_back(j);
    }
    void ClearJoints() { m_joints.clear(); }

    // Ball (point-to-point) joint: pins a point on A to a point on B (localA/localB
    // are offsets from each body's centre), leaving all 3 rotational DOF free.
    void AddBallJoint(ecs::Entity a, ecs::Entity b,
                      const glm::vec3& localA = glm::vec3(0.0f),
                      const glm::vec3& localB = glm::vec3(0.0f)) {
        Joint j; j.type = Joint::Type::Ball; j.a = a; j.b = b; j.localA = localA; j.localB = localB;
        m_joints.push_back(j);
    }
    void AddBallJointToWorld(ecs::Entity a, const glm::vec3& worldPoint,
                             const glm::vec3& localA = glm::vec3(0.0f)) {
        Joint j; j.type = Joint::Type::Ball; j.a = a; j.anchor = worldPoint; j.localA = localA;
        m_joints.push_back(j);
    }

    // Hinge joint: a ball joint (pinned point) plus an axis-alignment constraint,
    // leaving one rotational DOF -- rotation about the hinge axis (a door, a lid).
    void AddHingeJoint(ecs::Entity a, ecs::Entity b,
                       const glm::vec3& localA, const glm::vec3& localB,
                       const glm::vec3& axisA, const glm::vec3& axisB) {
        Joint j; j.type = Joint::Type::Hinge; j.a = a; j.b = b;
        j.localA = localA; j.localB = localB; j.axisA = axisA; j.axisB = axisB;
        m_joints.push_back(j);
    }
    void AddHingeJointToWorld(ecs::Entity a, const glm::vec3& worldPoint,
                              const glm::vec3& localAnchor,
                              const glm::vec3& localAxis, const glm::vec3& worldAxis) {
        Joint j; j.type = Joint::Type::Hinge; j.a = a; j.anchor = worldPoint;
        j.localA = localAnchor; j.axisA = localAxis; j.axisB = worldAxis;
        m_joints.push_back(j);
    }

private:
    std::vector<CollisionEvent>             m_events;    // events from the last step
    std::unordered_map<std::uint64_t, bool> m_touching;  // pair key -> wasTrigger (persists)
    std::vector<Joint>                      m_joints;

    // Persistent scratch, reused (cleared, not reallocated) every step.
    std::vector<SolverBody>                 m_bodies;
    std::vector<ContactManifold>            m_manifolds;
    std::vector<std::pair<int, int>>        m_pairs;
    std::vector<int>                        m_planes, m_finite;
    std::vector<std::int64_t>               m_keys;
    std::unordered_map<std::int64_t, std::vector<int>> m_grid;
    std::unordered_map<std::uint64_t, bool> m_touchingNow;
    std::unordered_map<std::uint64_t, ContactCache> m_contactCache;  // warm-start impulses
};

} // namespace engine
