#pragma once

#include "engine/ecs/Components.h"
#include "engine/physics/PhysicsComponents.h"

#include <glm/common.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace engine::physics {

struct WorldCollider {
    ecs::Transform transform;
    ecs::Collider collider;
};

// Converts a local collider into the world-space primitive consumed by the
// existing solver. This centralizes scale policy for physics, queries and editor
// debug drawing and prevents procedural generators from baking scale twice.
inline WorldCollider BuildWorldCollider(const ecs::Transform& owner,
                                        const ecs::Collider& local) {
    WorldCollider world;
    world.collider = local;
    const glm::vec3 ownerScale = local.inheritTransformScale
        ? owner.scale : glm::vec3(1.0f);
    const glm::vec3 signedScale = ownerScale * local.localScale;
    const glm::vec3 scale = glm::max(glm::abs(signedScale), glm::vec3(0.000001f));

    world.transform.position = owner.position
        + owner.rotation * (ownerScale * local.localPosition);
    world.transform.rotation = glm::normalize(owner.rotation * local.localRotation);
    world.transform.scale = glm::vec3(1.0f);

    switch (local.shape) {
    case ecs::ColliderShape::Sphere:
        world.collider.radius *= std::max({scale.x, scale.y, scale.z});
        break;
    case ecs::ColliderShape::Capsule:
        world.collider.radius *= std::max(scale.x, scale.z);
        world.collider.halfHeight *= scale.y;
        world.collider.halfExtents = glm::vec3(world.collider.radius,
            world.collider.radius + world.collider.halfHeight,
            world.collider.radius);
        break;
    case ecs::ColliderShape::Cylinder:
    case ecs::ColliderShape::Cone:
        world.collider.radius *= std::max(scale.x, scale.z);
        world.collider.halfHeight *= scale.y;
        world.collider.halfExtents = glm::vec3(world.collider.radius,
            world.collider.halfHeight, world.collider.radius);
        break;
    case ecs::ColliderShape::Torus: {
        const float radial = std::max(scale.x, scale.z);
        world.collider.majorRadius *= radial;
        world.collider.minorRadius *= std::max(radial, scale.y);
        const float outer = world.collider.majorRadius + world.collider.minorRadius;
        world.collider.halfExtents = glm::vec3(outer, world.collider.minorRadius, outer);
        break;
    }
    case ecs::ColliderShape::Plane: {
        const glm::vec3 normal = glm::normalize(world.transform.rotation * local.planeNormal);
        world.collider.planeNormal = normal;
        world.collider.planeOffset = glm::dot(normal, world.transform.position)
            + local.planeOffset;
        break;
    }
    case ecs::ColliderShape::Box:
    case ecs::ColliderShape::Pyramid:
    case ecs::ColliderShape::Staircase:
        world.collider.halfExtents *= scale;
        break;
    case ecs::ColliderShape::ConvexHull:
    case ecs::ColliderShape::TriangleMesh:
        world.collider.halfExtents *= scale;
        // Exact mesh queries need the original signed scale to transform the
        // cooked local-space triangles. Primitive solvers consume dimensions
        // that have already been scaled above and therefore keep unit scale.
        world.transform.scale = signedScale;
        break;
    }

    world.collider.localPosition = glm::vec3(0.0f);
    world.collider.localRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    world.collider.localScale = glm::vec3(1.0f);
    world.collider.inheritTransformScale = false;
    return world;
}

} // namespace engine::physics
