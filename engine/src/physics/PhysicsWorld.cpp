#include "engine/physics/PhysicsWorld.h"

#include "engine/physics/PhysicsComponents.h"
#include "engine/physics/ColliderTransform.h"
#include "engine/physics/CollisionMesh.h"
#include "engine/physics/ConvexCollision.h"   // Pass-2 GJK/EPA convex narrow phase
#include "engine/ecs/Registry.h"
#include "engine/ecs/Components.h"   // Transform

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>    // mat3_cast

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using engine::ecs::Entity;
using engine::ecs::Transform;
using engine::ecs::RigidBody;
using engine::ecs::Collider;
using engine::ecs::ColliderShape;
using engine::ecs::AdditionalColliders;

namespace engine {
namespace {

namespace convex = physics::convex;

// Gathered per step (defined in the header so PhysicsWorld can cache the list).
using Body = engine::SolverBody;

// A body that neither moves nor can be moved this step (static or asleep).
bool Inactive(const Body& b) { return !b.rb || b.rb->sleeping; }

// Order-independent 64-bit key for an entity pair (stable across steps).
std::uint64_t PairKey(Entity a, Entity b) {
    if (a > b) std::swap(a, b);
    return (std::uint64_t(a) << 32) | std::uint64_t(b);
}

// Collision filtering: each side's mask must include the other's layer bit(s).
bool LayersCollide(const Collider& a, const Collider& b) {
    return (a.mask & b.layer) != 0u && (b.mask & a.layer) != 0u;
}

// Build an orthonormal friction basis (t1, t2) perpendicular to the contact
// normal n. Deterministic so warm-started tangent impulses stay consistent.
void BuildTangents(const glm::vec3& n, glm::vec3& t1, glm::vec3& t2) {
    if (std::fabs(n.x) >= 0.577f) t1 = glm::normalize(glm::vec3(n.y, -n.x, 0.0f));
    else                          t1 = glm::normalize(glm::vec3(0.0f, n.z, -n.y));
    t2 = glm::cross(n, t1);
}

struct Contact {
    bool      hit = false;
    glm::vec3 normal{0.0f};   // points from A toward B
    float     penetration = 0.0f;
    int       count = 0;
    glm::vec3 points[4]{};    // world-space contact points
};

// An oriented bounding box in world space (built from Transform + Collider).
struct OBB {
    glm::vec3 center;
    glm::vec3 axis[3];   // unit, right-handed
    glm::vec3 ext;       // half-extents
};

float ColliderBoundRadius(const Collider& c) {
    switch (c.shape) {
        case ColliderShape::Sphere:   return std::max(c.radius, 0.0f);
        case ColliderShape::Capsule:  return std::max(c.radius + c.halfHeight, 0.0f);
        case ColliderShape::Box:
        case ColliderShape::Cylinder:
        case ColliderShape::Cone:
        case ColliderShape::Pyramid:
        case ColliderShape::Torus:
        case ColliderShape::Staircase:
        case ColliderShape::ConvexHull:
        case ColliderShape::TriangleMesh:
            return glm::length(c.halfExtents);
        case ColliderShape::Plane:    return std::numeric_limits<float>::infinity();
    }
    return 0.0f;
}

// Sleeping bodies act as immovable (invMass 0, zero velocity) until woken.
// Kinematic bodies are immovable too (infinite mass) -- they push but aren't
// pushed -- yet velOf() still reports their velocity so they impart momentum.
float invMassOf(const Body& b) {
    if (!b.rb || b.rb->sleeping || b.rb->kinematic) return 0.0f;
    return b.rb->invMass;
}
glm::vec3 velOf(const Body& b) {
    if (!b.rb || b.rb->sleeping) return glm::vec3(0.0f);
    return b.rb->velocity;
}
void Wake(Body& b) {
    if (b.rb && b.rb->sleeping) { b.rb->sleeping = false; b.rb->sleepTimer = 0.0f; }
}
glm::vec3 AngVelOf(const Body& b) {
    return (b.rb && !b.rb->sleeping) ? b.rb->angularVelocity : glm::vec3(0.0f);
}
glm::mat3 InvIWorld(const Body& b) {
    if (!b.rb || b.rb->sleeping || b.rb->kinematic) return glm::mat3(0.0f);
    const glm::mat3 R = glm::mat3_cast(b.owner ? b.owner->rotation : b.t->rotation);
    return R * b.rb->invInertiaLocal * glm::transpose(R);
}
// World centre of mass -- the point contact/impulse arms are measured from and the body
// rotates about. Falls back to the entity origin for bodies with no rigid body / no COM
// offset, so the common case is unchanged.
glm::vec3 BodyCenter(const Body& b) {
    const glm::vec3 origin = b.owner ? b.owner->position : b.t->position;
    if (!b.rb) return origin;
    const glm::quat rot = b.owner ? b.owner->rotation : b.t->rotation;
    return origin + rot * b.rb->centerOfMassLocal;
}

// Shape ordering so each pair is tested in one canonical direction. Sphere < Box
// < Plane; the higher-priority shape becomes B, and the contact normal points
// from A toward B.
int priority(ColliderShape s) {
    switch (s) {
        case ColliderShape::Sphere:  return 0;
        case ColliderShape::Capsule: return 1;
        case ColliderShape::Box:     return 2;
        case ColliderShape::Cylinder:return 3;
        case ColliderShape::Cone:    return 4;
        case ColliderShape::Pyramid: return 5;
        case ColliderShape::Torus:   return 6;
        case ColliderShape::Staircase:return 7;
        case ColliderShape::ConvexHull:return 8;
        case ColliderShape::TriangleMesh:return 9;
        case ColliderShape::Plane:   return 10;
    }
    return 0;
}

// Body-space inverse inertia for a collider (plane/other -> no rotation).
glm::mat3 InertiaFor(const Collider& c, float mass) {
    if (c.shape == ColliderShape::Box)     return RigidBody::SolidBoxInvInertia(mass, c.halfExtents);
    if (c.shape == ColliderShape::Sphere)  return RigidBody::SolidSphereInvInertia(mass, c.radius);
    if (c.shape == ColliderShape::Capsule) return RigidBody::CapsuleInvInertia(mass, c.radius, c.halfHeight);
    if (c.shape == ColliderShape::Cylinder) return RigidBody::CylinderInvInertia(mass, c.radius, c.halfHeight);
    if (c.shape == ColliderShape::Cone || c.shape == ColliderShape::Pyramid || c.shape == ColliderShape::Torus
        || c.shape == ColliderShape::Staircase || c.shape == ColliderShape::ConvexHull)
        // ConvexHull: approximate the rotational inertia with its bounding box
        // (c.halfExtents holds the hull's world-scaled bounds) so dynamic hulls
        // tumble instead of behaving as rotationally frozen.
        return RigidBody::SolidBoxInvInertia(mass, c.halfExtents);
    return glm::mat3(0.0f);
}

// World-space volume (m^3) of a canonical scaled collider, used to derive mass from a material
// density (Pass-4 Phase 55). Capsule = cylinder + sphere caps. Plane/mesh/degenerate shapes have
// no well-defined solid volume; return 0 so the caller keeps the authored mass instead.
float ShapeWorldVolume(const Collider& c) {
    constexpr float kPi = 3.14159265358979323846f;
    switch (c.shape) {
        case ColliderShape::Box:
            return 8.0f * c.halfExtents.x * c.halfExtents.y * c.halfExtents.z;
        case ColliderShape::Sphere:
            return (4.0f / 3.0f) * kPi * c.radius * c.radius * c.radius;
        case ColliderShape::Capsule: {
            const float cyl  = kPi * c.radius * c.radius * (2.0f * c.halfHeight);
            const float caps = (4.0f / 3.0f) * kPi * c.radius * c.radius * c.radius;
            return cyl + caps;
        }
        case ColliderShape::Cylinder:
        case ColliderShape::Cone: {
            const float full = kPi * c.radius * c.radius * (2.0f * c.halfHeight);
            return (c.shape == ColliderShape::Cone) ? full / 3.0f : full;
        }
        case ColliderShape::ConvexHull:
        case ColliderShape::Pyramid:
        case ColliderShape::Staircase:
            // Approximate with the world-scaled bounding box (halfExtents holds those bounds).
            return 8.0f * c.halfExtents.x * c.halfExtents.y * c.halfExtents.z;
        case ColliderShape::Torus: {
            // V = 2*pi^2 * R * r^2.
            return 2.0f * kPi * kPi * c.majorRadius * c.minorRadius * c.minorRadius;
        }
        default:
            return 0.0f;   // Plane / mesh / unknown: no solid volume
    }
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
    ct.points[0] = pa + ct.normal * ra;   // on A's surface toward B
    ct.count = 1;
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
    ct.points[0] = pa + ct.normal * ra;   // sphere's contact point on the plane side
    ct.count = 1;
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
        ct.points[0] = sc;                      // centre (sphere is inside the box)
        ct.count = 1;
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
    ct.points[0] = closest;                 // closest point on the box surface
    ct.count = 1;
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
        if (pen > 0.0f) {
            if (ct.count < 4) ct.points[ct.count++] = corner;   // every below-plane corner is a contact
            deepest = std::max(deepest, pen);
        }
    }
    if (ct.count == 0) return ct;
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

    glm::vec3 axes[15];
    int n = 0;
    for (int i = 0; i < 3; ++i) axes[n++] = A.axis[i];   // 0..2  A faces
    for (int i = 0; i < 3; ++i) axes[n++] = B.axis[i];   // 3..5  B faces
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) axes[n++] = glm::cross(A.axis[i], B.axis[j]);  // 6..14 edges

    float minOverlap = std::numeric_limits<float>::max();
    glm::vec3 bestAxis(0.0f);
    int bestIdx = -1;
    for (int k = 0; k < 15; ++k) {
        glm::vec3 L = axes[k];
        const float len2 = glm::dot(L, L);
        if (len2 < 1e-6f) continue;
        L /= std::sqrt(len2);
        float rA = 0.0f, rB = 0.0f;
        for (int i = 0; i < 3; ++i) rA += A.ext[i] * std::fabs(glm::dot(A.axis[i], L));
        for (int i = 0; i < 3; ++i) rB += B.ext[i] * std::fabs(glm::dot(B.axis[i], L));
        const float dist = std::fabs(glm::dot(t, L));
        const float overlap = rA + rB - dist;
        if (overlap < 0.0f) return ct;
        if (overlap < minOverlap - 1e-4f) {
            minOverlap = overlap;
            bestAxis = (glm::dot(t, L) < 0.0f) ? -L : L;   // orient A -> B
            bestIdx = k;
        }
    }
    ct.hit = true;
    ct.normal = bestAxis;
    ct.penetration = minOverlap;

    if (bestIdx >= 0 && bestIdx < 6) {
        // Face contact: clip the incident face's corners to the reference face,
        // giving up to four contact points -> stable resting and stacking.
        const bool refIsA = bestIdx < 3;
        const OBB& ref = refIsA ? A : B;
        const OBB& inc = refIsA ? B : A;
        const glm::vec3 rN = refIsA ? bestAxis : -bestAxis;   // ref face outward, toward inc

        int ra = 0; float bestP = -1.0f;
        for (int i = 0; i < 3; ++i) { const float d = std::fabs(glm::dot(ref.axis[i], rN)); if (d > bestP) { bestP = d; ra = i; } }
        const float rsign = (glm::dot(ref.axis[ra], rN) >= 0.0f) ? 1.0f : -1.0f;
        const glm::vec3 refN = rsign * ref.axis[ra];
        const glm::vec3 rC = ref.center + refN * ref.ext[ra];   // reference face centre
        const int ru = (ra + 1) % 3, rv = (ra + 2) % 3;
        const glm::vec3 uA = ref.axis[ru]; const float ue = ref.ext[ru];
        const glm::vec3 vA = ref.axis[rv]; const float ve = ref.ext[rv];

        int ia = 0; float bestI = -1.0f;
        for (int i = 0; i < 3; ++i) { const float d = std::fabs(glm::dot(inc.axis[i], refN)); if (d > bestI) { bestI = d; ia = i; } }
        const float isign = (glm::dot(inc.axis[ia], refN) >= 0.0f) ? -1.0f : 1.0f;
        const glm::vec3 iC = inc.center + (isign * inc.axis[ia]) * inc.ext[ia];   // incident face centre
        const int iu = (ia + 1) % 3, iv = (ia + 2) % 3;
        const glm::vec3 iua = inc.axis[iu] * inc.ext[iu];
        const glm::vec3 iva = inc.axis[iv] * inc.ext[iv];
        const glm::vec3 corners[4] = { iC + iua + iva, iC - iua + iva, iC - iua - iva, iC + iua - iva };

        // Contact margin (Pass-3): keep incident corners within kContactMargin of the reference
        // face -- both laterally and in depth -- as contact points, even if slightly separated.
        // A tilted/dropped box would otherwise shed corners the instant they lift a fraction of
        // a mm, collapsing a stable 4-point face contact to 1-2 points that cannot resist
        // tipping. These speculative points carry the manifold's shared penetration; the
        // velocity solver still only pushes on genuinely closing ones. This, with the 2-point
        // block solver, is what lets stacks and dropped boxes stay upright at low iteration counts.
        constexpr float kContactMargin = 0.015f;
        for (int c = 0; c < 4 && ct.count < 4; ++c) {
            const glm::vec3 d = corners[c] - rC;
            const float pu = glm::dot(d, uA), pv = glm::dot(d, vA);
            if (std::fabs(pu) <= ue + kContactMargin && std::fabs(pv) <= ve + kContactMargin) {
                const float depth = glm::dot(refN, rC - corners[c]);           // >0 = below the face
                if (depth > -kContactMargin) ct.points[ct.count++] = corners[c];
            }
        }
    }

    if (ct.count == 0) {   // edge contact (or no clipped corner): single midpoint
        glm::vec3 pA = A.center, pB = B.center;
        for (int i = 0; i < 3; ++i) {
            pA += (glm::dot(A.axis[i], ct.normal) >= 0.0f ? A.ext[i] : -A.ext[i]) * A.axis[i];
            pB += (glm::dot(B.axis[i], ct.normal) >= 0.0f ? -B.ext[i] : B.ext[i]) * B.axis[i];
        }
        ct.points[0] = 0.5f * (pA + pB);
        ct.count = 1;
    }
    return ct;
}

// ---- capsule geometry helpers + generators ---------------------------------

// Endpoints of a capsule body's central segment (local +Y, rotated by Transform).
void CapsuleSegment(const Body& b, glm::vec3& p0, glm::vec3& p1) {
    const glm::vec3 up = glm::mat3_cast(b.t->rotation) * glm::vec3(0.0f, 1.0f, 0.0f);
    const glm::vec3 h  = up * b.c->halfHeight;
    p0 = b.t->position - h;
    p1 = b.t->position + h;
}

// Closest point on segment [a,b] to point p.
glm::vec3 ClosestOnSeg(const glm::vec3& a, const glm::vec3& b, const glm::vec3& p) {
    const glm::vec3 ab = b - a;
    const float denom = glm::dot(ab, ab);
    float t = (denom > 1e-9f) ? glm::dot(p - a, ab) / denom : 0.0f;
    t = glm::clamp(t, 0.0f, 1.0f);
    return a + t * ab;
}

// Closest points between segments [p1,q1] and [p2,q2] (Ericson, Real-Time
// Collision Detection). Fills c1 on the first segment, c2 on the second.
void ClosestSegSeg(const glm::vec3& p1, const glm::vec3& q1,
                   const glm::vec3& p2, const glm::vec3& q2,
                   glm::vec3& c1, glm::vec3& c2) {
    const glm::vec3 d1 = q1 - p1, d2 = q2 - p2, r = p1 - p2;
    const float a = glm::dot(d1, d1), e = glm::dot(d2, d2), f = glm::dot(d2, r);
    float s, t;
    if (a <= 1e-9f && e <= 1e-9f) { c1 = p1; c2 = p2; return; }
    if (a <= 1e-9f) { s = 0.0f; t = glm::clamp(f / e, 0.0f, 1.0f); }
    else {
        const float c = glm::dot(d1, r);
        if (e <= 1e-9f) { t = 0.0f; s = glm::clamp(-c / a, 0.0f, 1.0f); }
        else {
            const float b = glm::dot(d1, d2);
            const float denom = a * e - b * b;
            s = (denom > 1e-9f) ? glm::clamp((b * f - c * e) / denom, 0.0f, 1.0f) : 0.0f;
            t = (b * s + f) / e;
            if (t < 0.0f)      { t = 0.0f; s = glm::clamp(-c / a, 0.0f, 1.0f); }
            else if (t > 1.0f) { t = 1.0f; s = glm::clamp((b - c) / a, 0.0f, 1.0f); }
        }
    }
    c1 = p1 + d1 * s;
    c2 = p2 + d2 * t;
}

// A = sphere, B = capsule. Nearest point on the capsule axis -> sphere-sphere.
Contact SphereCapsule(const glm::vec3& sc, float rs, const glm::vec3& p0, const glm::vec3& p1, float rc) {
    return SphereSphere(sc, rs, ClosestOnSeg(p0, p1, sc), rc);   // normal A(sphere) -> B(capsule)
}

// A = capsule, B = capsule. Closest points between the two axes -> sphere-sphere.
Contact CapsuleCapsule(const glm::vec3& a0, const glm::vec3& a1, float ra,
                       const glm::vec3& b0, const glm::vec3& b1, float rb) {
    glm::vec3 c1, c2; ClosestSegSeg(a0, a1, b0, b1, c1, c2);
    return SphereSphere(c1, ra, c2, rb);        // normal A -> B
}

// A = capsule, B = plane. Each endpoint hemisphere below the plane is a contact,
// giving up to two points -> a lying capsule rests without tipping.
Contact CapsulePlane(const glm::vec3& p0, const glm::vec3& p1, float r,
                     const glm::vec3& n, float off) {
    Contact ct;
    const glm::vec3 ends[2] = { p0, p1 };
    float deepest = 0.0f;
    for (int i = 0; i < 2; ++i) {
        const float s = glm::dot(n, ends[i]) - off;     // endpoint centre above plane
        const float pen = r - s;
        if (pen > 0.0f) {
            if (ct.count < 4) ct.points[ct.count++] = ends[i] - n * r;   // on the plane side
            deepest = std::max(deepest, pen);
        }
    }
    if (ct.count == 0) return ct;
    ct.hit = true;
    ct.normal = -n;                 // A(capsule) -> B(plane)
    ct.penetration = deepest;
    return ct;
}

// A = capsule, B = box. Probe the capsule endpoints (and, if neither touches, the
// segment point nearest the box) as spheres against the OBB. Two endpoint hits on
// the same face give a stable two-point manifold for a capsule lying on a box.
Contact CapsuleBox(const glm::vec3& p0, const glm::vec3& p1, float r, const OBB& box) {
    Contact ct;
    auto probe = [&](const glm::vec3& sc) {
        const Contact e = SphereBox(sc, r, box);   // normal sphere->box == capsule->box
        if (!e.hit) return;
        if (!ct.hit || e.penetration > ct.penetration) { ct.normal = e.normal; ct.penetration = e.penetration; }
        if (ct.count < 4) ct.points[ct.count++] = e.points[0];
        ct.hit = true;
    };
    probe(p0);
    probe(p1);
    if (!ct.hit) probe(ClosestOnSeg(p0, p1, box.center));
    return ct;
}

bool CompositeShape(ColliderShape shape) {
    return shape == ColliderShape::Cylinder || shape == ColliderShape::Cone
        || shape == ColliderShape::Pyramid || shape == ColliderShape::Torus
        || shape == ColliderShape::Staircase;
}

// Complex colliders are decomposed into supported convex pieces for narrow-phase
// collision. This gives first-class engine behavior while keeping the existing,
// stable sphere/box/capsule contact solvers and manifolds.
struct ProxySet {
    static constexpr int Capacity = 32;
    std::array<Transform, Capacity> transforms{};
    std::array<Collider, Capacity> colliders{};
    std::array<Body, Capacity> bodies{};
    int count = 0;
};

void AddProxy(ProxySet& set, const Body& parent, const glm::vec3& localCenter,
              const glm::quat& localRotation, const Collider& collider) {
    if (set.count >= ProxySet::Capacity) return;
    const int i = set.count++;
    set.transforms[i] = *parent.t;
    set.transforms[i].position = parent.t->position + parent.t->rotation * localCenter;
    set.transforms[i].rotation = glm::normalize(parent.t->rotation * localRotation);
    set.transforms[i].scale = glm::vec3(1.0f);
    set.colliders[i] = collider;
    set.bodies[i] = Body{parent.e, &set.transforms[i], &set.colliders[i], nullptr};
}

void BuildProxies(const Body& body, ProxySet& set) {
    const Collider& c = *body.c;
    if (!CompositeShape(c.shape)) {
        AddProxy(set, body, glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), c);
        return;
    }

    if (c.shape == ColliderShape::Cylinder) {
        // Flat-ended cylinder, approximated by vertical box strips across its
        // circular XZ cross-section. Unlike the old capsule proxy, these pieces
        // reach the same top/bottom plane at every radius and never add rounded
        // hemispherical caps. Twenty strips keep the radial error small while
        // staying within ProxySet capacity.
        constexpr int slices = 20;
        const float radius = std::max(c.radius, 0.001f);
        const float halfHeight = std::max(c.halfHeight, 0.001f);
        const float width = (2.0f * radius) / static_cast<float>(slices);
        for (int i = 0; i < slices; ++i) {
            const float x = -radius + (static_cast<float>(i) + 0.5f) * width;
            const float z = std::sqrt(std::max(radius * radius - x * x, 0.0f));
            AddProxy(set, body, glm::vec3(x, 0.0f, 0.0f),
                glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                Collider::MakeBox(glm::vec3(width * 0.5f, halfHeight, std::max(z, 0.001f))));
        }
        return;
    }

    if (c.shape == ColliderShape::Torus) {
        constexpr int segments = 20;
        for (int i = 0; i < segments; ++i) {
            const float a = glm::two_pi<float>() * static_cast<float>(i) / segments;
            AddProxy(set, body,
                glm::vec3(std::cos(a) * c.majorRadius, 0.0f, std::sin(a) * c.majorRadius),
                glm::quat(1.0f, 0.0f, 0.0f, 0.0f), Collider::MakeSphere(c.minorRadius));
        }
        return;
    }

    if (c.shape == ColliderShape::Staircase) {
        const int steps = glm::clamp(c.steps, 1, ProxySet::Capacity);
        const float slice = (c.halfExtents.z * 2.0f) / steps;
        for (int i = 0; i < steps; ++i) {
            const float height = (c.halfExtents.y * 2.0f) * static_cast<float>(i + 1) / steps;
            const glm::vec3 ext(c.halfExtents.x, height * 0.5f, slice * 0.5f);
            const glm::vec3 center(0.0f, -c.halfExtents.y + ext.y,
                -c.halfExtents.z + slice * (static_cast<float>(i) + 0.5f));
            AddProxy(set, body, center, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), Collider::MakeBox(ext));
        }
        return;
    }

    // Cone and pyramid are convex tapered volumes represented by thin box
    // slices. Pyramid slices are exact square cross-sections; cone slices use
    // the same conservative square profile supported by the current OBB solver.
    constexpr int layers = 12;
    for (int i = 0; i < layers; ++i) {
        const float y0 = -c.halfExtents.y + (2.0f * c.halfExtents.y * i) / layers;
        const float y1 = -c.halfExtents.y + (2.0f * c.halfExtents.y * (i + 1)) / layers;
        const float fraction = std::max(1.0f - static_cast<float>(i + 1) / layers, 0.02f);
        const float x = (c.shape == ColliderShape::Cone ? c.radius : c.halfExtents.x) * fraction;
        const float z = (c.shape == ColliderShape::Cone ? c.radius : c.halfExtents.z) * fraction;
        AddProxy(set, body, glm::vec3(0.0f, (y0 + y1) * 0.5f, 0.0f),
            glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
            Collider::MakeBox(glm::vec3(x, (y1 - y0) * 0.5f, z)));
    }
}

// ---- Pass-2: exact convex/convex via GJK + EPA ----------------------------
//
// Builds a support-mapped convex::Shape from a world collider. Primitive world
// colliders already carry world-scaled dimensions with transform.scale == 1
// (BuildWorldCollider bakes scale into the dims), so dims are used as-is. A
// ConvexHull's geometry is mesh-local, so its vertices are transformed by the
// FULL owner transform (position/rotation/scale) into `hullScratch`.
bool BuildConvexShape(const Body& b, std::vector<glm::vec3>& hullScratch,
                      convex::Shape& out) {
    const Collider& c = *b.c;
    out.center = b.t->position;
    out.basis  = glm::mat3_cast(b.t->rotation);
    switch (c.shape) {
        case ColliderShape::Sphere:
            out.kind = convex::Kind::Sphere; out.roundRadius = c.radius; return true;
        case ColliderShape::Box:
            out.kind = convex::Kind::Box; out.halfExtents = c.halfExtents; return true;
        case ColliderShape::Capsule:
            out.kind = convex::Kind::Capsule; out.halfHeight = c.halfHeight;
            out.roundRadius = c.radius; return true;
        case ColliderShape::Cylinder:
            out.kind = convex::Kind::Cylinder; out.radius = c.radius;
            out.halfHeight = c.halfHeight; return true;
        case ColliderShape::Cone:
            out.kind = convex::Kind::Cone; out.radius = c.radius;
            out.halfHeight = c.halfHeight; return true;
        case ColliderShape::Pyramid:
            out.kind = convex::Kind::Pyramid; out.halfExtents = c.halfExtents; return true;
        case ColliderShape::ConvexHull: {
            const auto mesh = physics::AcquireCollisionMesh(c.collisionAssetPath);
            if (!mesh || mesh->triangles.empty()) return false;
            hullScratch.clear();
            hullScratch.reserve(mesh->triangles.size() * 3);
            const glm::vec3 s = b.t->scale;
            const glm::quat q = b.t->rotation;
            const glm::vec3 p = b.t->position;
            const auto push = [&](const glm::vec3& v) {
                hullScratch.push_back(p + q * (s * v));
            };
            for (const auto& tri : mesh->triangles) { push(tri.a); push(tri.b); push(tri.c); }
            out.kind = convex::Kind::Hull;
            out.hullPoints = hullScratch.data();
            out.hullCount  = hullScratch.size();
            return true;
        }
        default: return false;   // Plane / TriangleMesh / Torus / Staircase handled elsewhere
    }
}

// Convex/convex contact (normal points from A toward B, matching the dispatch
// convention). Returns a miss on separation or a degenerate EPA result.
Contact ConvexGjkEpa(const Body& A, const Body& B) {
    Contact ct;
    std::vector<glm::vec3> hullA, hullB;
    convex::Shape sa, sb;
    if (!BuildConvexShape(A, hullA, sa) || !BuildConvexShape(B, hullB, sb)) return ct;
    const convex::GjkResult gjk = convex::Gjk(sa, sb);
    if (!gjk.intersecting) return ct;
    const convex::EpaResult epa = convex::Epa(sa, sb, gjk);
    if (!epa.ok || epa.depth <= 1e-5f) return ct;
    ct.hit = true;
    ct.normal = epa.normal;          // A -> B
    ct.penetration = epa.depth;
    ct.points[0] = epa.contact;      // on A's surface
    ct.count = 1;
    return ct;
}

// A convex hull (or any support shape) against an infinite plane half-space.
// Collects up to four deepest vertices for a stable resting manifold (single
// EPA points make flat-bottomed hulls rock on the floor). Normal points from the
// hull (A) toward the plane, i.e. -planeNormal, to match A -> B convention with
// the plane as B.
Contact ConvexPlane(const Body& hullBody, const glm::vec3& n, float off) {
    Contact ct;
    std::vector<glm::vec3> verts;
    convex::Shape s;
    if (!BuildConvexShape(hullBody, verts, s)) return ct;
    // Gather candidate surface points: hull vertices, or the single support point
    // for smooth/primitive shapes.
    const glm::vec3* pts = nullptr; std::size_t count = 0;
    glm::vec3 single;
    if (s.kind == convex::Kind::Hull) { pts = verts.data(); count = verts.size(); }
    else { single = convex::Support(s, -n); pts = &single; count = 1; }
    float deepest = 0.0f;
    for (std::size_t i = 0; i < count; ++i) {
        const float sep = glm::dot(n, pts[i]) - off;   // <0 => penetrating
        if (sep < -deepest) deepest = -sep;
    }
    if (deepest <= 1e-5f) return ct;
    ct.hit = true;
    ct.normal = -n;                 // hull -> plane
    ct.penetration = deepest;
    // Add up to four points within a slop band of the deepest penetration.
    const float band = 0.02f;
    for (std::size_t i = 0; i < count && ct.count < 4; ++i) {
        const float sep = glm::dot(n, pts[i]) - off;
        if (sep <= -deepest + band) {
            // Avoid near-duplicate contact points.
            bool dup = false;
            for (int k = 0; k < ct.count; ++k)
                if (glm::dot(ct.points[k] - pts[i], ct.points[k] - pts[i]) < 1e-6f) { dup = true; break; }
            if (!dup) ct.points[ct.count++] = pts[i];
        }
    }
    if (ct.count == 0) ct.points[ct.count++] = pts[0];
    return ct;
}

Contact Detect(const Body& A, const Body& B);

Contact DetectComposite(const Body& A, const Body& B) {
    ProxySet a, b;
    BuildProxies(A, a);
    BuildProxies(B, b);
    Contact best;
    for (int i = 0; i < a.count; ++i) {
        for (int j = 0; j < b.count; ++j) {
            Body* pa = &a.bodies[i];
            Body* pb = &b.bodies[j];
            bool swapped = false;
            if (priority(pa->c->shape) > priority(pb->c->shape)) {
                std::swap(pa, pb);
                swapped = true;
            }
            Contact hit = Detect(*pa, *pb);
            if (swapped && hit.hit) hit.normal = -hit.normal;
            if (hit.hit && (!best.hit || hit.penetration > best.penetration)) best = hit;
        }
    }
    return best;
}

// Dispatch by the (canonically ordered) shape pair.
Contact Detect(const Body& A, const Body& B) {
    const auto sa = A.c->shape, sb = B.c->shape;

    // Pass-2: exact convex collision for ConvexHull pairs the older dispatch could
    // not handle. A hull is a closed convex volume, so all primitive-volume pairs
    // use GJK/EPA. Treating sphere/capsule pairs as triangle-surface probes made
    // contacts disappear between sparse mesh triangles and near hull edges.
    if (sb == ColliderShape::ConvexHull
        && (sa == ColliderShape::Sphere || sa == ColliderShape::Capsule
            || sa == ColliderShape::Box || sa == ColliderShape::Cylinder
            || sa == ColliderShape::Cone || sa == ColliderShape::Pyramid
            || sa == ColliderShape::ConvexHull)) {
        return ConvexGjkEpa(A, B);
    }
    if (sa == ColliderShape::ConvexHull && sb == ColliderShape::Plane) {
        return ConvexPlane(A, B.c->planeNormal, B.c->planeOffset);   // normal: hull -> plane
    }

    if (CompositeShape(sa) || CompositeShape(sb)) return DetectComposite(A, B);
    const bool meshB = sb == ColliderShape::TriangleMesh || sb == ColliderShape::ConvexHull;
    if (meshB && !B.c->collisionAssetPath.empty()
        && (sa == ColliderShape::Sphere || sa == ColliderShape::Capsule)) {
        const auto mesh = physics::AcquireCollisionMesh(B.c->collisionAssetPath);
        if (!mesh) return Contact{};
        Contact result;
        const auto probe = [&](const glm::vec3& center, float radius) {
            glm::vec3 point, normal; float penetration = 0.0f;
            if (!physics::CollideSphereCollisionMesh(*mesh, *B.t, center, radius,
                                                     &point, &normal, &penetration)) return;
            if (!result.hit || penetration > result.penetration) {
                result.normal = normal; result.penetration = penetration;
            }
            if (result.count < 4) result.points[result.count++] = point;
            result.hit = true;
        };
        if (sa == ColliderShape::Sphere) probe(A.t->position, A.c->radius);
        else {
            glm::vec3 a0, a1; CapsuleSegment(A, a0, a1);
            probe(a0, A.c->radius); probe(a1, A.c->radius);
            probe((a0 + a1) * 0.5f, A.c->radius);
        }
        return result;
    }
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
    if (sa == ColliderShape::Sphere && sb == ColliderShape::Capsule) {
        glm::vec3 b0, b1; CapsuleSegment(B, b0, b1);
        return SphereCapsule(A.t->position, A.c->radius, b0, b1, B.c->radius);
    }
    if (sa == ColliderShape::Capsule && sb == ColliderShape::Capsule) {
        glm::vec3 a0, a1, b0, b1; CapsuleSegment(A, a0, a1); CapsuleSegment(B, b0, b1);
        return CapsuleCapsule(a0, a1, A.c->radius, b0, b1, B.c->radius);
    }
    if (sa == ColliderShape::Capsule && sb == ColliderShape::Box) {
        glm::vec3 a0, a1; CapsuleSegment(A, a0, a1);
        return CapsuleBox(a0, a1, A.c->radius, MakeOBB(B));
    }
    if (sa == ColliderShape::Capsule && sb == ColliderShape::Plane) {
        glm::vec3 a0, a1; CapsuleSegment(A, a0, a1);
        return CapsulePlane(a0, a1, A.c->radius, B.c->planeNormal, B.c->planeOffset);
    }
    return Contact{};
}

// Apply an impulse P at contact point k, using the manifold's cached inverse
// masses/inertia + offsets (no mat3 rebuilds in the hot loop).
inline void ApplyImpulse(const ContactManifold& m, Body& A, Body& B, int k, const glm::vec3& P) {
    if (A.rb) { A.rb->velocity -= P * m.invMassA; A.rb->angularVelocity -= m.invIA * glm::cross(m.rA[k], P); }
    if (B.rb) { B.rb->velocity += P * m.invMassB; B.rb->angularVelocity += m.invIB * glm::cross(m.rB[k], P); }
}

// Effective inverse mass of a contact along dir, from cached inverse inertia.
inline float EffectiveMass(const ContactManifold& m, int k, const glm::vec3& dir) {
    const glm::vec3 raxd = glm::cross(m.rA[k], dir), rbxd = glm::cross(m.rB[k], dir);
    return m.invMassA + m.invMassB
        + glm::dot(glm::cross(m.invIA * raxd, m.rA[k]) + glm::cross(m.invIB * rbxd, m.rB[k]), dir);
}

// Coupled effective inverse mass between contact points i and j along the normal: the change
// in point i's normal velocity caused by a unit normal impulse at point j (Kij; the diagonal
// Kii equals EffectiveMass). Symmetric. Used to build the 2x2 block-solver matrix.
inline float NormalCoupling(const ContactManifold& m, int i, int j) {
    const glm::vec3 n = m.normal;
    const glm::vec3 rAin = glm::cross(m.rA[i], n), rAjn = glm::cross(m.rA[j], n);
    const glm::vec3 rBin = glm::cross(m.rB[i], n), rBjn = glm::cross(m.rB[j], n);
    return m.invMassA + m.invMassB
        + glm::dot(rAin, m.invIA * rAjn) + glm::dot(rBin, m.invIB * rBjn);
}

// Prepare a manifold ONCE per step: wake fast-closing sleepers, cache inverse
// mass/inertia + contact offsets + effective masses + friction basis, capture the
// restitution target, and warm-start from last step's cached impulses.
void PrepareManifold(ContactManifold& m, Body& A, Body& B,
                     float restitutionThreshold,
                     const FlatU64Map<ContactCache>& cache) {
    const glm::vec3 n = m.normal;
    BuildTangents(n, m.tangent1, m.tangent2);

    // Cache the constants used every iteration (built once here).
    m.invMassA = invMassOf(A); m.invMassB = invMassOf(B);
    m.invIA = InvIWorld(A);     m.invIB = InvIWorld(B);
    // Pass-4 material combine. Static/dynamic coefficients are mixed with the pair's friction
    // combine mode (default GeometricMean == the legacy sqrt(a*b)); `m.friction` keeps the legacy
    // single value for the block solver's box clamp. ResolvedStatic/DynamicFriction fall back to
    // the collider's legacy `friction` when the material fields are left at 0, so old scenes are
    // numerically unchanged (static == dynamic == sqrt(fa*fb)).
    {
        const ecs::MaterialCombine mode =
            (A.c->frictionCombine == ecs::MaterialCombine::GeometricMean) ? B.c->frictionCombine
                                                                          : A.c->frictionCombine;
        m.staticFriction  = ecs::CombineMaterial(A.c->ResolvedStaticFriction(),
                                                 B.c->ResolvedStaticFriction(),  mode);
        m.dynamicFriction = ecs::CombineMaterial(A.c->ResolvedDynamicFriction(),
                                                 B.c->ResolvedDynamicFriction(), mode);
        m.friction        = m.dynamicFriction;  // block solver's per-axis box uses the slip limit
    }

    const glm::vec3 cA = BodyCenter(A);   // arms measured from the centre of mass (Pass-3)
    const glm::vec3 cB = BodyCenter(B);
    const ecs::MaterialCombine restMode =
        (A.c->restitutionCombine == ecs::MaterialCombine::GeometricMean) ? B.c->restitutionCombine
                                                                         : A.c->restitutionCombine;
    // Default GeometricMean of two 0-restitution surfaces is 0, matching the old min() for the
    // common non-bouncy case; Maximum lets a single bouncy surface dominate (Unity's default).
    const float e0 = ecs::CombineMaterial(A.c->restitution, B.c->restitution, restMode);

    const ContactCache* seed = cache.find(m.key);   // FlatU64Map::find returns ptr or nullptr
    // Restitution fires only on a fresh impact, never on a resting contact. m.restingContact is
    // set from a per-pair contact-age timer (see Step): a pair touching for several steps is
    // resting, so any closing speed now is the solver's own penetration-recovery noise, not a
    // real impact -- bouncing it re-injects energy and the stack never settles. The timer
    // survives the 1-frame separations restitution itself causes (unlike a raw cache check),
    // while a genuinely bouncing body -- airborne between hits for many frames -- ages out and
    // still bounces.
    const bool persistentContact = m.restingContact;

    for (int k = 0; k < m.count; ++k) {
        const glm::vec3 p  = m.points[k];
        m.rA[k] = p - cA; m.rB[k] = p - cB;

        // Effective masses (constant during the velocity solve).
        const float kN = EffectiveMass(m, k, n);
        m.normalMass[k] = (kN > 1e-12f) ? 1.0f / kN : 0.0f;
        const glm::vec3 axes[2] = { m.tangent1, m.tangent2 };
        for (int ax = 0; ax < 2; ++ax) {
            const float kT = EffectiveMass(m, k, axes[ax]);
            m.tangentMass[k][ax] = (kT > 1e-12f) ? 1.0f / kT : 0.0f;
        }

        const glm::vec3 relVel = (velOf(B) + glm::cross(AngVelOf(B), m.rB[k]))
                               - (velOf(A) + glm::cross(AngVelOf(A), m.rA[k]));
        const float vn = glm::dot(relVel, n);
        m.restBias[k] = (!persistentContact && -vn > restitutionThreshold) ? (-e0 * vn) : 0.0f;

        // Warm start: inherit impulses from the nearest cached point of this pair.
        float Pn = 0.0f, Pt1 = 0.0f, Pt2 = 0.0f;
        if (seed) {
            float best = 0.04f * 0.04f; int bi = -1;   // 4cm match tolerance
            for (int c = 0; c < seed->count; ++c) {
                const glm::vec3 d = seed->pts[c].point - p;
                const float d2 = glm::dot(d, d);
                if (d2 < best) { best = d2; bi = c; }
            }
            if (bi >= 0) { Pn = seed->pts[bi].normalImpulse;
                           Pt1 = seed->pts[bi].tangentImpulse[0];
                           Pt2 = seed->pts[bi].tangentImpulse[1]; }
        }
        m.normalImpulse[k] = Pn;
        m.tangentImpulse[k][0] = Pt1;
        m.tangentImpulse[k][1] = Pt2;
        ApplyImpulse(m, A, B, k, Pn * n + Pt1 * m.tangent1 + Pt2 * m.tangent2);
    }

    // Precompute the 2x2 coupled normal mass for the block solver (count == 2 only). Skip the
    // block path if the two points are (near) linearly dependent (det ~ 0), which would make
    // the 2x2 solve ill-conditioned; the sequential path handles that safely.
    m.useBlockSolver = false;
    if (m.count == 2) {
        m.blockK11 = NormalCoupling(m, 0, 0);
        m.blockK22 = NormalCoupling(m, 1, 1);
        m.blockK12 = NormalCoupling(m, 0, 1);
        // Use the block solver only when the 2x2 system is well-conditioned: the determinant
        // must be a healthy fraction of the diagonal product (Box2D uses this same guard).
        // Ill-conditioned (nearly collinear) contacts fall back to the sequential solve.
        const float det = m.blockK11 * m.blockK22 - m.blockK12 * m.blockK12;
        m.useBlockSolver = (det > 1e-9f)
            && (m.blockK11 * m.blockK11 < 1000.0f * det);
    }
}

// One velocity-solver iteration: normal impulse (clamped >= 0) then two-axis
// Coulomb friction (clamped to mu * accumulated normal impulse). Uses only cached
// constants + cheap dot/cross products -- no mat3 rebuilds.
// Coupled 2-point normal solve (Box2D block solver). Solves both contact points' normal
// impulses simultaneously as a 2x2 LCP (x >= 0, K x + b >= 0, complementary) via the four
// standard case checks, so the two impulses stay balanced instead of the sequential solver
// giving the first-solved point priority -- which is the asymmetry that topples stacks.
void SolveNormalBlock2(ContactManifold& m, Body& A, Body& B) {
    const glm::vec3 n = m.normal;
    const float k11 = m.blockK11, k12 = m.blockK12, k22 = m.blockK22;
    const glm::vec2 a(m.normalImpulse[0], m.normalImpulse[1]);   // current accumulated impulses

    // Current relative normal velocities, minus the (restitution) bias target.
    const glm::vec3 rv0 = (velOf(B) + glm::cross(AngVelOf(B), m.rB[0]))
                        - (velOf(A) + glm::cross(AngVelOf(A), m.rA[0]));
    const glm::vec3 rv1 = (velOf(B) + glm::cross(AngVelOf(B), m.rB[1]))
                        - (velOf(A) + glm::cross(AngVelOf(A), m.rA[1]));
    glm::vec2 b(glm::dot(rv0, n) - m.restBias[0], glm::dot(rv1, n) - m.restBias[1]);
    // Convert to the "zero-impulse" velocities so we can solve for the new TOTAL impulse x.
    b.x -= k11 * a.x + k12 * a.y;
    b.y -= k12 * a.x + k22 * a.y;

    const float det = k11 * k22 - k12 * k12;
    glm::vec2 x(0.0f);
    bool solved = false;
    // Case 1: both points active. x = -inv(K) b.
    x.x = -(k22 * b.x - k12 * b.y) / det;
    x.y = -(k11 * b.y - k12 * b.x) / det;
    if (x.x >= 0.0f && x.y >= 0.0f) solved = true;
    if (!solved) {                       // Case 2: only point 0 active.
        x.x = -b.x / k11; x.y = 0.0f;
        if (x.x >= 0.0f && (k12 * x.x + b.y) >= 0.0f) solved = true;
    }
    if (!solved) {                       // Case 3: only point 1 active.
        x.x = 0.0f; x.y = -b.y / k22;
        if (x.y >= 0.0f && (k12 * x.y + b.x) >= 0.0f) solved = true;
    }
    if (!solved) {                       // Case 4: neither active.
        x.x = 0.0f; x.y = 0.0f;
        if (b.x >= 0.0f && b.y >= 0.0f) solved = true;
    }
    if (!solved) return;                 // no valid case (degenerate) -- leave impulses as-is

    ApplyImpulse(m, A, B, 0, (x.x - a.x) * n);
    ApplyImpulse(m, A, B, 1, (x.y - a.y) * n);
    m.normalImpulse[0] = x.x;
    m.normalImpulse[1] = x.y;
}

void SolveManifoldVelocity(ContactManifold& m, Body& A, Body& B) {
    const glm::vec3 n = m.normal;
    const glm::vec3 axes[2] = { m.tangent1, m.tangent2 };

    // Normal solve. Two-point manifolds use the coupled block solver (balanced impulses ->
    // no toppling torque); other counts use the sequential solve. Either way, ALL normal
    // impulses are solved before ANY friction: interleaving lets an early friction guess bias
    // the normal solution and inject torque into a symmetric stack.
    if (m.useBlockSolver) {
        SolveNormalBlock2(m, A, B);
    } else {
        for (int k = 0; k < m.count; ++k) {
            const glm::vec3 relVel = (velOf(B) + glm::cross(AngVelOf(B), m.rB[k]))
                                   - (velOf(A) + glm::cross(AngVelOf(A), m.rA[k]));
            const float vn = glm::dot(relVel, n);
            float dPn = (-vn + m.restBias[k]) * m.normalMass[k];
            const float old = m.normalImpulse[k];
            m.normalImpulse[k] = std::max(old + dPn, 0.0f);
            dPn = m.normalImpulse[k] - old;
            ApplyImpulse(m, A, B, k, dPn * n);
        }
    }
    // Coulomb friction with a true 2-axis cone and static/dynamic coefficients (Pass-4). The two
    // tangent axes are solved as a coupled pair rather than two independent 1D boxes, so the
    // friction limit is a disc (|impulse| <= mu*Pn) not a square -- diagonal sliding no longer
    // gets sqrt(2) too much grip. While the accumulated tangent impulse stays inside the *static*
    // disc the contact sticks (holds its full tangential target); once it would exceed the static
    // limit the surface is sliding and the impulse is clamped to the smaller *dynamic* disc. With
    // the legacy defaults static == dynamic, so this reduces to the ordinary single-mu cone.
    for (int k = 0; k < m.count; ++k) {
        const float Pn        = m.normalImpulse[k];
        const float maxStatic = m.staticFriction  * Pn;
        const float maxSlip   = m.dynamicFriction * Pn;

        // Tentative new accumulated tangent impulse from this iteration's relative velocity.
        const glm::vec3 relVel = (velOf(B) + glm::cross(AngVelOf(B), m.rB[k]))
                               - (velOf(A) + glm::cross(AngVelOf(A), m.rA[k]));
        float t0 = m.tangentImpulse[k][0] - glm::dot(relVel, axes[0]) * m.tangentMass[k][0];
        float t1 = m.tangentImpulse[k][1] - glm::dot(relVel, axes[1]) * m.tangentMass[k][1];

        const float mag = std::sqrt(t0 * t0 + t1 * t1);
        if (mag > maxStatic && mag > 1e-12f) {   // exceeds the stick limit -> sliding: dynamic disc
            const float s = maxSlip / mag;
            t0 *= s; t1 *= s;
        }
        // else: within the static disc -> stick (keep the full tentative impulse).

        const float d0 = t0 - m.tangentImpulse[k][0];
        const float d1 = t1 - m.tangentImpulse[k][1];
        m.tangentImpulse[k][0] = t0;
        m.tangentImpulse[k][1] = t1;
        ApplyImpulse(m, A, B, k, d0 * axes[0] + d1 * axes[1]);
    }
}

// Pass-3 position solve for one manifold: a velocity-free, LINEAR, per-manifold Baumgarte
// correction. It translates the two bodies apart along the contact normal to remove
// penetration beyond the slop, WITHOUT touching linear/angular velocity -- so deep or
// freshly-spawned overlaps separate smoothly instead of injecting kinetic energy.
//
// Deliberately NOT rotating the bodies here: an angular pseudo-correction needs the contact
// arms recomputed from the updated transform each pass (Box2D does this), and reusing stale
// arms accumulates a tiny per-frame rotation that slowly topples stacks. Rotational settling
// is the velocity solver's job -- its per-point normal impulses already resist tipping using
// live relative velocities. Applied once per manifold (not per point) so a 4-point face
// contact, which shares a single penetration value, is not over-corrected 4x.
//
// `posApplied[0]` accumulates the separation resolved this step so repeated passes converge
// on (penetration - slop) instead of re-applying the full correction each pass. Returns the
// residual penetration so the caller can early-out.
float SolveManifoldPosition(ContactManifold& m, Body& A, Body& B,
                            float slop, float beta, float maxCorrection) {
    const float imA = m.invMassA, imB = m.invMassB;
    const float imSum = imA + imB;
    const float residual = m.penetration - m.posApplied[0];   // still-overlapping amount
    if (imSum <= 0.0f) return std::max(residual, 0.0f);
    // Push apart by a Baumgarte fraction of the residual beyond the slop, clamped per pass.
    const float moveMag = glm::clamp(beta * (residual - slop), 0.0f, maxCorrection);
    if (moveMag <= 0.0f) return std::max(residual, 0.0f);
    const glm::vec3 push = moveMag * m.normal;
    if (A.rb) {
        const glm::vec3 d = push * (imA / imSum);
        A.t->position -= d;
        if (A.owner && A.owner != A.t) A.owner->position -= d;
    }
    if (B.rb) {
        const glm::vec3 d = push * (imB / imSum);
        B.t->position += d;
        if (B.owner && B.owner != B.t) B.owner->position += d;
    }
    m.posApplied[0] += moveMag;
    return std::max(residual - moveMag, 0.0f);
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
    if (b.c->shape == ColliderShape::Capsule) {
        glm::vec3 p0, p1; CapsuleSegment(b, p0, p1);
        const glm::vec3 r(b.c->radius);
        return { glm::min(p0, p1) - r, glm::max(p0, p1) + r };
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
// Skew-symmetric matrix S with S*v == cross(r, v).
glm::mat3 Skew(const glm::vec3& r) {
    glm::mat3 s(0.0f);
    s[1][0] = -r.z; s[2][0] =  r.y;
    s[0][1] =  r.z; s[2][1] = -r.x;
    s[0][2] = -r.y; s[1][2] =  r.x;
    return s;
}
// World-space inverse inertia of a body (0 if static/asleep/frozen).
glm::mat3 JointInvI(const Transform& t, const RigidBody* rb) {
    if (!rb || rb->sleeping) return glm::mat3(0.0f);
    const glm::mat3 R = glm::mat3_cast(t.rotation);
    return R * rb->invInertiaLocal * glm::transpose(R);
}

// Resolve one joint's world anchor points and bodies. anchorA = posA + Ra*localA;
// the B end is either posB + Rb*localB or the fixed world 'anchor'.
struct JointEnds {
    Transform* ta = nullptr; RigidBody* ra = nullptr; glm::vec3 pA{0.0f}, rA{0.0f};
    Transform* tb = nullptr; RigidBody* rb = nullptr; glm::vec3 pB{0.0f}, rB{0.0f};
    bool ok = false;
};
JointEnds ResolveEnds(ecs::Registry& reg, const Joint& j) {
    JointEnds e;
    if (!reg.Has<Transform>(j.a)) return e;
    e.ta = &reg.Get<Transform>(j.a); e.ra = reg.TryGet<RigidBody>(j.a);
    e.rA = glm::mat3_cast(e.ta->rotation) * j.localA;
    e.pA = e.ta->position + e.rA;
    if (j.b == ecs::kNull) { e.pB = j.anchor; }
    else {
        if (!reg.Has<Transform>(j.b)) return e;
        e.tb = &reg.Get<Transform>(j.b); e.rb = reg.TryGet<RigidBody>(j.b);
        e.rB = glm::mat3_cast(e.tb->rotation) * j.localB;
        e.pB = e.tb->position + e.rB;
    }
    e.ok = true;
    return e;
}
glm::vec3 AnchorVel(const RigidBody* rb, const glm::vec3& r) {
    if (!rb || rb->sleeping) return glm::vec3(0.0f);
    return rb->velocity + glm::cross(rb->angularVelocity, r);
}

// Spring joint: Hooke + damping force applied AT the anchor points (so an
// off-centre spring also torques the body).
void ApplySpring(ecs::Registry& reg, const Joint& j) {
    JointEnds e = ResolveEnds(reg, j);
    if (!e.ok) return;
    const glm::vec3 d = e.pB - e.pA;
    const float len = glm::length(d);
    if (len < 1e-6f) return;
    const glm::vec3 n = d / len;
    const float vrel = glm::dot(AnchorVel(e.rb, e.rB) - AnchorVel(e.ra, e.rA), n);
    const float fMag = j.stiffness * (len - j.restLength) + j.damping * vrel;
    const glm::vec3 fOnA = fMag * n;   // toward B when stretched
    if (e.ra && e.ra->invMass > 0.0f) {
        e.ra->accumForce += fOnA; e.ra->accumTorque += glm::cross(e.rA, fOnA);
        e.ra->sleeping = false; e.ra->sleepTimer = 0.0f;
    }
    if (e.rb && e.rb->invMass > 0.0f) {
        e.rb->accumForce -= fOnA; e.rb->accumTorque += glm::cross(e.rB, -fOnA);
        e.rb->sleeping = false; e.rb->sleepTimer = 0.0f;
    }
}

// Perpendicular unit vector to v.
glm::vec3 PerpAxis(const glm::vec3& v) {
    const glm::vec3 a = (std::fabs(v.x) < 0.9f) ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
    return glm::normalize(glm::cross(v, a));
}

// Stable signed hinge angle about axisA, from the reference relative orientation captured at
// joint creation (avoids Euler extraction; handles wrap-around).
float HingeAngle(const JointEnds& e, const Joint& j) {
    // A relative to B, where a world anchor (no B body) contributes identity -- so a
    // world-anchored door still has a real hinge angle (the limit needs it).
    const glm::quat rotB = e.tb ? e.tb->rotation : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    const glm::quat rel = glm::inverse(rotB) * e.ta->rotation;
    const glm::quat delta = glm::normalize(glm::inverse(j.referenceRelative) * rel);
    const glm::vec3 axis = glm::normalize(j.axisA);
    float angle = 2.0f * std::atan2(glm::dot(glm::vec3(delta.x, delta.y, delta.z), axis), delta.w);
    while (angle >  glm::pi<float>()) angle -= glm::two_pi<float>();
    while (angle < -glm::pi<float>()) angle += glm::two_pi<float>();
    return angle;
}

// ---------------------------------------------------------------------------------------------
// Pass-3 joints: persistent, warm-started constraints. Each joint supports WarmStart (apply the
// impulses accumulated last frame -- what lets chains settle without stretch), SolveVelocity
// (pure velocity, no position bias, accumulating impulses; includes hinge motor + limits), and
// SolvePosition (a separate Baumgarte pass that removes anchor/length error without injecting
// kinetic energy). Impulses persist on the Joint across frames.
// ---------------------------------------------------------------------------------------------

// Wake both ends (called when a joint is active, so a jointed body can't sleep while its
// partner moves -- island wake propagation also covers this, but joints may connect bodies
// that never touch).
void WakeJointEnds(JointEnds& e) {
    if (e.ra && e.ra->sleeping) { e.ra->sleeping = false; e.ra->sleepTimer = 0.0f; }
    if (e.rb && e.rb->sleeping) { e.rb->sleeping = false; e.rb->sleepTimer = 0.0f; }
}

void WarmStartJoint(ecs::Registry& reg, Joint& j) {
    if (j.type == Joint::Type::Spring || j.broken) return;
    JointEnds e = ResolveEnds(reg, j);
    if (!e.ok) return;
    const glm::mat3 IA = JointInvI(*e.ta, e.ra);
    const glm::mat3 IB = e.tb ? JointInvI(*e.tb, e.rb) : glm::mat3(0.0f);
    const float imA = e.ra ? e.ra->invMass : 0.0f;
    const float imB = e.rb ? e.rb->invMass : 0.0f;
    glm::vec3 P(0.0f);
    if (j.type == Joint::Type::Distance) {
        const glm::vec3 d = e.pB - e.pA; const float len = glm::length(d);
        if (len > 1e-6f) P = j.distanceImpulse * (d / len);
    } else {
        P = j.pointImpulse;                              // ball + hinge share the anchor impulse
    }
    if (e.ra) { e.ra->velocity -= P * imA; e.ra->angularVelocity -= IA * glm::cross(e.rA, P); }
    if (e.rb) { e.rb->velocity += P * imB; e.rb->angularVelocity += IB * glm::cross(e.rB, P); }
    if (j.type == Joint::Type::Hinge) {
        const glm::vec3 aA = glm::normalize(glm::mat3_cast(e.ta->rotation) * j.axisA);
        const glm::vec3 p0 = PerpAxis(aA), p1 = glm::cross(aA, p0);
        const glm::vec3 L = j.axisImpulse.x * p0 + j.axisImpulse.y * p1
                          + (j.limitImpulse + j.motorImpulse) * aA;
        if (e.ra) e.ra->angularVelocity -= IA * L;
        if (e.rb) e.rb->angularVelocity += IB * L;
    }
}

void SolveJointVelocity(ecs::Registry& reg, Joint& j, float dt) {
    if (j.type == Joint::Type::Spring || j.broken) return;
    JointEnds e = ResolveEnds(reg, j);
    if (!e.ok) return;
    WakeJointEnds(e);
    const glm::mat3 IA = JointInvI(*e.ta, e.ra);
    const glm::mat3 IB = e.tb ? JointInvI(*e.tb, e.rb) : glm::mat3(0.0f);
    const glm::mat3 Isum = IA + IB;
    const float imA = e.ra ? e.ra->invMass : 0.0f;
    const float imB = e.rb ? e.rb->invMass : 0.0f;

    if (j.type == Joint::Type::Distance) {
        const glm::vec3 d = e.pB - e.pA; const float len = glm::length(d);
        if (len < 1e-6f) return;
        const glm::vec3 n = d / len;
        const glm::vec3 raxn = glm::cross(e.rA, n), rbxn = glm::cross(e.rB, n);
        const float k = imA + imB + glm::dot(glm::cross(IA * raxn, e.rA) + glm::cross(IB * rbxn, e.rB), n);
        if (k <= 0.0f) return;
        const float vrel = glm::dot(AnchorVel(e.rb, e.rB) - AnchorVel(e.ra, e.rA), n);
        float dP = -vrel / k;
        const float old = j.distanceImpulse;
        j.distanceImpulse = j.rope ? std::min(old + dP, 0.0f) : (old + dP);   // rope resists stretch only
        dP = j.distanceImpulse - old;
        const glm::vec3 P = dP * n;
        if (e.ra) { e.ra->velocity -= P * imA; e.ra->angularVelocity -= IA * glm::cross(e.rA, P); }
        if (e.rb) { e.rb->velocity += P * imB; e.rb->angularVelocity += IB * glm::cross(e.rB, P); }
        return;
    }

    // Point-to-point anchor (ball + hinge): coupled 3x3, warm-started.
    {
        const glm::mat3 sA = Skew(e.rA), sB = Skew(e.rB);
        const glm::mat3 K = glm::mat3(imA + imB) - sA * IA * sA - sB * IB * sB;
        if (std::fabs(glm::determinant(K)) > 1e-9f) {
            const glm::vec3 P = glm::inverse(K) * (-(AnchorVel(e.rb, e.rB) - AnchorVel(e.ra, e.rA)));
            j.pointImpulse += P;
            if (e.ra) { e.ra->velocity -= P * imA; e.ra->angularVelocity -= IA * glm::cross(e.rA, P); }
            if (e.rb) { e.rb->velocity += P * imB; e.rb->angularVelocity += IB * glm::cross(e.rB, P); }
        }
    }
    if (j.type != Joint::Type::Hinge) return;

    const glm::vec3 aA = glm::normalize(glm::mat3_cast(e.ta->rotation) * j.axisA);
    const glm::vec3 perp[2] = { PerpAxis(aA), glm::cross(aA, PerpAxis(aA)) };
    for (int i = 0; i < 2; ++i) {                        // remove the two perpendicular angular DOF
        const glm::vec3 t = perp[i];
        const float k = glm::dot(t, Isum * t); if (k < 1e-9f) continue;
        const glm::vec3 wA = e.ra ? e.ra->angularVelocity : glm::vec3(0.0f);
        const glm::vec3 wB = e.rb ? e.rb->angularVelocity : glm::vec3(0.0f);
        const float dP = -glm::dot(wB - wA, t) / k;
        j.axisImpulse[i] += dP;
        const glm::vec3 L = dP * t;
        if (e.ra) e.ra->angularVelocity -= IA * L;
        if (e.rb) e.rb->angularVelocity += IB * L;
    }
    const float kAxis = glm::dot(aA, Isum * aA);
    if (j.motorEnabled && kAxis > 1e-9f) {               // velocity motor, clamped to maxTorque*dt
        const glm::vec3 wA = e.ra ? e.ra->angularVelocity : glm::vec3(0.0f);
        const glm::vec3 wB = e.rb ? e.rb->angularVelocity : glm::vec3(0.0f);
        const float cdot = glm::dot(wB - wA, aA) - j.motorTargetVelocity;
        const float maxP = std::max(j.motorMaxTorque * dt, 0.0f);
        const float old = j.motorImpulse;
        j.motorImpulse = glm::clamp(old - cdot / kAxis, -maxP, maxP);
        const glm::vec3 L = (j.motorImpulse - old) * aA;
        if (e.ra) e.ra->angularVelocity -= IA * L;
        if (e.rb) e.rb->angularVelocity += IB * L;
    }
    if (j.angularLimit && kAxis > 1e-9f) {               // unilateral angular limit
        const float angle = HingeAngle(e, j);
        // atMin: below the lower limit -> may only PUSH the angle UP (impulse increases dtheta).
        // atMax: above the upper limit -> may only push DOWN. dtheta = d(hinge angle)/dt.
        const bool atMin = angle <= j.minAngle, atMax = angle >= j.maxAngle;
        if (atMin || atMax) {
            const glm::vec3 wA = e.ra ? e.ra->angularVelocity : glm::vec3(0.0f);
            const glm::vec3 wB = e.rb ? e.rb->angularVelocity : glm::vec3(0.0f);
            const float dtheta = glm::dot(wA - wB, aA);
            float lambda = dtheta / kAxis;               // impulse (about aA) that zeroes dtheta
            const float old = j.limitImpulse;
            // At the lower limit only negative accumulated impulse is allowed (pushes angle up);
            // at the upper limit only positive. This lets the joint leave the limit freely.
            j.limitImpulse = atMin ? std::min(old + lambda, 0.0f) : std::max(old + lambda, 0.0f);
            const glm::vec3 L = (j.limitImpulse - old) * aA;
            if (e.ra) e.ra->angularVelocity -= IA * L;
            if (e.rb) e.rb->angularVelocity += IB * L;
        } else j.limitImpulse = 0.0f;
    }
}

// Separate position pass: remove anchor / length error by translating the bodies (velocity-free,
// so no energy injection), plus a modest angular correction for the hinge axis alignment and
// limit. beta is the Baumgarte fraction.
void SolveJointPosition(ecs::Registry& reg, Joint& j) {
    if (j.type == Joint::Type::Spring || j.broken) return;
    JointEnds e = ResolveEnds(reg, j);
    if (!e.ok) return;
    const float imA = e.ra ? e.ra->invMass : 0.0f;
    const float imB = e.rb ? e.rb->invMass : 0.0f;
    const float imSum = imA + imB;
    constexpr float beta = 0.2f;

    if (j.type == Joint::Type::Distance) {
        const glm::vec3 d = e.pB - e.pA; const float len = glm::length(d);
        if (len < 1e-6f || imSum <= 0.0f) return;
        float C = len - j.restLength; if (j.rope && C < 0.0f) return;
        const glm::vec3 corr = (d / len) * (beta * C / imSum);
        if (e.ra)         e.ta->position += corr * imA;
        if (e.rb && e.tb) e.tb->position -= corr * imB;
    } else if (imSum > 0.0f) {
        const glm::vec3 C = e.pB - e.pA;                 // ball + hinge anchor coincidence
        const glm::vec3 corr = C * (beta / imSum);
        if (e.ra)         e.ta->position += corr * imA;
        if (e.rb && e.tb) e.tb->position -= corr * imB;
    }

    if (j.type == Joint::Type::Hinge) {
        // Angular correction of the RELATIVE orientation: keep the hinge axes aligned and pull
        // an exceeded angle back within [min,max]. Split between the two ends by which is
        // movable, so a world-anchored door (body A dynamic, B fixed) is corrected too. dRel is
        // the world rotation to apply to B relative to A; A takes the opposite share.
        const glm::vec3 aA = glm::normalize(glm::mat3_cast(e.ta->rotation) * j.axisA);
        const glm::vec3 aB = e.tb ? glm::normalize(glm::mat3_cast(e.tb->rotation) * j.axisB)
                                  : glm::normalize(j.axisB);
        glm::vec3 dRel = beta * glm::cross(aB, aA);      // aligns aB toward aA
        if (j.angularLimit) {
            const float angle = HingeAngle(e, j);
            const float target = glm::clamp(angle, j.minAngle, j.maxAngle);
            dRel += beta * (angle - target) * aA;        // B relative to A back toward the limit
        }
        const float wA = e.ra ? 1.0f : 0.0f, wB = e.rb ? 1.0f : 0.0f;
        const float ws = wA + wB;
        if (ws > 0.0f && glm::dot(dRel, dRel) > 1e-12f) {
            const auto rotate = [](Transform* t, const glm::vec3& dTheta) {
                const glm::quat dq(0.0f, dTheta.x, dTheta.y, dTheta.z);
                t->rotation = glm::normalize(t->rotation + 0.5f * dq * t->rotation);
            };
            if (e.ra) rotate(e.ta, -(wA / ws) * dRel);
            if (e.rb && e.tb) rotate(e.tb, (wB / ws) * dRel);
        }
    }
}

// Accumulated constraint impulse magnitude this step (for break thresholds).
float JointImpulseMagnitude(const Joint& j) {
    if (j.type == Joint::Type::Distance) return std::fabs(j.distanceImpulse);
    return glm::length(j.pointImpulse);
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
                 const glm::vec3& B, float r, glm::vec3& outN,
                 const std::vector<Entity>* candidates = nullptr) {
    float best = 1.0f; glm::vec3 bestN(0.0f);
    // Sweep against the CANONICAL world collider (Pass-1) so the collider's local
    // position/rotation/scale and inherited owner scale are respected exactly like discrete
    // collision and the scene queries -- previously CCD used the raw owner transform +
    // unscaled local dimensions and silently ignored those offsets.
    const auto testCandidate = [&](Entity e2, Transform& ownerT, Collider& localC) {
        if (e2 == self || localC.isTrigger) return;
        const physics::WorldCollider world = physics::BuildWorldCollider(ownerT, localC);
        const Transform& t2 = world.transform;
        const Collider& c2 = world.collider;
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
    };
    // Broad-phase candidates (from the caller) when available; otherwise the full scan.
    if (candidates) {
        for (Entity e2 : *candidates) {
            Transform* t = reg.TryGet<Transform>(e2);
            if (!t) continue;
            if (Collider* c = reg.TryGet<Collider>(e2))
                testCandidate(e2, *t, *c);
            if (AdditionalColliders* compound = reg.TryGet<AdditionalColliders>(e2))
                for (Collider& c : compound->values)
                    testCandidate(e2, *t, c);
        }
    } else {
        reg.view<Transform, Collider>().each(testCandidate);
        reg.view<Transform, AdditionalColliders>().each(
            [&](Entity e2, Transform& t, AdditionalColliders& compound) {
                for (Collider& c : compound.values)
                    testCandidate(e2, t, c);
            });
    }
    outN = bestN;
    return best;
}

float SweepRadiusOf(const Collider* c) {
    if (!c) return 0.1f;
    if (c->shape == ColliderShape::Sphere)  return c->radius;
    if (c->shape == ColliderShape::Box)     return glm::length(c->halfExtents);
    if (c->shape == ColliderShape::Capsule) return c->radius + c->halfHeight;
    return 0.1f;
}

} // namespace

void PhysicsWorld::Step(ecs::Registry& reg, float dt) {
    // Substepping (TGS-style): splitting the fixed step into several smaller solves is the
    // decisive fix for MISALIGNED stacks. A single large step lets the sequential-impulse
    // solver inject energy into tilted/rotating contacts (contacts shift within the step,
    // warm-started impulses land at stale points), which slowly blows a hand-placed 5+ box
    // stack apart even at restitution 0. Smaller steps keep contacts coherent within each
    // solve; headless tests: a 5-box misaligned stack goes from drift 3.3 (1 substep) to 0.05
    // (8 substeps). The broad/narrow phase re-runs per substep, so cost scales with the count.
    if (substeps > 1 && !m_inSubstep) {
        // Events are a public-frame contract, not an internal-substep contract. Preserve
        // the contact set seen by gameplay last frame, then classify the final contact
        // set against it. Otherwise an overlap first found in substep 1 is incorrectly
        // exposed as Stay by the final substep and OnTriggerEnter never fires.
        m_touchingBeforeFrame = m_touching;   // reuses the member's slab (no alloc after warmup)
        m_inSubstep = true;
        const float h = dt / static_cast<float>(substeps);
        for (int i = 0; i < substeps; ++i) Step(reg, h);
        m_inSubstep = false;

        m_finalContactEvents.clear();
        m_finalContactEvents.reserve(m_events.size());
        for (const CollisionEvent& event : m_events) {
            const std::uint64_t key = PairKey(event.a, event.b);
            if (m_touching.contains(key)) m_finalContactEvents[key] = event;
        }
        m_events.clear();
        m_events.reserve(m_touching.size() + m_touchingBeforeFrame.size());
        m_touching.for_each([&](std::uint64_t key, bool& trigger) {
            CollisionEvent event;
            if (const CollisionEvent* found = m_finalContactEvents.find(key)) event = *found;
            else { event.a = Entity(key >> 32); event.b = Entity(key & 0xFFFFFFFFu); }
            event.phase = m_touchingBeforeFrame.contains(key)
                ? CollisionEvent::Phase::Stay : CollisionEvent::Phase::Enter;
            event.trigger = trigger;
            m_events.push_back(event);
        });
        m_touchingBeforeFrame.for_each([&](std::uint64_t key, bool& trigger) {
            if (m_touching.contains(key)) return;
            CollisionEvent event;
            event.a = Entity(key >> 32);
            event.b = Entity(key & 0xFFFFFFFFu);
            event.phase = CollisionEvent::Phase::Exit;
            event.trigger = trigger;
            m_events.push_back(event);
        });
        // Deterministic dispatch order (Phase 44): this per-frame aggregation path also built
        // events from hash-map iteration; stable-sort by (a, b, phase) like the single-step path.
        std::sort(m_events.begin(), m_events.end(), [](const CollisionEvent& x, const CollisionEvent& y) {
            if (x.a != y.a) return x.a < y.a;
            if (x.b != y.b) return x.b < y.b;
            return static_cast<int>(x.phase) < static_cast<int>(y.phase);
        });
        return;
    }
    const auto kStepStart = std::chrono::steady_clock::now();
    auto bodyView = reg.view<Transform, RigidBody>();
    auto colliderView = reg.view<Transform, Collider>();
    auto compoundView = reg.view<Transform, AdditionalColliders>();

    // Pass-5 numerical safety (Phase 69): quarantine non-finite bodies BEFORE they reach the broad
    // phase (a NaN position would make the grid's floor(pos/cell) undefined) or the solver (a NaN
    // velocity propagates through every contact it touches). Zero the velocities, and reset a
    // corrupted position/rotation to a safe value so the body is merely dropped in place rather than
    // poisoning the whole simulation. Runs once per (sub)step; cheap single view pass.
    if (sanitizeState) {
        int quarantined = 0;
        const auto finite3 = [](const glm::vec3& v) {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };
        bodyView.each([&](Entity, Transform& t, RigidBody& rb) {
            bool bad = false;
            if (!finite3(rb.velocity))        { rb.velocity = glm::vec3(0.0f); bad = true; }
            if (!finite3(rb.angularVelocity)) { rb.angularVelocity = glm::vec3(0.0f); bad = true; }
            if (!finite3(rb.accumForce))      { rb.accumForce = glm::vec3(0.0f); bad = true; }
            if (!finite3(rb.accumTorque))     { rb.accumTorque = glm::vec3(0.0f); bad = true; }
            const float ql = glm::length(t.rotation);
            if (!std::isfinite(ql) || ql < 1e-6f) { t.rotation = glm::quat(1,0,0,0); bad = true; }
            if (!finite3(t.position)) {           // unrecoverable -- drop it at the origin, stop it dead
                t.position = glm::vec3(0.0f);
                rb.velocity = glm::vec3(0.0f); rb.angularVelocity = glm::vec3(0.0f);
                bad = true;
            }
            if (bad) ++quarantined;
        });
        m_stats.quarantinedBodies = quarantined;
    }
    if (bodyView.empty() && colliderView.empty() && m_touching.empty()) {
        m_events.clear();
        m_stats = PhysicsStats{};   // empty world: zero the simulation counters
        return;
    }
    // 0) Spring joints add forces before integration.
    for (const Joint& j : m_joints)
        if (j.type == Joint::Type::Spring) ApplySpring(reg, j);

    // 1) Integrate (semi-implicit Euler): velocity first, then position. A body
    //    with CCD enabled sweeps its motion and clamps to the first impact.
    m_stats.ccdSweeps = 0; m_stats.ccdSkipped = 0;   // per-(sub)step CCD budget counters
    bodyView.each([&](Entity e, Transform& t, RigidBody& rb) {
        if (rb.kinematic) {
            // Driven purely by its own (scripted/animated) velocity: no gravity,
            // forces, damping, CCD, or sleep -- but it moves and can push dynamics.
            t.position += rb.velocity * dt;
            if (!rb.freezeRotation && glm::dot(rb.angularVelocity, rb.angularVelocity) > 0.0f) {
                const glm::quat wq(0.0f, rb.angularVelocity.x, rb.angularVelocity.y, rb.angularVelocity.z);
                t.rotation = glm::normalize(t.rotation + 0.5f * wq * t.rotation * dt);
            }
            rb.accumForce = glm::vec3(0.0f); rb.accumTorque = glm::vec3(0.0f);
            rb.sleeping = false; rb.sleepTimer = 0.0f;
            return;
        }
        if (rb.invMass <= 0.0f) { rb.accumForce = glm::vec3(0.0f); rb.accumTorque = glm::vec3(0.0f); return; }
        if (rb.sleeping)          { rb.accumForce = glm::vec3(0.0f); rb.accumTorque = glm::vec3(0.0f); return; }
        glm::vec3 accel = rb.accumForce * rb.invMass;
        if (rb.useGravity) accel += gravity;
        rb.velocity  += accel * dt;
        rb.velocity  *= 1.0f / (1.0f + dt * std::max(rb.linearDamping, 0.0f));   // damp residual jitter

        glm::vec3 start = t.position;
        glm::vec3 end   = start + rb.velocity * dt;
        if (rb.ccd) {
            float r = SweepRadiusOf(reg.TryGet<Collider>(e));
            if (const AdditionalColliders* compound =
                    reg.TryGet<AdditionalColliders>(e)) {
                for (const Collider& collider : compound->values) {
                    // CCD sweeps one conservative sphere for the complete body. Include
                    // each child's offset so no authored lobe can tunnel independently.
                    r = std::max(r, glm::length(collider.localPosition)
                        + SweepRadiusOf(&collider));
                }
            }
            // Pass-5 early rejection (Phase 31): only sweep when the body moves far enough this step
            // to risk tunnelling (> ccdMotionThreshold * its sweep radius). Slow CCD bodies -- a
            // flagged crate sitting on the floor, a bullet at rest -- are resolved by the discrete
            // narrow phase and skip the expensive continuous sweep. Fast movers still get exact CCD,
            // so the tunnelling guarantee is preserved for the bodies that actually need it.
            const float sweepDist = glm::length(end - start);
            if (ccdMotionThreshold <= 0.0f || sweepDist > r * ccdMotionThreshold) {
                glm::vec3 n(0.0f);
                // Only test colliders whose cells overlap the swept sphere AABB, using the grid
                // from the previous step (valid here, before this step rebuilds it). Falls back
                // to the exact full scan when the grid is unavailable. m_ccdCandidates is reused
                // across bodies/steps so a fast-mover scene allocates nothing here.
                const std::vector<Entity>* ccdPtr = nullptr;
                if (m_broadphaseValid) {
                    const glm::vec3 mn = glm::min(start, end) - glm::vec3(r);
                    const glm::vec3 mx = glm::max(start, end) + glm::vec3(r);
                    if (GatherBroadphaseCandidates(mn, mx, m_ccdCandidates)) ccdPtr = &m_ccdCandidates;
                }
                const float toi = SweepToTOI(reg, e, start, end, r, n, ccdPtr);
                ++m_stats.ccdSweeps;
                if (toi < 1.0f) {
                    end = start + (end - start) * toi + n * 0.001f;
                    const float vn = glm::dot(rb.velocity, n);
                    if (vn < 0.0f) rb.velocity -= n * vn;
                }
            } else {
                ++m_stats.ccdSkipped;
            }
        }
        t.position    = end;

        // Angular: (re)compute inertia from the collider whenever the mass properties are
        // dirty (Phase 34: explicit dirty flag, not the old "is the tensor still zero?"
        // heuristic), then integrate the orientation from angular velocity (dq = 0.5 * w * q).
        // Inertia is derived from the CANONICAL scaled world collider so it matches the
        // collision shape exactly (Phase 36/37): a box with transform scale (4,1,1) gets
        // 4x1x1 inertia, and absolute scale means negative scale never yields negative
        // inertia. Static/kinematic bodies never reach here (they return above), so they do
        // no inertia work (Phase 38).
        if (!rb.freezeRotation && rb.massPropertiesDirty) {
            if (const Collider* col = reg.TryGet<Collider>(e)) {
                const physics::WorldCollider wc = physics::BuildWorldCollider(t, *col);
                // Density mass mode (Phase 55): derive mass from the material density * the
                // canonical world volume, so scaling a body or editing its collider keeps a
                // physically-plausible mass automatically. A zero/degenerate volume (plane, mesh)
                // leaves the authored invMass untouched. Manual mode skips this entirely.
                if (rb.massMode == RigidBody::MassMode::Density) {
                    const float vol = ShapeWorldVolume(wc.collider);
                    if (vol > 1e-9f && col->density > 0.0f) {
                        const float mass = col->density * vol;
                        rb.invMass = (mass > 0.0f) ? 1.0f / mass : rb.invMass;
                    }
                }
                rb.invInertiaLocal = InertiaFor(wc.collider, 1.0f / rb.invMass);
                // Auto COM: a single primitive's centre of mass is its collider offset in the
                // entity frame. Inertia (above) is already about that centre, so the two agree.
                if (rb.autoCenterOfMass) rb.centerOfMassLocal = col->localPosition;
                rb.massPropertiesDirty = false;
            }
        }
        if (rb.freezeRotation) {
            rb.angularVelocity = glm::vec3(0.0f);
        } else if (rb.invInertiaLocal != glm::mat3(0.0f)) {
            const glm::mat3 R     = glm::mat3_cast(t.rotation);
            const glm::mat3 invIw = R * rb.invInertiaLocal * glm::transpose(R);
            rb.angularVelocity += invIw * rb.accumTorque * dt;
            rb.angularVelocity *= 1.0f / (1.0f + dt * std::max(rb.angularDamping, 0.0f));  // stop rocking / rolling
            const glm::quat wq(0.0f, rb.angularVelocity.x, rb.angularVelocity.y, rb.angularVelocity.z);
            // Rotate ABOUT THE COM: capture the world COM, spin the orientation, then place the
            // origin so the COM is unmoved by the rotation. With centerOfMassLocal == 0 this is
            // exactly the old in-place spin (worldCOM == position), so default bodies are
            // unchanged; an offset COM makes the origin correctly orbit the physical centre.
            const glm::vec3 worldCOM = t.position + t.rotation * rb.centerOfMassLocal;
            t.rotation = glm::normalize(t.rotation + 0.5f * wq * t.rotation * dt);
            t.position = worldCOM - t.rotation * rb.centerOfMassLocal;
        }

        rb.accumForce  = glm::vec3(0.0f);
        rb.accumTorque = glm::vec3(0.0f);
    });

    // 2) Gather colliders into the reused body list.
    m_bodies.clear();
    m_worldColliderTransforms.clear();
    m_worldColliders.clear();
    std::size_t colliderCount = 0;
    colliderView.each([&](Entity, Transform&, Collider&) { ++colliderCount; });
    compoundView.each([&](Entity, Transform&, AdditionalColliders& colliders) {
        colliderCount += colliders.values.size();
    });
    m_bodies.reserve(colliderCount);
    m_worldColliderTransforms.reserve(colliderCount);
    m_worldColliders.reserve(colliderCount);
    // Does a cached static entry's cook inputs still match this collider? Compares every
    // field BuildWorldCollider / the broad phase / the narrow phase reads, so a match
    // guarantees the cooked world shape is byte-identical to a fresh cook. collisionDirty
    // forces a re-cook (mesh geometry may have changed under an unchanged path).
    const auto sameStaticInputs = [](const StaticColliderCache& e,
                                     const Transform& t, const Collider& c) -> bool {
        if (!e.valid || c.collisionDirty) return false;
        const Transform& lt = e.lastOwner; const Collider& lc = e.lastLocal;
        return lt.position == t.position && lt.rotation == t.rotation && lt.scale == t.scale
            && lc.shape == c.shape && lc.radius == c.radius && lc.halfExtents == c.halfExtents
            && lc.planeNormal == c.planeNormal && lc.planeOffset == c.planeOffset
            && lc.halfHeight == c.halfHeight && lc.majorRadius == c.majorRadius
            && lc.minorRadius == c.minorRadius && lc.steps == c.steps
            && lc.isTrigger == c.isTrigger
            && lc.localPosition == c.localPosition && lc.localRotation == c.localRotation
            && lc.localScale == c.localScale && lc.inheritTransformScale == c.inheritTransformScale
            && lc.collisionAssetPath == c.collisionAssetPath
            && lc.layer == c.layer && lc.mask == c.mask;
    };
    int staticRebuilt = 0, staticColliders = 0;
    colliderView.each([&](Entity e, Transform& ownerTransform, Collider& localCollider) {
        RigidBody* rigidBody = reg.TryGet<RigidBody>(e);
        // Static = no dynamic body: never moved by the solver, so its world shape only
        // changes when the author edits it. Kinematic/dynamic bodies move every step and
        // are always re-cooked.
        const bool isStatic = !rigidBody
            || (rigidBody->invMass <= 0.0f && !rigidBody->kinematic);
        physics::WorldCollider world;
        if (isStatic) {
            ++staticColliders;
            StaticColliderCache& entry = m_staticCache[e];
            entry.seen = true;
            if (sameStaticInputs(entry, ownerTransform, localCollider)) {
                world.transform = entry.worldTransform;   // reuse -- no re-cook
                world.collider  = entry.worldCollider;
            } else {
                world = physics::BuildWorldCollider(ownerTransform, localCollider);
                entry.valid = true;
                entry.lastOwner = ownerTransform; entry.lastLocal = localCollider;
                entry.worldTransform = world.transform; entry.worldCollider = world.collider;
                ++staticRebuilt;
            }
        } else {
            world = physics::BuildWorldCollider(ownerTransform, localCollider);
            if (world.collider.shape == ColliderShape::TriangleMesh
                && rigidBody->invMass > 0.0f && !rigidBody->kinematic) {
                static std::unordered_set<Entity> warned;
                if (warned.insert(e).second)
                    std::cerr << "[Physics] Dynamic TriangleMesh collider on entity " << e
                              << " is unsupported; using its bounds box. Use ConvexHull instead.\n";
                world.collider.shape = ColliderShape::Box;
                world.transform.scale = glm::vec3(1.0f);
            }
        }
        m_worldColliderTransforms.push_back(world.transform);
        m_worldColliders.push_back(world.collider);
        m_bodies.push_back(SolverBody{e, &m_worldColliderTransforms.back(),
            &m_worldColliders.back(), rigidBody, &ownerTransform});
    });
    // Compound children deliberately bypass the single-shape static cache: their
    // vector indices can change while authoring, and cooking these inexpensive
    // primitives is safer than serving an entry for the wrong child shape.
    compoundView.each([&](Entity e, Transform& ownerTransform,
                          AdditionalColliders& colliders) {
        RigidBody* rigidBody = reg.TryGet<RigidBody>(e);
        for (Collider& localCollider : colliders.values) {
            physics::WorldCollider world =
                physics::BuildWorldCollider(ownerTransform, localCollider);
            if (world.collider.shape == ColliderShape::TriangleMesh && rigidBody
                && rigidBody->invMass > 0.0f && !rigidBody->kinematic) {
                world.collider.shape = ColliderShape::Box;
                world.transform.scale = glm::vec3(1.0f);
            }
            m_worldColliderTransforms.push_back(world.transform);
            m_worldColliders.push_back(world.collider);
            m_bodies.push_back(SolverBody{e, &m_worldColliderTransforms.back(),
                &m_worldColliders.back(), rigidBody, &ownerTransform});
        }
    });
    // Prune cache entries for static colliders that vanished this step (deleted, or turned
    // dynamic/kinematic), so the map tracks the live set and cannot serve stale shapes.
    for (auto it = m_staticCache.begin(); it != m_staticCache.end(); ) {
        if (!it->second.seen) it = m_staticCache.erase(it);
        else { it->second.seen = false; ++it; }
    }
    m_stats.staticColliders    = staticColliders;
    m_stats.staticRebuiltThisStep = staticRebuilt;
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
    m_touchingNow.reserve(m_pairs.size());
    SolverBody* base = m_bodies.data();
    m_noCollisionJoints.clear();
    for (const Joint& joint : m_joints)
        if (!joint.collideConnected && joint.a != ecs::kNull && joint.b != ecs::kNull)
            m_noCollisionJoints[PairKey(joint.a, joint.b)] = 1;
    for (const auto& pr : m_pairs) {
        SolverBody* A = &m_bodies[pr.first];
        SolverBody* B = &m_bodies[pr.second];
        if (A->e == B->e) continue; // shapes in one compound never collide together
        if (priority(A->c->shape) > priority(B->c->shape)) std::swap(A, B);
        if (m_noCollisionJoints.contains(PairKey(A->e, B->e))) continue;
        // Collision layer/mask filter, gated by the global collision matrix (Pass-5) when active.
        // Inactive -> the original per-collider layer/mask test, byte-identical to before.
        if (m_layerMatrixActive) {
            const Collider& ca = *A->c; const Collider& cb = *B->c;
            if ((ca.mask & MatrixMaskOf(ca.layer) & cb.layer) == 0u
                || (cb.mask & MatrixMaskOf(cb.layer) & ca.layer) == 0u) continue;
        } else if (!LayersCollide(*A->c, *B->c)) {
            continue;
        }
        // Trigger volumes are often moved directly by gameplay systems (for example
        // the character-controller proxy) and therefore do not need a RigidBody.
        // They still require overlap detection even when both participants would
        // otherwise be classified as static/inactive.
        const bool triggerPair = A->c->isTrigger || B->c->isTrigger;
        if (!triggerPair && Inactive(*A) && Inactive(*B)) {
            const std::uint64_t key = PairKey(A->e, B->e);
            if (m_touching.contains(key))
                m_touchingNow[key] = false;
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
            m.count = ct.count;
            for (int pi = 0; pi < ct.count; ++pi) m.points[pi] = ct.points[pi];
            m.key = PairKey(A->e, B->e);
            m_manifolds.push_back(m);
        }
    }

    // 5) Emit Enter/Stay/Exit events by diffing against last step. Solid contacts
    //    carry contact point + normal now; the impulse is filled in after the solve.
    m_manifoldOf.clear();    // pair key -> manifold index
    m_manifoldOf.reserve(m_manifolds.size());
    for (int mi = 0; mi < static_cast<int>(m_manifolds.size()); ++mi)
        m_manifoldOf[m_manifolds[mi].key] = mi;
    m_eventOf.clear();       // pair key -> event index (solids)
    m_eventOf.reserve(m_touchingNow.size());

    m_events.clear();
    // Enter/Stay for every pair touching this step. Iteration order here is irrelevant -- the whole
    // event list is stable-sorted below, which is what makes dispatch deterministic (Phase 44).
    m_touchingNow.for_each([&](std::uint64_t key, bool& trigger) {
        const bool was = m_touching.contains(key);
        CollisionEvent ev;
        ev.a = Entity(key >> 32);
        ev.b = Entity(key & 0xFFFFFFFFu);
        ev.phase = was ? CollisionEvent::Phase::Stay : CollisionEvent::Phase::Enter;
        ev.trigger = trigger;
        const int* mit = m_manifoldOf.find(key);
        if (mit) {
            const ContactManifold& m = m_manifolds[*mit];
            ev.normal = m.normal;
            glm::vec3 avg(0.0f);
            for (int k = 0; k < m.count; ++k) avg += m.points[k];
            if (m.count > 0) avg /= static_cast<float>(m.count);
            ev.point = avg;
        }
        m_events.push_back(ev);
    });
    // Exit for every pair that was touching last step but not this one.
    m_touching.for_each([&](std::uint64_t key, bool& trigger) {
        if (!m_touchingNow.contains(key)) {
            CollisionEvent ev;
            ev.a = Entity(key >> 32);
            ev.b = Entity(key & 0xFFFFFFFFu);
            ev.phase = CollisionEvent::Phase::Exit;
            ev.trigger = trigger;
            m_events.push_back(ev);
        }
    });
    // Pass-5 determinism (Phase 44): the two loops above build m_events by iterating
    // unordered_maps, so delivery order depends on hash-bucket layout -- non-deterministic across
    // runs/platforms. Events are pure outputs (they never feed the solver), so a stable sort by
    // (a, b, phase) makes gameplay event dispatch order deterministic without touching simulation
    // results. m_eventOf (pair key -> event index, used to back-fill the impulse after the solve)
    // is rebuilt to match the new order.
    std::sort(m_events.begin(), m_events.end(), [](const CollisionEvent& x, const CollisionEvent& y) {
        if (x.a != y.a) return x.a < y.a;
        if (x.b != y.b) return x.b < y.b;
        return static_cast<int>(x.phase) < static_cast<int>(y.phase);
    });
    m_eventOf.clear();
    for (int ei = 0; ei < static_cast<int>(m_events.size()); ++ei) {
        const CollisionEvent& ev = m_events[ei];
        if (ev.phase != CollisionEvent::Phase::Exit && !ev.trigger)
            m_eventOf[PairKey(ev.a, ev.b)] = ei;   // solids: impulse back-filled after the solve
    }

    m_touching.swap(m_touchingNow);   // m_touching = this step; old cleared next step

    // 6) Velocity solve: warm-started, accumulating sequential impulses over the
    //    cached manifolds (normal + 2-axis Coulomb friction), plus the joints.
    const float wakeSpeed = sleepLinearVelocity * 2.0f;
    // Wake pass first: a fast-closing contact wakes either sleeper, so the prepare
    // pass below caches every body's inverse mass/inertia in its final awake state.
    for (ContactManifold& m : m_manifolds) {
        Body& A = m_bodies[m.a]; Body& B = m_bodies[m.b];
        if (glm::dot(velOf(B) - velOf(A), m.normal) < -wakeSpeed) { Wake(A); Wake(B); }
    }
    // Contact-age timer: age up pairs touching this step, age down (not reset) pairs that are
    // absent, and prune at zero. A pair that has been in contact for >= kRestingAge steps is
    // "resting" and suppresses restitution. Aging down rather than resetting means the brief
    // 1-frame separations a bounce causes don't re-arm restitution on a settling stack, while a
    // truly airborne body (absent many frames) still ages out and bounces on its next landing.
    constexpr int kRestingAge = 3, kAgeCap = 8;
    m_contactAge.for_each([](std::uint64_t, int& v){ v -= 1; });   // decay every tracked pair
    for (ContactManifold& m : m_manifolds) {
        int& age = m_contactAge[m.key];                       // inserts 0 for a brand-new pair
        age = std::min(age + 2, kAgeCap);                     // net +1 for a touched pair
        m.restingContact = (age >= kRestingAge);
    }
    // Prune faded pairs. Collect keys first (a member scratch, so no per-step allocation), then
    // erase -- erasing during for_each would backward-shift slots mid-iteration.
    m_ageScratch.clear();
    m_contactAge.for_each([&](std::uint64_t key, int& v){ if (v <= 0) m_ageScratch.push_back(key); });
    for (std::uint64_t key : m_ageScratch) m_contactAge.erase(key);
    // --- Build simulation islands. Union-find over dynamic bodies connected by contacts and
    //     joints; static / kinematic bodies are boundaries and are never merged, so a shared
    //     static floor cannot join two unrelated stacks into one island. Sleeping dynamic
    //     bodies ARE unioned (so waking one propagates through the whole connected group), but
    //     an island with no awake body is skipped by the solver entirely. ---
    const int numBodies = static_cast<int>(m_bodies.size());
    m_ufParent.resize(numBodies); m_ufRank.assign(numBodies, 0);
    for (int i = 0; i < numBodies; ++i) m_ufParent[i] = i;
    auto ufFind = [&](int x) { while (m_ufParent[x] != x) { m_ufParent[x] = m_ufParent[m_ufParent[x]]; x = m_ufParent[x]; } return x; };
    auto ufUnite = [&](int a, int b) {
        int ra = ufFind(a), rb = ufFind(b);
        if (ra == rb) return;
        if (m_ufRank[ra] < m_ufRank[rb]) std::swap(ra, rb);
        m_ufParent[rb] = ra;
        if (m_ufRank[ra] == m_ufRank[rb]) ++m_ufRank[ra];
    };
    auto isDyn = [&](int i) { const RigidBody* rb = m_bodies[i].rb; return rb && rb->invMass > 0.0f && !rb->kinematic; };

    for (const ContactManifold& m : m_manifolds)
        if (isDyn(m.a) && isDyn(m.b)) ufUnite(m.a, m.b);
    // Sleeping-sleeping pairs generate no manifold (narrow phase skips them), so a fully-asleep
    // stack would fragment into one island per box -- then disturbing the bottom box would wake
    // only it and leave the upper boxes floating. Union sleeping dynamic pairs from the broad
    // phase (their AABBs still overlap) so the stack stays one island and wakes as a unit.
    for (const auto& pr : m_pairs)
        if (isDyn(pr.first) && isDyn(pr.second)
            && m_bodies[pr.first].rb->sleeping && m_bodies[pr.second].rb->sleeping)
            ufUnite(pr.first, pr.second);
    m_entToIdx.clear(); m_entToIdx.reserve(static_cast<std::size_t>(numBodies));
    for (int i = 0; i < numBodies; ++i) m_entToIdx[m_bodies[i].e] = i;
    for (const Joint& j : m_joints) {
        const int* ia = m_entToIdx.find(j.a); const int* ib = m_entToIdx.find(j.b);
        if (ia && ib && isDyn(*ia) && isDyn(*ib)) ufUnite(*ia, *ib);
    }

    // Group into island slots (root -> slot); assign dynamic bodies, then their manifolds/joints.
    m_rootToIsland.clear();
    int islandN = 0;
    auto islandOf = [&](int bodyIdx) -> int {
        const int r = ufFind(bodyIdx);
        auto it = m_rootToIsland.find(r);
        if (it != m_rootToIsland.end()) return it->second;
        const int id = islandN++;
        m_rootToIsland[r] = id;
        if (static_cast<int>(m_islandBodies.size()) < islandN) {
            m_islandBodies.resize(islandN); m_islandManifolds.resize(islandN); m_islandJoints.resize(islandN);
        }
        m_islandBodies[id].clear(); m_islandManifolds[id].clear(); m_islandJoints[id].clear();
        return id;
    };
    m_entityIsland.clear();   // Pass-5: rebuild entity -> island map for the inspection accessors
    for (int i = 0; i < N; ++i)
        if (isDyn(i)) {
            const int isl = islandOf(i);
            m_islandBodies[isl].push_back(i);
            m_entityIsland[static_cast<std::uint64_t>(m_bodies[i].e)] = isl;
        }
    for (int mi = 0; mi < static_cast<int>(m_manifolds.size()); ++mi) {
        const ContactManifold& m = m_manifolds[mi];
        const int di = isDyn(m.a) ? m.a : (isDyn(m.b) ? m.b : -1);   // static-static skipped
        if (di >= 0) m_islandManifolds[islandOf(di)].push_back(mi);
    }
    for (int ji = 0; ji < static_cast<int>(m_joints.size()); ++ji) {
        const int* ia = m_entToIdx.find(m_joints[ji].a); const int* ib = m_entToIdx.find(m_joints[ji].b);
        int di = -1;
        if (ia && isDyn(*ia)) di = *ia;
        else if (ib && isDyn(*ib)) di = *ib;
        if (di >= 0) m_islandJoints[islandOf(di)].push_back(ji);
    }

    // Wake propagation: if ANY body in an island is awake, wake the whole island (so an impulse
    // or a new contact on one body wakes every body constrained to it). Islands with no awake
    // body stay asleep and are skipped below.
    m_islandAwake.assign(static_cast<std::size_t>(islandN), 0);   // reuses capacity (no per-step alloc)
    for (int isl = 0; isl < islandN; ++isl) {
        bool anyAwake = false;
        for (int bi : m_islandBodies[isl]) if (!m_bodies[bi].rb->sleeping) { anyAwake = true; break; }
        m_islandAwake[isl] = anyAwake ? 1 : 0;
        if (anyAwake) for (int bi : m_islandBodies[isl]) Wake(m_bodies[bi]);
    }

    // Prepare only manifolds in awake islands (sleeping islands are frozen). Manifolds touching
    // a static body live in the dynamic side's island; a manifold whose only dynamic side is in
    // a sleeping island is skipped with it.
    for (int isl = 0; isl < islandN; ++isl) {
        if (!m_islandAwake[isl]) continue;
        for (int mi : m_islandManifolds[isl])
            PrepareManifold(m_manifolds[mi], m_bodies[m_manifolds[mi].a], m_bodies[m_manifolds[mi].b],
                            restitutionThreshold, m_contactCache);
    }

    // Per-island velocity solve: each awake island is solved independently (structural
    // separation -- unrelated stacks never appear in the same constraint network).
    for (int isl = 0; isl < islandN; ++isl) {
        if (!m_islandAwake[isl]) continue;
        const auto& mans = m_islandManifolds[isl];
        const auto& jnts = m_islandJoints[isl];
        for (int ji : jnts) WarmStartJoint(reg, m_joints[ji]);   // apply last step's impulses once
        for (int iter = 0; iter < solverIterations; ++iter) {
            for (int mi : mans) SolveManifoldVelocity(m_manifolds[mi], m_bodies[m_manifolds[mi].a], m_bodies[m_manifolds[mi].b]);
            for (int ji : jnts) SolveJointVelocity(reg, m_joints[ji], dt);
        }
    }

    // 6b) Store this step's impulses for next step's warm start, and fill each
    //     solid collision event's impulse (sum of normal impulses = impact force).
    m_contactCache.clear();
    for (const ContactManifold& m : m_manifolds) {
        ContactCache cc;
        cc.count = m.count;
        float total = 0.0f;
        for (int k = 0; k < m.count; ++k) {
            cc.pts[k].point = m.points[k];
            cc.pts[k].normalImpulse = m.normalImpulse[k];
            cc.pts[k].tangentImpulse[0] = m.tangentImpulse[k][0];
            cc.pts[k].tangentImpulse[1] = m.tangentImpulse[k][1];
            total += m.normalImpulse[k];
        }
        m_contactCache[m.key] = cc;
        const int* eit = m_eventOf.find(m.key);        // O(1) instead of scanning m_events
        if (eit) m_events[*eit].impulse = total;
    }

    // 7) Split-impulse position solve: iterate a velocity-free position correction so
    //    penetration is removed without injecting kinetic energy. Early-outs once the
    //    deepest residual overlap is within the slop.
    float maxPenBefore = 0.0f;
    for (int isl = 0; isl < islandN; ++isl) {
        if (!m_islandAwake[isl]) continue;
        for (int mi : m_islandManifolds[isl]) {
            ContactManifold& m = m_manifolds[mi];
            maxPenBefore = std::max(maxPenBefore, m.penetration);
            for (int k = 0; k < m.count; ++k) m.posApplied[k] = 0.0f;   // reset per-step accumulator
        }
    }
    int posItersRun = 0;
    float maxPenAfter = 0.0f;
    // Each awake island runs its own position passes (with its own early-out), so a settled
    // island stops iterating while an active one keeps correcting.
    for (int isl = 0; isl < islandN; ++isl) {
        if (!m_islandAwake[isl]) continue;
        const auto& mans = m_islandManifolds[isl];
        const auto& jnts = m_islandJoints[isl];
        int run = 0;
        float worst = 0.0f;
        for (int it = 0; it < positionIterations; ++it) {
            worst = 0.0f;
            for (int mi : mans)
                worst = std::max(worst, SolveManifoldPosition(
                    m_manifolds[mi], m_bodies[m_manifolds[mi].a], m_bodies[m_manifolds[mi].b],
                    contactSlop, positionCorrectionBeta, maxPositionCorrection));
            for (int ji : jnts) SolveJointPosition(reg, m_joints[ji]);   // anchor/length/axis error
            ++run;
            if (worst < contactSlop) break;   // this island is settled (contacts)
        }
        maxPenAfter = std::max(maxPenAfter, worst);
        posItersRun = std::max(posItersRun, run);
    }
    m_stats.velocityIterations   = solverIterations;
    m_stats.positionIterations   = posItersRun;
    m_stats.maxPenetrationBefore = maxPenBefore;
    m_stats.maxPenetrationAfter  = maxPenAfter;

    // Joint break check (Pass-3, Phase 45): flag joints whose accumulated impulse this step
    // exceeded their threshold. Flagging (not erasing) is the safe deferred action -- a broken
    // joint is skipped by every solve stage; the editor/runtime removes it between steps.
    m_stats.distanceJoints = m_stats.ballJoints = m_stats.hingeJoints = 0;
    m_stats.motorsActive = m_stats.limitsActive = m_stats.brokenJoints = 0;
    for (Joint& j : m_joints) {
        if (!j.broken && j.breakImpulse > 0.0f && JointImpulseMagnitude(j) > j.breakImpulse)
            j.broken = true;
        switch (j.type) {
            case Joint::Type::Distance: ++m_stats.distanceJoints; break;
            case Joint::Type::Ball:     ++m_stats.ballJoints; break;
            case Joint::Type::Hinge:
                ++m_stats.hingeJoints;
                if (j.motorEnabled) ++m_stats.motorsActive;
                if (j.angularLimit) ++m_stats.limitsActive;
                break;
            default: break;
        }
        if (j.broken) ++m_stats.brokenJoints;
    }
    // Deferred removal: erase broken joints now that the solve/island lists are no longer in use
    // this step (next step rebuilds islands from scratch), so a snapped joint stops constraining.
    if (m_stats.brokenJoints > 0)
        m_joints.erase(std::remove_if(m_joints.begin(), m_joints.end(),
                                      [](const Joint& j) { return j.broken; }),
                       m_joints.end());
    { int cs = 0; for (const ContactManifold& m : m_manifolds) cs += m.count;
      m_stats.contactsSolved = cs; }

    // 8) Island-coherent sleep. An island sleeps only when EVERY one of its dynamic bodies has
    //    stayed below the sleep thresholds long enough; then they all sleep together (and wake
    //    together, via the propagation above). This replaces per-body sleeping, which could put
    //    one box in a stack to sleep while its neighbour was still settling.
    if (allowSleeping) {
        const float thresh2  = sleepLinearVelocity * sleepLinearVelocity;
        const float aThresh2 = sleepAngularVelocity * sleepAngularVelocity;
        for (int isl = 0; isl < islandN; ++isl) {
            if (!m_islandAwake[isl]) continue;               // already-asleep islands stay asleep
            const auto& bodies = m_islandBodies[isl];
            bool allSlow = true;
            for (int bi : bodies) {
                RigidBody* rb = m_bodies[bi].rb;
                if (!rb->allowSleep
                    || glm::dot(rb->velocity, rb->velocity) >= thresh2
                    || glm::dot(rb->angularVelocity, rb->angularVelocity) >= aThresh2) { allSlow = false; break; }
            }
            if (allSlow) {
                float minTimer = 1e30f;
                for (int bi : bodies) { RigidBody* rb = m_bodies[bi].rb; rb->sleepTimer += dt; minTimer = std::min(minTimer, rb->sleepTimer); }
                if (minTimer >= timeToSleep)                // whole island quiet long enough
                    for (int bi : bodies) { RigidBody* rb = m_bodies[bi].rb; rb->sleeping = true;
                        rb->velocity = glm::vec3(0.0f); rb->angularVelocity = glm::vec3(0.0f); }
            } else {
                for (int bi : bodies) m_bodies[bi].rb->sleepTimer = 0.0f;
            }
        }
    }

    // Island profiler counters.
    m_stats.islandCount = islandN;
    m_stats.awakeIslands = 0; m_stats.sleepingIslands = 0; m_stats.largestIslandBodies = 0;
    for (int isl = 0; isl < islandN; ++isl) {
        if (m_islandAwake[isl]) ++m_stats.awakeIslands; else ++m_stats.sleepingIslands;
        m_stats.largestIslandBodies = std::max(m_stats.largestIslandBodies,
                                               static_cast<int>(m_islandBodies[isl].size()));
    }

    // --- Pass-1 physics instrumentation (transient; never serialized). Populated from the
    //     step's already-computed persistent buffers so it adds no extra traversals. Note:
    //     gridRebuiltColliders currently equals the finite-collider count -- i.e. the whole
    //     static world is re-cooked and re-inserted every step -- which is the headline
    //     number later passes must drive toward zero for unchanged static scenery.
    {
        int staticC = 0, dynC = 0, kinC = 0, awake = 0, sleeping = 0, ccd = 0;
        for (const SolverBody& b : m_bodies) {
            const RigidBody* rb = b.rb;
            if (rb && rb->kinematic) { ++kinC; }
            else if (!rb || rb->invMass <= 0.0f) { ++staticC; }
            else {
                ++dynC;
                if (rb->sleeping) ++sleeping; else ++awake;
                if (rb->ccd) ++ccd;
            }
        }
        m_stats.colliders            = static_cast<int>(m_bodies.size());
        m_stats.staticColliders      = staticC;
        m_stats.dynamicBodies        = dynC;
        m_stats.kinematicBodies      = kinC;
        m_stats.awakeBodies          = awake;
        m_stats.sleepingBodies       = sleeping;
        m_stats.ccdBodies            = ccd;
        m_stats.candidatePairs       = static_cast<int>(m_pairs.size());
        m_stats.manifolds            = static_cast<int>(m_manifolds.size());
        m_stats.gridRebuiltColliders = static_cast<int>(m_finite.size());
        int occupied = 0;
        for (const auto& cell : m_grid) if (!cell.second.empty()) ++occupied;
        m_stats.occupiedGridCells = occupied;
        m_stats.stepMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - kStepStart).count();
    }
    // The grid + world colliders now describe the current world; scene queries may reuse
    // them until the next Step (or an editor InvalidateBroadphase()). Only meaningful when
    // the spatial grid was actually built (broadPhase on, >= 2 finite colliders).
    m_broadphaseValid = broadPhase && !m_finite.empty();
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

// Ray vs an upright capsule (segment p0..p1, radius r). Nearest surface entry
// across the two end spheres and the finite side cylinder. Returns t (>=0) or -1.
float RayCapsule(const glm::vec3& o, const glm::vec3& d,
                 const glm::vec3& p0, const glm::vec3& p1, float r, glm::vec3& outN) {
    float best = -1.0f; glm::vec3 bestN(0.0f);
    auto consider = [&](float t, const glm::vec3& n) {
        if (t >= 0.0f && (best < 0.0f || t < best)) { best = t; bestN = n; }
    };
    // End caps.
    { glm::vec3 n0; const float t0 = RaySphere(o, d, p0, r, n0); consider(t0, n0); }
    { glm::vec3 n1; const float t1 = RaySphere(o, d, p1, r, n1); consider(t1, n1); }

    // Finite side cylinder around axis va.
    glm::vec3 va = p1 - p0;
    const float L = glm::length(va);
    if (L > 1e-6f) {
        va /= L;
        const glm::vec3 dp = o - p0;
        const float dVa  = glm::dot(d, va);
        const float dpVa = glm::dot(dp, va);
        const float a = glm::dot(d, d) - dVa * dVa;
        const float b = glm::dot(d, dp) - dVa * dpVa;
        const float c = glm::dot(dp, dp) - dpVa * dpVa - r * r;
        if (std::fabs(a) > 1e-9f) {
            const float disc = b * b - a * c;
            if (disc >= 0.0f) {
                const float t = (-b - std::sqrt(disc)) / a;
                if (t >= 0.0f) {
                    const float along = dpVa + t * dVa;         // projection onto axis
                    if (along >= 0.0f && along <= L) {
                        const glm::vec3 hit = o + t * d;
                        const glm::vec3 axisPt = p0 + va * along;
                        glm::vec3 n = hit - axisPt;
                        const float nl = glm::length(n);
                        n = (nl > 1e-6f) ? n / nl : va;
                        consider(t, n);
                    }
                }
            }
        }
    }
    if (best < 0.0f) return -1.0f;
    outN = bestN;
    if (glm::dot(outN, d) > 0.0f) outN = -outN;
    return best;
}

} // namespace

void PhysicsWorld::RecordDebugTrace(const DebugTrace& trace) const {
    if (!m_debugTracing) return;
    constexpr std::size_t kMaxTraces = 512;
    if (m_debugTraces.size() >= kMaxTraces)
        m_debugTraces.erase(m_debugTraces.begin());
    m_debugTraces.push_back(trace);
}

bool PhysicsWorld::GatherBroadphaseCandidates(const glm::vec3& mn, const glm::vec3& mx,
                                              std::vector<ecs::Entity>& out) const {
    out.clear();
    const float cs = (cellSize > 1e-3f) ? cellSize : 1.0f;
    const int x0 = int(std::floor(mn.x / cs)), x1 = int(std::floor(mx.x / cs));
    const int y0 = int(std::floor(mn.y / cs)), y1 = int(std::floor(mx.y / cs));
    const int z0 = int(std::floor(mn.z / cs)), z1 = int(std::floor(mx.z / cs));
    // Bail on an unbounded / pathologically large region: the caller full-scans instead.
    const long long cellSpan = static_cast<long long>(x1 - x0 + 1)
        * static_cast<long long>(y1 - y0 + 1) * static_cast<long long>(z1 - z0 + 1);
    if (cellSpan < 0 || cellSpan > 200000) return false;

    // Pass-5: dedup body indices with a generation-stamp array instead of an unordered_set, whose
    // clear() frees every node and whose insert() re-allocates one per body -- ~one heap alloc per
    // body in the query region, on every query. The stamp vector is sized once to the body count
    // and "cleared" in O(1) by bumping m_queryGen, so a warmed-up query allocates nothing here.
    const int bodyCount = static_cast<int>(m_bodies.size());
    if (static_cast<int>(m_seenGen.size()) < bodyCount) m_seenGen.assign(bodyCount, 0);
    if (++m_queryGen == 0) { std::fill(m_seenGen.begin(), m_seenGen.end(), 0); m_queryGen = 1; }
    for (int ix = x0; ix <= x1; ++ix)
        for (int iy = y0; iy <= y1; ++iy)
            for (int iz = z0; iz <= z1; ++iz) {
                const auto it = m_grid.find(CellKey(ix, iy, iz));
                if (it == m_grid.end()) continue;
                for (int bi : it->second)
                    if (bi >= 0 && bi < bodyCount && m_seenGen[bi] != m_queryGen) {
                        m_seenGen[bi] = m_queryGen;
                        out.push_back(m_bodies[static_cast<std::size_t>(bi)].e);
                    }
            }
    // Infinite planes have no finite cell footprint and are never inserted into the grid,
    // so include them unconditionally -- a ray/sphere query can still hit a ground plane.
    for (int pi : m_planes)
        if (pi >= 0 && pi < bodyCount) out.push_back(m_bodies[static_cast<std::size_t>(pi)].e);
    // Compound colliders contribute several broad-phase bodies for one entity. Queries
    // visit every authored shape themselves, so return each owning entity only once.
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return true;
}

RaycastHit PhysicsWorld::Raycast(ecs::Registry& reg, const Ray& ray, float maxDistance,
                                 std::uint32_t layerMask, Entity ignored) const {
    RaycastHit best;
    best.distance = maxDistance;
    const auto finish = [&](RaycastHit result) {
        DebugTrace trace;
        trace.type = DebugTrace::Type::Ray;
        trace.start = ray.origin;
        const float directionLength = glm::length(ray.direction);
        const glm::vec3 direction = directionLength > 1.0e-6f
            ? ray.direction / directionLength : glm::vec3(0.0f);
        trace.end = ray.origin + direction * (result.hit ? result.distance : maxDistance);
        trace.hit = result.hit;
        trace.hitPoint = result.point;
        RecordDebugTrace(trace);
        return result;
    };
    const float len2 = glm::dot(ray.direction, ray.direction);
    if (len2 < 1e-12f) return finish(best);              // degenerate ray
    if (layerMask == 0u) return finish(best);
    const glm::vec3 d = ray.direction / std::sqrt(len2);

    ++m_stats.raycasts;
    auto colliderView = reg.view<Transform, Collider>();
    const auto testCandidate = [&](Entity e, Transform& ownerTransform, Collider& localCollider) {
        ++m_stats.queryCandidates;
        physics::WorldCollider world = physics::BuildWorldCollider(ownerTransform, localCollider);
        Transform& t = world.transform;
        Collider& c = world.collider;
        if (e == ignored || c.isTrigger) return;
        if ((c.layer & layerMask) == 0u) return;    // filtered out by the query mask
        if (std::isfinite(maxDistance)) {
            const float bound = ColliderBoundRadius(c);
            const glm::vec3 offset = t.position - ray.origin;
            const float reach = maxDistance + bound;
            if (glm::dot(offset, offset) > reach * reach) return;
        }
        ++m_stats.queryExactTests;
        glm::vec3 n(0.0f);
        float hitT = -1.0f;
        if ((c.shape == ColliderShape::TriangleMesh
             || c.shape == ColliderShape::ConvexHull)
            && !c.collisionAssetPath.empty()) {
            if (const auto mesh = physics::AcquireCollisionMesh(c.collisionAssetPath)) {
                float meshDistance = 0.0f;
                if (physics::RaycastCollisionMesh(*mesh, t, ray.origin, d,
                                                  best.distance, &meshDistance, &n))
                    hitT = meshDistance;
            }
        } else if (c.shape == ColliderShape::Sphere) {
            hitT = RaySphere(ray.origin, d, t.position, c.radius, n);
        } else if (c.shape == ColliderShape::Plane) {
            hitT = RayPlane(ray.origin, d, c.planeNormal, c.planeOffset, n);
        } else if (c.shape == ColliderShape::Capsule) {
            const glm::vec3 up = glm::mat3_cast(t.rotation) * glm::vec3(0.0f, 1.0f, 0.0f);
            const glm::vec3 h  = up * c.halfHeight;
            hitT = RayCapsule(ray.origin, d, t.position - h, t.position + h, c.radius, n);
        } else if (CompositeShape(c.shape)) {
            Body parent{e, &t, &c, nullptr};
            ProxySet proxies;
            BuildProxies(parent, proxies);
            for (int i = 0; i < proxies.count; ++i) {
                const Transform& pt = proxies.transforms[i];
                const Collider& pc = proxies.colliders[i];
                glm::vec3 pieceNormal(0.0f);
                float pieceT = -1.0f;
                if (pc.shape == ColliderShape::Sphere) {
                    pieceT = RaySphere(ray.origin, d, pt.position, pc.radius, pieceNormal);
                } else if (pc.shape == ColliderShape::Capsule) {
                    const glm::vec3 up = glm::mat3_cast(pt.rotation) * glm::vec3(0.0f, 1.0f, 0.0f);
                    const glm::vec3 h = up * pc.halfHeight;
                    pieceT = RayCapsule(ray.origin, d, pt.position - h, pt.position + h, pc.radius, pieceNormal);
                } else {
                    OBB piece; piece.center = pt.position;
                    const glm::mat3 R = glm::mat3_cast(pt.rotation);
                    piece.axis[0] = R[0]; piece.axis[1] = R[1]; piece.axis[2] = R[2];
                    piece.ext = pc.halfExtents;
                    pieceT = RayBox(ray.origin, d, piece, pieceNormal);
                }
                if (pieceT >= 0.0f && (hitT < 0.0f || pieceT < hitT)) {
                    hitT = pieceT;
                    n = pieceNormal;
                }
            }
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
    };
    const auto testEntityColliders = [&](Entity e, Transform& transform) {
        if (Collider* primary = reg.TryGet<Collider>(e))
            testCandidate(e, transform, *primary);
        if (AdditionalColliders* compound = reg.TryGet<AdditionalColliders>(e))
            for (Collider& collider : compound->values)
                testCandidate(e, transform, collider);
    };

    // Broad-phase: for a bounded ray, gather only colliders whose grid cells overlap the
    // ray-segment AABB (a complete superset), else fall back to the exact full scan. The
    // exact test above is identical either way, so results are unchanged.
    std::vector<Entity>& candidates = m_queryScratch;   // Pass-5: reused (see m_queryScratch note)
    bool accelerated = false;
    if (m_broadphaseValid && std::isfinite(maxDistance)) {
        const glm::vec3 a = ray.origin, b = ray.origin + d * maxDistance;
        if (GatherBroadphaseCandidates(glm::min(a, b), glm::max(a, b), candidates)) {
            accelerated = true;
            for (Entity e : candidates) {
                if (Transform* transform = reg.TryGet<Transform>(e))
                    testEntityColliders(e, *transform);
            }
        }
    }
    if (!accelerated) {
        colliderView.each(testCandidate);
        reg.view<Transform, AdditionalColliders>().each(
            [&](Entity e, Transform& transform, AdditionalColliders& compound) {
                // The primary view above already tested the first shape. This view
                // supplies every additional authored shape on the same owner.
                for (Collider& collider : compound.values)
                    testCandidate(e, transform, collider);
            });
    }

    if (!best.hit) best.distance = 0.0f;
    return finish(best);
}

RaycastHit PhysicsWorld::SphereCast(ecs::Registry& reg,
                                    const glm::vec3& start,
                                    const glm::vec3& end,
                                    float radius,
                                    Entity ignored,
                                    std::uint32_t layerMask,
                                    std::uint32_t queryLayer,
                                    Entity alsoIgnored) const {
    RaycastHit result;
    const glm::vec3 travel = end - start;
    const float distance = glm::length(travel);
    const auto finish = [&](RaycastHit value) {
        DebugTrace trace;
        trace.type = DebugTrace::Type::Sphere;
        trace.start = start;
        trace.end = end;
        trace.radius = std::max(radius, 0.0f);
        trace.hit = value.hit;
        trace.hitPoint = value.point;
        RecordDebugTrace(trace);
        return value;
    };
    if (distance < 1e-6f) return finish(result);
    if (layerMask == 0u) return finish(result);

    float bestToi = 1.0f;
    glm::vec3 bestNormal(0.0f);
    Entity bestEntity = ecs::kNull;
    const float sweepRadius = std::max(radius, 0.0f);
    const float travel2 = glm::dot(travel, travel);

    ++m_stats.sphereCasts;
    auto colliderView = reg.view<Transform, Collider>();
    const auto testCandidate = [&](Entity entity, Transform& ownerTransform, Collider& localCollider) {
        ++m_stats.queryCandidates;
        physics::WorldCollider world = physics::BuildWorldCollider(ownerTransform, localCollider);
        Transform& transform = world.transform;
        Collider& collider = world.collider;
        if (entity == ignored || entity == alsoIgnored || collider.isTrigger) return;
        if ((collider.layer & layerMask) == 0u) return;    // filtered out by the query mask
        if (queryLayer != 0u && (collider.mask & queryLayer) == 0u) return;
        const float bound = ColliderBoundRadius(collider);
        if (std::isfinite(bound)) {
            const float u = glm::clamp(
                glm::dot(transform.position - start, travel) / travel2,
                0.0f, 1.0f);
            const glm::vec3 closest = start + travel * u;
            const glm::vec3 offset = transform.position - closest;
            const float reach = sweepRadius + bound;
            if (glm::dot(offset, offset) > reach * reach) return;
        }

        float toi = 1.0f;
        glm::vec3 normal(0.0f);
        if (collider.shape == ColliderShape::Plane) {
            toi = SweptSpherePlane(start, end, sweepRadius,
                                   collider.planeNormal, collider.planeOffset, normal);
        } else if (collider.shape == ColliderShape::Sphere) {
            toi = SweptSphereSphere(start, end, sweepRadius,
                                    transform.position, collider.radius, normal);
        } else if ((collider.shape == ColliderShape::TriangleMesh
                    || collider.shape == ColliderShape::ConvexHull)
                   && !collider.collisionAssetPath.empty()) {
            if (const auto mesh = physics::AcquireCollisionMesh(collider.collisionAssetPath)) {
                float meshToi = 1.0f;
                if (physics::SweepSphereCollisionMesh(*mesh, transform, start, end,
                                                      sweepRadius, &meshToi, &normal))
                    toi = meshToi;
            }
        } else if (CompositeShape(collider.shape)) {
            Body parent{entity, &transform, &collider, nullptr};
            ProxySet proxies;
            BuildProxies(parent, proxies);
            for (int i = 0; i < proxies.count; ++i) {
                const Transform& proxyTransform = proxies.transforms[i];
                const Collider& proxyCollider = proxies.colliders[i];
                float proxyToi = 1.0f;
                glm::vec3 proxyNormal(0.0f);
                if (proxyCollider.shape == ColliderShape::Sphere) {
                    proxyToi = SweptSphereSphere(
                        start, end, sweepRadius, proxyTransform.position,
                        proxyCollider.radius, proxyNormal);
                } else {
                    OBB proxyBox;
                    proxyBox.center = proxyTransform.position;
                    const glm::mat3 rotation = glm::mat3_cast(proxyTransform.rotation);
                    proxyBox.axis[0] = rotation[0];
                    proxyBox.axis[1] = rotation[1];
                    proxyBox.axis[2] = rotation[2];
                    proxyBox.ext = proxyCollider.halfExtents;
                    proxyToi = SweptSphereBox(
                        start, end, sweepRadius, proxyBox, proxyNormal);
                }
                if (proxyToi < toi) {
                    toi = proxyToi;
                    normal = proxyNormal;
                }
            }
        } else {
            OBB box;
            box.center = transform.position;
            const glm::mat3 rotation = glm::mat3_cast(transform.rotation);
            box.axis[0] = rotation[0];
            box.axis[1] = rotation[1];
            box.axis[2] = rotation[2];
            box.ext = collider.halfExtents;
            if (collider.shape == ColliderShape::Capsule) {
                box.ext = glm::vec3(collider.radius,
                                    collider.radius + collider.halfHeight,
                                    collider.radius);
            }
            toi = SweptSphereBox(start, end, sweepRadius, box, normal);
        }

        if (toi < bestToi) {
            bestToi = toi;
            bestNormal = normal;
            bestEntity = entity;
        }
    };
    const auto testEntityColliders = [&](Entity e, Transform& transform) {
        if (Collider* primary = reg.TryGet<Collider>(e))
            testCandidate(e, transform, *primary);
        if (AdditionalColliders* compound = reg.TryGet<AdditionalColliders>(e))
            for (Collider& collider : compound->values)
                testCandidate(e, transform, collider);
    };

    // Broad-phase: gather colliders whose grid cells overlap the swept-sphere AABB (start
    // sphere U end sphere) -- a complete superset; otherwise fall back to the full scan.
    std::vector<Entity>& candidates = m_queryScratch;   // Pass-5: reused (see m_queryScratch note)
    bool accelerated = false;
    if (m_broadphaseValid) {
        const glm::vec3 mn = glm::min(start, end) - glm::vec3(sweepRadius);
        const glm::vec3 mx = glm::max(start, end) + glm::vec3(sweepRadius);
        if (GatherBroadphaseCandidates(mn, mx, candidates)) {
            accelerated = true;
            for (Entity e : candidates) {
                if (Transform* transform = reg.TryGet<Transform>(e))
                    testEntityColliders(e, *transform);
            }
        }
    }
    if (!accelerated) {
        colliderView.each(testCandidate);
        reg.view<Transform, AdditionalColliders>().each(
            [&](Entity e, Transform& transform, AdditionalColliders& compound) {
                for (Collider& collider : compound.values)
                    testCandidate(e, transform, collider);
            });
    }

    if (bestEntity == ecs::kNull) return finish(result);
    result.hit = true;
    result.entity = bestEntity;
    result.distance = distance * bestToi;
    result.point = start + travel * bestToi;
    result.normal = bestNormal;
    return finish(result);
}

RaycastHit PhysicsWorld::CapsuleCast(ecs::Registry& reg,
                                     const glm::vec3& start, const glm::vec3& end,
                                     float radius, float halfHeight, const glm::vec3& up,
                                     Entity ignored, std::uint32_t layerMask,
                                     std::uint32_t queryLayer, Entity alsoIgnored) const {
    ++m_stats.capsuleCasts;
    const float h = std::max(halfHeight, 0.0f);
    const glm::vec3 u = (glm::dot(up, up) > 1e-9f ? glm::normalize(up) : glm::vec3(0, 1, 0)) * h;
    // Sweep the capsule as its bottom-cap, centre and top-cap sphere paths; keep the earliest
    // (nearest) hit. The sub-casts do NOT bump the sphere-cast counter (report as one capsule
    // cast). travel length is shared, so the returned distance is comparable across sub-casts.
    RaycastHit best; float bestFrac = 1e30f;
    const float travelLen = glm::length(end - start);
    const int saveSphere = m_stats.sphereCasts;
    for (float o = -1.0f; o <= 1.0f; o += 1.0f) {
        const RaycastHit hh = SphereCast(reg, start + u * o, end + u * o, radius,
                                         ignored, layerMask, queryLayer, alsoIgnored);
        if (hh.hit) {
            const float frac = (travelLen > 1e-6f) ? hh.distance / travelLen : 0.0f;
            if (frac < bestFrac) { bestFrac = frac; best = hh; }
        }
    }
    m_stats.sphereCasts = saveSphere;   // count the whole thing as one capsule cast
    return best;
}

std::vector<ecs::Entity> PhysicsWorld::OverlapCapsule(ecs::Registry& reg,
                                                      const glm::vec3& center, float radius,
                                                      float halfHeight, const glm::vec3& up,
                                                      std::uint32_t layerMask) const {
    const float h = std::max(halfHeight, 0.0f);
    const glm::vec3 u = (glm::dot(up, up) > 1e-9f ? glm::normalize(up) : glm::vec3(0, 1, 0)) * h;
    // Union of the bottom-cap, centre and top-cap sphere overlaps (de-duplicated).
    std::vector<ecs::Entity> out;
    const int saveOverlaps = m_stats.overlaps;
    for (float o = -1.0f; o <= 1.0f; o += 1.0f) {
        for (Entity e : OverlapSphere(reg, center + u * o, radius, layerMask))
            if (std::find(out.begin(), out.end(), e) == out.end()) out.push_back(e);
    }
    m_stats.overlaps = saveOverlaps + 1;   // one capsule overlap
    return out;
}

bool PhysicsWorld::RaycastAny(ecs::Registry& reg, const Ray& ray, float maxDistance,
                              std::uint32_t layerMask, Entity ignored) const {
    // Closest-hit Raycast already tells us whether ANYTHING solid is on the ray; expose it as the
    // any-hit query (an early-out micro-optimisation is possible but the acceleration dominates).
    return Raycast(reg, ray, maxDistance, layerMask, ignored).hit;
}

std::vector<RaycastHit> PhysicsWorld::RaycastAll(ecs::Registry& reg, const Ray& ray,
                                                 float maxDistance, std::uint32_t layerMask) const {
    std::vector<RaycastHit> hits;
    const float len = glm::length(ray.direction);
    if (len < 1e-6f) return hits;
    const glm::vec3 d = ray.direction / len;
    glm::vec3 origin = ray.origin;
    float remaining = std::min(maxDistance, 1.0e30f);
    Entity last = ecs::kNull;
    // March the ray: closest hit, record it (as an absolute distance from the ORIGINAL origin),
    // then advance just past it and repeat. Skips re-hitting the same collider back-to-back.
    for (int i = 0; i < 64 && remaining > 1e-4f; ++i) {
        const RaycastHit h = Raycast(reg, Ray{origin, d}, remaining, layerMask, last);
        if (!h.hit) break;
        RaycastHit abs = h;
        abs.distance = glm::dot(h.point - ray.origin, d);
        hits.push_back(abs);
        const float step = h.distance + 0.01f;   // advance past this hit
        origin += d * step; remaining -= step; last = h.entity;
    }
    std::sort(hits.begin(), hits.end(),
              [](const RaycastHit& a, const RaycastHit& b) { return a.distance < b.distance; });
    return hits;
}

RaycastHit PhysicsWorld::BoxCast(ecs::Registry& reg, const glm::vec3& start, const glm::vec3& end,
                                 const glm::vec3& halfExtents, const glm::quat& /*rotation*/,
                                 Entity ignored, std::uint32_t layerMask) const {
    // Conservative bounding-sphere sweep (over-reports near corners). Rotation is unused by the
    // sphere approximation. An exact OBB sweep is a later refinement.
    const float r = glm::length(halfExtents);
    return SphereCast(reg, start, end, r, ignored, layerMask);
}

std::vector<ecs::Entity> PhysicsWorld::OverlapBox(ecs::Registry& reg, const glm::vec3& center,
                                                  const glm::vec3& halfExtents,
                                                  const glm::quat& rotation,
                                                  std::uint32_t layerMask) const {
    // Broad phase via the bounding-sphere overlap, then an AABB-vs-AABB refine against the query
    // OBB's world AABB (tighter than the bounding sphere; still conservative for oriented shapes).
    const glm::mat3 R = glm::mat3_cast(rotation);
    glm::vec3 h(0.0f);
    for (int i = 0; i < 3; ++i) h += glm::abs(R[i]) * halfExtents[i];
    const glm::vec3 qmn = center - h, qmx = center + h;
    const float boundR = glm::length(halfExtents);
    std::vector<ecs::Entity> out;
    for (Entity e : OverlapSphere(reg, center, boundR, layerMask)) {
        Transform* t = reg.TryGet<Transform>(e);
        Collider* c  = reg.TryGet<Collider>(e);
        if (!t || !c) continue;
        const physics::WorldCollider wc = physics::BuildWorldCollider(*t, *c);
        const float cb = ColliderBoundRadius(wc.collider);
        if (!std::isfinite(cb)) { out.push_back(e); continue; }   // plane: keep
        const glm::vec3 cmn = wc.transform.position - glm::vec3(cb);
        const glm::vec3 cmx = wc.transform.position + glm::vec3(cb);
        if (cmx.x >= qmn.x && cmn.x <= qmx.x && cmx.y >= qmn.y && cmn.y <= qmx.y
            && cmx.z >= qmn.z && cmn.z <= qmx.z)
            out.push_back(e);
    }
    return out;
}

bool PhysicsWorld::ComputePenetration(ecs::Registry& reg, const Transform& queryTransform,
                                      const Collider& queryCollider, Entity other,
                                      glm::vec3& outNormal, float& outDepth) const {
    Transform* ot = reg.TryGet<Transform>(other);
    Collider*  oc = reg.TryGet<Collider>(other);
    if (!ot || !oc) return false;
    physics::WorldCollider qw = physics::BuildWorldCollider(queryTransform, queryCollider);
    physics::WorldCollider ow = physics::BuildWorldCollider(*ot, *oc);
    // Reuse the full narrow phase in its canonical (priority-ordered) direction. Detect returns
    // the normal from A toward B; we re-orient it to point from `other` toward the query.
    Body q{ecs::kNull, &qw.transform, &qw.collider, nullptr};
    Body o{other,      &ow.transform, &ow.collider, nullptr};
    const bool swapped = priority(q.c->shape) > priority(o.c->shape);
    Body& A = swapped ? o : q;
    Body& B = swapped ? q : o;
    const Contact ct = Detect(A, B);
    if (!ct.hit) return false;
    // ct.normal is A->B. Query is B when swapped, A otherwise. We want other->query.
    outNormal = swapped ? ct.normal : -ct.normal;
    outDepth = ct.penetration;
    return true;
}

glm::vec3 PhysicsWorld::ClosestPoint(ecs::Registry& reg, const glm::vec3& point,
                                     Entity entity) const {
    Transform* t = reg.TryGet<Transform>(entity);
    Collider*  c = reg.TryGet<Collider>(entity);
    if (!t || !c) return point;
    const physics::WorldCollider w = physics::BuildWorldCollider(*t, *c);
    const Transform& wt = w.transform;
    const Collider&  wc = w.collider;
    switch (wc.shape) {
        case ColliderShape::Sphere: {
            const glm::vec3 d = point - wt.position;
            const float l = glm::length(d);
            return l > 1e-6f ? wt.position + d / l * std::min(l, wc.radius) : wt.position;
        }
        case ColliderShape::Capsule: {
            const glm::vec3 up = glm::mat3_cast(wt.rotation) * glm::vec3(0, 1, 0);
            const glm::vec3 a = wt.position - up * wc.halfHeight;
            const glm::vec3 ab = up * (2.0f * wc.halfHeight);
            const float denom = glm::dot(ab, ab);
            const float u = denom > 1e-9f ? glm::clamp(glm::dot(point - a, ab) / denom, 0.0f, 1.0f) : 0.0f;
            const glm::vec3 onSeg = a + ab * u;
            const glm::vec3 d = point - onSeg;
            const float l = glm::length(d);
            return l > 1e-6f ? onSeg + d / l * std::min(l, wc.radius) : onSeg;
        }
        case ColliderShape::Plane: {
            const float s = glm::dot(wc.planeNormal, point) - wc.planeOffset;
            return point - wc.planeNormal * s;
        }
        default: {   // Box (exact) + cylinder/cone/pyramid/hull/mesh (bounding-OBB approximation)
            const glm::mat3 R = glm::mat3_cast(wt.rotation);
            const glm::vec3 local = glm::transpose(R) * (point - wt.position);
            const glm::vec3 clamped = glm::clamp(local, -wc.halfExtents, wc.halfExtents);
            return wt.position + R * clamped;
        }
    }
}

std::uint64_t PhysicsWorld::StateHash(ecs::Registry& registry) const {
    struct Row { std::uint32_t e; float v[13]; int sleeping; };
    std::vector<Row> rows;
    registry.view<Transform, RigidBody>().each(
        [&](Entity e, Transform& t, RigidBody& rb) {
            rows.push_back(Row{ static_cast<std::uint32_t>(e),
                { t.position.x, t.position.y, t.position.z,
                  t.rotation.x, t.rotation.y, t.rotation.z, t.rotation.w,
                  rb.velocity.x, rb.velocity.y, rb.velocity.z,
                  rb.angularVelocity.x, rb.angularVelocity.y, rb.angularVelocity.z },
                rb.sleeping ? 1 : 0 });
        });
    // Sort by entity id so the digest never depends on ECS iteration order (Phase 44).
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b){ return a.e < b.e; });
    std::uint64_t h = 1469598103934665603ull;                       // FNV-1a offset basis
    const auto mix = [&h](std::uint64_t v){ h ^= v; h *= 1099511628211ull; };
    for (const Row& r : rows) {
        mix(r.e);
        for (float f : r.v) mix(static_cast<std::uint64_t>(std::llround(f * 10000.0f)));  // quantize 1e-4
        mix(static_cast<std::uint64_t>(r.sleeping));
    }
    return h;
}

std::vector<PhysicsWorld::ContactInfo> PhysicsWorld::ContactsForEntity(ecs::Entity e) const {
    // Scan the last step's cached manifolds for those touching `e`. Impulses were back-filled by
    // the solve, so normalImpulse is the impact force and frictionImpulse the tangential magnitude.
    std::vector<ContactInfo> out;
    for (const ContactManifold& m : m_manifolds) {
        if (m.a < 0 || m.b < 0 || m.a >= static_cast<int>(m_bodies.size())
            || m.b >= static_cast<int>(m_bodies.size())) continue;
        const ecs::Entity ea = m_bodies[m.a].e, eb = m_bodies[m.b].e;
        if (ea != e && eb != e) continue;
        ContactInfo info;
        info.other = (ea == e) ? eb : ea;
        info.normal = m.normal;
        info.penetration = m.penetration;
        glm::vec3 avg(0.0f);
        for (int k = 0; k < m.count; ++k) {
            avg += m.points[k];
            info.normalImpulse += m.normalImpulse[k];
            info.frictionImpulse += std::sqrt(m.tangentImpulse[k][0] * m.tangentImpulse[k][0]
                                            + m.tangentImpulse[k][1] * m.tangentImpulse[k][1]);
        }
        if (m.count > 0) avg /= static_cast<float>(m.count);
        info.point = avg;
        out.push_back(info);
    }
    return out;
}

std::vector<ecs::Entity> PhysicsWorld::OverlapSphere(ecs::Registry& reg,
                                                     const glm::vec3& center, float radius,
                                                     std::uint32_t layerMask) const {
    std::vector<ecs::Entity> hits;
    const auto finish = [&](std::vector<ecs::Entity> value) {
        DebugTrace trace;
        trace.type = DebugTrace::Type::Overlap;
        trace.start = center;
        trace.end = center;
        trace.radius = std::max(radius, 0.0f);
        trace.hit = !value.empty();
        trace.hitPoint = center;
        RecordDebugTrace(trace);
        return value;
    };
    if (layerMask == 0u) return finish(std::move(hits));
    const float r = std::max(radius, 0.0f);
    ++m_stats.overlaps;
    auto colliderView = reg.view<Transform, Collider>();
    const auto testCandidate = [&](Entity e, Transform& ownerTransform, Collider& localCollider) {
        ++m_stats.queryCandidates;
        physics::WorldCollider world = physics::BuildWorldCollider(ownerTransform, localCollider);
        Transform& t = world.transform;
        Collider& c = world.collider;
        if ((c.layer & layerMask) == 0u) return;
        const float bound = ColliderBoundRadius(c);
        if (std::isfinite(bound)) {
            const glm::vec3 offset = t.position - center;
            const float reach = r + bound;
            if (glm::dot(offset, offset) > reach * reach) return;
        }
        // Overlap the query sphere (radius r) against the collider by closest-point.
        bool overlap = false;
        if (c.shape == ColliderShape::Sphere) {
            const glm::vec3 d = t.position - center;
            overlap = glm::dot(d, d) <= (r + c.radius) * (r + c.radius);
        } else if (c.shape == ColliderShape::Plane) {
            overlap = std::fabs(glm::dot(c.planeNormal, center) - c.planeOffset) <= r;
        } else if (c.shape == ColliderShape::Capsule) {
            const glm::vec3 up = glm::mat3_cast(t.rotation) * glm::vec3(0.0f, 1.0f, 0.0f);
            const glm::vec3 h = up * c.halfHeight;
            const glm::vec3 cp = ClosestOnSeg(t.position - h, t.position + h, center);
            const glm::vec3 d = center - cp;
            overlap = glm::dot(d, d) <= (r + c.radius) * (r + c.radius);
        } else if ((c.shape == ColliderShape::TriangleMesh
                    || c.shape == ColliderShape::ConvexHull)
                   && !c.collisionAssetPath.empty()) {
            if (const auto mesh = physics::AcquireCollisionMesh(c.collisionAssetPath))
                overlap = physics::CollideSphereCollisionMesh(
                    *mesh, t, center, r, nullptr, nullptr, nullptr);
        } else if (c.shape == ColliderShape::Cylinder) {
            // Exact closest point from the query centre to a finite, oriented,
            // flat-ended cylinder.
            const glm::quat inverseRotation = glm::inverse(t.rotation);
            const glm::vec3 local = inverseRotation * (center - t.position);
            glm::vec3 closest = local;
            closest.y = glm::clamp(closest.y, -c.halfHeight, c.halfHeight);
            const float radialLength = glm::length(glm::vec2(local.x, local.z));
            if (radialLength > c.radius && radialLength > 0.000001f) {
                const float scale = c.radius / radialLength;
                closest.x *= scale;
                closest.z *= scale;
            }
            const glm::vec3 delta = local - closest;
            overlap = glm::dot(delta, delta) <= r * r;
        } else {
            // Box (and composite shapes conservatively via their world AABB): closest
            // point on the OBB to the query centre.
            OBB o; o.center = t.position;
            const glm::mat3 R = glm::mat3_cast(t.rotation);
            o.axis[0] = R[0]; o.axis[1] = R[1]; o.axis[2] = R[2];
            o.ext = c.halfExtents;
            const glm::vec3 dloc = center - o.center;
            glm::vec3 local(glm::dot(dloc, o.axis[0]), glm::dot(dloc, o.axis[1]), glm::dot(dloc, o.axis[2]));
            const glm::vec3 clamped = glm::clamp(local, -o.ext, o.ext);
            const glm::vec3 closest = o.center
                + clamped[0] * o.axis[0] + clamped[1] * o.axis[1] + clamped[2] * o.axis[2];
            const glm::vec3 d = center - closest;
            overlap = glm::dot(d, d) <= r * r;
        }
        if (overlap) hits.push_back(e);
    };
    const auto testEntityColliders = [&](Entity e, Transform& transform) {
        if (Collider* primary = reg.TryGet<Collider>(e))
            testCandidate(e, transform, *primary);
        if (AdditionalColliders* compound = reg.TryGet<AdditionalColliders>(e))
            for (Collider& collider : compound->values)
                testCandidate(e, transform, collider);
    };

    // Broad-phase: gather colliders whose grid cells overlap the query-sphere AABB (a
    // complete superset); otherwise fall back to the exact full scan.
    std::vector<Entity>& candidates = m_queryScratch;   // Pass-5: reused (see m_queryScratch note)
    bool accelerated = false;
    if (m_broadphaseValid) {
        if (GatherBroadphaseCandidates(center - glm::vec3(r), center + glm::vec3(r), candidates)) {
            accelerated = true;
            for (Entity e : candidates) {
                if (Transform* transform = reg.TryGet<Transform>(e))
                    testEntityColliders(e, *transform);
            }
        }
    }
    if (!accelerated) {
        colliderView.each(testCandidate);
        reg.view<Transform, AdditionalColliders>().each(
            [&](Entity e, Transform& transform, AdditionalColliders& compound) {
                for (Collider& collider : compound.values)
                    testCandidate(e, transform, collider);
            });
    }
    std::sort(hits.begin(), hits.end());
    hits.erase(std::unique(hits.begin(), hits.end()), hits.end());
    return finish(std::move(hits));
}

void PhysicsWorld::ApplyRadialImpulse(ecs::Registry& reg, const glm::vec3& center,
                                      float radius, float strength,
                                      std::uint32_t layerMask) const {
    if (radius <= 0.0f || strength == 0.0f || layerMask == 0u) return;
    auto bodyView = reg.view<Transform, RigidBody>();
    if (bodyView.empty()) return;
    const float radiusSq = radius * radius;
    bodyView.each([&](Entity e, Transform& t, RigidBody& rb) {
        if (rb.invMass <= 0.0f || rb.kinematic) return;
        if (const Collider* c = reg.TryGet<Collider>(e))
            if ((c->layer & layerMask) == 0u) return;
        const glm::vec3 d = t.position - center;
        const float distSq = glm::dot(d, d);
        if (distSq > radiusSq) return;
        const float dist = std::sqrt(distSq);
        const glm::vec3 dir = (dist > 1e-4f) ? d / dist : glm::vec3(0.0f, 1.0f, 0.0f);
        const float falloff = 1.0f - dist / radius;
        rb.velocity += dir * (strength * falloff * rb.invMass);
        rb.sleeping = false; rb.sleepTimer = 0.0f;
    });
}

} // namespace engine
