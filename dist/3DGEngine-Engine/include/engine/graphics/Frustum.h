#pragma once

#include <glm/glm.hpp>

namespace engine {

// The six clip planes of a view-projection, each as (nx, ny, nz, d) with the
// normal pointing INTO the frustum: a point p is inside a plane when
// dot(plane.xyz, p) + plane.w >= 0. Used to skip off-screen objects.
struct Frustum {
    glm::vec4 planes[6];   // left, right, bottom, top, near, far
};

// Gribb-Hartmann extraction from a view-projection matrix (glm column-major).
inline Frustum ExtractFrustum(const glm::mat4& m) {
    auto row = [&](int r) { return glm::vec4(m[0][r], m[1][r], m[2][r], m[3][r]); };
    const glm::vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
    Frustum f;
    f.planes[0] = r3 + r0;   // left
    f.planes[1] = r3 - r0;   // right
    f.planes[2] = r3 + r1;   // bottom
    f.planes[3] = r3 - r1;   // top
    f.planes[4] = r3 + r2;   // near
    f.planes[5] = r3 - r2;   // far
    for (auto& p : f.planes) {
        const float len = glm::length(glm::vec3(p));
        if (len > 0.0f) p /= len;
    }
    return f;
}

// A sphere is visible unless it is entirely behind some plane.
inline bool SphereInFrustum(const Frustum& f, const glm::vec3& center, float radius) {
    for (const auto& p : f.planes)
        if (glm::dot(glm::vec3(p), center) + p.w < -radius) return false;
    return true;
}

// An AABB is visible unless its "positive vertex" is behind some plane.
inline bool AABBInFrustum(const Frustum& f, const glm::vec3& mn, const glm::vec3& mx) {
    for (const auto& p : f.planes) {
        const glm::vec3 pv(p.x >= 0.0f ? mx.x : mn.x,
                           p.y >= 0.0f ? mx.y : mn.y,
                           p.z >= 0.0f ? mx.z : mn.z);
        if (glm::dot(glm::vec3(p), pv) + p.w < 0.0f) return false;
    }
    return true;
}

} // namespace engine