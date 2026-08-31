#include "EditorViewport.h"
#include <engine/physics/ColliderTransform.h>
#include <engine/physics/CollisionMesh.h>
#include "EditorLineRenderer.h"

#include <engine/ai/NavGrid.h>
#include <engine/ai/NavMesh.h>
#include <engine/animation/AnimatedModel.h>

#include <engine/assets/RuntimeAssetManager.h>
#include <engine/graphics/Camera.h>
#include <engine/graphics/DynamicIrradiance.h>
#include <engine/graphics/CameraSequence.h>
#include <engine/ecs/Components.h>
#include <engine/graphics/Mesh.h>
#include <engine/graphics/Model.h>
#include <engine/graphics/Renderer.h>
#include <engine/graphics/Shader.h>
#include <engine/graphics/SkinnedModel.h>
#include <engine/graphics/Terrain.h>
#include <engine/graphics/Texture.h>
#include <engine/math/Spline.h>

#include <glad/glad.h>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace {

struct PickRay {
    glm::vec3 origin{0.0f};
    glm::vec3 direction{0.0f, 0.0f, -1.0f};
};

template <class DrawGeometry>
void DrawStencilSelectionOutline(engine::Shader& shader,
                                 const engine::ecs::Transform& transform,
                                 const glm::vec3& color,
                                 float thickness,
                                 const glm::mat4& modelOffset,
                                 DrawGeometry&& drawGeometry) {
    GLboolean colorMask[4]{};
    GLboolean depthWrite = GL_TRUE;
    const GLboolean stencilEnabled = glIsEnabled(GL_STENCIL_TEST);
    const GLboolean cullEnabled = glIsEnabled(GL_CULL_FACE);
    GLint depthFunc = GL_LESS, cullFace = GL_BACK;
    GLint stencilFunc = GL_ALWAYS, stencilRef = 0, stencilValueMask = ~0;
    GLint stencilWriteMask = ~0, stencilFail = GL_KEEP, stencilDepthFail = GL_KEEP, stencilDepthPass = GL_KEEP;
    glGetBooleanv(GL_COLOR_WRITEMASK, colorMask);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWrite);
    glGetIntegerv(GL_DEPTH_FUNC, &depthFunc);
    glGetIntegerv(GL_CULL_FACE_MODE, &cullFace);
    glGetIntegerv(GL_STENCIL_FUNC, &stencilFunc);
    glGetIntegerv(GL_STENCIL_REF, &stencilRef);
    glGetIntegerv(GL_STENCIL_VALUE_MASK, &stencilValueMask);
    glGetIntegerv(GL_STENCIL_WRITEMASK, &stencilWriteMask);
    glGetIntegerv(GL_STENCIL_FAIL, &stencilFail);
    glGetIntegerv(GL_STENCIL_PASS_DEPTH_FAIL, &stencilDepthFail);
    glGetIntegerv(GL_STENCIL_PASS_DEPTH_PASS, &stencilDepthPass);

    const GLboolean depthTestEnabled = glIsEnabled(GL_DEPTH_TEST);

    // --- Pass 1: mark the object's visible silhouette in the stencil buffer (no colour). ---
    // Enable stencil test AND set the write mask to 0xFF BEFORE clearing: glClear obeys the current
    // stencil write mask, so clearing while an inherited mask of 0 is bound silently does nothing --
    // the buffer then holds stale values and the "border" pass below floods the whole object. The
    // depth test must also be ON so REPLACE marks only the object's front-most (visible) fragments.
    glEnable(GL_STENCIL_TEST);
    glStencilMask(0xFF);
    glClearStencil(0);
    glClear(GL_STENCIL_BUFFER_BIT);
    glStencilFunc(GL_ALWAYS, 1, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glDisable(GL_CULL_FACE);

    shader.SetMat4("uModel", transform.Model() * modelOffset);
    shader.SetVec3("uColor", color);
    shader.SetFloat("uThickness", 0.0f);
    drawGeometry();

    // --- Pass 2: draw the shell expanded along normals, but ONLY where stencil != 1 (outside the
    // silhouette) and with depth OFF so the rim isn't clipped by nearby geometry -> a clean edge. ---
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glStencilMask(0x00);
    glStencilFunc(GL_NOTEQUAL, 1, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    shader.SetFloat("uThickness", std::max(thickness, 1.0f));
    drawGeometry();

    // --- Restore all touched state. ---
    glColorMask(colorMask[0], colorMask[1], colorMask[2], colorMask[3]);
    glDepthMask(depthWrite);
    glDepthFunc(depthFunc);
    if (depthTestEnabled) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    glCullFace(cullFace);
    if (cullEnabled) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    glStencilMask(static_cast<GLuint>(stencilWriteMask));
    glStencilFunc(stencilFunc, stencilRef, static_cast<GLuint>(stencilValueMask));
    glStencilOp(stencilFail, stencilDepthFail, stencilDepthPass);
    if (!stencilEnabled) glDisable(GL_STENCIL_TEST);
}

float DistanceToSegmentSquared(const glm::vec2& point, const glm::vec2& a, const glm::vec2& b) {
    const glm::vec2 ab = b - a;
    const float lengthSquared = glm::dot(ab, ab);
    if (lengthSquared <= 0.0001f) {
        const glm::vec2 delta = point - a;
        return glm::dot(delta, delta);
    }

    const float t = std::clamp(glm::dot(point - a, ab) / lengthSquared, 0.0f, 1.0f);
    const glm::vec2 closest = a + ab * t;
    const glm::vec2 delta = point - closest;
    return glm::dot(delta, delta);
}

bool BuildPickRay(float x, float y, const glm::mat4& viewProj, int width, int height, PickRay* ray) {
    if (!ray || width <= 0 || height <= 0) {
        return false;
    }

    const float ndcX = (2.0f * x) / static_cast<float>(width) - 1.0f;
    const float ndcY = 1.0f - (2.0f * y) / static_cast<float>(height);
    const glm::mat4 inverseViewProj = glm::inverse(viewProj);

    glm::vec4 nearWorld = inverseViewProj * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
    glm::vec4 farWorld = inverseViewProj * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
    if (std::abs(nearWorld.w) <= 0.0001f || std::abs(farWorld.w) <= 0.0001f) {
        return false;
    }

    nearWorld /= nearWorld.w;
    farWorld /= farWorld.w;

    const glm::vec3 direction = glm::vec3(farWorld - nearWorld);
    if (glm::dot(direction, direction) <= 0.0001f) {
        return false;
    }

    ray->origin = glm::vec3(nearWorld);
    ray->direction = glm::normalize(direction);
    return true;
}

PickRay TransformRayToLocal(const PickRay& ray, const glm::mat4& inverseModel) {
    PickRay local;
    local.origin = glm::vec3(inverseModel * glm::vec4(ray.origin, 1.0f));
    local.direction = glm::vec3(inverseModel * glm::vec4(ray.direction, 0.0f));
    return local;
}

bool IntersectLocalAabb(const PickRay& ray, const glm::vec3& minimum, const glm::vec3& maximum, float* hitDistance) {
    float tMin = 0.0f;
    float tMax = std::numeric_limits<float>::max();

    for (int axis = 0; axis < 3; ++axis) {
        const float origin = ray.origin[axis];
        const float direction = ray.direction[axis];
        if (std::abs(direction) <= 0.0001f) {
            if (origin < minimum[axis] || origin > maximum[axis]) {
                return false;
            }
            continue;
        }

        float nearT = (minimum[axis] - origin) / direction;
        float farT = (maximum[axis] - origin) / direction;
        if (nearT > farT) {
            std::swap(nearT, farT);
        }

        tMin = std::max(tMin, nearT);
        tMax = std::min(tMax, farT);
        if (tMin > tMax) {
            return false;
        }
    }

    if (hitDistance) {
        *hitDistance = tMin;
    }
    return true;
}

bool IntersectLocalPlaneQuad(const PickRay& ray, float* hitDistance) {
    if (std::abs(ray.direction.y) <= 0.0001f) {
        return false;
    }

    const float t = -ray.origin.y / ray.direction.y;
    if (t < 0.0f) {
        return false;
    }

    const glm::vec3 hit = ray.origin + ray.direction * t;
    constexpr float halfSize = 0.5f;
    constexpr float epsilon = 0.0001f;
    if (hit.x < -halfSize - epsilon || hit.x > halfSize + epsilon
        || hit.z < -halfSize - epsilon || hit.z > halfSize + epsilon) {
        return false;
    }

    if (hitDistance) {
        *hitDistance = t;
    }
    return true;
}

glm::vec3 AxisVector(EditorGizmo::Axis axis) {
    switch (axis) {
    case EditorGizmo::Axis::X: return glm::vec3(1.0f, 0.0f, 0.0f);
    case EditorGizmo::Axis::Y: return glm::vec3(0.0f, 1.0f, 0.0f);
    case EditorGizmo::Axis::Z: return glm::vec3(0.0f, 0.0f, 1.0f);
    case EditorGizmo::Axis::All: return glm::vec3(1.0f);
    }
    return glm::vec3(1.0f, 0.0f, 0.0f);
}

glm::vec3 RingOffset(EditorGizmo::Axis axis, float t, float radius) {
    switch (axis) {
    case EditorGizmo::Axis::X:
        return glm::vec3(0.0f, std::cos(t) * radius, std::sin(t) * radius);
    case EditorGizmo::Axis::Y:
        return glm::vec3(std::cos(t) * radius, 0.0f, std::sin(t) * radius);
    case EditorGizmo::Axis::Z:
        return glm::vec3(std::cos(t) * radius, std::sin(t) * radius, 0.0f);
    }
    return glm::vec3(0.0f);
}

glm::vec3 AxisColor(EditorGizmo::Axis axis, EditorGizmo::Axis activeAxis) {
    if (axis == activeAxis) {
        return glm::vec3(1.0f, 0.86f, 0.24f);
    }

    switch (axis) {
    case EditorGizmo::Axis::X: return glm::vec3(0.95f, 0.12f, 0.14f);
    case EditorGizmo::Axis::Y: return glm::vec3(0.12f, 0.82f, 0.28f);
    case EditorGizmo::Axis::Z: return glm::vec3(0.16f, 0.36f, 1.0f);
    case EditorGizmo::Axis::All: return glm::vec3(1.0f, 0.86f, 0.24f);
    }
    return glm::vec3(1.0f);
}

float GizmoWorldScale(const engine::ecs::Transform& transform,
                      const engine::Camera& camera,
                      int viewportHeight) {
    const float distance = std::max(glm::length(camera.Position() - transform.position), 0.1f);
    const float halfFov = glm::radians(glm::clamp(camera.fov, 10.0f, 120.0f) * 0.5f);
    const float worldPerPixel = (2.0f * distance * std::tan(halfFov))
        / static_cast<float>(std::max(viewportHeight, 1));
    const glm::vec3 absoluteScale = glm::abs(transform.scale);
    const float objectScale = std::max({absoluteScale.x, absoluteScale.y, absoluteScale.z, 0.001f});
    const float objectInfluence = glm::clamp(std::pow(objectScale, 0.18f), 0.8f, 1.45f);
    return glm::clamp(worldPerPixel * 105.0f * objectInfluence, 0.12f, 40.0f);
}

glm::quat RotationFromX(const glm::vec3& direction) {
    if (glm::dot(direction, direction) <= 0.0001f) {
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    }

    const glm::vec3 from(1.0f, 0.0f, 0.0f);
    const glm::vec3 to = glm::normalize(direction);
    const float d = glm::clamp(glm::dot(from, to), -1.0f, 1.0f);
    if (d > 0.999f) {
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    }
    if (d < -0.999f) {
        return glm::angleAxis(glm::radians(180.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    }

    return glm::angleAxis(std::acos(d), glm::normalize(glm::cross(from, to)));
}

glm::quat RotationFromY(const glm::vec3& direction) {
    if (glm::dot(direction, direction) <= 0.0001f) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    const glm::vec3 from(0.0f, 1.0f, 0.0f);
    const glm::vec3 to = glm::normalize(direction);
    const float d = glm::clamp(glm::dot(from, to), -1.0f, 1.0f);
    if (d > 0.999f) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    if (d < -0.999f) return glm::angleAxis(glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::vec3 c = glm::cross(from, to);
    return glm::normalize(glm::quat(1.0f + d, c.x, c.y, c.z));
}

glm::vec3 LightGuideColor(const engine::ecs::Light& light) {
    const float maxChannel = std::max(std::max(light.color.r, light.color.g), light.color.b);
    if (maxChannel <= 0.0001f) {
        return glm::vec3(1.0f, 0.86f, 0.32f);
    }
    return glm::mix(glm::vec3(1.0f), light.color / maxChannel, 0.72f);
}

void DrawGizmoBox(engine::Renderer& renderer,
                  engine::Shader& shader,
                  const engine::Mesh& cube,
                  const glm::vec3& position,
                  const glm::vec3& scale,
                  const glm::vec3& color) {
    engine::ecs::Transform transform;
    transform.position = position;
    transform.scale = scale;
    shader.SetMat4("uModel", transform.Model());
    shader.SetVec3("uColor", color);
    renderer.Draw(cube);
}

void DrawGuideSegment(engine::Renderer& renderer,
                      engine::Shader& shader,
                      const engine::Mesh& cube,
                      const glm::vec3& a,
                      const glm::vec3& b,
                      float thickness,
                      const glm::vec3& color) {
    const glm::vec3 delta = b - a;
    const float length = glm::length(delta);
    if (length <= 0.0001f) {
        return;
    }

    engine::ecs::Transform transform;
    transform.position = (a + b) * 0.5f;
    transform.rotation = RotationFromX(delta);
    transform.scale = glm::vec3(length, thickness, thickness);
    shader.SetMat4("uModel", transform.Model());
    shader.SetVec3("uColor", color);
    renderer.Draw(cube);
}

void DrawGuideRing(engine::Renderer& renderer,
                   engine::Shader& shader,
                   const engine::Mesh& cube,
                   const glm::vec3& center,
                   EditorGizmo::Axis axis,
                   float radius,
                   float marker,
                   const glm::vec3& color) {
    if (radius <= 0.0001f) {
        return;
    }

    constexpr int segments = 64;
    constexpr float pi = 3.14159265359f;
    for (int i = 0; i < segments; ++i) {
        const float t = (static_cast<float>(i) / static_cast<float>(segments)) * 2.0f * pi;
        DrawGizmoBox(renderer, shader, cube, center + RingOffset(axis, t, radius), glm::vec3(marker), color);
    }
}

glm::vec3 PhysicsEventGuideColor(const EditorViewport::PhysicsEventGuide& guide) {
    if (guide.phase == 2) {
        return glm::vec3(1.0f, 0.22f, 0.18f);
    }
    if (guide.trigger) {
        return guide.phase == 0
            ? glm::vec3(0.26f, 0.92f, 1.0f)
            : glm::vec3(0.56f, 0.42f, 1.0f);
    }
    return guide.phase == 0
        ? glm::vec3(1.0f, 0.72f, 0.18f)
            : glm::vec3(1.0f, 0.48f, 0.20f);
}

glm::vec3 PhysicsJointGuideColor(const EditorViewport::PhysicsJointGuide& guide) {
    if (!guide.enabled) {
        return glm::vec3(0.34f, 0.36f, 0.40f);
    }
    if (guide.type == 1) {
        return glm::vec3(0.36f, 0.92f, 0.48f);   // spring: green
    }
    if (guide.type == 2) {
        return glm::vec3(1.0f, 0.32f, 0.90f);    // ball: magenta
    }
    if (guide.type == 3) {
        return glm::vec3(1.0f, 0.55f, 0.12f);    // hinge: orange
    }
    return guide.rope
        ? glm::vec3(0.30f, 0.78f, 1.0f)
        : glm::vec3(1.0f, 0.78f, 0.24f);
}

void BuildBasis(const glm::vec3& direction, glm::vec3* right, glm::vec3* up) {
    const glm::vec3 forward = glm::normalize(direction);
    glm::vec3 reference(0.0f, 1.0f, 0.0f);
    if (std::abs(glm::dot(forward, reference)) > 0.96f) {
        reference = glm::vec3(1.0f, 0.0f, 0.0f);
    }

    *right = glm::normalize(glm::cross(reference, forward));
    *up = glm::normalize(glm::cross(forward, *right));
}

void DrawSpotConeGuide(engine::Renderer& renderer,
                       engine::Shader& shader,
                       const engine::Mesh& cube,
                       const glm::vec3& origin,
                       const engine::ecs::Light& light,
                       const glm::vec3& color) {
    if (glm::dot(light.direction, light.direction) <= 0.0001f || light.range <= 0.0001f) {
        return;
    }

    const glm::vec3 forward = glm::normalize(light.direction);
    const float outerRadians = glm::radians(glm::clamp(light.outerAngle, 0.0f, 89.5f));
    const float radius = std::tan(outerRadians) * light.range;
    const glm::vec3 center = origin + forward * light.range;

    glm::vec3 right;
    glm::vec3 up;
    BuildBasis(forward, &right, &up);

    constexpr int segments = 48;
    constexpr float pi = 3.14159265359f;
    constexpr float thickness = 0.025f;
    for (int i = 0; i < segments; ++i) {
        const float t0 = (static_cast<float>(i) / static_cast<float>(segments)) * 2.0f * pi;
        const float t1 = (static_cast<float>(i + 1) / static_cast<float>(segments)) * 2.0f * pi;
        const glm::vec3 p0 = center + right * std::cos(t0) * radius + up * std::sin(t0) * radius;
        const glm::vec3 p1 = center + right * std::cos(t1) * radius + up * std::sin(t1) * radius;
        DrawGuideSegment(renderer, shader, cube, p0, p1, thickness, color);
    }

    for (int i = 0; i < 4; ++i) {
        const float t = (static_cast<float>(i) / 4.0f) * 2.0f * pi;
        const glm::vec3 edge = center + right * std::cos(t) * radius + up * std::sin(t) * radius;
        DrawGuideSegment(renderer, shader, cube, origin, edge, thickness, color);
    }
}

glm::vec3 PhysicsGuideColor(const EditorScene::Object& object) {
    if (object.rigidBodyEnabled && object.rigidBody.invMass > 0.0f) {
        return glm::vec3(0.24f, 0.82f, 1.0f);
    }
    if (object.collider.isTrigger) {
        return glm::vec3(0.78f, 0.45f, 1.0f);
    }
    return glm::vec3(0.24f, 1.0f, 0.58f);
}

void DrawBoxColliderGuide(EditorLineRenderer& lines,
                          const engine::ecs::Transform& transform,
                          const engine::ecs::Collider& collider,
                          const glm::vec3& color) {
    const glm::mat3 rotation = glm::mat3_cast(transform.rotation);
    const glm::vec3 halfExtents = glm::max(collider.halfExtents, glm::vec3(0.001f));
    const glm::vec3 corners[] = {
        {-halfExtents.x, -halfExtents.y, -halfExtents.z},
        { halfExtents.x, -halfExtents.y, -halfExtents.z},
        { halfExtents.x,  halfExtents.y, -halfExtents.z},
        {-halfExtents.x,  halfExtents.y, -halfExtents.z},
        {-halfExtents.x, -halfExtents.y,  halfExtents.z},
        { halfExtents.x, -halfExtents.y,  halfExtents.z},
        { halfExtents.x,  halfExtents.y,  halfExtents.z},
        {-halfExtents.x,  halfExtents.y,  halfExtents.z}
    };
    glm::vec3 world[8];
    for (int i = 0; i < 8; ++i) {
        world[i] = transform.position + rotation * corners[i];
    }

    constexpr int edges[][2] = {
        {0, 1}, {1, 2}, {2, 3}, {3, 0},
        {4, 5}, {5, 6}, {6, 7}, {7, 4},
        {0, 4}, {1, 5}, {2, 6}, {3, 7}
    };
    for (const auto& edge : edges) {
        lines.AddLine(world[edge[0]], world[edge[1]], color);
    }
}

void DrawPlaneColliderGuide(EditorLineRenderer& lines,
                            const engine::ecs::Collider& collider,
                            const glm::vec3& color) {
    glm::vec3 normal = collider.planeNormal;
    if (glm::dot(normal, normal) <= 0.0001f) {
        normal = glm::vec3(0.0f, 1.0f, 0.0f);
    } else {
        normal = glm::normalize(normal);
    }

    glm::vec3 right;
    glm::vec3 up;
    BuildBasis(normal, &right, &up);
    const glm::vec3 center = normal * collider.planeOffset;
    constexpr float size = 3.0f;
    const glm::vec3 corners[] = {
        center + right * -size + up * -size,
        center + right *  size + up * -size,
        center + right *  size + up *  size,
        center + right * -size + up *  size
    };

    lines.AddLine(corners[0], corners[1], color);
    lines.AddLine(corners[1], corners[2], color);
    lines.AddLine(corners[2], corners[3], color);
    lines.AddLine(corners[3], corners[0], color);
    lines.AddLine(center - right * size, center + right * size, color);
    lines.AddLine(center - up * size, center + up * size, color);
    lines.AddLine(center, center + normal * 0.75f, color);
}

void DrawBasisRing(EditorLineRenderer& lines,
                   const glm::vec3& center,
                   const glm::vec3& right,
                   const glm::vec3& up,
                   float radius,
                   const glm::vec3& color) {
    if (radius <= 0.0001f) {
        return;
    }

    constexpr int segments = 48;
    constexpr float pi = 3.14159265359f;
    for (int i = 0; i < segments; ++i) {
        const float a = (static_cast<float>(i) / static_cast<float>(segments)) * 2.0f * pi;
        const float b = (static_cast<float>(i + 1) / static_cast<float>(segments)) * 2.0f * pi;
        const glm::vec3 p0 = center + right * std::cos(a) * radius + up * std::sin(a) * radius;
        const glm::vec3 p1 = center + right * std::cos(b) * radius + up * std::sin(b) * radius;
        lines.AddLine(p0, p1, color);
    }
}

void DrawSphereColliderGuide(EditorLineRenderer& lines,
                             const engine::ecs::Transform& transform,
                             const engine::ecs::Collider& collider,
                             const glm::vec3& color) {
    const float radius = std::max(collider.radius, 0.001f);
    DrawBasisRing(lines, transform.position, glm::vec3(0, 1, 0),
        glm::vec3(0, 0, 1), radius, color);
    DrawBasisRing(lines, transform.position, glm::vec3(1, 0, 0),
        glm::vec3(0, 0, 1), radius, color);
    DrawBasisRing(lines, transform.position, glm::vec3(1, 0, 0),
        glm::vec3(0, 1, 0), radius, color);
}

void DrawHalfRing(EditorLineRenderer& lines, const glm::vec3& center,
                  const glm::vec3& radial, const glm::vec3& axis,
                  float radius, bool upper, const glm::vec3& color) {
    constexpr int segments = 24;
    constexpr float pi = 3.14159265359f;
    const float start = upper ? 0.0f : pi;
    for (int i = 0; i < segments; ++i) {
        const float a = start + pi * static_cast<float>(i) / segments;
        const float b = start + pi * static_cast<float>(i + 1) / segments;
        lines.AddLine(
            center + radial * std::cos(a) * radius + axis * std::sin(a) * radius,
            center + radial * std::cos(b) * radius + axis * std::sin(b) * radius,
            color);
    }
}

void DrawCapsuleColliderGuide(EditorLineRenderer& lines,
                              const engine::ecs::Transform& transform,
                              const engine::ecs::Collider& collider,
                              const glm::vec3& color) {
    const float radius = std::max(collider.radius, 0.001f);
    const float halfHeight = std::max(collider.halfHeight, 0.0f);
    glm::vec3 axis = glm::mat3_cast(transform.rotation) * glm::vec3(0.0f, 1.0f, 0.0f);
    if (glm::dot(axis, axis) <= 0.0001f) {
        axis = glm::vec3(0.0f, 1.0f, 0.0f);
    } else {
        axis = glm::normalize(axis);
    }

    glm::vec3 right;
    glm::vec3 forward;
    BuildBasis(axis, &right, &forward);
    const glm::vec3 a = transform.position - axis * halfHeight;
    const glm::vec3 b = transform.position + axis * halfHeight;

    DrawBasisRing(lines, a, right, forward, radius, color);
    DrawBasisRing(lines, b, right, forward, radius, color);
    lines.AddLine(a + right * radius, b + right * radius, color);
    lines.AddLine(a - right * radius, b - right * radius, color);
    lines.AddLine(a + forward * radius, b + forward * radius, color);
    lines.AddLine(a - forward * radius, b - forward * radius, color);
    DrawHalfRing(lines, b, right, axis, radius, true, color);
    DrawHalfRing(lines, b, forward, axis, radius, true, color);
    DrawHalfRing(lines, a, right, axis, radius, false, color);
    DrawHalfRing(lines, a, forward, axis, radius, false, color);
}

void DrawCylinderColliderGuide(EditorLineRenderer& lines,
                               const engine::ecs::Transform& transform,
                               const engine::ecs::Collider& collider,
                               const glm::vec3& color) {
    const float radius = std::max(collider.radius, 0.001f);
    const float halfHeight = std::max(collider.halfHeight, 0.001f);
    glm::vec3 axis = glm::mat3_cast(transform.rotation) * glm::vec3(0.0f, 1.0f, 0.0f);
    axis = glm::dot(axis, axis) > 0.0001f ? glm::normalize(axis) : glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 right;
    glm::vec3 forward;
    BuildBasis(axis, &right, &forward);
    const glm::vec3 bottom = transform.position - axis * halfHeight;
    const glm::vec3 top = transform.position + axis * halfHeight;

    // Two circular cap rims and straight side generators make the flat ends
    // visually distinct from the rounded capsule guide.
    DrawBasisRing(lines, bottom, right, forward, radius, color);
    DrawBasisRing(lines, top, right, forward, radius, color);
    constexpr int sideLines = 8;
    constexpr float twoPi = 6.28318530718f;
    for (int i = 0; i < sideLines; ++i) {
        const float angle = twoPi * static_cast<float>(i) / static_cast<float>(sideLines);
        const glm::vec3 radial = right * std::cos(angle) * radius
            + forward * std::sin(angle) * radius;
        lines.AddLine(bottom + radial, top + radial, color);
    }
}

void DrawConeColliderGuide(EditorLineRenderer& lines,
                           const engine::ecs::Transform& transform,
                           const engine::ecs::Collider& collider,
                           const glm::vec3& color) {
    const glm::mat3 rotation = glm::mat3_cast(transform.rotation);
    const glm::vec3 axis = rotation * glm::vec3(0, 1, 0);
    const glm::vec3 right = rotation * glm::vec3(1, 0, 0);
    const glm::vec3 forward = rotation * glm::vec3(0, 0, 1);
    const float halfHeight = std::max(collider.halfHeight, 0.001f);
    const float radius = std::max(collider.radius, 0.001f);
    const glm::vec3 base = transform.position - axis * halfHeight;
    const glm::vec3 tip = transform.position + axis * halfHeight;
    DrawBasisRing(lines, base, right, forward, radius, color);
    constexpr int sides = 12;
    for (int i = 0; i < sides; ++i) {
        const float angle = 6.28318530718f * static_cast<float>(i) / sides;
        lines.AddLine(base + (right * std::cos(angle)
            + forward * std::sin(angle)) * radius, tip, color);
    }
}

void DrawPyramidColliderGuide(EditorLineRenderer& lines,
                              const engine::ecs::Transform& transform,
                              const engine::ecs::Collider& collider,
                              const glm::vec3& color) {
    const glm::mat3 rotation = glm::mat3_cast(transform.rotation);
    const glm::vec3 ext = glm::max(collider.halfExtents, glm::vec3(0.001f));
    const glm::vec3 local[] = {
        {-ext.x, -ext.y, -ext.z}, {ext.x, -ext.y, -ext.z},
        {ext.x, -ext.y, ext.z}, {-ext.x, -ext.y, ext.z},
        {0.0f, ext.y, 0.0f}
    };
    glm::vec3 world[5];
    for (int i = 0; i < 5; ++i)
        world[i] = transform.position + rotation * local[i];
    for (int i = 0; i < 4; ++i) {
        lines.AddLine(world[i], world[(i + 1) % 4], color);
        lines.AddLine(world[i], world[4], color);
    }
}

void DrawTorusColliderGuide(EditorLineRenderer& lines,
                            const engine::ecs::Transform& transform,
                            const engine::ecs::Collider& collider,
                            const glm::vec3& color) {
    constexpr int majorSegments = 32;
    constexpr int minorSegments = 10;
    const float major = std::max(collider.majorRadius, 0.001f);
    const float minor = std::max(collider.minorRadius, 0.001f);
    const glm::mat3 rotation = glm::mat3_cast(transform.rotation);
    auto point = [&](int majorIndex, int minorIndex) {
        const float a = 6.28318530718f * majorIndex / majorSegments;
        const float b = 6.28318530718f * minorIndex / minorSegments;
        const float ringRadius = major + minor * std::cos(b);
        const glm::vec3 local(
            std::cos(a) * ringRadius, std::sin(b) * minor,
            std::sin(a) * ringRadius);
        return transform.position + rotation * local;
    };
    for (int i = 0; i < majorSegments; ++i) {
        for (int j = 0; j < minorSegments; ++j) {
            lines.AddLine(point(i, j), point((i + 1) % majorSegments, j), color);
            lines.AddLine(point(i, j), point(i, (j + 1) % minorSegments), color);
        }
    }
}

void DrawGizmoCone(engine::Renderer& renderer,
                   engine::Shader& shader,
                   const engine::Mesh& cone,
                   const glm::vec3& position,
                   const glm::vec3& direction,
                   const glm::vec3& color,
                   float size) {
    engine::ecs::Transform transform;
    transform.position = position;
    transform.scale = glm::vec3(size * 0.55f, size, size * 0.55f);
    transform.rotation = RotationFromY(direction);
    shader.SetMat4("uModel", transform.Model());
    shader.SetVec3("uColor", color);
    renderer.Draw(cone);
}

void DrawGizmoRing(engine::Renderer& renderer,
                   engine::Shader& shader,
                   const engine::Mesh& cube,
                   const glm::vec3& center,
                   EditorGizmo::Axis axis,
                   const glm::mat3& basis,
                   const glm::vec3& color,
                   float radius,
                   float marker) {
    constexpr int segments = 40;
    constexpr float pi = 3.14159265359f;

    for (int i = 0; i < segments; ++i) {
        const float t = (static_cast<float>(i) / static_cast<float>(segments)) * 2.0f * pi;
        DrawGizmoBox(renderer, shader, cube, center + basis * RingOffset(axis, t, radius), glm::vec3(marker), color);
    }
}

} // namespace

EditorViewport::EditorViewport()
    : m_gridLines(std::make_unique<EditorLineRenderer>()),
      m_gizmoLines(std::make_unique<EditorLineRenderer>()),
      m_colliderLines(std::make_unique<EditorLineRenderer>()),
      m_splineLines(std::make_unique<EditorLineRenderer>()),
      m_waterLines(std::make_unique<EditorLineRenderer>()),
      m_roomLines(std::make_unique<EditorLineRenderer>()),
      m_blockoutLines(std::make_unique<EditorLineRenderer>()),
      m_scatterLines(std::make_unique<EditorLineRenderer>()),
      m_arrayLines(std::make_unique<EditorLineRenderer>()),
      m_measurementLines(std::make_unique<EditorLineRenderer>()),
      m_dynamicGiLines(std::make_unique<EditorLineRenderer>())
{
}

void EditorViewport::DrawDynamicIrradianceProbes(
    const engine::DynamicIrradianceSystem& system,
    const glm::vec3& cameraPosition, const glm::mat4& viewProj) const {
    if (!m_dynamicGiLines || !system.Ready()) return;
    m_dynamicGiLines->Clear();
    const auto& probes = system.Probes();
    const float activeDistance = system.Settings().activeDistance;
    const float marker = std::clamp(system.Settings().probeSpacing * 0.075f, 0.04f, 0.35f);
    // Keep debug submission bounded for very large volumes.
    const std::size_t stride = std::max<std::size_t>(1, (probes.size() + 8191) / 8192);
    for (std::size_t index = 0; index < probes.size(); index += stride) {
        const engine::DynamicIrradianceProbe& probe = probes[index];
        const glm::vec3 position = system.ProbeSamplePosition(index);
        if (glm::distance(position, cameraPosition) > activeDistance) continue;
        glm::vec3 color(0.15f, 0.95f, 0.35f);
        switch (probe.state) {
        case engine::DynamicProbeState::Sleeping: color = glm::vec3(0.42f); break;
        case engine::DynamicProbeState::Relocated: color = glm::vec3(1.0f, 0.65f, 0.08f); break;
        case engine::DynamicProbeState::Invalid:
        case engine::DynamicProbeState::InsideGeometry: color = glm::vec3(1.0f, 0.12f, 0.08f); break;
        case engine::DynamicProbeState::OutsideGeometry: color = glm::vec3(0.15f, 0.45f, 1.0f); break;
        default: break;
        }
        m_dynamicGiLines->AddLine(position - glm::vec3(marker, 0, 0), position + glm::vec3(marker, 0, 0), color);
        m_dynamicGiLines->AddLine(position - glm::vec3(0, marker, 0), position + glm::vec3(0, marker, 0), color);
        m_dynamicGiLines->AddLine(position - glm::vec3(0, 0, marker), position + glm::vec3(0, 0, marker), color);
        if (glm::dot(probe.relocationOffset, probe.relocationOffset) > 1e-6f)
            m_dynamicGiLines->AddLine(system.ProbeGridPosition(index), position, glm::vec3(0.1f, 0.9f, 1.0f));
    }
    m_dynamicGiLines->Draw(viewProj, 1.5f, true, true);
}

EditorViewport::~EditorViewport() = default;

void EditorViewport::DrawRoomBuilderGuide(const glm::vec3& first,
                                          const glm::vec3& second,
                                          float wallHeight,
                                          const glm::mat4& viewProj) const {
    if (!m_roomLines) return;
    m_roomLines->Clear();
    const float minX = std::min(first.x, second.x);
    const float maxX = std::max(first.x, second.x);
    const float minZ = std::min(first.z, second.z);
    const float maxZ = std::max(first.z, second.z);
    const float y = first.y + 0.015f;
    const float top = y + std::max(wallHeight, 0.05f);
    const glm::vec3 color(0.15f, 0.92f, 0.48f);
    const glm::vec3 bottom[4] = {
        {minX, y, minZ}, {maxX, y, minZ}, {maxX, y, maxZ}, {minX, y, maxZ}};
    const glm::vec3 upper[4] = {
        {minX, top, minZ}, {maxX, top, minZ}, {maxX, top, maxZ}, {minX, top, maxZ}};
    for (int i = 0; i < 4; ++i) {
        const int next = (i + 1) % 4;
        m_roomLines->AddLine(bottom[i], bottom[next], color);
        m_roomLines->AddLine(upper[i], upper[next], color * 0.75f);
        m_roomLines->AddLine(bottom[i], upper[i], color * 0.75f);
    }
    m_roomLines->Draw(viewProj, 2.0f, true);
}

void EditorViewport::DrawBuildingFootprintGuide(
    const std::vector<glm::vec2>& footprint, float baseHeight,
    float totalHeight, const glm::mat4& viewProj) const {
    if (!m_roomLines || footprint.size() < 3) return;
    m_roomLines->Clear();
    const glm::vec3 baseColor(0.1f, 0.78f, 1.0f);
    const float bottomY = baseHeight + 0.015f;
    const float topY = bottomY + std::max(totalHeight, 0.05f);
    for (std::size_t i = 0; i < footprint.size(); ++i) {
        const glm::vec2& p = footprint[i];
        const glm::vec2& q = footprint[(i + 1) % footprint.size()];
        const glm::vec3 bottomA(p.x, bottomY, p.y), bottomB(q.x, bottomY, q.y);
        const glm::vec3 topA(p.x, topY, p.y), topB(q.x, topY, q.y);
        m_roomLines->AddLine(bottomA, bottomB, baseColor);
        m_roomLines->AddLine(topA, topB, baseColor * 0.75f);
        m_roomLines->AddLine(bottomA, topA, baseColor * 0.65f);
    }
    m_roomLines->Draw(viewProj, 2.25f, true);
}

void EditorViewport::DrawBlockoutPreview(const glm::vec3& base,
                                         const glm::vec3& dimensions,
                                         float yawDegrees,
                                         const glm::mat4& viewProj) const {
    if (!m_blockoutLines) return;
    m_blockoutLines->Clear();
    const glm::vec3 half(dimensions.x * 0.5f, 0.0f, dimensions.z * 0.5f);
    const float yaw = glm::radians(yawDegrees);
    const float c = std::cos(yaw), s = std::sin(yaw);
    auto rotate = [&](const glm::vec3& p) {
        return glm::vec3(p.x * c + p.z * s, p.y, -p.x * s + p.z * c);
    };
    glm::vec3 corners[8];
    int cursor = 0;
    for (int y = 0; y < 2; ++y)
        for (int z = 0; z < 2; ++z)
            for (int x = 0; x < 2; ++x) {
                const glm::vec3 local(x ? half.x : -half.x,
                    y ? dimensions.y : 0.0f, z ? half.z : -half.z);
                corners[cursor++] = base + rotate(local);
            }
    const int edges[][2] = {
        {0,1},{1,3},{3,2},{2,0}, {4,5},{5,7},{7,6},{6,4},
        {0,4},{1,5},{2,6},{3,7}};
    const glm::vec3 color(1.0f, 0.62f, 0.12f);
    for (const auto& edge : edges)
        m_blockoutLines->AddLine(corners[edge[0]], corners[edge[1]], color);
    m_blockoutLines->Draw(viewProj, 2.25f, true);
}

void EditorViewport::DrawScatterBrush(const glm::vec3& center,
                                      const glm::vec3& surfaceNormal,
                                      float radius,
                                      bool erase,
                                      const glm::mat4& viewProj) const {
    if (!m_scatterLines) return;
    m_scatterLines->Clear();
    const glm::vec3 normal = glm::dot(surfaceNormal, surfaceNormal) > 1.0e-8f
        ? glm::normalize(surfaceNormal) : glm::vec3(0, 1, 0);
    const glm::vec3 helper = std::abs(normal.y) < 0.95f
        ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    const glm::vec3 tangent = glm::normalize(glm::cross(helper, normal));
    const glm::vec3 bitangent = glm::normalize(glm::cross(normal, tangent));
    const glm::vec3 color = erase ? glm::vec3(1.0f, 0.18f, 0.12f)
                                  : glm::vec3(0.12f, 0.86f, 0.42f);
    constexpr int segments = 64;
    const glm::vec3 liftedCenter = center + normal * 0.018f;
    for (int i = 0; i < segments; ++i) {
        const float a = 6.28318530718f * static_cast<float>(i) / segments;
        const float b = 6.28318530718f * static_cast<float>(i + 1) / segments;
        const glm::vec3 p0 = liftedCenter + radius * (tangent * std::cos(a) + bitangent * std::sin(a));
        const glm::vec3 p1 = liftedCenter + radius * (tangent * std::cos(b) + bitangent * std::sin(b));
        m_scatterLines->AddLine(p0, p1, color);
    }
    m_scatterLines->AddLine(liftedCenter - tangent * radius * 0.15f,
                            liftedCenter + tangent * radius * 0.15f, color);
    m_scatterLines->AddLine(liftedCenter - bitangent * radius * 0.15f,
                            liftedCenter + bitangent * radius * 0.15f, color);
    m_scatterLines->Draw(viewProj, 2.5f, true);
}

void EditorViewport::DrawArrayPreview(const glm::vec3& source,
                                      const std::vector<glm::vec3>& copies,
                                      const glm::mat4& viewProj) const {
    if (!m_arrayLines || copies.empty()) return;
    m_arrayLines->Clear();
    const glm::vec3 lineColor(0.18f, 0.65f, 1.0f);
    const glm::vec3 pointColor(0.25f, 0.9f, 1.0f);
    glm::vec3 previous = source;
    for (const glm::vec3& position : copies) {
        m_arrayLines->AddLine(previous, position, lineColor * 0.72f);
        const float marker = 0.16f;
        m_arrayLines->AddLine(position - glm::vec3(marker, 0, 0),
                              position + glm::vec3(marker, 0, 0), pointColor);
        m_arrayLines->AddLine(position - glm::vec3(0, marker, 0),
                              position + glm::vec3(0, marker, 0), pointColor);
        m_arrayLines->AddLine(position - glm::vec3(0, 0, marker),
                              position + glm::vec3(0, 0, marker), pointColor);
        previous = position;
    }
    m_arrayLines->Draw(viewProj, 2.0f, true);
}

void EditorViewport::DrawMeasurementGuides(
    const std::vector<MeasurementGuide>& guides,
    const glm::mat4& viewProj) const {
    if (!m_measurementLines || guides.empty()) return;
    m_measurementLines->Clear();
    for (const MeasurementGuide& guide : guides) {
        const glm::vec3 color = guide.selected
            ? glm::vec3(0.2f, 0.95f, 1.0f) : glm::vec3(0.25f, 0.65f, 0.9f);
        const float marker = guide.selected ? 0.20f : 0.14f;
        if (!guide.box) {
            m_measurementLines->AddLine(guide.a, guide.b, color);
        } else {
            const glm::vec3 minimum = glm::min(guide.a, guide.b);
            const glm::vec3 maximum = glm::max(guide.a, guide.b);
            const glm::vec3 c[8] = {
                {minimum.x, minimum.y, minimum.z}, {maximum.x, minimum.y, minimum.z},
                {maximum.x, minimum.y, maximum.z}, {minimum.x, minimum.y, maximum.z},
                {minimum.x, maximum.y, minimum.z}, {maximum.x, maximum.y, minimum.z},
                {maximum.x, maximum.y, maximum.z}, {minimum.x, maximum.y, maximum.z}};
            const int edges[12][2] = {
                {0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},
                {0,4},{1,5},{2,6},{3,7}};
            for (const auto& edge : edges)
                m_measurementLines->AddLine(c[edge[0]], c[edge[1]], color);
        }
        for (const glm::vec3 endpoint : {guide.a, guide.b}) {
            m_measurementLines->AddLine(endpoint - glm::vec3(marker, 0, 0),
                                        endpoint + glm::vec3(marker, 0, 0), color);
            m_measurementLines->AddLine(endpoint - glm::vec3(0, marker, 0),
                                        endpoint + glm::vec3(0, marker, 0), color);
            m_measurementLines->AddLine(endpoint - glm::vec3(0, 0, marker),
                                        endpoint + glm::vec3(0, 0, marker), color);
        }
    }
    m_measurementLines->Draw(viewProj, 2.25f, true);
}

bool EditorViewport::ContainsPoint(float x, float y, int width, int height) const {
    return x > 380.0f
        && x < static_cast<float>(width) - 360.0f
        && y > 70.0f
        && y < static_cast<float>(height) - 90.0f;
}

void EditorViewport::DrawSceneGizmo(engine::Renderer& renderer,
                                    engine::Shader& shader,
                                    const engine::Mesh& cube,
                                    const engine::Mesh& cone,
                                    const EditorScene& scene,
                                    const EditorGizmo& gizmo,
                                    const glm::mat4& viewProj,
                                    const engine::Camera& camera,
                                    int viewportHeight,
                                    const glm::vec3* pivotOverride) const {
    const EditorScene::Object* selected = scene.SelectedObject();
    const engine::ecs::Transform* selectedTransform = selected
        ? scene.TryGetTransform(selected->entity)
        : nullptr;
    if (!selected || !selectedTransform || selected->locked) {
        return;
    }

    shader.Bind();
    shader.SetMat4("uViewProj", viewProj);
    shader.SetVec3("uLightDir", glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f)));
    shader.SetVec3("uEmissive", glm::vec3(0.0f));

    // Terrain renders as a mesh spanning local [0,size] (corner at the transform position),
    // so centre the gizmo on the terrain footprint instead of its corner.
    const glm::vec3 center = pivotOverride ? *pivotOverride : selectedTransform->position
        + (selected->isTerrain ? glm::vec3(selected->terrainSize * 0.5f, 0.0f, selected->terrainSize * 0.5f)
                               : glm::vec3(0.0f));
    const glm::vec3 xColor = AxisColor(EditorGizmo::Axis::X, gizmo.CurrentAxis());
    const glm::vec3 yColor = AxisColor(EditorGizmo::Axis::Y, gizmo.CurrentAxis());
    const glm::vec3 zColor = AxisColor(EditorGizmo::Axis::Z, gizmo.CurrentAxis());

    engine::ecs::Transform gizmoTransform = *selectedTransform;
    gizmoTransform.position = center;
    const float gizmoScale = GizmoWorldScale(gizmoTransform, camera, viewportHeight) * gizmo.VisualScale();
    const float length = gizmoScale * 0.78f;
    const float head = gizmoScale * 0.16f;
    const float ringRadius = gizmoScale * 0.72f;
    const float ringMarker = gizmoScale * 0.018f;
    const bool useLocalBasis = gizmo.CurrentSpace() == EditorGizmo::Space::Local
        || gizmo.CurrentMode() == EditorGizmo::Mode::Scale;
    const glm::mat3 localBasis = useLocalBasis
        ? glm::mat3_cast(selectedTransform->rotation) : glm::mat3(1.0f);
    const glm::vec3 localX = glm::normalize(localBasis * glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::vec3 localY = glm::normalize(localBasis * glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::vec3 localZ = glm::normalize(localBasis * glm::vec3(0.0f, 0.0f, 1.0f));
    const GLboolean depthEnabled = glIsEnabled(GL_DEPTH_TEST);
    glDisable(GL_DEPTH_TEST);

    if (m_gizmoLines && gizmo.CurrentMode() != EditorGizmo::Mode::Rotate) {
        m_gizmoLines->Clear();
        if(const auto* probe=scene.TryGetReflectionProbe(selected->entity)){
            const glm::vec3 probeColor=!probe->HasCapture()?glm::vec3(1.0f,0.55f,0.1f):glm::vec3(0.2f,0.75f,1.0f);
            if(probe->shape==engine::ecs::ReflectionProbe::Shape::Box){const glm::vec3 e=probe->boxExtents*glm::abs(selectedTransform->scale);
                glm::vec3 corners[8];for(int i=0;i<8;++i)corners[i]=center+glm::vec3((i&1)?e.x:-e.x,(i&2)?e.y:-e.y,(i&4)?e.z:-e.z);
                const int edges[12][2]={{0,1},{0,2},{0,4},{1,3},{1,5},{2,3},{2,6},{3,7},{4,5},{4,6},{5,7},{6,7}};
                for(const auto& edge:edges)m_gizmoLines->AddLine(corners[edge[0]],corners[edge[1]],probeColor);}
            else{const int segments=48;for(int axis=0;axis<3;++axis)for(int i=0;i<segments;++i){const float a=float(i)*6.28318530718f/segments,b=float(i+1)*6.28318530718f/segments;glm::vec3 p0(0),p1(0);
                    if(axis==0){p0=glm::vec3(0,std::cos(a),std::sin(a));p1=glm::vec3(0,std::cos(b),std::sin(b));}else if(axis==1){p0=glm::vec3(std::cos(a),0,std::sin(a));p1=glm::vec3(std::cos(b),0,std::sin(b));}else{p0=glm::vec3(std::cos(a),std::sin(a),0);p1=glm::vec3(std::cos(b),std::sin(b),0);}m_gizmoLines->AddLine(center+p0*probe->radius,center+p1*probe->radius,probeColor);}}
        }
        m_gizmoLines->AddLine(center, center + localX * length, xColor);
        m_gizmoLines->AddLine(center, center + localY * length, yColor);
        m_gizmoLines->AddLine(center, center + localZ * length, zColor);
        m_gizmoLines->Draw(viewProj, 3.0f, false, false);
        shader.Bind();
        shader.SetMat4("uViewProj", viewProj);
    }

    auto setAxisStyle = [&](const glm::vec3& color) {
        shader.SetVec3("uEmissive", color * 0.45f);
    };

    switch (gizmo.CurrentMode()) {
    case EditorGizmo::Mode::Translate:
        setAxisStyle(xColor);
        DrawGizmoCone(renderer, shader, cone, center + localX * (length + head * 0.55f), localX, xColor, head);

        setAxisStyle(yColor);
        DrawGizmoCone(renderer, shader, cone, center + localY * (length + head * 0.55f), localY, yColor, head);

        setAxisStyle(zColor);
        DrawGizmoCone(renderer, shader, cone, center + localZ * (length + head * 0.55f), localZ, zColor, head);
        break;

    case EditorGizmo::Mode::Scale:
        DrawGizmoBox(renderer, shader, cube, center, glm::vec3(head * 0.48f),
            gizmo.CurrentAxis() == EditorGizmo::Axis::All ? glm::vec3(1.0f, 0.86f, 0.24f) : glm::vec3(0.82f));
        setAxisStyle(xColor);
        DrawGizmoBox(renderer, shader, cube, center + localX * length,
            glm::vec3(head * 0.72f), xColor);

        setAxisStyle(yColor);
        DrawGizmoBox(renderer, shader, cube, center + localY * length,
            glm::vec3(head * 0.72f), yColor);

        setAxisStyle(zColor);
        DrawGizmoBox(renderer, shader, cube, center + localZ * length,
            glm::vec3(head * 0.72f), zColor);
        break;

    case EditorGizmo::Mode::Rotate:
        setAxisStyle(xColor);
        DrawGizmoRing(renderer, shader, cube, center, EditorGizmo::Axis::X, localBasis, xColor, ringRadius, ringMarker);
        setAxisStyle(yColor);
        DrawGizmoRing(renderer, shader, cube, center, EditorGizmo::Axis::Y, localBasis, yColor, ringRadius, ringMarker);
        setAxisStyle(zColor);
        DrawGizmoRing(renderer, shader, cube, center, EditorGizmo::Axis::Z, localBasis, zColor, ringRadius, ringMarker);
        break;
    }
    shader.SetVec3("uEmissive", glm::vec3(0.0f));
    if (depthEnabled) glEnable(GL_DEPTH_TEST);
}

void EditorViewport::DrawSelectedLightGuide(engine::Renderer& renderer,
                                            engine::Shader& shader,
                                            const engine::Mesh& cube,
                                            const EditorScene& scene,
                                            const glm::mat4& viewProj,
                                            bool selectedOnly) const {
    const EditorScene::Object* selected = scene.SelectedObject();
    if (selectedOnly && (!selected || !selected->light || !selected->visible)) {
        return;
    }

    shader.Bind();
    shader.SetMat4("uViewProj", viewProj);
    shader.SetVec3("uLightDir", glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f)));
    shader.SetInt("uHasDiffuse", 0);

    constexpr float marker = 0.045f;
    for (const EditorScene::Object& object : scene.Objects()) {
        if (!object.light || !object.visible) {
            continue;
        }
        const bool selectedLight = selected && selected->entity == object.entity;
        if (selectedOnly && !selectedLight) {
            continue;
        }

        const engine::ecs::Transform* transform = scene.TryGetTransform(object.entity);
        const engine::ecs::Light* light = scene.TryGetLight(object.entity);
        if (!transform || !light) {
            continue;
        }

        const glm::vec3 color = selectedLight
            ? LightGuideColor(*light)
            : glm::mix(glm::vec3(0.35f), LightGuideColor(*light), 0.58f);
        shader.SetVec3("uEmissive", color * (selectedLight ? 0.45f : 0.18f));

        switch (light->type) {
        case engine::ecs::Light::Type::Directional:
            if (glm::dot(light->direction, light->direction) <= 0.0001f) {
                break;
            }
            DrawGuideSegment(renderer, shader, cube, transform->position,
                transform->position + glm::normalize(light->direction) * 2.4f, 0.045f, color);
            break;
        case engine::ecs::Light::Type::Point: {
            const glm::vec3 c = light->color * light->intensity;
            const float radius = std::sqrt(std::max(std::max(c.r, c.g), c.b) / 0.03f);
            DrawGuideRing(renderer, shader, cube, transform->position, EditorGizmo::Axis::X, radius, marker, color);
            DrawGuideRing(renderer, shader, cube, transform->position, EditorGizmo::Axis::Y, radius, marker, color);
            DrawGuideRing(renderer, shader, cube, transform->position, EditorGizmo::Axis::Z, radius, marker, color);
            break;
        }
        case engine::ecs::Light::Type::Spot:
            DrawSpotConeGuide(renderer, shader, cube, transform->position, *light, color);
            break;
        case engine::ecs::Light::Type::Area:
            DrawGuideRing(renderer, shader, cube, transform->position, EditorGizmo::Axis::X, light->sourceRadius, marker, color);
            DrawGuideRing(renderer, shader, cube, transform->position, EditorGizmo::Axis::Y, light->sourceRadius, marker, color);
            DrawGuideRing(renderer, shader, cube, transform->position, EditorGizmo::Axis::Z, light->sourceRadius, marker, color);
            break;
        }
    }

    shader.SetVec3("uEmissive", glm::vec3(0.0f));
}

void EditorViewport::DrawWorldGrid(const glm::mat4& viewProj) const {
    if (!m_gridLines) return;

    const int   half   = 20;                 // grid half-extent in units (covers -20..+20)
    const float span   = static_cast<float>(half);
    const glm::vec3 minorColor(0.30f, 0.32f, 0.36f);
    const glm::vec3 majorColor(0.45f, 0.48f, 0.53f);

    m_gridLines->Clear();
    for (int i = -half; i <= half; ++i) {
        if (i == 0) continue;                // the centre lines are the coloured axes below
        const float f = static_cast<float>(i);
        const bool  major = (i % 5) == 0;
        const glm::vec3 color = major ? majorColor : minorColor;
        m_gridLines->AddLine(glm::vec3(f, 0.0f, -span), glm::vec3(f, 0.0f, span), color);
        m_gridLines->AddLine(glm::vec3(-span, 0.0f, f), glm::vec3(span, 0.0f, f), color);
    }
    m_gridLines->Draw(viewProj, 1.0f, false, true);

    // Coloured world axes through the origin: X red, Z blue, Y green (short, upward).
    const glm::vec3 xCol(0.86f, 0.30f, 0.30f);
    const glm::vec3 zCol(0.32f, 0.50f, 0.92f);
    const glm::vec3 yCol(0.34f, 0.80f, 0.40f);
    m_gridLines->Clear();
    m_gridLines->AddLine(glm::vec3(-span, 0.0f, 0.0f), glm::vec3(span, 0.0f, 0.0f), xCol);
    m_gridLines->AddLine(glm::vec3(0.0f, 0.0f, -span), glm::vec3(0.0f, 0.0f, span), zCol);
    m_gridLines->AddLine(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, span * 0.2f, 0.0f), yCol);
    m_gridLines->Draw(viewProj, 2.0f, false, true);
}

void EditorViewport::DrawLightingAnalysisOverlay(
    const std::vector<LightingAnalysisGuide>& guides,
    int mode, float cellSize, const glm::mat4& viewProj) const {
    if (!m_gridLines || guides.empty() || mode <= 0) return;
    m_gridLines->Clear();
    const float half = std::clamp(cellSize * 0.42f, 0.04f, 8.0f);
    const std::size_t count = std::min<std::size_t>(guides.size(), 4096);
    for (std::size_t i = 0; i < count; ++i) {
        const LightingAnalysisGuide& guide = guides[i];
        const float value = std::clamp(guide.value, 0.0f, 1.5f);
        glm::vec3 color(0.2f, 0.85f, 0.35f);
        switch (mode) {
        case 1: // light complexity: cool -> warm -> critical
            color = value < 0.5f
                ? glm::mix(glm::vec3(0.1f, 0.75f, 0.35f),
                           glm::vec3(1.0f, 0.75f, 0.1f), value * 2.0f)
                : glm::mix(glm::vec3(1.0f, 0.75f, 0.1f),
                           glm::vec3(1.0f, 0.12f, 0.08f),
                           std::min(1.0f, (value - 0.5f) * 2.0f));
            break;
        case 2: color = glm::mix(glm::vec3(0.12f, 0.65f, 1.0f),
                                 glm::vec3(0.95f, 0.15f, 0.85f),
                                 std::min(value, 1.0f)); break;
        case 3: color = glm::mix(glm::vec3(0.08f, 0.28f, 1.0f),
                                 glm::vec3(1.0f, 0.22f, 0.05f),
                                 std::min(value, 1.0f)); break;
        case 4: color = guide.warning ? glm::vec3(1.0f, 0.08f, 0.05f)
                                      : glm::vec3(0.18f, 0.6f, 0.28f); break;
        case 5: color = guide.warning ? glm::vec3(1.0f, 0.12f, 0.75f)
                                      : glm::vec3(0.7f, 0.35f, 1.0f); break;
        default: break;
        }
        const glm::vec3 p = guide.position + glm::vec3(0.0f, 0.025f, 0.0f);
        m_gridLines->AddLine(p + glm::vec3(-half, 0, -half),
                             p + glm::vec3(half, 0, -half), color);
        m_gridLines->AddLine(p + glm::vec3(half, 0, -half),
                             p + glm::vec3(half, 0, half), color);
        m_gridLines->AddLine(p + glm::vec3(half, 0, half),
                             p + glm::vec3(-half, 0, half), color);
        m_gridLines->AddLine(p + glm::vec3(-half, 0, half),
                             p + glm::vec3(-half, 0, -half), color);
        if (guide.warning || mode == 5) {
            m_gridLines->AddLine(p + glm::vec3(-half, 0, -half),
                                 p + glm::vec3(half, 0, half), color);
            m_gridLines->AddLine(p + glm::vec3(-half, 0, half),
                                 p + glm::vec3(half, 0, -half), color);
        }
    }
    m_gridLines->Draw(viewProj, 2.0f, true);
}

void EditorViewport::DrawPhysicsColliderGuides(const EditorScene& scene,
                                               const glm::mat4& viewProj,
                                               bool selectedOnly) const {
    const EditorScene::Object* selected = scene.SelectedObject();
    if (selectedOnly && (!selected || !selected->colliderEnabled || !selected->visible)) {
        return;
    }

    if (!m_colliderLines) return;
    m_colliderLines->Clear();

    for (const EditorScene::Object& object : scene.Objects()) {
        if (!object.colliderEnabled || !object.visible) {
            continue;
        }
        const bool selectedCollider = selected && selected->entity == object.entity;
        if (selectedOnly && !selectedCollider) {
            continue;
        }

        const engine::ecs::Transform* transform = scene.TryGetTransform(object.entity);
        if (!transform) {
            continue;
        }

        std::vector<const engine::ecs::Collider*> colliderSet{&object.collider};
        for (const engine::ecs::Collider& extra : object.additionalColliders)
            colliderSet.push_back(&extra);
        for (const engine::ecs::Collider* localCollider : colliderSet) {
        const engine::physics::WorldCollider world =
            engine::physics::BuildWorldCollider(*transform, *localCollider);
        const engine::ecs::Transform& colliderTransform = world.transform;
        const engine::ecs::Collider& collider = world.collider;
        const glm::vec3 baseColor = PhysicsGuideColor(object);
        const glm::vec3 color = selectedCollider
            ? baseColor
            : glm::mix(glm::vec3(0.30f), baseColor, 0.48f);

        // Center-of-mass debug marker (Pass-3, Phase 68): draw the physical COM (yellow cross)
        // and the entity origin (cyan cross) distinctly, once per rigid-body object, so an
        // offset COM is visible. COM local = collider offset (auto) or the authored offset.
        if (localCollider == &object.collider && object.rigidBodyEnabled
            && object.rigidBody.invMass > 0.0f) {
            const glm::vec3 comLocal = object.rigidBody.autoCenterOfMass
                ? object.collider.localPosition : object.rigidBody.centerOfMassLocal;
            const glm::vec3 comW = transform->position + transform->rotation * comLocal;
            const auto cross = [&](const glm::vec3& c, const glm::vec3& col, float s) {
                m_colliderLines->AddLine(c - glm::vec3(s, 0, 0), c + glm::vec3(s, 0, 0), col);
                m_colliderLines->AddLine(c - glm::vec3(0, s, 0), c + glm::vec3(0, s, 0), col);
                m_colliderLines->AddLine(c - glm::vec3(0, 0, s), c + glm::vec3(0, 0, s), col);
            };
            cross(comW, glm::vec3(1.0f, 0.85f, 0.1f), 0.13f);          // COM: yellow
            if (glm::dot(comLocal, comLocal) > 1e-6f)
                cross(transform->position, glm::vec3(0.2f, 0.8f, 1.0f), 0.07f);  // origin: cyan
        }

        switch (collider.shape) {
        case engine::ecs::ColliderShape::Sphere:
            DrawSphereColliderGuide(*m_colliderLines, colliderTransform, collider, color);
            break;
        case engine::ecs::ColliderShape::Box:
            DrawBoxColliderGuide(*m_colliderLines, colliderTransform, collider, color);
            break;
        case engine::ecs::ColliderShape::ConvexHull:
        case engine::ecs::ColliderShape::TriangleMesh: {
            const std::string source = collider.collisionAssetPath.empty()
                ? object.modelAssetPath : collider.collisionAssetPath;
            const auto mesh = engine::physics::AcquireCollisionMesh(
                source);
            if (!mesh) {
                DrawBoxColliderGuide(*m_colliderLines, colliderTransform, collider, color);
                break;
            }
            const auto worldPoint = [&](const glm::vec3& local) {
                return colliderTransform.position + colliderTransform.rotation
                    * (colliderTransform.scale * local);
            };
            bool drewGeometry = false;
            if (collider.shape == engine::ecs::ColliderShape::ConvexHull) {
                for (const engine::physics::CollisionEdge& edge : mesh->convexHullEdges) {
                    m_colliderLines->AddLine(worldPoint(edge.a), worldPoint(edge.b), color);
                    drewGeometry = true;
                }
            } else {
                for (const engine::physics::CollisionTriangle& triangle : mesh->triangles) {
                    const glm::vec3 a = worldPoint(triangle.a);
                    const glm::vec3 b = worldPoint(triangle.b);
                    const glm::vec3 c = worldPoint(triangle.c);
                    m_colliderLines->AddLine(a, b, color);
                    m_colliderLines->AddLine(b, c, color);
                    m_colliderLines->AddLine(c, a, color);
                    drewGeometry = true;
                }
            }
            if (!drewGeometry)
                DrawBoxColliderGuide(*m_colliderLines, colliderTransform, collider, color);
            break;
        }
        case engine::ecs::ColliderShape::Plane:
            DrawPlaneColliderGuide(*m_colliderLines, collider, color);
            break;
        case engine::ecs::ColliderShape::Capsule:
            DrawCapsuleColliderGuide(*m_colliderLines, colliderTransform, collider, color);
            break;
        case engine::ecs::ColliderShape::Cylinder:
            DrawCylinderColliderGuide(*m_colliderLines, colliderTransform, collider, color);
            break;
        case engine::ecs::ColliderShape::Cone:
            DrawConeColliderGuide(*m_colliderLines, colliderTransform, collider, color);
            break;
        case engine::ecs::ColliderShape::Pyramid:
            DrawPyramidColliderGuide(*m_colliderLines, colliderTransform, collider, color);
            break;
        case engine::ecs::ColliderShape::Torus:
            DrawTorusColliderGuide(*m_colliderLines, colliderTransform, collider, color);
            break;
        case engine::ecs::ColliderShape::Staircase: {
            const int steps = glm::clamp(collider.steps, 1, 32);
            const float slice = collider.halfExtents.z * 2.0f / steps;
            for (int i = 0; i < steps; ++i) {
                const float height = collider.halfExtents.y * 2.0f * static_cast<float>(i + 1) / steps;
                const glm::vec3 ext(collider.halfExtents.x, height * 0.5f, slice * 0.5f);
                engine::ecs::Transform piece = colliderTransform;
                piece.position += colliderTransform.rotation * glm::vec3(0.0f,
                    -collider.halfExtents.y + ext.y,
                    -collider.halfExtents.z + slice * (static_cast<float>(i) + 0.5f));
                piece.scale = glm::vec3(1.0f);
                DrawBoxColliderGuide(*m_colliderLines, piece,
                    engine::ecs::Collider::MakeBox(ext), color);
            }
            break;
        }
        }
        }
    }

    m_colliderLines->Draw(viewProj, 1.5f, true);
}

void EditorViewport::DrawTerrainBrushGuide(
    const engine::Terrain& terrain,
    const engine::ecs::Transform& transform,
    const glm::vec2& localCenter,
    float radius,
    int brushMode,
    bool applying,
    const glm::mat4& viewProj) const {
    if (!m_colliderLines || !terrain.Ready() || terrain.Map().h.empty()) return;

    radius = std::max(radius, 0.1f);
    const float terrainSize = terrain.Map().size;
    if (localCenter.x + radius < 0.0f || localCenter.y + radius < 0.0f
        || localCenter.x - radius > terrainSize
        || localCenter.y - radius > terrainSize) {
        return;
    }

    static const glm::vec3 modeColors[] = {
        {0.20f, 0.95f, 0.42f}, // raise
        {1.00f, 0.30f, 0.24f}, // lower
        {0.20f, 0.78f, 1.00f}, // smooth
        {0.82f, 0.42f, 1.00f}, // flatten
        {0.30f, 0.95f, 0.32f}, // paint
        {1.00f, 0.52f, 0.16f}  // erase
    };
    const int colorIndex = std::clamp(brushMode, 0, 5);
    const glm::vec3 color = applying
        ? glm::mix(modeColors[colorIndex], glm::vec3(1.0f), 0.30f)
        : modeColors[colorIndex];
    const glm::vec3 innerColor = color * 0.62f;
    const glm::mat4 model = transform.Model();
    // Lift just above the surface to avoid z-fighting while retaining depth tests.
    const float lift = std::max(terrain.Map().maxHeight * 0.00035f, 0.015f);

    auto inside = [terrainSize](const glm::vec2& p) {
        return p.x >= 0.0f && p.y >= 0.0f
            && p.x <= terrainSize && p.y <= terrainSize;
    };
    auto worldPoint = [&](const glm::vec2& p) {
        const float y = terrain.HeightAt(p.x, p.y) + lift;
        return glm::vec3(model * glm::vec4(p.x, y, p.y, 1.0f));
    };
    auto addRing = [&](float ringRadius, const glm::vec3& ringColor) {
        constexpr int segments = 96;
        for (int i = 0; i < segments; ++i) {
            const float a0 = glm::two_pi<float>() * static_cast<float>(i) / segments;
            const float a1 = glm::two_pi<float>() * static_cast<float>(i + 1) / segments;
            const glm::vec2 p0 = localCenter
                + glm::vec2(std::cos(a0), std::sin(a0)) * ringRadius;
            const glm::vec2 p1 = localCenter
                + glm::vec2(std::cos(a1), std::sin(a1)) * ringRadius;
            // Do not draw beyond the heightfield: the clipped ring now shows the
            // exact portion of the brush that can affect edge vertices.
            if (inside(p0) && inside(p1))
                m_colliderLines->AddLine(worldPoint(p0), worldPoint(p1), ringColor);
        }
    };

    m_colliderLines->Clear();
    addRing(radius, color);
    // A subtle half-radius ring makes the smooth falloff easier to judge.
    addRing(radius * 0.5f, innerColor);

    if (inside(localCenter)) {
        const float crossRadius = std::clamp(radius * 0.08f, 0.08f, 0.45f);
        const glm::vec2 x0(localCenter.x - crossRadius, localCenter.y);
        const glm::vec2 x1(localCenter.x + crossRadius, localCenter.y);
        const glm::vec2 z0(localCenter.x, localCenter.y - crossRadius);
        const glm::vec2 z1(localCenter.x, localCenter.y + crossRadius);
        if (inside(x0) && inside(x1))
            m_colliderLines->AddLine(worldPoint(x0), worldPoint(x1), color);
        if (inside(z0) && inside(z1))
            m_colliderLines->AddLine(worldPoint(z0), worldPoint(z1), color);
    }

    m_colliderLines->Draw(viewProj, applying ? 3.0f : 2.0f, false);
}

void EditorViewport::DrawFoliageBrushGuide(
    const std::vector<glm::vec3>& ring, const glm::vec3& center,
    bool erase, bool applying, const glm::mat4& viewProj) const {
    if (!m_colliderLines || ring.size() < 3) return;
    const glm::vec3 base = erase
        ? glm::vec3(1.0f, 0.28f, 0.18f)
        : glm::vec3(0.22f, 1.0f, 0.42f);
    const glm::vec3 color = applying
        ? glm::mix(base, glm::vec3(1.0f), 0.35f) : base;
    m_colliderLines->Clear();
    for (std::size_t i = 0; i < ring.size(); ++i)
        m_colliderLines->AddLine(ring[i], ring[(i + 1) % ring.size()], color);
    // Centre cross remains readable even when the terrain is steep.
    const float arm = std::clamp(glm::distance(ring.front(), center) * 0.08f,
                                 0.08f, 0.55f);
    m_colliderLines->AddLine(center - glm::vec3(arm, 0.0f, 0.0f),
                             center + glm::vec3(arm, 0.0f, 0.0f), color);
    m_colliderLines->AddLine(center - glm::vec3(0.0f, 0.0f, arm),
                             center + glm::vec3(0.0f, 0.0f, arm), color);
    m_colliderLines->Draw(viewProj, applying ? 3.5f : 2.5f, false);
}

void EditorViewport::DrawCharacterFacingArrows(engine::Renderer& renderer,
                                               engine::Shader& shader,
                                               const engine::Mesh& cube,
                                               const EditorScene& scene,
                                               const glm::mat4& viewProj) const {
    const EditorScene::Object* selected = scene.SelectedObject();
    shader.Bind();
    shader.SetMat4("uViewProj", viewProj);
    shader.SetVec3("uLightDir", glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f)));
    shader.SetInt("uHasDiffuse", 0);

    for (const EditorScene::Object& object : scene.Objects()) {
        if (!object.visible || !object.skeletalModel) {
            continue;
        }
        const engine::ecs::Transform* transform = scene.TryGetTransform(object.entity);
        if (!transform) {
            continue;
        }

        // Object forward = local -Z, projected onto the ground plane so the arrow lies
        // flat like Unreal's. The player controller faces the mesh this way, so aligning
        // the character to the arrow makes it face forward when it moves.
        glm::vec3 dir = glm::mat3_cast(transform->rotation) * glm::vec3(0.0f, 0.0f, -1.0f);
        dir.y = 0.0f;
        if (glm::dot(dir, dir) < 1e-6f) {
            dir = glm::vec3(0.0f, 0.0f, -1.0f);
        }
        dir = glm::normalize(dir);
        const glm::vec3 up(0.0f, 1.0f, 0.0f);
        const glm::vec3 side = glm::normalize(glm::cross(up, dir));

        const bool isSelected = selected && selected->entity == object.entity;
        const glm::vec3 color = isSelected ? glm::vec3(0.15f, 0.95f, 1.0f)
                                           : glm::vec3(0.10f, 0.55f, 0.62f);
        shader.SetVec3("uEmissive", color * (isSelected ? 0.40f : 0.15f));

        const float radius = std::max(object.collider.radius, 0.25f);
        const float length = std::max(radius * 2.6f, 0.9f);
        const glm::vec3 base = transform->position + up * 0.02f;
        const glm::vec3 tip = base + dir * length;
        const float head = length * 0.28f;
        DrawGuideSegment(renderer, shader, cube, base, tip, 0.03f, color);
        DrawGuideSegment(renderer, shader, cube, tip,
            tip - dir * head + side * head * 0.6f, 0.03f, color);
        DrawGuideSegment(renderer, shader, cube, tip,
            tip - dir * head - side * head * 0.6f, 0.03f, color);
    }

    shader.SetVec3("uEmissive", glm::vec3(0.0f));
}

void EditorViewport::DrawPhysicsEventGuides(engine::Renderer& renderer,
                                            engine::Shader& shader,
                                            const engine::Mesh& cube,
                                            const std::vector<PhysicsEventGuide>& guides,
                                            const glm::mat4& viewProj) const {
    if (guides.empty()) {
        return;
    }

    shader.Bind();
    shader.SetMat4("uViewProj", viewProj);
    shader.SetVec3("uLightDir", glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f)));
    shader.SetInt("uHasDiffuse", 0);

    for (const PhysicsEventGuide& guide : guides) {
        if (glm::length(guide.b - guide.a) <= 0.001f) {
            continue;
        }

        const glm::vec3 color = PhysicsEventGuideColor(guide);
        const float thickness = guide.trigger ? 0.045f : 0.035f;
        const glm::vec3 markerSize(guide.trigger ? 0.09f : 0.065f);

        shader.SetVec3("uEmissive", color * (guide.trigger ? 0.48f : 0.32f));
        DrawGuideSegment(renderer, shader, cube, guide.a, guide.b, thickness, color);
        DrawGizmoBox(renderer, shader, cube, guide.a, markerSize, color);
        DrawGizmoBox(renderer, shader, cube, guide.b, markerSize, color);
    }

    shader.SetVec3("uEmissive", glm::vec3(0.0f));
}

void EditorViewport::DrawGameplayTraceGuides(
    engine::Renderer& renderer, engine::Shader& shader, const engine::Mesh& cube,
    const std::vector<GameplayTraceGuide>& guides, const glm::mat4& viewProj) const {
    if (guides.empty()) return;
    shader.Bind();
    shader.SetMat4("uViewProj", viewProj);
    shader.SetVec3("uLightDir", glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f)));
    shader.SetInt("uHasDiffuse", 0);
    for (const GameplayTraceGuide& guide : guides) {
        const glm::vec3 color = guide.hit
            ? glm::vec3(1.0f, 0.16f, 0.10f)
            : glm::vec3(0.15f, 0.95f, 0.35f);
        shader.SetVec3("uEmissive", color * 0.55f);
        if (guide.type == 2) {
            const float r = std::max(guide.radius, 0.05f);
            DrawGuideSegment(renderer, shader, cube,
                guide.a - glm::vec3(r, 0.0f, 0.0f), guide.a + glm::vec3(r, 0.0f, 0.0f), 0.025f, color);
            DrawGuideSegment(renderer, shader, cube,
                guide.a - glm::vec3(0.0f, r, 0.0f), guide.a + glm::vec3(0.0f, r, 0.0f), 0.025f, color);
            DrawGuideSegment(renderer, shader, cube,
                guide.a - glm::vec3(0.0f, 0.0f, r), guide.a + glm::vec3(0.0f, 0.0f, r), 0.025f, color);
            continue;
        }
        if (glm::length(guide.b - guide.a) <= 0.001f) continue;
        DrawGuideSegment(renderer, shader, cube, guide.a, guide.b,
            guide.type == 1 ? 0.05f : 0.035f, color);
        DrawGizmoBox(renderer, shader, cube, guide.a,
            glm::vec3(std::max(guide.radius, 0.055f)), color);
        DrawGizmoBox(renderer, shader, cube, guide.b,
            glm::vec3(std::max(guide.radius, 0.055f)), color);
    }
    shader.SetVec3("uEmissive", glm::vec3(0.0f));
}

void EditorViewport::DrawPhysicsJointGuides(engine::Renderer& renderer,
                                            engine::Shader& shader,
                                            const engine::Mesh& cube,
                                            const std::vector<PhysicsJointGuide>& guides,
                                            const glm::mat4& viewProj) const {
    if (guides.empty()) {
        return;
    }

    shader.Bind();
    shader.SetMat4("uViewProj", viewProj);
    shader.SetVec3("uLightDir", glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f)));
    shader.SetInt("uHasDiffuse", 0);

    for (const PhysicsJointGuide& guide : guides) {
        const glm::vec3 color = PhysicsJointGuideColor(guide);
        shader.SetVec3("uEmissive", color * (guide.enabled ? 0.32f : 0.10f));

        if (guide.hasPivot) {
            // Ball/Hinge: draw both bodies tied to the pivot pin, a slightly larger pivot box, and
            // (hinge) the rotation axis through the pivot.
            if (glm::length(guide.a - guide.pivot) > 0.001f)
                DrawGuideSegment(renderer, shader, cube, guide.a, guide.pivot, 0.030f, color);
            if (glm::length(guide.b - guide.pivot) > 0.001f)
                DrawGuideSegment(renderer, shader, cube, guide.b, guide.pivot, 0.030f, color);
            DrawGizmoBox(renderer, shader, cube, guide.pivot, glm::vec3(0.075f), color);
            if (guide.type == 3) {
                const glm::vec3 ax = (glm::dot(guide.axis, guide.axis) > 1e-6f)
                    ? glm::normalize(guide.axis) : glm::vec3(0.0f, 1.0f, 0.0f);
                const glm::vec3 axisCol(1.0f, 0.92f, 0.45f);
                shader.SetVec3("uEmissive", axisCol * 0.4f);
                DrawGuideSegment(renderer, shader, cube, guide.pivot - ax * 0.45f,
                                 guide.pivot + ax * 0.45f, 0.022f, axisCol);
            }
            continue;
        }

        if (glm::length(guide.b - guide.a) <= 0.001f) {
            continue;
        }
        DrawGuideSegment(renderer, shader, cube, guide.a, guide.b, guide.rope ? 0.030f : 0.038f, color);
        DrawGizmoBox(renderer, shader, cube, guide.a, glm::vec3(0.055f), color);
        DrawGizmoBox(renderer, shader, cube, guide.b, glm::vec3(0.055f), color);
    }

    shader.SetVec3("uEmissive", glm::vec3(0.0f));
}

void EditorViewport::DrawCameraSequenceGuides(
    engine::Renderer& renderer, engine::Shader& shader, const engine::Mesh& cube,
    const EditorScene& scene, const glm::mat4& viewProj) const {
    if (scene.CameraSequences().empty()) return;
    auto findCamera = [&](const std::string& name) -> const EditorScene::CameraPreset* {
        const auto found = std::find_if(
            scene.CameraPresets().begin(), scene.CameraPresets().end(),
            [&](const EditorScene::CameraPreset& camera) { return camera.name == name; });
        return found == scene.CameraPresets().end() ? nullptr : &*found;
    };

    shader.Bind();
    shader.SetMat4("uViewProj", viewProj);
    shader.SetVec3("uLightDir", glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f)));
    shader.SetInt("uHasDiffuse", 0);
    const glm::vec3 railColor(0.25f, 0.75f, 1.0f);
    const glm::vec3 shotColor(1.0f, 0.55f, 0.18f);
    shader.SetVec3("uEmissive", railColor * 0.35f);

    for (const EditorScene::CameraSequence& sequence : scene.CameraSequences()) {
        std::vector<const EditorScene::CameraPreset*> cameras;
        std::vector<const EditorScene::CameraSequenceShot*> shots;
        for (const EditorScene::CameraSequenceShot& shot : sequence.shots) {
            if (const EditorScene::CameraPreset* camera = findCamera(shot.cameraName)) {
                cameras.push_back(camera);
                shots.push_back(&shot);
                DrawGizmoBox(renderer, shader, cube, camera->position,
                             glm::vec3(0.12f), shotColor);
                DrawGuideSegment(renderer, shader, cube, camera->position,
                                 camera->target, 0.018f, glm::vec3(0.8f, 0.5f, 1.0f));
            }
        }
        for (std::size_t i = 1; i < cameras.size(); ++i) {
            glm::vec3 previous = cameras[i - 1]->position;
            constexpr int kSamples = 16;
            for (int sample = 1; sample <= kSamples; ++sample) {
                const float t = static_cast<float>(sample) / kSamples;
                glm::vec3 point = glm::mix(
                    cameras[i - 1]->position, cameras[i]->position, t);
                if (shots[i]->pathMode == 1) {
                    const glm::vec3 p0 = i > 1
                        ? cameras[i - 2]->position : cameras[i - 1]->position;
                    const glm::vec3 p3 = i + 1 < cameras.size()
                        ? cameras[i + 1]->position : cameras[i]->position;
                    point = engine::CameraSequencePlayer::CatmullRom(
                        p0, cameras[i - 1]->position, cameras[i]->position, p3, t);
                }
                DrawGuideSegment(renderer, shader, cube, previous, point, 0.025f, railColor);
                previous = point;
            }
        }
    }
    shader.SetVec3("uEmissive", glm::vec3(0.0f));
}

void EditorViewport::DrawSplineGuides(
    engine::Renderer& renderer, engine::Shader& shader, const engine::Mesh& cube,
    const EditorScene& scene, const glm::mat4& viewProj, int selectedPoint) const {
    (void)renderer;
    (void)shader;
    (void)cube;
    const EditorScene::Object* selected = scene.SelectedObject();
    if (!m_splineLines) return;
    m_splineLines->Clear();

    for (const EditorScene::Object& object : scene.Objects()) {
        if (!object.isSpline || !object.visible) continue;
        const bool isSel = selected && selected->entity == object.entity;

        // Curve colour by type: river = cyan, rail = orange, path = green; brighten if selected.
        glm::vec3 curveColor = object.splineType == 1 ? glm::vec3(0.2f, 0.7f, 1.0f)
                             : object.splineType == 2 ? glm::vec3(1.0f, 0.6f, 0.2f)
                             :                          glm::vec3(0.4f, 0.9f, 0.5f);
        if (isSel) curveColor = glm::mix(curveColor, glm::vec3(1.0f), 0.35f);

        if (object.splinePoints.size() >= 2) {
            engine::Spline spline(object.splinePoints, object.splineClosed);
            std::vector<glm::vec3> pts;
            const int samples = std::max(24, static_cast<int>(object.splinePoints.size()) * 12);
            spline.SampleUniform(samples, pts);
            for (std::size_t i = 1; i < pts.size(); ++i) {
                m_splineLines->AddLine(pts[i - 1], pts[i], curveColor);
            }

            // Direction chevrons make flow and rail orientation readable without
            // selecting the spline or opening its inspector.
            const int arrows = std::clamp(static_cast<int>(spline.Length() / 5.0f), 1, 12);
            for (int i = 1; i <= arrows; ++i) {
                const float distance = spline.Length() * static_cast<float>(i)
                    / static_cast<float>(arrows + 1);
                const glm::vec3 p = spline.PositionAtDistance(distance);
                const glm::vec3 tangent = spline.TangentAtDistance(distance);
                glm::vec3 side = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), tangent);
                if (glm::dot(side, side) < 1.0e-6f) side = glm::vec3(1.0f, 0.0f, 0.0f);
                else side = glm::normalize(side);
                const float size = isSel ? 0.42f : 0.3f;
                m_splineLines->AddLine(p, p - tangent * size + side * size * 0.55f, curveColor);
                m_splineLines->AddLine(p, p - tangent * size - side * size * 0.55f, curveColor);
            }
        }

        // A subdued control polygon plus shader lines for point handles. This removes
        // the old cube-mesh markers and keeps authoring fast on long splines.
        if (isSel) {
            const glm::vec3 polygonColor(0.34f, 0.38f, 0.43f);
            for (std::size_t i = 1; i < object.splinePoints.size(); ++i) {
                m_splineLines->AddLine(object.splinePoints[i - 1], object.splinePoints[i], polygonColor);
            }
            if (object.splineClosed && object.splinePoints.size() > 2) {
                m_splineLines->AddLine(object.splinePoints.back(), object.splinePoints.front(), polygonColor);
            }
        }
        for (std::size_t i = 0; i < object.splinePoints.size(); ++i) {
            const glm::vec3& p = object.splinePoints[i];
            const bool active = isSel && selectedPoint == static_cast<int>(i);
            const glm::vec3 handleColor = active ? glm::vec3(1.0f) : glm::vec3(1.0f, 0.82f, 0.16f);
            const float handle = active ? 0.32f : (isSel ? 0.22f : 0.14f);
            m_splineLines->AddLine(p - glm::vec3(handle, 0, 0), p + glm::vec3(handle, 0, 0), handleColor);
            m_splineLines->AddLine(p - glm::vec3(0, handle, 0), p + glm::vec3(0, handle, 0), handleColor);
            m_splineLines->AddLine(p - glm::vec3(0, 0, handle), p + glm::vec3(0, 0, handle), handleColor);
        }
    }
    m_splineLines->Draw(viewProj, 2.5f, true);
}

void EditorViewport::DrawSelectedRiverBoundary(
    const EditorScene& scene, const glm::mat4& viewProj) const {
    const EditorScene::Object* water = scene.SelectedObject();
    if (!water || !water->isWater || water->waterFlowSpline.empty() || !m_waterLines) return;
    const EditorScene::Object* splineObject = nullptr;
    for (const EditorScene::Object& object : scene.Objects()) {
        if (object.isSpline && object.name == water->waterFlowSpline
            && object.splinePoints.size() >= 2) {
            splineObject = &object;
            break;
        }
    }
    if (!splineObject) return;

    const engine::Spline spline(splineObject->splinePoints, splineObject->splineClosed);
    const float length = spline.Length();
    if (length <= 0.001f) return;
    const int samples = std::clamp(
        std::max(static_cast<int>(splineObject->splinePoints.size()) * 24,
                 static_cast<int>(std::ceil(length * 3.0f))), 16, 640);
    const float halfWidth = std::max(water->waterRiverWidth, 0.1f) * 0.5f;
    const glm::vec3 color = water->locked
        ? glm::vec3(1.0f, 0.28f, 0.08f) : glm::vec3(1.0f, 0.55f, 0.05f);
    m_waterLines->Clear();
    std::vector<glm::vec3> centers(static_cast<std::size_t>(samples + 1));
    for (int i = 0; i <= samples; ++i) {
        const float distance = length * static_cast<float>(i) / static_cast<float>(samples);
        centers[static_cast<std::size_t>(i)] = spline.PositionAtDistance(distance);
    }
    std::vector<float> rowHalfWidths(static_cast<std::size_t>(samples + 1), halfWidth);
    for (int i = 1; i < samples; ++i) {
        glm::vec3 before = centers[static_cast<std::size_t>(i)]
            - centers[static_cast<std::size_t>(i - 1)];
        glm::vec3 after = centers[static_cast<std::size_t>(i + 1)]
            - centers[static_cast<std::size_t>(i)];
        const float beforeLength = glm::length(before);
        const float afterLength = glm::length(after);
        if (beforeLength <= 1.0e-5f || afterLength <= 1.0e-5f) continue;
        before /= beforeLength;
        after /= afterLength;
        const float angle = std::acos(std::clamp(glm::dot(before, after), -1.0f, 1.0f));
        if (angle > 1.0e-4f) {
            const float radius = ((beforeLength + afterLength) * 0.5f) / angle;
            rowHalfWidths[static_cast<std::size_t>(i)] = std::min(
                halfWidth, std::max(radius * 0.82f, halfWidth * 0.24f));
        }
    }
    for (int pass = 0; pass < 4; ++pass) {
        std::vector<float> smoothed = rowHalfWidths;
        for (int i = 1; i < samples; ++i) {
            smoothed[static_cast<std::size_t>(i)] =
                rowHalfWidths[static_cast<std::size_t>(i - 1)] * 0.25f
                + rowHalfWidths[static_cast<std::size_t>(i)] * 0.5f
                + rowHalfWidths[static_cast<std::size_t>(i + 1)] * 0.25f;
        }
        rowHalfWidths.swap(smoothed);
    }
    glm::vec3 previousLeft(0.0f), previousRight(0.0f), firstLeft(0.0f), firstRight(0.0f);
    glm::vec3 transportedSide(1.0f, 0.0f, 0.0f);
    glm::vec3 previousTangent(0.0f, 0.0f, 1.0f);
    for (int i = 0; i <= samples; ++i) {
        const glm::vec3 center = centers[static_cast<std::size_t>(i)];
        const int tangentLo = std::max(i - 2, 0);
        const int tangentHi = std::min(i + 2, samples);
        glm::vec3 tangent = centers[static_cast<std::size_t>(tangentHi)]
            - centers[static_cast<std::size_t>(tangentLo)];
        if (glm::dot(tangent, tangent) < 1.0e-8f) tangent = glm::vec3(0.0f, 0.0f, 1.0f);
        else tangent = glm::normalize(tangent);

        if (i == 0) {
            transportedSide = glm::cross(tangent, glm::vec3(0.0f, 1.0f, 0.0f));
            if (glm::dot(transportedSide, transportedSide) < 1.0e-8f)
                transportedSide = glm::cross(tangent, glm::vec3(0.0f, 0.0f, 1.0f));
            transportedSide = glm::normalize(transportedSide);
        } else {
            glm::vec3 axis = glm::cross(previousTangent, tangent);
            const float sine = glm::length(axis);
            const float cosine = std::clamp(glm::dot(previousTangent, tangent), -1.0f, 1.0f);
            if (sine > 1.0e-6f) {
                axis /= sine;
                transportedSide = transportedSide * cosine
                    + glm::cross(axis, transportedSide) * sine
                    + axis * glm::dot(axis, transportedSide) * (1.0f - cosine);
            }
            transportedSide -= tangent * glm::dot(transportedSide, tangent);
            if (glm::dot(transportedSide, transportedSide) < 1.0e-8f)
                transportedSide = glm::cross(tangent, glm::vec3(0.0f, 1.0f, 0.0f));
            transportedSide = glm::normalize(transportedSide);
        }
        previousTangent = tangent;
        float rollDegrees = 0.0f;
        if (!splineObject->splinePointRotations.empty()) {
            const std::size_t count = splineObject->splinePointRotations.size();
            const float progress = static_cast<float>(i) / static_cast<float>(samples);
            const float keyed = progress * static_cast<float>(splineObject->splineClosed ? count : count - 1);
            const std::size_t lo = std::min(static_cast<std::size_t>(std::floor(keyed)), count - 1);
            const std::size_t hi = splineObject->splineClosed ? (lo + 1) % count : std::min(lo + 1, count - 1);
            float f = keyed - std::floor(keyed);
            f = f * f * (3.0f - 2.0f * f);
            const float a = splineObject->splinePointRotations[lo].z;
            rollDegrees = a + std::remainder(splineObject->splinePointRotations[hi].z - a, 360.0f) * f;
        }
        const float roll = glm::radians(rollDegrees);
        const glm::vec3 side = transportedSide * std::cos(roll)
            + glm::cross(tangent, transportedSide) * std::sin(roll);
        const glm::vec3 left = center + side * rowHalfWidths[static_cast<std::size_t>(i)];
        const glm::vec3 right = center - side * rowHalfWidths[static_cast<std::size_t>(i)];
        if (i == 0) {
            firstLeft = left;
            firstRight = right;
        } else {
            m_waterLines->AddLine(previousLeft, left, color);
            m_waterLines->AddLine(previousRight, right, color);
        }
        previousLeft = left;
        previousRight = right;
    }
    if (splineObject->splineClosed) {
        m_waterLines->AddLine(previousLeft, firstLeft, color);
        m_waterLines->AddLine(previousRight, firstRight, color);
    } else {
        m_waterLines->AddLine(firstLeft, firstRight, color);
        m_waterLines->AddLine(previousLeft, previousRight, color);
    }
    m_waterLines->Draw(viewProj, 3.0f, true);
}

void EditorViewport::DrawNavAgentGuides(engine::Renderer& renderer,
                                        engine::Shader& shader,
                                        const engine::Mesh& cube,
                                        const EditorScene& scene,
                                        const glm::mat4& viewProj) const {
    const EditorScene::Object* selected = scene.SelectedObject();
    if (!selected || !selected->navAgentEnabled) {
        return;
    }
    const engine::ecs::Transform* transform = scene.TryGetTransform(selected->entity);

    shader.Bind();
    shader.SetMat4("uViewProj", viewProj);
    shader.SetVec3("uLightDir", glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f)));
    shader.SetInt("uHasDiffuse", 0);

    // Patrol path: a marker at each waypoint plus looped connecting segments.
    const glm::vec3 pathColor(0.25f, 0.85f, 0.45f);
    shader.SetVec3("uEmissive", pathColor * 0.3f);
    const std::vector<glm::vec3>& waypoints = selected->patrolPoints;
    for (std::size_t i = 0; i < waypoints.size(); ++i) {
        DrawGizmoBox(renderer, shader, cube, waypoints[i], glm::vec3(0.12f), pathColor);
        if (waypoints.size() > 1) {
            const glm::vec3& next = waypoints[(i + 1) % waypoints.size()];
            DrawGuideSegment(renderer, shader, cube, waypoints[i], next, 0.03f, pathColor);
        }
    }

    // Vision cone: boundary rays out to range at +/- the half-angle around forward.
    if (transform && !selected->navAgentTargetName.empty() && selected->navAgentVisionRange > 0.0f) {
        const glm::vec3 coneColor(0.95f, 0.8f, 0.3f);
        shader.SetVec3("uEmissive", coneColor * 0.3f);
        const glm::vec3 eye = transform->position + glm::vec3(0.0f, 0.5f, 0.0f);
        // Skeletal characters use object-local -Z as forward (the same convention
        // as the cyan character-facing arrow and the runtime AI rotation).
        const glm::vec3 forward = glm::normalize(
            transform->rotation * glm::vec3(0.0f, 0.0f, -1.0f));
        const float range = selected->navAgentVisionRange;
        const float half = glm::radians(selected->navAgentVisionHalfAngle);
        const glm::vec3 up(0.0f, 1.0f, 0.0f);
        for (float side : {-1.0f, 1.0f}) {
            const glm::vec3 dir = glm::angleAxis(side * half, up) * forward;
            DrawGuideSegment(renderer, shader, cube, eye, eye + dir * range, 0.02f, coneColor);
        }
        DrawGuideSegment(renderer, shader, cube, eye, eye + forward * range, 0.015f, coneColor * 0.7f);
    }

    shader.SetVec3("uEmissive", glm::vec3(0.0f));
}

void EditorViewport::DrawNavMeshBoundsGuides(engine::Renderer& renderer,
                                             engine::Shader& shader,
                                             const engine::Mesh& cube,
                                             const EditorScene& scene,
                                             const glm::mat4& viewProj) const {
    const EditorScene::Object* selected = scene.SelectedObject();
    shader.Bind();
    shader.SetMat4("uViewProj", viewProj);
    shader.SetVec3("uLightDir", glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f)));
    shader.SetInt("uHasDiffuse", 0);

    constexpr int edges[][2] = {
        {0,1},{1,3},{3,2},{2,0}, {4,5},{5,7},{7,6},{6,4},
        {0,4},{1,5},{2,6},{3,7}
    };
    for (const EditorScene::Object& object : scene.Objects()) {
        if (!object.navMeshBoundsVolume || !object.visible) continue;
        const engine::ecs::Transform* transform = scene.TryGetTransform(object.entity);
        if (!transform) continue;
        const bool isSelected = selected && selected->entity == object.entity;
        const glm::vec3 color = isSelected ? glm::vec3(0.15f, 0.82f, 1.0f)
                                           : glm::vec3(0.10f, 0.48f, 0.76f);
        shader.SetVec3("uEmissive", color * (isSelected ? 0.65f : 0.28f));
        glm::vec3 corners[8];
        const glm::mat4 model = transform->Model();
        for (int i = 0; i < 8; ++i) {
            const glm::vec3 local((i & 1) ? 0.5f : -0.5f,
                                  (i & 4) ? 0.5f : -0.5f,
                                  (i & 2) ? 0.5f : -0.5f);
            corners[i] = glm::vec3(model * glm::vec4(local, 1.0f));
        }
        for (const auto& edge : edges) {
            DrawGuideSegment(renderer, shader, cube, corners[edge[0]], corners[edge[1]],
                isSelected ? 0.045f : 0.028f, color);
        }
    }
    shader.SetVec3("uEmissive", glm::vec3(0.0f));
}

void EditorViewport::DrawAudioSourceGuides(engine::Renderer& renderer,
                                           engine::Shader& shader,
                                           const engine::Mesh& cube,
                                           const EditorScene& scene,
                                           const glm::mat4& viewProj) const {
    const EditorScene::Object* selected = scene.SelectedObject();
    if (!selected || !selected->audioSourceEnabled || !selected->audioSpatial || !selected->visible) return;
    const engine::ecs::Transform* transform = scene.TryGetTransform(selected->entity);
    if (!transform) return;
    shader.Bind();
    shader.SetMat4("uViewProj", viewProj);
    shader.SetVec3("uLightDir", glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f)));
    shader.SetInt("uHasDiffuse", 0);
    const glm::vec3 innerColor(0.25f, 0.85f, 1.0f);
    const glm::vec3 outerColor(0.12f, 0.48f, 0.88f);
    shader.SetVec3("uEmissive", innerColor * 0.5f);
    DrawGizmoBox(renderer, shader, cube, transform->position, glm::vec3(0.16f), innerColor);
    DrawGuideRing(renderer, shader, cube, transform->position, EditorGizmo::Axis::Y,
        std::max(selected->audioMinDistance, 0.01f), 0.035f, innerColor);
    shader.SetVec3("uEmissive", outerColor * 0.3f);
    DrawGuideRing(renderer, shader, cube, transform->position, EditorGizmo::Axis::Y,
        std::max(selected->audioMaxDistance, selected->audioMinDistance), 0.025f, outerColor);
    shader.SetVec3("uEmissive", glm::vec3(0.0f));
}

void EditorViewport::DrawParticleSystemGuides(const EditorScene& scene,
                                               const glm::mat4& viewProj,
                                               bool selectedOnly,
                                               bool showShapes,
                                               bool showDirections,
                                               bool showBounds,
                                               bool showCullingState) const {
    const EditorScene::Object* selected = scene.SelectedObject();
    if (selectedOnly && (!selected || !selected->particleSystemEnabled)) return;
    if (!m_colliderLines) return;
    m_colliderLines->Clear();
    const glm::vec3 shapeColor(1.0f, 0.48f, 0.12f);
    const glm::vec3 cullingColor(0.15f, 0.85f, 1.0f);
    const glm::vec3 uncullableColor(0.72f, 0.32f, 0.92f);
    for (const EditorScene::Object& object : scene.Objects()) {
        if (!object.particleSystemEnabled) continue;
        if (selectedOnly && &object != selected) continue;
        const engine::ecs::Transform* transform = scene.TryGetTransform(object.entity);
        if (!transform) continue;

        const glm::vec3 center = transform->position;
        const glm::vec3 boundsColor = object.particleConfig.cullingEnabled
            ? cullingColor : uncullableColor;
        if (showBounds) {
            const float bounds = std::max(object.particleConfig.boundsRadius, 0.01f);
            DrawBasisRing(*m_colliderLines, center, glm::vec3(0, 1, 0),
                glm::vec3(0, 0, 1), bounds, boundsColor);
            DrawBasisRing(*m_colliderLines, center, glm::vec3(1, 0, 0),
                glm::vec3(0, 0, 1), bounds, boundsColor);
            DrawBasisRing(*m_colliderLines, center, glm::vec3(1, 0, 0),
                glm::vec3(0, 1, 0), bounds, boundsColor);
        }
        if (showCullingState) {
            constexpr float crossSize = 0.12f;
            m_colliderLines->AddLine(center - glm::vec3(crossSize, 0, 0),
                center + glm::vec3(crossSize, 0, 0), boundsColor);
            m_colliderLines->AddLine(center - glm::vec3(0, crossSize, 0),
                center + glm::vec3(0, crossSize, 0), boundsColor);
            m_colliderLines->AddLine(center - glm::vec3(0, 0, crossSize),
                center + glm::vec3(0, 0, crossSize), boundsColor);
        }

        const float radius = std::max(object.particleConfig.shapeRadius, 0.08f);
        if (showShapes) {
            if (object.particleConfig.shape == engine::EmitShape::Sphere) {
                DrawBasisRing(*m_colliderLines, center, glm::vec3(0, 1, 0),
                    glm::vec3(0, 0, 1), radius, shapeColor);
                DrawBasisRing(*m_colliderLines, center, glm::vec3(1, 0, 0),
                    glm::vec3(0, 0, 1), radius, shapeColor);
                DrawBasisRing(*m_colliderLines, center, glm::vec3(1, 0, 0),
                    glm::vec3(0, 1, 0), radius, shapeColor);
            } else {
                DrawBasisRing(*m_colliderLines, center, glm::vec3(1, 0, 0),
                    glm::vec3(0, 0, 1), radius, shapeColor);
            }
        }
        if (showDirections) {
            glm::vec3 direction = object.particleConfig.direction;
            if (glm::dot(direction, direction) < 1.0e-6f) direction = glm::vec3(0, 1, 0);
            direction = glm::normalize(direction);
            const float length = object.particleConfig.shape == engine::EmitShape::Cone ? 1.5f : 0.8f;
            const glm::vec3 tip = center + direction * length;
            m_colliderLines->AddLine(center, tip, shapeColor);
            glm::vec3 arrowRight;
            glm::vec3 arrowUp;
            BuildBasis(direction, &arrowRight, &arrowUp);
            const float head = std::min(length * 0.18f, 0.20f);
            m_colliderLines->AddLine(tip,
                tip - direction * head + arrowRight * head * 0.45f, shapeColor);
            m_colliderLines->AddLine(tip,
                tip - direction * head - arrowRight * head * 0.45f, shapeColor);
        }
    }
    m_colliderLines->Draw(viewProj, 1.5f, true);
}

void EditorViewport::DrawAiAgentDebugGuides(engine::Renderer& renderer,
                                            engine::Shader& shader,
                                            const engine::Mesh& cube,
                                            const std::vector<AiAgentGuide>& guides,
                                            const glm::mat4& viewProj) const {
    if (guides.empty()) {
        return;
    }

    shader.Bind();
    shader.SetMat4("uViewProj", viewProj);
    shader.SetVec3("uLightDir", glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f)));
    shader.SetInt("uHasDiffuse", 0);

    for (const AiAgentGuide& guide : guides) {
        const glm::vec3 color = (guide.state == 1) ? glm::vec3(0.9f, 0.25f, 0.2f)     // chase = red
                              : (guide.state == 2) ? glm::vec3(0.95f, 0.8f, 0.25f)    // search = amber
                                                   : glm::vec3(0.25f, 0.85f, 0.45f);  // patrol = green

        // Floating state marker above the agent, brighter when it sees the target.
        shader.SetVec3("uEmissive", color * (guide.seesTarget ? 0.7f : 0.35f));
        DrawGizmoBox(renderer, shader, cube, guide.position + glm::vec3(0.0f, 1.4f, 0.0f),
                     glm::vec3(guide.seesTarget ? 0.2f : 0.15f), color);

        // Live path (chase/search corridor) drawn as segments + a goal marker.
        if (guide.path.size() > 1) {
            shader.SetVec3("uEmissive", color * 0.25f);
            for (std::size_t i = 0; i + 1 < guide.path.size(); ++i) {
                DrawGuideSegment(renderer, shader, cube, guide.path[i], guide.path[i + 1], 0.025f, color);
            }
            DrawGizmoBox(renderer, shader, cube, guide.path.back(), glm::vec3(0.1f), color);
        }

        // Vision cone on the ground: two edge rays + a closing arc. Warmer/brighter when
        // the agent currently sees the target, cool blue otherwise.
        if (guide.visionRange > 0.05f && guide.visionHalfAngleDeg > 0.5f) {
            glm::vec3 forward(guide.facing.x, 0.0f, guide.facing.z);
            if (glm::dot(forward, forward) < 1e-5f) forward = glm::vec3(0.0f, 0.0f, 1.0f);
            forward = glm::normalize(forward);
            const glm::vec3 up(0.0f, 1.0f, 0.0f);
            const glm::vec3 base = guide.position + glm::vec3(0.0f, 0.05f, 0.0f);
            const glm::vec3 visionColor = guide.seesTarget
                ? glm::vec3(1.0f, 0.85f, 0.30f) : glm::vec3(0.35f, 0.70f, 1.0f);
            shader.SetVec3("uEmissive", visionColor * (guide.seesTarget ? 0.50f : 0.28f));
            const float half = glm::radians(guide.visionHalfAngleDeg);
            const glm::vec3 edgeL = glm::angleAxis(half, up) * forward;
            const glm::vec3 edgeR = glm::angleAxis(-half, up) * forward;
            DrawGuideSegment(renderer, shader, cube, base, base + edgeL * guide.visionRange, 0.02f, visionColor);
            DrawGuideSegment(renderer, shader, cube, base, base + edgeR * guide.visionRange, 0.02f, visionColor);
            constexpr int arcSegments = 12;
            glm::vec3 previous = base + edgeR * guide.visionRange;
            for (int i = 1; i <= arcSegments; ++i) {
                const float t = static_cast<float>(i) / static_cast<float>(arcSegments);
                const glm::vec3 dir = glm::angleAxis(-half + t * 2.0f * half, up) * forward;
                const glm::vec3 point = base + dir * guide.visionRange;
                DrawGuideSegment(renderer, shader, cube, previous, point, 0.02f, visionColor);
                previous = point;
            }
        }

        // Line of sight to the pursued target: green when visible, red when blocked.
        if (guide.hasTarget) {
            const glm::vec3 losColor = guide.seesTarget
                ? glm::vec3(0.30f, 0.95f, 0.40f) : glm::vec3(0.95f, 0.30f, 0.25f);
            shader.SetVec3("uEmissive", losColor * 0.45f);
            DrawGuideSegment(renderer, shader, cube,
                             guide.position + glm::vec3(0.0f, 0.6f, 0.0f),
                             guide.targetPosition + glm::vec3(0.0f, 0.6f, 0.0f),
                             0.02f, losColor);
        }
    }

    shader.SetVec3("uEmissive", glm::vec3(0.0f));
}

void EditorViewport::DrawNavGridOverlay(engine::Renderer& renderer,
                                        engine::Shader& shader,
                                        const engine::Mesh& cube,
                                        const engine::ai::NavGrid& grid,
                                        const glm::mat4& viewProj) const {
    if (grid.width <= 0 || grid.height <= 0) {
        return;
    }

    shader.Bind();
    shader.SetMat4("uViewProj", viewProj);
    shader.SetVec3("uLightDir", glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f)));
    shader.SetInt("uHasDiffuse", 0);

    const glm::vec3 blockedColor(0.85f, 0.30f, 0.28f);
    shader.SetVec3("uEmissive", blockedColor * 0.22f);
    const float marker = grid.cellSize * 0.42f;
    constexpr int kMaxCells = 4000;   // keep the overlay cheap on large grids
    int drawn = 0;
    for (int y = 0; y < grid.height && drawn < kMaxCells; ++y) {
        for (int x = 0; x < grid.width && drawn < kMaxCells; ++x) {
            if (grid.Walkable(x, y)) {
                continue;   // draw only the blocked cells pathfinding avoids
            }
            glm::vec3 p = grid.CellToWorld(x, y);
            p.y += 0.03f;
            DrawGizmoBox(renderer, shader, cube, p, glm::vec3(marker, 0.02f, marker), blockedColor);
            ++drawn;
        }
    }

    shader.SetVec3("uEmissive", glm::vec3(0.0f));
}

void EditorViewport::DrawNavMeshOverlay(engine::Renderer& renderer,
                                        engine::Shader& shader,
                                        const engine::Mesh& cube,
                                        const engine::ai::NavMesh& mesh,
                                        const glm::mat4& viewProj) const {
    if (mesh.polys.empty()) {
        return;
    }

    shader.Bind();
    shader.SetMat4("uViewProj", viewProj);
    shader.SetVec3("uLightDir", glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f)));
    shader.SetInt("uHasDiffuse", 0);

    const glm::vec3 edgeColor(0.30f, 0.72f, 0.92f);   // walkable polygon outlines
    shader.SetVec3("uEmissive", edgeColor * 0.28f);

    constexpr int kMaxPolys = 2000;   // keep the overlay cheap on large meshes
    int drawn = 0;
    for (const engine::ai::NavMesh::Poly& poly : mesh.polys) {
        if (drawn >= kMaxPolys) {
            break;
        }
        const std::size_t n = poly.verts.size();
        if (n < 2) {
            continue;
        }
        for (std::size_t i = 0; i < n; ++i) {
            const glm::vec3 a = mesh.vertices[poly.verts[i]] + glm::vec3(0.0f, 0.04f, 0.0f);
            const glm::vec3 b = mesh.vertices[poly.verts[(i + 1) % n]] + glm::vec3(0.0f, 0.04f, 0.0f);
            DrawGuideSegment(renderer, shader, cube, a, b, 0.02f, edgeColor);
        }
        ++drawn;
    }

    shader.SetVec3("uEmissive", glm::vec3(0.0f));
}

void EditorViewport::DrawEditorNavMeshOverlay(engine::Renderer& renderer,
                                               engine::Shader& shader,
                                               const engine::Mesh& cube,
                                               const engine::ai::NavMesh& mesh,
                                               const glm::mat4& viewProj) const {
    if (mesh.polys.empty()) return;
    shader.Bind();
    shader.SetMat4("uViewProj", viewProj);
    shader.SetVec3("uLightDir", glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f)));
    shader.SetInt("uHasDiffuse", 0);
    const glm::vec3 surfaceColor(0.18f, 0.72f, 0.24f);
    const glm::vec3 edgeColor(0.34f, 1.0f, 0.42f);
    constexpr float tileSpacing = 1.0f;
    constexpr int maxTiles = 4000;
    int tiles = 0;

    for (const engine::ai::NavMesh::Poly& poly : mesh.polys) {
        if (poly.verts.size() < 3) continue;
        glm::vec3 minimum(1.0e9f), maximum(-1.0e9f);
        for (int vertex : poly.verts) {
            if (vertex < 0 || vertex >= static_cast<int>(mesh.vertices.size())) continue;
            minimum = glm::min(minimum, mesh.vertices[static_cast<std::size_t>(vertex)]);
            maximum = glm::max(maximum, mesh.vertices[static_cast<std::size_t>(vertex)]);
        }
        const float width = maximum.x - minimum.x;
        const float depth = maximum.z - minimum.z;
        if (width <= 0.001f || depth <= 0.001f) continue;
        const float y = minimum.y + 0.035f;
        shader.SetVec3("uEmissive", surfaceColor * 0.38f);
        const int columns = std::max(1, static_cast<int>(std::ceil(width / tileSpacing)));
        const int rows = std::max(1, static_cast<int>(std::ceil(depth / tileSpacing)));
        const float cellWidth = width / columns;
        const float cellDepth = depth / rows;
        for (int z = 0; z < rows && tiles < maxTiles; ++z) {
            for (int x = 0; x < columns && tiles < maxTiles; ++x) {
                const glm::vec3 center(minimum.x + (x + 0.5f) * cellWidth, y,
                    minimum.z + (z + 0.5f) * cellDepth);
                DrawGizmoBox(renderer, shader, cube, center,
                    glm::vec3(cellWidth * 0.82f, 0.018f, cellDepth * 0.82f), surfaceColor);
                ++tiles;
            }
        }
        shader.SetVec3("uEmissive", edgeColor * 0.55f);
        for (std::size_t i = 0; i < poly.verts.size(); ++i) {
            const glm::vec3 a = mesh.vertices[poly.verts[i]] + glm::vec3(0.0f, 0.055f, 0.0f);
            const glm::vec3 b = mesh.vertices[poly.verts[(i + 1) % poly.verts.size()]]
                + glm::vec3(0.0f, 0.055f, 0.0f);
            DrawGuideSegment(renderer, shader, cube, a, b, 0.025f, edgeColor);
        }
    }
    shader.SetVec3("uEmissive", glm::vec3(0.0f));
}

void EditorViewport::DrawSelectedModelOutline(engine::Renderer& renderer,
                                              engine::Shader& shader,
                                              const engine::ecs::Transform& transform,
                                              const engine::Model& model,
                                              const glm::vec3& color,
                                              float thickness) const {
    DrawStencilSelectionOutline(shader, transform, color, thickness, glm::mat4(1.0f), [&]() {
        for (const engine::SubMesh& subMesh : model.SubMeshes()) renderer.Draw(subMesh.mesh);
    });
}

void EditorViewport::DrawSelectedMeshOutline(engine::Renderer& renderer,
                                             engine::Shader& shader,
                                             const engine::ecs::Transform& transform,
                                             const engine::Mesh& mesh,
                                             const glm::vec3& color,
                                             float thickness) const {
    DrawStencilSelectionOutline(shader, transform, color, thickness, glm::mat4(1.0f),
                                [&]() { renderer.Draw(mesh); });
}

void EditorViewport::DrawSelectedSkinnedModelOutline(engine::Renderer& renderer,
                                                      engine::Shader& shader,
                                                      const engine::ecs::Transform& transform,
                                                      const engine::SkinnedModel& model,
                                                      const std::vector<glm::mat4>& pose,
                                                      const glm::vec3& color,
                                                      float thickness,
                                                      const glm::mat4& modelOffset) const {
    const std::size_t boneCount = std::min<std::size_t>(pose.size(), engine::SkinnedModel::kMaxBones);
    for (std::size_t i = 0; i < boneCount; ++i) {
        shader.SetMat4("uBones[" + std::to_string(i) + "]", pose[i]);
    }
    DrawStencilSelectionOutline(shader, transform, color, thickness, modelOffset, [&]() {
        for (const engine::SubMesh& subMesh : model.SubMeshes()) renderer.Draw(subMesh.mesh);
    });
}

void EditorViewport::DrawLoadedModel(engine::Shader& shader,
                                     const engine::ecs::Transform& transform,
                                     const engine::Model& model) const {
    const glm::mat4 modelMatrix = transform.Model();
    shader.SetMat4("uModel", modelMatrix);
    shader.SetMat3("uNormalMat", glm::mat3(glm::transpose(glm::inverse(modelMatrix))));
    engine::DrawModel(model, shader);
}

void EditorViewport::DrawSceneObject(engine::Renderer& renderer,
                                     engine::Shader& shader,
                                     const engine::ecs::Transform& transform,
                                     const engine::ecs::MeshRenderer& meshRenderer,
                                     const engine::Texture* diffuseTexture,
                                     const glm::vec3& emissive) const {
    if (!meshRenderer.mesh) {
        return;
    }

    shader.SetMat4("uModel", transform.Model());
    shader.SetVec3("uColor", meshRenderer.color);
    shader.SetVec3("uEmissive", emissive);
    shader.SetInt("uHasDiffuse", diffuseTexture ? 1 : 0);
    if (diffuseTexture) {
        diffuseTexture->Bind(0);
        shader.SetInt("uDiffuseTex", 0);
    }
    renderer.Draw(*meshRenderer.mesh);
}

bool EditorViewport::ProjectWorldToScreen(const glm::vec3& world,
                                          const glm::mat4& viewProj,
                                          int width,
                                          int height,
                                          glm::vec2* screen) const {
    const glm::vec4 clip = viewProj * glm::vec4(world, 1.0f);
    if (clip.w <= 0.0001f) {
        return false;
    }

    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    if (ndc.z < -1.0f || ndc.z > 1.0f) {
        return false;
    }

    if (screen) {
        screen->x = (ndc.x * 0.5f + 0.5f) * static_cast<float>(width);
        screen->y = (1.0f - (ndc.y * 0.5f + 0.5f)) * static_cast<float>(height);
    }
    return true;
}

int EditorViewport::PickSceneObject(const EditorScene& scene,
                                    const engine::RuntimeAssetManager& assets,
                                    float x,
                                    float y,
                                    const glm::mat4& viewProj,
                                    int width,
                                    int height,
                                    glm::vec3* hitPosition,
                                    glm::vec3* hitNormal,
                                    const char* ignoredNamePrefix) const {
    PickRay ray;
    if (!BuildPickRay(x, y, viewProj, width, height, &ray)) {
        return -1;
    }

    int picked = -1;
    float bestDistance = std::numeric_limits<float>::max();
    glm::vec3 bestPosition(0.0f);
    glm::vec3 bestNormal(0.0f, 1.0f, 0.0f);

    const std::vector<EditorScene::Object>& objects = scene.Objects();
    for (int i = 0; i < static_cast<int>(objects.size()); ++i) {
        const EditorScene::Object& object = objects[static_cast<std::size_t>(i)];
        if (ignoredNamePrefix && object.name.rfind(ignoredNamePrefix, 0) == 0) continue;
        // Large authoring volumes surround normal level geometry; treating their
        // full AABB as solid picking geometry would block selection inside them.
        if (!object.visible || object.navMeshBoundsVolume
            || object.primitive == EditorScene::Primitive::Empty) {
            continue;
        }

        if (object.isSpline) {
            // Splines are screen-space editor guides, not solid placement surfaces.
            if (hitPosition) continue;
            engine::Spline spline(object.splinePoints, object.splineClosed);
            std::vector<glm::vec3> points;
            spline.SampleUniform(std::max(24, static_cast<int>(object.splinePoints.size()) * 12), points);
            float bestScreenSq = 8.0f * 8.0f;
            float curveDistance = std::numeric_limits<float>::max();
            glm::vec2 previous;
            bool hasPrevious = false;
            for (const glm::vec3& point : points) {
                glm::vec2 screen;
                if (!ProjectWorldToScreen(point, viewProj, width, height, &screen)) {
                    hasPrevious = false;
                    continue;
                }
                if (hasPrevious) {
                    const float screenSq = DistanceToSegmentSquared(glm::vec2(x, y), previous, screen);
                    if (screenSq <= bestScreenSq) {
                        bestScreenSq = screenSq;
                        curveDistance = std::min(curveDistance, glm::length(point - ray.origin));
                    }
                }
                previous = screen;
                hasPrevious = true;
            }
            if (curveDistance < bestDistance) {
                bestDistance = curveDistance;
                picked = i;
            }
            continue;
        }

        if (object.isWater && !object.waterFlowSpline.empty()) {
            // A river ribbon has no simple authored AABB surface. Fall through to the
            // ground plane for modular placement instead of returning an invalid point.
            if (hitPosition) continue;
            const EditorScene::Object* flow = nullptr;
            for (const EditorScene::Object& candidate : objects) {
                if (candidate.isSpline && candidate.name == object.waterFlowSpline
                    && candidate.splinePoints.size() >= 2) {
                    flow = &candidate;
                    break;
                }
            }
            if (flow) {
                const engine::Spline spline(flow->splinePoints, flow->splineClosed);
                const int samples = std::clamp(
                    std::max(24, static_cast<int>(flow->splinePoints.size()) * 16), 24, 320);
                const float length = spline.Length();
                glm::vec2 previousScreen;
                float previousHalfWidth = 8.0f;
                bool hasPrevious = false;
                float riverDistance = std::numeric_limits<float>::max();
                for (int sample = 0; sample <= samples; ++sample) {
                    const float distance = length * static_cast<float>(sample)
                        / static_cast<float>(samples);
                    const glm::vec3 center = spline.PositionAtDistance(distance);
                    glm::vec3 tangent = spline.TangentAtDistance(distance);
                    tangent.y = 0.0f;
                    if (glm::dot(tangent, tangent) < 1.0e-8f) tangent = glm::vec3(0, 0, 1);
                    else tangent = glm::normalize(tangent);
                    const glm::vec3 side(tangent.z, 0.0f, -tangent.x);
                    glm::vec2 centerScreen, edgeScreen;
                    if (!ProjectWorldToScreen(center, viewProj, width, height, &centerScreen)
                        || !ProjectWorldToScreen(center + side * object.waterRiverWidth * 0.5f,
                                                 viewProj, width, height, &edgeScreen)) {
                        hasPrevious = false;
                        continue;
                    }
                    const float halfWidthPixels = std::max(glm::length(edgeScreen - centerScreen), 8.0f);
                    if (hasPrevious) {
                        const float threshold = std::max(previousHalfWidth, halfWidthPixels);
                        if (DistanceToSegmentSquared(glm::vec2(x, y), previousScreen, centerScreen)
                            <= threshold * threshold) {
                            riverDistance = std::min(riverDistance, glm::length(center - ray.origin));
                        }
                    }
                    previousScreen = centerScreen;
                    previousHalfWidth = halfWidthPixels;
                    hasPrevious = true;
                }
                if (riverDistance < bestDistance) {
                    bestDistance = riverDistance;
                    picked = i;
                }
                continue;
            }
        }

        const engine::ecs::Transform* transform = scene.TryGetTransform(object.entity);
        if (!transform) {
            continue;
        }

        const glm::mat4 inverseModel = glm::inverse(transform->Model());
        const PickRay localRay = TransformRayToLocal(ray, inverseModel);
        float hitDistance = 0.0f;
        bool hit = false;
        glm::mat4 hitModel = transform->Model();
        PickRay hitRay = localRay;
        glm::vec3 hitMinimum(-0.5f);
        glm::vec3 hitMaximum(0.5f);
        bool planeHit = false;
        if (!object.modelAssetPath.empty()) {
            if (object.skeletalModel) {
                // A skeletal character is drawn at Transform * renderOffset (render-only
                // stand-up + re-centre on the origin). Pick against that same space using
                // the skinned model's real bounds -- FindModel returns null for a skinned
                // asset, so the old path fell back to a tiny box that (at the character's
                // ~0.009 scale) was effectively impossible to click.
                const engine::SkinnedModel* skinned = assets.FindSkinnedModel(object.modelAssetPath);
                const glm::mat4 offset = skinned
                    ? engine::MakeModelRenderOffset(object.modelOffsetPosition,
                          object.modelOrientationEuler, object.modelOffsetScale, skinned->Center())
                    : glm::mat4(1.0f);
                const PickRay skinnedRay =
                    TransformRayToLocal(ray, glm::inverse(transform->Model() * offset));
                hitModel = transform->Model() * offset;
                hitRay = skinnedRay;
                if (skinned) {
                    hitMinimum = skinned->Min();
                    hitMaximum = skinned->Max();
                    hit = IntersectLocalAabb(skinnedRay, skinned->Min(), skinned->Max(), &hitDistance);
                } else {
                    hit = IntersectLocalAabb(skinnedRay, glm::vec3(-0.5f), glm::vec3(0.5f), &hitDistance);
                }
            } else {
                const engine::Model* model = assets.FindModel(object.modelAssetPath);
                if (model) {
                    hitMinimum = model->Min();
                    hitMaximum = model->Max();
                    hit = IntersectLocalAabb(localRay, model->Min(), model->Max(), &hitDistance);
                } else {
                    hit = IntersectLocalAabb(localRay, glm::vec3(-0.5f), glm::vec3(0.5f), &hitDistance);
                }
            }
        } else if (object.isTerrain) {
            // Terrain renders as a generated mesh spanning local [0,size] in X/Z (not the
            // object's unit plane quad), so pick against that footprint or you can't click
            // the terrain to select it (and reach the sculpt controls).
            const float s = std::max(object.terrainSize, 0.01f);
            const float h = std::max(object.terrainMaxHeight, 0.5f);
            hitMinimum = glm::vec3(0.0f, -0.5f, 0.0f);
            hitMaximum = glm::vec3(s, h + 0.5f, s);
            hit = IntersectLocalAabb(localRay, glm::vec3(0.0f, -0.5f, 0.0f),
                                     glm::vec3(s, h + 0.5f, s), &hitDistance);
        } else {
            switch (object.primitive) {
            case EditorScene::Primitive::Plane:
                hit = IntersectLocalPlaneQuad(localRay, &hitDistance);
                planeHit = true;
                break;
            case EditorScene::Primitive::Cube:
                hit = IntersectLocalAabb(localRay, glm::vec3(-0.5f), glm::vec3(0.5f), &hitDistance);
                break;
            case EditorScene::Primitive::Sphere:
                hit = IntersectLocalAabb(localRay, glm::vec3(-0.5f), glm::vec3(0.5f), &hitDistance);
                break;
            case EditorScene::Primitive::Capsule:
                hit = IntersectLocalAabb(localRay, glm::vec3(-0.4f, -0.9f, -0.4f),
                    glm::vec3(0.4f, 0.9f, 0.4f), &hitDistance);
                break;
            case EditorScene::Primitive::Cylinder:
            case EditorScene::Primitive::Cone:
            case EditorScene::Primitive::Pyramid:
            case EditorScene::Primitive::Torus:
            case EditorScene::Primitive::Staircase:
                hit = IntersectLocalAabb(localRay, glm::vec3(-0.5f), glm::vec3(0.5f), &hitDistance);
                break;
            }
        }

        if (hit && hitDistance < bestDistance) {
            bestDistance = hitDistance;
            picked = i;
            const glm::vec3 localPosition = hitRay.origin + hitRay.direction * hitDistance;
            bestPosition = glm::vec3(hitModel * glm::vec4(localPosition, 1.0f));
            glm::vec3 localNormal(0.0f, 1.0f, 0.0f);
            if (!planeHit) {
                float nearest = std::numeric_limits<float>::max();
                for (int axis = 0; axis < 3; ++axis) {
                    const float toMinimum = std::abs(localPosition[axis] - hitMinimum[axis]);
                    if (toMinimum < nearest) {
                        nearest = toMinimum;
                        localNormal = glm::vec3(0.0f);
                        localNormal[axis] = -1.0f;
                    }
                    const float toMaximum = std::abs(localPosition[axis] - hitMaximum[axis]);
                    if (toMaximum < nearest) {
                        nearest = toMaximum;
                        localNormal = glm::vec3(0.0f);
                        localNormal[axis] = 1.0f;
                    }
                }
            }
            const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(hitModel)));
            bestNormal = glm::normalize(normalMatrix * localNormal);
        }
    }

    if (picked >= 0) {
        if (hitPosition) *hitPosition = bestPosition;
        if (hitNormal) *hitNormal = bestNormal;
    }
    return picked;
}

int EditorViewport::PickSplinePoint(const EditorScene& scene,
                                    float x, float y,
                                    const glm::mat4& viewProj,
                                    int width, int height) const {
    const EditorScene::Object* selected = scene.SelectedObject();
    if (!selected || !selected->isSpline || selected->locked) return -1;
    const glm::vec2 mouse(x, y);
    int best = -1;
    float bestSq = 13.0f * 13.0f;
    for (std::size_t i = 0; i < selected->splinePoints.size(); ++i) {
        glm::vec2 screen;
        if (!ProjectWorldToScreen(selected->splinePoints[i], viewProj, width, height, &screen)) continue;
        const glm::vec2 delta = screen - mouse;
        const float distanceSq = glm::dot(delta, delta);
        if (distanceSq <= bestSq) {
            bestSq = distanceSq;
            best = static_cast<int>(i);
        }
    }
    return best;
}

bool EditorViewport::PickGizmoHandle(EditorGizmo& gizmo,
                                     const EditorScene& scene,
                                     float x,
                                     float y,
                                     const glm::mat4& viewProj,
                                     int width,
                                     int height,
                                     const engine::Camera& camera,
                                     const glm::vec3* pivotOverride) const {
    const EditorScene::Object* selectedObject = scene.SelectedObject();
    const engine::ecs::Transform* selectedTransform = selectedObject
        ? scene.TryGetTransform(selectedObject->entity)
        : nullptr;
    if (!selectedTransform || scene.SelectedLocked()) {
        return false;
    }

    engine::ecs::Transform gizmoTransform = *selectedTransform;
    constexpr float pi = 3.14159265359f;
    const glm::vec2 mouse{x, y};
    // Match the drawn gizmo: terrain's gizmo sits at its footprint centre, not the corner.
    const glm::vec3 center = pivotOverride ? *pivotOverride : selectedTransform->position
        + (selectedObject && selectedObject->isTerrain
               ? glm::vec3(selectedObject->terrainSize * 0.5f, 0.0f, selectedObject->terrainSize * 0.5f)
               : glm::vec3(0.0f));
    gizmoTransform.position = center;
    const float gizmoScale = GizmoWorldScale(gizmoTransform, camera, height) * gizmo.VisualScale();
    const float length = gizmoScale * 0.78f;
    const float head = gizmoScale * 0.16f;
    const float rotateRadius = gizmoScale * 0.72f;
    glm::vec2 centerScreen;
    if (!ProjectWorldToScreen(center, viewProj, width, height, &centerScreen)) {
        return false;
    }

    auto testAxisSegment = [&](EditorGizmo::Axis axis, float axisLength, float maxDistance) {
        glm::vec2 endScreen;
        glm::vec3 worldAxis = AxisVector(axis);
        if (gizmo.CurrentSpace() == EditorGizmo::Space::Local
            || gizmo.CurrentMode() == EditorGizmo::Mode::Scale) {
            worldAxis = glm::mat3_cast(selectedTransform->rotation) * worldAxis;
        }
        if (!ProjectWorldToScreen(center + worldAxis * axisLength, viewProj, width, height, &endScreen)) {
            return false;
        }

        return DistanceToSegmentSquared(mouse, centerScreen, endScreen) <= maxDistance * maxDistance;
    };

    switch (gizmo.CurrentMode()) {
    case EditorGizmo::Mode::Translate:
        if (testAxisSegment(EditorGizmo::Axis::X, length + head * 0.55f, 12.0f)) {
            gizmo.SetAxis(EditorGizmo::Axis::X);
            return true;
        }
        if (testAxisSegment(EditorGizmo::Axis::Y, length + head * 0.55f, 12.0f)) {
            gizmo.SetAxis(EditorGizmo::Axis::Y);
            return true;
        }
        if (testAxisSegment(EditorGizmo::Axis::Z, length + head * 0.55f, 12.0f)) {
            gizmo.SetAxis(EditorGizmo::Axis::Z);
            return true;
        }
        break;

    case EditorGizmo::Mode::Scale:
        if (glm::dot(mouse - centerScreen, mouse - centerScreen) <= 14.0f * 14.0f) {
            gizmo.SetAxis(EditorGizmo::Axis::All);
            return true;
        }
        if (testAxisSegment(EditorGizmo::Axis::X, length, 12.0f)) {
            gizmo.SetAxis(EditorGizmo::Axis::X);
            return true;
        }
        if (testAxisSegment(EditorGizmo::Axis::Y, length, 12.0f)) {
            gizmo.SetAxis(EditorGizmo::Axis::Y);
            return true;
        }
        if (testAxisSegment(EditorGizmo::Axis::Z, length, 12.0f)) {
            gizmo.SetAxis(EditorGizmo::Axis::Z);
            return true;
        }
        break;

    case EditorGizmo::Mode::Rotate:
        {
            const EditorGizmo::Axis axes[] = {
                EditorGizmo::Axis::X,
                EditorGizmo::Axis::Y,
                EditorGizmo::Axis::Z
            };
            for (EditorGizmo::Axis axis : axes) {
                float bestDistanceSquared = std::numeric_limits<float>::max();
                glm::vec2 previousScreen;
                bool hasPrevious = false;
                for (int i = 0; i <= 40; ++i) {
                    const float t = (static_cast<float>(i) / 40.0f) * 2.0f * pi;
                    glm::vec2 screen;
                    const glm::mat3 ringBasis = gizmo.CurrentSpace() == EditorGizmo::Space::Local
                        ? glm::mat3_cast(selectedTransform->rotation) : glm::mat3(1.0f);
                    if (!ProjectWorldToScreen(center + ringBasis * RingOffset(axis, t, rotateRadius),
                            viewProj, width, height, &screen)) {
                        hasPrevious = false;
                        continue;
                    }
                    if (hasPrevious) {
                        bestDistanceSquared = std::min(bestDistanceSquared,
                            DistanceToSegmentSquared(mouse, previousScreen, screen));
                    }
                    previousScreen = screen;
                    hasPrevious = true;
                }

                if (bestDistanceSquared <= 16.0f * 16.0f) {
                    gizmo.SetAxis(axis);
                    return true;
                }
            }
        }
        break;
    }

    return false;
}

glm::vec3 EditorViewport::SceneDropPosition(float x,
                                            float y,
                                            const glm::mat4& viewProj,
                                            int width,
                                            int height) const {
    PickRay ray;
    if (!BuildPickRay(x, y, viewProj, width, height, &ray)) {
        return glm::vec3(0.0f);
    }

    if (std::abs(ray.direction.y) > 0.0001f) {
        const float t = -ray.origin.y / ray.direction.y;
        if (t > 0.0f) {
            return ray.origin + ray.direction * t;
        }
    }

    return ray.origin + ray.direction * 6.0f;
}
