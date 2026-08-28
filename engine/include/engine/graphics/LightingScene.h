#pragma once

#include "engine/graphics/LightingBuildData.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace engine {

// Immutable, material-aware acceleration structure shared by baked and dynamic
// diffuse-light transport. After Build() returns, Trace/Occluded are safe to call
// concurrently because the query path performs no mutation or allocation.
class LightingSceneBvh {
public:
    LightingSceneBvh() = default;
    explicit LightingSceneBvh(const std::vector<LightingTriangle>& triangles) {
        Build(triangles);
    }

    void Build(const std::vector<LightingTriangle>& triangles);
    void Reset();

    bool Trace(const glm::vec3& origin, const glm::vec3& direction,
               float maximumDistance, LightingRayHit* result = nullptr) const;
    bool Occluded(const glm::vec3& origin, const glm::vec3& direction,
                  float maximumDistance) const;

    bool Empty() const { return m_triangles.empty(); }
    std::size_t TriangleCount() const { return m_triangles.size(); }
    const glm::vec3& BoundsMin() const { return m_boundsMin; }
    const glm::vec3& BoundsMax() const { return m_boundsMax; }

private:
    struct Node {
        glm::vec3 lo{0.0f}, hi{0.0f};
        std::uint32_t begin = 0, count = 0;
        int left = -1, right = -1;
    };

    static bool RayTriangle(const glm::vec3& origin, const glm::vec3& direction,
                            const LightingTriangle& triangle, float maximumDistance,
                            float* hitDistance, glm::vec3* barycentric);
    static bool HitBox(const glm::vec3& origin, const glm::vec3& direction,
                       const glm::vec3& lo, const glm::vec3& hi,
                       float maximumDistance);
    static glm::vec3 Centroid(const LightingTriangle& triangle);
    int BuildNode(std::uint32_t begin, std::uint32_t count);

    std::vector<LightingTriangle> m_triangles;
    std::vector<std::uint32_t> m_order;
    std::vector<Node> m_nodes;
    glm::vec3 m_boundsMin{0.0f}, m_boundsMax{0.0f};
};

} // namespace engine
