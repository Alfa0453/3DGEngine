#pragma once

// Pass-5 "Validate Physics" scan (Phase 67). A pure, read-only pass over the scene's colliders and
// rigid bodies that reports the common configuration mistakes that silently break simulation:
// invalid dimensions, bad mass, non-finite transforms/velocities, unsupported dynamic mesh
// colliders, degenerate rotations, and filters that collide with nothing. Header-only (uses the
// ECS registry directly) so it needs no new .cpp / CMake reconfigure. The editor's "Validate
// Physics" command and unit tests both call this; it never mutates the scene.

#include "engine/ecs/Registry.h"
#include "engine/ecs/Components.h"
#include "engine/physics/PhysicsComponents.h"

#include <cmath>
#include <string>
#include <vector>

namespace engine {

struct PhysicsIssue {
    enum class Severity { Warning, Error };
    ecs::Entity entity = ecs::kNull;
    Severity    severity = Severity::Error;
    std::string message;
};

namespace detail {
inline bool Finite(const glm::vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
inline bool Finite(const glm::quat& q) { return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w); }

// Check one collider's authored dimensions for the shape it claims to be.
inline void CheckColliderDims(ecs::Entity e, const ecs::Collider& c, std::vector<PhysicsIssue>& out) {
    const auto err = [&](std::string m) { out.push_back({e, PhysicsIssue::Severity::Error, std::move(m)}); };
    switch (c.shape) {
        case ecs::ColliderShape::Sphere:
            if (!(c.radius > 0.0f)) err("Sphere collider has non-positive radius");
            break;
        case ecs::ColliderShape::Box:
        case ecs::ColliderShape::ConvexHull:
        case ecs::ColliderShape::TriangleMesh:
        case ecs::ColliderShape::Pyramid:
        case ecs::ColliderShape::Staircase:
            if (!(c.halfExtents.x > 0.0f && c.halfExtents.y > 0.0f && c.halfExtents.z > 0.0f))
                err("Box/hull/mesh collider has a non-positive half-extent");
            break;
        case ecs::ColliderShape::Capsule:
            if (!(c.radius > 0.0f)) err("Capsule collider has non-positive radius");
            if (!(c.halfHeight >= 0.0f)) err("Capsule collider has negative half-height");
            break;
        case ecs::ColliderShape::Cylinder:
        case ecs::ColliderShape::Cone:
            if (!(c.radius > 0.0f)) err("Cylinder/cone collider has non-positive radius");
            if (!(c.halfHeight > 0.0f)) err("Cylinder/cone collider has non-positive half-height");
            break;
        case ecs::ColliderShape::Torus:
            if (!(c.majorRadius > 0.0f) || !(c.minorRadius > 0.0f)) err("Torus collider has non-positive radii");
            break;
        case ecs::ColliderShape::Plane: {
            const float n2 = glm::dot(c.planeNormal, c.planeNormal);
            if (!(n2 > 1e-8f)) err("Plane collider has a degenerate (zero) normal");
            break;
        }
        default: break;
    }
    const float ql = glm::length(c.localRotation);
    if (!std::isfinite(ql) || ql < 1e-4f) err("Collider local rotation is a degenerate quaternion");
    if (c.layer == 0u) out.push_back({e, PhysicsIssue::Severity::Warning, "Collider layer is 0 (bit-empty) -- it may collide with nothing"});
    if (c.mask == 0u)  out.push_back({e, PhysicsIssue::Severity::Warning, "Collider mask is 0 -- this collider ignores every other layer"});
    if (c.density < 0.0f) err("Collider density is negative");
}
} // namespace detail

// Scan the whole scene. Returns one issue per problem found (empty == clean).
inline std::vector<PhysicsIssue> ValidatePhysics(ecs::Registry& reg) {
    using namespace detail;
    std::vector<PhysicsIssue> issues;

    reg.view<ecs::Transform, ecs::Collider>().each(
        [&](ecs::Entity e, ecs::Transform& t, ecs::Collider& c) {
            if (!Finite(t.position)) issues.push_back({e, PhysicsIssue::Severity::Error, "Transform position is NaN/inf"});
            if (!Finite(t.rotation)) issues.push_back({e, PhysicsIssue::Severity::Error, "Transform rotation is NaN/inf"});
            if (!Finite(t.scale))    issues.push_back({e, PhysicsIssue::Severity::Error, "Transform scale is NaN/inf"});
            CheckColliderDims(e, c, issues);

            if (ecs::RigidBody* rb = reg.TryGet<ecs::RigidBody>(e)) {
                const bool dynamic = rb->invMass > 0.0f && !rb->kinematic;
                if (rb->invMass < 0.0f)
                    issues.push_back({e, PhysicsIssue::Severity::Error, "RigidBody has negative inverse mass"});
                if (!std::isfinite(rb->invMass))
                    issues.push_back({e, PhysicsIssue::Severity::Error, "RigidBody inverse mass is NaN/inf"});
                if (!Finite(rb->velocity))
                    issues.push_back({e, PhysicsIssue::Severity::Error, "RigidBody linear velocity is NaN/inf"});
                if (!Finite(rb->angularVelocity))
                    issues.push_back({e, PhysicsIssue::Severity::Error, "RigidBody angular velocity is NaN/inf"});
                if (dynamic && c.shape == ecs::ColliderShape::TriangleMesh)
                    issues.push_back({e, PhysicsIssue::Severity::Error,
                        "Dynamic body with a TriangleMesh collider is unsupported -- use ConvexHull (it falls back to a bounds box)"});
                if (rb->massMode == ecs::RigidBody::MassMode::Density && dynamic && !(c.density > 0.0f))
                    issues.push_back({e, PhysicsIssue::Severity::Error, "Density mass mode needs a positive collider density"});
            }
        });
    return issues;
}

// Convenience: count only errors (warnings excluded).
inline int PhysicsErrorCount(const std::vector<PhysicsIssue>& issues) {
    int n = 0;
    for (const PhysicsIssue& i : issues) if (i.severity == PhysicsIssue::Severity::Error) ++n;
    return n;
}

} // namespace engine
