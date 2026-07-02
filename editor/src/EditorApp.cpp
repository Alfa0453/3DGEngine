#include "EditorApp.h"

#include <engine/assets/RuntimeAssetManager.h>
#include <engine/ecs/Registry.h>
#include <engine/ecs/RuntimeSystems.h>
#include <engine/ecs/Systems.h>
#include <engine/graphics/Primitives.h>

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdio>
#include <filesystem>
#include <limits>

using engine::ecs::Entity;
using engine::ecs::MeshRenderer;
using engine::ecs::Transform;

namespace {

engine::WindowProps MakeEditorWindowProps(const engine::Config& config) {
    engine::WindowProps props;
    props.title = "3DG Editor";
    props.width = config.GetInt("window.width", 1440);
    props.height = config.GetInt("window.height", 900);
    props.vsync = config.GetBool("window.vsync", true);
    return props;
}

glm::vec3 SelectedColor(bool selected, const glm::vec3& base) {
    return selected ? glm::vec3(1.0f, 0.78f, 0.22f) : base;
}

template <class T>
std::size_t ComponentCount(engine::ecs::Registry& registry) {
    engine::ecs::Pool<T>* pool = registry.TryPool<T>();
    return pool ? pool->dense.size() : 0;
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

struct PickRay {
    glm::vec3 origin{0.0f};
    glm::vec3 direction{0.0f, 0.0f, -1.0f};
};

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

} // namespace

EditorApp::EditorApp(engine::Config &config)
    : engine::Application(MakeEditorWindowProps(config)), m_config(config)
{
}

void EditorApp::OnInit()
{
    m_renderer.Init();
    m_renderer.SetClearColor({0.08f, 0.09f, 0.11f, 1.0f});

    m_cube.emplace(engine::primitives::Cube());
    m_cone.emplace(engine::primitives::Cone());
    m_plane.emplace(engine::primitives::Plane(1.0f, 8.0f));
    m_shader.emplace(
        R"glsl(
            #version 330 core
            layout(location = 0) in vec3 aPos;
            layout(location = 1) in vec3 aNormal;
            layout(location = 2) in vec2 aTexCoord;

            uniform mat4 uViewProj;
            uniform mat4 uModel;

            out vec3 vNormal;
            out vec3 vWorldPos;
            out vec2 vTexCoord;

            void main() {
                vec4 world = uModel * vec4(aPos, 1.0);
                vWorldPos = world.xyz;
                vNormal = mat3(transpose(inverse(uModel))) * aNormal;
                vTexCoord = aTexCoord;
                gl_Position = uViewProj * world;
            }
        )glsl",
        R"glsl(
            #version 330 core
            in vec3 vNormal;
            in vec3 vWorldPos;
            in vec2 vTexCoord;

            uniform vec3 uColor;
            uniform vec3 uLightDir;
            uniform int uHasDiffuse;
            uniform sampler2D uDiffuseTex;

            out vec4 FragColor;

            void main() {
                vec3 normal = normalize(vNormal);
                float diffuse = max(dot(normal, normalize(-uLightDir)), 0.0);
                vec3 base = uColor;
                if (uHasDiffuse == 1) {
                    base *= texture(uDiffuseTex, vTexCoord).rgb;
                }
                vec3 ambient = uColor * 0.24;
                vec3 lit = ambient + base * diffuse * 0.76;
                FragColor = vec4(lit, 1.0);
            }
        )glsl");
    m_outlineShader.emplace(
    R"glsl(
        #version 330 core
        layout(location = 0) in vec3 aPos;
        layout(location = 1) in vec3 aNormal;

        uniform mat4 uViewProj;
        uniform mat4 uModel;
        uniform float uThickness;

        void main() {
            mat3 normalMat = mat3(transpose(inverse(uModel)));
            vec3 worldNormal = normalize(normalMat * aNormal);
            vec4 world = uModel * vec4(aPos, 1.0);
            world.xyz += worldNormal * uThickness;
            gl_Position = uViewProj * world;
        }
    )glsl",
    R"glsl(
        #version 330 core
        uniform vec3 uColor;

        out vec4 FragColor;

        void main() {
            FragColor = vec4(uColor, 1.0);
        }
    )glsl");
    m_modelShader.emplace(
        R"glsl(
            #version 330 core
            layout(location = 0) in vec3 aPos;
            layout(location = 1) in vec3 aNormal;
            layout(location = 2) in vec2 aTexCoord;
            layout(location = 3) in vec3 aTangent;

            uniform mat4 uViewProj;
            uniform mat4 uModel;
            uniform mat3 uNormalMat;

            out vec3 vWorldPos;
            out vec2 vUV;
            out mat3 vTBN;

            void main() {
                vec4 world = uModel * vec4(aPos, 1.0);
                vWorldPos = world.xyz;
                vUV = aTexCoord;

                vec3 n = normalize(uNormalMat * aNormal);
                vec3 t = normalize(uNormalMat * aTangent);
                t = normalize(t - dot(t, n) * n);
                vec3 b = cross(n, t);
                vTBN = mat3(t, b, n);

                gl_Position = uViewProj * world;
            }
        )glsl",
        R"glsl(
            #version 330 core
            in vec3 vWorldPos;
            in vec2 vUV;
            in mat3 vTBN;

            uniform vec3 uLightPos;
            uniform vec3 uLightColor;
            uniform vec3 uViewPos;

            uniform vec3 uColor;
            uniform vec3 uSpecular;
            uniform vec3 uEmissive;
            uniform float uShininess;

            uniform int uHasDiffuse;
            uniform int uHasNormal;
            uniform int uHasSpecular;
            uniform int uHasEmissive;
            uniform sampler2D uDiffuseTex;
            uniform sampler2D uNormalTex;
            uniform sampler2D uSpecularTex;
            uniform sampler2D uEmissiveTex;

            out vec4 FragColor;

            void main() {
                vec3 base = uColor;
                if (uHasDiffuse == 1) {
                    base *= texture(uDiffuseTex, vUV).rgb;
                }

                vec3 normal = normalize(vTBN[2]);
                if (uHasNormal == 1) {
                    vec3 sampled = texture(uNormalTex, vUV).rgb * 2.0 - 1.0;
                    normal = normalize(vTBN * sampled);
                }

                vec3 light = normalize(uLightPos - vWorldPos);
                vec3 view = normalize(uViewPos - vWorldPos);
                vec3 reflectDir = reflect(-light, normal);

                float diffuseAmount = max(dot(normal, light), 0.0);
                float specularAmount = pow(max(dot(view, reflectDir), 0.0), max(uShininess, 1.0));

                vec3 specularColor = uSpecular;
                if (uHasSpecular == 1) {
                    specularColor *= texture(uSpecularTex, vUV).rgb;
                }

                vec3 emissiveColor = uEmissive;
                if (uHasEmissive == 1) {
                    emissiveColor *= texture(uEmissiveTex, vUV).rgb;
                }

                vec3 ambient = base * uLightColor * 0.18;
                vec3 diffuse = base * uLightColor * diffuseAmount;
                vec3 specular = specularColor * uLightColor * specularAmount;
                FragColor = vec4(ambient + diffuse + specular + emissiveColor, 1.0);
            }
        )glsl"
    );
    m_text.emplace();

    m_project.Load(m_config);
    RefreshAssets();
    m_scene.BuildDefault(*m_cube, *m_plane);
    m_imgui.Init(GetWindow());
}

void EditorApp::OnUpdate(float dt)
{
    engine::Window& window = GetWindow();
    m_elapsed += dt;
    if (dt > 0.0f) {
        m_fps = m_fps * 0.92f + (1.0f / dt) * 0.08f;
    }
    if (m_mode == EditorMode::Play && m_playRegistry) {
        engine::ecs::UpdateRuntimeMotion(*m_playRegistry, dt);
    }

    HandleGlobalShortcuts(window);
    UpdateMouseCapture(window);
    HandleMouseAssetDrag();
    HandleMouseViewportSelection();
    HandleMouseViewportGizmo();

    const bool controlDown = window.IsKeyPressed(GLFW_KEY_LEFT_CONTROL)
        || window.IsKeyPressed(GLFW_KEY_RIGHT_CONTROL);
    HandleAssetShortcuts(window, controlDown);
    HandleEditorCommandShortcuts(window, controlDown);
    UpdateCameraControls(window, dt);
    UpdateSelectedTransformShortcuts(window, dt);
}

void EditorApp::OnRender()
{
    m_renderer.Clear();

    const engine::Window& window = GetWindow();
    const glm::mat4 viewProj = m_camera.ProjectionMatrix(window.AspectRatio()) * m_camera.ViewMatrix();

    if (m_mode == EditorMode::Play && m_playRegistry) {
        DrawPlayScene(viewProj);
    } else {
        DrawEditScene(viewProj);
    }

    m_imgui.BeginFrame();
    DrawEditorOverlay();
    m_imgui.EndFrame();
}

void EditorApp::OnShutdown()
{
    m_imgui.Shutdown();
    m_project.Save(m_config);
    m_config.Set("window.vsync", GetWindow().IsVSync());
    m_config.Set("window.fullscreen", GetWindow().IsFullscreen());
    m_config.Save();
}

void EditorApp::DrawSceneObject(const engine::ecs::Transform &transform, const engine::ecs::MeshRenderer &renderer, const engine::Texture* diffuseTexture)
{
    if (!renderer.mesh) {
        return;
    }

    m_shader->SetMat4("uModel", transform.Model());
    m_shader->SetVec3("uColor", renderer.color);
    m_shader->SetInt("uHasDiffuse", diffuseTexture ? 1 : 0);
    if (diffuseTexture) {
        diffuseTexture->Bind(0);
        m_shader->SetInt("uDiffuseTex", 0);
    }
    m_renderer.Draw(*renderer.mesh);
}

void EditorApp::DrawEditModeModels(const glm::mat4 & viewProj)
{
    if (!m_modelShader) {
        return;
    }

    m_modelShader->Bind();
    m_modelShader->SetMat4("uViewProj", viewProj);
    m_modelShader->SetVec3("uLightPos", m_camera.Position() + glm::vec3(-4.0f, 6.0f, 4.0f));
    m_modelShader->SetVec3("uLightColor", glm::vec3(1.0f));
    m_modelShader->SetVec3("uViewPos", m_camera.Position());

    for (const EditorScene::Object& object : m_scene.Objects()) {
        if (!object.visible || object.modelAssetPath.empty()) {
            continue;
        }

        const Transform* transform = m_scene.TryGetTransform(object.entity);
        if (!transform) {
            continue;
        }

        std::string error;
        const engine::Model* model = m_editAssets.LoadModel(object.modelAssetPath, &error);
        if (!model) {
            if (!m_editModelLoadErrors[object.modelAssetPath]) {
                m_log.Error("Could not load edit model: " + error);
                m_editModelLoadErrors[object.modelAssetPath] = true;
            }
            continue;
        }

        const glm::mat4 modelMatrix = transform->Model();
        m_modelShader->SetMat4("uModel", modelMatrix);
        m_modelShader->SetMat3("uNormalMat", glm::mat3(glm::transpose(glm::inverse(modelMatrix))));
        engine::DrawModel(*model, *m_modelShader);
    }

    m_shader->Bind();
    m_shader->SetMat4("uViewProj", viewProj);
    m_shader->SetVec3("uLightDir", glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f)));
}

void EditorApp::DrawSelectionOutline(const glm::mat4 & viewProj)
{
    if (m_mode != EditorMode::Edit || !m_cube || !m_outlineShader) {
        return;
    }

    const EditorScene::Object* selected = m_scene.SelectedObject();
    const Transform* transform = m_scene.SelectedTransform();
    if (!selected || !transform || !selected->visible) {
        return;
    }

    if (!selected->modelAssetPath.empty()) {
        std::string error;
        const engine::Model* model = m_editAssets.LoadModel(selected->modelAssetPath, &error);
        if (model) {
            const glm::vec3 color = selected->locked
                ? glm::vec3(1.0f, 0.48f, 0.22f)
                : glm::vec3(1.0f, 0.92f, 0.24f);
            m_outlineShader->Bind();
            m_outlineShader->SetMat4("uViewProj", viewProj);
            DrawSelectedModelOutline(*transform, *model, color, 0.045f);
        }
        return;
    }

    const MeshRenderer* renderer = m_scene.TryGetMeshRenderer(selected->entity);
    if (!renderer || !renderer->mesh) {
        return;
    }

    const glm::vec3 color = selected->locked
        ? glm::vec3(1.0f, 0.48f, 0.22f)
        : glm::vec3(1.0f, 0.92f, 0.24f);
    m_outlineShader->Bind();
    m_outlineShader->SetMat4("uViewProj", viewProj);
    DrawSelectedMeshOutline(*transform, *renderer->mesh, color, 0.045f);
}

void EditorApp::DrawSelectedModelOutline(const engine::ecs::Transform & transform, const engine::Model & model, const glm::vec3 & color, float thickness)
{
    for (const engine::SubMesh& subMesh : model.SubMeshes()) {
        DrawSelectedMeshOutline(transform, subMesh.mesh, color, thickness);
    }
}

void EditorApp::DrawSelectedMeshOutline(const engine::ecs::Transform & transform, const engine::Mesh & mesh, const glm::vec3 & color, float thickness)
{
    if (!m_outlineShader) {
        return;
    }
    
    const GLboolean wasCullEnabled = glIsEnabled(GL_CULL_FACE);
    GLint previousCullFace = GL_BACK;
    GLint previousDepthFunc = GL_LESS;
    glGetIntegerv(GL_CULL_FACE_MODE, &previousCullFace);
    glGetIntegerv(GL_DEPTH_FUNC, &previousDepthFunc);

    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);
    glDepthFunc(GL_LEQUAL);

    m_outlineShader->SetMat4("uModel", transform.Model());
    m_outlineShader->SetVec3("uColor", color);
    m_outlineShader->SetFloat("uThickness", thickness);
    m_renderer.Draw(mesh);

    glCullFace(previousCullFace);
    if (!wasCullEnabled) {
        glDisable(GL_CULL_FACE);
    }
    glDepthFunc(previousDepthFunc);
}

void EditorApp::DrawSceneGizmo(const glm::mat4 & viewProj)
{
        if (m_mode != EditorMode::Edit || !m_cube || !m_shader) {
        return;
    }

    const EditorScene::Object* selected = m_scene.SelectedObject();
    const Transform* selectedTransform = m_scene.SelectedTransform();
    if (!selected || !selectedTransform || selected->locked) {
        return;
    }

    m_shader->Bind();
    m_shader->SetMat4("uViewProj", viewProj);
    m_shader->SetVec3("uLightDir", glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f)));

    const glm::vec3 center = selectedTransform->position;
    const glm::vec3 xColor = m_gizmo.CurrentAxis() == EditorGizmo::Axis::X
        ? glm::vec3(1.0f, 0.86f, 0.24f)
        : glm::vec3(0.95f, 0.12f, 0.14f);
    const glm::vec3 yColor = m_gizmo.CurrentAxis() == EditorGizmo::Axis::Y
        ? glm::vec3(1.0f, 0.86f, 0.24f)
        : glm::vec3(0.12f, 0.82f, 0.28f);
    const glm::vec3 zColor = m_gizmo.CurrentAxis() == EditorGizmo::Axis::Z
        ? glm::vec3(1.0f, 0.86f, 0.24f)
        : glm::vec3(0.16f, 0.36f, 1.0f);

    constexpr float length = 1.65f;
    constexpr float thickness = 0.055f;
    constexpr float head = 0.18f;

    switch (m_gizmo.CurrentMode()) {
    case EditorGizmo::Mode::Translate:
        DrawGizmoBox(center + glm::vec3(length * 0.5f, 0.0f, 0.0f), glm::vec3(length, thickness, thickness), xColor);
        DrawGizmoCone(center + glm::vec3(length + head * 1.6f, 0.0f, 0.0f), EditorGizmo::Axis::X, xColor);

        DrawGizmoBox(center + glm::vec3(0.0f, length * 0.5f, 0.0f), glm::vec3(thickness, length, thickness), yColor);
        DrawGizmoCone(center + glm::vec3(0.0f, length + head * 1.6f, 0.0f), EditorGizmo::Axis::Y, yColor);

        DrawGizmoBox(center + glm::vec3(0.0f, 0.0f, length * 0.5f), glm::vec3(thickness, thickness, length), zColor);
        DrawGizmoCone(center + glm::vec3(0.0f, 0.0f, length + head * 1.6f), EditorGizmo::Axis::Z, zColor);
        break;

    case EditorGizmo::Mode::Scale:
        DrawGizmoBox(center + glm::vec3(length * 0.45f, 0.0f, 0.0f), glm::vec3(length * 0.9f, thickness, thickness), xColor);
        DrawGizmoBox(center + glm::vec3(length, 0.0f, 0.0f), glm::vec3(head * 1.15f), xColor);

        DrawGizmoBox(center + glm::vec3(0.0f, length * 0.45f, 0.0f), glm::vec3(thickness, length * 0.9f, thickness), yColor);
        DrawGizmoBox(center + glm::vec3(0.0f, length, 0.0f), glm::vec3(head * 1.15f), yColor);

        DrawGizmoBox(center + glm::vec3(0.0f, 0.0f, length * 0.45f), glm::vec3(thickness, thickness, length * 0.9f), zColor);
        DrawGizmoBox(center + glm::vec3(0.0f, 0.0f, length), glm::vec3(head * 1.15f), zColor);
        break;

    case EditorGizmo::Mode::Rotate:
        DrawGizmoRing(center, EditorGizmo::Axis::X, xColor);
        DrawGizmoRing(center, EditorGizmo::Axis::Y, yColor);
        DrawGizmoRing(center, EditorGizmo::Axis::Z, zColor);
        break;
    }
}

void EditorApp::DrawGizmoBox(const glm::vec3 & position, const glm::vec3 & scale, const glm::vec3 & color)
{
        if (!m_cube || !m_shader) {
        return;
    }

    Transform transform;
    transform.position = position;
    transform.scale = scale;
    m_shader->SetMat4("uModel", transform.Model());
    m_shader->SetVec3("uColor", color);
    m_renderer.Draw(*m_cube);
}

void EditorApp::DrawGizmoCone(const glm::vec3 & position, EditorGizmo::Axis axis, const glm::vec3 & color)
{
        if (!m_cone || !m_shader) {
        return;
    }

    Transform transform;
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
    m_shader->SetMat4("uModel", transform.Model());
    m_shader->SetVec3("uColor", color);
    m_renderer.Draw(*m_cone);
}

void EditorApp::DrawGizmoRing(const glm::vec3 & center, EditorGizmo::Axis axis, const glm::vec3 & color)
{
        constexpr int segments = 40;
    constexpr float radius = 1.25f;
    constexpr float pi = 3.14159265359f;
    constexpr float marker = 0.055f;

    for (int i = 0; i < segments; ++i) {
        const float t = (static_cast<float>(i) / static_cast<float>(segments)) * 2.0f * pi;
        glm::vec3 offset;
        switch (axis) {
        case EditorGizmo::Axis::X:
            offset = glm::vec3(0.0f, std::cos(t) * radius, std::sin(t) * radius);
            break;
        case EditorGizmo::Axis::Y:
            offset = glm::vec3(std::cos(t) * radius, 0.0f, std::sin(t) * radius);
            break;
        case EditorGizmo::Axis::Z:
            offset = glm::vec3(std::cos(t) * radius, std::sin(t) * radius, 0.0f);
            break;
        }
        DrawGizmoBox(center + offset, glm::vec3(marker), color);
    }
}

void EditorApp::DrawEditorOverlay()
{
   const engine::Window& window = GetWindow();
    const int width = window.Width();
    const int height = window.Height();
    EditorDockspace::Context dockspaceContext;
    dockspaceContext.panels = &m_panels;
    dockspaceContext.scene = &m_scene;
    dockspaceContext.assets = &m_assets;
    dockspaceContext.dragDrop = &m_dragDrop;
    dockspaceContext.project = &m_project;
    dockspaceContext.log = &m_log;
    dockspaceContext.gizmo = &m_gizmo;
    dockspaceContext.modeName = m_mode == EditorMode::Edit ? "Edit" : "Play";
    dockspaceContext.fps = m_fps;
    dockspaceContext.sceneDirty = m_scene.IsDirty();
    const bool dockspaceDrawn = m_dockspace.Draw(dockspaceContext);
    if (dockspaceContext.viewportDropRequested) {
        DropPayloadOnScene();
    }

    m_text->Begin(width, height);

    const glm::vec3 text(0.92f, 0.94f, 0.96f);
    const glm::vec3 muted(0.66f, 0.70f, 0.76f);
    const glm::vec3 accent(1.0f, 0.78f, 0.22f);

    char line[160];
    std::snprintf(line, sizeof(line), "3DG EDITOR  %s%s  %.0f fps",
        m_mode == EditorMode::Edit ? "EDIT" : "PLAY",
        (m_mode == EditorMode::Edit && m_scene.IsDirty()) ? " *" : "",
        m_fps);
    m_text->Text(line, 24.0f, 22.0f, 1.8f, text);
    std::snprintf(line, sizeof(line), "%s  scene: %s",
        m_project.ProjectName().c_str(), m_project.ScenePath().c_str());
    m_text->Text(line, 24.0f, 52.0f, 1.15f, muted);

    float y = 120.0f;
    if (!dockspaceDrawn && m_panels.IsOpen(EditorPanels::Panel::Hierarchy)) {
        m_text->Text("Hierarchy", 24.0f, 88.0f, 1.45f, text);
        const std::vector<EditorScene::Object>& objects = m_scene.Objects();
        for (int i = 0; i < static_cast<int>(objects.size()); ++i) {
            const EditorScene::Object& object = objects[static_cast<std::size_t>(i)];
            std::snprintf(line, sizeof(line), "%s%s%s %s",
                i == m_scene.SelectedIndex() ? ">" : " ",
                object.visible ? " " : "H",
                object.locked ? "L" : " ",
                object.name.c_str());
            m_text->Text(line, 30.0f, y, 1.25f, i == m_scene.SelectedIndex() ? accent : muted);
            y += 26.0f;
        }
    }
    if (!dockspaceDrawn && m_panels.IsOpen(EditorPanels::Panel::Assets)) {
        DrawAssetOverlay(24.0f, y + 28.0f, text, muted);
    }

    if (!dockspaceDrawn && m_panels.IsOpen(EditorPanels::Panel::Inspector)) {
        m_text->Text("Inspector", static_cast<float>(width) - 330.0f, 70.0f, 1.45f, text);
        if (const EditorScene::Object* selected = m_scene.SelectedObject()) {
            const Transform* transform = m_scene.Registry().TryGet<Transform>(selected->entity);
            const MeshRenderer* renderer = m_scene.Registry().TryGet<MeshRenderer>(selected->entity);
            std::snprintf(line, sizeof(line), "Name: %s", selected->name.c_str());
            m_text->Text(line, static_cast<float>(width) - 330.0f, 106.0f, 1.2f, muted);
            std::snprintf(line, sizeof(line), "Type: %s",
                selected->modelAssetPath.empty()
                    ? (selected->primitive == EditorScene::Primitive::Plane ? "Plane" : "Cube")
                    : "Model");
            m_text->Text(line, static_cast<float>(width) - 330.0f, 134.0f, 1.2f, muted);
            std::snprintf(line, sizeof(line), "Visible: %s", selected->visible ? "yes" : "no");
            m_text->Text(line, static_cast<float>(width) - 330.0f, 162.0f, 1.2f, muted);
            std::snprintf(line, sizeof(line), "Locked: %s", selected->locked ? "yes" : "no");
            m_text->Text(line, static_cast<float>(width) - 330.0f, 190.0f, 1.2f, muted);
            std::snprintf(line, sizeof(line), "Model: %s",
                selected->modelAssetPath.empty() ? "-" : selected->modelAssetPath.c_str());
            m_text->Text(line, static_cast<float>(width) - 330.0f, 218.0f, 1.05f, muted);
            std::snprintf(line, sizeof(line), "Material: %s",
                selected->materialAssetPath.empty() ? "-" : selected->materialAssetPath.c_str());
            m_text->Text(line, static_cast<float>(width) - 330.0f, 240.0f, 1.05f, muted);
            std::snprintf(line, sizeof(line), "Velocity: %.2f, %.2f, %.2f",
                selected->linearVelocity.x, selected->linearVelocity.y, selected->linearVelocity.z);
            m_text->Text(line, static_cast<float>(width) - 330.0f, 262.0f, 1.05f, muted);
            std::snprintf(line, sizeof(line), "Spin: %.2f around %.1f, %.1f, %.1f",
                selected->angularVelocityRadians,
                selected->angularVelocityAxis.x, selected->angularVelocityAxis.y, selected->angularVelocityAxis.z);
            m_text->Text(line, static_cast<float>(width) - 330.0f, 284.0f, 1.05f, muted);
            if (transform) {
                std::snprintf(line, sizeof(line), "Position: %.2f, %.2f, %.2f",
                    transform->position.x, transform->position.y, transform->position.z);
                m_text->Text(line, static_cast<float>(width) - 330.0f, 314.0f, 1.2f, muted);
                std::snprintf(line, sizeof(line), "Scale: %.2f, %.2f, %.2f",
                    transform->scale.x, transform->scale.y, transform->scale.z);
                m_text->Text(line, static_cast<float>(width) - 330.0f, 342.0f, 1.2f, muted);
                std::snprintf(line, sizeof(line), "Rotation: %.2f, %.2f, %.2f, %.2f",
                    transform->rotation.w, transform->rotation.x, transform->rotation.y, transform->rotation.z);
                m_text->Text(line, static_cast<float>(width) - 330.0f, 370.0f, 1.2f, muted);
            }
            if (renderer) {
                std::snprintf(line, sizeof(line), "Color: %.2f, %.2f, %.2f",
                    renderer->color.r, renderer->color.g, renderer->color.b);
                m_text->Text(line, static_cast<float>(width) - 330.0f, 398.0f, 1.2f, muted);
            }
        }
    }

    std::snprintf(line, sizeof(line), "Gizmo: %s %s   < / > or right-drag",
        m_gizmo.ModeName(), m_gizmo.AxisName());
    m_text->Text(line, static_cast<float>(width) - 330.0f, 40.0f, 1.15f, accent);

    if (!dockspaceDrawn && m_panels.IsOpen(EditorPanels::Panel::Console)) {
        DrawLogOverlay(static_cast<float>(width) - 330.0f, 348.0f, text, muted);
    }

    std::snprintf(line, sizeof(line), "Status: %s", m_log.LatestMessage().c_str());
    m_text->Text(line, 24.0f, static_cast<float>(height) - 62.0f, 1.2f, accent);
    m_text->Text("F1-F4 panels   mouse/keyboard drag   G gizmo   F5 save   F7 export   F8 validate",
        24.0f, static_cast<float>(height) - 34.0f, 1.2f, muted);

    m_text->End();
}

void EditorApp::DrawAssetOverlay(float x, float y, const glm::vec3 & text, const glm::vec3 & muted)
{
    m_text->Text("Content", x, y, 1.45f, text);

    char line[180];
    std::snprintf(line, sizeof(line), "%s/%s  (%zu files)",
        m_assets.RootPath().c_str(),
        m_assets.CurrentFolder().empty() ? "" : m_assets.CurrentFolder().c_str(),
        m_assets.TotalFileCount());
    m_text->Text(line, x + 6.0f, y + 32.0f, 1.05f, muted);
    m_text->Text("Enter: open/use  Ctrl+C/V: copy/paste  Del: delete  U: up",
        x + 6.0f, y + 52.0f, 0.9f, muted);

    const std::vector<EditorAssets::Folder>& folders = m_assets.Folders();
    const std::vector<EditorAssets::Asset>& assets = m_assets.Assets();
    const int maxVisible = 8;
    int row = 0;
    for (int i = 0; i < static_cast<int>(folders.size()) && row < maxVisible; ++i, ++row) {
        const EditorAssets::Folder& folder = folders[static_cast<std::size_t>(i)];
        std::snprintf(line, sizeof(line), "%s[Folder]  %s",
            (m_assets.SelectedType() == EditorAssets::SelectionType::Folder
                && i == m_assets.SelectedFolderIndex()) ? ">" : " ",
            folder.displayName.c_str());
        m_text->Text(line, x + 6.0f, y + 78.0f + static_cast<float>(row) * 22.0f, 1.05f,
            (m_assets.SelectedType() == EditorAssets::SelectionType::Folder
                && i == m_assets.SelectedFolderIndex()) ? text : muted);
    }
    for (int i = 0; i < static_cast<int>(assets.size()) && row < maxVisible; ++i, ++row) {
        const EditorAssets::Asset& asset = assets[static_cast<std::size_t>(i)];
        std::snprintf(line, sizeof(line), "%s%s  %s",
            (m_assets.SelectedType() == EditorAssets::SelectionType::Asset
                && i == m_assets.SelectedIndex()) ? ">" : " ",
            EditorAssets::TypeName(asset.type), asset.displayName.c_str());
        m_text->Text(line, x + 6.0f, y + 78.0f + static_cast<float>(row) * 22.0f, 1.05f, muted);
    }

    if (const EditorAssets::Asset* selected = m_assets.SelectedAsset()) {
        std::snprintf(line, sizeof(line), "Selected: %s", selected->displayName.c_str());
        m_text->Text(line, x + 6.0f, y + 248.0f, 1.05f, text);
        std::snprintf(line, sizeof(line), "Type: %s", EditorAssets::TypeName(selected->type));
        m_text->Text(line, x + 6.0f, y + 272.0f, 1.05f, muted);
    } else if (const EditorAssets::Folder* selectedFolder = m_assets.SelectedFolder()) {
        std::snprintf(line, sizeof(line), "Selected: %s", selectedFolder->displayName.c_str());
        m_text->Text(line, x + 6.0f, y + 248.0f, 1.05f, text);
        m_text->Text("Type: Folder", x + 6.0f, y + 272.0f, 1.05f, muted);
    }

    if (m_assets.HasCopiedEntry()) {
        std::snprintf(line, sizeof(line), "Copied: %s", m_assets.CopiedDisplayName().c_str());
        m_text->Text(line, x + 6.0f, y + 292.0f, 1.05f, muted);
    }

    if (m_dragDrop.HasPayload()) {
        const EditorDragDrop::Payload& payload = m_dragDrop.CurrentPayload();
        if (payload.mouseDriven) {
            std::snprintf(line, sizeof(line), "Dragging: %s  @ %.0f, %.0f",
                payload.path.c_str(), payload.cursorX, payload.cursorY);
        } else {
            std::snprintf(line, sizeof(line), "Dragging: %s", payload.path.c_str());
        }
        m_text->Text(line, x + 6.0f, y + 320.0f, 1.05f, text);
    }
}

void EditorApp::HandleGlobalShortcuts(engine::Window & window)
{
    if (Pressed(GLFW_KEY_ESCAPE)) {
       window.SetShouldClose(true);
    }
   if (Pressed(GLFW_KEY_F11)) {
       window.ToggleFullscreen();
    }
   if (Pressed(GLFW_KEY_F1)) {
       TogglePanel(EditorPanels::Panel::Hierarchy);
    }
   if (Pressed(GLFW_KEY_F2)) {
       TogglePanel(EditorPanels::Panel::Inspector);
    }
   if (Pressed(GLFW_KEY_F3)) {
       TogglePanel(EditorPanels::Panel::Assets);
    }
   if (Pressed(GLFW_KEY_F4)) {
       TogglePanel(EditorPanels::Panel::Console);
    }
   if (Pressed(GLFW_KEY_TAB)) {
       m_scene.SelectNext();
    }
   if (Pressed(GLFW_KEY_BACKSPACE)) {
       m_scene.SelectPrevious();
    }
   if (Pressed(GLFW_KEY_P)) {
       if (m_mode == EditorMode::Edit) {
           EnterPlayMode();
       } else {
           ExitPlayMode();
       }
    }
   if (Pressed(GLFW_KEY_M)) {
       m_mouseLookPinned = !m_mouseLookPinned;
    }
}

void EditorApp::UpdateMouseCapture(engine::Window & window)
{
    const bool rightMouseDown = window.Native()
        && glfwGetMouseButton(window.Native(), GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    const bool rightMousePressed = rightMouseDown && !m_rightMouseLookPrev;
    if (!rightMouseDown) {
        m_rightMouseLookActive = false;
    } else if (rightMousePressed && m_mode == EditorMode::Edit && window.Native()) {
        double cursorX = 0.0;
        double cursorY = 0.0;
        glfwGetCursorPos(window.Native(), &cursorX, &cursorY);
        m_rightMouseLookActive = IsViewportDropPosition(static_cast<float>(cursorX), static_cast<float>(cursorY));
    }
    m_rightMouseLookPrev = rightMouseDown;
    const bool shouldMouseLook = m_mouseLookPinned || m_rightMouseLookActive;
    if (m_mouseLook != shouldMouseLook) {
        m_mouseLook = shouldMouseLook;
        window.SetCursorCaptured(m_mouseLook);
    }
    const bool middleMouseDown = window.Native()
        && glfwGetMouseButton(window.Native(), GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS;
    const bool middleMousePressed = middleMouseDown && !m_middleMousePanPrev;
    if (!middleMouseDown || m_mouseLook) {
        m_middleMousePanActive = false;
    } else if (middleMousePressed && m_mode == EditorMode::Edit && window.Native()) {
        double cursorX = 0.0;
        double cursorY = 0.0;
        glfwGetCursorPos(window.Native(), &cursorX, &cursorY);
        m_middleMousePanActive = IsViewportDropPosition(static_cast<float>(cursorX), static_cast<float>(cursorY));
    }
    m_middleMousePanPrev = middleMouseDown;
}

void EditorApp::HandleAssetShortcuts(engine::Window & window, bool controlDown)
{
    if (m_mode == EditorMode::Edit && Pressed(GLFW_KEY_F5)) {
        SaveScene();
    }
    if (m_mode == EditorMode::Edit && Pressed(GLFW_KEY_F7)) {
        ExportRuntimeScene();
    }
    if (m_mode == EditorMode::Edit && Pressed(GLFW_KEY_F8)) {
        ValidateRuntimeScene();
    }
    if (m_mode == EditorMode::Edit && Pressed(GLFW_KEY_F9)) {
        LoadScene();
    }
    if (Pressed(GLFW_KEY_F6)) {
        RefreshAssets();
    }
    if (m_mode == EditorMode::Edit && Pressed(GLFW_KEY_U)) {
        std::string error;
        if (m_assets.GoUp(&error)) {
            m_log.Info("Content folder: " + (m_assets.CurrentFolder().empty() ? std::string("/") : m_assets.CurrentFolder()));
        } else {
            m_log.Error(error);
        }
    }
    if (Pressed(GLFW_KEY_LEFT_BRACKET)) {
        m_assets.SelectPrevious();
    }
    if (Pressed(GLFW_KEY_RIGHT_BRACKET)) {
        m_assets.SelectNext();
    }
    if (m_mode == EditorMode::Edit && m_panels.IsOpen(EditorPanels::Panel::Assets)
        && controlDown && Pressed(GLFW_KEY_C)) {
        CopyContentEntry();
    }
    if (m_mode == EditorMode::Edit && m_panels.IsOpen(EditorPanels::Panel::Assets)
        && controlDown && Pressed(GLFW_KEY_V)) {
        PasteContentEntry();
    }
    if (controlDown && Pressed(GLFW_KEY_ENTER)) {
        BeginAssetDrag();
    } else
    if (m_mode == EditorMode::Edit && Pressed(GLFW_KEY_ENTER)) {
        UseSelectedAsset();
    }
    if (m_mode == EditorMode::Edit && Pressed(GLFW_KEY_V)) {
        DropPayloadOnScene();
    }
    if (Pressed(GLFW_KEY_BACKSLASH)) {
        m_dragDrop.Clear();
        m_log.Info("Drag/drop payload cleared");
    }
}

void EditorApp::HandleEditorCommandShortcuts(engine::Window & window, bool controlDown)
{
    if (controlDown && Pressed(GLFW_KEY_Z)) {
        Undo();
    }
    if (controlDown && Pressed(GLFW_KEY_Y)) {
        Redo();
    }
    if (controlDown && Pressed(GLFW_KEY_L)) {
        m_log.Clear();
    }
    if (m_mode == EditorMode::Edit && Pressed(GLFW_KEY_N)) {
        AddCube();
    }
    if (m_mode == EditorMode::Edit && Pressed(GLFW_KEY_B)) {
        AddPlane();
    }
    if (m_mode == EditorMode::Edit && Pressed(GLFW_KEY_C)) {
        CycleSelectedColor();
    }
    if (m_mode == EditorMode::Edit && Pressed(GLFW_KEY_H)) {
        ToggleSelectedVisible();
    }
    if (m_mode == EditorMode::Edit && Pressed(GLFW_KEY_O)) {
        ToggleSelectedLocked();
    }
    if (m_mode == EditorMode::Edit && Pressed(GLFW_KEY_1)) {
        SetSelectedPrimitive(EditorScene::Primitive::Cube);
    }
    if (m_mode == EditorMode::Edit && Pressed(GLFW_KEY_2)) {
        SetSelectedPrimitive(EditorScene::Primitive::Plane);
    }
    if (m_mode == EditorMode::Edit && !m_mouseLook && Pressed(GLFW_KEY_D)) {
        DuplicateSelected();
    }
    if (m_mode == EditorMode::Edit && Pressed(GLFW_KEY_DELETE)) {
        if (m_panels.IsOpen(EditorPanels::Panel::Assets)
            && m_assets.SelectedType() != EditorAssets::SelectionType::None) {
            DeleteContentEntry();
        } else {
            DeleteSelected();
        }
    }
    if (m_mode == EditorMode::Edit && Pressed(GLFW_KEY_R)) {
        m_scene.ResetSelectedTransform();
        m_log.Info("Reset selected transform");
    }
    if (m_mode == EditorMode::Edit && Pressed(GLFW_KEY_G)) {
        m_gizmo.CycleMode();
        m_log.Info(std::string("Gizmo mode: ") + m_gizmo.ModeName());
    }
    if (m_mode == EditorMode::Edit && Pressed(GLFW_KEY_X)) {
        m_gizmo.SetAxis(EditorGizmo::Axis::X);
        m_log.Info("Gizmo axis: X");
    }
    if (m_mode == EditorMode::Edit && Pressed(GLFW_KEY_Y)) {
        m_gizmo.SetAxis(EditorGizmo::Axis::Y);
        m_log.Info("Gizmo axis: Y");
    }
    if (m_mode == EditorMode::Edit && Pressed(GLFW_KEY_Z)) {
        m_gizmo.SetAxis(EditorGizmo::Axis::Z);
        m_log.Info("Gizmo axis: Z");
    }
}

void EditorApp::UpdateCameraControls(engine::Window & window, float dt)
{
    const float cameraSpeed = (window.IsKeyPressed(GLFW_KEY_LEFT_SHIFT) ? 12.0f : 5.0f) * dt;
    if (m_mode == EditorMode::Edit && window.Native()) {
        double cursorX = 0.0;
        double cursorY = 0.0;
        glfwGetCursorPos(window.Native(), &cursorX, &cursorY);
        const float scrollY = window.ScrollDeltaY();
        if (scrollY != 0.0f
            && IsViewportDropPosition(static_cast<float>(cursorX), static_cast<float>(cursorY))) {
            const float zoomSpeed = window.IsKeyPressed(GLFW_KEY_LEFT_SHIFT) ? 2.0f : 1.0f;
            m_camera.MoveForward(scrollY * zoomSpeed);
        }
    }
    if (m_mouseLook) {
        m_camera.AddYawPitch(window.MouseDeltaX() * 0.1f, -window.MouseDeltaY() * 0.1f);
        if (window.IsKeyPressed(GLFW_KEY_W)) m_camera.MoveForward(cameraSpeed);
        if (window.IsKeyPressed(GLFW_KEY_S)) m_camera.MoveForward(-cameraSpeed);
        if (window.IsKeyPressed(GLFW_KEY_D)) m_camera.MoveRight(cameraSpeed);
        if (window.IsKeyPressed(GLFW_KEY_A)) m_camera.MoveRight(-cameraSpeed);
        if (window.IsKeyPressed(GLFW_KEY_SPACE)) m_camera.MoveUp(cameraSpeed);
        if (window.IsKeyPressed(GLFW_KEY_LEFT_CONTROL)) m_camera.MoveUp(-cameraSpeed);
    } else if (m_middleMousePanActive) {
        const float panSpeed = window.IsKeyPressed(GLFW_KEY_LEFT_SHIFT) ? 0.04f : 0.02f;
        m_camera.MoveRight(-window.MouseDeltaX() * panSpeed);
        m_camera.MoveUp(window.MouseDeltaY() * panSpeed);
    }
}

void EditorApp::UpdateSelectedTransformShortcuts(engine::Window & window, float dt)
{
    const float objectSpeed = 2.0f * dt;
    if (m_mode == EditorMode::Edit) {
        const bool transformEditing = IsTransformEditActive(window);
        if (transformEditing && !m_wasTransformEditing) {
            m_scene.BeginTransformEdit();
        }

        if (window.IsKeyPressed(GLFW_KEY_LEFT)) m_scene.MoveSelected(glm::vec3(-objectSpeed, 0.0f, 0.0f));
        if (window.IsKeyPressed(GLFW_KEY_RIGHT)) m_scene.MoveSelected(glm::vec3(objectSpeed, 0.0f, 0.0f));
        if (window.IsKeyPressed(GLFW_KEY_UP)) m_scene.MoveSelected(glm::vec3(0.0f, 0.0f, -objectSpeed));
        if (window.IsKeyPressed(GLFW_KEY_DOWN)) m_scene.MoveSelected(glm::vec3(0.0f, 0.0f, objectSpeed));
        if (window.IsKeyPressed(GLFW_KEY_Q)) m_scene.MoveSelected(glm::vec3(0.0f, objectSpeed, 0.0f));
        if (window.IsKeyPressed(GLFW_KEY_E)) m_scene.MoveSelected(glm::vec3(0.0f, -objectSpeed, 0.0f));
        if (window.IsKeyPressed(GLFW_KEY_J)) m_scene.RotateSelectedYaw(-90.0f * dt);
        if (window.IsKeyPressed(GLFW_KEY_L)) m_scene.RotateSelectedYaw(90.0f * dt);
        if (window.IsKeyPressed(GLFW_KEY_EQUAL) || window.IsKeyPressed(GLFW_KEY_KP_ADD)) {
            m_scene.ScaleSelectedAxis(m_gizmo.AxisVector(), 1.0f + dt);
        }
        if (window.IsKeyPressed(GLFW_KEY_MINUS) || window.IsKeyPressed(GLFW_KEY_KP_SUBTRACT)) {
            m_scene.ScaleSelectedAxis(m_gizmo.AxisVector(), 1.0f - dt);
        }
        if (window.IsKeyPressed(GLFW_KEY_COMMA)) {
            ApplyGizmoNudge(-1.0f, dt);
        }
        if (window.IsKeyPressed(GLFW_KEY_PERIOD)) {
            ApplyGizmoNudge(1.0f, dt);
        }

        if (!transformEditing && m_wasTransformEditing) {
            m_scene.EndTransformEdit();
        }
        m_wasTransformEditing = transformEditing;
    } else if (m_wasTransformEditing) {
        m_scene.EndTransformEdit();
        m_wasTransformEditing = false;
    }
}

void EditorApp::DrawPlayScene(const glm::mat4 & viewProj)
{
    m_shader->Bind();
    m_shader->SetMat4("uViewProj", viewProj);
    m_shader->SetVec3("uLightDir", glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f)));

    if (m_modelShader) {
        m_modelShader->Bind();
        m_modelShader->SetMat4("uViewProj", viewProj);
        m_modelShader->SetVec3("uLightPos", m_camera.Position() + glm::vec3(-4.0f, 6.0f, 4.0f));
        m_modelShader->SetVec3("uLightColor", glm::vec3(1.0f));
        m_modelShader->SetVec3("uViewPos", m_camera.Position());
        engine::ecs::RenderLoadedModels(*m_playRegistry, *m_modelShader);
    }

    m_shader->Bind();
    m_shader->SetMat4("uViewProj", viewProj);
    m_shader->SetVec3("uLightDir", glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f)));
    engine::ecs::RenderMeshes(*m_playRegistry, m_renderer, *m_shader);
}

void EditorApp::DrawEditScene(const glm::mat4 & viewProj)
{
    m_shader->Bind();
    m_shader->SetMat4("uViewProj", viewProj);
    m_shader->SetVec3("uLightDir", glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f)));

    m_scene.Registry().view<Transform, MeshRenderer>().each(
        [&](Entity entity, Transform& transform, MeshRenderer& renderer) {
            if (!m_scene.IsVisible(entity)) {
                return;
            }
            const std::vector<EditorScene::Object>& objects = m_scene.Objects();
            const auto objectIt = std::find_if(objects.begin(), objects.end(),
                [&](const EditorScene::Object& object) { return object.entity == entity; });
            if (objectIt != objects.end() && !objectIt->modelAssetPath.empty()) {
                return;
            }
            const EditorScene::Object* selectedObject = m_scene.SelectedObject();
            const bool selected = selectedObject && selectedObject->entity == entity;
            MeshRenderer drawRenderer = renderer;
            drawRenderer.color = SelectedColor(selected, renderer.color);
            const engine::Texture* diffuseTexture = nullptr;
            if (objectIt != objects.end() && !objectIt->materialAssetPath.empty()) {
                std::string error;
                diffuseTexture = m_editAssets.LoadTexture(objectIt->materialAssetPath, &error);
                if (!diffuseTexture && !m_editTextureLoadErrors[objectIt->materialAssetPath]) {
                    m_editTextureLoadErrors[objectIt->materialAssetPath] = true;
                    m_log.Error("Texture preview failed: " + error);
                }
            }
            DrawSceneObject(transform, drawRenderer, diffuseTexture);
        });
    DrawEditModeModels(viewProj);
    DrawSelectionOutline(viewProj);
    DrawSceneGizmo(viewProj);
}

void EditorApp::DrawLogOverlay(float x, float y, const glm::vec3 & text, const glm::vec3 & muted)
{
    m_text->Text("Console", x, y, 1.45f, text);

    const std::vector<EditorLog::Entry>& entries = m_log.Entries();
    const int maxVisible = 7;
    const int first = static_cast<int>(entries.size()) > maxVisible
        ? static_cast<int>(entries.size()) - maxVisible
        : 0;

    char line[180];
    float rowY = y + 32.0f;
    for (int i = first; i < static_cast<int>(entries.size()); ++i) {
        const EditorLog::Entry& entry = entries[static_cast<std::size_t>(i)];
        std::snprintf(line, sizeof(line), "%s  %s",
            EditorLog::LevelName(entry.level), entry.message.c_str());
        m_text->Text(line, x + 6.0f, rowY, 1.05f, muted);
        rowY += 22.0f;
    }
}

void EditorApp::ApplyGizmoNudge(float direction, float dt)
{
    const float amount = direction * dt;
    const glm::vec3 axis = m_gizmo.AxisVector();

    switch (m_gizmo.CurrentMode()) {
    case EditorGizmo::Mode::Translate:
        m_scene.MoveSelected(axis * (amount * 2.0f));
        break;
    case EditorGizmo::Mode::Rotate:
        m_scene.RotateSelected(axis, amount * 90.0f);
        break;
    case EditorGizmo::Mode::Scale:
        m_scene.ScaleSelectedAxis(axis, 1.0f + amount);
        break;
    }
}

void EditorApp::ApplyGizmoDrag(float pixels)
{
    const float amount = pixels * 0.01f;
    const glm::vec3 axis = m_gizmo.AxisVector();

    switch (m_gizmo.CurrentMode()) {
    case EditorGizmo::Mode::Translate:
        m_scene.MoveSelected(axis * amount);
        break;
    case EditorGizmo::Mode::Rotate:
        m_scene.RotateSelected(axis, pixels * 0.35f);
        break;
    case EditorGizmo::Mode::Scale:
        {
            float factor = 1.0f + amount * 0.25f;
            if (factor < 0.05f) {
                factor = 0.05f;
            }
            m_scene.ScaleSelectedAxis(axis, factor);
        }
        break;
    }
}

void EditorApp::TogglePanel(EditorPanels::Panel panel)
{
    m_panels.Toggle(panel);
    m_log.Info(std::string(EditorPanels::Name(panel)) +
        (m_panels.IsOpen(panel) ? " panel shown" : " panel hidden"));
}

void EditorApp::HandleMouseAssetDrag()
{
    engine::Window& window = GetWindow();
    if (m_mouseLook || m_mode != EditorMode::Edit || !window.Native()) {
        m_leftMousePrev = false;
        return;
    }

    double cursorX = 0.0;
    double cursorY = 0.0;
    glfwGetCursorPos(window.Native(), &cursorX, &cursorY);

    const bool leftDown = glfwGetMouseButton(window.Native(), GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    const bool leftPressed = leftDown && !m_leftMousePrev;
    const bool leftReleased = !leftDown && m_leftMousePrev;
    const float x = static_cast<float>(cursorX);
    const float y = static_cast<float>(cursorY);

    if (leftPressed && m_panels.IsOpen(EditorPanels::Panel::Assets)) {
        const int assetIndex = AssetIndexAtPosition(x, y);
        if (assetIndex >= 0) {
            m_assets.SelectIndex(assetIndex);
            if (const EditorAssets::Asset* asset = m_assets.SelectedAsset()) {
                m_dragDrop.BeginAssetDragAt(AssetFullPath(*asset), EditorAssets::TypeName(asset->type), x, y);
                m_log.Info("Mouse dragging asset " + asset->relativePath);
            }
        }
    }

    if (leftDown && m_dragDrop.IsMouseDriven()) {
        m_dragDrop.UpdateCursor(x, y);
    }

    if (leftReleased && m_dragDrop.IsMouseDriven()) {
        m_dragDrop.UpdateCursor(x, y);
        if (IsViewportDropPosition(x, y)) {
            DropPayloadOnScene();
        } else {
            m_dragDrop.Clear();
            m_log.Info("Mouse drag cancelled");
        }
    }

    m_leftMousePrev = leftDown;
}

void EditorApp::HandleMouseViewportSelection()
{
    engine::Window& window = GetWindow();
    if (m_mouseLook || m_mode != EditorMode::Edit || !window.Native()) {
        if (m_mouseGizmoActive && m_mouseGizmoButton == GLFW_MOUSE_BUTTON_LEFT) {
            m_scene.EndTransformEdit();
        }
        m_mouseGizmoActive = m_mouseGizmoButton == GLFW_MOUSE_BUTTON_LEFT ? false : m_mouseGizmoActive;
        m_mouseGizmoButton = m_mouseGizmoButton == GLFW_MOUSE_BUTTON_LEFT ? -1 : m_mouseGizmoButton;
        m_viewportLeftMousePrev = false;
        return;
    }

    double cursorX = 0.0;
    double cursorY = 0.0;
    glfwGetCursorPos(window.Native(), &cursorX, &cursorY);

    const bool leftDown = glfwGetMouseButton(window.Native(), GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    const bool leftPressed = leftDown && !m_viewportLeftMousePrev;
    const bool leftReleased = !leftDown && m_viewportLeftMousePrev;
    const float x = static_cast<float>(cursorX);
    const float y = static_cast<float>(cursorY);

    const bool activeLeftGizmo = m_mouseGizmoActive && m_mouseGizmoButton == GLFW_MOUSE_BUTTON_LEFT;
    if (m_dragDrop.HasPayload() || (!IsViewportDropPosition(x, y) && !activeLeftGizmo)) {
        m_viewportLeftMousePrev = leftDown;
        return;
    }

    const glm::mat4 viewProj = m_camera.ProjectionMatrix(window.AspectRatio()) * m_camera.ViewMatrix();

    if (leftPressed) {
        if (PickGizmoHandle(x, y, viewProj, window.Width(), window.Height())) {
            m_mouseGizmoActive = true;
            m_mouseGizmoButton = GLFW_MOUSE_BUTTON_LEFT;
            m_mouseGizmoLastX = x;
            m_mouseGizmoLastY = y;
            m_scene.BeginTransformEdit();
            m_log.Info(std::string("Mouse gizmo: ") + m_gizmo.ModeName() + " " + m_gizmo.AxisName());
        } else {
            const int picked = PickSceneObject(x, y, viewProj, window.Width(), window.Height());
            if (picked >= 0) {
                m_scene.SelectIndex(picked);
                if (const EditorScene::Object* selected = m_scene.SelectedObject()) {
                    m_log.Info("Selected " + selected->name);
                }
            } else if (m_scene.SelectedObject()) {
                m_scene.Deselect();
                m_log.Info("Deselected object");
            }
        }
    }

    if (leftDown && m_mouseGizmoActive && m_mouseGizmoButton == GLFW_MOUSE_BUTTON_LEFT) {
        const float dx = x - m_mouseGizmoLastX;
        const float dy = y - m_mouseGizmoLastY;
        const float pixels = m_gizmo.CurrentAxis() == EditorGizmo::Axis::Y ? -dy : dx;

        if (pixels != 0.0f) {
            ApplyGizmoDrag(pixels);
        }

        m_mouseGizmoLastX = x;
        m_mouseGizmoLastY = y;
    }

    if (leftReleased && m_mouseGizmoActive && m_mouseGizmoButton == GLFW_MOUSE_BUTTON_LEFT) {
        m_scene.EndTransformEdit();
        m_mouseGizmoActive = false;
        m_mouseGizmoButton = -1;
        m_log.Info("Mouse gizmo edit complete");
    }

    m_viewportLeftMousePrev = leftDown;
}

void EditorApp::HandleMouseViewportGizmo()
{
    engine::Window& window = GetWindow();
    if (m_mouseLook || m_mode != EditorMode::Edit || !window.Native()) {
        if (m_mouseGizmoActive) {
            m_scene.EndTransformEdit();
        }
        m_mouseGizmoActive = false;
        m_mouseGizmoButton = -1;
        m_rightMousePrev = false;
        return;
    }

    double cursorX = 0.0;
    double cursorY = 0.0;
    glfwGetCursorPos(window.Native(), &cursorX, &cursorY);

    const bool rightDown = glfwGetMouseButton(window.Native(), GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    const bool rightPressed = rightDown && !m_rightMousePrev;
    const bool rightReleased = !rightDown && m_rightMousePrev;
    const float x = static_cast<float>(cursorX);
    const float y = static_cast<float>(cursorY);

    if (rightPressed
        && !m_mouseGizmoActive
        && !m_dragDrop.HasPayload()
        && m_scene.SelectedObject()
        && !m_scene.SelectedLocked()
        && IsViewportDropPosition(x, y)) {
        m_mouseGizmoActive = true;
        m_mouseGizmoButton = GLFW_MOUSE_BUTTON_RIGHT;
        m_mouseGizmoLastX = x;
        m_mouseGizmoLastY = y;
        m_scene.BeginTransformEdit();
        m_log.Info(std::string("Mouse gizmo: ") + m_gizmo.ModeName() + " " + m_gizmo.AxisName());
    }

    if (rightDown && m_mouseGizmoActive && m_mouseGizmoButton == GLFW_MOUSE_BUTTON_RIGHT) {
        const float dx = x - m_mouseGizmoLastX;
        const float dy = y - m_mouseGizmoLastY;
        const float pixels = m_gizmo.CurrentAxis() == EditorGizmo::Axis::Y ? -dy : dx;

        if (pixels != 0.0f) {
            ApplyGizmoDrag(pixels);
        }

        m_mouseGizmoLastX = x;
        m_mouseGizmoLastY = y;
    }

    if (rightReleased && m_mouseGizmoActive && m_mouseGizmoButton == GLFW_MOUSE_BUTTON_RIGHT) {
        m_scene.EndTransformEdit();
        m_mouseGizmoActive = false;
        m_mouseGizmoButton = -1;
        m_log.Info("Mouse gizmo edit complete");
    }

    m_rightMousePrev = rightDown;
}

bool EditorApp::ProjectWorldToScreen(const glm::vec3& world, const glm::mat4 viewProj,
                                     int width, int height, glm::vec2* screen) const
{
    const glm::vec4 clip = viewProj * glm::vec4(world, 1.0f);
    if (clip.w <= 0.0f) {
        return false;
    }
    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    if (ndc.z < -1.0f || ndc.z > 1.0f) {
        return false;
    }
    screen->x = (ndc.x * 0.5f + 0.5f) * static_cast<float>(width);
    screen->y = (1.0f - (ndc.y * 0.5f + 0.5f)) * static_cast<float>(height);
    return true;
}

int EditorApp::PickSceneObject(float x, float y, const glm::mat4 & viewProj, int width, int height) const
{
    PickRay ray;
    if (!BuildPickRay(x, y, viewProj, width, height, &ray)) {
        return -1;
    }

    int picked = -1;
    float bestDistance = std::numeric_limits<float>::max();

    const std::vector<EditorScene::Object>& objects = m_scene.Objects();
    for (int i = 0; i < static_cast<int>(objects.size()); ++i) {
        const EditorScene::Object& object = objects[static_cast<std::size_t>(i)];
        if (!object.visible) {
            continue;
        }

        const Transform* transform = m_scene.TryGetTransform(object.entity);
        if (!transform) {
            continue;
        }
        
        const glm::mat4 inverseModel = glm::inverse(transform->Model());
        const PickRay localRay = TransformRayToLocal(ray, inverseModel);
        float hitDistance = 0.0f;
        bool hit = false;
        if (!object.modelAssetPath.empty()) {
            const engine::Model* model = m_editAssets.FindModel(object.modelAssetPath);
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
                hit = IntersectLocalAabb(localRay,
                    glm::vec3(-0.5f),
                    glm::vec3(0.5f),
                    &hitDistance);
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

bool EditorApp::PickGizmoHandle(float x, float y, const glm::mat4 & viewProj, int width, int height)
{
    const Transform* selectedTransform = m_scene.SelectedTransform();
    if (!selectedTransform || m_scene.SelectedLocked()) {
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
        glm::vec3 axisVector;
        switch (axis) {
        case EditorGizmo::Axis::X: axisVector = glm::vec3(1.0f, 0.0f, 0.0f); break;
        case EditorGizmo::Axis::Y: axisVector = glm::vec3(0.0f, 1.0f, 0.0f); break;
        case EditorGizmo::Axis::Z: axisVector = glm::vec3(0.0f, 0.0f, 1.0f); break;
        }

        glm::vec2 endScreen;
        if (!ProjectWorldToScreen(center + axisVector * axisLength, viewProj, width, height, &endScreen)) {
            return false;
        }

        return DistanceToSegmentSquared(mouse, centerScreen, endScreen) <= maxDistance * maxDistance;
    };

    switch (m_gizmo.CurrentMode()) {
    case EditorGizmo::Mode::Translate:
        if (testAxisSegment(EditorGizmo::Axis::X, length + head * 1.6f, 18.0f)) {
                        m_gizmo.SetAxis(EditorGizmo::Axis::X);
            return true;
        }
        if (testAxisSegment(EditorGizmo::Axis::Y, length + head * 1.6f, 18.0f)) {
            m_gizmo.SetAxis(EditorGizmo::Axis::Y);
            return true;
        }
        if (testAxisSegment(EditorGizmo::Axis::Z, length + head * 1.6f, 18.0f)) {
            m_gizmo.SetAxis(EditorGizmo::Axis::Z);
            return true;
        }
        break;

    case EditorGizmo::Mode::Scale:
        if (testAxisSegment(EditorGizmo::Axis::X, length, 18.0f)) {
            m_gizmo.SetAxis(EditorGizmo::Axis::X);
            return true;
        }
        if (testAxisSegment(EditorGizmo::Axis::Y, length, 18.0f)) {
            m_gizmo.SetAxis(EditorGizmo::Axis::Y);
            return true;
        }
        if (testAxisSegment(EditorGizmo::Axis::Z, length, 18.0f)) {
            m_gizmo.SetAxis(EditorGizmo::Axis::Z);
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
                    glm::vec3 offset;
                    switch (axis) {
                    case EditorGizmo::Axis::X:
                        offset = glm::vec3(0.0f, std::cos(t) * rotateRadius, std::sin(t) * rotateRadius);
                        break;
                                        case EditorGizmo::Axis::Y:
                        offset = glm::vec3(std::cos(t) * rotateRadius, 0.0f, std::sin(t) * rotateRadius);
                        break;
                    case EditorGizmo::Axis::Z:
                        offset = glm::vec3(std::cos(t) * rotateRadius, std::sin(t) * rotateRadius, 0.0f);
                        break;
                    }

                    glm::vec2 screen;
                    if (!ProjectWorldToScreen(center + offset, viewProj, width, height, &screen)) {
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
                    m_gizmo.SetAxis(axis);
                    return true;
                }
            }
        }
        break;
    }

    return false;
}

void EditorApp::BeginAssetDrag()
{
    const EditorAssets::Asset* asset = m_assets.SelectedAsset();
    if (!asset) {
        m_log.Warning("No asset selected for drag");
        return;
    }

    m_dragDrop.BeginAssetDrag(AssetFullPath(*asset), EditorAssets::TypeName(asset->type));
    m_log.Info("Dragging asset " + asset->relativePath);
}

void EditorApp::DropPayloadOnScene()
{
    if (!m_dragDrop.HasPayload()) {
        m_log.Warning("No drag/drop payload to drop");
        return;
    }

    const EditorDragDrop::Payload payload = m_dragDrop.CurrentPayload();
    if (payload.type != EditorDragDrop::PayloadType::Asset) {
        m_log.Warning("Unsurpported drag/drop payload");
        m_dragDrop.Clear();
        return;
    }

    if (payload.typeName == "Scene") {
        m_project.SetScenePath(payload.path);
        LoadScene();
        m_dragDrop.Clear();
        return;
    }

    if (payload.typeName == "Model") {
        std::string error;
        const engine::Model* model = m_editAssets.LoadModel(payload.path, &error);
        if (!model) {
            m_log.Error("Model drop failed: " + error);
            m_dragDrop.Clear();
            return;
        }

        Transform transform;
        transform.position = SceneDropPosition();
        const float radius = std::max(model->BoundingRadius(), 0.001f);
        const float targetRadius = 0.8f;
        const float uniformScale = targetRadius / radius;
        transform.scale = glm::vec3(uniformScale);

        if (m_cube && m_scene.AddModel(payload.path, *m_cube, transform)) {
            m_editModelLoadErrors.erase(payload.path);
            m_log.Info("Added model object: " + payload.path);
        } else {
            m_log.Warning("Model drop failed: could not create scene object");
        }
    } else if (payload.typeName == "Texture") {
        std::string error;
        const engine::Texture* texture = m_editAssets.LoadTexture(payload.path, &error);
        if (!texture) {
            m_log.Error("Texture drop failed: " + error);
            m_dragDrop.Clear();
            return;
        }

        if (m_scene.SetSelectedMaterialAsset(payload.path)) {
            m_editTextureLoadErrors.erase(payload.path);
            m_log.Info("Assigned material texture to selected object");
        } else {
            m_log.Warning("Texture drop failed: select an unlocked object first");
        }
    } else {
        m_log.Warning("Asset type cannot be dropped on the scene yet");
    }
    m_dragDrop.Clear();
}

glm::vec3 EditorApp::SceneDropPosition()
{
    const engine::Window& window = GetWindow();
    const glm::mat4 viewProj = m_camera.ProjectionMatrix(window.AspectRatio()) * m_camera.ViewMatrix();

    double cursorX = 0.0;
    double cursorY = 0.0;
    glfwGetCursorPos(window.Native(), &cursorX, &cursorY);

    PickRay ray;
    if (!BuildPickRay(static_cast<float>(cursorX), static_cast<float>(cursorY),
            viewProj, window.Width(), window.Height(), &ray)) {
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

void EditorApp::RefreshAssets()
{
    std::string error;
    if (m_assets.Refresh(m_project.AssetRoot(), &error)) {
        m_log.Info("Scanned assets: " + std::to_string(m_assets.Assets().size()) + "files");
    } else {
        m_log.Error(error);
    }
}

void EditorApp::CtreateContentFolder(const std::string & name)
{
    std::string error;
    if (m_assets.CreateFolder(name, &error)) {
        m_log.Info("Created Content folder: " + name);
    } else {
        m_log.Error(error);
    }
}

void EditorApp::ImportContentAsset(const std::string & sourcePath)
{
    std::string error;
    if (m_assets.ImportAsset(sourcePath, &error)) {
        m_log.Info("Imported asset: " + sourcePath);
    } else {
        m_log.Error(error);
    }
}

void EditorApp::CopyContentEntry()
{
    std::string error;
    if (m_assets.CopySelected(&error)) {
        m_log.Info("Copied Content entry: " + m_assets.CopiedDisplayName());
    } else {
        m_log.Warning(error);
    }
}

void EditorApp::PasteContentEntry()
{
    std::string error;
    if (m_assets.PasteCopied(&error)) {
        m_log.Info("Pasted Content entry");
    } else {
        m_log.Warning(error);
    }
}

void EditorApp::DeleteContentEntry()
{
    std::string error;
    if (m_assets.DeleteSelectedEntry(&error)) {
        m_log.Info("Deleted Content entry");
    } else {
        m_log.Warning(error);
    }
}

void EditorApp::UseSelectedAsset()
{
    const EditorAssets::Asset* asset = m_assets.SelectedAsset();
    if (!asset) {
        m_log.Warning("No asset selected");
        return;
    }

    if (asset->type != EditorAssets::Type::Scene) {
        m_log.Warning("Selected asset is not a scene");
        return;
    }

    m_project.SetScenePath(AssetFullPath(*asset));
    LoadScene();
}

std::string EditorApp::AssetFullPath(const EditorAssets::Asset & asset) const
{
    return (std::filesystem::path(m_assets.RootPath()) / asset.relativePath).string();
}

float EditorApp::AssetPanelTop() const
{
    float y = 120.0f;
    if (m_panels.IsOpen(EditorPanels::Panel::Hierarchy)) {
        y += static_cast<float>(m_scene.Objects().size()) * 26.0f;
    }
    return y + 28.0f;
}

int EditorApp::FolderIndexAtPosition(float x, float y) const
{
    if (x < 30.0f || x > 430.0f) {
        return -1;
    }

    const float rowTop = AssetPanelTop() + 78.0f;
    const float rowHeight = 22.0f;
    if (y < rowTop) {
        return -1;
    }

    const int row = static_cast<int>((y - rowTop) / rowHeight);
    const int maxVisible = 8;
    if (row < 0 || row >= maxVisible || row >= static_cast<int>(m_assets.Folders().size())) {
        return -1;
    }

    return row;
}

int EditorApp::AssetIndexAtPosition(float x, float y) const
{
    if (x < 30.0f || x > 430.0f) {
        return -1;
    }

    const float rowTop = AssetPanelTop() + 56.0f;
    const float rowHeight = 22.0f;
    if (y < rowTop) {
        return -1;
    }

    const int index = static_cast<int>((y - rowTop) / rowHeight);
    const int maxVisible = 8;
    if (index < 0 || index >= maxVisible || index >= static_cast<int>(m_assets.Assets().size())) {
        return -1;
    }

    return index;
}

bool EditorApp::IsViewportDropPosition(float x, float y)
{
    const engine::Window& window = GetWindow();
    return x > 380.0f
        && x < static_cast<float>(window.Width()) - 360.0f
        && y > 70.0f
        && y < static_cast<float>(window.Height()) - 90.0f;
}

void EditorApp::AddCube()
{
    if (!m_cube) {
        m_log.Error("Add failed: cube mesh is not ready");
        return;
    }

    m_scene.AddCube(*m_cube);
    m_log.Info("Added cube");
}

void EditorApp::AddPlane()
{
    if (!m_plane) {
        m_log.Error("Add failed: plane mesh is not ready");
        return;
    }

    m_scene.AddPlane(*m_plane);
    m_log.Info("Added plane");
}

void EditorApp::CycleSelectedColor()
{
    if (m_scene.CycleSelectedColor()) {
        m_log.Info("Changed selected color");
    } else {
        m_log.Warning("Color change failed: no selected object");
    }
}

void EditorApp::SetSelectedPrimitive(EditorScene::Primitive primitive)
{
    if (!m_cube || !m_plane) {
        m_log.Error("Type change failed: editor meshes are not ready");
        return;
    }

    const engine::Mesh& mesh = primitive == EditorScene::Primitive::Cube ? *m_cube : *m_plane;
    if (m_scene.SetSelectedPrimitive(primitive, mesh)) {
        m_log.Info(primitive == EditorScene::Primitive::Cube
            ? "Changed selected type to cube"
            : "Changed selected type to plane");
    } else {
        m_log.Warning("Type change skipped");
    }
}

void EditorApp::ToggleSelectedVisible()
{
    if (m_scene.ToggleSelectVisible()) {
        const EditorScene::Object* selected = m_scene.SelectedObject();
        m_log.Info(selected && selected->visible ? "Selected object shown" : "Selected object hidden");
    } else {
        m_log.Warning("Visibility change failed: no selected object");
    }
}

void EditorApp::ToggleSelectedLocked()
{
    if (m_scene.ToggleSelectedLocked()) {
        const EditorScene::Object* selected = m_scene.SelectedObject();
        m_log.Info(selected && selected->locked ? "Selected object locked" : "Selected object unlocked");
    } else {
        m_log.Warning("Lock change failed: no selected object");
    }
}

void EditorApp::DuplicateSelected()
{
    if (!m_cube || !m_plane) {
        m_log.Error("Duplicate failed: editor meshes are not ready");
        return;
    }

    if (m_scene.DuplicateSelected(*m_cube, *m_plane)) {
        m_log.Info("Duplicated selected object");
    } else {
        m_log.Warning("Duplicate failed: no selected object");
    }
}

void EditorApp::DeleteSelected()
{
    if (m_scene.DeleteSelected()) {
        m_log.Info("Deleted selected object");
    } else {
        m_log.Warning("Delete failed: no selected object");
    }
}

void EditorApp::Undo()
{
    if (!m_cube || !m_plane) {
        m_log.Error("Undo failed: editor meshes are not ready");
        return;
    }

    if (m_scene.Undo(*m_cube, *m_plane)) {
        m_log.Info("Undo");
    } else {
        m_log.Warning("Nothing to undo");
    }
}

void EditorApp::Redo()
{
    if (!m_cube || !m_plane) {
    m_log.Error("Redo failed: editor meshes are not ready");
    return;
    }

    if (m_scene.Redo(*m_cube, *m_plane)) {
        m_log.Info("Redo");
    } else {
        m_log.Warning("Nothing to redo");
    }
}

void EditorApp::SaveScene()
{
    std::string error;
    if (m_scene.Save(m_project.ScenePath(), &error)) {
        m_log.Info("Saved " + m_project.ScenePath());
    } else {
        m_log.Error("Save failed: " + error);
    }
}

void EditorApp::LoadScene()
{
    if (!m_cube || !m_plane) {
        m_log.Error("Load failed: editor meshes are not ready");
        return;
    }

    std::string error;
    if (m_scene.Load(m_project.ScenePath(), *m_cube, *m_plane, &error)) {
        m_log.Info("Loaded " + m_project.ScenePath()); 
    } else {
        m_log.Error("Load failed: " + error);
    }
}

void EditorApp::ExportRuntimeScene()
{
    std::filesystem::path exportPath(m_project.ScenePath());
    exportPath.replace_extension(".runtime.scene");

    std::string error;
    if (RuntimeSceneExporter::Export(m_scene, exportPath.string(), &error)) {
        m_log.Info("Exported runtime scene " + exportPath.string());
    } else {
        m_log.Error("Runtime export failed: " + error);
    }
}

void EditorApp::ValidateRuntimeScene()
{
    std::filesystem::path runtimePath(m_project.ScenePath());
    runtimePath.replace_extension(".runtime.scene");

    engine::RuntimeSceneLoader::Scene runtimeScene;
    std::string error;
    if (!engine::RuntimeSceneLoader::Load(runtimePath.string(), &runtimeScene, &error)) {
        m_log.Error("Runtime scene validation failed: " + error);
    }

    if (!m_cube || !m_plane) {
        m_log.Error("Runtime scene validation failed: editor primitive meshes are not ready");
        return;
    }

    engine::ecs::Registry registry;
    engine::RuntimeSceneLoader::PrimitiveMeshes meshes{&*m_cube, &*m_plane};
    if (!engine::RuntimeSceneLoader::Instantiate(runtimeScene, registry, meshes, nullptr, &error)) {
        m_log.Error("Runtime scene validation failed: " + error);
        return;
    }

    engine::RuntimeAssetManager assets;
    const engine::RuntimeAssetManager::ResolveReport report = assets.ResolveRegistryAssets(registry);
    if (!report.errors.empty()) {
        m_log.Error("Runtime asset validation failed: " + report.errors.front());
        return;
    }

    m_log.Info("Validated runtime scene: "
        + std::to_string(runtimeScene.entities.size()) + " entities, "
        + std::to_string(report.modelsAssigned) + " models, "
        + std::to_string(report.texturesAssigned) + " tectures, "
        + std::to_string(ComponentCount<engine::ecs::LinearVelocity>(registry)) + " linear, "
        + std::to_string(ComponentCount<engine::ecs::AngularVelocity>(registry)) + " angular");
}

void EditorApp::EnterPlayMode()
{
    m_editSnapshot = m_scene.CreateSnapshot();
    std::string error;
    if (!BuildPlayRuntimePreview(&error)) {
        m_editSnapshot.reset();
        m_mode = EditorMode::Edit;
        m_log.Error("Play mode failed: " + error);
        return;
    }

    const std::size_t linearCount = m_playRegistry
        ? ComponentCount<engine::ecs::LinearVelocity>(*m_playRegistry)
        : 0;
    const std::size_t angularCount = m_playRegistry
        ? ComponentCount<engine::ecs::AngularVelocity>(*m_playRegistry)
        : 0;

    m_mode = EditorMode::Play;
    m_log.Info("Play mode: runtime preview loaded, "
        + std::to_string(linearCount) + " linear, "
        + std::to_string(angularCount) + " angular");
}

void EditorApp::ExitPlayMode()
{
    m_playRegistry.reset();
    m_playAssets.reset();

    if (!m_cube || !m_plane) {
        m_log.Error("Could not restore edit scene");
        m_mode = EditorMode::Edit;
        m_editSnapshot.reset();
        return;
    }

    if (m_editSnapshot) {
        m_scene.RestoreFromSnapshot(*m_editSnapshot, *m_cube, *m_plane);
    }
    m_editSnapshot.reset();
    m_mode = EditorMode::Edit;
    m_log.Info("Edit mode: restored scene from before Play");
}

bool EditorApp::BuildPlayRuntimePreview(std::string * error)
{
    if (!m_cube || !m_plane) {
        if (error) {
            *error = "editor primitive meshes are not ready";
        }
        return false;
    }

    std::filesystem::path runtimePath(m_project.ScenePath());
    runtimePath.replace_extension(".runtime.scene");

    if (!RuntimeSceneExporter::Export(m_scene, runtimePath.string(), error)) {
        return false;
    }

    engine::RuntimeSceneLoader::Scene runtimeScene;
    if (!engine::RuntimeSceneLoader::Load(runtimePath.string(), &runtimeScene, error)) {
        return false;
    }

    m_playRegistry.emplace();
    m_playAssets.emplace();

    engine::RuntimeSceneLoader::PrimitiveMeshes meshes{&*m_cube, &*m_plane};
    if (!engine::RuntimeSceneLoader::Instantiate(runtimeScene, *m_playRegistry, meshes, nullptr, error)) {
        m_playRegistry.reset();
        m_playAssets.reset();
        return false;
    }

    const engine::RuntimeAssetManager::ResolveReport report = m_playAssets->ResolveRegistryAssets(*m_playRegistry);
    if (!report.errors.empty()) {
        if (error) {
            *error = report.errors.front();
        }
        m_playRegistry.reset();
        m_playAssets.reset();
        return false;
    }

    if (error) {
        *error = {};
    }
    return true;
}

bool EditorApp::IsTransformEditActive(const engine::Window &window) const
{
    return window.IsKeyPressed(GLFW_KEY_LEFT)
        || window.IsKeyPressed(GLFW_KEY_RIGHT)
        || window.IsKeyPressed(GLFW_KEY_UP)
        || window.IsKeyPressed(GLFW_KEY_DOWN)
        || window.IsKeyPressed(GLFW_KEY_Q)
        || window.IsKeyPressed(GLFW_KEY_E)
        || window.IsKeyPressed(GLFW_KEY_J)
        || window.IsKeyPressed(GLFW_KEY_L)
        || window.IsKeyPressed(GLFW_KEY_EQUAL)
        || window.IsKeyPressed(GLFW_KEY_KP_ADD)
        || window.IsKeyPressed(GLFW_KEY_MINUS)
        || window.IsKeyPressed(GLFW_KEY_KP_SUBTRACT)
        || window.IsKeyPressed(GLFW_KEY_COMMA)
        || window.IsKeyPressed(GLFW_KEY_PERIOD);
}

bool EditorApp::Pressed(int key)
{
    const bool down = GetWindow().IsKeyPressed(key);
    const bool was = m_keyPrev[key];
    m_keyPrev[key] = down;
    return down && !was;
}
