#include "EditorViewport.h"

#include <engine/assets/RuntimeAssetManager.h>
#include <engine/ecs/Components.h>
#include <engine/graphics/Mesh.h>
#include <engine/graphics/Model.h>
#include <engine/graphics/Renderer.h>
#include <engine/graphics/Shader.h>
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

void DrawGizmoCone(engine::Renderer& renderer,
                   engine::Shader& shader,
                   const engine::Mesh& cone,
                   const glm::vec3& position,
                   EditorGizmo::Axis axis,
                   const glm::vec3& color) {
    engine::ecs::Transform transform;
    transform.position = position;
    transform.scale = glm::vec3(0.22f, 0.36f, 0.22f);
    switch (axis) {
    case EditorGizmo::Axis::X:
        transform.rotation = glm::angleAxis(glm::radians(-90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        break;
    case EditorGizmo::Axis::Y:
        transform.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        break;
    case EditorGizmo::Axis::Z:
        transform.rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
        break;
    }
    shader.SetMat4("uModel", transform.Model());
    shader.SetVec3("uColor", color);
    renderer.Draw(cone);
}

void DrawGizmoRing(engine::Renderer& renderer,
                   engine::Shader& shader,
                   const engine::Mesh& cube,
                   const glm::vec3& center,
                   EditorGizmo::Axis axis,
                   const glm::vec3& color) {
    constexpr int segments = 40;
    constexpr float radius = 1.25f;
    constexpr float pi = 3.14159265359f;
    constexpr float marker = 0.055f;

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
                                    const glm::mat4& viewProj) const {
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

    const glm::vec3 center = selectedTransform->position;
    const glm::vec3 xColor = AxisColor(EditorGizmo::Axis::X, gizmo.CurrentAxis());
    const glm::vec3 yColor = AxisColor(EditorGizmo::Axis::Y, gizmo.CurrentAxis());
    const glm::vec3 zColor = AxisColor(EditorGizmo::Axis::Z, gizmo.CurrentAxis());

    constexpr float length = 1.65f;
    constexpr float thickness = 0.055f;
    constexpr float head = 0.18f;

    switch (gizmo.CurrentMode()) {
    case EditorGizmo::Mode::Translate:
        DrawGizmoBox(renderer, shader, cube, center + glm::vec3(length * 0.5f, 0.0f, 0.0f),
            glm::vec3(length, thickness, thickness), xColor);
        DrawGizmoCone(renderer, shader, cone, center + glm::vec3(length + head * 1.6f, 0.0f, 0.0f),
            EditorGizmo::Axis::X, xColor);

        DrawGizmoBox(renderer, shader, cube, center + glm::vec3(0.0f, length * 0.5f, 0.0f),
            glm::vec3(thickness, length, thickness), yColor);
        DrawGizmoCone(renderer, shader, cone, center + glm::vec3(0.0f, length + head * 1.6f, 0.0f),
            EditorGizmo::Axis::Y, yColor);

        DrawGizmoBox(renderer, shader, cube, center + glm::vec3(0.0f, 0.0f, length * 0.5f),
            glm::vec3(thickness, thickness, length), zColor);
        DrawGizmoCone(renderer, shader, cone, center + glm::vec3(0.0f, 0.0f, length + head * 1.6f),
            EditorGizmo::Axis::Z, zColor);
        break;

    case EditorGizmo::Mode::Scale:
        DrawGizmoBox(renderer, shader, cube, center + glm::vec3(length * 0.45f, 0.0f, 0.0f),
            glm::vec3(length * 0.9f, thickness, thickness), xColor);
        DrawGizmoBox(renderer, shader, cube, center + glm::vec3(length, 0.0f, 0.0f),
            glm::vec3(head * 1.15f), xColor);

        DrawGizmoBox(renderer, shader, cube, center + glm::vec3(0.0f, length * 0.45f, 0.0f),
            glm::vec3(thickness, length * 0.9f, thickness), yColor);
        DrawGizmoBox(renderer, shader, cube, center + glm::vec3(0.0f, length, 0.0f),
            glm::vec3(head * 1.15f), yColor);

        DrawGizmoBox(renderer, shader, cube, center + glm::vec3(0.0f, 0.0f, length * 0.45f),
            glm::vec3(thickness, thickness, length * 0.9f), zColor);
        DrawGizmoBox(renderer, shader, cube, center + glm::vec3(0.0f, 0.0f, length),
            glm::vec3(head * 1.15f), zColor);
        break;

    case EditorGizmo::Mode::Rotate:
        DrawGizmoRing(renderer, shader, cube, center, EditorGizmo::Axis::X, xColor);
        DrawGizmoRing(renderer, shader, cube, center, EditorGizmo::Axis::Y, yColor);
        DrawGizmoRing(renderer, shader, cube, center, EditorGizmo::Axis::Z, zColor);
        break;
    }
}

void EditorViewport::DrawSelectedModelOutline(engine::Renderer& renderer,
                                              engine::Shader& shader,
                                              const engine::ecs::Transform& transform,
                                              const engine::Model& model,
                                              const glm::vec3& color,
                                              float thickness) const {
    for (const engine::SubMesh& subMesh : model.SubMeshes()) {
        DrawSelectedMeshOutline(renderer, shader, transform, subMesh.mesh, color, thickness);
    }
}

void EditorViewport::DrawSelectedMeshOutline(engine::Renderer& renderer,
                                             engine::Shader& shader,
                                             const engine::ecs::Transform& transform,
                                             const engine::Mesh& mesh,
                                             const glm::vec3& color,
                                             float thickness) const {
    const GLboolean wasCullEnabled = glIsEnabled(GL_CULL_FACE);
    GLint previousCullFace = GL_BACK;
    GLint previousDepthFunc = GL_LESS;
    glGetIntegerv(GL_CULL_FACE_MODE, &previousCullFace);
    glGetIntegerv(GL_DEPTH_FUNC, &previousDepthFunc);

    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);
    glDepthFunc(GL_LEQUAL);

    shader.SetMat4("uModel", transform.Model());
    shader.SetFloat("uThickness", thickness);
    shader.SetVec3("uColor", color);
    renderer.Draw(mesh);

    glCullFace(previousCullFace);
    if (!wasCullEnabled) {
        glDisable(GL_CULL_FACE);
    }
    glDepthFunc(previousDepthFunc);
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
                                     const engine::Texture* diffuseTexture) const {
    if (!meshRenderer.mesh) {
        return;
    }

    shader.SetMat4("uModel", transform.Model());
    shader.SetVec3("uColor", meshRenderer.color);
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
                                     int height) const {
    const EditorScene::Object* selectedObject = scene.SelectedObject();
    const engine::ecs::Transform* selectedTransform = selectedObject
        ? scene.TryGetTransform(selectedObject->entity)
        : nullptr;
    if (!selectedTransform || scene.SelectedLocked()) {
        return false;
    }

    constexpr float length = 1.65f;
    constexpr float head = 0.18f;
    constexpr float rotateRadius = 1.25f;
    constexpr float pi = 3.14159265359f;
    const glm::vec2 mouse{x, y};
    const glm::vec3 center = selectedTransform->position;
    glm::vec2 centerScreen;
    if (!ProjectWorldToScreen(center, viewProj, width, height, &centerScreen)) {
        return false;
    }

    auto testAxisSegment = [&](EditorGizmo::Axis axis, float axisLength, float maxDistance) {
        glm::vec2 endScreen;
        if (!ProjectWorldToScreen(center + AxisVector(axis) * axisLength, viewProj, width, height, &endScreen)) {
            return false;
        }

        return DistanceToSegmentSquared(mouse, centerScreen, endScreen) <= maxDistance * maxDistance;
    };

    switch (gizmo.CurrentMode()) {
    case EditorGizmo::Mode::Translate:
        if (testAxisSegment(EditorGizmo::Axis::X, length + head * 1.6f, 18.0f)) {
            gizmo.SetAxis(EditorGizmo::Axis::X);
            return true;
        }
        if (testAxisSegment(EditorGizmo::Axis::Y, length + head * 1.6f, 18.0f)) {
            gizmo.SetAxis(EditorGizmo::Axis::Y);
            return true;
        }
        if (testAxisSegment(EditorGizmo::Axis::Z, length + head * 1.6f, 18.0f)) {
            gizmo.SetAxis(EditorGizmo::Axis::Z);
            return true;
        }
        break;

    case EditorGizmo::Mode::Scale:
        if (testAxisSegment(EditorGizmo::Axis::X, length, 18.0f)) {
            gizmo.SetAxis(EditorGizmo::Axis::X);
            return true;
        }
        if (testAxisSegment(EditorGizmo::Axis::Y, length, 18.0f)) {
            gizmo.SetAxis(EditorGizmo::Axis::Y);
            return true;
        }
        if (testAxisSegment(EditorGizmo::Axis::Z, length, 18.0f)) {
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
