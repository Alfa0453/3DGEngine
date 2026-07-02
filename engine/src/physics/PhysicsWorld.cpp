#include "engine/physics/PhysicsWorld.h"

#include "engine/physics/PhysicsComponents.h"
#include "engine/ecs/Registry.h"
#include "engine/ecs/Components.h"  // Transform

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>   // mat3_cast

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

// A collider paired with its transform and (optional) body, gathered for the
// pair-test phase. A null body means a static, immovable collider.
struct Body {
    Entity     e;
    Transform* t;
    Collider*  c;
    RigidBody* rb;  // may be null (static)
};

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
int priority(ColliderShape  s) {
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
    ct.normal = (dist > 1e-6f) ? d / dist : glm::vec3(0.0f, 1.0f, 1.0f);
    ct.penetration = r - dist;
    return ct;
}

// A = sphere, B = plane. Normal points from sphere toward the plane.
Contact SpherePlane(const glm::vec3& pa, float ra, const glm::vec3& n, float off) {
    Contact ct;
    const float s = glm::dot(n, pa) - off;   // signed distance, centre to plane
    const float pen = ra - s;
    if (pen <= 0.0f) return ct;
    ct.hit = true;
    ct.normal = -n;          // from sphere (A) toward plane (B)
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
        ct.normal = -boxToSphere;       // A(sphere) -> B(box)
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
    ct.normal = (dist > 1e-6f) ? -(delta / dist) : glm::vec3(0.0f, -1.0f, 0.0f); // A -> B
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
        const float pen = off - glm::dot(n, corner);    // >0 means below the plane
        deepest = std::max(deepest, pen);
    }
    if (deepest <= 0.0f) return ct;
    ct.hit = true;
    ct.normal = -n;                // A(box) -> B(plane)
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
    for (int i = 0; i < 3; ++i) axes[n++] = A.axis[i];  // face normals of A (tested first)
    for (int i = 0; i < 3; ++i) axes[n++] = B.axis[i];  // face normals of B
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            glm::vec3 c = glm::cross(A.axis[i], B.axis[j]);
            axes[n++] = c;  // edge-edge axes (may be near-zero -> skipped below)
        }
    
    float minOverlap = std::numeric_limits<float>::max();
    glm::vec3 bestAxis(0.0f);
    for (int k = 0; k < 15; ++k) {
        glm::vec3 L = axes[k];
        const float len2 = glm::dot(L, L);
        if (len2 < 1e-6f) continue;         // parallel edges: degenerate axis
        L /= std::sqrt(len2);
        float rA = 0.0f, rB = 0.0f;
        for (int i = 0; i < 3; ++i) rA += A.ext[i] * std::fabs(glm::dot(A.axis[i], L));
        for (int i = 0; i < 3; ++i) rB += B.ext[i] * std::fabs(glm::dot(B.axis[i], L));
        const float dist = std::fabs(glm::dot(t, L));
        const float overlap = rA + rB - dist;
        if (overlap < 0.0f) return ct;       // separating axis found: no contact
        if (overlap < minOverlap - 1e-4f) {  // bias favours earlier (face) axes on ties
            minOverlap = overlap;
            bestAxis = (glm::dot(t, L) < 0.0f) ? -L : L;    // orient A -> B
        }
    }
    ct.hit = true;
    ct.normal = bestAxis;
    ct.penetration = minOverlap;
    return ct;
}

// Dispatch by the (canonically ordered) shape pair.
Contact Detect(Body& A, Body& B) {
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
    return Contact{};    // other combos handled in later milestones
}

void Resolve(Body& A, Body& B, const Contact& ct, 
             float restitutionThreshold, float wakeSpeed) {
    // A fast-closing contact wakes either sleeper before we resolve it.
    {
        const glm::vec3 rv0 = velOf(B) - velOf(A);
        if (glm::dot(rv0, ct.normal) < -wakeSpeed) { Wake(A); Wake(B); }
    }

    const float imA = invMassOf(A), imB = invMassOf(B);
    const float imSum = imA + imB;
    if (imSum <= 0.0f) return;    // both static
    const glm::vec3 n = ct.normal;

    const glm::vec3 vA = velOf(A);
    const glm::vec3 vB = velOf(B);
    const glm::vec3 relVel = vB - vA;
    const float vn = glm::dot(relVel, n);

    if (vn < 0.0f) {    // approaching: apply normal + friction impulse
        // Restitution slop: below a small closing speed, don't bounce.
        const float e = (-vn > restitutionThreshold)
                        ? std::min(A.c->restitution, B.c->restitution) : 0.0f;
        const float j = -(1.0f + e) * vn / imSum;
        const glm::vec3 impulse = j * n;
        if (A.rb) A.rb->velocity -= impulse * imA;
        if (B.rb) B.rb->velocity += impulse * imB;

        const glm::vec3 vA2 = velOf(A);
        const glm::vec3 vB2 = velOf(B);
        glm::vec3 rv = vB2 - vA2;
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

    // Positional correction (Baumgarte): push the pair apart along the normal.
    const float percent = 0.8f, slop = 0.001f;
    const glm::vec3 corr = (std::max(ct.penetration - slop, 0.0f) / imSum) * percent * n;
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
        return {b.t->position - r, b.t->position + r};
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
    const glm::vec3 fOnA = fMag * n;    // toward b when stretched
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
    if (j.rope && C < 0.0f) return;     // slack: nothing to do

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

    const float beta = 0.2f;            // position stabilisation factor
    const glm::vec3 corr = n * (C * beta / imSum);
    if (ra)          ta.position   += corr * imA;   // pull a toward b when C>0
    if (rb && tb)    tb->position  -= corr * imB;
}

} // namespace

void PhysicsWorld::Step(ecs::Registry& reg, float dt) {
    // 0) Spring joints add forces before integration.
    for (const Joint& j : m_joints)
        if (j.type == Joint::Type::Spring) ApplySpring(reg, j);

    // 1) Integrate (semi-implicit Euler): velocity first, then position.
    reg.view<Transform, RigidBody>().each([&](Entity, Transform& t, RigidBody& rb) {
        if (rb.invMass <= 0.0f) { rb.accumForce = glm::vec3(0.0f); return; }
        if (rb.sleeping)        { rb.accumForce = glm::vec3(0.0f); return; }
        glm::vec3 accel = rb.accumForce * rb.invMass;
        if (rb.useGravity) accel += gravity;
        rb.velocity  += accel * dt;
        t.position   += rb.velocity * dt;
        rb.accumForce = glm::vec3(0.0f);
    });

    // 2) Gather colliders (pointers stay valid: no add/remove during the step).
    std::vector<Body> bodies;
    reg.view<Transform, Collider>().each([&](Entity e, Transform& t, Collider& c) {
        bodies.push_back(Body{e, &t, &c, reg.TryGet<RigidBody>(e)});
    });
    const int N = static_cast<int>(bodies.size());

    // 3) Broad phase -> a sorted, de-duplicated list of candidate index pairs.
    //    Sorting makes the sequential-impulse solver order-independent of the
    //    broad phase, so grid and brute-force produce identical results.
    std::vector<std::pair<int,int>> pairs;

    if (!broadPhase || N < 2) {
        for (int i = 0; i < N; ++i)
            for (int j = i + 1; j < N; ++j) pairs.emplace_back(i, j);
    } else {
        // Finite bodies go in the hash grid; planes are tested against all finite
        // bodies directly (they have no bounded AABB).
        std::vector<int> planes, finite;
        planes.reserve(N); finite.reserve(N);
        for (int i = 0; i < N; ++i) {
            if (bodies[i].c->shape == ColliderShape::Plane) planes.push_back(i);
            else finite.push_back(i);
        }

        const float cs = (cellSize > 1e-2f) ? cellSize : 1.0f;
        const float margin = 0.1f;   // small skin so approaching pairs are caught
        std::unordered_map<std::int64_t, std::vector<int>> grid;
        grid.reserve(finite.size() * 2 + 1);

        std::vector<AABB> boxes(N);
        for (int fi : finite) {
            AABB a = ComputeAABB(bodies[fi]);
            a.mn -= glm::vec3(margin); a.mx += glm::vec3(margin);
            boxes[fi] = a;
            const int x0 = int(std::floor(a.mn.x / cs)), x1 = int(std::floor(a.mx.x / cs));
            const int y0 = int(std::floor(a.mn.y / cs)), y1 = int(std::floor(a.mx.y / cs));
            const int z0 = int(std::floor(a.mn.z / cs)), z1 = int(std::floor(a.mx.z / cs));
            for (int ix = x0; ix <= x1; ++ix)
                for (int iy = y0; iy <= y1; ++iy)
                    for (int iz = z0; iz <= z1; ++iz)
                        grid[CellKey(ix, iy, iz)].push_back(fi);
        }

        // Candidate pairs from bodies sharing a cell (de-duplicated via a set).
        std::vector<std::int64_t> keys;
        for (auto& cell : grid) {
            auto& list = cell.second;
            for (std::size_t a = 0; a < list.size(); ++a)
                for (std::size_t b = a + 1; b < list.size(); ++b) {
                    int i = list[a], j = list[b];
                    if (i > j) std::swap(i, j);
                    keys.push_back(std::int64_t(i) * N + j);
                }
        }
        std::sort(keys.begin(), keys.end());
        keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
        pairs.reserve(keys.size() + planes.size() * finite.size());
        for (std::int64_t k : keys) pairs.emplace_back(int(k / N), int(k % N));

        // Every finite body vs every plane.
        for (int p : planes)
            for (int f : finite) {
                int i = p, j = f; if (i > j) std::swap(i, j);
                pairs.emplace_back(i, j);
            }
        std::sort(pairs.begin(), pairs.end());
        pairs.erase(std::unique(pairs.begin(), pairs.end()), pairs.end());
    }

    // 4) Overlap detection for events: which pairs are touching this step.
    std::unordered_map<std::uint64_t, bool> touchingNow;
    for (const auto& pr : pairs) {
        Body* A = &bodies[pr.first];
        Body* B = &bodies[pr.second];
        if (priority(A->c->shape) > priority(B->c->shape)) std::swap(A, B);
        const Contact ct = Detect(*A, *B);
        if (ct.hit)
            touchingNow[PairKey(A->e, B->e)] = (A->c->isTrigger || B->c->isTrigger);
    }
    m_events.clear();
    for (const auto& kv : touchingNow) {
        const bool wasTouching = (m_touching.find(kv.first) != m_touching.end());
        CollisionEvent ev;
        ev.a = Entity(kv.first >> 32);
        ev.b = Entity(kv.first & 0xFFFFFFFFu);
        ev.phase = wasTouching ? CollisionEvent::Phase::stay : CollisionEvent::Phase::Enter;
        ev.trigger = kv.second;
        m_events.push_back(ev);
    }
    for (const auto& kv : m_touching) {
        if (touchingNow.find(kv.first) == touchingNow.end()) {
            CollisionEvent ev;
            ev.a = Entity(kv.first >> 32);
            ev.b = Entity(kv.first & 0XFFFFFFFFu);
            ev.phase = CollisionEvent::Phase::Exit;
            ev.trigger = kv.second;  // remembered from when they were touching
            m_events.push_back(ev);
        }
    }
    m_touching = std::move(touchingNow);
    
    // 5) Detect + resolve candidate pairs, iterated for stability. Trigger pairs
    //    are detected for events (above) but never physically resolved.
    const float wakeSpeed = sleepLinearVelocity * 2.0f;
    for (int iter = 0; iter < solverIterations; ++iter) {
        for (const auto& pr : pairs) {
            Body* A = &bodies[pr.first];
            Body* B = &bodies[pr.second];
            if (priority(A->c->shape) > priority(B->c->shape)) std::swap(A, B);
            if (A->c->isTrigger || B->c->isTrigger) continue;  // overlap-only
            const Contact ct = Detect(*A, *B);
            if (ct.hit) Resolve(*A, *B, ct, restitutionThreshold, wakeSpeed);
        }
        for (const Joint& j : m_joints)
            if (j.type == Joint::Type::Distace) SolveDistance(reg, j);
    }

    // 6) Update sleep state: bodies that stayed slow long enough go to sleep.
    if (allowSleeping) {
        const float thresh2 = sleepLinearVelocity * sleepLinearVelocity;
        reg.view<Transform, RigidBody>().each([&](Entity, Transform&, RigidBody& rb) {
            if (rb.invMass <= 0.0f) return;                 // static
            if (!rb.allowSleep) { rb.sleeping = false; rb.sleepTimer = 0.0f; return; }
            if (rb.sleeping) return;                        // stays asleep until a contact wakes it
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
    if (std::fabs(denom) < 1e-8f) return -1.0f;   // parallel
    const float t = (off - glm::dot(n, o)) / denom;
    if (t < 0.0f) return -1.0f;
    outN = (denom < 0.0f) ? n : -n;               // face the ray
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
    if (glm::dot(outN, d) > 0.0f) outN = -outN;     // ensure it faces the ray
    return t;
}

}   // namespace

RaycastHit PhysicsWorld::Raycast(ecs::Registry &reg, const Ray &ray, float maxDistance) const {
    RaycastHit best;
    best.distance = maxDistance;
    const float len2 = glm::dot(ray.direction, ray.direction);
    if (len2 < 1e-12f) return best;             // degenerate ray
    const glm::vec3 d = ray.direction / std::sqrt(len2);

    reg.view<Transform, Collider>().each([&](Entity e, Transform& t, Collider& c) {
        glm::vec3 n(0.0f);
        float hitT = -1.0f;
        if (c.shape == ColliderShape::Sphere) {
            hitT = RaySphere(ray.origin, d, t.position, c.radius, n);
        } else if (c.shape == ColliderShape::Plane) {
            hitT = RayPlane(ray.origin, d, c.planeNormal, c.planeOffset, n);
        } else {    // Box
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