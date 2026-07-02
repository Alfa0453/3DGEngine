#include "engine/physics/PhysicsWorld.h"

#include "engine/physics/PhysicsComponents.h"
#include "engine/ecs/Registry.h"
#include "engine/ecs/Components.h"   // Transform

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>    // mat3_cast

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

using engine::ecs::Entity;
using engine::ecs::Transform;
using engine::ecs::RigidBody;
using engine::ecs::Collider;
using engine::ecs::ColliderShape;

namespace engine {
namespace {

// Gathered per step (defined in the header so PhysicsWorld can cache the list).
using Body = engine::SolverBody;

// A body that neither moves nor can be moved this step (static or asleep).
bool Inactive(const Body& b) { return !b.rb || b.rb->sleeping; }

// Order-independent 64-bit key for an entity pair (stable across steps).
std::uint64_t PairKey(Entity a, Entity b) {
    if (a > b) std::swap(a, b);
    return (std::uint64_t(a) << 32) | std::uint64_t(b);
}

struct Contact {
    bool      hit = false;
    glm::vec3 normal{0.0f};   // points from A toward B
    float     penetration = 0.0f;
};

// An oriented bounding box in world space (built from Transform + Collider).
struct OBB {
    glm::vec3 center;
    glm::vec3 axis[3];   // unit, right-handed
    glm::vec3 ext;       // half-extents
};

// Sleeping bodies act as immovable (invMass 0, zero velocity) until woken.
float invMassOf(const Body& b) {
    if (!b.rb || b.rb->sleeping) return 0.0f;
    return b.rb->invMass;
}
glm::vec3 velOf(const Body& b) {
    if (!b.rb || b.rb->sleeping) return glm::vec3(0.0f);
    return b.rb->velocity;
}
void Wake(Body& b) {
    if (b.rb && b.rb->sleeping) { b.rb->sleeping = false; b.rb->sleepTimer = 0.0f; }
}

// Shape ordering so each pair is tested in one canonical direction. Sphere < Box
// < Plane; the higher-priority shape becomes B, and the contact normal points
// from A toward B.
int priority(ColliderShape s) {
    switch (s) {
        case ColliderShape::Sphere: return 0;
        case ColliderShape::Box:    return 1;
        case ColliderShape::Plane:  return 2;
    }
    return 0;
}

OBB MakeOBB(const Body& b) {
    OBB o;
    o.center = b.t->position;
    const glm::mat3 R = glm::mat3_cast(b.t->rotation);
    o.axis[0] = R[0]; o.axis[1] = R[1]; o.axis[2] = R[2];
    o.ext = b.c->halfExtents;
    return o;
}

// ---- narrow-phase contact generators (all return the normal from A -> B) ----

Contact SphereSphere(const glm::vec3& pa, float ra, const glm::vec3& pb, float rb) {
    Contact ct;
    const glm::vec3 d = pb - pa;
    const float dist2 = glm::dot(d, d);
    const float r = ra + rb;
    if (dist2 >= r * r) return ct;
    const float dist = std::sqrt(dist2);
    ct.hit = true;
    ct.normal = (dist > 1e-6f) ? d / dist : glm::vec3(0.0f, 1.0f, 0.0f);
    ct.penetration = r - dist;
    return ct;
}

// A = sphere, B = plane. Normal points from the sphere toward the plane.
Contact SpherePlane(const glm::vec3& pa, float ra, const glm::vec3& n, float off) {
    Contact ct;
    const float s = glm::dot(n, pa) - off;
    const float pen = ra - s;
    if (pen <= 0.0f) return ct;
    ct.hit = true;
    ct.normal = -n;
    ct.penetration = pen;
    return ct;
}

// A = sphere, B = box. Closest point on the OBB to the sphere centre.
Contact SphereBox(const glm::vec3& sc, float r, const OBB& box) {
    Contact ct;
    const glm::vec3 d = sc - box.center;
    glm::vec3 local(glm::dot(d, box.axis[0]), glm::dot(d, box.axis[1]), glm::dot(d, box.axis[2]));
    glm::vec3 clamped = glm::clamp(local, -box.ext, box.ext);
    const bool inside = (clamped == local);

    if (inside) {
        // Centre is within the box: push out through the least-penetrated face.
        int best = 0; float bestFace = box.ext[0] - std::fabs(local[0]);
        for (int i = 1; i < 3; ++i) {
            const float f = box.ext[i] - std::fabs(local[i]);
            if (f < bestFace) { bestFace = f; best = i; }
        }
        const float sign = (local[best] >= 0.0f) ? 1.0f : -1.0f;
        const glm::vec3 boxToSphere = sign * box.axis[best];   // outward face normal
        ct.hit = true;
        ct.normal = -boxToSphere;               // A(sphere) -> B(box)
        ct.penetration = r + bestFace;
        return ct;
    }

    const glm::vec3 closest = box.center
        + clamped[0] * box.axis[0] + clamped[1] * box.axis[1] + clamped[2] * box.axis[2];
    const glm::vec3 delta = sc - closest;       // box surface -> sphere centre
    const float dist2 = glm::dot(delta, delta);
    if (dist2 >= r * r) return ct;
    const float dist = std::sqrt(dist2);
    ct.hit = true;
    ct.normal = (dist > 1e-6f) ? -(delta / dist) : glm::vec3(0.0f, -1.0f, 0.0f);  // A -> B
    ct.penetration = r - dist;
    return ct;
}

// A = box, B = plane. Deepest of the 8 corners below the plane sets penetration.
Contact BoxPlane(const OBB& box, const glm::vec3& n, float off) {
    Contact ct;
    float deepest = 0.0f;
    for (int sx = -1; sx <= 1; sx += 2)
    for (int sy = -1; sy <= 1; sy += 2)
    for (int sz = -1; sz <= 1; sz += 2) {
        const glm::vec3 corner = box.center
            + float(sx) * box.ext[0] * box.axis[0]
            + float(sy) * box.ext[1] * box.axis[1]
            + float(sz) * box.ext[2] * box.axis[2];
        const float pen = off - glm::dot(n, corner);   // >0 means below the plane
        deepest = std::max(deepest, pen);
    }
    if (deepest <= 0.0f) return ct;
    ct.hit = true;
    ct.normal = -n;                 // A(box) -> B(plane)
    ct.penetration = deepest;
    return ct;
}

// A = box, B = box. Separating Axis Theorem over 15 candidate axes; the axis of
// least overlap becomes the contact normal (oriented A -> B).
Contact BoxBox(const OBB& A, const OBB& B) {
    Contact ct;
    const glm::vec3 t = B.center - A.center;

    std::array<glm::vec3, 15> axes;
    int n = 0;
    for (int i = 0; i < 3; ++i) axes[n++] = A.axis[i];   // face normals of A (tested first)
    for (int i = 0; i < 3; ++i) axes[n++] = B.axis[i];   // face normals of B
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            glm::vec3 c = glm::cross(A.axis[i], B.axis[j]);
            axes[n++] = c;   // edge-edge axes (may be near-zero -> skipped below)
        }

    float minOverlap = std::numeric_limits<float>::max();
    glm::vec3 bestAxis(0.0f);
    for (int k = 0; k < 15; ++k) {
        glm::vec3 L = axes[k];
        const float len2 = glm::dot(L, L);
        if (len2 < 1e-6f) continue;          // parallel edges: degenerate axis
        L /= std::sqrt(len2);
        float rA = 0.0f, rB = 0.0f;
        for (int i = 0; i < 3; ++i) rA += A.ext[i] * std::fabs(glm::dot(A.axis[i], L));
        for (int i = 0; i < 3; ++i) rB += B.ext[i] * std::fabs(glm::dot(B.axis[i], L));
        const float dist = std::fabs(glm::dot(t, L));
        const float overlap = rA + rB - dist;
        if (overlap < 0.0f) return ct;       // separating axis found: no contact
        if (overlap < minOverlap - 1e-4f) {  // bias favours earlier (face) axes on ties
            minOverlap = overlap;
            bestAxis = (glm::dot(t, L) < 0.0f) ? -L : L;   // orient A -> B
        }
    }
    ct.hit = true;
    ct.normal = bestAxis;
    ct.penetration = minOverlap;
    return ct;
}

// Dispatch by the (canonically ordered) shape pair.
Contact Detect(const Body& A, const Body& B) {
    const auto sa = A.c->shape, sb = B.c->shape;
    if (sa == ColliderShape::Sphere && sb == ColliderShape::Sphere)
        return SphereSphere(A.t->position, A.c->radius, B.t->position, B.c->radius);
    if (sa == ColliderShape::Sphere && sb == ColliderShape::Box)
        return SphereBox(A.t->position, A.c->radius, MakeOBB(B));
    if (sa == ColliderShape::Sphere && sb == ColliderShape::Plane)
        return SpherePlane(A.t->position, A.c->radius, B.c->planeNormal, B.c->planeOffset);
    if (sa == ColliderShape::Box && sb == ColliderShape::Box)
        return BoxBox(MakeOBB(A), MakeOBB(B));
    if (sa == ColliderShape::Box && sb == ColliderShape::Plane)
        return BoxPlane(MakeOBB(A), B.c->planeNormal, B.c->planeOffset);
    return Contact{};
}

void ResolveVelocity(Body& A, Body& B, const glm::vec3& n,
                     float restitutionThreshold, float wakeSpeed) {
    // A fast-closing contact wakes either sleeper before we resolve it.
    {
        const glm::vec3 rv0 = velOf(B) - velOf(A);
        if (glm::dot(rv0, n) < -wakeSpeed) { Wake(A); Wake(B); }
    }

    const float imA = invMassOf(A), imB = invMassOf(B);
    const float imSum = imA + imB;
    if (imSum <= 0.0f) return;   // both static or asleep

    const glm::vec3 relVel = velOf(B) - velOf(A);
    const float vn = glm::dot(relVel, n);
    if (vn >= 0.0f) return;      // separating

    // Restitution slop: below a small closing speed, don't bounce.
    const float e = (-vn > restitutionThreshold)
                    ? std::min(A.c->restitution, B.c->restitution) : 0.0f;
    const float j = -(1.0f + e) * vn / imSum;
    const glm::vec3 impulse = j * n;
    if (A.rb) A.rb->velocity -= impulse * imA;
    if (B.rb) B.rb->velocity += impulse * imB;

    // Coulomb friction along the tangent.
    glm::vec3 rv = velOf(B) - velOf(A);
    glm::vec3 tangent = rv - glm::dot(rv, n) * n;
    if (glm::dot(tangent, tangent) > 1e-8f) {
        tangent = glm::normalize(tangent);
        float jt = -glm::dot(rv, tangent) / imSum;
        const float mu = std::sqrt(A.c->friction * B.c->friction);
        jt = std::clamp(jt, -j * mu, j * mu);
        const glm::vec3 fr = jt * tangent;
        if (A.rb) A.rb->velocity -= fr * imA;
        if (B.rb) B.rb->velocity += fr * imB;
    }
}

// One Baumgarte positional correction from the cached penetration.
void CorrectPosition(Body& A, Body& B, const glm::vec3& n, float penetration) {
    const float imA = invMassOf(A), imB = invMassOf(B);
    const float imSum = imA + imB;
    if (imSum <= 0.0f) return;
    const float percent = 0.8f, slop = 0.001f;
    const glm::vec3 corr = (std::max(penetration - slop, 0.0f) / imSum) * percent * n;
    if (A.rb) A.t->position -= corr * imA;
    if (B.rb) B.t->position += corr * imB;
}

} // namespace

namespace {

struct AABB { glm::vec3 mn, mx; };

// World-space AABB of a finite collider (sphere or box). Planes are infinite and
// are handled separately (never inserted into the grid).
AABB ComputeAABB(const Body& b) {
    if (b.c->shape == ColliderShape::Sphere) {
        const glm::vec3 r(b.c->radius);
        return { b.t->position - r, b.t->position + r };
    }
    // Box: project the oriented half-extents onto the world axes.
    const OBB o = MakeOBB(b);
    glm::vec3 h(0.0f);
    for (int k = 0; k < 3; ++k)
        h[k] = o.ext[0] * std::fabs(o.axis[0][k])
             + o.ext[1] * std::fabs(o.axis[1][k])
             + o.ext[2] * std::fabs(o.axis[2][k]);
    return { o.center - h, o.center + h };
}

// Pack integer cell coordinates into one 64-bit key (21 bits each, signed range
// ~ +/-1e6 cells) so distinct cells never alias.
std::int64_t CellKey(int ix, int iy, int iz) {
    const std::int64_t X = std::int64_t(ix) & 0x1FFFFF;
    const std::int64_t Y = std::int64_t(iy) & 0x1FFFFF;
    const std::int64_t Z = std::int64_t(iz) & 0x1FFFFF;
    return (X << 42) | (Y << 21) | Z;
}

} // namespace

namespace {

// Apply a spring joint's Hooke + damping force to its bodies' force accumulators
// (called before integration). Spring bodies are kept awake so they stay live.
void ApplySpring(ecs::Registry& reg, const Joint& j) {
    if (!reg.Has<Transform>(j.a)) return;
    const glm::vec3 pa = reg.Get<Transform>(j.a).position;
    RigidBody* ra = reg.TryGet<RigidBody>(j.a);
    glm::vec3 pb; RigidBody* rb = nullptr;
    if (j.b == ecs::kNull) { pb = j.anchor; }
    else { if (!reg.Has<Transform>(j.b)) return; pb = reg.Get<Transform>(j.b).position; rb = reg.TryGet<RigidBody>(j.b); }

    const glm::vec3 d = pb - pa;
    const float len = glm::length(d);
    if (len < 1e-6f) return;
    const glm::vec3 n = d / len;
    const glm::vec3 va = ra ? ra->velocity : glm::vec3(0.0f);
    const glm::vec3 vb = rb ? rb->velocity : glm::vec3(0.0f);
    const float vrel = glm::dot(vb - va, n);
    const float fMag = j.stiffness * (len - j.restLength) + j.damping * vrel;
    const glm::vec3 fOnA = fMag * n;   // toward b when stretched
    if (ra && ra->invMass > 0.0f) { ra->accumForce += fOnA; ra->sleeping = false; ra->sleepTimer = 0.0f; }
    if (rb && rb->invMass > 0.0f) { rb->accumForce -= fOnA; rb->sleeping = false; rb->sleepTimer = 0.0f; }
}

// Solve one rigid distance joint (velocity impulse + Baumgarte position pull).
// Called inside the solver iteration loop. Ropes only resist stretching.
void SolveDistance(ecs::Registry& reg, const Joint& j) {
    if (!reg.Has<Transform>(j.a)) return;
    Transform& ta = reg.Get<Transform>(j.a);
    RigidBody* ra = reg.TryGet<RigidBody>(j.a);
    Transform* tb = nullptr; RigidBody* rb = nullptr; glm::vec3 pb;
    if (j.b == ecs::kNull) { pb = j.anchor; }
    else { if (!reg.Has<Transform>(j.b)) return; tb = &reg.Get<Transform>(j.b); pb = tb->position; rb = reg.TryGet<RigidBody>(j.b); }

    const glm::vec3 d = pb - ta.position;
    const float len = glm::length(d);
    if (len < 1e-6f) return;
    const glm::vec3 n = d / len;
    const float C = len - j.restLength;
    if (j.rope && C < 0.0f) return;      // slack: nothing to do

    // Wake bodies if the constraint is meaningfully violated.
    if (std::fabs(C) > 0.005f) {
        if (ra && ra->sleeping) { ra->sleeping = false; ra->sleepTimer = 0.0f; }
        if (rb && rb->sleeping) { rb->sleeping = false; rb->sleepTimer = 0.0f; }
    }
    const float imA = (ra && !ra->sleeping) ? ra->invMass : 0.0f;
    const float imB = (rb && !rb->sleeping) ? rb->invMass : 0.0f;
    const float imSum = imA + imB;
    if (imSum <= 0.0f) return;

    const glm::vec3 va = ra ? ra->velocity : glm::vec3(0.0f);
    const glm::vec3 vb = rb ? rb->velocity : glm::vec3(0.0f);
    const float vrel = glm::dot(vb - va, n);
    const glm::vec3 P = (-vrel / imSum) * n;
    if (ra) ra->velocity -= P * imA;
    if (rb) rb->velocity += P * imB;

    const float beta = 0.2f;             // position stabilisation factor
    const glm::vec3 corr = n * (C * beta / imSum);
    if (ra)          ta.position   += corr * imA;   // pull a toward b when C>0
    if (rb && tb)    tb->position  -= corr * imB;
}

} // namespace

namespace {

// --- swept-sphere (moving A->B, radius r) vs static shapes. Return earliest
//     time-of-impact in [0,1], or 1 (no hit) with outN the surface normal. -----

float SweptSpherePlane(const glm::vec3& A, const glm::vec3& B, float r,
                       const glm::vec3& n, float off, glm::vec3& outN) {
    const float dA = glm::dot(n, A) - off;
    const float dB = glm::dot(n, B) - off;
    if (dA < r)  return 1.0f;             // starts touching/inside: leave to discrete
    if (dB >= r) return 1.0f;             // ends outside: no crossing
    outN = n;
    return (dA - r) / (dA - dB);
}

float SweptSphereSphere(const glm::vec3& A, const glm::vec3& B, float r,
                        const glm::vec3& C, float R, glm::vec3& outN) {
    const glm::vec3 d = B - A;
    const glm::vec3 m = A - C;
    const float rr = r + R;
    const float a = glm::dot(d, d);
    if (a < 1e-12f) return 1.0f;
    const float c = glm::dot(m, m) - rr * rr;
    if (c < 0.0f) return 1.0f;            // starts overlapping
    const float b = 2.0f * glm::dot(m, d);
    const float disc = b * b - 4.0f * a * c;
    if (disc < 0.0f) return 1.0f;
    const float t = (-b - std::sqrt(disc)) / (2.0f * a);
    if (t < 0.0f || t > 1.0f) return 1.0f;
    outN = glm::normalize((A + t * d) - C);
    return t;
}

// Swept sphere vs OBB: ray in box-local space against the box inflated by r on
// each axis (a rounded box approximated by its slab bounds -- slightly
// conservative near corners, which is the safe direction for anti-tunneling).
float SweptSphereBox(const glm::vec3& A, const glm::vec3& B, float r,
                     const OBB& box, glm::vec3& outN) {
    const glm::vec3 pa = A - box.center;
    const glm::vec3 la(glm::dot(pa, box.axis[0]), glm::dot(pa, box.axis[1]), glm::dot(pa, box.axis[2]));
    const glm::vec3 pb = B - box.center;
    const glm::vec3 lb(glm::dot(pb, box.axis[0]), glm::dot(pb, box.axis[1]), glm::dot(pb, box.axis[2]));
    const glm::vec3 ld = lb - la;
    const glm::vec3 e = box.ext + glm::vec3(r);

    float tmin = 0.0f, tmax = 1.0f;
    int axis = -1; float sgn = 1.0f;
    for (int k = 0; k < 3; ++k) {
        if (std::fabs(ld[k]) < 1e-8f) {
            if (la[k] < -e[k] || la[k] > e[k]) return 1.0f;   // parallel, outside slab
            continue;
        }
        const float inv = 1.0f / ld[k];
        float t1 = (-e[k] - la[k]) * inv;
        float t2 = ( e[k] - la[k]) * inv;
        if (t1 > t2) std::swap(t1, t2);
        if (t1 > tmin) { tmin = t1; axis = k; sgn = (ld[k] > 0.0f) ? -1.0f : 1.0f; }
        if (t2 < tmax) tmax = t2;
        if (tmin > tmax) return 1.0f;
    }
    if (axis < 0) return 1.0f;             // started inside: leave to discrete
    outN = sgn * box.axis[axis];
    return tmin;
}

// Sweep entity 'self' (as a sphere of radius r) from A to B against every other
// non-trigger collider; return the earliest time-of-impact and its normal.
float SweepToTOI(ecs::Registry& reg, Entity self, const glm::vec3& A,
                 const glm::vec3& B, float r, glm::vec3& outN) {
    float best = 1.0f; glm::vec3 bestN(0.0f);
    reg.view<Transform, Collider>().each([&](Entity e2, Transform& t2, Collider& c2) {
        if (e2 == self || c2.isTrigger) return;
        glm::vec3 n(0.0f); float t = 1.0f;
        if (c2.shape == ColliderShape::Plane) {
            t = SweptSpherePlane(A, B, r, c2.planeNormal, c2.planeOffset, n);
        } else if (c2.shape == ColliderShape::Sphere) {
            t = SweptSphereSphere(A, B, r, t2.position, c2.radius, n);
        } else {
            OBB o; o.center = t2.position;
            const glm::mat3 R = glm::mat3_cast(t2.rotation);
            o.axis[0] = R[0]; o.axis[1] = R[1]; o.axis[2] = R[2];
            o.ext = c2.halfExtents;
            t = SweptSphereBox(A, B, r, o, n);
        }
        if (t < best) { best = t; bestN = n; }
    });
    outN = bestN;
    return best;
}

float SweepRadiusOf(const Collider* c) {
    if (!c) return 0.1f;
    if (c->shape == ColliderShape::Sphere) return c->radius;
    if (c->shape == ColliderShape::Box)    return glm::length(c->halfExtents);
    return 0.1f;
}

} // namespace

void PhysicsWorld::Step(ecs::Registry& reg, float dt) {
    // 0) Spring joints add forces before integration.
    for (const Joint& j : m_joints)
        if (j.type == Joint::Type::Spring) ApplySpring(reg, j);

    // 1) Integrate (semi-implicit Euler): velocity first, then position. A body
    //    with CCD enabled sweeps its motion and clamps to the first impact.
    reg.view<Transform, RigidBody>().each([&](Entity e, Transform& t, RigidBody& rb) {
        if (rb.invMass <= 0.0f) { rb.accumForce = glm::vec3(0.0f); return; }
        if (rb.sleeping)          { rb.accumForce = glm::vec3(0.0f); return; }
        glm::vec3 accel = rb.accumForce * rb.invMass;
        if (rb.useGravity) accel += gravity;
        rb.velocity  += accel * dt;

        glm::vec3 start = t.position;
        glm::vec3 end   = start + rb.velocity * dt;
        if (rb.ccd) {
            const float r = SweepRadiusOf(reg.TryGet<Collider>(e));
            glm::vec3 n(0.0f);
            const float toi = SweepToTOI(reg, e, start, end, r, n);
            if (toi < 1.0f) {
                end = start + (end - start) * toi + n * 0.001f;
                const float vn = glm::dot(rb.velocity, n);
                if (vn < 0.0f) rb.velocity -= n * vn;
            }
        }
        t.position    = end;
        rb.accumForce = glm::vec3(0.0f);
    });

    // 2) Gather colliders into the reused body list.
    m_bodies.clear();
    reg.view<Transform, Collider>().each([&](Entity e, Transform& t, Collider& c) {
        m_bodies.push_back(SolverBody{e, &t, &c, reg.TryGet<RigidBody>(e)});
    });
    const int N = static_cast<int>(m_bodies.size());

    // 3) Broad phase -> a sorted, de-duplicated list of candidate index pairs
    //    (reused buffers). Sorting makes the solver order-independent of the
    //    broad phase, so grid and brute-force give identical results.
    m_pairs.clear();
    if (!broadPhase || N < 2) {
        for (int i = 0; i < N; ++i)
            for (int j = i + 1; j < N; ++j) m_pairs.emplace_back(i, j);
    } else {
        m_planes.clear(); m_finite.clear();
        for (int i = 0; i < N; ++i) {
            if (m_bodies[i].c->shape == ColliderShape::Plane) m_planes.push_back(i);
            else m_finite.push_back(i);
        }
        const float cs = (cellSize > 1e-3f) ? cellSize : 1.0f;
        const float margin = 0.1f;
        for (auto& cell : m_grid) cell.second.clear();     // keep buckets + cell vectors
        m_keys.clear();
        for (int fi : m_finite) {
            AABB a = ComputeAABB(m_bodies[fi]);
            a.mn -= glm::vec3(margin); a.mx += glm::vec3(margin);
            const int x0 = int(std::floor(a.mn.x / cs)), x1 = int(std::floor(a.mx.x / cs));
            const int y0 = int(std::floor(a.mn.y / cs)), y1 = int(std::floor(a.mx.y / cs));
            const int z0 = int(std::floor(a.mn.z / cs)), z1 = int(std::floor(a.mx.z / cs));
            for (int ix = x0; ix <= x1; ++ix)
                for (int iy = y0; iy <= y1; ++iy)
                    for (int iz = z0; iz <= z1; ++iz)
                        m_grid[CellKey(ix, iy, iz)].push_back(fi);
        }
        for (auto& cell : m_grid) {
            auto& list = cell.second;
            for (std::size_t a = 0; a < list.size(); ++a)
                for (std::size_t b = a + 1; b < list.size(); ++b) {
                    int i = list[a], j = list[b];
                    if (i > j) std::swap(i, j);
                    m_keys.push_back(std::int64_t(i) * N + j);
                }
        }
        std::sort(m_keys.begin(), m_keys.end());
        m_keys.erase(std::unique(m_keys.begin(), m_keys.end()), m_keys.end());
        for (std::int64_t k : m_keys) m_pairs.emplace_back(int(k / N), int(k % N));
        for (int p : m_planes)
            for (int f : m_finite) { int i = p, j = f; if (i > j) std::swap(i, j); m_pairs.emplace_back(i, j); }
        std::sort(m_pairs.begin(), m_pairs.end());
        m_pairs.erase(std::unique(m_pairs.begin(), m_pairs.end()), m_pairs.end());
    }

    // 4) Narrow phase ONCE: build the cached contact manifolds and the set of
    //    touching pairs. Pairs where both bodies are static/asleep are skipped
    //    (they can't move) -- but a touching such pair is carried so its Stay
    //    event is preserved without re-detecting.
    m_manifolds.clear();
    m_touchingNow.clear();
    SolverBody* base = m_bodies.data();
    for (const auto& pr : m_pairs) {
        SolverBody* A = &m_bodies[pr.first];
        SolverBody* B = &m_bodies[pr.second];
        if (priority(A->c->shape) > priority(B->c->shape)) std::swap(A, B);
        if (Inactive(*A) && Inactive(*B)) {
            const std::uint64_t key = PairKey(A->e, B->e);
            if (m_touching.find(key) != m_touching.end())
                m_touchingNow[key] = (A->c->isTrigger || B->c->isTrigger);
            continue;
        }
        const Contact ct = Detect(*A, *B);
        if (!ct.hit) continue;
        const bool trig = A->c->isTrigger || B->c->isTrigger;
        m_touchingNow[PairKey(A->e, B->e)] = trig;
        if (!trig) {
            ContactManifold m;
            m.a = static_cast<int>(A - base);
            m.b = static_cast<int>(B - base);
            m.normal = ct.normal;
            m.penetration = ct.penetration;
            m_manifolds.push_back(m);
        }
    }

    // 5) Emit Enter/Stay/Exit events by diffing against last step.
    m_events.clear();
    for (const auto& kv : m_touchingNow) {
        const bool was = (m_touching.find(kv.first) != m_touching.end());
        CollisionEvent ev;
        ev.a = Entity(kv.first >> 32);
        ev.b = Entity(kv.first & 0xFFFFFFFFu);
        ev.phase = was ? CollisionEvent::Phase::Stay : CollisionEvent::Phase::Enter;
        ev.trigger = kv.second;
        m_events.push_back(ev);
    }
    for (const auto& kv : m_touching) {
        if (m_touchingNow.find(kv.first) == m_touchingNow.end()) {
            CollisionEvent ev;
            ev.a = Entity(kv.first >> 32);
            ev.b = Entity(kv.first & 0xFFFFFFFFu);
            ev.phase = CollisionEvent::Phase::Exit;
            ev.trigger = kv.second;
            m_events.push_back(ev);
        }
    }
    m_touching.swap(m_touchingNow);   // m_touching = this step; old cleared next step

    // 6) Velocity solve: sequential impulses over the cached manifolds (re-solved
    //    each iteration, no re-detection) plus rigid distance joints.
    const float wakeSpeed = sleepLinearVelocity * 2.0f;
    for (int iter = 0; iter < solverIterations; ++iter) {
        for (const ContactManifold& m : m_manifolds)
            ResolveVelocity(m_bodies[m.a], m_bodies[m.b], m.normal, restitutionThreshold, wakeSpeed);
        for (const Joint& j : m_joints)
            if (j.type == Joint::Type::Distance) SolveDistance(reg, j);
    }

    // 7) One positional-correction pass from the cached penetrations.
    for (const ContactManifold& m : m_manifolds)
        CorrectPosition(m_bodies[m.a], m_bodies[m.b], m.normal, m.penetration);

    // 8) Update sleep state: bodies that stayed slow long enough go to sleep.
    if (allowSleeping) {
        const float thresh2 = sleepLinearVelocity * sleepLinearVelocity;
        reg.view<Transform, RigidBody>().each([&](Entity, Transform&, RigidBody& rb) {
            if (rb.invMass <= 0.0f) return;
            if (!rb.allowSleep) { rb.sleeping = false; rb.sleepTimer = 0.0f; return; }
            if (rb.sleeping) return;
            if (glm::dot(rb.velocity, rb.velocity) < thresh2) {
                rb.sleepTimer += dt;
                if (rb.sleepTimer >= timeToSleep) { rb.sleeping = true; rb.velocity = glm::vec3(0.0f); }
            } else {
                rb.sleepTimer = 0.0f;
            }
        });
    }
}

namespace {

// ---- ray/shape intersections. Each returns the nearest t>=0 (or -1 on miss),
//      writing the surface normal (oriented against the ray) into outN. --------

float RaySphere(const glm::vec3& o, const glm::vec3& d,
                const glm::vec3& c, float r, glm::vec3& outN) {
    const glm::vec3 m = o - c;
    const float b = glm::dot(m, d);
    const float cc = glm::dot(m, m) - r * r;
    if (cc > 0.0f && b > 0.0f) return -1.0f;    // origin outside and pointing away
    const float disc = b * b - cc;
    if (disc < 0.0f) return -1.0f;
    const float sq = std::sqrt(disc);
    float t = -b - sq;
    if (t < 0.0f) t = -b + sq;                  // origin inside the sphere
    if (t < 0.0f) return -1.0f;
    outN = glm::normalize((o + t * d) - c);
    return t;
}

// Infinite half-space plane: dot(n, x) = off.
float RayPlane(const glm::vec3& o, const glm::vec3& d,
               const glm::vec3& n, float off, glm::vec3& outN) {
    const float denom = glm::dot(d, n);
    if (std::fabs(denom) < 1e-8f) return -1.0f;  // parallel
    const float t = (off - glm::dot(n, o)) / denom;
    if (t < 0.0f) return -1.0f;
    outN = (denom < 0.0f) ? n : -n;              // face the ray
    return t;
}

// Slab test in the box's local frame.
float RayBox(const glm::vec3& o, const glm::vec3& d, const OBB& box, glm::vec3& outN) {
    const glm::vec3 p = o - box.center;
    const glm::vec3 lo(glm::dot(p, box.axis[0]), glm::dot(p, box.axis[1]), glm::dot(p, box.axis[2]));
    const glm::vec3 ld(glm::dot(d, box.axis[0]), glm::dot(d, box.axis[1]), glm::dot(d, box.axis[2]));

    float tmin = -std::numeric_limits<float>::max();
    float tmax =  std::numeric_limits<float>::max();
    int   axis = 0; float nsign = 1.0f;
    for (int k = 0; k < 3; ++k) {
        if (std::fabs(ld[k]) < 1e-8f) {
            if (lo[k] < -box.ext[k] || lo[k] > box.ext[k]) return -1.0f;  // parallel, outside slab
            continue;
        }
        const float inv = 1.0f / ld[k];
        float tNear = (-box.ext[k] - lo[k]) * inv;
        float tFar  = ( box.ext[k] - lo[k]) * inv;
        if (tNear > tFar) std::swap(tNear, tFar);
        if (tNear > tmin) { tmin = tNear; axis = k; nsign = (ld[k] > 0.0f) ? -1.0f : 1.0f; }
        if (tFar  < tmax)   tmax = tFar;
        if (tmin > tmax) return -1.0f;
    }
    float t = (tmin >= 0.0f) ? tmin : tmax;
    if (t < 0.0f) return -1.0f;
    outN = nsign * box.axis[axis];
    if (glm::dot(outN, d) > 0.0f) outN = -outN;   // ensure it faces the ray
    return t;
}

} // namespace

RaycastHit PhysicsWorld::Raycast(ecs::Registry& reg, const Ray& ray, float maxDistance) const {
    RaycastHit best;
    best.distance = maxDistance;
    const float len2 = glm::dot(ray.direction, ray.direction);
    if (len2 < 1e-12f) return best;              // degenerate ray
    const glm::vec3 d = ray.direction / std::sqrt(len2);

    reg.view<Transform, Collider>().each([&](Entity e, Transform& t, Collider& c) {
        glm::vec3 n(0.0f);
        float hitT = -1.0f;
        if (c.shape == ColliderShape::Sphere) {
            hitT = RaySphere(ray.origin, d, t.position, c.radius, n);
        } else if (c.shape == ColliderShape::Plane) {
            hitT = RayPlane(ray.origin, d, c.planeNormal, c.planeOffset, n);
        } else { // Box
            OBB o; o.center = t.position;
            const glm::mat3 R = glm::mat3_cast(t.rotation);
            o.axis[0] = R[0]; o.axis[1] = R[1]; o.axis[2] = R[2];
            o.ext = c.halfExtents;
            hitT = RayBox(ray.origin, d, o, n);
        }
        if (hitT >= 0.0f && hitT < best.distance) {
            best.hit = true;
            best.entity = e;
            best.distance = hitT;
            best.point = ray.origin + hitT * d;
            best.normal = n;
        }
    });

    if (!best.hit) best.distance = 0.0f;
    return best;
}

} // namespace engine
