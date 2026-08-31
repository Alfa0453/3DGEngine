#pragma once

#include <glm/glm.hpp>

#include "engine/ecs/Entity.h"
#include "engine/ecs/Components.h"
#include "engine/physics/PhysicsComponents.h"

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
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
    // t/c describe the world collision shape. owner is the entity transform and
    // remains the body centre used by integration and positional correction.
    ecs::Transform* owner = nullptr;
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
    float     posApplied[4]{};                  // Pass-3 split-impulse: normal separation already
                                                // resolved by the position solver this step
    std::uint64_t key = 0;                      // entity-pair key (warm-start lookup)
    bool      restingContact = false;           // pair has been touching for several steps ->
                                                // restitution is suppressed (Pass-3 anti-jitter)

    // Constants precomputed ONCE per step (positions/inertia are frozen during the
    // velocity solve), so the per-iteration loop does only dot products + impulse
    // accumulation -- no mat3 rebuilds. This is the bulk of the solver cost.
    glm::vec3 rA[4]{}, rB[4]{};                  // contact offsets from each centre
    float     normalMass[4]{};                   // 1 / effective mass along the normal
    float     tangentMass[4][2]{};               // 1 / effective mass along each tangent
    float     invMassA = 0.0f, invMassB = 0.0f;  // cached inverse masses
    glm::mat3 invIA{0.0f}, invIB{0.0f};          // cached world inverse inertia
    float     friction = 0.0f;                   // combined Coulomb coefficient

    // Pass-3 block solver: when count == 2, the two normal constraints are solved as one
    // coupled 2x2 system (Box2D-style) instead of sequentially, so their impulses stay
    // balanced and no net toppling torque accumulates. blockK is the symmetric 2x2 coupled
    // effective-mass matrix [k11, k12; k12, k22]; useBlockSolver is set in PrepareManifold.
    bool      useBlockSolver = false;
    float     blockK11 = 0.0f, blockK12 = 0.0f, blockK22 = 0.0f;
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

// Optional trace capture for the editor gameplay debugger. Recording is
// disabled by default and has no query overhead when disabled.
struct DebugTrace {
    enum class Type { Ray, Sphere, Overlap };
    Type type = Type::Ray;
    glm::vec3 start{0.0f};
    glm::vec3 end{0.0f};
    float radius = 0.0f;
    bool hit = false;
    glm::vec3 hitPoint{0.0f};
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
    bool        collideConnected = true;
    bool        angularLimit = false;
    float       minAngle = -3.14159265f;
    float       maxAngle = 3.14159265f;
    glm::quat   referenceRelative{1.0f, 0.0f, 0.0f, 0.0f};
};

// Per-step + per-frame physics instrumentation (Pass-1 profiling foundation). The
// simulation fields are refreshed by each Step(); the query fields accumulate across a
// frame and are cleared by ResetQueryStats(). Everything here is transient measurement
// data -- it is never serialized. `gridRebuiltColliders` is the number of colliders the
// broad phase re-cooked and re-inserted this step; until the persistent static cache
// lands it equals the finite-collider count (i.e. "everything, every step"), which is the
// headline number later passes must drive toward zero for unchanged static scenery.
struct PhysicsStats {
    // Simulation (set by Step)
    int    colliders          = 0;   // total Collider entities
    int    staticColliders    = 0;   // no dynamic RigidBody (immovable)
    int    dynamicBodies      = 0;   // solver-driven
    int    kinematicBodies    = 0;   // script/animation-driven, collides
    int    awakeBodies        = 0;
    int    sleepingBodies     = 0;
    int    candidatePairs     = 0;   // broad-phase pairs after de-dup
    int    occupiedGridCells  = 0;
    int    gridRebuiltColliders = 0; // colliders re-inserted into the grid this step
    int    staticRebuiltThisStep = 0;// static colliders re-cooked this step (0 == fully cached)
    int    manifolds          = 0;   // contacts generated
    int    ccdBodies          = 0;   // bodies that ran a CCD sweep this step
    double stepMs             = 0.0; // wall-clock of the last Step()

    // Pass-3 solver instrumentation.
    int    velocityIterations = 0;   // velocity passes actually run this step
    int    positionIterations = 0;   // position (split-impulse) passes actually run this step
    int    contactsSolved     = 0;   // manifold points fed to the velocity solver
    float  maxPenetrationBefore = 0.0f;  // deepest manifold penetration pre-position-solve
    float  maxPenetrationAfter  = 0.0f;  // deepest residual after the position solve

    // Queries (accumulate across a frame; cleared by ResetQueryStats)
    int          raycasts        = 0;
    int          sphereCasts     = 0;
    int          overlaps        = 0;
    std::int64_t queryCandidates = 0;  // colliders considered by queries
    std::int64_t queryExactTests = 0;  // exact shape tests queries performed
};

// The physics solver. Step() integrates every RigidBody under gravity, detects
// contacts between Collider entities, and resolves them with impulses plus
// positional correction. Call it from the fixed-timestep OnFixedUpdate so the
// simulation is deterministic and frame-rate independent.
class PhysicsWorld {
public:
    glm::vec3 gravity{0.0f, -9.81f, 0.0f};
    // Substeps per fixed step (TGS-style). >1 splits each Step into smaller solves, which is
    // what keeps MISALIGNED / hand-placed stacks from slowly blowing apart (contacts stay
    // coherent within each small solve; headless tests: a 5-box tilted stack drift 3.3 -> 0.05).
    // 4 handles ~5-box tilted stacks; raise for taller. Physics is a tiny fraction of frame
    // time, so the ~linear cost is cheap. Guarded against recursion by m_inSubstep.
    int       substeps = 4;
    // Pass-3 solver: velocity and position solves use SEPARATE iteration counts.
    // solverIterations is the velocity-iteration count (name kept so existing callers and
    // serialized settings still apply); positionIterations drives the new position solve.
    int       solverIterations = 14;     // velocity (sequential-impulse) passes per step
                                         // (headless stack tests: ~6 iters/box for the current
                                         // sequential solver; 14 holds a 3-box stack solidly.
                                         // Taller stacks need the coupled/block contact solve.)
    int       positionIterations = 4;    // split-impulse position-correction passes per step
    // Split-impulse position correction (velocity-free): keeps a small penetration slop,
    // corrects a Baumgarte fraction per pass, and clamps the per-step correction so deep
    // overlaps separate smoothly instead of teleporting. Tuned in world units (engine ~1u = 1m).
    // beta must be firm enough to actually reach equilibrium each rest step -- a gentle value
    // leaves resting boxes penetrating, so the velocity solver keeps fighting gravity and the
    // contact limit-cycles (visible jitter even on a single resting box). 0.8 removes the
    // penetration (down to the slop) and lets contacts settle; the per-manifold, once-per-step
    // application (not per-point) keeps it from over-shooting.
    float     contactSlop           = 0.005f;  // penetration left uncorrected (anti-jitter)
    float     positionCorrectionBeta = 0.8f;   // fraction of remaining error resolved per pass
    float     maxPositionCorrection = 0.2f;    // max normal separation applied per step (m)

    // Broad phase: a uniform spatial hash culls the O(n^2) pair test. Disable to
    // fall back to brute-force all-pairs (identical results; used for testing).
    bool      broadPhase = true;
    float     cellSize   = 2.0f;         // grid cell edge length (world units)

    // Below this closing speed a contact does not bounce (restitution slop) --
    // stops resting stacks from micro-bouncing so they can settle and sleep.
    float     restitutionThreshold = 1.0f;   // m/s. Below this closing speed a contact does NOT
                                             // bounce. Was 0.5 -- too low: the tiny per-step
                                             // penetration recovery produced ~1 m/s closing speeds
                                             // that tripped restitution, so resting bouncy boxes
                                             // (restitution > 0) jittered forever. 1.0 matches the
                                             // Box2D default and lets them settle.

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
                       std::uint32_t layerMask = 0xFFFFFFFFu,
                       ecs::Entity ignored = ecs::kNull) const;

    // Sweep a sphere from start to end and return the earliest solid collider.
    // Trigger volumes and the optional ignored entity are skipped. This is useful
    // for camera booms and other obstruction tests that need volume, not a thin ray.
    RaycastHit SphereCast(ecs::Registry& registry,
                          const glm::vec3& start,
                          const glm::vec3& end,
                          float radius,
                          ecs::Entity ignored = ecs::kNull,
                          std::uint32_t layerMask = 0xFFFFFFFFu,
                          std::uint32_t queryLayer = 0u,
                          ecs::Entity alsoIgnored = ecs::kNull) const;

    // Collect every collider overlapping a world-space sphere (AoE queries:
    // explosions, detection). Trigger volumes are included. Filtered by layerMask.
    std::vector<ecs::Entity> OverlapSphere(ecs::Registry& registry,
                                           const glm::vec3& center, float radius,
                                           std::uint32_t layerMask = 0xFFFFFFFFu) const;

    void SetDebugTracing(bool enabled) const { m_debugTracing = enabled; }
    bool DebugTracing() const { return m_debugTracing; }
    void ClearDebugTraces() const { m_debugTraces.clear(); }
    const std::vector<DebugTrace>& DebugTraces() const { return m_debugTraces; }

    // Apply a radial impulse to dynamic bodies within 'radius' of 'center',
    // scaled by 1 - dist/radius (linear falloff). Wakes sleeping bodies. Handy for
    // explosions. strength is the peak impulse magnitude at the centre.
    void ApplyRadialImpulse(ecs::Registry& registry, const glm::vec3& center,
                            float radius, float strength,
                            std::uint32_t layerMask = 0xFFFFFFFFu) const;

    // Enter/Stay/Exit events generated by the most recent Step (both solid
    // contacts and trigger overlaps). Valid until the next Step.
    const std::vector<CollisionEvent>& Events() const { return m_events; }

    // Physics instrumentation. Stats() is the live snapshot; ResetQueryStats() zeroes the
    // per-frame query counters (call once per rendered frame, e.g. from the profiler).
    const PhysicsStats& Stats() const { return m_stats; }
    void ResetQueryStats() const {
        m_stats.raycasts = 0; m_stats.sphereCasts = 0; m_stats.overlaps = 0;
        m_stats.queryCandidates = 0; m_stats.queryExactTests = 0;
    }

    // Broad-phase reuse for scene queries. Step() builds the world colliders + spatial
    // grid every step and marks the broad phase valid; Raycast/SphereCast/OverlapSphere
    // then gather only the colliders whose grid cells overlap the query region (a provably
    // complete superset -> identical results, far fewer exact tests) instead of scanning
    // every ECS collider. The cached grid reflects the last Step's world state, which is
    // exactly what a fixed-step physics query should see. Editors that mutate colliders
    // without stepping must call InvalidateBroadphase() so queries fall back to the exact
    // full scan until the next Step (or an explicit re-sync).
    void InvalidateBroadphase() const { m_broadphaseValid = false; }
    bool BroadphaseValid() const { return m_broadphaseValid; }

    // Gather the distinct collider entities whose broad-phase grid cells overlap the world
    // AABB [mn,mx] (plus every infinite plane). A superset of colliders whose AABB overlaps
    // the region, so callers still run their own exact test and get identical results.
    // Reads the grid/bodies built by the last Step(); only meaningful when BroadphaseValid().
    // Returns false when the region spans too many cells to be worth gathering (e.g. an
    // unbounded ray) -- the caller then falls back to the exact full scan. Used by the scene
    // queries and by CharacterController / CCD candidate lookups.
    bool GatherBroadphaseCandidates(const glm::vec3& mn, const glm::vec3& mx,
                                    std::vector<ecs::Entity>& out) const;

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
    void RemoveJointsFor(ecs::Entity entity) {
        m_joints.erase(std::remove_if(m_joints.begin(), m_joints.end(),
            [&](const Joint& joint) { return joint.a == entity || joint.b == entity; }),
            m_joints.end());
    }

    // Ball (point-to-point) joint: pins a point on A to a point on B (localA/localB
    // are offsets from each body's centre), leaving all 3 rotational DOF free.
    void AddBallJoint(ecs::Entity a, ecs::Entity b,
                      const glm::vec3& localA = glm::vec3(0.0f),
                      const glm::vec3& localB = glm::vec3(0.0f),
                      bool collideConnected = true,
                      bool angularLimit = false,
                      float maxAngle = 3.14159265f,
                      const glm::quat& referenceRelative = glm::quat(1.0f,0.0f,0.0f,0.0f)) {
        Joint j; j.type = Joint::Type::Ball; j.a = a; j.b = b; j.localA = localA; j.localB = localB;
        j.collideConnected = collideConnected;
        j.angularLimit = angularLimit; j.maxAngle = maxAngle;
        j.referenceRelative = referenceRelative;
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
                       const glm::vec3& axisA, const glm::vec3& axisB,
                       bool collideConnected = true,
                       bool angularLimit = false,
                       float minAngle = -3.14159265f,
                       float maxAngle = 3.14159265f,
                       const glm::quat& referenceRelative = glm::quat(1.0f,0.0f,0.0f,0.0f)) {
        Joint j; j.type = Joint::Type::Hinge; j.a = a; j.b = b;
        j.localA = localA; j.localB = localB; j.axisA = axisA; j.axisB = axisB;
        j.collideConnected = collideConnected; j.angularLimit = angularLimit;
        j.minAngle = minAngle; j.maxAngle = maxAngle;
        j.referenceRelative = referenceRelative;
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
    void RecordDebugTrace(const DebugTrace& trace) const;
    mutable PhysicsStats                    m_stats;     // transient instrumentation (never serialized)
    mutable bool m_broadphaseValid = false;              // grid reflects current world (set by Step)
    mutable std::unordered_set<int> m_broadphaseScratch; // dedup buffer for candidate gather

    // Persistent per-static-collider world-shape cache. A static collider (no dynamic body)
    // is expensive to re-cook via BuildWorldCollider every step; here we keep the last owner
    // transform + local collider it was cooked from and the resulting world shape, and skip
    // the re-cook while it is unchanged. Produces byte-identical world colliders, so the
    // broad-phase pairs, narrow phase and solver are unaffected (same simulation) -- it only
    // removes redundant per-step cooking work. Purely transient; never serialized.
    struct StaticColliderCache {
        bool           valid = false;   // has been cooked at least once
        bool           seen  = false;   // present this step (unseen entries are pruned)
        ecs::Transform lastOwner;       // owner transform it was cooked from
        ecs::Collider  lastLocal;       // local collider it was cooked from
        ecs::Transform worldTransform;  // cached BuildWorldCollider().transform
        ecs::Collider  worldCollider;   // cached BuildWorldCollider().collider
    };
    std::unordered_map<ecs::Entity, StaticColliderCache> m_staticCache;
    std::vector<CollisionEvent>             m_events;    // events from the last step
    std::unordered_map<std::uint64_t, bool> m_touching;  // pair key -> wasTrigger (persists)
    mutable bool m_debugTracing = false;
    mutable std::vector<DebugTrace> m_debugTraces;
    std::vector<Joint>                      m_joints;

    // Persistent scratch, reused (cleared, not reallocated) every step.
    std::vector<SolverBody>                 m_bodies;
    std::vector<ecs::Transform>             m_worldColliderTransforms;
    std::vector<ecs::Collider>              m_worldColliders;
    std::vector<ContactManifold>            m_manifolds;
    std::vector<std::pair<int, int>>        m_pairs;
    std::vector<int>                        m_planes, m_finite;
    std::vector<std::int64_t>               m_keys;
    std::unordered_map<std::int64_t, std::vector<int>> m_grid;
    std::unordered_map<std::uint64_t, bool> m_touchingNow;
    std::unordered_map<std::uint64_t, ContactCache> m_contactCache;  // warm-start impulses
    std::unordered_map<std::uint64_t, int> m_contactAge;  // pair key -> consecutive-ish contact
                                                          // frames; drives restitution suppression
    bool m_inSubstep = false;                             // recursion guard for substepping
    std::unordered_map<std::uint64_t, int> m_manifoldOf;
    std::unordered_map<std::uint64_t, int> m_eventOf;
};

} // namespace engine
