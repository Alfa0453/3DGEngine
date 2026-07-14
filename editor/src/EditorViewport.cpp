#include "EditorViewport.h"

#include <engine/assets/RuntimeAssetManager.h>
#include <engine/graphics/Camera.h>
#include <engine/ecs/Components.h>
#include <engine/graphics/Mesh.h>
#include <engine/graphics/Model.h>
#include <engine/graphics/Renderer.h>
#include <engine/graphics/Shader.h>
#include <engine/graphics/SkinnedModel.h>
#include <engine/graphics/Texture.h>

#include <glad/glad.h>
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

    glClearStencil(0);
    glClear(GL_STENCIL_BUFFER_BIT);
    glEnable(GL_STENCIL_TEST);
    glStencilMask(0xFF);
    glStencilFunc(GL_ALWAYS, 1, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glDepthMask(GL_FALSE);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_CULL_FACE);

    shader.SetMat4("uModel", transform.Model());
    shader.SetVec3("uColor", color);
    shader.SetFloat("uThickness", 0.0f);
    drawGeometry();

    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glStencilMask(0x00);
    glStencilFunc(GL_NOTEQUAL, 1, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);
    shader.SetFloat("uThickness", std::max(thickness, 1.0f));
    drawGeometry();

    glColorMask(colorMask[0], colorMask[1], colorMask[2], colorMask[3]);
    glDepthMask(depthWrite);
    glDepthFunc(depthFunc);
    glCullFace(cullFace);
    if (!cullEnabled) glDisable(GL_CULL_FACE);
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
        return glm::vec3(0.36f, 0.92f, 0.48f);
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

void DrawBoxColliderGuide(engine::Renderer& renderer,
                          engine::Shader& shader,
                          const engine::Mesh& cube,
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
        DrawGuideSegment(renderer, shader, cube, world[edge[0]], world[edge[1]], 0.025f, color);
    }
}

void DrawPlaneColliderGuide(engine::Renderer& renderer,
                            engine::Shader& shader,
                            const engine::Mesh& cube,
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

    DrawGuideSegment(renderer, shader, cube, corners[0], corners[1], 0.025f, color);
    DrawGuideSegment(renderer, shader, cube, corners[1], corners[2], 0.025f, color);
    DrawGuideSegment(renderer, shader, cube, corners[2], corners[3], 0.025f, color);
    DrawGuideSegment(renderer, shader, cube, corners[3], corners[0], 0.025f, color);
    DrawGuideSegment(renderer, shader, cube, center - right * size, center + right * size, 0.018f, color);
    DrawGuideSegment(renderer, shader, cube, center - up * size, center + up * size, 0.018f, color);
    DrawGuideSegment(renderer, shader, cube, center, center + normal * 0.75f, 0.035f, color);
}

void DrawSphereColliderGuide(engine::Renderer& renderer,
                             engine::Shader& shader,
                             const engine::Mesh& cube,
                             const engine::ecs::Transform& transform,
                             const engine::ecs::Collider& collider,
                             const glm::vec3& color) {
    const float radius = std::max(collider.radius, 0.001f);
    DrawGuideRing(renderer, shader, cube, transform.position, EditorGizmo::Axis::X, radius, 0.035f, color);
    DrawGuideRing(renderer, shader, cube, transform.position, EditorGizmo::Axis::Y, radius, 0.035f, color);
    DrawGuideRing(renderer, shader, cube, transform.position, EditorGizmo::Axis::Z, radius, 0.035f, color);
}

void DrawBasisRing(engine::Renderer& renderer,
                   engine::Shader& shader,
                   const engine::Mesh& cube,
                   const glm::vec3& center,
                   const glm::vec3& right,
                   const glm::vec3& up,
                   float radius,
                   float thickness,
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
        DrawGuideSegment(renderer, shader, cube, p0, p1, thickness, color);
    }
}

void DrawCapsuleColliderGuide(engine::Renderer& renderer,
                              engine::Shader& shader,
                              const engine::Mesh& cube,
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

    DrawBasisRing(renderer, shader, cube, a, right, forward, radius, 0.028f, color);
    DrawBasisRing(renderer, shader, cube, b, right, forward, radius, 0.028f, color);
    DrawGuideSegment(renderer, shader, cube, a + right * radius, b + right * radius, 0.028f, color);
    DrawGuideSegment(renderer, shader, cube, a - right * radius, b - right * radius, 0.028f, color);
    DrawGuideSegment(renderer, shader, cube, a + forward * radius, b + forward * radius, 0.028f, color);
    DrawGuideSegment(renderer, shader, cube, a - forward * radius, b - forward * radius, 0.028f, color);
    DrawGuideRing(renderer, shader, cube, a, EditorGizmo::Axis::X, radius, 0.024f, color);
    DrawGuideRing(renderer, shader, cube, b, EditorGizmo::Axis::X, radius, 0.024f, color);
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
                   const glm::vec3& color,
                   float radius,
                   float marker) {
    constexpr int segments = 40;
    constexpr float pi = 3.14159265359f;

    for (int i = 0; i < segments; ++i) {
        const float t = (static_cast<float>(i) / static_cast<float>(segments)) * 2.0f * pi;
        DrawGizmoBox(renderer, shader, cube, center + RingOffset(axis, t, radius), glm::vec3(marker), color);
    }
}

} // namespace

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
                                    int viewportHeight) const {
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

    const glm::vec3 center = selectedTransform->position;
    const glm::vec3 xColor = AxisColor(EditorGizmo::Axis::X, gizmo.CurrentAxis());
    const glm::vec3 yColor = AxisColor(EditorGizmo::Axis::Y, gizmo.CurrentAxis());
    const glm::vec3 zColor = AxisColor(EditorGizmo::Axis::Z, gizmo.CurrentAxis());

    const float gizmoScale = GizmoWorldScale(*selectedTransform, camera, viewportHeight);
    const float length = gizmoScale * 0.78f;
    const float thickness = gizmoScale * 0.022f;
    const float head = gizmoScale * 0.16f;
    const float ringRadius = gizmoScale * 0.72f;
    const float ringMarker = gizmoScale * 0.018f;
    const glm::mat3 localBasis = glm::mat3_cast(selectedTransform->rotation);
    const glm::vec3 localX = glm::normalize(localBasis * glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::vec3 localY = glm::normalize(localBasis * glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::vec3 localZ = glm::normalize(localBasis * glm::vec3(0.0f, 0.0f, 1.0f));
    const GLboolean depthEnabled = glIsEnabled(GL_DEPTH_TEST);
    glDisable(GL_DEPTH_TEST);

    auto setAxisStyle = [&](const glm::vec3& color) {
        shader.SetVec3("uEmissive", color * 0.45f);
    };

    switch (gizmo.CurrentMode()) {
    case EditorGizmo::Mode::Translate:
        setAxisStyle(xColor);
        DrawGuideSegment(renderer, shader, cube, center, center + localX * length, thickness, xColor);
        DrawGizmoCone(renderer, shader, cone, center + localX * (length + head * 0.55f), localX, xColor, head);

        setAxisStyle(yColor);
        DrawGuideSegment(renderer, shader, cube, center, center + localY * length, thickness, yColor);
        DrawGizmoCone(renderer, shader, cone, center + localY * (length + head * 0.55f), localY, yColor, head);

        setAxisStyle(zColor);
        DrawGuideSegment(renderer, shader, cube, center, center + localZ * length, thickness, zColor);
        DrawGizmoCone(renderer, shader, cone, center + localZ * (length + head * 0.55f), localZ, zColor, head);
        break;

    case EditorGizmo::Mode::Scale:
        setAxisStyle(xColor);
        DrawGuideSegment(renderer, shader, cube, center, center + localX * length, thickness, xColor);
        DrawGizmoBox(renderer, shader, cube, center + localX * length,
            glm::vec3(head * 0.72f), xColor);

        setAxisStyle(yColor);
        DrawGuideSegment(renderer, shader, cube, center, center + localY * length, thickness, yColor);
        DrawGizmoBox(renderer, shader, cube, center + localY * length,
            glm::vec3(head * 0.72f), yColor);

        setAxisStyle(zColor);
        DrawGuideSegment(renderer, shader, cube, center, center + localZ * length, thickness, zColor);
        DrawGizmoBox(renderer, shader, cube, center + localZ * length,
            glm::vec3(head * 0.72f), zColor);
        break;

    case EditorGizmo::Mode::Rotate:
        setAxisStyle(xColor);
        DrawGizmoRing(renderer, shader, cube, center, EditorGizmo::Axis::X, xColor, ringRadius, ringMarker);
        setAxisStyle(yColor);
        DrawGizmoRing(renderer, shader, cube, center, EditorGizmo::Axis::Y, yColor, ringRadius, ringMarker);
        setAxisStyle(zColor);
        DrawGizmoRing(renderer, shader, cube, center, EditorGizmo::Axis::Z, zColor, ringRadius, ringMarker);
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

void EditorViewport::DrawPhysicsColliderGuides(engine::Renderer& renderer,
                                               engine::Shader& shader,
                                               const engine::Mesh& cube,
                                               const EditorScene& scene,
                                               const glm::mat4& viewProj,
                                               bool selectedOnly) const {
    const EditorScene::Object* selected = scene.SelectedObject();
    if (selectedOnly && (!selected || !selected->colliderEnabled || !selected->visible)) {
        return;
    }

    shader.Bind();
    shader.SetMat4("uViewProj", viewProj);
    shader.SetVec3("uLightDir", glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f)));
    shader.SetInt("uHasDiffuse", 0);

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

        const glm::vec3 baseColor = PhysicsGuideColor(object);
        const glm::vec3 color = selectedCollider
            ? baseColor
            : glm::mix(glm::vec3(0.30f), baseColor, 0.48f);
        shader.SetVec3("uEmissive", color * (selectedCollider ? 0.36f : 0.12f));

        switch (object.collider.shape) {
        case engine::ecs::ColliderShape::Sphere:
            DrawSphereColliderGuide(renderer, shader, cube, *transform, object.collider, color);
            break;
        case engine::ecs::ColliderShape::Box:
            DrawBoxColliderGuide(renderer, shader, cube, *transform, object.collider, color);
            break;
        case engine::ecs::ColliderShape::Plane:
            DrawPlaneColliderGuide(renderer, shader, cube, object.collider, color);
            break;
        case engine::ecs::ColliderShape::Capsule:
            DrawCapsuleColliderGuide(renderer, shader, cube, *transform, object.collider, color);
            break;
        }
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
        if (glm::length(guide.b - guide.a) <= 0.001f) {
            continue;
        }

        const glm::vec3 color = PhysicsJointGuideColor(guide);
        shader.SetVec3("uEmissive", color * (guide.enabled ? 0.32f : 0.10f));
        DrawGuideSegment(renderer, shader, cube, guide.a, guide.b, guide.rope ? 0.030f : 0.038f, color);
        DrawGizmoBox(renderer, shader, cube, guide.a, glm::vec3(0.055f), color);
        DrawGizmoBox(renderer, shader, cube, guide.b, glm::vec3(0.055f), color);
    }

    shader.SetVec3("uEmissive", glm::vec3(0.0f));
}

void EditorViewport::DrawSelectedModelOutline(engine::Renderer& renderer,
                                              engine::Shader& shader,
                                              const engine::ecs::Transform& transform,
                                              const engine::Model& model,
                                              const glm::vec3& color,
                                              float thickness) const {
    DrawStencilSelectionOutline(shader, transform, color, thickness, [&]() {
        for (const engine::SubMesh& subMesh : model.SubMeshes()) renderer.Draw(subMesh.mesh);
    });
}

void EditorViewport::DrawSelectedMeshOutline(engine::Renderer& renderer,
                                             engine::Shader& shader,
                                             const engine::ecs::Transform& transform,
                                             const engine::Mesh& mesh,
                                             const glm::vec3& color,
                                             float thickness) const {
    DrawStencilSelectionOutline(shader, transform, color, thickness, [&]() { renderer.Draw(mesh); });
}

void EditorViewport::DrawSelectedSkinnedModelOutline(engine::Renderer& renderer,
                                                      engine::Shader& shader,
                                                      const engine::ecs::Transform& transform,
                                                      const engine::SkinnedModel& model,
                                                      const std::vector<glm::mat4>& pose,
                                                      const glm::vec3& color,
                                                      float thickness) const {
    const std::size_t boneCount = std::min<std::size_t>(pose.size(), engine::SkinnedModel::kMaxBones);
    for (std::size_t i = 0; i < boneCount; ++i) {
        shader.SetMat4("uBones[" + std::to_string(i) + "]", pose[i]);
    }
    DrawStencilSelectionOutline(shader, transform, color, thickness, [&]() {
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
                                    int height) const {
    PickRay ray;
    if (!BuildPickRay(x, y, viewProj, width, height, &ray)) {
        return -1;
    }

    int picked = -1;
    float bestDistance = std::numeric_limits<float>::max();

    const std::vector<EditorScene::Object>& objects = scene.Objects();
    for (int i = 0; i < static_cast<int>(objects.size()); ++i) {
        const EditorScene::Object& object = objects[static_cast<std::size_t>(i)];
        if (!object.visible) {
            continue;
        }

        const engine::ecs::Transform* transform = scene.TryGetTransform(object.entity);
        if (!transform) {
            continue;
        }

        const glm::mat4 inverseModel = glm::inverse(transform->Model());
        const PickRay localRay = TransformRayToLocal(ray, inverseModel);
        float hitDistance = 0.0f;
        bool hit = false;
        if (!object.modelAssetPath.empty()) {
            const engine::Model* model = assets.FindModel(object.modelAssetPath);
            if (model) {
                hit = IntersectLocalAabb(localRay, model->Min(), model->Max(), &hitDistance);
            } else {
                hit = IntersectLocalAabb(localRay, glm::vec3(-0.5f), glm::vec3(0.5f), &hitDistance);
            }
        } else {
            switch (object.primitive) {
            case EditorScene::Primitive::Plane:
                hit = IntersectLocalPlaneQuad(localRay, &hitDistance);
                break;
            case EditorScene::Primitive::Cube:
                hit = IntersectLocalAabb(localRay, glm::vec3(-0.5f), glm::vec3(0.5f), &hitDistance);
                break;
            }
        }

        if (hit && hitDistance < bestDistance) {
            bestDistance = hitDistance;
            picked = i;
        }
    }

    return picked;
}

bool EditorViewport::PickGizmoHandle(EditorGizmo& gizmo,
                                     const EditorScene& scene,
                                     float x,
                                     float y,
                                     const glm::mat4& viewProj,
                                     int width,
                                     int height,
                                     const engine::Camera& camera) const {
    const EditorScene::Object* selectedObject = scene.SelectedObject();
    const engine::ecs::Transform* selectedTransform = selectedObject
        ? scene.TryGetTransform(selectedObject->entity)
        : nullptr;
    if (!selectedTransform || scene.SelectedLocked()) {
        return false;
    }

    const float gizmoScale = GizmoWorldScale(*selectedTransform, camera, height);
    const float length = gizmoScale * 0.78f;
    const float head = gizmoScale * 0.16f;
    const float rotateRadius = gizmoScale * 0.72f;
    constexpr float pi = 3.14159265359f;
    const glm::vec2 mouse{x, y};
    const glm::vec3 center = selectedTransform->position;
    glm::vec2 centerScreen;
    if (!ProjectWorldToScreen(center, viewProj, width, height, &centerScreen)) {
        return false;
    }

    auto testAxisSegment = [&](EditorGizmo::Axis axis, float axisLength, float maxDistance) {
        glm::vec2 endScreen;
        glm::vec3 worldAxis = AxisVector(axis);
        if (gizmo.CurrentMode() == EditorGizmo::Mode::Translate
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
                    if (!ProjectWorldToScreen(center + RingOffset(axis, t, rotateRadius),
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
