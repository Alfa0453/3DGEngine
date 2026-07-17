#include "engine/physics/PhysicsWorld.h"

#include "engine/physics/PhysicsComponents.h"
#include "engine/ecs/Registry.h"
#include "engine/ecs/Components.h"   // Transform

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
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
    const glm::mat3 R = glm::mat3_cast(b.t->rotation);
    return R * b.rb->invInertiaLocal * glm::transpose(R);
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
        case ColliderShape::Plane:   return 8;
    }
    return 0;
}

// Body-space inverse inertia for a collider (plane/other -> no rotation).
glm::mat3 InertiaFor(const Collider& c, float mass) {
    if (c.shape == ColliderShape::Box)     return RigidBody::SolidBoxInvInertia(mass, c.halfExtents);
    if (c.shape == ColliderShape::Sphere)  return RigidBody::SolidSphereInvInertia(mass, c.radius);
    if (c.shape == ColliderShape::Capsule) return RigidBody::CapsuleInvInertia(mass, c.radius, c.halfHeight);
    if (c.shape == ColliderShape::Cylinder || c.shape == ColliderShape::Cone
        || c.shape == ColliderShape::Pyramid || c.shape == ColliderShape::Torus
        || c.shape == ColliderShape::Staircase)
        return RigidBody::SolidBoxInvInertia(mass, c.halfExtents);
    return glm::mat3(0.0f);
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

        for (int c = 0; c < 4 && ct.count < 4; ++c) {
            const glm::vec3 d = corners[c] - rC;
            const float pu = glm::dot(d, uA), pv = glm::dot(d, vA);
            if (std::fabs(pu) <= ue + 1e-3f && std::fabs(pv) <= ve + 1e-3f) {   // within the ref face
                const float depth = glm::dot(refN, rC - corners[c]);           // >0 = below the face
                if (depth > -1e-3f) ct.points[ct.count++] = corners[c];
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
        // A capsule contained inside the finite cylinder: no collision extends
        // beyond the flat caps, while the curved side remains radial.
        AddProxy(set, body, glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
            Collider::MakeCapsule(c.radius, std::max(c.halfHeight - c.radius, 0.0f)));
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
    if (CompositeShape(sa) || CompositeShape(sb)) return DetectComposite(A, B);
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

// Prepare a manifold ONCE per step: wake fast-closing sleepers, cache inverse
// mass/inertia + contact offsets + effective masses + friction basis, capture the
// restitution target, and warm-start from last step's cached impulses.
void PrepareManifold(ContactManifold& m, Body& A, Body& B,
                     float restitutionThreshold,
                     const std::unordered_map<std::uint64_t, ContactCache>& cache) {
    const glm::vec3 n = m.normal;
    BuildTangents(n, m.tangent1, m.tangent2);

    // Cache the constants used every iteration (built once here).
    m.invMassA = invMassOf(A); m.invMassB = invMassOf(B);
    m.invIA = InvIWorld(A);     m.invIB = InvIWorld(B);
    m.friction = std::sqrt(A.c->friction * B.c->friction);

    const glm::vec3 cA = A.t->position, cB = B.t->position;
    const float e0 = std::min(A.c->restitution, B.c->restitution);

    const auto it = cache.find(m.key);
    const ContactCache* seed = (it != cache.end()) ? &it->second : nullptr;

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
        m.restBias[k] = (-vn > restitutionThreshold) ? (-e0 * vn) : 0.0f;

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
}

// One velocity-solver iteration: normal impulse (clamped >= 0) then two-axis
// Coulomb friction (clamped to mu * accumulated normal impulse). Uses only cached
// constants + cheap dot/cross products -- no mat3 rebuilds.
void SolveManifoldVelocity(ContactManifold& m, Body& A, Body& B) {
    const glm::vec3 n = m.normal;
    const glm::vec3 axes[2] = { m.tangent1, m.tangent2 };

    for (int k = 0; k < m.count; ++k) {
        const glm::vec3& rA = m.rA[k];
        const glm::vec3& rB = m.rB[k];

        // Normal impulse (accumulated + clamped to stay pushing).
        {
            const glm::vec3 relVel = (velOf(B) + glm::cross(AngVelOf(B), rB))
                                   - (velOf(A) + glm::cross(AngVelOf(A), rA));
            const float vn = glm::dot(relVel, n);
            float dPn = (-vn + m.restBias[k]) * m.normalMass[k];
            const float old = m.normalImpulse[k];
            m.normalImpulse[k] = std::max(old + dPn, 0.0f);
            dPn = m.normalImpulse[k] - old;
            ApplyImpulse(m, A, B, k, dPn * n);
        }

        // Friction along each tangent axis, clamped to the Coulomb cone.
        const float maxF = m.friction * m.normalImpulse[k];
        for (int ax = 0; ax < 2; ++ax) {
            const glm::vec3 relVel = (velOf(B) + glm::cross(AngVelOf(B), rB))
                                   - (velOf(A) + glm::cross(AngVelOf(A), rA));
            const float vt = glm::dot(relVel, axes[ax]);
            float dPt = -vt * m.tangentMass[k][ax];
            const float old = m.tangentImpulse[k][ax];
            m.tangentImpulse[k][ax] = std::clamp(old + dPt, -maxF, maxF);
            dPt = m.tangentImpulse[k][ax] - old;
            ApplyImpulse(m, A, B, k, dPt * axes[ax]);
        }
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

// Rigid distance joint at the anchor points (impulse applies linear + angular).
void SolveDistance(ecs::Registry& reg, const Joint& j) {
    JointEnds e = ResolveEnds(reg, j);
    if (!e.ok) return;
    const glm::vec3 d = e.pB - e.pA;
    const float len = glm::length(d);
    if (len < 1e-6f) return;
    const glm::vec3 n = d / len;
    const float C = len - j.restLength;
    if (j.rope && C < 0.0f) return;

    if (std::fabs(C) > 0.005f) {
        if (e.ra && e.ra->sleeping) { e.ra->sleeping = false; e.ra->sleepTimer = 0.0f; }
        if (e.rb && e.rb->sleeping) { e.rb->sleeping = false; e.rb->sleepTimer = 0.0f; }
    }
    const float imA = (e.ra && !e.ra->sleeping) ? e.ra->invMass : 0.0f;
    const float imB = (e.rb && !e.rb->sleeping) ? e.rb->invMass : 0.0f;
    const glm::mat3 IA = JointInvI(*e.ta, e.ra);
    const glm::mat3 IB = e.tb ? JointInvI(*e.tb, e.rb) : glm::mat3(0.0f);

    const glm::vec3 raxn = glm::cross(e.rA, n), rbxn = glm::cross(e.rB, n);
    const float k = imA + imB + glm::dot(glm::cross(IA * raxn, e.rA) + glm::cross(IB * rbxn, e.rB), n);
    if (k <= 0.0f) return;

    const float vrel = glm::dot(AnchorVel(e.rb, e.rB) - AnchorVel(e.ra, e.rA), n);
    const glm::vec3 P = (-vrel / k) * n;
    if (e.ra) { e.ra->velocity -= P * imA; e.ra->angularVelocity -= IA * glm::cross(e.rA, P); }
    if (e.rb) { e.rb->velocity += P * imB; e.rb->angularVelocity += IB * glm::cross(e.rB, P); }

    const float imSum = imA + imB;
    if (imSum > 0.0f) {
        const glm::vec3 corr = n * (C * 0.2f / imSum);
        if (e.ra)          e.ta->position += corr * imA;
        if (e.rb && e.tb)  e.tb->position -= corr * imB;
    }
}

// Perpendicular unit vector to v.
glm::vec3 PerpAxis(const glm::vec3& v) {
    const glm::vec3 a = (std::fabs(v.x) < 0.9f) ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
    return glm::normalize(glm::cross(v, a));
}

// Point-to-point constraint at the anchor points (shared by ball + hinge). Drives
// the two anchor points together in 3D with the constraint effective mass.
void SolvePointConstraint(JointEnds& e) {
    const glm::vec3 C = e.pB - e.pA;
    if (glm::length(C) > 0.005f) {
        if (e.ra && e.ra->sleeping) { e.ra->sleeping = false; e.ra->sleepTimer = 0.0f; }
        if (e.rb && e.rb->sleeping) { e.rb->sleeping = false; e.rb->sleepTimer = 0.0f; }
    }
    const float imA = (e.ra && !e.ra->sleeping) ? e.ra->invMass : 0.0f;
    const float imB = (e.rb && !e.rb->sleeping) ? e.rb->invMass : 0.0f;
    const glm::mat3 IA = JointInvI(*e.ta, e.ra);
    const glm::mat3 IB = e.tb ? JointInvI(*e.tb, e.rb) : glm::mat3(0.0f);

    const glm::mat3 sA = Skew(e.rA), sB = Skew(e.rB);
    glm::mat3 K = glm::mat3(imA + imB) - sA * IA * sA - sB * IB * sB;
    if (std::fabs(glm::determinant(K)) < 1e-9f) return;
    const glm::vec3 P = glm::inverse(K) * (-(AnchorVel(e.rb, e.rB) - AnchorVel(e.ra, e.rA)));
    if (e.ra) { e.ra->velocity -= P * imA; e.ra->angularVelocity -= IA * glm::cross(e.rA, P); }
    if (e.rb) { e.rb->velocity += P * imB; e.rb->angularVelocity += IB * glm::cross(e.rB, P); }

    const float imSum = imA + imB;
    if (imSum > 0.0f) {
        const glm::vec3 corr = C * (0.2f / imSum);
        if (e.ra)          e.ta->position += corr * imA;
        if (e.rb && e.tb)  e.tb->position -= corr * imB;
    }
}

// Ball (point-to-point) joint: just the point constraint.
void SolveBall(ecs::Registry& reg, const Joint& j) {
    JointEnds e = ResolveEnds(reg, j);
    if (!e.ok) return;
    SolvePointConstraint(e);
}

// Hinge joint: point constraint + keep the two hinge axes aligned (removing the
// two rotational DOF perpendicular to the axis, leaving rotation about it).
void SolveHinge(ecs::Registry& reg, const Joint& j) {
    JointEnds e = ResolveEnds(reg, j);
    if (!e.ok) return;
    SolvePointConstraint(e);

    const glm::mat3 IA = JointInvI(*e.ta, e.ra);
    const glm::mat3 IB = e.tb ? JointInvI(*e.tb, e.rb) : glm::mat3(0.0f);
    const glm::mat3 Isum = IA + IB;

    const glm::vec3 aA = glm::normalize(glm::mat3_cast(e.ta->rotation) * j.axisA);
    const glm::vec3 aB = e.tb ? glm::normalize(glm::mat3_cast(e.tb->rotation) * j.axisB)
                              : glm::normalize(j.axisB);
    const glm::vec3 err = glm::cross(aA, aB);          // 0 when aligned
    const glm::vec3 perp[2] = { PerpAxis(aA), glm::cross(aA, PerpAxis(aA)) };

    for (int i = 0; i < 2; ++i) {
        const glm::vec3 t = perp[i];
        const float k = glm::dot(t, Isum * t);
        if (k < 1e-9f) continue;
        const glm::vec3 wA = (e.ra && !e.ra->sleeping) ? e.ra->angularVelocity : glm::vec3(0.0f);
        const glm::vec3 wB = (e.rb && !e.rb->sleeping) ? e.rb->angularVelocity : glm::vec3(0.0f);
        const float vn = glm::dot(wB - wA, t);
        const float Ct = glm::dot(err, t);            // alignment drift along t
        const glm::vec3 L = (-(vn + 0.2f * Ct) / k) * t;
        if (e.ra) e.ra->angularVelocity -= IA * L;
        if (e.rb) e.rb->angularVelocity += IB * L;
    }
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
    if (c->shape == ColliderShape::Sphere)  return c->radius;
    if (c->shape == ColliderShape::Box)     return glm::length(c->halfExtents);
    if (c->shape == ColliderShape::Capsule) return c->radius + c->halfHeight;
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

        // Angular: initialise inertia from the collider once, then integrate the
        // orientation from angular velocity (dq = 0.5 * omega * q).
        if (!rb.freezeRotation && rb.invInertiaLocal == glm::mat3(0.0f)) {
            if (const Collider* col = reg.TryGet<Collider>(e))
                rb.invInertiaLocal = InertiaFor(*col, 1.0f / rb.invMass);
        }
        if (rb.freezeRotation) {
            rb.angularVelocity = glm::vec3(0.0f);
        } else if (rb.invInertiaLocal != glm::mat3(0.0f)) {
            const glm::mat3 R     = glm::mat3_cast(t.rotation);
            const glm::mat3 invIw = R * rb.invInertiaLocal * glm::transpose(R);
            rb.angularVelocity += invIw * rb.accumTorque * dt;
            rb.angularVelocity *= 1.0f / (1.0f + dt * std::max(rb.angularDamping, 0.0f));  // stop rocking / rolling
            const glm::quat wq(0.0f, rb.angularVelocity.x, rb.angularVelocity.y, rb.angularVelocity.z);
            t.rotation = glm::normalize(t.rotation + 0.5f * wq * t.rotation * dt);
        }

        rb.accumForce  = glm::vec3(0.0f);
        rb.accumTorque = glm::vec3(0.0f);
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
        if (!LayersCollide(*A->c, *B->c)) continue;    // collision layer/mask filter
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
            m.count = ct.count;
            for (int pi = 0; pi < ct.count; ++pi) m.points[pi] = ct.points[pi];
            m.key = PairKey(A->e, B->e);
            m_manifolds.push_back(m);
        }
    }

    // 5) Emit Enter/Stay/Exit events by diffing against last step. Solid contacts
    //    carry contact point + normal now; the impulse is filled in after the solve.
    std::unordered_map<std::uint64_t, int> manifoldOf;    // pair key -> manifold index
    for (int mi = 0; mi < static_cast<int>(m_manifolds.size()); ++mi)
        manifoldOf[m_manifolds[mi].key] = mi;
    std::unordered_map<std::uint64_t, int> eventOf;       // pair key -> event index (solids)

    m_events.clear();
    for (const auto& kv : m_touchingNow) {
        const bool was = (m_touching.find(kv.first) != m_touching.end());
        CollisionEvent ev;
        ev.a = Entity(kv.first >> 32);
        ev.b = Entity(kv.first & 0xFFFFFFFFu);
        ev.phase = was ? CollisionEvent::Phase::Stay : CollisionEvent::Phase::Enter;
        ev.trigger = kv.second;
        const auto mit = manifoldOf.find(kv.first);
        if (mit != manifoldOf.end()) {
            const ContactManifold& m = m_manifolds[mit->second];
            ev.normal = m.normal;
            glm::vec3 avg(0.0f);
            for (int k = 0; k < m.count; ++k) avg += m.points[k];
            if (m.count > 0) avg /= static_cast<float>(m.count);
            ev.point = avg;
            eventOf[kv.first] = static_cast<int>(m_events.size());   // impulse filled after solve
        }
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

    // 6) Velocity solve: warm-started, accumulating sequential impulses over the
    //    cached manifolds (normal + 2-axis Coulomb friction), plus the joints.
    const float wakeSpeed = sleepLinearVelocity * 2.0f;
    // Wake pass first: a fast-closing contact wakes either sleeper, so the prepare
    // pass below caches every body's inverse mass/inertia in its final awake state.
    for (ContactManifold& m : m_manifolds) {
        Body& A = m_bodies[m.a]; Body& B = m_bodies[m.b];
        if (glm::dot(velOf(B) - velOf(A), m.normal) < -wakeSpeed) { Wake(A); Wake(B); }
    }
    for (ContactManifold& m : m_manifolds)
        PrepareManifold(m, m_bodies[m.a], m_bodies[m.b], restitutionThreshold, m_contactCache);
    for (int iter = 0; iter < solverIterations; ++iter) {
        for (ContactManifold& m : m_manifolds)
            SolveManifoldVelocity(m, m_bodies[m.a], m_bodies[m.b]);
        for (const Joint& j : m_joints) {
            if (j.type == Joint::Type::Distance) SolveDistance(reg, j);
            else if (j.type == Joint::Type::Ball)  SolveBall(reg, j);
            else if (j.type == Joint::Type::Hinge) SolveHinge(reg, j);
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
        const auto eit = eventOf.find(m.key);        // O(1) instead of scanning m_events
        if (eit != eventOf.end()) m_events[eit->second].impulse = total;
    }

    // 7) One positional-correction pass from the cached penetrations.
    for (const ContactManifold& m : m_manifolds)
        CorrectPosition(m_bodies[m.a], m_bodies[m.b], m.normal, m.penetration);

    // 8) Update sleep state: bodies that stayed slow long enough go to sleep.
    if (allowSleeping) {
        const float thresh2  = sleepLinearVelocity * sleepLinearVelocity;
        const float aThresh2 = sleepAngularVelocity * sleepAngularVelocity;
        reg.view<Transform, RigidBody>().each([&](Entity, Transform&, RigidBody& rb) {
            if (rb.invMass <= 0.0f || rb.kinematic) return;
            if (!rb.allowSleep) { rb.sleeping = false; rb.sleepTimer = 0.0f; return; }
            if (rb.sleeping) return;
            const bool slow = glm::dot(rb.velocity, rb.velocity) < thresh2
                           && glm::dot(rb.angularVelocity, rb.angularVelocity) < aThresh2;
            if (slow) {
                rb.sleepTimer += dt;
                if (rb.sleepTimer >= timeToSleep) {
                    rb.sleeping = true; rb.velocity = glm::vec3(0.0f); rb.angularVelocity = glm::vec3(0.0f);
                }
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

RaycastHit PhysicsWorld::Raycast(ecs::Registry& reg, const Ray& ray, float maxDistance,
                                 std::uint32_t layerMask) const {
    RaycastHit best;
    best.distance = maxDistance;
    const float len2 = glm::dot(ray.direction, ray.direction);
    if (len2 < 1e-12f) return best;              // degenerate ray
    const glm::vec3 d = ray.direction / std::sqrt(len2);

    reg.view<Transform, Collider>().each([&](Entity e, Transform& t, Collider& c) {
        if ((c.layer & layerMask) == 0u) return;    // filtered out by the query mask
        glm::vec3 n(0.0f);
        float hitT = -1.0f;
        if (c.shape == ColliderShape::Sphere) {
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
    });

    if (!best.hit) best.distance = 0.0f;
    return best;
}

RaycastHit PhysicsWorld::SphereCast(ecs::Registry& reg,
                                    const glm::vec3& start,
                                    const glm::vec3& end,
                                    float radius,
                                    Entity ignored,
                                    std::uint32_t layerMask) const {
    RaycastHit result;
    const glm::vec3 travel = end - start;
    const float distance = glm::length(travel);
    if (distance < 1e-6f) return result;

    float bestToi = 1.0f;
    glm::vec3 bestNormal(0.0f);
    Entity bestEntity = ecs::kNull;
    const float sweepRadius = std::max(radius, 0.0f);

    reg.view<Transform, Collider>().each([&](Entity entity, Transform& transform, Collider& collider) {
        if (entity == ignored || collider.isTrigger) return;
        if ((collider.layer & layerMask) == 0u) return;    // filtered out by the query mask

        float toi = 1.0f;
        glm::vec3 normal(0.0f);
        if (collider.shape == ColliderShape::Plane) {
            toi = SweptSpherePlane(start, end, sweepRadius,
                                   collider.planeNormal, collider.planeOffset, normal);
        } else if (collider.shape == ColliderShape::Sphere) {
            toi = SweptSphereSphere(start, end, sweepRadius,
                                    transform.position, collider.radius, normal);
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
    });

    if (bestEntity == ecs::kNull) return result;
    result.hit = true;
    result.entity = bestEntity;
    result.distance = distance * bestToi;
    result.point = start + travel * bestToi;
    result.normal = bestNormal;
    return result;
}

std::vector<ecs::Entity> PhysicsWorld::OverlapSphere(ecs::Registry& reg,
                                                     const glm::vec3& center, float radius,
                                                     std::uint32_t layerMask) const {
    std::vector<ecs::Entity> hits;
    const float r = std::max(radius, 0.0f);
    reg.view<Transform, Collider>().each([&](Entity e, Transform& t, Collider& c) {
        if ((c.layer & layerMask) == 0u) return;
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
    });
    return hits;
}

void PhysicsWorld::ApplyRadialImpulse(ecs::Registry& reg, const glm::vec3& center,
                                      float radius, float strength,
                                      std::uint32_t layerMask) const {
    if (radius <= 0.0f) return;
    reg.view<Transform, RigidBody>().each([&](Entity e, Transform& t, RigidBody& rb) {
        if (rb.invMass <= 0.0f || rb.kinematic) return;
        if (const Collider* c = reg.TryGet<Collider>(e))
            if ((c->layer & layerMask) == 0u) return;
        const glm::vec3 d = t.position - center;
        const float dist = glm::length(d);
        if (dist > radius) return;
        const glm::vec3 dir = (dist > 1e-4f) ? d / dist : glm::vec3(0.0f, 1.0f, 0.0f);
        const float falloff = 1.0f - dist / radius;
        rb.velocity += dir * (strength * falloff * rb.invMass);
        rb.sleeping = false; rb.sleepTimer = 0.0f;
    });
}

} // namespace engine
