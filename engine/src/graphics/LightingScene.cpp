#include "engine/graphics/LightingScene.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>

namespace engine {

void LightingSceneBvh::Reset() {
    m_triangles.clear();
    m_order.clear();
    m_nodes.clear();
    m_boundsMin = glm::vec3(0.0f);
    m_boundsMax = glm::vec3(0.0f);
}

void LightingSceneBvh::Build(const std::vector<LightingTriangle>& triangles) {
    Reset();
    m_triangles = triangles;
    if (m_triangles.empty()) return;
    m_order.resize(m_triangles.size());
    std::iota(m_order.begin(), m_order.end(), 0u);
    m_nodes.reserve(m_triangles.size() * 2u);
    BuildNode(0, static_cast<std::uint32_t>(m_order.size()));
    m_boundsMin = m_nodes.front().lo;
    m_boundsMax = m_nodes.front().hi;
}

bool LightingSceneBvh::RayTriangle(const glm::vec3& origin,
                                   const glm::vec3& direction,
                                   const LightingTriangle& triangle,
                                   float maximumDistance,
                                   float* hitDistance,
                                   glm::vec3* barycentric) {
    const glm::vec3 e1 = triangle.b - triangle.a;
    const glm::vec3 e2 = triangle.c - triangle.a;
    const glm::vec3 p = glm::cross(direction, e2);
    const float determinant = glm::dot(e1, p);
    if (std::abs(determinant) < 1e-7f) return false;
    const float inverse = 1.0f / determinant;
    const glm::vec3 t = origin - triangle.a;
    const float u = glm::dot(t, p) * inverse;
    if (u < 0.0f || u > 1.0f) return false;
    const glm::vec3 q = glm::cross(t, e1);
    const float v = glm::dot(direction, q) * inverse;
    if (v < 0.0f || u + v > 1.0f) return false;
    const float distance = glm::dot(e2, q) * inverse;
    if (distance <= 0.015f || distance >= maximumDistance) return false;
    if (hitDistance) *hitDistance = distance;
    if (barycentric) *barycentric = glm::vec3(1.0f - u - v, u, v);
    return true;
}

bool LightingSceneBvh::HitBox(const glm::vec3& origin,
                              const glm::vec3& direction,
                              const glm::vec3& lo, const glm::vec3& hi,
                              float maximumDistance) {
    const glm::vec3 safe(
        std::abs(direction.x) < 1e-8f ? std::copysign(1e-8f, direction.x) : direction.x,
        std::abs(direction.y) < 1e-8f ? std::copysign(1e-8f, direction.y) : direction.y,
        std::abs(direction.z) < 1e-8f ? std::copysign(1e-8f, direction.z) : direction.z);
    const glm::vec3 a = (lo - origin) / safe;
    const glm::vec3 b = (hi - origin) / safe;
    const glm::vec3 nearValue = glm::min(a, b);
    const glm::vec3 farValue = glm::max(a, b);
    return std::max({nearValue.x, nearValue.y, nearValue.z, 0.0f})
        <= std::min({farValue.x, farValue.y, farValue.z, maximumDistance});
}

glm::vec3 LightingSceneBvh::Centroid(const LightingTriangle& triangle) {
    return (triangle.a + triangle.b + triangle.c) / 3.0f;
}

int LightingSceneBvh::BuildNode(std::uint32_t begin, std::uint32_t count) {
    const int index = static_cast<int>(m_nodes.size());
    m_nodes.emplace_back();
    Node& node = m_nodes.back();
    node.begin = begin;
    node.count = count;
    node.lo = glm::vec3(std::numeric_limits<float>::max());
    node.hi = -node.lo;
    glm::vec3 centroidLo = node.lo;
    glm::vec3 centroidHi = node.hi;
    for (std::uint32_t i = 0; i < count; ++i) {
        const LightingTriangle& triangle = m_triangles[m_order[begin + i]];
        node.lo = glm::min(node.lo, glm::min(triangle.a, glm::min(triangle.b, triangle.c)));
        node.hi = glm::max(node.hi, glm::max(triangle.a, glm::max(triangle.b, triangle.c)));
        const glm::vec3 centroid = Centroid(triangle);
        centroidLo = glm::min(centroidLo, centroid);
        centroidHi = glm::max(centroidHi, centroid);
    }
    if (count <= 8) return index;
    const glm::vec3 extent = centroidHi - centroidLo;
    int axis = extent.y > extent.x ? 1 : 0;
    if (extent.z > extent[axis]) axis = 2;
    const std::uint32_t middle = begin + count / 2;
    std::nth_element(m_order.begin() + begin, m_order.begin() + middle,
                     m_order.begin() + begin + count,
                     [&](std::uint32_t a, std::uint32_t b) {
                         return Centroid(m_triangles[a])[axis]
                              < Centroid(m_triangles[b])[axis];
                     });
    const int left = BuildNode(begin, middle - begin);
    const int right = BuildNode(middle, begin + count - middle);
    m_nodes[index].left = left;
    m_nodes[index].right = right;
    m_nodes[index].count = 0;
    return index;
}

bool LightingSceneBvh::Occluded(const glm::vec3& origin,
                                const glm::vec3& inputDirection,
                                float maximumDistance) const {
    if (m_nodes.empty() || maximumDistance <= 0.0f) return false;
    const float lengthSquared = glm::dot(inputDirection, inputDirection);
    if (lengthSquared < 1e-12f) return false;
    const glm::vec3 direction = inputDirection / std::sqrt(lengthSquared);
    std::array<int, 128> stack{};
    int top = 0;
    stack[top++] = 0;
    while (top) {
        const Node& node = m_nodes[stack[--top]];
        if (!HitBox(origin, direction, node.lo, node.hi, maximumDistance)) continue;
        if (node.left < 0) {
            for (std::uint32_t i = 0; i < node.count; ++i) {
                if (RayTriangle(origin, direction,
                                m_triangles[m_order[node.begin + i]],
                                maximumDistance, nullptr, nullptr)) return true;
            }
        } else if (top + 2 <= static_cast<int>(stack.size())) {
            stack[top++] = node.left;
            stack[top++] = node.right;
        }
    }
    return false;
}

bool LightingSceneBvh::Trace(const glm::vec3& origin,
                             const glm::vec3& inputDirection,
                             float maximumDistance,
                             LightingRayHit* result) const {
    if (result) *result = LightingRayHit{};
    if (m_nodes.empty() || maximumDistance <= 0.0f) return false;
    const float lengthSquared = glm::dot(inputDirection, inputDirection);
    if (lengthSquared < 1e-12f) return false;
    const glm::vec3 direction = inputDirection / std::sqrt(lengthSquared);
    std::array<int, 128> stack{};
    int top = 0;
    stack[top++] = 0;
    float nearest = maximumDistance;
    std::uint32_t nearestIndex = 0;
    glm::vec3 nearestBarycentric(0.0f);
    bool found = false;
    while (top) {
        const Node& node = m_nodes[stack[--top]];
        if (!HitBox(origin, direction, node.lo, node.hi, nearest)) continue;
        if (node.left < 0) {
            for (std::uint32_t i = 0; i < node.count; ++i) {
                const std::uint32_t triangleIndex = m_order[node.begin + i];
                float distance = 0.0f;
                glm::vec3 barycentric;
                if (RayTriangle(origin, direction, m_triangles[triangleIndex], nearest,
                                &distance, &barycentric)) {
                    nearest = distance;
                    nearestIndex = triangleIndex;
                    nearestBarycentric = barycentric;
                    found = true;
                }
            }
        } else if (top + 2 <= static_cast<int>(stack.size())) {
            stack[top++] = node.left;
            stack[top++] = node.right;
        }
    }
    if (found && result) {
        const LightingTriangle& triangle = m_triangles[nearestIndex];
        result->hit = true;
        result->distance = nearest;
        result->position = origin + direction * nearest;
        result->geometricNormal = glm::normalize(glm::cross(
            triangle.b - triangle.a, triangle.c - triangle.a));
        if (glm::dot(result->geometricNormal, direction) > 0.0f)
            result->geometricNormal = -result->geometricNormal;
        const glm::vec3 interpolated =
            triangle.normalA * nearestBarycentric.x
            + triangle.normalB * nearestBarycentric.y
            + triangle.normalC * nearestBarycentric.z;
        result->shadingNormal = glm::dot(interpolated, interpolated) > 1e-10f
            ? glm::normalize(interpolated) : result->geometricNormal;
        if (glm::dot(result->shadingNormal, result->geometricNormal) < 0.0f)
            result->shadingNormal = -result->shadingNormal;
        if (glm::dot(result->shadingNormal, direction) > 0.0f)
            result->shadingNormal = result->geometricNormal;
        result->normal = result->shadingNormal;
        result->barycentric = nearestBarycentric;
        result->uv = triangle.uvA * nearestBarycentric.x
            + triangle.uvB * nearestBarycentric.y
            + triangle.uvC * nearestBarycentric.z;
        result->albedo = triangle.albedo;
        result->emissive = triangle.emissive;
        result->metallic = glm::clamp(triangle.metallic, 0.0f, 1.0f);
        result->baseColorTexture = triangle.baseColorTexture;
        result->entityId = triangle.entityId;
        result->materialSlot = triangle.materialSlot;
        result->triangleIndex = nearestIndex;
    }
    return found;
}

} // namespace engine
