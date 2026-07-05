#include "EditorApp.h"

#include <engine/ecs/Registry.h>
#include <engine/ecs/RuntimeSystems.h>
#include <engine/ecs/Systems.h>
#include <engine/graphics/Model.h>
#include <engine/graphics/Primitives.h>
#include <engine/graphics/Texture.h>

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <filesystem>

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

struct PhysicsRuntimeStats {
    std::size_t rigidBodies = 0;
    std::size_t dynamicBodies = 0;
    std::size_t colliders = 0;
    std::size_t staticColliders = 0;
    std::size_t triggerColliders = 0;
    std::size_t dynamicBodiesWithoutCollider = 0;
    std::size_t invalidColliders = 0;
};

bool RuntimeColliderShapeIsInvalid(const engine::ecs::Collider& collider) {
    switch (collider.shape) {
    case engine::ecs::ColliderShape::Sphere:
        return collider.radius <= 0.0f;
    case engine::ecs::ColliderShape::Box:
        return collider.halfExtents.x <= 0.0f
            || collider.halfExtents.y <= 0.0f
            || collider.halfExtents.z <= 0.0f;
    case engine::ecs::ColliderShape::Plane:
        return glm::dot(collider.planeNormal, collider.planeNormal) <= 0.0001f;
    }

    return true;
}

PhysicsRuntimeStats CollectPhysicsRuntimeStats(engine::ecs::Registry& registry) {
    PhysicsRuntimeStats stats;

    if (engine::ecs::Pool<engine::ecs::RigidBody>* bodies = registry.TryPool<engine::ecs::RigidBody>()) {
        stats.rigidBodies = bodies->dense.size();
        for (const engine::ecs::Entity entity : bodies->dense) {
            const engine::ecs::RigidBody& body = bodies->Get(entity);
            if (body.invMass > 0.0f) {
                ++stats.dynamicBodies;
                if (!registry.Has<engine::ecs::Collider>(entity)) {
                    ++stats.dynamicBodiesWithoutCollider;
                }
            }
        }
    }

    if (engine::ecs::Pool<engine::ecs::Collider>* colliders = registry.TryPool<engine::ecs::Collider>()) {
        stats.colliders = colliders->dense.size();
        for (const engine::ecs::Entity entity : colliders->dense) {
            const engine::ecs::Collider& collider = colliders->Get(entity);
            if (collider.isTrigger) {
                ++stats.triggerColliders;
            }
            if (RuntimeColliderShapeIsInvalid(collider)) {
                ++stats.invalidColliders;
            }

            const engine::ecs::RigidBody* body = registry.TryGet<engine::ecs::RigidBody>(entity);
            if (!body || body->invMass <= 0.0f) {
                ++stats.staticColliders;
            }
        }
    }

    return stats;
}

const char* CollisionPhaseName(engine::CollisionEvent::Phase phase) {
    switch (phase) {
    case engine::CollisionEvent::Phase::Enter: return "Enter";
    case engine::CollisionEvent::Phase::Stay: return "Stay";
    case engine::CollisionEvent::Phase::Exit: return "Exit";
    }

    return "Event";
}

bool IsMaterialDocumentPath(const std::string& path) {
    return std::filesystem::path(path).extension() == ".3dgmat";
}

bool IsEditorKeyboardCaptured() {
    const ImGuiIO& io = ImGui::GetIO();
    return io.WantTextInput || io.WantCaptureKeyboard;
}

float LightEmissiveScale(const EditorScene& scene, const EditorScene::Object& object) {
    if (!object.light) {
        return 1.0f;
    }

    const engine::ecs::Light* light = scene.TryGetLight(object.entity);
    const float intensity = light ? light->intensity : object.lightData.intensity;
    return 1.0f + std::max(intensity, 0.0f) * 0.1f;
}

engine::ecs::Light EnvironmentSunLight(const engine::DayNightCycle::Sample& sky, float intensityScale) {
    engine::ecs::Light light;
    light.type = engine::ecs::Light::Type::Directional;
    light.direction = sky.keyLightDirection;

    const glm::vec3 radiance = sky.keyLightColor * std::max(intensityScale, 0.0f);
    light.intensity = std::max(std::max(radiance.r, radiance.g), radiance.b);
    light.color = light.intensity > 0.0001f ? radiance / light.intensity : glm::vec3(1.0f);
    return light;
}

void AddEnvironmentSunIfNeeded(engine::ecs::Registry& registry,
                               const EditorScene::Environment& environment,
                               const engine::DayNightCycle::Sample& sky,
                               bool alreadyApplied) {
    if (!environment.driveSunLight || alreadyApplied) {
        return;
    }

    const Entity entity = registry.Create();
    registry.Add<Transform>(entity, Transform{});
    registry.Add<engine::ecs::Light>(entity, EnvironmentSunLight(sky, environment.sunIntensity));
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
    m_sphere.emplace(engine::primitives::Sphere());
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
            uniform vec3 uEmissive;
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
                vec3 ambient = base * 0.24;
                vec3 lit = ambient + base * diffuse * 0.76;
                FragColor = vec4(lit + uEmissive, 1.0);
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
    m_pbrRenderer.emplace();
    m_sky.emplace();
    m_text.emplace();

    m_project.Load(m_config);
    SetScenePathDraft(m_project.ScenePath());
    m_content.Refresh(m_assets, m_project, m_log);
    m_materialMaker.SetOutputDirectory(m_project.AssetRoot());
    m_scene.BuildDefault(*m_cube, *m_plane, *m_sphere);
    m_imgui.Init(GetWindow());
}

void EditorApp::OnUpdate(float dt)
{
    engine::Window& window = GetWindow();
    m_elapsed += dt;
    m_dt = dt;
    if (dt > 0.0f) {
        m_fps = m_fps * 0.92f + (1.0f / dt) * 0.08f;
    }
    StepPlayPhysics(dt);
    UpdateAutosave(dt);

    const bool keyboardCaptured = IsEditorKeyboardCaptured();
    if (keyboardCaptured) {
        for (auto& keyState : m_keyPrev) {
            keyState.second = window.IsKeyPressed(keyState.first);
        }
    } else {
        HandleGlobalShortcuts(window);
    }

    m_cameraController.UpdateMouseCapture(window,
        m_mode == EditorMode::Edit,
        [&](float x, float y) { return IsViewportDropPosition(x, y); });
    HandleMouseAssetDrag();
    HandleMouseViewportSelection();
    HandleMouseViewportGizmo();

    const bool controlDown = window.IsKeyPressed(GLFW_KEY_LEFT_CONTROL)
        || window.IsKeyPressed(GLFW_KEY_RIGHT_CONTROL);
    if (!keyboardCaptured) {
        HandleAssetShortcuts(window, controlDown);
        HandleEditorCommandShortcuts(window, controlDown);
    }
    m_cameraController.UpdateCamera(window,
        m_camera,
        m_mode == EditorMode::Edit && !keyboardCaptured,
        dt,
        [&](float x, float y) { return IsViewportDropPosition(x, y); });
    m_transformController.UpdateKeyboardShortcuts(window,
        m_scene,
        m_gizmo,
        m_mode == EditorMode::Edit && !keyboardCaptured && !m_cameraController.MouseLookActive() && !controlDown,
        dt);
}

void EditorApp::OnRender()
{
    const engine::Window& window = GetWindow();
    const glm::mat4 viewProj = m_camera.ProjectionMatrix(window.AspectRatio()) * m_camera.ViewMatrix();
    const EditorScene::Environment& environment = m_scene.GetEnvironment();
    const bool useHdrPost = environment.ssr;

    if (useHdrPost) {
        if (!m_postProcess) {
            m_postProcess.emplace(window.Width(), window.Height());
            m_postProcess->settings.bloom = false;
            m_postProcess->settings.autoExposure = false;
            m_postProcess->settings.exposure = 1.0f;
        }
        if (!m_ssr) {
            m_ssr.emplace(window.Width(), window.Height());
        }
        m_postProcess->Resize(window.Width(), window.Height());
        m_ssr->Resize(window.Width(), window.Height());
        m_postProcess->BeginScene();
        m_renderer.Clear();
        m_renderingHdrPreview = true;
    } else {
        m_renderer.Clear();
    }

    if (m_mode == EditorMode::Play && m_playRegistry) {
        DrawPlayScene(viewProj);
    } else {
        DrawEditScene(viewProj);
    }

    if (useHdrPost && m_postProcess) {
        if (m_ssr && m_ssao) {
            m_ssr->intensity = environment.ssrIntensity;
            m_ssr->Apply(m_postProcess->HdrColor(), m_ssao->PositionTexture(), m_ssao->NormalTexture(),
                         m_camera.ProjectionMatrix(window.AspectRatio()), m_postProcess->HdrFbo(),
                         window.Width(), window.Height());
        }
        m_renderingHdrPreview = false;
        m_postProcess->RenderToScreen(window.Width(), window.Height(), m_dt);
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

        m_viewport.DrawLoadedModel(*m_modelShader, *transform, *model);
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
            m_viewport.DrawSelectedModelOutline(m_renderer, *m_outlineShader, *transform, *model, color, 0.045f);
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
    m_viewport.DrawSelectedMeshOutline(m_renderer, *m_outlineShader, *transform, *renderer->mesh, color, 0.045f);
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
    dockspaceContext.playMode = m_mode == EditorMode::Play;
    dockspaceContext.physicsPaused = m_physicsPaused;
    dockspaceContext.physicsFixedTimestep = m_physicsFixedTimestep;
    dockspaceContext.physicsAccumulator = m_physicsAccumulator;
    dockspaceContext.physicsStepsLastFrame = m_physicsStepsLastFrame;
    dockspaceContext.physicsEventEnterCount = m_physicsEventEnterCount;
    dockspaceContext.physicsEventStayCount = m_physicsEventStayCount;
    dockspaceContext.physicsEventExitCount = m_physicsEventExitCount;
    dockspaceContext.physicsEventRows = &m_physicsEventRows;
    dockspaceContext.showPhysicsEventGuides = &m_showPhysicsEventGuides;
    dockspaceContext.physicsEventGuidesSelectedOnly = &m_physicsEventGuidesSelectedOnly;
    dockspaceContext.physicsEventGuidesTriggersOnly = &m_physicsEventGuidesTriggersOnly;
    dockspaceContext.physicsEventGuidesEnterExitOnly = &m_physicsEventGuidesEnterExitOnly;
    dockspaceContext.scenePathBuffer = m_scenePathDraft.data();
    dockspaceContext.scenePathBufferSize = m_scenePathDraft.size();
    dockspaceContext.fps = m_fps;
    dockspaceContext.sceneDirty = m_scene.IsDirty();
    const bool dockspaceDrawn = m_dockspace.Draw(dockspaceContext);
    DrawMaterialMakerPanel();
    DrawDirtyScenePrompt();
    if (dockspaceContext.viewportDropRequested) {
        DropPayloadOnScene();
    }
    if (dockspaceContext.newSceneRequested) {
        RequestNewScene();
    }
    if (dockspaceContext.enterPlayModeRequested && m_mode == EditorMode::Edit) {
        EnterPlayMode();
    }
    if (dockspaceContext.exitPlayModeRequested && m_mode == EditorMode::Play) {
        ExitPlayMode();
    }
    if (dockspaceContext.undoRequested) {
        Undo();
    }
    if (dockspaceContext.redoRequested) {
        Redo();
    }
    if (dockspaceContext.saveSceneRequested) {
        SaveScene();
    }
    if (dockspaceContext.saveAsSceneRequested) {
        SaveSceneAs(m_scenePathDraft.data());
    }
    if (dockspaceContext.loadSceneRequested) {
        RequestLoadSceneFromPath(m_scenePathDraft.data());
    }
    if (dockspaceContext.recentSceneRequested >= 0
        && dockspaceContext.project
        && dockspaceContext.recentSceneRequested < static_cast<int>(dockspaceContext.project->RecentScenes().size())) {
        RequestLoadSceneFromPath(dockspaceContext.project->RecentScenes()[static_cast<std::size_t>(dockspaceContext.recentSceneRequested)]);
    }
    if (dockspaceContext.exportRuntimeRequested) {
        ExportRuntimeScene();
    }
    if (dockspaceContext.validateRuntimeRequested) {
        ValidateRuntimeScene();
    }
    if (dockspaceContext.physicsPauseToggleRequested && m_mode == EditorMode::Play) {
        m_physicsPaused = !m_physicsPaused;
        m_log.Info(m_physicsPaused ? "Play physics paused" : "Play physics resumed");
    }
    if (dockspaceContext.physicsStepRequested && m_mode == EditorMode::Play) {
        m_physicsStepRequested = true;
        if (!m_physicsPaused) {
            m_physicsPaused = true;
            m_log.Info("Play physics paused for single-step");
        }
    }
    if (dockspaceContext.clearPhysicsEventGuidesRequested) {
        m_physicsEventRows.clear();
        m_physicsEventGuides.clear();
        m_physicsEventEnterCount = 0;
        m_physicsEventStayCount = 0;
        m_physicsEventExitCount = 0;
    }
    if (dockspaceContext.addCubeRequested) {
        AddCube();
    }
    if (dockspaceContext.addPlaneRequested) {
        AddPlane();
    }
    if (dockspaceContext.addSphereRequested) {
        AddSphere();
    }
    if (dockspaceContext.addDynamicCubeRequested) {
        AddDynamicCube();
    }
    if (dockspaceContext.addStaticFloorRequested) {
        AddStaticFloor();
    }
    if (dockspaceContext.addTriggerVolumeRequested) {
        AddTriggerVolume();
    }
    if (m_sphere) {
        if (dockspaceContext.addDirectionalLightRequested) {
            m_scene.AddDirectionalLight(*m_sphere);
            m_log.Info("Added directional light");
        }
        if (dockspaceContext.addPointLightRequested) {
            m_scene.AddPointLight(*m_sphere);
            m_log.Info("Added point light");
        }
        if (dockspaceContext.addSpotLightRequested) {
            m_scene.AddSpotLight(*m_sphere);
            m_log.Info("Added spot light");
        }
        if (dockspaceContext.addAreaLightRequested) {
            m_scene.AddAreaLight(*m_cube);
            m_log.Info("Added area light");
        }
    }
    if (dockspaceContext.duplicateSelectedRequested) {
        DuplicateSelected();
    }
    if (dockspaceContext.deleteSelectedRequested) {
        DeleteSelected();
    }
    if (dockspaceContext.frameSelectedRequested) {
        FrameSelected();
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

void EditorApp::DrawMaterialMakerPanel() {
    if (!m_panels.IsOpen(EditorPanels::Panel::MaterialMaker)) {
        return;
    }

    bool open = true;
    if (ImGui::Begin(EditorPanels::Name(EditorPanels::Panel::MaterialMaker), &open)) {
        const bool materialSaved = m_materialMaker.DrawContent();
        ImGui::Separator();
        DrawMaterialMakerTools(materialSaved);
    }
    ImGui::End();
    m_panels.SetOpen(EditorPanels::Panel::MaterialMaker, open);
}

void EditorApp::DrawMaterialMakerTools(bool materialSaved) {
    if (materialSaved) {
        m_content.Refresh(m_assets, m_project, m_log);
    }

    if (materialSaved) {
        ImGui::Text("Saved: %s", m_materialMaker.LastSavedPath().c_str());
    }

    if (ImGui::Button("Save and Apply")) {
        if (m_materialMaker.SaveCurrent()) {
            m_content.Refresh(m_assets, m_project, m_log);
            if (m_scene.SetSelectedMaterialAsset(m_materialMaker.LastSavedPath())) {
                m_editTextureLoadErrors.erase(m_materialMaker.LastSavedPath());
                m_log.Info("Applied saved material to selected object");
            } else {
                m_log.Warning("Material apply failed: select an unlocked object first");
            }
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Apply Saved")) {
        if (m_materialMaker.LastSavedPath().empty()) {
            m_log.Warning("Material apply failed: save the material first");
        } else if (m_scene.SetSelectedMaterialAsset(m_materialMaker.LastSavedPath())) {
            m_editTextureLoadErrors.erase(m_materialMaker.LastSavedPath());
            m_log.Info("Applied saved material to selected object");
        } else {
            m_log.Warning("Material apply failed: select an unlocked object first");
        }
    }

    const EditorAssets::Asset* selectedAsset = m_assets.SelectedAsset();
    const bool selectedTexture = selectedAsset && selectedAsset->type == EditorAssets::Type::Texture;
    if (selectedTexture) {
        const std::string texturePath = m_content.AssetFullPath(m_assets, *selectedAsset);
        if (ImGui::Button("Copy Selected Texture Path")) {
            ImGui::SetClipboardText(texturePath.c_str());
            m_log.Info("Copied selected texture path");
        }
        ImGui::SameLine();
        if (ImGui::Button("Use as Albedo")) {
            m_materialMaker.SetAlbedoMap(texturePath);
        }
        ImGui::SameLine();
        if (ImGui::Button("Use as Normal")) {
            m_materialMaker.SetNormalMap(texturePath);
        }
        ImGui::SameLine();
        if (ImGui::Button("Use as Metal/Rough")) {
            m_materialMaker.SetMetalRoughMap(texturePath);
        }
    }

    const bool selectedMaterial = selectedAsset && selectedAsset->type == EditorAssets::Type::Material;
    if (selectedMaterial && ImGui::Button("Load Selected Material")) {
        const std::string materialPath = m_content.AssetFullPath(m_assets, *selectedAsset);
        if (m_materialMaker.LoadFromFile(materialPath)) {
            m_log.Info("Loaded material into Material Maker");
        } else {
            m_log.Warning(m_materialMaker.StatusMessage());
        }
    }
}

void EditorApp::DrawDirtyScenePrompt() {
    if (m_pendingSceneAction == PendingSceneAction::None) {
        return;
    }

    if (m_dirtyScenePromptQueued) {
        ImGui::OpenPopup("Unsaved Scene");
        m_dirtyScenePromptQueued = false;
    }

    const ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::BeginPopupModal("Unsaved Scene", nullptr, flags)) {
        ImGui::TextUnformatted("The current scene has unsaved changes.");
        if (m_pendingSceneAction == PendingSceneAction::LoadScene && !m_pendingScenePath.empty()) {
            ImGui::Text("Next scene: %s", m_pendingScenePath.c_str());
        }
        ImGui::Separator();

        if (ImGui::Button("Save", ImVec2(92.0f, 0.0f))) {
            SaveScene();
            if (!m_scene.IsDirty()) {
                ImGui::CloseCurrentPopup();
                CompletePendingSceneAction();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard", ImVec2(92.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
            CompletePendingSceneAction();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(92.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
            CancelPendingSceneAction();
        }

        ImGui::EndPopup();
    }
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
       RequestCloseEditor();
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
    if (Pressed(GLFW_KEY_F10)) {
        TogglePanel(EditorPanels::Panel::MaterialMaker);
    }
    if (Pressed(GLFW_KEY_F12)) {
        TogglePanel(EditorPanels::Panel::PhysicsStatus);
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
       m_cameraController.TogglePinnedMouseLook();
    }
}

void EditorApp::HandleAssetShortcuts(engine::Window& window, bool controlDown)
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
    if (Pressed(GLFW_KEY_F6)) {
        m_content.Refresh(m_assets, m_project, m_log);
    }
    if (m_mode == EditorMode::Edit && Pressed(GLFW_KEY_U)) {
        m_content.GoUp(m_assets, m_log);
    }
    if (Pressed(GLFW_KEY_LEFT_BRACKET)) {
        m_assets.SelectPrevious();
    }
    if (Pressed(GLFW_KEY_RIGHT_BRACKET)) {
        m_assets.SelectNext();
    }
    if (m_mode == EditorMode::Edit && m_panels.IsOpen(EditorPanels::Panel::Assets)
        && controlDown && Pressed(GLFW_KEY_C)) {
        const bool copied = m_content.CopyEntry(m_assets, m_log);
        const EditorAssets::Asset* selectedAsset = m_assets.SelectedAsset();
        if (copied && selectedAsset && selectedAsset->type == EditorAssets::Type::Texture) {
            const std::string texturePath = m_assets.SelectedAssetFullPath();
            ImGui::SetClipboardText(texturePath.c_str());
            m_log.Info("Texture path ready for Material Maker paste");
        }
    }
    if (m_mode == EditorMode::Edit && m_panels.IsOpen(EditorPanels::Panel::Assets)
        && controlDown && Pressed(GLFW_KEY_V)) {
        m_content.PasteEntry(m_assets, m_log);
    }
    if (controlDown && Pressed(GLFW_KEY_ENTER)) {
        BeginAssetDrag();
    } else
    if (m_mode == EditorMode::Edit && Pressed(GLFW_KEY_ENTER)) {
        if (m_content.UseSelectedAsset(m_assets, m_project, m_log)) {
            LoadScene();
        }
    }
    if (m_mode == EditorMode::Edit && Pressed(GLFW_KEY_V)) {
        DropPayloadOnScene();
    }
    if (Pressed(GLFW_KEY_BACKSLASH)) {
        m_dragDrop.Clear();
        m_log.Info("Drag/drop payload cleared");
    }
}

void EditorApp::HandleEditorCommandShortcuts(engine::Window& window, bool controlDown)
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
    if (m_mode == EditorMode::Edit && Pressed(GLFW_KEY_F)) {
        FrameSelected();
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
    if (m_mode == EditorMode::Edit && !m_cameraController.MouseLookActive() && controlDown && Pressed(GLFW_KEY_D)) {
        DuplicateSelected();
    }
    if (m_mode == EditorMode::Edit && Pressed(GLFW_KEY_DELETE)) {
        if (m_panels.IsOpen(EditorPanels::Panel::Assets)
            && m_assets.SelectedType() != EditorAssets::SelectionType::None) {
            m_content.DeleteEntry(m_assets, m_log);
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

void EditorApp::DrawPlayScene(const glm::mat4 & viewProj)
{
    const EditorScene::Environment& environment = m_scene.GetEnvironment();
    const engine::DayNightCycle::Sample sky = engine::DayNightCycle::At(environment.timeOfDay);
    if (m_sky) {
        const engine::Window& window = GetWindow();
        m_sky->Draw(m_camera.ViewMatrix(), m_camera.ProjectionMatrix(window.AspectRatio()), sky);
    }

    if (m_pbrRenderer && m_playRegistry) {
        engine::ecs::Registry pbrRegistry;
        bool environmentSunApplied = false;

        m_playRegistry->view<Transform, MeshRenderer>().each(
            [&](Entity source, Transform& transform, MeshRenderer& renderer) {
                if (m_playRegistry->Has<engine::ecs::LoadedModelAsset>(source) || !renderer.mesh) {
                    return;
                }

                engine::ecs::PbrMaterial material;
                material.albedo = renderer.color;
                material.roughness = 0.55f;
                if (engine::ecs::LoadedMaterialAsset* loaded = m_playRegistry->TryGet<engine::ecs::LoadedMaterialAsset>(source)) {
                    material = loaded->material;
                }

                const Entity entity = pbrRegistry.Create();
                pbrRegistry.Add<Transform>(entity, transform);
                pbrRegistry.Add<engine::ecs::MeshPBR>(entity, engine::ecs::MeshPBR{renderer.mesh, material});
                if (engine::ecs::Light* light = m_playRegistry->TryGet<engine::ecs::Light>(source)) {
                    engine::ecs::Light renderLight = *light;
                    if (environment.driveSunLight
                        && renderLight.type == engine::ecs::Light::Type::Directional
                        && !environmentSunApplied) {
                        renderLight = EnvironmentSunLight(sky, environment.sunIntensity);
                        environmentSunApplied = true;
                    }
                    pbrRegistry.Add<engine::ecs::Light>(entity, renderLight);
                }
            }
        );

        m_playRegistry->view<Transform, engine::ecs::Light>().each(
            [&](Entity source, Transform& transform, engine::ecs::Light& light) {
                if (m_playRegistry->Has<MeshRenderer>(source)) {
                    return;
                }
                const Entity entity = pbrRegistry.Create();
                pbrRegistry.Add<Transform>(entity, transform);
                engine::ecs::Light renderLight = light;
                if (environment.driveSunLight
                    && renderLight.type == engine::ecs::Light::Type::Directional
                    && !environmentSunApplied) {
                    renderLight = EnvironmentSunLight(sky, environment.sunIntensity);
                    environmentSunApplied = true;
                }
                pbrRegistry.Add<engine::ecs::Light>(entity, renderLight);
            }
        );
        AddEnvironmentSunIfNeeded(pbrRegistry, environment, sky, environmentSunApplied);

        engine::PbrRenderer::Options options;
        ConfigureEnvironmentPbrOptions(pbrRegistry, options, environment, sky);

        const engine::Window& window = GetWindow();
        m_pbrRenderer->Render(pbrRegistry, m_camera, window.AspectRatio(), window.Width(), window.Height(), options);
    }

    if (m_modelShader) {
        m_modelShader->Bind();
        m_modelShader->SetMat4("uViewProj", viewProj);
        m_modelShader->SetVec3("uLightPos", m_camera.Position() + glm::vec3(-4.0f, 6.0f, 4.0f));
        m_modelShader->SetVec3("uLightColor", glm::vec3(1.0f));
        m_modelShader->SetVec3("uViewPos", m_camera.Position());
        engine::ecs::RenderLoadedModels(*m_playRegistry, *m_modelShader);
    }
    if (m_shader && m_cube && m_showPhysicsEventGuides && !m_physicsEventGuides.empty()) {
        std::vector<EditorViewport::PhysicsEventGuide> visibleGuides;
        visibleGuides.reserve(m_physicsEventGuides.size());

        std::string selectedName;
        if (const EditorScene::Object* selected = m_scene.SelectedObject()) {
            selectedName = selected->name;
        }

        for (const EditorViewport::PhysicsEventGuide& guide : m_physicsEventGuides) {
            if (m_physicsEventGuidesSelectedOnly && (selectedName.empty()
                || (guide.objectA != selectedName && guide.objectB != selectedName))) {
                continue;
            }
            if (m_physicsEventGuidesTriggersOnly && !guide.trigger) {
                continue;
            }
            if (m_physicsEventGuidesEnterExitOnly && guide.phase == 1) {
                continue;
            }
            visibleGuides.push_back(guide);
        }

        if (!visibleGuides.empty()) {
            m_viewport.DrawPhysicsEventGuides(m_renderer, *m_shader, *m_cube, visibleGuides, viewProj);
        }
    }
}

void EditorApp::DrawEditScene(const glm::mat4 & viewProj)
{
    const EditorScene::Environment& environment = m_scene.GetEnvironment();
    const engine::DayNightCycle::Sample sky = engine::DayNightCycle::At(environment.timeOfDay);
    if (m_sky) {
        const engine::Window& window = GetWindow();
        m_sky->Draw(m_camera.ViewMatrix(), m_camera.ProjectionMatrix(window.AspectRatio()), sky);
    }

    if (m_pbrRenderer) {
        engine::ecs::Registry pbrRegistry;
        const std::vector<EditorScene::Object>& objects = m_scene.Objects();
        const EditorScene::Object* selectedObject = m_scene.SelectedObject();
        bool environmentSunApplied = false;

        for (const EditorScene::Object& object : objects) {
            if (!object.visible || !object.modelAssetPath.empty()) {
                continue;
            }

            const Transform* transform = m_scene.TryGetTransform(object.entity);
            const MeshRenderer* renderer = m_scene.TryGetMeshRenderer(object.entity);
            if (!transform || !renderer || !renderer->mesh) {
                continue;
            }

            engine::ecs::PbrMaterial material;
            material.albedo = renderer->color;
            material.roughness = object.light ? 0.24f : 0.55f;
            const bool selected = selectedObject && selectedObject->entity == object.entity;

            if (!object.materialAssetPath.empty()) {
                std::string error;
                if (IsMaterialDocumentPath(object.materialAssetPath)) {
                    const engine::RuntimeMaterialAsset* loaded = m_editAssets.LoadMaterial(object.materialAssetPath, &error);
                    if (loaded) {
                        material = loaded->material;
                        material.albedo = SelectedColor(selected, material.albedo);
                        material.emissive *= LightEmissiveScale(m_scene, object);
                        if (!loaded->albedoMapPath.empty()) {
                            material.albedoMap = m_editAssets.LoadTexture(loaded->albedoMapPath, &error);
                        }
                        if (!loaded->normalMapPath.empty()) {
                            material.normalMap = m_editAssets.LoadTexture(loaded->normalMapPath, &error);
                        }
                        if (!loaded->metalRoughMapPath.empty()) {
                            material.metalRoughMap = m_editAssets.LoadTexture(loaded->metalRoughMapPath, &error);
                        }
                    } else if (!m_editTextureLoadErrors[object.materialAssetPath]) {
                        m_editTextureLoadErrors[object.materialAssetPath] = true;
                        m_log.Error("Material preview failed: " + error);
                    }
                } else {
                    material.albedoMap = m_editAssets.LoadTexture(object.materialAssetPath, &error);
                    if (!material.albedoMap && !m_editTextureLoadErrors[object.materialAssetPath]) {
                        m_editTextureLoadErrors[object.materialAssetPath] = true;
                        m_log.Error("Texture preview failed: " + error);
                    }
                    material.albedo = SelectedColor(selected, material.albedo);
                }
            } else {
                material.albedo = SelectedColor(selected, material.albedo);
                if (object.light) {
                    material.emissive = object.lightData.color * LightEmissiveScale(m_scene, object);
                }
            }

            const Entity entity = pbrRegistry.Create();
            pbrRegistry.Add<Transform>(entity, *transform);
            pbrRegistry.Add<engine::ecs::MeshPBR>(entity, engine::ecs::MeshPBR{renderer->mesh, material});
            if (const engine::ecs::Light* light = m_scene.TryGetLight(object.entity)) {
                pbrRegistry.Add<engine::ecs::Light>(entity, *light);
            }
        }

        AddEnvironmentSunIfNeeded(pbrRegistry, environment, sky, environmentSunApplied);

        engine::PbrRenderer::Options options;
        ConfigureEnvironmentPbrOptions(pbrRegistry, options, environment, sky);

        const engine::Window& window = GetWindow();
        m_pbrRenderer->Render(pbrRegistry, m_camera, window.AspectRatio(), window.Width(), window.Height(), options);
    }
    DrawEditModeModels(viewProj);
    DrawSelectionOutline(viewProj);
    if (m_shader && m_cube && environment.showLightGuides) {
        m_viewport.DrawSelectedLightGuide(m_renderer, *m_shader, *m_cube, m_scene, viewProj, environment.selectedLightGuideOnly);
    }
    if (m_shader && m_cube && environment.showPhysicsGuides) {
        m_viewport.DrawPhysicsColliderGuides(m_renderer, *m_shader, *m_cube, m_scene, viewProj,
            environment.selectedPhysicsGuideOnly);
    }
    if (m_shader && m_cube && m_cone) {
        m_viewport.DrawSceneGizmo(m_renderer, *m_shader, *m_cube, *m_cone, m_scene, m_gizmo, viewProj);
    }
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

void EditorApp::UpdateEnvironmentIbl(const EditorScene::Environment& environment,
                                     const engine::DayNightCycle::Sample& sky) {
    if (!environment.ibl || !m_sky) {
        return;
    }

    if (!m_ibl) {
        m_ibl.emplace(256);
    }

    if (std::abs(sky.dayFactor - m_lastIblDay) > 0.04f) {
        m_ibl->Generate([&](const glm::mat4& view, const glm::mat4& projection) {
            m_sky->Draw(view, projection, sky, false);
        });
        m_lastIblDay = sky.dayFactor;
    }
}

void EditorApp::ConfigureEnvironmentPbrOptions(engine::ecs::Registry& registry,
                                               engine::PbrRenderer::Options& options,
                                               const EditorScene::Environment& environment,
                                               const engine::DayNightCycle::Sample& sky) {
    const engine::Window& window = GetWindow();
    UpdateEnvironmentIbl(environment, sky);

    options.ambient = sky.ambient * environment.skyLightIntensity;
    options.tonemap = !m_renderingHdrPreview;
    options.ibl = environment.ibl && m_ibl ? &*m_ibl : nullptr;
    options.pointShadows = environment.pointShadows;
    options.spotShadows = environment.spotShadows;
    options.directionalShadows = environment.directionalShadows;
    options.shadowSoftness = environment.shadowSoftness;
    options.fog = environment.fog;
    options.fogColor = environment.fogColor;
    options.fogColor = sky.horizon;
    options.fogDensity = environment.fogDensity;
    options.fogHeight = environment.fogHeight;
    options.fogHeightFalloff = environment.fogHeightFalloff;

    if (environment.ssao || environment.ssr) {
        if (!m_ssao) {
            m_ssao.emplace(window.Width(), window.Height());
        }
        m_ssao->radius = std::max(environment.ssaoRadius, 0.05f);
        m_ssao->bias = std::max(environment.ssaoBias, 0.0f);
        m_ssao->Generate(registry, m_camera, window.AspectRatio(), window.Width(), window.Height());
        options.ssao = environment.ssao ? &*m_ssao : nullptr;
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
    if (m_cameraController.MouseLookActive() || m_mode != EditorMode::Edit || !window.Native()) {
        m_mouse.ResetAssetLeft();
        return;
    }

    double cursorX = 0.0;
    double cursorY = 0.0;
    glfwGetCursorPos(window.Native(), &cursorX, &cursorY);

    const bool leftDown = glfwGetMouseButton(window.Native(), GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    const EditorMouseController::ButtonState left = m_mouse.UpdateAssetLeft(leftDown);
    const float x = static_cast<float>(cursorX);
    const float y = static_cast<float>(cursorY);

    if (left.pressed && m_panels.IsOpen(EditorPanels::Panel::Assets)) {
        const int folderIndex = m_content.FolderIndexAtPosition(m_assets, m_panels, m_scene, x, y);
        if (folderIndex >= 0) {
            m_assets.SelectFolderIndex(folderIndex);
            return;
        }

        const int assetIndex = m_content.AssetIndexAtPosition(m_assets, m_panels, m_scene, x, y);
        if (assetIndex >= 0) {
            m_assets.SelectIndex(assetIndex);
            if (const EditorAssets::Asset* asset = m_assets.SelectedAsset()) {
                m_dragDrop.BeginAssetDragAt(m_content.AssetFullPath(m_assets, *asset),
                    EditorAssets::TypeName(asset->type),
                    x,
                    y);
                m_log.Info("Mouse dragging asset " + asset->relativePath);
            }
        }
    }

    if (left.down && m_dragDrop.IsMouseDriven()) {
        m_dragDrop.UpdateCursor(x, y);
    }

    if (left.released && m_dragDrop.IsMouseDriven()) {
        m_dragDrop.UpdateCursor(x, y);
        if (IsViewportDropPosition(x, y)) {
            DropPayloadOnScene();
        } else {
            m_dragDrop.Clear();
            m_log.Info("Mouse drag cancelled");
        }
    }
}

void EditorApp::HandleMouseViewportSelection()
{
    engine::Window& window = GetWindow();
    if (m_cameraController.MouseLookActive() || m_mode != EditorMode::Edit || !window.Native()) {
        if (m_mouse.GizmoActiveFor(GLFW_MOUSE_BUTTON_LEFT)) {
            m_scene.EndTransformEdit();
            m_mouse.EndGizmoDrag();
        }
        m_mouse.ResetViewportLeft();
        return;
    }

    double cursorX = 0.0;
    double cursorY = 0.0;
    glfwGetCursorPos(window.Native(), &cursorX, &cursorY);

    const bool leftDown = glfwGetMouseButton(window.Native(), GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    const EditorMouseController::ButtonState left = m_mouse.UpdateViewportLeft(leftDown);
    const float x = static_cast<float>(cursorX);
    const float y = static_cast<float>(cursorY);

    const bool activeLeftGizmo =  m_mouse.GizmoActiveFor(GLFW_MOUSE_BUTTON_LEFT);
    if (m_dragDrop.HasPayload() || (!IsViewportDropPosition(x, y) && !activeLeftGizmo)) {
        return;
    }

    const glm::mat4 viewProj = m_camera.ProjectionMatrix(window.AspectRatio()) * m_camera.ViewMatrix();

    if (left.pressed) {
        if (m_viewport.PickGizmoHandle(m_gizmo, m_scene, x, y, viewProj, window.Width(), window.Height())) {
            m_mouse.BeginGizmoDrag(GLFW_MOUSE_BUTTON_LEFT, x, y);
            m_scene.BeginTransformEdit();
            m_log.Info(std::string("Mouse gizmo: ") + m_gizmo.ModeName() + " " + m_gizmo.AxisName());
        } else {
            const int picked = m_viewport.PickSceneObject(m_scene, m_editAssets, x, y, viewProj, window.Width(), window.Height());
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

    if (left.down && m_mouse.GizmoActiveFor(GLFW_MOUSE_BUTTON_LEFT)) {
        const float dx = x - m_mouse.GizmoLastX();
        const float dy = y - m_mouse.GizmoLastY();
        const float pixels = m_gizmo.CurrentAxis() == EditorGizmo::Axis::Y ? -dy : dx;

        if (pixels != 0.0f) {
            m_transformController.ApplyGizmoDrag(m_scene, m_gizmo, pixels);
        }

        m_mouse.UpdateGizmoLast(x, y);
    }

    if (left.released && m_mouse.GizmoActiveFor(GLFW_MOUSE_BUTTON_LEFT)) {
        m_scene.EndTransformEdit();
        m_mouse.EndGizmoDrag();
        m_log.Info("Mouse gizmo edit complete");
    }
}

void EditorApp::HandleMouseViewportGizmo()
{
    engine::Window& window = GetWindow();
    if (m_cameraController.MouseLookActive() || m_mode != EditorMode::Edit || !window.Native()) {
        if (m_mouse.GizmoActive()) {
            m_scene.EndTransformEdit();
        }
        m_mouse.EndGizmoDrag();
        m_mouse.ResetRight();
        return;
    }

    double cursorX = 0.0;
    double cursorY = 0.0;
    glfwGetCursorPos(window.Native(), &cursorX, &cursorY);

    const bool rightDown = glfwGetMouseButton(window.Native(), GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    const EditorMouseController::ButtonState right = m_mouse.UpdateRight(rightDown);
    const float x = static_cast<float>(cursorX);
    const float y = static_cast<float>(cursorY);

    if (right.pressed
        && !m_mouse.GizmoActive()
        && !m_dragDrop.HasPayload()
        && m_scene.SelectedObject()
        && !m_scene.SelectedLocked()
        && IsViewportDropPosition(x, y)) {
        m_mouse.BeginGizmoDrag(GLFW_MOUSE_BUTTON_RIGHT, x, y);
        m_scene.BeginTransformEdit();
        m_log.Info(std::string("Mouse gizmo: ") + m_gizmo.ModeName() + " " + m_gizmo.AxisName());
    }

    if (right.down && m_mouse.GizmoActiveFor(GLFW_MOUSE_BUTTON_RIGHT)) {
        const float dx = x - m_mouse.GizmoLastX();
        const float dy = y - m_mouse.GizmoLastY();
        const float pixels = m_gizmo.CurrentAxis() == EditorGizmo::Axis::Y ? -dy : dx;

        if (pixels != 0.0f) {
            m_transformController.ApplyGizmoDrag(m_scene, m_gizmo, pixels);
        }

        m_mouse.UpdateGizmoLast(x, y);
    }

    if (right.released && m_mouse.GizmoActiveFor(GLFW_MOUSE_BUTTON_RIGHT)) {
        m_scene.EndTransformEdit();
        m_mouse.EndGizmoDrag();
        m_log.Info("Mouse gizmo edit complete");
    }
}

void EditorApp::BeginAssetDrag()
{
    const EditorAssets::Asset* asset = m_assets.SelectedAsset();
    if (!asset) {
        m_log.Warning("No asset selected for drag");
        return;
    }

    m_dragDrop.BeginAssetDrag(m_content.AssetFullPath(m_assets, *asset), EditorAssets::TypeName(asset->type));
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
        RequestLoadSceneFromPath(payload.path);
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
    } else if (payload.typeName == "Material") {
        std::string error;
        const engine::RuntimeMaterialAsset* material = m_editAssets.LoadMaterial(payload.path, &error);
        if (!material) {
            m_log.Error("Material drop failed: " + error);
            m_dragDrop.Clear();
            return;
        }

        if (m_scene.SetSelectedMaterialAsset(payload.path)) {
            m_editMaterialLoadErrors.erase(payload.path);
            m_log.Info("Assigned material to selected object");
        } else {
            m_log.Warning("Material drop failed: select an unlocked object first");
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

    return m_viewport.SceneDropPosition(static_cast<float>(cursorX),
        static_cast<float>(cursorY),
        viewProj,
        window.Width(),
        window.Height());
}

bool EditorApp::IsViewportDropPosition(float x, float y)
{
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse) {
        return false;
    }

    const engine::Window& window = GetWindow();
    return m_viewport.ContainsPoint(x, y, window.Width(), window.Height());
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

void EditorApp::AddSphere()
{
    if (!m_sphere) {
        m_log.Error("Add failed: sphere mesh is not ready");
        return;
    }

    m_scene.AddSphere(*m_sphere);
    m_log.Info("Added sphere");
}

void EditorApp::AddDynamicCube() {
    if (!m_cube) {
        m_log.Error("Add failed: cube mesh is not ready");
        return;
    }

    m_scene.AddCube(*m_cube);
    engine::ecs::RigidBody rigidBody = engine::ecs::RigidBody::Dynamic(1.0f);
    m_scene.SetSelectedRigidBody(rigidBody);

    const Transform* transform = m_scene.SelectedTransform();
    engine::ecs::Collider collider = engine::ecs::Collider::MakeBox(transform
        ? glm::max(transform->scale * 0.5f, glm::vec3(0.001f))
        : glm::vec3(0.5f));
    m_scene.SetSelectedCollider(collider);
    m_log.Info("Added dynamic cube");
}

void EditorApp::AddStaticFloor() {
    if (!m_plane) {
        m_log.Error("Add failed: plane mesh is not ready");
        return;
    }

    m_scene.AddPlane(*m_plane);
    m_scene.SetSelectedRigidBodyEnabled(false);
    const Transform* transform = m_scene.SelectedTransform();
    engine::ecs::Collider collider = engine::ecs::Collider::MakePlane(glm::vec3(0.0f, 1.0f, 0.0f),
        transform ? transform->position.y : 0.0f);
    m_scene.SetSelectedCollider(collider);
    m_log.Info("Added static floor");
}

void EditorApp::AddTriggerVolume() {
    if (!m_cube) {
        m_log.Error("Add failed: cube mesh is not ready");
        return;
    }

    m_scene.AddCube(*m_cube);
    m_scene.SetSelectedRigidBodyEnabled(false);
    const Transform* transform = m_scene.SelectedTransform();
    engine::ecs::Collider collider = engine::ecs::Collider::MakeBox(transform
        ? glm::max(transform->scale * 0.5f, glm::vec3(0.001f))
        : glm::vec3(0.5f));
    collider.isTrigger = true;
    m_scene.SetSelectedCollider(collider);
    m_log.Info("Added trigger volume");
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
    if (!m_cube || !m_plane || !m_sphere) {
        m_log.Error("Type change failed: editor meshes are not ready");
        return;
    }

    const engine::Mesh& mesh = primitive == EditorScene::Primitive::Cube ? *m_cube : primitive == EditorScene::Primitive::Plane ? *m_plane : *m_sphere;
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

void EditorApp::FrameSelected() {
    const EditorScene::Object* selected = m_scene.SelectedObject();
    if (!selected) {
        m_log.Warning("Frame selected failed: no selected object");
        return;
    }

    const Transform* transform = m_scene.TryGetTransform(selected->entity);
    if (!transform) {
        m_log.Warning("Frame selected failed: selected object has no transform");
        return;
    }

    const float largestScale = std::max(std::max(transform->scale.x, transform->scale.y), transform->scale.z);
    const float radius = std::max(largestScale, 0.5f);
    const glm::vec3 target = transform->position;
    const glm::vec3 offset(0.0f, std::max(1.5f, radius * 1.5f), std::max(3.0f, radius * 4.0f));

    m_camera.SetPosition(target + offset);
    m_camera.LookAt(target);
    m_log.Info("Framed selected object");
}

void EditorApp::DuplicateSelected()
{
    if (!m_cube || !m_plane || !m_sphere) {
        m_log.Error("Duplicate failed: editor meshes are not ready");
        return;
    }

    if (m_scene.DuplicateSelected(*m_cube, *m_plane, *m_sphere)) {
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
    if (!m_cube || !m_plane || !m_sphere) {
        m_log.Error("Undo failed: editor meshes are not ready");
        return;
    }

    if (m_scene.Undo(*m_cube, *m_plane, *m_sphere)) {
        m_log.Info("Undo");
    } else {
        m_log.Warning("Nothing to undo");
    }
}

void EditorApp::Redo()
{
    if (!m_cube || !m_plane || !m_sphere) {
    m_log.Error("Redo failed: editor meshes are not ready");
    return;
    }

    if (m_scene.Redo(*m_cube, *m_plane, *m_sphere)) {
        m_log.Info("Redo");
    } else {
        m_log.Warning("Nothing to redo");
    }
}

void EditorApp::SaveScene()
{
    if (m_runtime.SaveScene(m_scene, m_project, m_log)) {
        m_autosaveTimer = 0.0f;
        SetScenePathDraft(m_project.ScenePath());
    }
}

void EditorApp::SaveSceneAs(const std::string& path) {
    if (path.empty()) {
        m_log.Warning("Save As failed: scene path is empty");
        return;
    }

    m_project.SetScenePath(path);
    if (m_runtime.SaveScene(m_scene, m_project, m_log)) {
        m_project.Save(m_config);
        m_config.Save();
        m_autosaveTimer = 0.0f;
        SetScenePathDraft(m_project.ScenePath());
    }
}

void EditorApp::SetScenePathDraft(const std::string& path) {
    std::memset(m_scenePathDraft.data(), 0, m_scenePathDraft.size());
    std::snprintf(m_scenePathDraft.data(), m_scenePathDraft.size(), "%s", path.c_str());
}

void EditorApp::UpdateAutosave(float dt) {
    if (m_mode != EditorMode::Edit || !m_scene.IsDirty()) {
        m_autosaveTimer = 0.0f;
        return;
    }

    m_autosaveTimer += dt;
    if (m_autosaveTimer >= 60.0f) {
        m_runtime.AutosaveScene(m_scene, m_project, m_log);
        m_autosaveTimer = 0.0f;
    }
}

void EditorApp::LoadScene()
{
    if (!m_cube || !m_plane || !m_sphere) {
        m_log.Error("Load failed: editor meshes are not ready");
        return;
    }

    if (m_runtime.LoadScene(m_scene, m_project, *m_cube, *m_plane, *m_sphere, m_log)) {
        m_project.AddRecentScene(m_project.ScenePath());
        m_project.Save(m_config);
        m_config.Save();
        m_autosaveTimer = 0.0f;
        SetScenePathDraft(m_project.ScenePath());
    }
}

void EditorApp::RequestCloseEditor() {
    if (m_scene.IsDirty()) {
        QueueDirtySceneAction(PendingSceneAction::CloseEditor);
        return;
    }

    GetWindow().SetShouldClose(true);
}

void EditorApp::RequestNewScene()
{
    if (m_scene.IsDirty()) {
        QueueDirtySceneAction(PendingSceneAction::NewScene);
        return;
    }

    PerformNewScene();
}

void EditorApp::RequestLoadSceneFromPath(const std::string& path) {
    if (m_scene.IsDirty()) {
        QueueDirtySceneAction(PendingSceneAction::LoadScene, path);
        return;
    }

    PerformLoadSceneFromPath(path);
}

void EditorApp::PerformNewScene()
{
    if (!m_cube || !m_plane) {
        m_log.Error("New scene failed: editor primitive meshes are not ready");
        return;
    }

    m_scene.BuildDefault(*m_cube, *m_plane, *m_sphere);
    m_scene.MarkDirty();
    m_autosaveTimer = 0.0f;
    m_log.Info("Created new scene");
}

void EditorApp::PerformLoadSceneFromPath(const std::string& path) {
    if (path.empty()) {
        m_log.Warning("Load failed: scene path is empty");
        return;
    }
    if (!m_cube || !m_plane) {
        m_log.Error("Load failed: editor meshes are not ready");
        return;
    }

    const std::string previousPath = m_project.ScenePath();
    m_project.SetScenePath(path);
    if (m_runtime.LoadScene(m_scene, m_project, *m_cube, *m_plane, *m_sphere, m_log)) {
        m_project.AddRecentScene(m_project.ScenePath());
        m_project.Save(m_config);
        m_config.Save();
        m_autosaveTimer = 0.0f;
        SetScenePathDraft(m_project.ScenePath());
        return;
    }

    m_project.SetScenePath(previousPath);
    SetScenePathDraft(previousPath);
}

void EditorApp::QueueDirtySceneAction(PendingSceneAction action, const std::string& path) {
    m_pendingSceneAction = action;
    m_pendingScenePath = path;
    m_dirtyScenePromptQueued = true;
}

void EditorApp::CompletePendingSceneAction() {
    const PendingSceneAction action = m_pendingSceneAction;
    const std::string path = m_pendingScenePath;
    CancelPendingSceneAction();

    switch (action) {
    case PendingSceneAction::CloseEditor:
        GetWindow().SetShouldClose(true);
        break;
    case PendingSceneAction::LoadScene:
        PerformLoadSceneFromPath(path);
        break;
    case PendingSceneAction::None:
        break;
    }
}

void EditorApp::CancelPendingSceneAction() {
    m_pendingSceneAction = PendingSceneAction::None;
    m_pendingScenePath.clear();
    m_dirtyScenePromptQueued = false;
}

void EditorApp::LoadSceneFromPath(const std::string& path) {
    if (path.empty()) {
        m_log.Warning("Load failed: scene path is empty");
        return;
    }
    if (!m_cube || !m_plane) {
        m_log.Error("Load failed: editor meshes are not ready");
        return;
    }

    const std::string previousPath = m_project.ScenePath();
    m_project.SetScenePath(path);
    if (m_runtime.LoadScene(m_scene, m_project, *m_cube, *m_plane, *m_sphere, m_log)) {
        m_project.Save(m_config);
        m_config.Save();
        m_autosaveTimer = 0.0f;
        SetScenePathDraft(m_project.ScenePath());
        return;
    }

    m_project.SetScenePath(previousPath);
    SetScenePathDraft(previousPath);
}

void EditorApp::ExportRuntimeScene()
{
    m_runtime.ExportRuntimeScene(m_scene, m_project, m_log);
}

void EditorApp::ValidateRuntimeScene()
{
    if (!m_cube || !m_plane || !m_sphere) {
        m_log.Error("Runtime scene validation failed: editor primitive meshes are not ready");
        return;
    }

    m_runtime.ValidateRuntimeScene(m_project, *m_cube, *m_plane, *m_sphere, m_log);
}

void EditorApp::EnterPlayMode()
{
    m_editSnapshot = m_scene.CreateSnapshot();
    m_physicsPaused = false;
    m_physicsStepRequested = false;
    m_physicsAccumulator = 0.0f;
    m_physicsStepsLastFrame = 0;
    m_physicsEventEnterCount = 0;
    m_physicsEventStayCount = 0;
    m_physicsEventExitCount = 0;
    m_physicsEventRows.clear();
    m_physicsEventGuides.clear();
    m_playEntityNames.clear();
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
    const PhysicsRuntimeStats physics = m_playRegistry
        ? CollectPhysicsRuntimeStats(*m_playRegistry)
        : PhysicsRuntimeStats{};

    m_mode = EditorMode::Play;
    m_log.Info("Play mode: runtime preview loaded, "
        + std::to_string(linearCount) + " linear, "
        + std::to_string(angularCount) + " angular, "
        + std::to_string(physics.rigidBodies) + " rigid bodies, "
        + std::to_string(physics.dynamicBodies) + " dynamic, "
        + std::to_string(physics.colliders) + " colliders, "
        + std::to_string(physics.staticColliders) + " static, "
        + std::to_string(physics.triggerColliders) + " triggers");
    if (physics.dynamicBodiesWithoutCollider > 0) {
        m_log.Warning("Play mode physics: "
            + std::to_string(physics.dynamicBodiesWithoutCollider)
            + " dynamic body/bodies have no collider");
    }
    if (physics.invalidColliders > 0) {
        m_log.Warning("Play mode physics: "
            + std::to_string(physics.invalidColliders)
            + " collider(s) have invalid radius, extents, or plane normal");
    }
    if (physics.dynamicBodies > 0 && physics.staticColliders == 0) {
        m_log.Warning("Play mode physics: dynamic bodies exist but there are no static colliders to collide with");
    }
}

void EditorApp::ExitPlayMode()
{
    m_playRegistry.reset();
    m_playAssets.reset();
    m_physicsPaused = false;
    m_physicsStepRequested = false;
    m_physicsAccumulator = 0.0f;
    m_physicsStepsLastFrame = 0;
    m_physicsEventEnterCount = 0;
    m_physicsEventStayCount = 0;
    m_physicsEventExitCount = 0;
    m_physicsEventRows.clear();
    m_physicsEventGuides.clear();
    m_playEntityNames.clear();

    if (!m_cube || !m_plane) {
        m_log.Error("Could not restore edit scene");
        m_mode = EditorMode::Edit;
        m_editSnapshot.reset();
        return;
    }

    if (m_editSnapshot) {
        m_scene.RestoreFromSnapshot(*m_editSnapshot, *m_cube, *m_plane, *m_sphere);
    }
    m_editSnapshot.reset();
    m_mode = EditorMode::Edit;
    m_log.Info("Edit mode: restored scene from before Play");
}

bool EditorApp::BuildPlayRuntimePreview(std::string * error)
{
    if (!m_cube || !m_plane || !m_sphere) {
        if (error) {
            *error = "editor primitive meshes are not ready";
        }
        return false;
    }

    m_playRegistry.emplace();
    m_playAssets.emplace();
    std::vector<engine::ecs::Entity> createdEntities;
    std::vector<std::string> createdNames;
    if (!m_runtime.BuildPlayRuntimePreview(m_scene,
            m_project,
            *m_cube,
            *m_plane,
            *m_sphere,
            *m_playRegistry,
            *m_playAssets,
            &createdEntities,
            &createdNames,
            error)) {
        m_playRegistry.reset();
        m_playAssets.reset();
        return false;
    }

    m_playEntityNames.clear();
    std::unordered_map<std::string, engine::ecs::Entity> playEntitiesByName;
    const std::size_t count = std::min(createdEntities.size(), createdNames.size());
    for (std::size_t i = 0; i < count; ++i) {
        m_playEntityNames[createdEntities[i]] = createdNames[i];
        playEntitiesByName[createdNames[i]] = createdEntities[i];
    }

    m_playPhysics.ClearJoints();
    for (const EditorScene::PhysicsJoint& joint : m_scene.PhysicsJoints()) {
        if (!joint.enabled) {
            continue;
        }

        const auto a = playEntitiesByName.find(joint.objectA);
        if (a == playEntitiesByName.end()) {
            continue;
        }

        if (joint.worldAnchor) {
            if (joint.type == EditorScene::PhysicsJoint::Type::Spring) {
                m_playPhysics.AddSpringJointToWorld(a->second, joint.anchor, joint.restLength, joint.stiffness, joint.damping);
            } else {
                m_playPhysics.AddDistanceJointToWorld(a->second, joint.anchor, joint.restLength, joint.rope);
            }
            continue;
        }

        const auto b = playEntitiesByName.find(joint.objectB);
        if (b == playEntitiesByName.end()) {
            continue;
        }

        if (joint.type == EditorScene::PhysicsJoint::Type::Spring) {
            m_playPhysics.AddSpringJoint(a->second, b->second, joint.restLength, joint.stiffness, joint.damping);
        } else {
            m_playPhysics.AddDistanceJoint(a->second, b->second, joint.restLength, joint.rope);
        }
    }

    return true;
}

void EditorApp::StepPlayPhysics(float dt)
{
    m_physicsStepsLastFrame = 0;
    m_physicsEventEnterCount = 0;
    m_physicsEventStayCount = 0;
    m_physicsEventExitCount = 0;
    if (m_mode != EditorMode::Play || !m_playRegistry) {
        m_physicsStepRequested = false;
        return;
    }

    const EditorScene::Environment& environment = m_scene.GetEnvironment();
    m_playPhysics.gravity = environment.physicsGravity;
    m_playPhysics.solverIterations = environment.physicsSolverIterations;
    m_playPhysics.broadPhase = environment.physicsBroadPhase;
    m_playPhysics.cellSize = environment.physicsCellSize;
    m_playPhysics.restitutionThreshold = environment.physicsRestitutionThreshold;
    m_playPhysics.allowSleeping = environment.physicsAllowSleeping;
    m_playPhysics.sleepLinearVelocity = environment.physicsSleepLinearVelocity;
    m_playPhysics.sleepAngularVelocity = environment.physicsSleepAngularVelocity;
    m_playPhysics.timeToSleep = environment.physicsTimeToSleep;

    const float step = std::max(m_physicsFixedTimestep, 0.0001f);
    if (m_physicsStepRequested) {
        engine::ecs::UpdateRuntimeMotion(*m_playRegistry, step);
        m_playPhysics.Step(*m_playRegistry, step);
        CapturePlayPhysicsEvents();
        m_physicsStepRequested = false;
        m_physicsStepsLastFrame = 1;
        return;
    }

    if (m_physicsPaused) {
        return;
    }

    m_physicsAccumulator += std::min(dt, 0.25f);
    constexpr int kMaxPhysicsStepsPerFrame = 5;
    while (m_physicsAccumulator >= step && m_physicsStepsLastFrame < kMaxPhysicsStepsPerFrame) {
        engine::ecs::UpdateRuntimeMotion(*m_playRegistry, step);
        m_playPhysics.Step(*m_playRegistry, step);
        CapturePlayPhysicsEvents();
        m_physicsAccumulator -= step;
        ++m_physicsStepsLastFrame;
    }

    if (m_physicsStepsLastFrame == kMaxPhysicsStepsPerFrame && m_physicsAccumulator >= step) {
        m_physicsAccumulator = 0.0f;
        m_log.Warning("Play physics skipped accumulated time to keep the editor responsive");
    }
}

void EditorApp::CapturePlayPhysicsEvents()
{
    constexpr std::size_t kMaxRecentEvents = 32;

    auto entityName = [this](engine::ecs::Entity entity) {
        const auto it = m_playEntityNames.find(entity);
        if (it != m_playEntityNames.end() && !it->second.empty()) {
            return it->second;
        }

        char fallback[48];
        std::snprintf(fallback, sizeof(fallback), "Entity_%u", engine::ecs::EntityIndex(entity));
        return std::string(fallback);
    };

    for (const engine::CollisionEvent& event : m_playPhysics.Events()) {
        switch (event.phase) {
        case engine::CollisionEvent::Phase::Enter:
            ++m_physicsEventEnterCount;
            break;
        case engine::CollisionEvent::Phase::Stay:
            ++m_physicsEventStayCount;
            break;
        case engine::CollisionEvent::Phase::Exit:
            ++m_physicsEventExitCount;
            break;
        }

        const std::string type = event.trigger ? "Trigger" : "Collision";
        EditorDockspace::PhysicsEventRow row;
        row.objectA = entityName(event.a);
        row.objectB = entityName(event.b);
        row.phase = static_cast<int>(event.phase);
        row.trigger = event.trigger;
        row.text = std::string(CollisionPhaseName(event.phase))
            + " " + type + ": "
            + row.objectA + " <-> " + row.objectB;
        m_physicsEventRows.push_back(row);

        if (m_playRegistry) {
            const Transform* transformA = m_playRegistry->TryGet<Transform>(event.a);
            const Transform* transformB = m_playRegistry->TryGet<Transform>(event.b);
            if (transformA && transformB) {
                EditorViewport::PhysicsEventGuide guide;
                guide.a = transformA->position;
                guide.b = transformB->position;
                guide.phase = row.phase;
                guide.trigger = row.trigger;
                m_physicsEventGuides.push_back(guide);
            }
        }
    }

    if (m_physicsEventRows.size() > kMaxRecentEvents) {
        m_physicsEventRows.erase(
            m_physicsEventRows.begin(),
            m_physicsEventRows.end() - static_cast<std::ptrdiff_t>(kMaxRecentEvents));
    }
    if (m_physicsEventGuides.size() > kMaxRecentEvents) {
        m_physicsEventGuides.erase(
            m_physicsEventGuides.begin(),
            m_physicsEventGuides.end() - static_cast<std::ptrdiff_t>(kMaxRecentEvents));
    }
}

bool EditorApp::Pressed(int key)
{
    const bool down = GetWindow().IsKeyPressed(key);
    const bool was = m_keyPrev[key];
    m_keyPrev[key] = down;
    return down && !was;
}
