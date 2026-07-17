#include "engine/physics/CharacterController.h"

#include "engine/physics/PhysicsComponents.h"
#include "engine/ecs/Registry.h"
#include "engine/ecs/Components.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

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
        const Collider proxy = Collider::MakeCapsule(c.radius,
            std::max(c.halfHeight - c.radius, 0.0f));
        return CapsuleVsCollider(p0, p1, r, t, proxy);
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

bool CharacterController::ResolvePenetrations(ecs::Registry& reg) {
    const float halfSeg = std::max(0.0f, height * 0.5f - radius);
    bool touchedWall = false;
    for (int it = 0; it < depenetrationIters; ++it) {
        Pen best;
        const glm::vec3 p0 = position - glm::vec3(0, halfSeg, 0);
        const glm::vec3 p1 = position + glm::vec3(0, halfSeg, 0);
        reg.view<Transform, Collider>().each([&](Entity, Transform& t, Collider& c) {
            if (c.isTrigger) return;
            const Pen p = CapsuleVsCollider(p0, p1, radius, t, c);
            if (p.hit && p.depth > best.depth) best = p;
        });
        if (!best.hit) break;
        position += best.normal * best.depth;            // push out
        const float vn = glm::dot(velocity, best.normal);
        if (vn < 0.0f) velocity -= best.normal * vn;     // slide along the surface
        if (best.normal.y > maxSlopeCos) { grounded = true; groundNormal = best.normal; }
        else                             { touchedWall = true; }    // steep: a wall/step
    }
    return touchedWall;
}

void CharacterController::Move(ecs::Registry& reg, const glm::vec3& wishVel, float dt) {
    velocity.y += gravity.y * dt;
    const bool wasGrounded = grounded;
    grounded = false;

    const glm::vec3 disp(wishVel.x * dt, velocity.y * dt, wishVel.z * dt);
    const glm::vec3 startPos = position;

    position += disp;
    const bool blockedByWall = ResolvePenetrations(reg);

    // Step-up: if we were on the ground but an obstacle blocked our horizontal
    // motion, try to climb it. Lift by stepHeight, nudge forward far enough to
    // clear the ledge edge (per-frame motion is too small on its own), then drop
    // back down. A wall taller than stepHeight still blocks the forward nudge and
    // the attempt is reverted.
    const glm::vec3 horizWanted(disp.x, 0.0f, disp.z);
    const glm::vec3 horizGot(position.x - startPos.x, 0.0f, position.z - startPos.z);
    if (wasGrounded && blockedByWall && glm::length(horizWanted) > 1e-5f &&
        glm::length(horizGot) < glm::length(horizWanted) * 0.9f) {
        const glm::vec3 plainPos = position;
        const glm::vec3 plainVel = velocity;
        const glm::vec3 dir = glm::normalize(horizWanted);
        position = startPos + glm::vec3(0, stepHeight, 0);
        ResolvePenetrations(reg);
        position += dir * (radius + 0.05f);             // clear the ledge edge
        ResolvePenetrations(reg);
        position += glm::vec3(0, -(stepHeight + 0.02f), 0);     // drop onto the step
        ResolvePenetrations(reg);
        const bool rose = position.y > plainPos.y + 0.05f;
        if (!(grounded && rose)) { position = plainPos; velocity = plainVel; }  // no step: revert
    }

    // Stable ground probe: count as grounded if a walkable surface is just below.
    {
        const glm::vec3 save = position;
        position.y -= 0.05f;
        const float halfSeg = std::max(0.0f, height * 0.5f - radius);
        const glm::vec3 p0 = position - glm::vec3(0, halfSeg, 0);
        const glm::vec3 p1 = position + glm::vec3(0, halfSeg, 0);
        reg.view<Transform, Collider>().each([&](Entity, Transform& t, Collider& c) {
            if (c.isTrigger) return;
            const Pen p = CapsuleVsCollider(p0, p1, radius, t, c);
            if (p.hit && p.normal.y > maxSlopeCos) { grounded = true; groundNormal = p.normal; }
        });
        position = save;
    }

    if (grounded && velocity.y < 0.0f) velocity.y = 0.0f;
}

} // namespace engine
