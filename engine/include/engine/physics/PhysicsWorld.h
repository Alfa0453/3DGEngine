#pragma once

#include <glm/glm.hpp>

#include "engine/ecs/Entity.h"
#include "engine/ecs/Components.h"
#include "engine/physics/PhysicsComponents.h"
#include "engine/physics/FlatU64Map.h"

#include <algorithm>
#include <array>
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
    float     friction = 0.0f;                   // combined Coulomb coefficient (legacy / block box)
    float     staticFriction  = 0.0f;            // Pass-4: combined static coefficient (stick limit)
    float     dynamicFriction = 0.0f;            // Pass-4: combined dynamic coefficient (slip limit)

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

    // Hinge motor (Pass-3): drive rotation about the hinge axis toward a target angular speed,
    // limited by a maximum torque (the per-step impulse is clamped to maxTorque * dt).
    bool        motorEnabled = false;
    float       motorTargetVelocity = 0.0f;   // rad/s about the hinge axis (B relative to A)
    float       motorMaxTorque = 0.0f;        // N*m

    // Optional break threshold (Pass-3). When the accumulated constraint impulse over a step
    // exceeds breakImpulse the joint is flagged broken (removed safely after the solve). 0 = off.
    float       breakImpulse = 0.0f;
    bool        broken = false;

    // --- Transient solver state: accumulated impulses for warm starting. Never serialized;
    //     reset when a joint is (re)created. Persisting them across frames is what makes
    //     jointed chains settle without stretching. ---
    glm::vec3   pointImpulse{0.0f};       // point-to-point (ball / hinge anchor) accumulated impulse
    float       distanceImpulse = 0.0f;   // distance-joint scalar accumulated impulse
    glm::vec2   axisImpulse{0.0f};        // hinge axis-alignment impulses (two perpendicular axes)
    float       limitImpulse = 0.0f;      // hinge angular-limit accumulated impulse
    float       motorImpulse = 0.0f;      // hinge motor accumulated impulse
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
    int    ccdBodies          = 0;   // CCD-enabled bodies present this step
    int    ccdSweeps          = 0;   // Pass-5: CCD bodies that actually ran the expensive sweep
    int    ccdSkipped         = 0;   // Pass-5: CCD bodies early-rejected (too slow to tunnel)
    int    quarantinedBodies  = 0;   // Pass-5: bodies whose non-finite state was reset this step
    double stepMs             = 0.0; // wall-clock of the last Step()

    // Pass-3 solver instrumentation.
    int    velocityIterations = 0;   // velocity passes actually run this step
    int    positionIterations = 0;   // position (split-impulse) passes actually run this step
    int    contactsSolved     = 0;   // manifold points fed to the velocity solver
    float  maxPenetrationBefore = 0.0f;  // deepest manifold penetration pre-position-solve
    float  maxPenetrationAfter  = 0.0f;  // deepest residual after the position solve

    // Islands (Pass-3).
    int    islandCount        = 0;   // total simulation islands this step
    int    awakeIslands       = 0;   // islands with at least one awake body (solved)
    int    sleepingIslands    = 0;   // fully-asleep islands (skipped)
    int    largestIslandBodies = 0;  // body count of the biggest island

    // Joints (Pass-3).
    int    distanceJoints = 0;
    int    ballJoints     = 0;
    int    hingeJoints    = 0;
    int    motorsActive   = 0;
    int    limitsActive   = 0;
    int    brokenJoints   = 0;

    // Queries (accumulate across a frame; cleared by ResetQueryStats)
    int          raycasts        = 0;
    int          sphereCasts     = 0;
    int          capsuleCasts    = 0;   // Pass-4 shape cast
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

    // Pass-5 numerical safety (Phase 69). Each step, quarantine any body whose state has gone
    // non-finite: zero its velocities and reset a NaN position/rotation, so a single poisoned body
    // (a divide-by-zero in a script, a bad impulse) cannot spread NaN through contacts/islands and
    // corrupt the whole solve. Cheap (one view pass) and on by default; stats.quarantinedBodies
    // reports how many were caught. Turn off only if a profiler shows it matters.
    bool      sanitizeState = true;

    // Pass-5 CCD budgeting (Phases 30-31). CCD is opt-in per body (RigidBody::ccd), but a body that
    // moves less than this fraction of its own sweep radius in one step CANNOT tunnel through solid
    // geometry -- the discrete narrow phase already resolves it -- so the expensive continuous sweep
    // is skipped. This bounds CCD cost by the number of genuinely fast movers rather than the number
    // of CCD-flagged bodies, WITHOUT ever skipping a body that could tunnel (unlike a hard per-step
    // cap). 0 forces every CCD body to sweep (legacy behaviour). Lower = safer (sweeps more often).
    float     ccdMotionThreshold = 0.5f;

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

    // Pass-4: sweep an upright capsule (segment 2*halfHeight along `up`, given radius) from
    // start to end and return the earliest solid hit. Built on the accelerated SphereCast
    // (bottom-cap / centre / top-cap sphere paths), so it inherits broad-phase acceleration and
    // shape/mesh handling. This is the canonical sweep the CharacterController moves with.
    RaycastHit CapsuleCast(ecs::Registry& registry,
                           const glm::vec3& start, const glm::vec3& end,
                           float radius, float halfHeight,
                           const glm::vec3& up = glm::vec3(0.0f, 1.0f, 0.0f),
                           ecs::Entity ignored = ecs::kNull,
                           std::uint32_t layerMask = 0xFFFFFFFFu,
                           std::uint32_t queryLayer = 0u,
                           ecs::Entity alsoIgnored = ecs::kNull) const;

    // Collect every collider overlapping an upright capsule (segment + radius). Trigger volumes
    // included; filtered by layerMask. Used for character initial-overlap recovery.
    std::vector<ecs::Entity> OverlapCapsule(ecs::Registry& registry,
                                            const glm::vec3& center, float radius, float halfHeight,
                                            const glm::vec3& up = glm::vec3(0.0f, 1.0f, 0.0f),
                                            std::uint32_t layerMask = 0xFFFFFFFFu) const;

    // Pass-4 gameplay query suite. RaycastAny early-outs at the first solid hit (AI line-of-sight
    // / occlusion). RaycastAll returns every hit sorted nearest-first (respecting filters).
    bool RaycastAny(ecs::Registry& registry, const Ray& ray, float maxDistance = 1.0e30f,
                    std::uint32_t layerMask = 0xFFFFFFFFu, ecs::Entity ignored = ecs::kNull) const;
    std::vector<RaycastHit> RaycastAll(ecs::Registry& registry, const Ray& ray,
                                       float maxDistance = 1.0e30f,
                                       std::uint32_t layerMask = 0xFFFFFFFFu) const;

    // Sweep an axis-aligned-in-`rotation` box. NOTE: currently a CONSERVATIVE bounding-sphere
    // sweep (over-reports near corners) -- an exact OBB sweep is a later refinement; the sphere
    // sweep is accelerated and safe for "is the path clear" gameplay checks.
    RaycastHit BoxCast(ecs::Registry& registry, const glm::vec3& start, const glm::vec3& end,
                       const glm::vec3& halfExtents,
                       const glm::quat& rotation = glm::quat(1, 0, 0, 0),
                       ecs::Entity ignored = ecs::kNull,
                       std::uint32_t layerMask = 0xFFFFFFFFu) const;

    // Collider entities overlapping a world box (OBB). Broad-phase accelerated with an exact
    // OBB-vs-collider AABB overlap refine.
    std::vector<ecs::Entity> OverlapBox(ecs::Registry& registry, const glm::vec3& center,
                                        const glm::vec3& halfExtents,
                                        const glm::quat& rotation = glm::quat(1, 0, 0, 0),
                                        std::uint32_t layerMask = 0xFFFFFFFFu) const;

    // Penetration of a query collider (at queryTransform) against another entity's collider,
    // via the full narrow phase (all shape pairs, incl. GJK/EPA + mesh). outNormal points FROM
    // the other collider TOWARD the query -- i.e. move the query by outNormal*outDepth to
    // separate. Returns false when not overlapping.
    bool ComputePenetration(ecs::Registry& registry, const ecs::Transform& queryTransform,
                            const ecs::Collider& queryCollider, ecs::Entity other,
                            glm::vec3& outNormal, float& outDepth) const;

    // Closest point on an entity's collider to a world point. Exact for sphere/box/capsule/plane;
    // approximate (bounding OBB) for cylinder/cone/hull/mesh. Returns the point itself when it is
    // inside a solid convex shape.
    glm::vec3 ClosestPoint(ecs::Registry& registry, const glm::vec3& point,
                           ecs::Entity entity) const;

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

    // Pass-5 determinism validation (Phases 46-48). Hash the stable dynamic-body state at the end
    // of a fixed step so two runs of the same scene+inputs can be compared frame-by-frame. Bodies
    // are gathered and sorted by entity id first, so the hash never depends on ECS iteration order
    // (Phase 44). Floats are quantized to 1e-4 units so the comparison tolerates only true noise.
    // A development tool (allocates a small scratch vector) -- not for the hot path. Returns an
    // FNV-1a digest of (entity, position, orientation, linear vel, angular vel, sleeping).
    std::uint64_t StateHash(ecs::Registry& registry) const;

    // Pass-5 live inspection (Phases 61/63). These read the LAST step's cached solver state, valid
    // until the next Step(). Editor-tool helpers, not for the hot path.
    //
    // Simulation island a dynamic entity belongs to this step, or -1 if it has no dynamic body /
    // isn't in the world. Two entities sharing an id are in the same constraint network.
    int IslandOfEntity(ecs::Entity e) const {
        const int* p = m_entityIsland.find(static_cast<std::uint64_t>(e));
        return p ? *p : -1;
    }
    // One resolved contact touching the queried entity: the other collider, the world contact
    // point/normal, penetration, and the impulses the solver applied (impact force ~ normalImpulse).
    struct ContactInfo {
        ecs::Entity other = ecs::kNull;
        glm::vec3   point{0.0f};
        glm::vec3   normal{0.0f};
        float       penetration = 0.0f;
        float       normalImpulse = 0.0f;
        float       frictionImpulse = 0.0f;
    };
    std::vector<ContactInfo> ContactsForEntity(ecs::Entity e) const;

    // Pass-5 collision matrix (Phases 35-36). A global, symmetric layer-vs-layer interaction table
    // that gates the broad-phase pair filter on top of the existing per-collider layer/mask. It is
    // INACTIVE by default, so an untouched world filters exactly as before (byte-identical). The
    // editor compiles its authored grid into these per-layer masks and calls SetLayerMatrixActive.
    // m_layerCollisionMask[i] is the set of layer bits that layer i is allowed to collide with;
    // symmetry is the caller's responsibility (SetLayerCollides keeps both halves in sync).
    void SetLayerMatrixActive(bool active) { m_layerMatrixActive = active; }
    bool LayerMatrixActive() const { return m_layerMatrixActive; }
    void ResetLayerMatrix() { m_layerCollisionMask.fill(0xFFFFFFFFu); m_layerMatrixActive = false; }
    // Enable/disable collision between two layer indices (0..31); keeps the table symmetric.
    void SetLayerCollides(int a, int b, bool enabled) {
        if (a < 0 || a > 31 || b < 0 || b > 31) return;
        const std::uint32_t bitB = 1u << b, bitA = 1u << a;
        if (enabled) { m_layerCollisionMask[a] |= bitB; m_layerCollisionMask[b] |= bitA; }
        else         { m_layerCollisionMask[a] &= ~bitB; m_layerCollisionMask[b] &= ~bitA; }
    }
    bool LayerCollides(int a, int b) const {
        if (a < 0 || a > 31 || b < 0 || b > 31) return true;
        return (m_layerCollisionMask[a] & (1u << b)) != 0u;
    }
    std::uint32_t LayerCollisionMask(int layer) const {
        return (layer >= 0 && layer <= 31) ? m_layerCollisionMask[layer] : 0xFFFFFFFFu;
    }
    void SetLayerCollisionMask(int layer, std::uint32_t mask) {
        if (layer >= 0 && layer <= 31) m_layerCollisionMask[layer] = mask;
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

    // Access the joint most recently added, so authoring code can set fields not covered by the
    // Add* signatures (hinge motor parameters, the break-impulse threshold). Valid only right
    // after an Add*Joint call. Returns a reference into the joint list.
    Joint& LastJoint() { return m_joints.back(); }
    bool   HasJoints() const { return !m_joints.empty(); }

private:
    void RecordDebugTrace(const DebugTrace& trace) const;
    mutable PhysicsStats                    m_stats;     // transient instrumentation (never serialized)
    mutable bool m_broadphaseValid = false;              // grid reflects current world (set by Step)
    // Pass-5: generation-stamp dedup for candidate gather (replaces an unordered_set whose
    // clear()+insert() allocated one node per body every query). m_seenGen[bodyIndex] == m_queryGen
    // means "already added this query"; a bump of m_queryGen is an O(1) clear.
    mutable std::vector<std::uint32_t> m_seenGen;
    mutable std::uint32_t m_queryGen = 0;

    // Collision matrix (Pass-5). Per-layer "collides-with" masks; default all-ones (everything
    // collides) and inactive, so the filter is unchanged until the editor authors a matrix.
    static std::array<std::uint32_t, 32> AllLayersMask() { std::array<std::uint32_t, 32> a; a.fill(0xFFFFFFFFu); return a; }
    std::array<std::uint32_t, 32> m_layerCollisionMask = AllLayersMask();
    bool m_layerMatrixActive = false;
    // OR of the collides-with masks for every layer bit set in `layer` (usually one bit).
    std::uint32_t MatrixMaskOf(std::uint32_t layer) const {
        std::uint32_t m = 0u;
        for (int i = 0; i < 32 && layer; ++i)
            if (layer & (1u << i)) { m |= m_layerCollisionMask[i]; layer &= ~(1u << i); }
        return m;
    }
    // Pass-5: reused candidate buffer for the base scene queries (Raycast / SphereCast /
    // OverlapSphere), so a heavy per-frame query load allocates nothing. SAFE because the engine's
    // queries are already single-threaded (they share m_broadphaseScratch) and no query holds this
    // buffer across a call to another query -- the composite casts (CapsuleCast/BoxCast/OverlapBox/
    // RaycastAll) invoke the base queries strictly sequentially. A future parallel query API must
    // give each worker its own scratch (see the batch-query note).
    mutable std::vector<ecs::Entity> m_queryScratch;

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
    FlatU64Map<bool> m_touching;  // pair key -> wasTrigger (persists; Pass-5: alloc-free clear)
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
    FlatU64Map<bool> m_touchingNow;
    FlatU64Map<ContactCache> m_contactCache;  // warm-start impulses (Pass-5: alloc-free clear)
    FlatU64Map<int> m_contactAge;  // pair key -> consecutive-ish contact (Pass-5: alloc-free)
                                                          // frames; drives restitution suppression
    std::vector<std::uint64_t> m_ageScratch;             // reused prune buffer (no per-step alloc)
    std::vector<ecs::Entity>   m_ccdCandidates;          // reused CCD broad-phase gather buffer
    // Substep event-aggregation scratch (reused across frames; alloc-free after warmup).
    FlatU64Map<bool> m_touchingBeforeFrame;
    FlatU64Map<CollisionEvent> m_finalContactEvents;
    bool m_inSubstep = false;                             // recursion guard for substepping

    // Simulation-island scratch (rebuilt every step; never serialized). Union-find groups the
    // dynamic bodies connected by contacts/joints into islands so each solves and sleeps
    // independently; static/kinematic bodies are boundaries and never merge two islands.
    std::vector<int> m_ufParent;                          // union-find parent per body index
    std::vector<int> m_ufRank;                            // union-find rank
    std::vector<std::vector<int>> m_islandManifolds;      // manifold indices per island
    std::vector<std::vector<int>> m_islandJoints;         // joint indices per island
    std::vector<std::vector<int>> m_islandBodies;         // dynamic body indices per island
    std::unordered_map<int, int> m_rootToIsland;          // union-find root -> island slot
    FlatU64Map<int> m_entityIsland;                        // Pass-5: entity -> island id (inspection)
    // Pass-5: reused island-build scratch, so a settled scene allocates nothing per step.
    FlatU64Map<int>   m_entToIdx;                          // entity id -> body index (joints)
    FlatU64Map<char>  m_noCollisionJoints;                // pair key -> present (collideConnected=false)
    std::vector<char> m_islandAwake;                       // per-island awake flag
    FlatU64Map<int> m_manifoldOf;   // Pass-5: alloc-free clear (was std::unordered_map)
    FlatU64Map<int> m_eventOf;
};

} // namespace engine
