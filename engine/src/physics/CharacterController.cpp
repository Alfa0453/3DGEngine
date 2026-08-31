#include "engine/physics/CharacterController.h"

#include "engine/physics/PhysicsComponents.h"
#include "engine/physics/PhysicsWorld.h"
#include "engine/physics/ColliderTransform.h"
#include "engine/ecs/Registry.h"
#include "engine/ecs/Components.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

using engine::ecs::Entity;
using engine::ecs::Transform;
using engine::ecs::Collider;
using engine::ecs::ColliderShape;

namespace engine {
namespace {

struct Pen { bool hit = false; glm::vec3 normal{0.0f}; float depth = 0.0f; };

// Closest point on segment [a,b] to point p.
glm::vec3 ClosestOnSegment(const glm::vec3& a, const glm::vec3& b, const glm::vec3& p) {
    const glm::vec3 ab = b - a;
    const float denom = glm::dot(ab, ab);
    float t = (denom > 1e-12f) ? glm::dot(p - a, ab) / denom : 0.0f;
    t = glm::clamp(t, 0.0f, 1.0f);
    return a + t * ab;
}

// Capsule (segment p0..p1, radius r) vs a collider. Returns the push-out normal
// (pointing away from the surface, toward the capsule) and penetration depth.
Pen CapsuleVsCollider(const glm::vec3& p0, const glm::vec3& p1, float r, const Transform& t, const Collider& c) {
    Pen out;
    if (c.shape == ColliderShape::Plane) {
        const glm::vec3& n = c.planeNormal;
        const float d0 = glm::dot(n, p0) - c.planeOffset;
        const float d1 = glm::dot(n, p1) - c.planeOffset;
        const float d = std::min(d0, d1);       // nearest endpoint to the plane
        if (d < r) { out.hit = true; out.normal = n; out.depth = r - d; }
        return out;
    }
    if (c.shape == ColliderShape::Sphere) {
        const glm::vec3 q = ClosestOnSegment(p0, p1, t.position);
        const glm::vec3 delta = q - t.position;
        const float dist = glm::length(delta);
        const float rr = r + c.radius;
        if (dist < rr) {
            out.hit = true;
            out.normal = (dist > 1e-6f) ? delta / dist : glm::vec3(0, 1, 0);
            out.depth = rr - dist;
        }
        return out;
    }
    if (c.shape == ColliderShape::Capsule) {
        // Capsule vs capsule: closest points between the two axis segments.
        const glm::vec3 up = glm::mat3_cast(t.rotation) * glm::vec3(0.0f, 1.0f, 0.0f);
        const glm::vec3 h  = up * c.halfHeight;
        const glm::vec3 b0 = t.position - h, b1 = t.position + h;
        // Cheap closest approach: clamp each segment's midpoint onto the other.
        const glm::vec3 qa = ClosestOnSegment(p0, p1, ClosestOnSegment(b0, b1, 0.5f * (p0 + p1)));
        const glm::vec3 qb = ClosestOnSegment(b0, b1, qa);
        const glm::vec3 delta = qa - qb;
        const float dist = glm::length(delta);
        const float rr = r + c.radius;
        if (dist < rr) {
            out.hit = true;
            out.normal = (dist > 1e-6f) ? delta / dist : glm::vec3(0, 1, 0);
            out.depth = rr - dist;
        }
        return out;
    }
    auto consider = [&](const Pen& p) {
        if (p.hit && (!out.hit || p.depth > out.depth)) out = p;
    };
    if (c.shape == ColliderShape::Cylinder) {
        // Match PhysicsWorld's flat-ended cylinder decomposition. Using box
        // strips here preserves the circular side while keeping both cap planes
        // flat for character stepping and push-out.
        constexpr int slices = 20;
        const float radius = std::max(c.radius, 0.001f);
        const float halfHeight = std::max(c.halfHeight, 0.001f);
        const float width = (2.0f * radius) / static_cast<float>(slices);
        for (int i = 0; i < slices; ++i) {
            const float x = -radius + (static_cast<float>(i) + 0.5f) * width;
            const float z = std::sqrt(std::max(radius * radius - x * x, 0.0f));
            Transform piece = t;
            piece.position += t.rotation * glm::vec3(x, 0.0f, 0.0f);
            consider(CapsuleVsCollider(p0, p1, r, piece,
                Collider::MakeBox(glm::vec3(width * 0.5f, halfHeight, std::max(z, 0.001f)))));
        }
        return out;
    }
    if (c.shape == ColliderShape::Torus) {
        constexpr int segments = 20;
        for (int i = 0; i < segments; ++i) {
            const float a = 6.28318530718f * static_cast<float>(i) / segments;
            Transform piece = t;
            piece.position += t.rotation * glm::vec3(
                std::cos(a) * c.majorRadius, 0.0f, std::sin(a) * c.majorRadius);
            consider(CapsuleVsCollider(p0, p1, r, piece, Collider::MakeSphere(c.minorRadius)));
        }
        return out;
    }
    if (c.shape == ColliderShape::Staircase) {
        const int steps = glm::clamp(c.steps, 1, 32);
        const float slice = c.halfExtents.z * 2.0f / steps;
        for (int i = 0; i < steps; ++i) {
            const float height = c.halfExtents.y * 2.0f * static_cast<float>(i + 1) / steps;
            const glm::vec3 ext(c.halfExtents.x, height * 0.5f, slice * 0.5f);
            Transform piece = t;
            piece.position += t.rotation * glm::vec3(0.0f, -c.halfExtents.y + ext.y,
                -c.halfExtents.z + slice * (static_cast<float>(i) + 0.5f));
            consider(CapsuleVsCollider(p0, p1, r, piece, Collider::MakeBox(ext)));
        }
        return out;
    }
    if (c.shape == ColliderShape::Cone || c.shape == ColliderShape::Pyramid) {
        constexpr int layers = 12;
        for (int i = 0; i < layers; ++i) {
            const float y0 = -c.halfExtents.y + (2.0f * c.halfExtents.y * i) / layers;
            const float y1 = -c.halfExtents.y + (2.0f * c.halfExtents.y * (i + 1)) / layers;
            const float fraction = std::max(1.0f - static_cast<float>(i + 1) / layers, 0.02f);
            const float x = (c.shape == ColliderShape::Cone ? c.radius : c.halfExtents.x) * fraction;
            const float z = (c.shape == ColliderShape::Cone ? c.radius : c.halfExtents.z) * fraction;
            Transform piece = t;
            piece.position += t.rotation * glm::vec3(0.0f, (y0 + y1) * 0.5f, 0.0f);
            consider(CapsuleVsCollider(p0, p1, r, piece,
                Collider::MakeBox(glm::vec3(x, (y1 - y0) * 0.5f, z))));
        }
        return out;
    }
    // Box (OBB): approximate by the capsule's closest segment point to the box.
    const glm::mat3 R = glm::mat3_cast(t.rotation);
    const glm::vec3 ax[3] = { R[0], R[1], R[2] };
    const glm::vec3 he = c.halfExtents;
    const glm::vec3 q = ClosestOnSegment(p0, p1, t.position);   // point on capsule axis
    const glm::vec3 dl = q - t.position;
    glm::vec3 local(glm::dot(dl, ax[0]), glm::dot(dl, ax[1]), glm::dot(dl, ax[2]));
    glm::vec3 clamped = glm::clamp(local, -he, he);
    const bool inside = (clamped == local);
    if (inside) {
        int best = 0; float bestFace = he[0] - std::fabs(local[0]);
        for (int i = 1; i < 3; ++i) { const float f = he[i] - std::fabs(local[i]); if (f < bestFace) { bestFace = f; best = i; } }
        const float sign = (local[best] >= 0.0f) ? 1.0f : -1.0f;
        out.hit = true;
        out.normal = sign * ax[best];
        out.depth = r + bestFace;
        return out;
    }

    const glm::vec3 cp = t.position + clamped[0]*ax[0] + clamped[1]*ax[1] + clamped[2]*ax[2];
    const glm::vec3 delta = q - cp;
    const float dist = glm::length(delta);
    if (dist < r) {
        out.hit = true;
        out.normal = (dist > 1e-6f) ? delta / dist : glm::vec3(0, 1, 0);
        out.depth = r - dist;
    }
    return out;
}

} // namespace

void CharacterController::GatherCandidates(
    ecs::Registry& reg, const glm::vec3& p0, const glm::vec3& p1,
    std::vector<std::pair<Transform, Collider>>& out) const {
    out.clear();
    // World AABB of the capsule (segment +/- radius, small margin for the step/probe slack).
    const glm::vec3 pad(radius + 0.05f);
    const glm::vec3 mn = glm::min(p0, p1) - pad;
    const glm::vec3 mx = glm::max(p0, p1) + pad;
    const auto accept = [&](Transform& t, Collider& c) {
        if (c.isTrigger || (c.layer & collisionMask) == 0u) return;
        // Canonical world collider (Pass-1): respect the collider's local
        // position/rotation/scale + inherited owner scale, exactly like the solver/queries.
        const physics::WorldCollider w = physics::BuildWorldCollider(t, c);
        out.emplace_back(w.transform, w.collider);
    };
    // Broad phase: only nearby candidates when the physics grid is fresh (during play).
    if (m_world && m_world->BroadphaseValid()) {
        std::vector<ecs::Entity> ents;
        if (m_world->GatherBroadphaseCandidates(mn, mx, ents)) {
            for (ecs::Entity e : ents) {
                Transform* t = reg.TryGet<Transform>(e);
                Collider* c = reg.TryGet<Collider>(e);
                if (t && c) accept(*t, *c);
            }
            return;
        }
    }
    // Fallback: exact full scan (editor / no valid grid).
    reg.view<Transform, Collider>().each([&](Entity, Transform& t, Collider& c) { accept(t, c); });
}

bool CharacterController::ResolvePenetrations(ecs::Registry& reg) {
    const float halfSeg = std::max(0.0f, height * 0.5f - radius);
    bool touchedWall = false;
    for (int it = 0; it < depenetrationIters; ++it) {
        Pen best;
        const glm::vec3 p0 = position - glm::vec3(0, halfSeg, 0);
        const glm::vec3 p1 = position + glm::vec3(0, halfSeg, 0);
        std::vector<std::pair<Transform, Collider>> candidates;
        GatherCandidates(reg, p0, p1, candidates);
        for (const auto& wc : candidates) {
            const Pen p = CapsuleVsCollider(p0, p1, radius, wc.first, wc.second);
            if (p.hit && p.depth > best.depth) best = p;
        }
        if (!best.hit) break;
        position += best.normal * best.depth;            // push out
        const float vn = glm::dot(velocity, best.normal);
        if (vn < 0.0f) velocity -= best.normal * vn;     // slide along the surface
        if (best.normal.y > maxSlopeCos) { grounded = true; groundNormal = best.normal; }
        else                             { touchedWall = true; }    // steep: a wall/step
    }
    return touchedWall;
}

RaycastHit CharacterController::SweepCapsule(ecs::Registry& reg, const glm::vec3& from,
                                            const glm::vec3& to, float r, float segHalf,
                                            const glm::vec3& up) const {
    if (!m_world) return RaycastHit{};
    // collisionMask filters which collider layers block the character (excludes triggers/etc).
    return m_world->CapsuleCast(reg, from, to, r, segHalf, up, ecs::kNull, collisionMask);
}

void CharacterController::TryStep(ecs::Registry& reg, const glm::vec3& startPos,
                                  const glm::vec3& wantHoriz, float segHalf, const glm::vec3& up) {
    const float skin = contactOffset;
    const glm::vec3 dir = glm::normalize(wantHoriz);
    const float horizLen = glm::length(wantHoriz);

    // UP: how far can we rise (capped at stepHeight)? If blocked immediately, no headroom.
    const RaycastHit u = SweepCapsule(reg, startPos, startPos + up * stepHeight, radius, segHalf, up);
    const float rise = u.hit ? std::max(u.distance - skin, 0.0f) : stepHeight;
    if (rise < 0.02f) return;
    const glm::vec3 raised = startPos + up * rise;

    // FORWARD from the raised position. Blocked immediately -> it's a wall, not a step.
    const RaycastHit f = SweepCapsule(reg, raised, raised + dir * horizLen, radius, segHalf, up);
    const float fwd = f.hit ? std::max(f.distance - skin, 0.0f) : horizLen;
    if (fwd < 0.02f) return;
    const glm::vec3 fore = raised + dir * fwd;

    // DOWN onto the step; accept only a walkable landing that actually rose us.
    const RaycastHit d = SweepCapsule(reg, fore, fore - up * (rise + groundProbeDistance),
                                      radius, segHalf, up);
    if (!d.hit || d.normal.y < maxSlopeCos) return;
    const glm::vec3 landed = fore - up * std::max(d.distance - skin, 0.0f);
    if (landed.y > startPos.y + 0.02f) {
        position = landed;
        grounded = true; groundNormal = d.normal; groundEntity = d.entity;
    }
}

namespace {
// Velocity of a platform at a world contact point: linear + omega x (point - COM). A platform
// with no RigidBody is static (zero). Works for kinematic and dynamic ground bodies.
glm::vec3 PlatformPointVelocity(engine::ecs::Registry& reg, engine::ecs::Entity e,
                                const glm::vec3& contactPoint) {
    using namespace engine;
    if (e == ecs::kNull) return glm::vec3(0.0f);
    ecs::RigidBody* rb = reg.TryGet<ecs::RigidBody>(e);
    ecs::Transform* t  = reg.TryGet<ecs::Transform>(e);
    if (!rb || !t) return glm::vec3(0.0f);
    const glm::vec3 com = t->position + t->rotation * rb->centerOfMassLocal;
    return rb->velocity + glm::cross(rb->angularVelocity, contactPoint - com);
}

// Push a dynamic body the character swept into. pushDir points from the character into the body
// (unit). The impulse is what would bring the body up to the character's into-speed, clamped so a
// heavy body barely moves and a light one is shoved to ~character speed.
void PushDynamicBody(engine::ecs::Registry& reg, engine::ecs::Entity e, const glm::vec3& pushDir,
                     const glm::vec3& charVel, float strength, float maxImpulse) {
    using namespace engine;
    ecs::RigidBody* rb = reg.TryGet<ecs::RigidBody>(e);
    if (!rb || rb->invMass <= 0.0f || rb->kinematic) return;   // only movable dynamic bodies
    const float vInto = glm::dot(charVel, pushDir);
    if (vInto <= 0.0f) return;                                 // not moving into it
    const float J = std::min(strength * vInto / rb->invMass, maxImpulse);
    rb->velocity += pushDir * (J * rb->invMass);
    if (rb->sleeping) { rb->sleeping = false; rb->sleepTimer = 0.0f; }
}
} // namespace

void CharacterController::Move(ecs::Registry& reg, const glm::vec3& wishVel, float dt,
                              const PhysicsWorld* world) {
    m_world = world;
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    const float segHalf = std::max(0.0f, height * 0.5f - radius);
    const float skin = contactOffset;

    // (0) Moving-platform carry (Phases 35-41): if we were standing on something last step, move
    //     with its contact-point velocity this step (before applying gravity / input). A
    //     teleporting platform reports an absurd velocity, which is clamped out (no inherited
    //     teleport speed). The carry is swept so a platform can't shove us through a wall.
    groundVelocity = PlatformPointVelocity(reg, groundEntity, groundPoint);
    if (m_world && glm::length(groundVelocity) > 1e-4f
        && glm::length(groundVelocity) < maxPlatformSpeed) {
        const glm::vec3 carry = groundVelocity * dt;
        const float cdist = glm::length(carry);
        const RaycastHit h = SweepCapsule(reg, position, position + carry, radius, segHalf, up);
        position += carry * (h.hit ? glm::clamp((h.distance - skin) / cdist, 0.0f, 1.0f) : 1.0f);
    }

    velocity.y += gravity.y * dt;
    const bool wasGrounded = grounded;
    grounded = false;
    groundNormal = glm::vec3(0.0f, 1.0f, 0.0f);
    groundEntity = ecs::kNull;

    // (1) Initial-overlap recovery ONLY (bounded). Normal movement is the sweep below -- the
    //     old full-move-then-depenetrate is gone (Pass-4 Phase 3).
    ResolvePenetrations(reg);

    const glm::vec3 startPos = position;
    const glm::vec3 wantHoriz(wishVel.x * dt, 0.0f, wishVel.z * dt);

    if (!m_world) {                    // no accelerated caster: legacy fallback (editor/no grid)
        position += glm::vec3(wishVel.x * dt, velocity.y * dt, wishVel.z * dt);
        ResolvePenetrations(reg);
        if (grounded && velocity.y < 0.0f) velocity.y = 0.0f;
        return;
    }

    // (2) Iterative sweep-and-slide (Phases 6-11). A surface only blocks when motion points INTO
    //     it (dot < 0); a surface we are moving along/away from (e.g. the floor while walking or
    //     jumping) does not stop or reproject us -- otherwise a resting capsule could never jump.
    glm::vec3 remaining(wishVel.x * dt, velocity.y * dt, wishVel.z * dt);
    glm::vec3 planes[4]; int nplanes = 0;
    bool ceilingHit = false;
    for (int iter = 0; iter < maxSlideIterations; ++iter) {
        const float dist = glm::length(remaining);
        if (dist < 1e-5f) break;
        const RaycastHit hit = SweepCapsule(reg, position, position + remaining, radius, segHalf, up);
        if (!hit.hit) { position += remaining; break; }                 // free path: move all
        const glm::vec3 n = hit.normal;
        if (glm::dot(remaining, n) >= 0.0f) { position += remaining; break; }  // not blocking
        // Push a dynamic body we're moving into (Phases 32-34), before sliding off it.
        if (pushDynamicBodies && dt > 0.0f)
            PushDynamicBody(reg, hit.entity, -n, remaining / dt, pushStrength, maxPushImpulse);
        const float advance = glm::clamp((hit.distance - skin) / dist, 0.0f, 1.0f);
        position += remaining * advance;                                // move to TOI - skin
        remaining -= remaining * advance;

        if (n.y >= maxSlopeCos) { grounded = true; groundNormal = n; groundEntity = hit.entity; }
        else if (n.y < -0.2f)   { ceilingHit = true; }

        if (nplanes < 4) planes[nplanes++] = n;
        remaining = remaining - n * glm::dot(remaining, n);             // slide along the plane
        // Two-plane crease (Phase 10): if sliding now drives back into an earlier plane, constrain
        // motion to the crease direction so inside corners don't oscillate.
        for (int p = 0; p < nplanes - 1; ++p) {
            if (glm::dot(remaining, planes[p]) < 0.0f) {
                glm::vec3 crease = glm::cross(planes[p], n);
                const float cl = glm::length(crease);
                remaining = (cl > 1e-5f) ? (crease / cl) * glm::dot(remaining, crease / cl)
                                         : glm::vec3(0.0f);
            }
        }
    }

    // (3) Ceiling: stop upward motion, do NOT mark grounded (Phase 11).
    if (ceilingHit && velocity.y > 0.0f) velocity.y = 0.0f;

    // (4) Step-up/forward/down (Phases 20-26): if a wall clipped horizontal motion while grounded
    //     and we are not rising. Sweep-based, not depenetration.
    const glm::vec3 gotHoriz(position.x - startPos.x, 0.0f, position.z - startPos.z);
    if (wasGrounded && velocity.y <= 0.05f && glm::length(wantHoriz) > 1e-4f
        && glm::length(gotHoriz) < glm::length(wantHoriz) * 0.85f) {
        TryStep(reg, startPos, wantHoriz, segHalf, up);
    }

    // (5) Ground detect + snap (Phases 13/14). One downward cast: snaps the feet to a skin height
    //     above a walkable surface within range, keeping the capsule slightly separated so the
    //     horizontal sweeps above never self-hit the floor. Uses the larger snap range when we
    //     were grounded (to hug descending slopes/stairs), the short probe otherwise, and never
    //     runs while moving upward -- so a jumping character is not glued to the floor.
    if (velocity.y <= 0.05f) {
        const float range = (wasGrounded ? groundSnapDistance : groundProbeDistance) + skin;
        const RaycastHit down = SweepCapsule(reg, position, position - up * range,
                                             radius, segHalf, up);
        if (down.hit && down.normal.y >= maxSlopeCos) {
            position -= up * std::max(down.distance - skin, 0.0f);
            grounded = true; groundNormal = down.normal; groundEntity = down.entity;
            groundPoint = down.point;   // remembered for next step's platform carry
        }
    }

    if (grounded && velocity.y < 0.0f) velocity.y = 0.0f;
}

void CharacterController::MoveFree(ecs::Registry& reg, const glm::vec3& wishVel, float dt,
                                  const PhysicsWorld* world) {
    m_world = world;
    const float safeDt = std::max(dt, 0.0f);
    velocity = wishVel;
    grounded = false;
    groundNormal = glm::vec3(0.0f, 1.0f, 0.0f);
    position += wishVel * safeDt;
    ResolvePenetrations(reg);
    // Contact with a floor while swimming must not turn the controller into a
    // grounded walker; the water movement mode remains authoritative.
    grounded = false;
}

bool CharacterController::TrySetHeight(ecs::Registry& reg, float newHeight,
                                      const PhysicsWorld* world) {
    m_world = world;
    newHeight = std::max(newHeight, radius * 2.0f);
    if (std::abs(newHeight - height) <= 0.00001f) return true;

    const float oldHeight = height;
    const glm::vec3 oldPosition = position;
    const float feetY = position.y - oldHeight * 0.5f;
    height = newHeight;
    position.y = feetY + newHeight * 0.5f;

    if (newHeight < oldHeight) return true;

    const float halfSeg = std::max(0.0f, height * 0.5f - radius);
    const glm::vec3 p0 = position - glm::vec3(0.0f, halfSeg, 0.0f);
    const glm::vec3 p1 = position + glm::vec3(0.0f, halfSeg, 0.0f);
    bool blocked = false;
    std::vector<std::pair<Transform, Collider>> candidates;
    GatherCandidates(reg, p0, p1, candidates);
    for (const auto& wc : candidates) {
        if (blocked) break;
        const Pen penetration = CapsuleVsCollider(p0, p1, radius, wc.first, wc.second);
        // Ignore tiny contact noise at the feet, but reject actual overlap in the
        // additional standing volume.
        if (penetration.hit && penetration.depth > 0.001f) blocked = true;
    }
    if (blocked) {
        height = oldHeight;
        position = oldPosition;
        return false;
    }
    return true;
}

} // namespace engine
