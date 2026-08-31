#include "engine/physics/CollisionMesh.h"

#include "engine/assets/StaticMeshAsset.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/norm.hpp>

#include <algorithm>
#include <cmath>
#include <chrono>
#include <filesystem>
#include <limits>
#include <map>
#include <mutex>
#include <numeric>
#include <unordered_map>

namespace engine::physics {
namespace {

std::mutex g_cacheMutex;
// Keep one strong reference per cooked source. Collider components retain only
// the asset path, so a weak-only cache would expire after every query and cook
// the same BVH again on the next frame.
std::unordered_map<std::string, std::shared_ptr<const CollisionMeshData>> g_cache;

std::string CacheKey(const std::string& path) {
    std::error_code ec;
    const auto canonical = std::filesystem::weakly_canonical(path, ec);
    return ec ? std::filesystem::path(path).lexically_normal().string()
              : canonical.string();
}

glm::vec3 TriangleCenter(const CollisionTriangle& t) {
    return (t.a + t.b + t.c) / 3.0f;
}

void Expand(glm::vec3& mn, glm::vec3& mx, const glm::vec3& p) {
    mn = glm::min(mn, p);
    mx = glm::max(mx, p);
}

int BuildNode(CollisionMeshData& mesh, std::uint32_t first, std::uint32_t count) {
    CollisionBvhNode node;
    node.first = first;
    node.count = count;
    node.minimum = glm::vec3(std::numeric_limits<float>::max());
    node.maximum = glm::vec3(-std::numeric_limits<float>::max());
    glm::vec3 centerMin = node.minimum, centerMax = node.maximum;
    for (std::uint32_t i = first; i < first + count; ++i) {
        const CollisionTriangle& t = mesh.triangles[mesh.order[i]];
        Expand(node.minimum, node.maximum, t.a);
        Expand(node.minimum, node.maximum, t.b);
        Expand(node.minimum, node.maximum, t.c);
        Expand(centerMin, centerMax, TriangleCenter(t));
    }
    const int index = static_cast<int>(mesh.nodes.size());
    mesh.nodes.push_back(node);
    if (count <= 8u) return index;

    const glm::vec3 span = centerMax - centerMin;
    int axis = span.y > span.x ? 1 : 0;
    if (span.z > span[axis]) axis = 2;
    const std::uint32_t middle = first + count / 2u;
    std::nth_element(mesh.order.begin() + first, mesh.order.begin() + middle,
                     mesh.order.begin() + first + count,
        [&](std::uint32_t a, std::uint32_t b) {
            return TriangleCenter(mesh.triangles[a])[axis]
                 < TriangleCenter(mesh.triangles[b])[axis];
        });
    const int left = BuildNode(mesh, first, middle - first);
    const int right = BuildNode(mesh, middle, first + count - middle);
    mesh.nodes[index].left = left;
    mesh.nodes[index].right = right;
    mesh.nodes[index].count = 0;
    return index;
}

struct HullFace {
    int a = 0, b = 0, c = 0;
    glm::vec3 normal{0.0f};
};

HullFace MakeHullFace(int a, int b, int c, const std::vector<glm::vec3>& points,
                      const glm::vec3& interior) {
    HullFace face{a, b, c};
    glm::vec3 normal = glm::cross(points[b] - points[a], points[c] - points[a]);
    const float length = glm::length(normal);
    if (length > 1.0e-8f) normal /= length;
    if (glm::dot(normal, interior - points[a]) > 0.0f) {
        std::swap(face.b, face.c);
        normal = -normal;
    }
    face.normal = normal;
    return face;
}

void BuildConvexHullEdges(CollisionMeshData& mesh) {
    std::vector<glm::vec3> points;
    points.reserve(mesh.triangles.size() * 3u);
    for (const CollisionTriangle& triangle : mesh.triangles) {
        points.push_back(triangle.a);
        points.push_back(triangle.b);
        points.push_back(triangle.c);
    }
    std::sort(points.begin(), points.end(), [](const glm::vec3& a, const glm::vec3& b) {
        if (a.x != b.x) return a.x < b.x;
        if (a.y != b.y) return a.y < b.y;
        return a.z < b.z;
    });
    points.erase(std::unique(points.begin(), points.end()), points.end());
    if (points.size() < 4u) return;

    int i0 = 0;
    int i1 = 1;
    float farthest = 0.0f;
    for (int i = 1; i < static_cast<int>(points.size()); ++i) {
        const float distance = glm::length2(points[i] - points[i0]);
        if (distance > farthest) { farthest = distance; i1 = i; }
    }
    int i2 = -1;
    float lineDistance = 0.0f;
    const glm::vec3 line = points[i1] - points[i0];
    for (int i = 0; i < static_cast<int>(points.size()); ++i) {
        const float distance = glm::length2(glm::cross(line, points[i] - points[i0]));
        if (distance > lineDistance) { lineDistance = distance; i2 = i; }
    }
    if (i2 < 0 || lineDistance < 1.0e-14f) return;
    const glm::vec3 plane = glm::normalize(glm::cross(
        points[i1] - points[i0], points[i2] - points[i0]));
    int i3 = -1;
    float planeDistance = 0.0f;
    for (int i = 0; i < static_cast<int>(points.size()); ++i) {
        const float distance = std::abs(glm::dot(plane, points[i] - points[i0]));
        if (distance > planeDistance) { planeDistance = distance; i3 = i; }
    }
    if (i3 < 0 || planeDistance < 1.0e-7f) return;

    const glm::vec3 interior = (points[i0] + points[i1] + points[i2] + points[i3]) * 0.25f;
    std::vector<HullFace> faces;
    faces.reserve(points.size() * 2u);
    faces.push_back(MakeHullFace(i0, i1, i2, points, interior));
    faces.push_back(MakeHullFace(i0, i3, i1, points, interior));
    faces.push_back(MakeHullFace(i0, i2, i3, points, interior));
    faces.push_back(MakeHullFace(i1, i3, i2, points, interior));
    const float epsilon = std::max(glm::length(mesh.maximum - mesh.minimum) * 1.0e-5f,
                                   1.0e-6f);

    for (int pointIndex = 0; pointIndex < static_cast<int>(points.size()); ++pointIndex) {
        if (pointIndex == i0 || pointIndex == i1 || pointIndex == i2 || pointIndex == i3)
            continue;
        std::vector<bool> visible(faces.size(), false);
        bool anyVisible = false;
        for (std::size_t faceIndex = 0; faceIndex < faces.size(); ++faceIndex) {
            visible[faceIndex] = glm::dot(faces[faceIndex].normal,
                points[pointIndex] - points[faces[faceIndex].a]) > epsilon;
            anyVisible |= visible[faceIndex];
        }
        if (!anyVisible) continue;

        std::vector<std::pair<int, int>> horizon;
        const auto addHorizon = [&horizon](int a, int b) {
            const auto reverse = std::find(horizon.begin(), horizon.end(), std::pair<int,int>{b, a});
            if (reverse != horizon.end()) horizon.erase(reverse);
            else horizon.emplace_back(a, b);
        };
        std::vector<HullFace> kept;
        kept.reserve(faces.size() + 8u);
        for (std::size_t faceIndex = 0; faceIndex < faces.size(); ++faceIndex) {
            if (!visible[faceIndex]) { kept.push_back(faces[faceIndex]); continue; }
            const HullFace& face = faces[faceIndex];
            addHorizon(face.a, face.b);
            addHorizon(face.b, face.c);
            addHorizon(face.c, face.a);
        }
        faces.swap(kept);
        for (const auto& edge : horizon) {
            HullFace face = MakeHullFace(edge.first, edge.second, pointIndex, points, interior);
            if (glm::dot(face.normal, face.normal) > 0.5f) faces.push_back(face);
        }
    }

    // Do not draw triangulation diagonals across a flat hull face. Retain outer
    // boundaries and genuine creases where adjacent face normals differ.
    std::map<std::pair<int, int>, std::vector<glm::vec3>> edgeNormals;
    for (const HullFace& face : faces) {
        const int edge[3][2] = {{face.a, face.b}, {face.b, face.c}, {face.c, face.a}};
        for (const auto& endpoints : edge) {
            const std::pair<int, int> key{
                std::min(endpoints[0], endpoints[1]),
                std::max(endpoints[0], endpoints[1])};
            edgeNormals[key].push_back(face.normal);
        }
    }
    mesh.convexHullEdges.clear();
    mesh.convexHullEdges.reserve(edgeNormals.size());
    for (const auto& entry : edgeNormals) {
        const auto& normals = entry.second;
        bool crease = normals.size() == 1u;
        for (std::size_t i = 1; i < normals.size() && !crease; ++i)
            crease = glm::dot(normals[0], normals[i]) < 0.9995f;
        if (crease) mesh.convexHullEdges.push_back(
            {points[entry.first.first], points[entry.first.second]});
    }
}

bool RayAabb(const glm::vec3& o, const glm::vec3& d,
             const glm::vec3& mn, const glm::vec3& mx, float limit) {
    float nearT = 0.0f, farT = limit;
    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(d[axis]) < 1.0e-8f) {
            if (o[axis] < mn[axis] || o[axis] > mx[axis]) return false;
            continue;
        }
        float a = (mn[axis] - o[axis]) / d[axis];
        float b = (mx[axis] - o[axis]) / d[axis];
        if (a > b) std::swap(a, b);
        nearT = std::max(nearT, a);
        farT = std::min(farT, b);
        if (nearT > farT) return false;
    }
    return farT >= 0.0f;
}

bool RayTriangle(const glm::vec3& o, const glm::vec3& d,
                 const CollisionTriangle& tri, float* hit) {
    const glm::vec3 e1 = tri.b - tri.a, e2 = tri.c - tri.a;
    const glm::vec3 p = glm::cross(d, e2);
    const float determinant = glm::dot(e1, p);
    if (std::abs(determinant) < 1.0e-8f) return false;
    const float inverse = 1.0f / determinant;
    const glm::vec3 s = o - tri.a;
    const float u = glm::dot(s, p) * inverse;
    if (u < 0.0f || u > 1.0f) return false;
    const float v = glm::dot(d, glm::cross(s, e1)) * inverse;
    if (v < 0.0f || u + v > 1.0f) return false;
    const float t = glm::dot(e2, glm::cross(s, e1)) * inverse;
    if (t < 0.0f) return false;
    *hit = t;
    return true;
}

} // namespace

std::size_t CollisionMeshData::SourceBytes() const {
    return triangles.size() * sizeof(CollisionTriangle);
}
std::size_t CollisionMeshData::CookedBytes() const {
    return SourceBytes() + order.size() * sizeof(std::uint32_t)
        + nodes.size() * sizeof(CollisionBvhNode)
        + convexHullEdges.size() * sizeof(CollisionEdge);
}

std::shared_ptr<const CollisionMeshData> AcquireCollisionMesh(
    const std::string& path, std::string* error) {
    if (path.empty()) { if (error) *error = "Collision mesh path is empty"; return {}; }
    const std::string key = CacheKey(path);
    {
        std::lock_guard<std::mutex> lock(g_cacheMutex);
        const auto found = g_cache.find(key);
        if (found != g_cache.end()) return found->second;
    }

    const auto cookStart = std::chrono::steady_clock::now();
    StaticMeshAssetData asset;
    if (!LoadStaticMeshAsset(path, &asset, error)) return {};
    auto cooked = std::make_shared<CollisionMeshData>();
    cooked->sourcePath = key;
    cooked->minimum = {asset.minimum[0], asset.minimum[1], asset.minimum[2]};
    cooked->maximum = {asset.maximum[0], asset.maximum[1], asset.maximum[2]};
    for (const StaticMeshSubMeshData& sub : asset.subMeshes) {
        const std::size_t count = sub.vertices.size() / kStaticMeshVertexStride;
        for (std::size_t i = 0; i + 2 < sub.indices.size(); i += 3) {
            const std::uint32_t ia = sub.indices[i], ib = sub.indices[i + 1], ic = sub.indices[i + 2];
            if (ia >= count || ib >= count || ic >= count) continue;
            const auto point = [&](std::uint32_t index) {
                const std::size_t at = static_cast<std::size_t>(index) * kStaticMeshVertexStride;
                return glm::vec3(sub.vertices[at], sub.vertices[at + 1], sub.vertices[at + 2]);
            };
            const CollisionTriangle triangle{point(ia), point(ib), point(ic)};
            const glm::vec3 area = glm::cross(triangle.b - triangle.a,
                                              triangle.c - triangle.a);
            if (glm::dot(area, area) > 1.0e-16f) cooked->triangles.push_back(triangle);
        }
    }
    if (cooked->triangles.empty()) {
        if (error) *error = "Mesh contains no valid collision triangles: " + path;
        return {};
    }
    cooked->order.resize(cooked->triangles.size());
    std::iota(cooked->order.begin(), cooked->order.end(), 0u);
    cooked->nodes.reserve(cooked->triangles.size() / 4u + 1u);
    BuildNode(*cooked, 0u, static_cast<std::uint32_t>(cooked->triangles.size()));
    BuildConvexHullEdges(*cooked);
    cooked->cookMilliseconds = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - cookStart).count();
    {
        std::lock_guard<std::mutex> lock(g_cacheMutex);
        g_cache[key] = cooked;
    }
    return cooked;
}

void InvalidateCollisionMesh(const std::string& path) {
    std::lock_guard<std::mutex> lock(g_cacheMutex);
    g_cache.erase(CacheKey(path));
}
void ClearCollisionMeshCache() {
    std::lock_guard<std::mutex> lock(g_cacheMutex);
    g_cache.clear();
}

bool RaycastCollisionMesh(const CollisionMeshData& mesh,
                          const ecs::Transform& transform,
                          const glm::vec3& origin,
                          const glm::vec3& direction,
                          float maxDistance,
                          float* distance,
                          glm::vec3* normal) {
    if (mesh.nodes.empty()) return false;
    const glm::vec3 scale = transform.scale;
    if (std::abs(scale.x) < 1e-8f || std::abs(scale.y) < 1e-8f || std::abs(scale.z) < 1e-8f)
        return false;
    const glm::quat inverseRotation = glm::inverse(transform.rotation);
    const glm::vec3 localOrigin = (inverseRotation * (origin - transform.position)) / scale;
    const glm::vec3 localDirection = (inverseRotation * direction) / scale;

    float bestWorld = maxDistance;
    glm::vec3 bestNormal(0.0f);
    bool found = false;
    std::vector<int> stack{0};
    while (!stack.empty()) {
        const int index = stack.back(); stack.pop_back();
        const CollisionBvhNode& node = mesh.nodes[static_cast<std::size_t>(index)];
        if (!RayAabb(localOrigin, localDirection, node.minimum, node.maximum,
                     std::numeric_limits<float>::max())) continue;
        if (node.left >= 0) {
            stack.push_back(node.left); stack.push_back(node.right);
            continue;
        }
        for (std::uint32_t i = node.first; i < node.first + node.count; ++i) {
            const CollisionTriangle& triangle = mesh.triangles[mesh.order[i]];
            float localT = 0.0f;
            if (!RayTriangle(localOrigin, localDirection, triangle, &localT)) continue;
            const glm::vec3 localPoint = localOrigin + localDirection * localT;
            const glm::vec3 worldPoint = transform.position
                + transform.rotation * (scale * localPoint);
            const float worldT = glm::dot(worldPoint - origin, direction);
            if (worldT < 0.0f || worldT >= bestWorld) continue;
            glm::vec3 localNormal = glm::cross(triangle.b - triangle.a, triangle.c - triangle.a);
            glm::vec3 worldNormal = transform.rotation * (localNormal / scale);
            const float length = glm::length(worldNormal);
            if (length < 1e-8f) continue;
            worldNormal /= length;
            if (glm::dot(worldNormal, direction) > 0.0f) worldNormal = -worldNormal;
            bestWorld = worldT; bestNormal = worldNormal; found = true;
        }
    }
    if (found) {
        if (distance) *distance = bestWorld;
        if (normal) *normal = bestNormal;
    }
    return found;
}

bool CollideSphereCollisionMesh(const CollisionMeshData& mesh,
                                const ecs::Transform& transform,
                                const glm::vec3& center,
                                float radius,
                                glm::vec3* point,
                                glm::vec3* sphereToMeshNormal,
                                float* penetration) {
    if (mesh.nodes.empty() || radius <= 0.0f) return false;
    const glm::vec3 absScale = glm::abs(transform.scale);
    const float minimumScale = std::min({absScale.x, absScale.y, absScale.z});
    if (minimumScale < 1.0e-8f) return false;
    const glm::quat inverseRotation = glm::inverse(transform.rotation);
    const glm::vec3 localCenter = (inverseRotation * (center - transform.position))
        / transform.scale;
    const float localRadius = radius / minimumScale;
    const glm::vec3 queryMin = localCenter - glm::vec3(localRadius);
    const glm::vec3 queryMax = localCenter + glm::vec3(localRadius);
    const auto overlaps = [](const glm::vec3& a0, const glm::vec3& a1,
                             const glm::vec3& b0, const glm::vec3& b1) {
        return a0.x <= b1.x && a1.x >= b0.x
            && a0.y <= b1.y && a1.y >= b0.y
            && a0.z <= b1.z && a1.z >= b0.z;
    };
    const auto closest = [](const glm::vec3& p, const CollisionTriangle& t) {
        const glm::vec3 ab=t.b-t.a, ac=t.c-t.a, ap=p-t.a;
        const float d1=glm::dot(ab,ap), d2=glm::dot(ac,ap);
        if(d1<=0&&d2<=0)return t.a;
        const glm::vec3 bp=p-t.b;const float d3=glm::dot(ab,bp),d4=glm::dot(ac,bp);
        if(d3>=0&&d4<=d3)return t.b;
        const float vc=d1*d4-d3*d2;if(vc<=0&&d1>=0&&d3<=0)return t.a+ab*(d1/(d1-d3));
        const glm::vec3 cp=p-t.c;const float d5=glm::dot(ab,cp),d6=glm::dot(ac,cp);
        if(d6>=0&&d5<=d6)return t.c;
        const float vb=d5*d2-d1*d6;if(vb<=0&&d2>=0&&d6<=0)return t.a+ac*(d2/(d2-d6));
        const float va=d3*d6-d5*d4;if(va<=0&&(d4-d3)>=0&&(d5-d6)>=0)
            return t.b+(t.c-t.b)*((d4-d3)/((d4-d3)+(d5-d6)));
        const float inverse=1.0f/(va+vb+vc);return t.a+ab*(vb*inverse)+ac*(vc*inverse);
    };

    float bestDistance2 = radius * radius;
    glm::vec3 bestPoint(0.0f), bestNormal(0.0f);
    bool found = false;
    std::vector<int> stack{0};
    while (!stack.empty()) {
        const int index = stack.back(); stack.pop_back();
        const CollisionBvhNode& node = mesh.nodes[static_cast<std::size_t>(index)];
        if (!overlaps(queryMin, queryMax, node.minimum, node.maximum)) continue;
        if (node.left >= 0) { stack.push_back(node.left); stack.push_back(node.right); continue; }
        for (std::uint32_t i=node.first;i<node.first+node.count;++i) {
            const CollisionTriangle& triangle=mesh.triangles[mesh.order[i]];
            const glm::vec3 localPoint=closest(localCenter,triangle);
            const glm::vec3 worldPoint=transform.position
                + transform.rotation*(transform.scale*localPoint);
            const glm::vec3 delta=worldPoint-center;
            const float distance2=glm::dot(delta,delta);
            if(distance2>bestDistance2)continue;
            glm::vec3 n;
            if(distance2>1.0e-12f)n=delta/std::sqrt(distance2);
            else {
                const glm::vec3 localN=glm::cross(triangle.b-triangle.a,triangle.c-triangle.a);
                n=transform.rotation*(localN/transform.scale);
                const float len=glm::length(n);if(len<1.0e-8f)continue;n/=len;
            }
            bestDistance2=distance2;bestPoint=worldPoint;bestNormal=n;found=true;
        }
    }
    if(!found)return false;
    if(point)*point=bestPoint;
    if(sphereToMeshNormal)*sphereToMeshNormal=bestNormal;
    if(penetration)*penetration=radius-std::sqrt(std::max(bestDistance2,0.0f));
    return true;
}

bool SweepSphereCollisionMesh(const CollisionMeshData& mesh,
                              const ecs::Transform& transform,
                              const glm::vec3& start,
                              const glm::vec3& end,
                              float radius,
                              float* timeOfImpact,
                              glm::vec3* normal) {
    const glm::vec3 travel = end - start;
    const float distance = glm::length(travel);
    if (radius <= 0.0f || distance <= 1.0e-7f) return false;

    // A radius-relative step prevents thin triangles from being skipped. Once
    // contact is bracketed, binary refinement produces a stable query result.
    const float stepLength = std::max(radius * 0.25f, 0.005f);
    const int steps = std::clamp(static_cast<int>(std::ceil(distance / stepLength)), 1, 4096);
    float previous = 0.0f;
    for (int i = 0; i <= steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        glm::vec3 point, hitNormal;
        float penetration = 0.0f;
        if (!CollideSphereCollisionMesh(mesh, transform, start + travel * t, radius,
                                        &point, &hitNormal, &penetration)) {
            previous = t;
            continue;
        }
        float low = previous, high = t;
        for (int iteration = 0; iteration < 12; ++iteration) {
            const float middle = (low + high) * 0.5f;
            if (CollideSphereCollisionMesh(mesh, transform, start + travel * middle,
                                           radius, nullptr, nullptr, nullptr))
                high = middle;
            else
                low = middle;
        }
        CollideSphereCollisionMesh(mesh, transform, start + travel * high, radius,
                                   &point, &hitNormal, &penetration);
        if (timeOfImpact) *timeOfImpact = high;
        if (normal) *normal = -hitNormal;
        return true;
    }
    return false;
}

} // namespace engine::physics
