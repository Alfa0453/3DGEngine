#pragma once

#include "engine/ecs/Components.h"

#include <glm/glm.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace engine::physics {

struct CollisionTriangle {
    glm::vec3 a{0.0f};
    glm::vec3 b{0.0f};
    glm::vec3 c{0.0f};
};

struct CollisionEdge {
    glm::vec3 a{0.0f};
    glm::vec3 b{0.0f};
};

struct CollisionBvhNode {
    glm::vec3 minimum{0.0f};
    glm::vec3 maximum{0.0f};
    std::uint32_t first = 0;
    std::uint32_t count = 0;
    std::int32_t left = -1;
    std::int32_t right = -1;
};

// Immutable, shared CPU collision data. Multiple scene instances referencing
// one .3dgmesh share this allocation and BVH instead of recooking per object.
struct CollisionMeshData {
    std::string sourcePath;
    std::vector<CollisionTriangle> triangles;
    std::vector<std::uint32_t> order;
    std::vector<CollisionBvhNode> nodes;
    // Crease/boundary edges of the convex envelope. Cached with the triangle BVH
    // so both editor previews draw the same hull that support-mapped physics uses.
    std::vector<CollisionEdge> convexHullEdges;
    glm::vec3 minimum{0.0f};
    glm::vec3 maximum{0.0f};
    double cookMilliseconds = 0.0;
    std::size_t SourceBytes() const;
    std::size_t CookedBytes() const;
};

std::shared_ptr<const CollisionMeshData> AcquireCollisionMesh(
    const std::string& path, std::string* error = nullptr);
void InvalidateCollisionMesh(const std::string& path);
void ClearCollisionMeshCache();

// Ray direction must be normalized. Returns a world-space distance and normal.
bool RaycastCollisionMesh(const CollisionMeshData& mesh,
                          const ecs::Transform& transform,
                          const glm::vec3& origin,
                          const glm::vec3& direction,
                          float maxDistance,
                          float* distance,
                          glm::vec3* normal);

bool CollideSphereCollisionMesh(const CollisionMeshData& mesh,
                                const ecs::Transform& transform,
                                const glm::vec3& center,
                                float radius,
                                glm::vec3* point,
                                glm::vec3* sphereToMeshNormal,
                                float* penetration);

// Sweeps a world-space sphere against the shared local-space triangle mesh.
// Returns a normalized time of impact in [0, 1].
bool SweepSphereCollisionMesh(const CollisionMeshData& mesh,
                              const ecs::Transform& transform,
                              const glm::vec3& start,
                              const glm::vec3& end,
                              float radius,
                              float* timeOfImpact,
                              glm::vec3* normal);

} // namespace engine::physics
