#include "EditorApp.h"

#include <engine/ecs/Registry.h>
#include <engine/ecs/RuntimeSystems.h>
#include <engine/gameplay/GameplayComponents.h>
#include <engine/gameplay/GameplaySystems.h>
#include <engine/gameplay/Script.h>
#include <engine/ecs/Systems.h>
#include <engine/graphics/Model.h>
#include <engine/graphics/Primitives.h>
#include <engine/graphics/Texture.h>
#include <engine/animation/Animator.h>
#include <engine/ai/NavMeshBuilder.h>
#include <engine/ai/BtScript.h>

#include "GameBtScripts.h"
#include <game/GameModule.h>
#include "ParticleAsset.h"

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <imgui.h>

#include <algorithm>
#include <limits>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <unordered_set>

using engine::ecs::Entity;
using engine::ecs::MeshRenderer;
using engine::ecs::Transform;

namespace {

// --- TEMPORARY terrain crash tracing ---------------------------------------
// Appends to "terrain_debug.log" beside the running exe, flushing after every
// line so the LAST line written survives a hard crash. When the engine dies on
// a terrain action, open that file: the final line is the last step that ran,
// so the crash is between it and the next expected step. Remove once fixed.
void TerrainTrace(const char* fmt, ...) {
#ifdef _MSC_VER
    static std::FILE* file = [] {
        std::FILE* f = nullptr;
        fopen_s(&f, "D:/C++_Projects/3DGEngine/terrain_debug.log", "a");
        return f;
    }();
#else
    static std::FILE* file = std::fopen("D:/C++_Projects/3DGEngine/terrain_debug.log", "a");
#endif
    if (!file) {
        return;
    }
    std::va_list args;
    va_start(args, fmt);
    std::vfprintf(file, fmt, args);
    va_end(args);
    std::fputc('\n', file);
    std::fflush(file);
}

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

std::size_t CountAuthoredRotators(const EditorScene& scene) {
    return static_cast<std::size_t>(std::count_if(
        scene.Objects().begin(),
        scene.Objects().end(),
        [](const EditorScene::Object& object) {
            return object.rotatorEnabled;
        }));
}

std::size_t CountAuthoredMovers(const EditorScene& scene) {
    return static_cast<std::size_t>(std::count_if(
        scene.Objects().begin(),
        scene.Objects().end(),
        [](const EditorScene::Object& object) {
            return object.moverEnabled;
        }));
}

float AnimationClipSeconds(const engine::Animation& clip) {
    const float ticksPerSecond = clip.ticksPerSecond > 0.0f ? clip.ticksPerSecond : 25.0f;
    return clip.duration > 0.0f ? clip.duration / ticksPerSecond : 0.0f;
}

std::size_t CountRuntimeRotatorsWithFrozenRigidBody(engine::ecs::Registry& registry) {
    engine::ecs::Pool<engine::ecs::Rotator>* rotators = registry.TryPool<engine::ecs::Rotator>();
    if (!rotators) {
        return 0;
    }

    std::size_t count = 0;
    for (const engine::ecs::Entity entity : rotators->dense) {
        const engine::ecs::RigidBody* body = registry.TryGet<engine::ecs::RigidBody>(entity);
        if (body && body->freezeRotation) {
            ++count;
        }
    }
    return count;
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
    case engine::ecs::ColliderShape::Pyramid:
    case engine::ecs::ColliderShape::Staircase:
        return collider.halfExtents.x <= 0.0f
            || collider.halfExtents.y <= 0.0f
            || collider.halfExtents.z <= 0.0f;
    case engine::ecs::ColliderShape::Capsule:
        return collider.radius <= 0.0f || collider.halfHeight < 0.0f;
    case engine::ecs::ColliderShape::Cylinder:
    case engine::ecs::ColliderShape::Cone:
        return collider.radius <= 0.0f || collider.halfHeight <= 0.0f;
    case engine::ecs::ColliderShape::Torus:
        return collider.majorRadius <= 0.0f || collider.minorRadius <= 0.0f;
    case engine::ecs::ColliderShape::Plane:
        return glm::dot(collider.planeNormal, collider.planeNormal) <= 0.0001f;
    }

    return true;
}

bool TriggerActionShouldEnable(EditorScene::TriggerActionMode mode, bool currentlyEnabled, bool* shouldChange) {
    *shouldChange = false;
    switch (mode) {
    case EditorScene::TriggerActionMode::Enable:
        *shouldChange = !currentlyEnabled;
        return true;
    case EditorScene::TriggerActionMode::Disable:
        *shouldChange = currentlyEnabled;
        return false;
    case EditorScene::TriggerActionMode::Toggle:
        *shouldChange = true;
        return !currentlyEnabled;
    case EditorScene::TriggerActionMode::None:
        return currentlyEnabled;
    }
    return currentlyEnabled;
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
    : engine::Application(MakeEditorWindowProps(config)), m_config(config),
      m_runtimeAudio(m_audio)
{
}

void EditorApp::OnInit()
{
    m_renderer.Init();
    m_renderer.SetClearColor({0.08f, 0.09f, 0.11f, 1.0f});

    // Push the far clip plane out so objects don't vanish when the camera zooms out
    // (the engine default is 100). Kept editor-local so demo shadow tuning is untouched.
    m_camera.nearPlane = 0.1f;
    m_camera.farPlane  = 3000.0f;

    m_cube.emplace(engine::primitives::Cube());
    m_cone.emplace(engine::primitives::Cone());
    m_plane.emplace(engine::primitives::Plane(1.0f, 8.0f));
    m_sphere.emplace(engine::primitives::Sphere());
    m_capsule.emplace(engine::primitives::Capsule());
    m_cylinder.emplace(engine::primitives::Cylinder());
    m_pyramid.emplace(engine::primitives::Pyramid());
    m_torus.emplace(engine::primitives::Torus());
    m_staircase.emplace(engine::primitives::Staircase());
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
        uniform vec2 uViewportSize;

        void main() {
            mat3 normalMat = mat3(transpose(inverse(uModel)));
            vec3 worldNormal = normalize(normalMat * aNormal);
            vec4 world = uModel * vec4(aPos, 1.0);
            vec4 clip = uViewProj * world;
            vec2 direction = (uViewProj * vec4(worldNormal, 0.0)).xy;
            float directionLength = length(direction);
            if (directionLength > 0.00001 && uThickness > 0.0) {
                clip.xy += direction / directionLength * (2.0 * uThickness / uViewportSize) * clip.w;
            }
            gl_Position = clip;
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
    m_skinnedOutlineShader.emplace(
    R"glsl(
        #version 330 core
        const int MAX_BONES = 128;
        layout(location = 0) in vec3 aPos;
        layout(location = 1) in vec3 aNormal;
        layout(location = 3) in vec4 aBoneIds;
        layout(location = 4) in vec4 aWeights;

        uniform mat4 uViewProj;
        uniform mat4 uModel;
        uniform mat4 uBones[MAX_BONES];
        uniform float uThickness;
        uniform vec2 uViewportSize;

        void main() {
            mat4 skin = aWeights.x * uBones[int(aBoneIds.x)]
                      + aWeights.y * uBones[int(aBoneIds.y)]
                      + aWeights.z * uBones[int(aBoneIds.z)]
                      + aWeights.w * uBones[int(aBoneIds.w)];
            vec4 local = skin * vec4(aPos, 1.0);
            vec3 localNormal = normalize(mat3(skin) * aNormal);
            vec4 world = uModel * local;
            vec3 worldNormal = normalize(mat3(transpose(inverse(uModel))) * localNormal);
            vec4 clip = uViewProj * world;
            vec2 direction = (uViewProj * vec4(worldNormal, 0.0)).xy;
            float directionLength = length(direction);
            if (directionLength > 0.00001 && uThickness > 0.0) {
                clip.xy += direction / directionLength * (2.0 * uThickness / uViewportSize) * clip.w;
            }
            gl_Position = clip;
        }
    )glsl",
    R"glsl(
        #version 330 core
        uniform vec3 uColor;
        out vec4 FragColor;
        void main() { FragColor = vec4(uColor, 1.0); }
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
    m_particleRenderer.emplace();
    m_skinnedRenderer.emplace();
    m_sky.emplace();
    m_text.emplace();

    m_project.Load(m_config);
    SetScenePathDraft(m_project.ScenePath());
    m_content.Refresh(m_assets, m_project, m_log);
    m_materialMaker.SetOutputDirectory(m_project.AssetRoot());
    m_behaviorGraph.SetOutputDirectory(m_project.AssetRoot());
    engine::ai::RegisterExampleBtScripts();   // built-in example scripts (idempotent)
    RegisterGameBtScripts();                  // legacy: editor/src/GameBtScripts.cpp
    RegisterGameModule();                     // shared game module (scripts used by editor + player)
    m_scene.BuildDefault(*m_cube, *m_plane, *m_sphere, *m_capsule, *m_cylinder, *m_cone,
        *m_pyramid, *m_torus, *m_staircase);
    m_imgui.Init(GetWindow());
}

void EditorApp::OnUpdate(float dt)
{
    engine::Window& window = GetWindow();
    RestoreCameraBeforeShake();
    m_elapsed += dt;
    m_dt = dt;
    if (dt > 0.0f) {
        m_fps = m_fps * 0.92f + (1.0f / dt) * 0.08f;
    }
    const bool keyboardCaptured = IsEditorKeyboardCaptured()
        || (m_panels.IsOpen(EditorPanels::Panel::ShaderEditor)
            && m_shaderEditor.WantsKeyboard());

    // Play mode: ESC toggles the cursor between captured (mouse-look) and free (editor UI).
    if (m_mode == EditorMode::Play) {
        const bool escDown = window.IsKeyPressed(GLFW_KEY_ESCAPE);
        if (escDown && !m_playCursorTogglePrev) {
            m_playMouseCaptured = !m_playMouseCaptured;
            window.SetCursorCaptured(m_playMouseCaptured);
        }
        m_playCursorTogglePrev = escDown;
    }

    const bool skipHeld = m_mode == EditorMode::Play
        && m_cameraDirector.Skippable()
        && window.IsKeyPressed(GLFW_KEY_ENTER);
    if (skipHeld && !m_cinematicSkipPrev) m_cameraDirector.Skip();
    m_cinematicSkipPrev = skipHeld;

    const bool playInputEnabled = !keyboardCaptured && !m_cameraDirector.InputLocked();
    if (m_mode == EditorMode::Play && m_playRegistry) {
        const engine::ScriptInputState scriptInput =
            CapturePlayScriptInput(playInputEnabled, true);
        engine::UpdateScripts(
            *m_playRegistry, dt, &scriptInput, &m_runtimeAudio,
            &m_cameraShake, &m_cameraDirector);
        // Camera events are frame events. Every script has now had one opportunity
        // to observe them; fixed updates receive held input and simulation events.
        m_cameraDirector.ClearEvents();
    }
    StepPlayPhysics(dt, playInputEnabled);
    ProcessCameraDirectorCommands();
    if (m_mode == EditorMode::Play) {
        if (m_cameraBlend.Active()) UpdateCameraBlend(dt);
        else ApplyManagedPlayCamera();
    }
    if (m_mode == EditorMode::Play && m_playRegistry)
        engine::UpdateParticleSystems(*m_playRegistry, dt);
    m_audio.SetListener(m_camera.Position(), m_camera.Front());
    m_audio.UpdateMixer(dt);
    if (m_mode == EditorMode::Play) UpdatePlayAudioSources();
    UpdateAutosave(dt);

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
    if (m_terrainSculpt) {
        HandleTerrainSculpt();   // left-drag paints the selected terrain instead of selecting
    } else {
        HandleMouseViewportSelection();
        HandleMouseViewportGizmo();
    }

    const bool controlDown = window.IsKeyPressed(GLFW_KEY_LEFT_CONTROL)
        || window.IsKeyPressed(GLFW_KEY_RIGHT_CONTROL);
    if (!keyboardCaptured) {
        HandleAssetShortcuts(window, controlDown);
        HandleEditorCommandShortcuts(window, controlDown);
    }
    if (m_mode == EditorMode::Edit && !m_cameraBlend.Active() && !m_cameraSequence.Active()) {
        m_cameraController.UpdateCamera(window,
            m_camera,
            !keyboardCaptured,
            dt,
            [&](float x, float y) { return IsViewportDropPosition(x, y); });
    }
    if (m_mode == EditorMode::Edit && m_cameraBlend.Active() && !m_cameraSequence.Active()) {
        UpdateCameraBlend(dt);
    }
    if (m_cameraSequence.Active()) {
        UpdateCameraSequence(dt);
    }
    UpdateCameraShake(dt);
    m_audio.SetListener(m_camera.Position(), m_camera.Front());
    
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

    // Render scale: render the 3D scene at a fraction of window resolution into the
    // post buffer and upscale on present. Only in the simple path (no SSR/SSAO) so we
    // don't have to scale the SSAO G-buffer / SSR pass.
    const bool  canScale  = !environment.ssr && !environment.ssao;
    const float scale     = canScale ? std::clamp(environment.renderScale, 0.25f, 1.0f) : 1.0f;
    const bool  wantScale = scale < 0.999f;
    const bool hasGraphPost = std::any_of(
        environment.postProcessEffects.begin(),
        environment.postProcessEffects.end(),
        [](const EditorScene::Environment::PostProcessEffect& effect) {
            return effect.enabled && !effect.shaderPath.empty();
        });
    const bool  useHdrPost = environment.ssr || wantScale || hasGraphPost;
    const int   sw = std::max(1, static_cast<int>(std::lround(window.Width()  * scale)));
    const int   sh = std::max(1, static_cast<int>(std::lround(window.Height() * scale)));
    m_renderW = window.Width();
    m_renderH = window.Height();

    const auto cpuRenderStart = std::chrono::high_resolution_clock::now();
    m_gpuProfiler.BeginFrame();

    m_renderer.SetMultisample(environment.msaa);   // MSAA toggle (direct render path)

    if (useHdrPost) {
        if (!m_postProcess) {
            m_postProcess.emplace(sw, sh);
            m_postProcess->settings.bloom = false;
            m_postProcess->settings.autoExposure = false;
            m_postProcess->settings.exposure = 1.0f;
        }
        if (environment.ssr && !m_ssr) {
            m_ssr.emplace(window.Width(), window.Height());
        }
        m_postProcess->settings.fxaa = environment.fxaa;   // FXAA toggle (SSR/HDR path)
        m_postProcess->Resize(sw, sh);
        std::vector<engine::PostProcess::Effect> graphEffects;
        engine::RuntimeAssetManager* effectAssets =
            m_mode == EditorMode::Play && m_playAssets
                ? &*m_playAssets : &m_editAssets;
        for (const auto& authored : environment.postProcessEffects) {
            if (!authored.enabled || authored.shaderPath.empty()) continue;
            std::string loadError;
            engine::PostProcess::Effect effect;
            effect.enabled = true;
            effect.shader =
                effectAssets->LoadShader(authored.shaderPath, false, &loadError);
            if (!effect.shader) continue;
            for (const auto& parameter : authored.parameters) {
                effect.parameters[parameter.name] = parameter.value;
                effect.parameterTypes[parameter.name] = parameter.type;
                if (parameter.type
                        == static_cast<int>(engine::ShaderValueType::Texture2D)
                    && !parameter.value.empty()) {
                    const engine::Texture* texture =
                        effectAssets->LoadTexture(parameter.value, &loadError);
                    if (texture) effect.textures[parameter.name] = texture;
                }
            }
            graphEffects.push_back(std::move(effect));
        }
        m_postProcess->SetEffects(std::move(graphEffects));
        if (m_ssr && environment.ssr) {
            m_ssr->Resize(sw, sh);
        }
        m_renderW = sw;
        m_renderH = sh;
        m_postProcess->BeginScene();
        m_renderer.Clear();
        m_renderingHdrPreview = true;
    } else {
        m_renderer.Clear();
    }

    const auto sceneCpuStart = std::chrono::high_resolution_clock::now();
    m_gpuProfiler.Begin("Scene");
    if (m_mode == EditorMode::Play && m_playRegistry) {
        DrawPlayScene(viewProj);
    } else {
        DrawEditScene(viewProj);
    }
    DrawWaterBodies(m_camera, GetWindow().AspectRatio());   // animated water surfaces (edit + play)
    m_gpuProfiler.End();
    m_cpuSceneMs = std::chrono::duration<double, std::milli>(
        std::chrono::high_resolution_clock::now() - sceneCpuStart).count();

    if (useHdrPost && m_postProcess) {
        m_gpuProfiler.Begin("Post");
        if (m_ssao) {
            m_postProcess->SetSceneTextures(
                m_ssao->NormalTexture(), m_ssao->VelocityTexture());
        } else {
            m_postProcess->SetSceneTextures(0, 0);
        }
        if (environment.ssr && m_ssr && m_ssao) {
            m_ssr->intensity = environment.ssrIntensity;
            m_ssr->Apply(m_postProcess->HdrColor(), m_ssao->PositionTexture(), m_ssao->NormalTexture(),
                         m_camera.ProjectionMatrix(window.AspectRatio()), m_postProcess->HdrFbo(),
                         m_renderW, m_renderH);
        }
        m_renderingHdrPreview = false;
        m_postProcess->RenderToScreen(window.Width(), window.Height(), m_dt);   // upscales to window
        m_gpuProfiler.End();
    }

    // Game HUD overlay (play mode): drawn on the presented scene, under the editor UI.
    if (m_mode == EditorMode::Play) {
        DrawPlayHud();
    }

    const auto uiCpuStart = std::chrono::high_resolution_clock::now();
    m_gpuProfiler.Begin("UI");
    // While the cursor is captured for play-mode mouse look, GLFW still reports a
    // virtual cursor position that the ImGui backend would feed to ImGui, making
    // panels light up as if hovered. Disable ImGui mouse input while captured.
    {
        ImGuiIO& io = ImGui::GetIO();
        if (m_playMouseCaptured) {
            io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
        } else {
            io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
        }
    }
    m_imgui.BeginFrame();
    DrawEditorOverlay();
    m_imgui.EndFrame();
    m_gpuProfiler.End();
    m_cpuUiMs = std::chrono::duration<double, std::milli>(
        std::chrono::high_resolution_clock::now() - uiCpuStart).count();

    m_cpuFrameMs = std::chrono::duration<double, std::milli>(
        std::chrono::high_resolution_clock::now() - cpuRenderStart).count();
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
    m_editAnimationPoses.clear();
    if (!m_modelShader && !m_skinnedRenderer) {
        return;
    }

    if (m_modelShader) {
        m_modelShader->Bind();
        m_modelShader->SetMat4("uViewProj", viewProj);
        m_modelShader->SetVec3("uLightPos", m_camera.Position() + glm::vec3(-4.0f, 6.0f, 4.0f));
        m_modelShader->SetVec3("uLightColor", glm::vec3(1.0f));
        m_modelShader->SetVec3("uViewPos", m_camera.Position());
    }

    const EditorScene::Environment& environment = m_scene.GetEnvironment();
    const engine::DayNightCycle::Sample sky = engine::DayNightCycle::At(environment.timeOfDay);
    const engine::Window& window = GetWindow();

    for (const EditorScene::Object& object : m_scene.Objects()) {
        if (!object.visible || object.modelAssetPath.empty()) {
            continue;
        }

        const Transform* transform = m_scene.TryGetTransform(object.entity);
        if (!transform) {
            continue;
        }

        std::string error;
        if (object.skeletalModel && m_skinnedRenderer) {
            const engine::SkinnedModel* model = m_editAssets.LoadSkinnedModel(object.modelAssetPath, &error);
            if (!model) {
                if (!m_editModelLoadErrors[object.modelAssetPath]) {
                    m_log.Error("Could not load edit skinned model: " + error);
                    m_editModelLoadErrors[object.modelAssetPath] = true;
                }
                continue;
            }

            engine::AnimatedModel animated;
            animated.SetModel(model);
            if (model->AnimationCount() > 0) {
                auto resolveClip = [&](int fallback, const std::string& name) {
                    int clip = fallback;
                    if (!name.empty()) {
                        const auto& animations = model->Animations();
                        for (std::size_t i = 0; i < animations.size(); ++i) {
                            if (animations[i].name == name) {
                                clip = static_cast<int>(i);
                                break;
                            }
                        }
                    }
                    return std::clamp(clip, 0, static_cast<int>(model->AnimationCount() - 1));
                };
                auto clipSeconds = [&](int clip) {
                    const auto& animations = model->Animations();
                    if (clip < 0 || clip >= static_cast<int>(animations.size())) {
                        return 0.0f;
                    }
                    return AnimationClipSeconds(animations[static_cast<std::size_t>(clip)]);
                };
                if (!object.animationStates.empty()) {
                    std::unordered_map<std::string, int> stateIndices;
                    for (const EditorScene::AnimationStateNode& stateNode : object.animationStates) {
                        const int clip = resolveClip(stateNode.clipIndex, stateNode.clipName);
                        const int index = animated.controller.AddState(engine::AnimationController::State{
                            stateNode.name.empty() ? std::string("State") : stateNode.name,
                            clip,
                            stateNode.loop,
                            std::max(stateNode.speed, 0.0f),
                            -std::numeric_limits<float>::infinity(),
                            std::numeric_limits<float>::infinity(),
                            clipSeconds(clip),
                            stateNode.blendClipIndex >= 0
                                ? resolveClip(stateNode.blendClipIndex, stateNode.blendClipName)
                                : -1,
                            stateNode.blendParameter,
                            stateNode.blendMin,
                            stateNode.blendMax,
                            stateNode.rootMotion
                        });
                        stateIndices[stateNode.name] = index;
                    }
                    for (const EditorScene::AnimationParameter& parameter : object.animationParameters) {
                        animated.controller.DeclareParameter({
                            parameter.name,
                            static_cast<engine::AnimationController::ParameterType>(parameter.type),
                            parameter.defaultValue
                        });
                    }
                    for (const EditorScene::AnimationStateTransition& transition : object.animationTransitions) {
                        const auto from = stateIndices.find(transition.fromState);
                        const auto to = stateIndices.find(transition.toState);
                        if ((!transition.fromState.empty() && from == stateIndices.end()) || to == stateIndices.end()) {
                            continue;
                        }
                        animated.controller.AddTransition(engine::AnimationController::Transition{
                            transition.fromState.empty() ? -1 : from->second,
                            to->second,
                            transition.parameter,
                            static_cast<engine::AnimationController::Transition::Compare>(
                            std::clamp(static_cast<int>(transition.compare), 0, 3)),
                            transition.threshold,
                            std::max(transition.fade, 0.0f),
                            std::clamp(transition.exitTime, 0.0f, 1.0f),
                            transition.priority,
                            transition.canInterrupt
                        });
                    }
                } else if (object.animationLocomotionEnabled) {
                    animated.controller = engine::AnimationController::Locomotion(
                        resolveClip(object.animationIdleClipIndex, object.animationIdleClipName),
                        resolveClip(object.animationWalkClipIndex, object.animationWalkClipName),
                        resolveClip(object.animationRunClipIndex, object.animationRunClipName),
                        std::max(object.animationWalkAt, 0.0f),
                        std::max(object.animationRunAt, object.animationWalkAt),
                        0.2f);
                    animated.controller.SetParameter(0.0f);
                } else {
                    const int clip = resolveClip(object.animationClipIndex, object.animationClipName);
                    animated.controller.AddState(engine::AnimationController::State{
                        object.animationClipName.empty() ? std::string("Preview") : object.animationClipName,
                        clip,
                        object.animationLoop,
                        std::max(object.animationSpeed, 0.0f)
                    });
                }
            }
            float& previewTime = m_animationPreviewTimes[object.entity];
            if (object.animationAutoplay) {
                previewTime += m_dt;
            }
            engine::ecs::Registry previewRegistry;
            const Entity previewEntity = previewRegistry.Create();
            previewRegistry.Add<Transform>(previewEntity, *transform);
            previewRegistry.Add<engine::AnimatedModel>(previewEntity, std::move(animated));
            if (engine::AnimatedModel* preview = previewRegistry.TryGet<engine::AnimatedModel>(previewEntity)) {
                for (const auto& entry : m_animationPreviewParameters) {
                    preview->controller.SetParameter(entry.first, entry.second);
                }
            }
            if (m_animationPreviewAction.active
                && m_animationPreviewAction.entity == object.entity
                && m_animationPreviewAction.clip >= 0
                && m_animationPreviewAction.clip < static_cast<int>(model->AnimationCount())) {
                if (engine::AnimatedModel* preview = previewRegistry.TryGet<engine::AnimatedModel>(previewEntity)) {
                    preview->controller.Update(previewTime);
                    preview->PlayAction(m_animationPreviewAction.clip,
                        {},
                        {},
                        m_animationPreviewAction.fadeIn,
                        m_animationPreviewAction.fadeOut,
                        m_animationPreviewAction.speed);
                    preview->action.time = m_animationPreviewAction.time;
                }
                engine::UpdateAnimations(previewRegistry, 0.0f);

                const engine::Animation& clip = model->Animations()[static_cast<std::size_t>(m_animationPreviewAction.clip)];
                const float duration = AnimationClipSeconds(clip);
                m_animationPreviewAction.time += m_dt * std::max(m_animationPreviewAction.speed, 0.0f);
                if (duration <= 0.0f || m_animationPreviewAction.time >= duration) {
                    m_animationPreviewAction.active = false;
                }
            } else {
                engine::UpdateAnimations(previewRegistry, previewTime);
            }
            if (engine::AnimatedModel* preview = previewRegistry.TryGet<engine::AnimatedModel>(previewEntity)) {
                m_editAnimationPoses[object.entity] = preview->pose;
                if (!object.materialAssetPath.empty()
                    && IsMaterialDocumentPath(object.materialAssetPath)) {
                    engine::ecs::MaterialAsset material;
                    material.path = object.materialAssetPath;
                    material.parameterOverrides = object.materialParameterOverrides;
                    previewRegistry.Add<engine::ecs::MaterialAsset>(
                        previewEntity, std::move(material));
                    m_editAssets.ResolveRegistryAssets(previewRegistry);
                }
                if (m_pbrRenderer) {
                    engine::SkinnedLighting lighting;
                    lighting.sunDir = sky.keyLightDirection;
                    lighting.sunColor = sky.keyLightColor * environment.sunIntensity;
                    lighting.ambient = sky.ambient * environment.skyLightIntensity;
                    lighting.cascade = &m_pbrRenderer->Cascade();
                    lighting.ibl = environment.ibl && m_ibl ? &*m_ibl : nullptr;
                    lighting.shadowSoftness = environment.shadowSoftness;
                    lighting.tonemap = !m_renderingHdrPreview;
                    lighting.fog = environment.fog;
                    lighting.fogColor = sky.horizon;
                    lighting.fogDensity = environment.fogDensity;
                    lighting.fogHeight = environment.fogHeight;
                    lighting.fogHeightFalloff = environment.fogHeightFalloff;
                    m_skinnedRenderer->DrawScene(
                        previewRegistry, m_camera, window.AspectRatio(), lighting);
                } else {
                    m_skinnedRenderer->Draw(*model,
                                            preview->pose,
                                            transform->Model(),
                                            m_camera,
                                            window.AspectRatio(),
                                            sky.keyLightDirection,
                                            sky.keyLightColor * environment.sunIntensity,
                                            sky.ambient * environment.skyLightIntensity);
                }
            }
            continue;
        }

        const engine::Model* model = m_editAssets.LoadModel(object.modelAssetPath, &error);
        if (!model) {
            if (!m_editModelLoadErrors[object.modelAssetPath]) {
                m_log.Error("Could not load edit model: " + error);
                m_editModelLoadErrors[object.modelAssetPath] = true;
            }
            continue;
        }
        if (!m_modelShader) {
            continue;
        }
        engine::ecs::Registry previewRegistry;
        const Entity previewEntity = previewRegistry.Create();
        previewRegistry.Add<Transform>(previewEntity, *transform);
        previewRegistry.Add<engine::ecs::LoadedModelAsset>(
            previewEntity, engine::ecs::LoadedModelAsset{model});
        if (!object.materialAssetPath.empty()
            && IsMaterialDocumentPath(object.materialAssetPath)) {
            engine::ecs::MaterialAsset material;
            material.path = object.materialAssetPath;
            material.parameterOverrides = object.materialParameterOverrides;
            previewRegistry.Add<engine::ecs::MaterialAsset>(
                previewEntity, std::move(material));
            m_editAssets.ResolveRegistryAssets(previewRegistry);
        }
        engine::ecs::RenderLoadedModels(
            previewRegistry, *m_modelShader, viewProj, sky.keyLightDirection,
            std::max({sky.keyLightColor.x, sky.keyLightColor.y,
                      sky.keyLightColor.z}) * environment.sunIntensity);
    }

    if (m_shader) {
        m_shader->Bind();
        m_shader->SetMat4("uViewProj", viewProj);
        m_shader->SetVec3("uLightDir", glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f)));
    }
}

void EditorApp::DrawSelectionOutline(const glm::mat4 & viewProj)
{
    if (m_mode != EditorMode::Edit || !m_cube || !m_outlineShader) {
        return;
    }

    const EditorScene::Object* selected = m_scene.SelectedObject();
    const Transform* transform = m_scene.SelectedTransform();
    if (!selected || !transform || !selected->visible || selected->navMeshBoundsVolume) {
        return;
    }

    if (!selected->modelAssetPath.empty()) {
        std::string error;
        const glm::vec3 color = selected->locked
            ? glm::vec3(1.0f, 0.28f, 0.08f)
            : glm::vec3(1.0f, 0.55f, 0.05f);
        const engine::Window& window = GetWindow();
        const glm::vec2 viewportSize(static_cast<float>(window.Width()), static_cast<float>(window.Height()));
        if (selected->skeletalModel && m_skinnedOutlineShader) {
            if (const engine::SkinnedModel* model = m_editAssets.LoadSkinnedModel(selected->modelAssetPath, &error)) {
                std::vector<glm::mat4> bindPose;
                const auto found = m_editAnimationPoses.find(selected->entity);
                const std::vector<glm::mat4>* pose = found != m_editAnimationPoses.end() ? &found->second : nullptr;
                if (!pose) {
                    engine::Animator::ComputeBindPose(model->GetSkeleton(), bindPose);
                    pose = &bindPose;
                }
                m_skinnedOutlineShader->Bind();
                m_skinnedOutlineShader->SetMat4("uViewProj", viewProj);
                m_skinnedOutlineShader->SetVec2("uViewportSize", viewportSize);
                m_viewport.DrawSelectedSkinnedModelOutline(m_renderer, *m_skinnedOutlineShader,
                    *transform, *model, *pose, color, 2.0f);
            }
            return;
        }
        const engine::Model* model = m_editAssets.LoadModel(selected->modelAssetPath, &error);
        if (model) {
            m_outlineShader->Bind();
            m_outlineShader->SetMat4("uViewProj", viewProj);
            m_outlineShader->SetVec2("uViewportSize", viewportSize);
            m_viewport.DrawSelectedModelOutline(m_renderer, *m_outlineShader, *transform, *model, color, 2.0f);
        }
        return;
    }

    // Terrain: outline the generated terrain mesh (from the terrain cache), not the flat
    // placeholder plane the object carries. The terrain mesh spans the sculpted surface, so
    // the highlight now hugs the mountains and stays visible when the camera faces them.
    if (selected->isTerrain) {
        const auto terrainIt = m_terrains.find(selected->entity);
        TerrainTrace("Outline: terrain selected found=%d ready=%d",
                     terrainIt != m_terrains.end() ? 1 : 0,
                     (terrainIt != m_terrains.end() && terrainIt->second.terrain.Ready()) ? 1 : 0);
        if (terrainIt != m_terrains.end() && terrainIt->second.terrain.Ready()) {
            TerrainTrace("Outline: drawing terrain mesh...");
            const glm::vec3 color = selected->locked
                ? glm::vec3(1.0f, 0.28f, 0.08f)
                : glm::vec3(1.0f, 0.55f, 0.05f);
            m_outlineShader->Bind();
            m_outlineShader->SetMat4("uViewProj", viewProj);
            const engine::Window& window = GetWindow();
            m_outlineShader->SetVec2("uViewportSize", glm::vec2(window.Width(), window.Height()));
            m_viewport.DrawSelectedMeshOutline(m_renderer, *m_outlineShader, *transform,
                                               terrainIt->second.terrain.GetMesh(), color, 2.0f);
            TerrainTrace("Outline: terrain mesh drawn");
        }
        return;
    }

    const MeshRenderer* renderer = m_scene.TryGetMeshRenderer(selected->entity);
    if (!renderer || !renderer->mesh) {
        return;
    }

    const glm::vec3 color = selected->locked
        ? glm::vec3(1.0f, 0.28f, 0.08f)
        : glm::vec3(1.0f, 0.55f, 0.05f);
    m_outlineShader->Bind();
    m_outlineShader->SetMat4("uViewProj", viewProj);
    const engine::Window& window = GetWindow();
    m_outlineShader->SetVec2("uViewportSize", glm::vec2(window.Width(), window.Height()));
    m_viewport.DrawSelectedMeshOutline(m_renderer, *m_outlineShader, *transform, *renderer->mesh, color, 2.0f);
}

void EditorApp::DrawEditorOverlay()
{
   const engine::Window& window = GetWindow();

    // Frame profiler overlay: CPU render cost + per-pass GPU timings (previous frame).
    if (m_showProfiler) {
        if (ImGui::Begin("Profiler", &m_showProfiler,
                         ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing)) {
            const ImGuiIO& io = ImGui::GetIO();
            ImGui::Text("FPS: %.0f  (%.2f ms/frame)", io.Framerate,
                        io.Framerate > 0.0f ? 1000.0f / io.Framerate : 0.0f);
            ImGui::Text("CPU render: %.2f ms", m_cpuFrameMs);
            ImGui::Text("  scene submit: %.2f ms", m_cpuSceneMs);
            ImGui::Text("  ui build:     %.2f ms", m_cpuUiMs);
            ImGui::Separator();
            double gpuTotal = 0.0;
            for (const std::pair<std::string, double>& r : m_gpuProfiler.Results()) {
                ImGui::Text("GPU %-8s %6.2f ms", r.first.c_str(), r.second);
                gpuTotal += r.second;
            }
            if (!m_gpuProfiler.Results().empty()) {
                ImGui::Separator();
                ImGui::Text("GPU total  %6.2f ms", gpuTotal);
            } else {
                ImGui::TextDisabled("GPU timings warming up...");
            }
        }
        ImGui::End();
    }

    m_audio.SetListener(m_camera.Position(), m_camera.Front());
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
    dockspaceContext.camera = &m_camera;
    dockspaceContext.cameraShakeActive = m_cameraShake.Active();
    dockspaceContext.cameraSequenceActive = m_cameraSequence.Active();
    dockspaceContext.cameraSequenceInputLocked = m_cameraDirector.InputLocked();
    dockspaceContext.cameraSequenceSkippable = m_cameraDirector.Skippable();
    dockspaceContext.cameraSequenceActiveName = m_cameraDirector.ActiveName();
    dockspaceContext.showCameraRails = &m_showCameraRails;
    dockspaceContext.cameraSequenceTime = m_cameraSequence.Time();
    dockspaceContext.cameraSequenceDuration = m_cameraSequence.Duration();
    dockspaceContext.cameraSequencePaused = m_cameraSequencePaused;
    dockspaceContext.modeName = m_mode == EditorMode::Edit ? "Edit" : "Play";
    dockspaceContext.playMode = m_mode == EditorMode::Play;
    dockspaceContext.physicsPaused = m_physicsPaused;
    dockspaceContext.physicsFixedTimestep = m_physicsFixedTimestep;
    dockspaceContext.physicsAccumulator = m_physicsAccumulator;
    dockspaceContext.physicsStepsLastFrame = m_physicsStepsLastFrame;
    dockspaceContext.physicsEventEnterCount = m_physicsEventEnterCount;
    dockspaceContext.physicsEventStayCount = m_physicsEventStayCount;
    dockspaceContext.physicsEventExitCount = m_physicsEventExitCount;
    dockspaceContext.physicsActionCount = m_physicsActionCount;
    dockspaceContext.physicsEventRows = &m_physicsEventRows;
    dockspaceContext.gameplayDebug = BuildGameplayDebugState();
    dockspaceContext.animationPreview = BuildAnimationPreviewState();
    dockspaceContext.showPhysicsEventGuides = &m_showPhysicsEventGuides;
    dockspaceContext.showAiDebug = &m_showAiDebug;
    dockspaceContext.useNavMesh = &m_useNavMesh;
    dockspaceContext.vsync = GetWindow().IsVSync();
    dockspaceContext.terrainSculpt = &m_terrainSculpt;
    dockspaceContext.terrainSculptMode = &m_terrainSculptMode;
    dockspaceContext.terrainPaintLayer = &m_terrainPaintLayer;
    dockspaceContext.terrainBrushRadius = &m_terrainBrushRadius;
    dockspaceContext.terrainBrushStrength = &m_terrainBrushStrength;
    dockspaceContext.showNavigationPreview = &m_showNavigationPreview;
    dockspaceContext.showParticleDebug = &m_showParticleDebug;
    dockspaceContext.particleDebugSelectedOnly = &m_particleDebugSelectedOnly;
    dockspaceContext.particleDebugShapes = &m_particleDebugShapes;
    dockspaceContext.particleDebugDirections = &m_particleDebugDirections;
    dockspaceContext.particleDebugBounds = &m_particleDebugBounds;
    dockspaceContext.particleDebugCullingState = &m_particleDebugCullingState;
    dockspaceContext.navigationPreviewPolygons = static_cast<int>(m_editorNavMesh.polys.size());
    dockspaceContext.physicsEventGuidesSelectedOnly = &m_physicsEventGuidesSelectedOnly;
    dockspaceContext.physicsEventGuidesTriggersOnly = &m_physicsEventGuidesTriggersOnly;
    dockspaceContext.physicsEventGuidesEnterExitOnly = &m_physicsEventGuidesEnterExitOnly;
    dockspaceContext.scenePathBuffer = m_scenePathDraft.data();
    dockspaceContext.scenePathBufferSize = m_scenePathDraft.size();
    dockspaceContext.fps = m_fps;
    if (m_particleRenderer) {
        const engine::ParticleRenderer::Stats& stats = m_particleRenderer->GetStats();
        dockspaceContext.particleDrawCalls = stats.drawCalls;
        dockspaceContext.particleCulledEmitters = stats.culledEmitters;
        dockspaceContext.particleRenderedCount = stats.particles;
        dockspaceContext.particleCpuMilliseconds = stats.cpuMilliseconds;
        dockspaceContext.particleGpuMilliseconds = stats.gpuMilliseconds;
    }
    if (const EditorScene::Object* selected = m_scene.SelectedObject()) {
        dockspaceContext.animationPreviewTime = &m_animationPreviewTimes[selected->entity];
    }
    dockspaceContext.animationActionClip = &m_animationActionClip;
    dockspaceContext.animationActionFadeIn = &m_animationActionFadeIn;
    dockspaceContext.animationActionFadeOut = &m_animationActionFadeOut;
    dockspaceContext.animationActionSpeed = &m_animationActionSpeed;
    dockspaceContext.animationActionMaskRoot = m_animationActionMaskRoot.data();
    dockspaceContext.animationActionMaskRootSize = m_animationActionMaskRoot.size();
    dockspaceContext.animationPreviewParameters = &m_animationPreviewParameters;
    dockspaceContext.audioAvailable = m_audio.IsAvailable();
    for (int i = static_cast<int>(engine::AudioBus::Master);
         i < static_cast<int>(engine::AudioBus::Count); ++i) {
        const engine::AudioBus bus = static_cast<engine::AudioBus>(i);
        dockspaceContext.audioBusVolumes[static_cast<std::size_t>(i)] = m_audio.BusVolume(bus);
        dockspaceContext.audioBusMuted[static_cast<std::size_t>(i)] = m_audio.IsBusMuted(bus);
        dockspaceContext.audioBusEffects[static_cast<std::size_t>(i)] = m_audio.BusEffects(bus);
    }
    dockspaceContext.activeAudioSnapshot = m_audio.ActiveSnapshot();
    dockspaceContext.dialogueDucking = m_audio.DialogueDuckingEnabled();
    dockspaceContext.audioDebugStats = m_audio.GetDebugStats();
    dockspaceContext.audioDeviceInfo = m_audio.GetDeviceInfo();
    dockspaceContext.audioMaxVoices = m_audio.MaxVoices();
    engine::AudioEngine::SourceHandle selectedRuntimeAudio = engine::AudioEngine::InvalidSource;
    if (m_mode == EditorMode::Play) {
        if (const EditorScene::Object* selected = m_scene.SelectedObject()) {
            for (const PlayAudioSource& source : m_playAudioSources) {
                if (source.name == selected->name) {
                    selectedRuntimeAudio = source.source;
                    break;
                }
            }
        }
    }
    if (selectedRuntimeAudio != engine::AudioEngine::InvalidSource) {
        dockspaceContext.selectedRuntimeAudioAvailable = true;
        dockspaceContext.selectedRuntimeAudioPlaying = m_audio.IsSourcePlaying(selectedRuntimeAudio);
        dockspaceContext.selectedRuntimeAudioPaused = m_audio.IsSourcePaused(selectedRuntimeAudio);
        dockspaceContext.selectedRuntimeAudioCursor = m_audio.SourceCursorSeconds(selectedRuntimeAudio);
    }
    dockspaceContext.sceneDirty = m_scene.IsDirty();
    const bool dockspaceDrawn = m_dockspace.Draw(dockspaceContext);
    if (dockspaceContext.cameraBlendRequested && m_mode == EditorMode::Edit) {
        BeginCameraBlend(dockspaceContext.cameraBlendPreset);
    }
    if (dockspaceContext.cameraShakeStopRequested) {
        m_cameraShake.Clear();
        RestoreCameraBeforeShake();
    }
    if (dockspaceContext.cameraShakeRequested) {
        m_cameraShake.Start(dockspaceContext.cameraShakeSettings);
    }
    if (dockspaceContext.cameraSequenceStopRequested) {
        m_cameraSequence.Stop();
        m_cameraDirector.SetStopped();
        m_activeCinematicCues.clear();
        m_cameraSequencePaused = false;
    }
    if (dockspaceContext.cameraSequencePlayRequested) {
        StartCameraSequence(dockspaceContext.cameraSequence);
    }
    if (dockspaceContext.cameraSequencePauseToggleRequested && m_cameraSequence.Active()) {
        m_cameraSequencePaused = !m_cameraSequencePaused;
    }
    if (dockspaceContext.cameraSequenceSeekRequested && m_cameraSequence.Active()) {
        engine::CameraBlend::Apply(
            m_cameraSequence.Seek(dockspaceContext.cameraSequenceSeekTime), m_camera);
    }
    for (int i = static_cast<int>(engine::AudioBus::Master);
         i < static_cast<int>(engine::AudioBus::Count); ++i) {
        const engine::AudioBus bus = static_cast<engine::AudioBus>(i);
        m_audio.SetBusVolume(bus, dockspaceContext.audioBusVolumes[static_cast<std::size_t>(i)]);
        m_audio.SetBusMuted(bus, dockspaceContext.audioBusMuted[static_cast<std::size_t>(i)]);
        m_audio.SetBusEffects(bus, dockspaceContext.audioBusEffects[static_cast<std::size_t>(i)]);
    }
    m_audio.SetDialogueDucking(dockspaceContext.dialogueDucking);
    if (dockspaceContext.audioMaxVoicesChanged)
        m_audio.SetMaxVoices(dockspaceContext.audioMaxVoices);
    if (dockspaceContext.saveAudioMixerPresetRequested) {
        std::string error;
        if (!engine::SaveAudioMixerPreset(dockspaceContext.audioMixerPresetPath.data(),
                m_audio.CaptureMixerPreset("Project Mixer"), &error))
            m_log.Error("Audio mixer: " + error);
        else
            m_log.Info("Saved audio mixer preset: "
                + std::string(dockspaceContext.audioMixerPresetPath.data()));
    }
    if (dockspaceContext.loadAudioMixerPresetRequested) {
        engine::AudioMixerPreset preset;
        std::string error;
        if (engine::LoadAudioMixerPreset(dockspaceContext.audioMixerPresetPath.data(),
                                         &preset, &error))
            m_audio.ApplyMixerPreset(preset);
        else
            m_log.Error("Audio mixer: " + error);
    }
    if (dockspaceContext.audioSnapshotRequested) {
        m_audio.ApplySnapshot(dockspaceContext.requestedAudioSnapshot,
                              dockspaceContext.audioSnapshotTransition);
    }
    DrawMaterialMakerPanel();
    DrawBehaviorGraphPanel();
    DrawParticleEditorPanel();
    DrawShaderEditorPanel();
    DrawHudEditorPanel();
    DrawDirtyScenePrompt();
    if (selectedRuntimeAudio != engine::AudioEngine::InvalidSource) {
        if (dockspaceContext.runtimeAudioRestartRequested) m_audio.PlaySource(selectedRuntimeAudio, true);
        if (dockspaceContext.runtimeAudioPauseResumeRequested) {
            if (m_audio.IsSourcePaused(selectedRuntimeAudio)) m_audio.ResumeSource(selectedRuntimeAudio);
            else m_audio.PauseSource(selectedRuntimeAudio);
        }
        if (dockspaceContext.runtimeAudioStopRequested) m_audio.StopSource(selectedRuntimeAudio);
    }
    if (dockspaceContext.stopAudioPreviewRequested) {
        m_audio.StopAllSounds();
        m_audio.StopMusic();
    }
    if (dockspaceContext.previewAudioRequested && !dockspaceContext.previewAudioPath.empty()) {
        m_audio.StopAllSounds();
        m_audio.StopMusic();
        std::string previewExtension =
            std::filesystem::path(dockspaceContext.previewAudioPath).extension().string();
        std::transform(previewExtension.begin(), previewExtension.end(), previewExtension.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (previewExtension == ".3dgaudio") {
            const Transform* sourceTransform = m_scene.SelectedTransform();
            m_audio.PlayCue(dockspaceContext.previewAudioPath,
                sourceTransform ? sourceTransform->position : glm::vec3(0.0f),
                !dockspaceContext.previewAudioSpatial);
        } else if (!dockspaceContext.previewAudioSpatial && dockspaceContext.previewAudioLoop) {
            m_audio.PlayMusic(dockspaceContext.previewAudioPath,
                dockspaceContext.previewAudioVolume, dockspaceContext.previewAudioBus);
        } else if (dockspaceContext.previewAudioSpatial) {
            m_audio.SetAttenuation(dockspaceContext.previewAudioMinDistance,
                dockspaceContext.previewAudioMaxDistance, dockspaceContext.previewAudioRolloff);
            const Transform* sourceTransform = m_scene.SelectedTransform();
            m_audio.PlayAt(dockspaceContext.previewAudioPath,
                sourceTransform ? sourceTransform->position : glm::vec3(0.0f),
                dockspaceContext.previewAudioPitch, dockspaceContext.previewAudioVolume,
                dockspaceContext.previewAudioBus);
        } else {
            m_audio.Play(dockspaceContext.previewAudioPath,
                dockspaceContext.previewAudioPitch, dockspaceContext.previewAudioVolume,
                dockspaceContext.previewAudioBus);
        }
        m_log.Info("Previewing audio: " + dockspaceContext.previewAudioPath);
    }
    if (dockspaceContext.rebuildNavigationPreviewRequested) {
        BakeEditorNavMesh();
    }
    if (dockspaceContext.viewportDropRequested && m_dragDrop.HasPayload()) {
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
    if (!dockspaceContext.sceneAssetOpenRequested.empty()) {
        RequestLoadSceneFromPath(dockspaceContext.sceneAssetOpenRequested);
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
        m_physicsActionCount = 0;
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
    if (dockspaceContext.addCapsuleRequested) {
        AddCapsule();
    }
    if (dockspaceContext.addConfiguredPrimitiveRequested) {
        AddConfiguredPrimitive(dockspaceContext.configuredPrimitive,
            dockspaceContext.configuredPrimitiveTransform,
            dockspaceContext.configuredPrimitiveColliderEnabled
                ? &dockspaceContext.configuredPrimitiveCollider : nullptr,
            dockspaceContext.configuredPrimitiveName);
    }
    if (dockspaceContext.addDynamicCubeRequested) {
        AddDynamicCube();
    }
    if (dockspaceContext.vsyncChangeRequested) {
        GetWindow().SetVSync(dockspaceContext.vsync);
        m_log.Info(dockspaceContext.vsync ? "VSync on" : "VSync off");
    }
    if (dockspaceContext.addStaticFloorRequested) {
        AddStaticFloor();
    }
    if (dockspaceContext.addTerrainRequested) {
        AddTerrain();
    }
    if (dockspaceContext.addWaterRequested) {
        AddWater();
    }
    if (dockspaceContext.addTriggerVolumeRequested) {
        AddTriggerVolume();
    }
    if (dockspaceContext.addNavMeshBoundsVolumeRequested) {
        AddNavMeshBoundsVolume();
    }
    if (dockspaceContext.addPlayerStartRequested) {
        AddPlayerStart();
    }
    if (dockspaceContext.addDoorRequested) {
        AddGameplayDoor();
    }
    if (dockspaceContext.addPickupRequested) {
        AddGameplayPickup();
    }
    if (dockspaceContext.addDamageZoneRequested) {
        AddGameplayDamageZone();
    }
    if (dockspaceContext.addMovingPlatformRequested) {
        AddGameplayMovingPlatform();
    }
    if (dockspaceContext.addTriggerMoverTestRequested) {
        AddGameplayTriggerMoverTest();
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
            const char* primitiveType = selected->primitive == EditorScene::Primitive::Plane ? "Plane"
                : selected->primitive == EditorScene::Primitive::Sphere ? "Sphere"
                : selected->primitive == EditorScene::Primitive::Capsule ? "Capsule"
                : selected->primitive == EditorScene::Primitive::Cylinder ? "Cylinder"
                : selected->primitive == EditorScene::Primitive::Cone ? "Cone"
                : selected->primitive == EditorScene::Primitive::Pyramid ? "Pyramid"
                : selected->primitive == EditorScene::Primitive::Torus ? "Torus"
                : selected->primitive == EditorScene::Primitive::Staircase ? "Staircase"
                : "Cube";
            std::snprintf(line, sizeof(line), "Type: %s",
                selected->modelAssetPath.empty() ? primitiveType : "Model");
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

    std::snprintf(line, sizeof(line), "Gizmo: %s %s %s%s   < / > or right-drag",
        m_gizmo.ModeName(), m_gizmo.AxisName(), m_gizmo.SpaceName(),
        m_gizmo.SnappingEnabled() ? " Snap" : "");
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

void EditorApp::DrawParticleEditorPanel() {
    if (!m_panels.IsOpen(EditorPanels::Panel::ParticleEditor)) return;
    bool open = true;
    m_particleEditor.Draw(m_scene, m_assets, &open, m_dt);
    m_panels.SetOpen(EditorPanels::Panel::ParticleEditor, open);
}

void ResolveParticleGraphShader(
    engine::ParticleSystemComponent& system,
    engine::RuntimeAssetManager& assets) {
    system.config.customShader = nullptr;
    system.config.shaderTextures.clear();
    if (system.config.shaderPath.empty()) return;
    std::string error;
    system.config.customShader =
        assets.LoadShader(system.config.shaderPath, false, &error);
    for (const engine::ParticleShaderParameter& parameter :
         system.config.shaderParameters) {
        if (parameter.type
                != static_cast<int>(engine::ShaderValueType::Texture2D)
            || parameter.value.empty()) continue;
        const engine::Texture* texture =
            assets.LoadTexture(parameter.value, &error);
        if (texture)
            system.config.shaderTextures[parameter.name] = texture;
    }
}

void EditorApp::DrawShaderEditorPanel() {
    if (!m_panels.IsOpen(EditorPanels::Panel::ShaderEditor)) return;
    bool open = true;
    m_shaderEditor.Draw(m_assets, &open);
    m_panels.SetOpen(EditorPanels::Panel::ShaderEditor, open);
}

unsigned int EditorApp::HudTextureId(const std::string& relPath) {
    if (relPath.empty()) return 0;
    const std::filesystem::path full = std::filesystem::path(m_project.AssetRoot()) / relPath;
    const engine::Texture* tex = m_editAssets.LoadTexture(full.string());
    return tex ? tex->ID() : 0u;
}

void EditorApp::ScanHudImages() {
    m_hudImageChoices.clear();
    std::error_code ec;
    const std::filesystem::path root(m_project.AssetRoot());
    if (!std::filesystem::exists(root, ec)) return;

    for (std::filesystem::recursive_directory_iterator it(root, ec), end; it != end; it.increment(ec)) {
        if (ec) break;
        if (!it->is_regular_file(ec)) continue;
        std::string ext = it->path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp") {
            const std::string rel = std::filesystem::relative(it->path(), root, ec).generic_string();
            if (!rel.empty()) m_hudImageChoices.push_back(rel);
        }
    }
    std::sort(m_hudImageChoices.begin(), m_hudImageChoices.end());
}

void EditorApp::DrawHudEditorPanel() {
    if (!m_panels.IsOpen(EditorPanels::Panel::Hud)) return;

    if (m_hudImageChoices.empty()) ScanHudImages();   // first-open populate
    const auto texLookup = [this](const std::string& rel) { return HudTextureId(rel); };

    bool open = true;
    const HudEditorPanel::Result r = m_hudPanel.Draw(m_hud, &open, m_hudImageChoices, texLookup);

    if (r.refreshImagesRequested) ScanHudImages();

    if (r.newRequested) {
        m_hud.Clear();
        m_hudPanel.SetSelected(-1);
        m_log.Info("HUD: new document");
    }
    if (r.saveRequested && !r.path.empty()) {
        std::error_code ec;
        const std::filesystem::path fp(r.path);
        if (fp.has_parent_path()) std::filesystem::create_directories(fp.parent_path(), ec);
        std::string err;
        if (m_hud.Save(r.path, &err)) { m_hudPath = r.path; m_log.Info("HUD saved: " + r.path); }
        else m_log.Error("HUD save failed: " + err);
    }
    if (r.loadRequested && !r.path.empty()) {
        std::string err;
        if (m_hud.Load(r.path, &err)) {
            m_hudPath = r.path;
            m_hudPanel.SetSelected(-1);
            m_log.Info("HUD loaded: " + r.path);
        } else {
            m_log.Error("HUD load failed: " + err);
        }
    }
    if (r.setAsSceneHud && !r.path.empty()) {
        EditorScene::Environment env = m_scene.GetEnvironment();
        env.hudAsset = r.path;
        m_scene.SetEnvironment(env);
        m_hudPath = r.path;
        m_log.Info("HUD set as scene HUD: " + r.path);
    }

    m_panels.SetOpen(EditorPanels::Panel::Hud, open);
}

void EditorApp::DrawPlayHud() {
    if (m_mode != EditorMode::Play || !m_text || m_hud.widgets.empty()) return;

    const engine::Window& window = GetWindow();
    engine::HudContext ctx;

    // Resolve optional Unlit/UI graph shaders and their reflected defaults.
    for (engine::HudWidget& widget : m_hud.widgets) {
        widget.customShader = nullptr;
        widget.shaderTextures.clear();
        if (widget.shaderPath.empty() || widget.type == engine::HudWidgetType::Text) continue;
        engine::ShaderAsset asset;
        std::string error;
        if (!engine::LoadShaderAsset(widget.shaderPath, &asset, &error)
            || asset.domain != engine::ShaderDomain::Unlit) {
            continue;
        }
        widget.customShader = m_editAssets.LoadShader(widget.shaderPath, false, &error);
        for (const engine::ShaderParameter& parameter : asset.parameters) {
            widget.shaderParameterTypes[parameter.name] = static_cast<int>(parameter.type);
            auto [it, inserted] =
                widget.shaderParameters.emplace(parameter.name, parameter.defaultValue);
            if (parameter.type == engine::ShaderValueType::Texture2D && !it->second.empty()) {
                widget.shaderTextures[parameter.name] = m_editAssets.LoadTexture(it->second);
            }
        }
    }

    // Health from the play player entity (if it carries a Health component).
    if (m_playRegistry && m_playPlayerEntity != engine::ecs::kNull) {
        if (const engine::Health* h = m_playRegistry->TryGet<engine::Health>(m_playPlayerEntity)) {
            ctx.hasHealth = true;
            ctx.health = h->hp;
            ctx.maxHealth = h->maxHp;
            ctx.healthFraction = h->maxHp > 0.0f ? h->hp / h->maxHp : 0.0f;
            ctx.alive = h->alive;
        }
    }

    // Resolve Image widget paths -> GL texture ids (cached in m_editAssets).
    ctx.textureLookup = [this](const std::string& rel) { return HudTextureId(rel); };

    // Named values: gameplay-pushed map plus a couple of built-ins.
    ctx.floats = m_hudFloats;
    ctx.strings = m_hudStrings;
    ctx.floats["fps"] = m_fps;
    if (ctx.hasHealth) {
        ctx.floats["hp"] = ctx.health;
        ctx.floats["maxhp"] = ctx.maxHealth;
    }

    // Cursor + click edge for button hit-testing (only when the cursor is free).
    ctx.cursorActive = !m_playMouseCaptured;
    ctx.cursorX = window.MouseX();
    ctx.cursorY = window.MouseY();
    const bool down = window.IsMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT);
    ctx.mousePressed = down && !m_hudMousePrev && ctx.cursorActive;
    m_hudMousePrev = down;

    const engine::HudDrawResult result =
        engine::DrawHud(*m_text, m_hud, ctx, window.Width(), window.Height());

    switch (result.clickedAction) {
        case engine::HudButtonAction::ExitPlay:    ExitPlayMode(); break;
        case engine::HudButtonAction::RestartPlay: ExitPlayMode(); EnterPlayMode(); break;
        case engine::HudButtonAction::EmitEvent:
            if (!result.clickedKey.empty()) m_hudFloats[result.clickedKey] = 1.0f;
            break;
        case engine::HudButtonAction::None:
        default: break;
    }
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

void EditorApp::DrawBehaviorGraphPanel() {
    if (!m_panels.IsOpen(EditorPanels::Panel::BehaviorGraph)) {
        return;
    }

    // Live debugger: feed the panel the first running graph agent's node status +
    // blackboard so it can highlight nodes and show values during Play.
    const PlayAgent* debugAgent = nullptr;
    if (m_mode == EditorMode::Play) {
        for (const PlayAgent& a : m_playAgents) {
            if (a.useGraph) { debugAgent = &a; break; }
        }
    }
    if (debugAgent) {
        m_behaviorGraph.SetDebugSnapshot(debugAgent->name, debugAgent->ctx.nodeStatus,
                                         debugAgent->ctx.blackboard.Snapshot());
    } else {
        m_behaviorGraph.ClearDebug();
    }

    bool open = true;
    if (ImGui::Begin(EditorPanels::Name(EditorPanels::Panel::BehaviorGraph), &open)) {
        m_behaviorGraph.DrawContent();
    }
    ImGui::End();
    m_panels.SetOpen(EditorPanels::Panel::BehaviorGraph, open);
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
        ImGui::SameLine();
        if (ImGui::Button("Use as Height")) {
            m_materialMaker.SetHeightMap(texturePath);
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
    const bool selectedShader = selectedAsset && selectedAsset->type == EditorAssets::Type::Shader
        && std::filesystem::path(selectedAsset->relativePath).extension() == ".3dgshader";
    if (selectedShader && ImGui::Button("Use Selected Shader in Material")) {
        const std::string shaderPath = m_content.AssetFullPath(m_assets, *selectedAsset);
        if (m_materialMaker.SetShaderAsset(shaderPath))
            m_log.Info("Loaded shader parameters into Material Maker");
        else
            m_log.Warning(m_materialMaker.StatusMessage());
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

EditorDockspace::GameplayDebugState EditorApp::BuildGameplayDebugState() {
    EditorDockspace::GameplayDebugState debug;
    const EditorScene::Object* selected = m_scene.SelectedObject();
    if (!selected) {
        return debug;
    }

    debug.hasSelection = true;
    debug.selectedName = selected->name;
    debug.authoredFieldCount = static_cast<int>(selected->scriptFields.size());

    if (selected->healthEnabled && m_mode != EditorMode::Play) {
        debug.hasHealth = true;
        debug.health = selected->health.hp;
        debug.maxHealth = selected->health.maxHp;
        debug.healthAlive = selected->health.alive;
        debug.healthJustDied = selected->health.justDied;
    }
    if (selected->scriptEnabled && m_mode != EditorMode::Play) {
        debug.hasScript = true;
        debug.scriptEnabled = selected->scriptEnabled;
        debug.scriptClassName = selected->scriptClassName;
        debug.scriptPath = selected->scriptPath;
        debug.runtimeFieldCount = static_cast<int>(selected->scriptFields.size());
    }

    if (m_mode != EditorMode::Play || !m_playRegistry) {
        return debug;
    }

    engine::ecs::Entity playEntity = engine::ecs::kNull;
    for (const auto& entry : m_playEntityNames) {
        if (entry.second == selected->name) {
            playEntity = entry.first;
            break;
        }
    }
    if (playEntity == engine::ecs::kNull || !m_playRegistry->Valid(playEntity)) {
        return debug;
    }

    debug.playEntityFound = true;
    if (engine::Health* health = m_playRegistry->TryGet<engine::Health>(playEntity)) {
        debug.hasHealth = true;
        debug.health = health->hp;
        debug.maxHealth = health->maxHp;
        debug.healthAlive = health->alive;
        debug.healthJustDied = health->justDied;
    }

    if (engine::NativeScriptComponent* script = m_playRegistry->TryGet<engine::NativeScriptComponent>(playEntity)) {
        debug.hasScript = true;
        debug.scriptEnabled = script->enabled;
        debug.scriptCreated = script->created;
        debug.scriptMissingFactory = script->missingFactory;
        debug.scriptClassName = script->className;
        debug.scriptPath = script->sourcePath;
        debug.runtimeFieldCount = static_cast<int>(script->fields.size());
    }

    for (const EditorDockspace::PhysicsEventRow& row : m_physicsEventRows) {
        if (!row.trigger || (row.objectA != selected->name && row.objectB != selected->name)) {
            continue;
        }
        if (row.phase == 0) {
            ++debug.selectedTriggerEnterCount;
        } else if (row.phase == 1) {
            ++debug.selectedTriggerTouchCount;
        } else if (row.phase == 2) {
            ++debug.selectedTriggerExitCount;
        }
    }
    return debug;
}

EditorDockspace::AnimationPreviewState EditorApp::BuildAnimationPreviewState() {
    EditorDockspace::AnimationPreviewState state;
    const EditorScene::Object* selected = m_scene.SelectedObject();
    if (!selected) {
        return state;
    }

    state.hasSelection = true;
    state.selectedName = selected->name;
    state.skeletalModel = selected->skeletalModel;
    state.modelPath = selected->modelAssetPath;
    state.playMode = m_mode == EditorMode::Play;
    state.defaultClipIndex = selected->animationClipIndex;
    state.defaultClipName = selected->animationClipName;
    state.autoplay = selected->animationAutoplay;
    state.loop = selected->animationLoop;
    state.playbackSpeed = selected->animationSpeed;
    state.events = selected->animationEvents;
    state.actionProfiles = selected->animationActionProfiles;
    state.states = selected->animationStates;
    state.parameterDefinitions = selected->animationParameters;
    state.transitions = selected->animationTransitions;
    auto addPreviewParameter = [&](const std::string& name, float value,
                                   EditorScene::AnimationParameter::Type type = EditorScene::AnimationParameter::Type::Float) {
        const std::string parameterName = name.empty() ? std::string("Speed") : name;
        for (EditorDockspace::AnimationPreviewState::ParameterInfo& parameter : state.parameters) {
            if (parameter.name == parameterName) {
                parameter.value = value;
                parameter.type = type;
                return;
            }
        }
        state.parameters.push_back(EditorDockspace::AnimationPreviewState::ParameterInfo{
            parameterName,
            value,
            type
        });
    };
    for (const EditorScene::AnimationParameter& parameter : selected->animationParameters) {
        const auto found = m_animationPreviewParameters.find(parameter.name);
        addPreviewParameter(parameter.name,
            found == m_animationPreviewParameters.end() ? parameter.defaultValue : found->second,
            parameter.type);
    }
    for (const EditorScene::AnimationStateTransition& transition : selected->animationTransitions) {
        const std::string name = transition.parameter.empty() ? std::string("Speed") : transition.parameter;
        const auto found = m_animationPreviewParameters.find(name);
        auto type = EditorScene::AnimationParameter::Type::Float;
        for (const EditorScene::AnimationParameter& definition : selected->animationParameters) {
            if (definition.name == name) { type = definition.type; break; }
        }
        addPreviewParameter(name, found == m_animationPreviewParameters.end() ? 0.0f : found->second, type);
    }
    state.actionPlaying = m_animationPreviewAction.active
        && m_animationPreviewAction.entity == selected->entity;
    const auto previewTimeIt = m_animationPreviewTimes.find(selected->entity);
    state.previewTime = previewTimeIt != m_animationPreviewTimes.end() ? previewTimeIt->second : 0.0f;
    state.locomotionEnabled = selected->animationLocomotionEnabled;
    state.idleClipIndex = selected->animationIdleClipIndex;
    state.walkClipIndex = selected->animationWalkClipIndex;
    state.runClipIndex = selected->animationRunClipIndex;
    state.idleClipName = selected->animationIdleClipName;
    state.walkClipName = selected->animationWalkClipName;
    state.runClipName = selected->animationRunClipName;
    state.walkAt = selected->animationWalkAt;
    state.runAt = selected->animationRunAt;

    auto fillClips = [&](const engine::SkinnedModel& model) {
        state.modelLoaded = true;
        state.clips.clear();
        for (const engine::Animation& clip : model.Animations()) {
            const float ticksPerSecond = clip.ticksPerSecond > 0.0f ? clip.ticksPerSecond : 25.0f;
            state.clips.push_back(EditorDockspace::AnimationPreviewState::ClipInfo{
                clip.name,
                clip.duration > 0.0f ? clip.duration / ticksPerSecond : 0.0f
            });
        }
        if (!state.clips.empty()) {
            const int clipIndex = std::clamp(state.defaultClipIndex, 0, static_cast<int>(state.clips.size() - 1));
            state.previewDuration = state.clips[static_cast<std::size_t>(clipIndex)].durationSeconds;
        }

        const engine::Skeleton& skeleton = model.GetSkeleton();
        state.bones.clear();
        state.bones.reserve(skeleton.bones.size());
        for (std::size_t i = 0; i < skeleton.bones.size(); ++i) {
            const engine::Bone& bone = skeleton.bones[i];
            int depth = 0;
            int parent = bone.parent;
            while (parent >= 0 && parent < static_cast<int>(skeleton.bones.size())) {
                ++depth;
                parent = skeleton.bones[static_cast<std::size_t>(parent)].parent;
            }
            state.bones.push_back(EditorDockspace::AnimationPreviewState::BoneInfo{
                bone.name,
                bone.parent,
                depth
            });
        }
    };

    if (selected->skeletalModel && !selected->modelAssetPath.empty()) {
        std::string error;
        if (const engine::SkinnedModel* model = m_editAssets.LoadSkinnedModel(selected->modelAssetPath, &error)) {
            fillClips(*model);
        } else {
            state.loadError = error;
        }
    }

    {
        std::unordered_set<std::string> names;
        for (const EditorScene::AnimationStateNode& node : state.states) {
            if (node.name.empty()) {
                state.graphWarnings.push_back("A state has no name.");
            } else if (!names.insert(node.name).second) {
                state.graphWarnings.push_back("Duplicate state name: " + node.name);
            }
            if (state.modelLoaded && (node.clipIndex < 0 || node.clipIndex >= static_cast<int>(state.clips.size()))) {
                state.graphWarnings.push_back("State '" + node.name + "' references a missing clip.");
            }
        }
        std::unordered_set<std::string> parameters;
        for (const EditorScene::AnimationParameter& parameter : state.parameterDefinitions) {
            if (parameter.name.empty()) state.graphWarnings.push_back("A graph parameter has no name.");
            else if (!parameters.insert(parameter.name).second) {
                state.graphWarnings.push_back("Duplicate parameter: " + parameter.name);
            }
        }
        for (const EditorScene::AnimationStateTransition& transition : state.transitions) {
            if ((!transition.fromState.empty() && names.find(transition.fromState) == names.end())
                || names.find(transition.toState) == names.end()) {
                state.graphWarnings.push_back("Transition references a missing state.");
            }
            if (!transition.parameter.empty() && parameters.find(transition.parameter) == parameters.end()) {
                state.graphWarnings.push_back("Transition uses undeclared parameter: " + transition.parameter);
            }
        }
        if (!state.states.empty()) {
            std::unordered_set<std::string> reachable{state.states.front().name};
            bool expanded = true;
            while (expanded) {
                expanded = false;
                for (const auto& transition : state.transitions) {
                    if ((transition.fromState.empty() || reachable.count(transition.fromState))
                        && reachable.insert(transition.toState).second) expanded = true;
                }
            }
            for (const auto& node : state.states) {
                if (!reachable.count(node.name)) state.graphWarnings.push_back("Unreachable state: " + node.name);
            }
        }
    }

    if (m_mode != EditorMode::Play || !m_playRegistry) {
        return state;
    }

    engine::ecs::Entity playEntity = engine::ecs::kNull;
    for (const auto& entry : m_playEntityNames) {
        if (entry.second == selected->name) {
            playEntity = entry.first;
            break;
        }
    }
    if (playEntity == engine::ecs::kNull || !m_playRegistry->Valid(playEntity)) {
        return state;
    }

    if (engine::AnimatedModel* animated = m_playRegistry->TryGet<engine::AnimatedModel>(playEntity)) {
        state.runtimeAnimated = true;
        state.currentState = animated->controller.CurrentStateName();
        state.currentClip = animated->controller.CurrentClip();
        state.previousClip = animated->controller.PrevClip();
        state.currentTime = animated->controller.CurrentTime();
        state.previousTime = animated->controller.PrevTime();
        state.blend = animated->controller.Blend();
        state.parameter = animated->controller.Parameter();
        state.stateCount = animated->controller.StateCount();
        state.poseBones = animated->pose.size();
        for (const auto& parameter : animated->controller.Parameters()) {
            addPreviewParameter(parameter.first, parameter.second,
                static_cast<EditorScene::AnimationParameter::Type>(animated->controller.ParameterKind(parameter.first)));
        }
        for (const engine::AnimationController::TransitionDebugInfo& debug : animated->controller.TransitionDebug()) {
            state.transitionDebugRows.push_back(EditorDockspace::AnimationPreviewState::TransitionDebugRow{
                debug.fromState,
                debug.toState,
                debug.parameter,
                debug.value,
                debug.threshold,
                debug.exitTime,
                debug.priority,
                debug.canInterrupt,
                debug.conditionMet,
                debug.exitTimeReached,
                debug.blockedByBlend,
                debug.eligible,
                debug.selected
            });
        }
        if (animated->model && state.clips.empty()) {
            fillClips(*animated->model);
        }
    }

    return state;
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
    if (Pressed(GLFW_KEY_F9)) {
       TogglePanel(EditorPanels::Panel::AudioEditor);
    }
    if (Pressed(GLFW_KEY_F8)) {
        m_showProfiler = !m_showProfiler;
    }
    if (Pressed(GLFW_KEY_F10)) {
        TogglePanel(EditorPanels::Panel::MaterialMaker);
    }
    if (Pressed(GLFW_KEY_F11)) {
        TogglePanel(EditorPanels::Panel::BehaviorGraph);
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

void EditorApp::HandleAssetShortcuts(engine::Window&, bool controlDown)
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

void EditorApp::HandleEditorCommandShortcuts(engine::Window&, bool controlDown)
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
                engine::ecs::LoadedMaterialAsset* loaded =
                    m_playRegistry->TryGet<engine::ecs::LoadedMaterialAsset>(source);
                if (loaded) {
                    material = loaded->material;
                }

                const Entity entity = pbrRegistry.Create();
                pbrRegistry.Add<Transform>(entity, transform);
                engine::ecs::MeshPBR renderMesh{renderer.mesh, material};
                if (loaded) {
                    renderMesh.customShader = loaded->shader;
                    renderMesh.shaderParameters = loaded->shaderParameters;
                    renderMesh.shaderParameterTypes = loaded->shaderParameterTypes;
                    renderMesh.shaderTextures = loaded->shaderTextures;
                }
                pbrRegistry.Add<engine::ecs::MeshPBR>(entity, std::move(renderMesh));
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
        AddTerrainMeshes(pbrRegistry);
        AddEnvironmentSunIfNeeded(pbrRegistry, environment, sky, environmentSunApplied);

        engine::PbrRenderer::Options options;
        ConfigureEnvironmentPbrOptions(pbrRegistry, options, environment, sky);
        if (m_skinnedRenderer && m_playRegistry) {
            options.shadowCasters = [&](const glm::mat4& lightViewProjection) {
                m_skinnedRenderer->DrawSceneDepth(
                    *m_playRegistry, lightViewProjection);
            };
        }

        const engine::Window& window = GetWindow();
        m_pbrRenderer->Render(pbrRegistry, m_camera, window.AspectRatio(), m_renderW, m_renderH, options);
    }

    if (m_modelShader) {
        m_modelShader->Bind();
        m_modelShader->SetMat4("uViewProj", viewProj);
        m_modelShader->SetVec3("uLightPos", m_camera.Position() + glm::vec3(-4.0f, 6.0f, 4.0f));
        m_modelShader->SetVec3("uLightColor", glm::vec3(1.0f));
        m_modelShader->SetVec3("uViewPos", m_camera.Position());
        engine::ecs::RenderLoadedModels(
            *m_playRegistry, *m_modelShader, viewProj, sky.keyLightDirection,
            std::max({sky.keyLightColor.x, sky.keyLightColor.y,
                      sky.keyLightColor.z}) * environment.sunIntensity);
    }

    if (m_skinnedRenderer && m_playRegistry && m_pbrRenderer) {
        const engine::Window& window = GetWindow();
        engine::SkinnedLighting lighting;
        lighting.sunDir = sky.keyLightDirection;
        lighting.sunColor = sky.keyLightColor * environment.sunIntensity;
        lighting.ambient = sky.ambient * environment.skyLightIntensity;
        lighting.cascade = &m_pbrRenderer->Cascade();
        lighting.ibl = environment.ibl && m_ibl ? &*m_ibl : nullptr;
        lighting.shadowSoftness = environment.shadowSoftness;
        lighting.tonemap = !m_renderingHdrPreview;
        lighting.fog = environment.fog;
        lighting.fogColor = sky.horizon;
        lighting.fogDensity = environment.fogDensity;
        lighting.fogHeight = environment.fogHeight;
        lighting.fogHeightFalloff = environment.fogHeightFalloff;
        m_skinnedRenderer->DrawScene(
            *m_playRegistry, m_camera, window.AspectRatio(), lighting);
    }

    if (m_particleRenderer && m_playRegistry) {
        const engine::Window& window = GetWindow();
        m_particleRenderer->ResetStats();
        engine::RuntimeAssetManager& particleAssets =
            m_playAssets ? *m_playAssets : m_editAssets;
        m_playRegistry->view<engine::ParticleSystemComponent>().each(
            [&](Entity, engine::ParticleSystemComponent& system) {
                ResolveParticleGraphShader(system, particleAssets);
                m_particleRenderer->Draw(system, m_camera, window.AspectRatio());
            });
        m_playRegistry->view<engine::ParticleEffectComponent>().each(
            [&](Entity, engine::ParticleEffectComponent& effect) {
                if (!effect.enabled) return;
                for (engine::ParticleEffectLayer& layer : effect.layers) {
                    if (layer.enabled) {
                        ResolveParticleGraphShader(layer.system, particleAssets);
                        m_particleRenderer->Draw(layer.system, m_camera, window.AspectRatio());
                    }
                }
            });
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

    if (m_shader && m_cube && m_showAiDebug && !m_playAgents.empty()) {
        std::vector<EditorViewport::AiAgentGuide> aiGuides;
        aiGuides.reserve(m_playAgents.size());
        for (const PlayAgent& playAgent : m_playAgents) {
            EditorViewport::AiAgentGuide guide;
            if (playAgent.useGraph) {
                // Behaviour-tree agents don't expose a patrol/chase/search enum, so
                // approximate the debug state from perception (chase when it sees the
                // target, else patrol) and read pose/path off the blackboard.
                guide.position = playAgent.ctx.agent.position;
                guide.facing = playAgent.ctx.facing;
                guide.path = playAgent.ctx.path;
                guide.state = playAgent.ctx.seesTarget ? 1 : 0;
                guide.seesTarget = playAgent.ctx.seesTarget;
            } else {
                guide.position = playAgent.brain.Position();
                guide.facing = playAgent.brain.Facing();
                guide.path = playAgent.brain.Path();
                guide.state = static_cast<int>(playAgent.brain.GetState());
                guide.seesTarget = playAgent.brain.SeesTarget();
            }
            guide.hasTarget = playAgent.targetEntity != engine::ecs::kNull;
            aiGuides.push_back(std::move(guide));
        }
        m_viewport.DrawAiAgentDebugGuides(m_renderer, *m_shader, *m_cube, aiGuides, viewProj);
        if (m_useNavMesh) {
            m_viewport.DrawNavMeshOverlay(m_renderer, *m_shader, *m_cube, m_playNavMesh, viewProj);
        } else {
            m_viewport.DrawNavGridOverlay(m_renderer, *m_shader, *m_cube, m_playNavGrid, viewProj);
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
            if (!object.visible || object.navMeshBoundsVolume || !object.modelAssetPath.empty()) {
                continue;
            }

            if (object.isTerrain) {
                continue;   // terrain meshes are added by AddTerrainMeshes() below
            }
            // Water objects keep their flat plane (the opaque "bed"); the animated
            // transparent surface is drawn on top by DrawWaterBodies().

            const Transform* transform = m_scene.TryGetTransform(object.entity);
            const MeshRenderer* renderer = m_scene.TryGetMeshRenderer(object.entity);
            if (!transform || !renderer || !renderer->mesh) {
                continue;
            }

            engine::ecs::PbrMaterial material;
            material.albedo = renderer->color;
            material.roughness = object.light ? 0.24f : 0.55f;
            const bool selected = selectedObject && selectedObject->entity == object.entity;
            const engine::RuntimeMaterialAsset* customMaterial = nullptr;
            std::string customShaderError;

            if (!object.materialAssetPath.empty()) {
                std::string error;
                if (IsMaterialDocumentPath(object.materialAssetPath)) {
                    const engine::RuntimeMaterialAsset* loaded = m_editAssets.LoadMaterial(object.materialAssetPath, &error);
                    if (loaded) {
                        customMaterial = loaded;
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
                        if (!loaded->heightMapPath.empty()) {
                            material.heightMap = m_editAssets.LoadTexture(loaded->heightMapPath, &error);
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
            engine::ecs::MeshPBR renderMesh{renderer->mesh, material};
            if (customMaterial && !customMaterial->shaderPath.empty()) {
                renderMesh.customShader =
                    m_editAssets.LoadShader(customMaterial->shaderPath, false, &customShaderError);
                for (const auto& parameter : customMaterial->shaderParameters) {
                    renderMesh.shaderParameters[parameter.name] = parameter.value;
                    renderMesh.shaderParameterTypes[parameter.name] = parameter.type;
                    if (parameter.type == static_cast<int>(engine::ShaderValueType::Texture2D)
                        && !parameter.value.empty())
                            renderMesh.shaderTextures[parameter.name] =
                            m_editAssets.LoadTexture(parameter.value, &customShaderError);
                }
                for (const auto& overrideValue : object.materialParameterOverrides)
                    renderMesh.shaderParameters[overrideValue.first] =
                        overrideValue.second;
            }
            pbrRegistry.Add<engine::ecs::MeshPBR>(entity, std::move(renderMesh));
            if (const engine::ecs::Light* light = m_scene.TryGetLight(object.entity)) {
                pbrRegistry.Add<engine::ecs::Light>(entity, *light);
            }
        }

        AddTerrainMeshes(pbrRegistry);
        AddEnvironmentSunIfNeeded(pbrRegistry, environment, sky, environmentSunApplied);

        engine::PbrRenderer::Options options;
        ConfigureEnvironmentPbrOptions(pbrRegistry, options, environment, sky);

        const engine::Window& window = GetWindow();
        TerrainTrace("EditRender: PbrRenderer.Render... hasShadowCasters=%d", options.shadowCasters ? 1 : 0);
        m_pbrRenderer->Render(pbrRegistry, m_camera, window.AspectRatio(), m_renderW, m_renderH, options);
        TerrainTrace("EditRender: PbrRenderer.Render done");
    }
    DrawEditModeModels(viewProj);
    if (m_showNavigationPreview && m_shader && m_cube) {
        m_viewport.DrawEditorNavMeshOverlay(m_renderer, *m_shader, *m_cube, m_editorNavMesh, viewProj);
    }
    DrawSelectionOutline(viewProj);
    if (m_shader && m_cube) {
        m_viewport.DrawNavMeshBoundsGuides(m_renderer, *m_shader, *m_cube, m_scene, viewProj);
        m_viewport.DrawAudioSourceGuides(m_renderer, *m_shader, *m_cube, m_scene, viewProj);
        if (m_showCameraRails) {
            m_viewport.DrawCameraSequenceGuides(
                m_renderer, *m_shader, *m_cube, m_scene, viewProj);
        }
        if (m_showParticleDebug) {
            m_viewport.DrawParticleSystemGuides(m_renderer, *m_shader, *m_cube, m_scene, viewProj,
                m_particleDebugSelectedOnly, m_particleDebugShapes, m_particleDebugDirections,
                m_particleDebugBounds, m_particleDebugCullingState);
        }
    }
    if (m_shader && m_cube && environment.showLightGuides) {
        m_viewport.DrawSelectedLightGuide(m_renderer, *m_shader, *m_cube, m_scene, viewProj, environment.selectedLightGuideOnly);
    }
    if (m_shader && m_cube && environment.showPhysicsGuides) {
        m_viewport.DrawPhysicsColliderGuides(m_renderer, *m_shader, *m_cube, m_scene, viewProj,
            environment.selectedPhysicsGuideOnly);
        
        std::vector<EditorViewport::PhysicsJointGuide> jointGuides;
        for (const EditorScene::PhysicsJoint& joint : m_scene.PhysicsJoints()) {
            const EditorScene::Object* objectA = nullptr;
            const EditorScene::Object* objectB = nullptr;
            for (const EditorScene::Object& object : m_scene.Objects()) {
                if (object.name == joint.objectA) {
                    objectA = &object;
                }
                if (object.name == joint.objectB) {
                    objectB = &object;
                }
            }

            if (!objectA || (!joint.worldAnchor && !objectB)) {
                continue;
            }
            if (environment.selectedPhysicsGuideOnly) {
                const EditorScene::Object* selected = m_scene.SelectedObject();
                if (!selected || (selected->name != joint.objectA && selected->name != joint.objectB)) {
                    continue;
                }
            }

            const Transform* transformA = m_scene.TryGetTransform(objectA->entity);
            const Transform* transformB = objectB ? m_scene.TryGetTransform(objectB->entity) : nullptr;
            if (!transformA || (!joint.worldAnchor && !transformB)) {
                continue;
            }

            EditorViewport::PhysicsJointGuide guide;
            guide.a = transformA->position;
            guide.b = joint.worldAnchor ? joint.anchor : transformB->position;
            guide.type = joint.type == EditorScene::PhysicsJoint::Type::Spring ? 1 : 0;
            guide.rope = joint.rope;
            guide.enabled = joint.enabled;
            jointGuides.push_back(guide);
        }
        m_viewport.DrawPhysicsJointGuides(m_renderer, *m_shader, *m_cube, jointGuides, viewProj);
    }
    if (m_shader && m_cube && m_cone) {
        m_viewport.DrawSceneGizmo(m_renderer, *m_shader, *m_cube, *m_cone, m_scene, m_gizmo,
            viewProj, m_camera, GetWindow().Height());
    }
    if (m_shader && m_cube) {
        m_viewport.DrawNavAgentGuides(m_renderer, *m_shader, *m_cube, m_scene, viewProj);
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
    // Fog tint follows the sky horizon so it darkens with the day/night cycle
    // (this intentionally overrides the authored Environment.fogColor).
    options.fogColor = sky.horizon;
    options.fogDensity = environment.fogDensity;
    options.fogHeight = environment.fogHeight;
    options.fogHeightFalloff = environment.fogHeightFalloff;

    const bool graphPostNeedsGeometry = std::any_of(
        environment.postProcessEffects.begin(),
        environment.postProcessEffects.end(),
        [](const EditorScene::Environment::PostProcessEffect& effect) {
            return effect.enabled && !effect.shaderPath.empty();
        });
    if (environment.ssao || environment.ssr || graphPostNeedsGeometry) {
        if (!m_ssao) {
            m_ssao.emplace(window.Width(), window.Height());
        }
        m_ssao->radius = std::max(environment.ssaoRadius, 0.05f);
        m_ssao->bias = std::max(environment.ssaoBias, 0.0f);
        m_ssao->Generate(
            registry, m_camera, window.AspectRatio(), m_renderW, m_renderH);
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
            m_transformController.EndGizmoDrag();
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
        if (m_viewport.PickGizmoHandle(m_gizmo, m_scene, x, y, viewProj,
                window.Width(), window.Height(), m_camera)) {
            m_mouse.BeginGizmoDrag(GLFW_MOUSE_BUTTON_LEFT, x, y);
            m_transformController.BeginGizmoDrag();
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
        const float pixels = ProjectGizmoDrag(dx, dy, viewProj, window.Width(), window.Height());

        if (pixels != 0.0f) {
            m_transformController.ApplyGizmoDrag(m_scene, m_gizmo, pixels);
        }

        m_mouse.UpdateGizmoLast(x, y);
    }

    if (left.released && m_mouse.GizmoActiveFor(GLFW_MOUSE_BUTTON_LEFT)) {
        m_scene.EndTransformEdit();
        m_transformController.EndGizmoDrag();
        m_mouse.EndGizmoDrag();
        m_log.Info("Mouse gizmo edit complete");
    }
}

void EditorApp::AddTerrainMeshes(engine::ecs::Registry& pbrRegistry)
{
    for (const EditorScene::Object& object : m_scene.Objects()) {
        if (!object.isTerrain || !object.visible) {
            continue;
        }
        const Transform* t = m_scene.TryGetTransform(object.entity);
        TerrainCache& tc = m_terrains[object.entity];
        TerrainTrace("AddTerrainMeshes: object=%u res=%d size=%.2f maxH=%.2f seed=%d oct=%d freq=%.3f heights=%zu paint=%zu hasTransform=%d",
                     static_cast<unsigned>(object.entity), object.terrainRes, object.terrainSize,
                     object.terrainMaxHeight, object.terrainSeed, object.terrainOctaves, object.terrainFrequency,
                     object.terrainHeights.size(), object.terrainPaint.size(), t ? 1 : 0);
        const bool needsGen = tc.terrain.Map().h.empty() ||
            tc.res != object.terrainRes || tc.size != object.terrainSize ||
            tc.maxHeight != object.terrainMaxHeight || tc.seed != object.terrainSeed ||
            tc.octaves != object.terrainOctaves || tc.frequency != object.terrainFrequency;
        if (needsGen) {
            const bool haveHeights = static_cast<int>(object.terrainHeights.size()) ==
                                     object.terrainRes * object.terrainRes;
            TerrainTrace("AddTerrainMeshes: needsGen haveHeights=%d", haveHeights ? 1 : 0);
            if (haveHeights) {
                engine::Heightmap hm;
                hm.res = object.terrainRes;
                hm.size = object.terrainSize;
                hm.origin = glm::vec3(0.0f);
                hm.maxHeight = object.terrainMaxHeight;
                hm.h = object.terrainHeights;
                TerrainTrace("AddTerrainMeshes: SetHeightmap...");
                tc.terrain.SetHeightmap(hm);
                TerrainTrace("AddTerrainMeshes: SetHeightmap done");
            } else {
                TerrainTrace("AddTerrainMeshes: Generate...");
                tc.terrain.Generate(std::max(object.terrainRes, 2), object.terrainSize,
                                    glm::vec3(0.0f), object.terrainMaxHeight,
                                    static_cast<unsigned>(object.terrainSeed),
                                    std::max(object.terrainOctaves, 1), object.terrainFrequency);
                TerrainTrace("AddTerrainMeshes: Generate done");
            }
            if (static_cast<int>(object.terrainPaint.size()) == object.terrainRes * object.terrainRes) {
                TerrainTrace("AddTerrainMeshes: SetPaint...");
                tc.terrain.SetPaint(object.terrainPaint);   // overlay painted layers
                TerrainTrace("AddTerrainMeshes: SetPaint done");
            }
            tc.res = object.terrainRes; tc.size = object.terrainSize;
            tc.maxHeight = object.terrainMaxHeight; tc.seed = object.terrainSeed;
            tc.octaves = object.terrainOctaves; tc.frequency = object.terrainFrequency;
        }
        // Generation needs a live GL context; if the mesh/albedo somehow didn't build
        // (e.g. a failed generate), skip this terrain rather than dereferencing an empty
        // optional in Albedo()/GetMesh(), which would crash.
        TerrainTrace("AddTerrainMeshes: ready=%d hasMesh=%d hasAlbedo=%d",
                     tc.terrain.Ready() ? 1 : 0, tc.terrain.HasMesh() ? 1 : 0, tc.terrain.HasAlbedo() ? 1 : 0);
        if (!tc.terrain.Ready()) {
            TerrainTrace("AddTerrainMeshes: SKIP not ready");
            continue;
        }
        engine::ecs::PbrMaterial mat;
        mat.albedo = glm::vec3(1.0f);
        mat.roughness = 0.92f;
        mat.metallic = 0.0f;
        mat.albedoMap = &tc.terrain.Albedo();
        const Entity e = pbrRegistry.Create();
        pbrRegistry.Add<Transform>(e, t ? *t : Transform{});
        pbrRegistry.Add<engine::ecs::MeshPBR>(e, engine::ecs::MeshPBR{&tc.terrain.GetMesh(), mat});
        TerrainTrace("AddTerrainMeshes: added mesh entity");
    }
}

float EditorApp::TerrainSurfaceY(float worldX, float worldZ, bool& over)
{
    over = false;
    float best = 0.0f;
    for (const EditorScene::Object& object : m_scene.Objects()) {
        if (!object.isTerrain) {
            continue;
        }
        const auto it = m_terrains.find(object.entity);
        if (it == m_terrains.end() || it->second.terrain.Map().h.empty()) {
            continue;
        }
        const Transform* t = m_scene.TryGetTransform(object.entity);
        const glm::vec3 base = t ? t->position : glm::vec3(0.0f);
        const float lx = worldX - base.x;
        const float lz = worldZ - base.z;
        const float size = it->second.terrain.Map().size;
        if (lx < 0.0f || lz < 0.0f || lx > size || lz > size) {
            continue;   // outside this terrain's footprint
        }
        const float y = base.y + it->second.terrain.HeightAt(lx, lz);
        if (!over || y > best) { best = y; over = true; }   // highest terrain wins on overlap
    }
    return best;
}

void EditorApp::HandleTerrainSculpt()
{
    if (!m_terrainSculpt || m_mode != EditorMode::Edit) {
        return;
    }
    const EditorScene::Object* sel = m_scene.SelectedObject();
    if (!sel || !sel->isTerrain) {
        return;
    }
    engine::Window& window = GetWindow();
    if (!window.Native() || m_cameraController.MouseLookActive()) {
        return;
    }
    double cx = 0.0, cy = 0.0;
    glfwGetCursorPos(window.Native(), &cx, &cy);
    if (!IsViewportDropPosition(static_cast<float>(cx), static_cast<float>(cy))) {
        return;
    }
    if (glfwGetMouseButton(window.Native(), GLFW_MOUSE_BUTTON_LEFT) != GLFW_PRESS) {
        return;
    }
    const auto it = m_terrains.find(sel->entity);
    if (it == m_terrains.end() || it->second.terrain.Map().h.empty()) {
        return;
    }

    const glm::vec3 hit = SceneDropPosition();   // world XZ under the cursor (ground plane)
    const engine::ecs::Transform* t = m_scene.TryGetTransform(sel->entity);
    const glm::vec3 base = t ? t->position : glm::vec3(0.0f);

    engine::Heightmap hm = it->second.terrain.Map();   // working copy
    const int   res  = hm.res;
    TerrainTrace("Sculpt: mode=%d res=%d hSize=%zu paintSize=%zu hit=(%.1f,%.1f) base=(%.1f,%.1f)",
                 m_terrainSculptMode, res, hm.h.size(), it->second.terrain.Paint().size(),
                 hit.x, hit.z, base.x, base.z);
    if (res < 2) return;
    const float cell = hm.size / static_cast<float>(res - 1);
    const float lx = hit.x - base.x;   // brush centre in terrain-local XZ
    const float lz = hit.z - base.z;
    const float radius = std::max(m_terrainBrushRadius, 0.1f);
    const float delta  = m_terrainBrushStrength * m_dt;
    const int gi = static_cast<int>(std::round(lx / cell));
    const int gj = static_cast<int>(std::round(lz / cell));
    const int rad = static_cast<int>(std::ceil(radius / cell));
    // --- Paint mode: set the per-vertex paint layer within the brush. ---
    if (m_terrainSculptMode == 4) {
        std::vector<unsigned char> paint = it->second.terrain.Paint();
        if (static_cast<int>(paint.size()) != res * res) paint.assign(static_cast<std::size_t>(res) * res, 0);
        const unsigned char layer = static_cast<unsigned char>(std::clamp(m_terrainPaintLayer, 0, 5));
        for (int j = gj - rad; j <= gj + rad; ++j) {
            for (int i = gi - rad; i <= gi + rad; ++i) {
                if (i < 0 || j < 0 || i >= res || j >= res) continue;
                const float vx = i * cell, vz = j * cell;
                const float d = std::sqrt((vx - lx) * (vx - lx) + (vz - lz) * (vz - lz));
                if (d > radius) continue;
                paint[static_cast<std::size_t>(j) * res + i] = layer;
            }
        }
        TerrainTrace("Sculpt: paint SetPaint... paintSize=%zu", paint.size());
        it->second.terrain.SetPaint(paint);       // rebuild albedo with painted layers
        TerrainTrace("Sculpt: paint SetPaint done");
        m_scene.SetSelectedTerrainPaint(std::move(paint));
        TerrainTrace("Sculpt: paint persisted");
        return;
    }

    // --- Sculpt modes: modify the heightmap. ---
    const std::vector<float> src = hm.h;   // for smooth/flatten reference
    float centerH = 0.0f;
    if (gi >= 0 && gj >= 0 && gi < res && gj < res) centerH = src[static_cast<std::size_t>(gj) * res + gi];

    for (int j = gj - rad; j <= gj + rad; ++j) {
        for (int i = gi - rad; i <= gi + rad; ++i) {
            if (i < 0 || j < 0 || i >= res || j >= res) continue;
            const float vx = i * cell, vz = j * cell;
            const float d = std::sqrt((vx - lx) * (vx - lx) + (vz - lz) * (vz - lz));
            if (d > radius) continue;
            const float u = 1.0f - d / radius;
            const float fall = u * u * (3.0f - 2.0f * u);   // smoothstep falloff
            const std::size_t idx = static_cast<std::size_t>(j) * res + i;
            float h = hm.h[idx];
            switch (m_terrainSculptMode) {
                case 0: h += delta * fall; break;                       // raise
                case 1: h -= delta * fall; break;                       // lower
                case 2: {                                               // smooth
                    float sum = 0.0f; int n = 0;
                    for (int dj = -1; dj <= 1; ++dj)
                        for (int di = -1; di <= 1; ++di) {
                            const int ni = i + di, nj = j + dj;
                            if (ni >= 0 && nj >= 0 && ni < res && nj < res) {
                                sum += src[static_cast<std::size_t>(nj) * res + ni]; ++n;
                            }
                        }
                    const float avg = n ? sum / n : h;
                    h += (avg - h) * std::min(fall * m_terrainBrushStrength * m_dt, 1.0f);
                    break;
                }
                default: h += (centerH - h) * std::min(fall * m_terrainBrushStrength * m_dt, 1.0f); break; // flatten
            }
            hm.h[idx] = std::clamp(h, 0.0f, hm.maxHeight);
        }
    }

    TerrainTrace("Sculpt: height SetHeightmap... hSize=%zu", hm.h.size());
    it->second.terrain.SetHeightmap(hm);        // rebuild mesh + albedo
    TerrainTrace("Sculpt: height SetHeightmap done");
    m_scene.SetSelectedTerrainHeights(hm.h);    // persist the stroke
    TerrainTrace("Sculpt: height persisted");
}

void EditorApp::HandleMouseViewportGizmo()
{
    engine::Window& window = GetWindow();
    if (m_cameraController.MouseLookActive() || m_mode != EditorMode::Edit || !window.Native()) {
        if (m_mouse.GizmoActive()) {
            m_scene.EndTransformEdit();
            m_transformController.EndGizmoDrag();
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
        m_transformController.BeginGizmoDrag();
        m_scene.BeginTransformEdit();
        m_log.Info(std::string("Mouse gizmo: ") + m_gizmo.ModeName() + " " + m_gizmo.AxisName());
    }

    if (right.down && m_mouse.GizmoActiveFor(GLFW_MOUSE_BUTTON_RIGHT)) {
        const float dx = x - m_mouse.GizmoLastX();
        const float dy = y - m_mouse.GizmoLastY();
        const glm::mat4 viewProj = m_camera.ProjectionMatrix(window.AspectRatio()) * m_camera.ViewMatrix();
        const float pixels = ProjectGizmoDrag(dx, dy, viewProj, window.Width(), window.Height());

        if (pixels != 0.0f) {
            m_transformController.ApplyGizmoDrag(m_scene, m_gizmo, pixels);
        }

        m_mouse.UpdateGizmoLast(x, y);
    }

    if (right.released && m_mouse.GizmoActiveFor(GLFW_MOUSE_BUTTON_RIGHT)) {
        m_scene.EndTransformEdit();
        m_transformController.EndGizmoDrag();
        m_mouse.EndGizmoDrag();
        m_log.Info("Mouse gizmo edit complete");
    }
}

float EditorApp::ProjectGizmoDrag(float dx, float dy, const glm::mat4& viewProj,
                                  int viewportWidth, int viewportHeight) const {
    const EditorScene::Object* selected = m_scene.SelectedObject();
    const Transform* transform = selected ? m_scene.TryGetTransform(selected->entity) : nullptr;
    if (!transform || m_gizmo.CurrentMode() == EditorGizmo::Mode::Rotate) {
        return m_gizmo.CurrentAxis() == EditorGizmo::Axis::Y ? -dy : dx;
    }

    if (m_gizmo.CurrentAxis() == EditorGizmo::Axis::All) {
        return std::abs(dx) >= std::abs(dy) ? dx : -dy;
    }

    glm::vec2 centerScreen;
    glm::vec2 axisScreen;
    glm::vec3 axis = m_gizmo.AxisVector();
    if (m_gizmo.CurrentSpace() == EditorGizmo::Space::Local
        || m_gizmo.CurrentMode() == EditorGizmo::Mode::Scale) {
        axis = glm::mat3_cast(transform->rotation) * axis;
    }
    if (!m_viewport.ProjectWorldToScreen(transform->position, viewProj,
            viewportWidth, viewportHeight, &centerScreen)
        || !m_viewport.ProjectWorldToScreen(transform->position + axis, viewProj,
            viewportWidth, viewportHeight, &axisScreen)) {
        return m_gizmo.CurrentAxis() == EditorGizmo::Axis::Y ? -dy : dx;
    }

    const glm::vec2 screenDirection = axisScreen - centerScreen;
    const float lengthSquared = glm::dot(screenDirection, screenDirection);
    if (lengthSquared <= 0.0001f) {
        return std::abs(dx) >= std::abs(dy) ? dx : -dy;
    }
    return glm::dot(glm::vec2(dx, dy), screenDirection / std::sqrt(lengthSquared));
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

    if (payload.typeName == "Model" || payload.typeName == "Skeletal Model") {
        bool skeletalModel = payload.typeName == "Skeletal Model";
        std::string error;
        float radius = 0.0f;
        glm::vec3 boundsSize(0.0f);
        if (!skeletalModel) {
            std::string skeletalError;
            if (const engine::SkinnedModel* skinned = m_editAssets.LoadSkinnedModel(payload.path, &skeletalError);
                skinned && !skinned->GetSkeleton().bones.empty()) {
                skeletalModel = true;
                radius = skinned->BoundingRadius();
                boundsSize = skinned->Max() - skinned->Min();
            }
        }
        if (!skeletalModel) {
            const engine::Model* model = m_editAssets.LoadModel(payload.path, &error);
            if (!model) {
                m_log.Error("Model drop failed: " + error);
                m_dragDrop.Clear();
                return;
            }
            radius = model->BoundingRadius();
            boundsSize = model->Max() - model->Min();
        } else if (radius <= 0.0f) {
            const engine::SkinnedModel* model = m_editAssets.LoadSkinnedModel(payload.path, &error);
            if (!model) {
                m_log.Error("Skeletal model drop failed: " + error);
                m_dragDrop.Clear();
                return;
            }
            radius = model->BoundingRadius();
            boundsSize = model->Max() - model->Min();
        }

        Transform transform;
        transform.position = SceneDropPosition();
        // T-pose characters are often wider on X than they are tall, so X is not
        // useful for detecting the authored up axis. UE/FBX characters are Z-up:
        // their Z extent is much larger than their front/back depth on Y.
        const bool zUpAsset = skeletalModel && boundsSize.z > boundsSize.y * 1.25f;
        if (zUpAsset) {
            transform.rotation = glm::angleAxis(glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
        }
        radius = std::max(radius, 0.001f);
        const float targetRadius = 0.8f;
        const float uniformScale = targetRadius / radius;
        transform.scale = glm::vec3(uniformScale);

        if (m_cube && m_scene.AddModel(payload.path, *m_cube, transform)) {
            if (skeletalModel) {
                m_scene.SetSelectedAnimationSettings(true, 0, std::string(), true, true, 1.0f);
            }
            m_editModelLoadErrors.erase(payload.path);
            m_log.Info(std::string("Added ") + (skeletalModel ? "skeletal model" : "model") + " object: " + payload.path);
            if (zUpAsset) {
                m_log.Info("Applied Z-up to Y-up model orientation correction");
            }
        } else {
            m_log.Warning("Model drop failed: could not create scene object");
        }
    } else if (payload.typeName == "Particle") {
        engine::ParticleSystemComponent settings;
        std::string error;
        if (!particle_asset::Load(payload.path, &settings, &error)) {
            m_log.Error("Particle drop failed: " + error);
            m_dragDrop.Clear();
            return;
        }
        if (!m_cube) {
            m_log.Error("Particle drop failed: placeholder mesh is unavailable");
            m_dragDrop.Clear();
            return;
        }
        Transform transform;
        transform.position = SceneDropPosition();
        m_scene.AddParticleSystem(*m_cube, transform, payload.path, settings);
        m_log.Info("Created particle-system object from asset: " + payload.path);
    } else if (payload.typeName == "Material") {
        std::string error;
        const engine::RuntimeMaterialAsset* material = m_editAssets.LoadMaterial(payload.path, &error);
        if (!material) {
            m_log.Error("Material drop failed: " + error);
            m_dragDrop.Clear();
            return;
        }

        if (m_scene.SetSelectedMaterialAsset(payload.path)) {
            m_editTextureLoadErrors.erase(payload.path);
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
    if (io.WantCaptureMouse && !m_dragDrop.IsMouseDriven()) {
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

void EditorApp::AddCapsule()
{
    if (!m_capsule) {
        m_log.Error("Add failed: capsule mesh is not ready");
        return;
    }

    m_scene.AddCapsule(*m_capsule);
    m_log.Info("Added capsule");
}

void EditorApp::AddConfiguredPrimitive(EditorScene::Primitive primitive,
                                       const Transform& transform,
                                       const engine::ecs::Collider* collider,
                                       const std::string& name)
{
    if (!m_cube || !m_plane || !m_sphere || !m_capsule || !m_cylinder || !m_cone
        || !m_pyramid || !m_torus || !m_staircase) {
        m_log.Error("Add failed: primitive meshes are not ready");
        return;
    }

    const engine::Mesh& mesh = primitive == EditorScene::Primitive::Cube ? *m_cube
        : primitive == EditorScene::Primitive::Plane ? *m_plane
        : primitive == EditorScene::Primitive::Sphere ? *m_sphere
        : primitive == EditorScene::Primitive::Capsule ? *m_capsule
        : primitive == EditorScene::Primitive::Cylinder ? *m_cylinder
        : primitive == EditorScene::Primitive::Cone ? *m_cone
        : primitive == EditorScene::Primitive::Pyramid ? *m_pyramid
        : primitive == EditorScene::Primitive::Torus ? *m_torus
        : *m_staircase;
    m_scene.AddConfiguredPrimitive(primitive, mesh, transform, collider, name);
    m_log.Info("Added configured primitive");
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

void EditorApp::AddTerrain() {
    TerrainTrace("AddTerrain: begin");
    if (!m_plane) {
        m_log.Error("Add failed: plane mesh is not ready");
        TerrainTrace("AddTerrain: ABORT plane mesh not ready");
        return;
    }
    m_scene.AddPlane(*m_plane);   // a plane object that becomes terrain (mesh is replaced)
    TerrainTrace("AddTerrain: AddPlane done");
    m_scene.SetSelectedTerrain(true, 128, 64.0f, 8.0f, 1337, 5, 2.0f);
    TerrainTrace("AddTerrain: SetSelectedTerrain done");
    m_log.Info("Added terrain");
    TerrainTrace("AddTerrain: end");
}

void EditorApp::AddWater() {
    if (!m_plane) {
        m_log.Error("Add failed: plane mesh is not ready");
        return;
    }
    m_scene.AddPlane(*m_plane);   // a plane object that becomes a water body (rendered by the water pass)
    m_scene.SetSelectedWater(80.0f, 160, 0.0f,
                             glm::vec3(0.10f, 0.42f, 0.50f), glm::vec3(0.02f, 0.10f, 0.18f),
                             glm::vec3(0.55f, 0.72f, 0.92f), 0.72f, 4.0f, 1.2f, 220.0f);
    m_log.Info("Added water");
}

void EditorApp::DrawWaterBodies(const engine::Camera& camera, float aspect) {
    const EditorScene::Environment& env = m_scene.GetEnvironment();
    const engine::DayNightCycle::Sample sky = engine::DayNightCycle::At(env.timeOfDay);
    const glm::vec3 sunDir   = sky.keyLightDirection;
    const glm::vec3 sunColor = sky.keyLightColor * env.sunIntensity;
    const glm::vec3 ambient  = sky.ambient * env.skyLightIntensity;

    for (const EditorScene::Object& object : m_scene.Objects()) {
        if (!object.isWater || !object.visible) continue;
        const engine::ecs::Transform* t = m_scene.TryGetTransform(object.entity);

        engine::WaterConfig cfg;
        // Surface follows the object's Transform (gizmo-movable); the bed plane is
        // scaled to match in SetSelectedWater, so bed + surface stay aligned.
        cfg.center = t ? t->position : glm::vec3(0.0f, object.waterLevel, 0.0f);
        cfg.size = object.waterSize;
        cfg.resolution = object.waterResolution;
        cfg.shallowColor = object.waterShallow;
        cfg.deepColor = object.waterDeep;
        cfg.reflectionColor = object.waterReflection;
        cfg.transparency = object.waterTransparency;
        cfg.fresnelPower = object.waterFresnel;
        cfg.specularStrength = object.waterSpecular;
        cfg.shininess = object.waterShininess;

        auto res = m_waters.try_emplace(object.entity, cfg);
        if (!res.second) res.first->second.SetConfig(cfg);
        res.first->second.Update(m_dt);
        res.first->second.Draw(camera, aspect, sunDir, sunColor, ambient);
    }
}

float EditorApp::WaterSurfaceY(float worldX, float worldZ, bool& over) {
    // Highest water surface at this XZ, if the point is over a water patch. Used to
    // float the player on the waves. Reads the cached engine::Water (built by
    // DrawWaterBodies) so the height matches what's rendered.
    over = false;
    float best = 0.0f;
    for (const EditorScene::Object& object : m_scene.Objects()) {
        if (!object.isWater || !object.visible) continue;
        const engine::ecs::Transform* t = m_scene.TryGetTransform(object.entity);
        const float cx = t ? t->position.x : 0.0f;
        const float cz = t ? t->position.z : 0.0f;
        const float half = object.waterSize * 0.5f;
        if (worldX < cx - half || worldX > cx + half || worldZ < cz - half || worldZ > cz + half) {
            continue;
        }
        const auto it = m_waters.find(object.entity);
        const float y = (it != m_waters.end())
            ? it->second.HeightAt(worldX, worldZ)
            : (t ? t->position.y : object.waterLevel);
        if (!over || y > best) { best = y; over = true; }
    }
    return best;
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

bool FindAuthoredNavBounds(const EditorScene& scene, glm::vec2* boundsMin,
                           glm::vec2* boundsMax, float* groundY) {
    glm::vec2 mn(1.0e9f);
    glm::vec2 mx(-1.0e9f);
    float minY = 1.0e9f;
    bool found = false;
    for (const EditorScene::Object& object : scene.Objects()) {
        if (!object.navMeshBoundsVolume) continue;
        const Transform* transform = scene.TryGetTransform(object.entity);
        if (!transform) continue;
        const glm::mat4 model = transform->Model();
        for (int i = 0; i < 8; ++i) {
            const glm::vec3 local((i & 1) ? 0.5f : -0.5f,
                                  (i & 4) ? 0.5f : -0.5f,
                                  (i & 2) ? 0.5f : -0.5f);
            const glm::vec3 world = glm::vec3(model * glm::vec4(local, 1.0f));
            mn = glm::min(mn, glm::vec2(world.x, world.z));
            mx = glm::max(mx, glm::vec2(world.x, world.z));
            minY = std::min(minY, world.y);
            found = true;
        }
    }
    if (found) {
        *boundsMin = mn;
        *boundsMax = mx;
        *groundY = minY;
    }
    return found;
}

void EditorApp::AddNavMeshBoundsVolume() {
    if (!m_cube) {
        m_log.Error("Add failed: cube mesh is not ready");
        return;
    }
    m_scene.AddNavMeshBoundsVolume(*m_cube);
    if (m_showNavigationPreview) BakeEditorNavMesh();
    m_log.Info("Added nav mesh bounds volume");
}

void EditorApp::AddPlayerStart()
{
    if (!m_cube) {
        m_log.Error("Add failed: cube mesh is not ready");
        return;
    }

    m_scene.AddCube(*m_cube);
    m_scene.SetSelectedName("PlayerStart");

    EditorScene::PlayerControllerSettings player;
    player.firstPerson = false;
    player.walkSpeed = 4.0f;
    player.runSpeed = 7.0f;
    player.jumpSpeed = 5.0f;
    player.lookSensitivity = 0.1f;
    player.capsuleRadius = 0.4f;
    player.capsuleHeight = 1.8f;
    player.eyeHeight = 0.6f;
    player.cameraDistance = 5.0f;
    player.cameraTargetHeight = 1.0f;
    player.maxSlopeDegrees = 50.0f;
    player.stepHeight = 0.35f;

    Transform transform;
    transform.position = glm::vec3(0.0f, player.capsuleHeight * 0.5f, 4.0f);
    transform.scale = glm::vec3(player.capsuleRadius * 2.0f, player.capsuleHeight, player.capsuleRadius * 2.0f);
    m_scene.SetSelectedTransform(transform);
    m_scene.SetSelectedColor(glm::vec3(0.18f, 0.72f, 1.0f));
    m_scene.SetSelectedRigidBodyEnabled(false);
    m_scene.SetSelectedPlayerController(player);
    engine::Health health;
    health.Reset(100.0f);
    m_scene.SetSelectedHealth(health);
    m_log.Info("Added player start");
}

void EditorApp::AddGameplayDoor()
{
    if (!m_cube) {
        m_log.Error("Add failed: cube mesh is not ready");
        return;
    }

    m_scene.AddCube(*m_cube);
    m_scene.SetSelectedName("Door");
    Transform transform;
    transform.position = glm::vec3(0.0f, 1.5f, -2.5f);
    transform.scale = glm::vec3(1.2f, 3.0f, 0.25f);
    m_scene.SetSelectedTransform(transform);
    m_scene.SetSelectedColor(glm::vec3(0.52f, 0.34f, 0.18f));
    m_scene.SetSelectedRigidBodyEnabled(false);
    m_scene.SetSelectedCollider(engine::ecs::Collider::MakeBox(transform.scale * 0.5f));
    m_scene.SetSelectedScript("DoorOpener", "Game/Scripts/DoorOpener.cpp", true);

    std::vector<EditorScene::ScriptField> fields;
    fields.push_back(EditorScene::ScriptField{"target", EditorScene::ScriptField::Type::String, "Door"});
    fields.push_back(EditorScene::ScriptField{"speed", EditorScene::ScriptField::Type::Float, "2.0"});
    fields.push_back(EditorScene::ScriptField{"height", EditorScene::ScriptField::Type::Float, "3.0"});
    m_scene.SetSelectedScriptFields(fields);
    m_log.Info("Added gameplay door");
}

void EditorApp::AddGameplayPickup()
{
    if (!m_cube) {
        m_log.Error("Add failed: cube mesh is not ready");
        return;
    }

    m_scene.AddCube(*m_cube);
    m_scene.SetSelectedName("Pickup");
    Transform transform;
    transform.position = glm::vec3(1.5f, 0.5f, -1.0f);
    transform.scale = glm::vec3(0.45f);
    m_scene.SetSelectedTransform(transform);
    m_scene.SetSelectedColor(glm::vec3(0.95f, 0.82f, 0.22f));
    m_scene.SetSelectedRigidBodyEnabled(false);
    engine::ecs::Collider collider = engine::ecs::Collider::MakeBox(transform.scale * 0.5f);
    collider.isTrigger = true;
    m_scene.SetSelectedCollider(collider);
    m_scene.SetSelectedScript("Pickup", "Game/Scripts/Pickup.cpp", true);

    std::vector<EditorScene::ScriptField> fields;
    fields.push_back(EditorScene::ScriptField{"interactKey", EditorScene::ScriptField::Type::String, "E"});
    m_scene.SetSelectedScriptFields(fields);
    m_log.Info("Added gameplay pickup");
}

void EditorApp::AddGameplayDamageZone()
{
    if (!m_cube) {
        m_log.Error("Add failed: cube mesh is not ready");
        return;
    }

    m_scene.AddCube(*m_cube);
    m_scene.SetSelectedName("DamageZone");
    Transform transform;
    transform.position = glm::vec3(-1.5f, 0.25f, -1.0f);
    transform.scale = glm::vec3(2.0f, 0.5f, 2.0f);
    m_scene.SetSelectedTransform(transform);
    m_scene.SetSelectedColor(glm::vec3(0.86f, 0.12f, 0.10f));
    m_scene.SetSelectedRigidBodyEnabled(false);
    engine::ecs::Collider collider = engine::ecs::Collider::MakeBox(transform.scale * 0.5f);
    collider.isTrigger = true;
    m_scene.SetSelectedCollider(collider);
    m_scene.SetSelectedScript("DamageZone", "Game/Scripts/DamageZone.cpp", true);

    std::vector<EditorScene::ScriptField> fields;
    fields.push_back(EditorScene::ScriptField{"target", EditorScene::ScriptField::Type::String, "PlayerStart"});
    fields.push_back(EditorScene::ScriptField{"damagePerSecond", EditorScene::ScriptField::Type::Float, "10.0"});
    m_scene.SetSelectedScriptFields(fields);
    m_log.Info("Added gameplay damage zone");
}

void EditorApp::AddGameplayMovingPlatform()
{
    if (!m_cube) {
        m_log.Error("Add failed: cube mesh is not ready");
        return;
    }

    m_scene.AddCube(*m_cube);
    m_scene.SetSelectedName("MovingPlatform");
    Transform transform;
    transform.position = glm::vec3(0.0f, 0.35f, 2.5f);
    transform.scale = glm::vec3(2.5f, 0.3f, 1.0f);
    m_scene.SetSelectedTransform(transform);
    m_scene.SetSelectedColor(glm::vec3(0.18f, 0.56f, 0.78f));
    m_scene.SetSelectedRigidBodyEnabled(false);
    m_scene.SetSelectedCollider(engine::ecs::Collider::MakeBox(transform.scale * 0.5f));

    engine::ecs::Mover mover;
    mover.axis = glm::vec3(1.0f, 0.0f, 0.0f);
    mover.distance = 2.5f;
    mover.speed = 1.0f;
    mover.phase = 0.0f;
    mover.initialized = false;
    m_scene.SetSelectedMover(mover);
    m_scene.SetSelectedMoverEnabled(true);
    m_log.Info("Added gameplay moving platform");
}

void EditorApp::AddGameplayTriggerMoverTest()
{
    if (!m_cube) {
        m_log.Error("Add failed: cube mesh is not ready");
        return;
    }

    m_scene.AddCube(*m_cube);
    m_scene.SetSelectedName("GameplayMoverTarget");
    Transform targetTransform;
    targetTransform.position = glm::vec3(2.5f, 0.5f, 0.0f);
    targetTransform.scale = glm::vec3(0.8f);
    m_scene.SetSelectedTransform(targetTransform);
    m_scene.SetSelectedColor(glm::vec3(0.18f, 0.52f, 0.86f));

    engine::ecs::Mover mover;
    mover.axis = glm::vec3(1.0f, 0.0f, 0.0f);
    mover.distance = 2.0f;
    mover.speed = 1.0f;
    mover.phase = 0.0f;
    mover.initialized = false;
    m_scene.SetSelectedMover(mover);
    m_scene.SetSelectedMoverEnabled(false);

    m_scene.AddCube(*m_cube);
    m_scene.SetSelectedName("GameplayTrigger");
    Transform triggerTransform;
    triggerTransform.position = glm::vec3(-1.5f, 0.5f, 0.0f);
    triggerTransform.scale = glm::vec3(1.5f, 1.0f, 1.5f);
    m_scene.SetSelectedTransform(triggerTransform);
    m_scene.SetSelectedColor(glm::vec3(0.90f, 0.62f, 0.18f));
    m_scene.SetSelectedRigidBodyEnabled(false);

    engine::ecs::Collider triggerCollider = engine::ecs::Collider::MakeBox(triggerTransform.scale * 0.5f);
    triggerCollider.isTrigger = true;
    m_scene.SetSelectedCollider(triggerCollider);
    m_scene.SetSelectedTriggerAction("GameplayMoverTarget",
        EditorScene::TriggerActionMode::Enable,
        EditorScene::TriggerActionMode::None,
        EditorScene::TriggerActionMode::Disable,
        EditorScene::TriggerActionMode::None);
    const int triggerIndex = m_scene.SelectedIndex();

    m_scene.AddCube(*m_cube);
    m_scene.SetSelectedName("GameplayActivator");
    Transform activatorTransform;
    activatorTransform.position = glm::vec3(-4.0f, 0.5f, 0.0f);
    activatorTransform.scale = glm::vec3(0.6f);
    m_scene.SetSelectedTransform(activatorTransform);
    m_scene.SetSelectedColor(glm::vec3(0.24f, 0.78f, 0.36f));

    engine::ecs::RigidBody activatorBody = engine::ecs::RigidBody::Dynamic(1.0f);
    activatorBody.useGravity = false;
    activatorBody.velocity = glm::vec3(2.0f, 0.0f, 0.0f);
    m_scene.SetSelectedRigidBody(activatorBody);
    m_scene.SetSelectedCollider(engine::ecs::Collider::MakeBox(activatorTransform.scale * 0.5f));

    m_scene.SelectIndex(triggerIndex);

    m_log.Info("Added gameplay trigger mover test");
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
    if (!m_cube || !m_plane || !m_sphere || !m_capsule || !m_cylinder || !m_cone || !m_pyramid || !m_torus || !m_staircase) {
        m_log.Error("Type change failed: editor meshes are not ready");
        return;
    }

    const engine::Mesh& mesh = primitive == EditorScene::Primitive::Cube ? *m_cube
        : primitive == EditorScene::Primitive::Plane ? *m_plane
        : primitive == EditorScene::Primitive::Sphere ? *m_sphere
        : primitive == EditorScene::Primitive::Capsule ? *m_capsule
        : primitive == EditorScene::Primitive::Cylinder ? *m_cylinder
        : primitive == EditorScene::Primitive::Cone ? *m_cone
        : primitive == EditorScene::Primitive::Pyramid ? *m_pyramid
        : primitive == EditorScene::Primitive::Torus ? *m_torus
        : *m_staircase;
    if (m_scene.SetSelectedPrimitive(primitive, mesh)) {
        const char* typeName = primitive == EditorScene::Primitive::Cube ? "cube"
            : primitive == EditorScene::Primitive::Plane ? "plane"
            : primitive == EditorScene::Primitive::Sphere ? "sphere"
            : primitive == EditorScene::Primitive::Capsule ? "capsule"
            : primitive == EditorScene::Primitive::Cylinder ? "cylinder"
            : primitive == EditorScene::Primitive::Cone ? "cone"
            : primitive == EditorScene::Primitive::Pyramid ? "pyramid"
            : primitive == EditorScene::Primitive::Torus ? "torus"
            : "staircase";
        m_log.Info(std::string("Changed selected type to ") + typeName);
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
    if (!m_cube || !m_plane || !m_sphere || !m_capsule || !m_cylinder || !m_cone || !m_pyramid || !m_torus || !m_staircase) {
        m_log.Error("Duplicate failed: editor meshes are not ready");
        return;
    }

    if (m_scene.DuplicateSelected(*m_cube, *m_plane, *m_sphere, *m_capsule, *m_cylinder, *m_cone, *m_pyramid, *m_torus, *m_staircase)) {
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
    if (!m_cube || !m_plane || !m_sphere || !m_capsule || !m_cylinder || !m_cone || !m_pyramid || !m_torus || !m_staircase) {
        m_log.Error("Undo failed: editor meshes are not ready");
        return;
    }

    if (m_scene.Undo(*m_cube, *m_plane, *m_sphere, *m_capsule, *m_cylinder, *m_cone, *m_pyramid, *m_torus, *m_staircase)) {
        m_log.Info("Undo");
    } else {
        m_log.Warning("Nothing to undo");
    }
}

void EditorApp::Redo()
{
    if (!m_cube || !m_plane || !m_sphere || !m_capsule || !m_cylinder || !m_cone || !m_pyramid || !m_torus || !m_staircase) {
    m_log.Error("Redo failed: editor meshes are not ready");
    return;
    }

    if (m_scene.Redo(*m_cube, *m_plane, *m_sphere, *m_capsule, *m_cylinder, *m_cone, *m_pyramid, *m_torus, *m_staircase)) {
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
        m_content.Refresh(m_assets, m_project, m_log);
    }
}

void EditorApp::SaveSceneAs(const std::string& path) {
    if (path.empty()) {
        m_log.Warning("Save As failed: scene path is empty");
        return;
    }

    m_project.SetScenePath(m_project.ResolveScenePath(path));
    if (m_runtime.SaveScene(m_scene, m_project, m_log)) {
        m_project.Save(m_config);
        m_config.Save();
        m_autosaveTimer = 0.0f;
        SetScenePathDraft(m_project.ScenePath());
        m_content.Refresh(m_assets, m_project, m_log);
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
    if (!m_cube || !m_plane || !m_sphere || !m_capsule || !m_cylinder || !m_cone || !m_pyramid || !m_torus || !m_staircase) {
        m_log.Error("Load failed: editor meshes are not ready");
        return;
    }

    if (m_runtime.LoadScene(m_scene, m_project, *m_cube, *m_plane, *m_sphere, *m_capsule, *m_cylinder, *m_cone, *m_pyramid, *m_torus, *m_staircase, m_log)) {
        m_project.AddRecentScene(m_project.ScenePath());
        m_project.Save(m_config);
        m_config.Save();
        m_autosaveTimer = 0.0f;
        SetScenePathDraft(m_project.ScenePath());
        SyncHudFromScene();
    }
}

void EditorApp::SyncHudFromScene() {
    const std::string path = m_scene.GetEnvironment().hudAsset;
    m_hudPanel.SetSelected(-1);
    if (path.empty()) {
        m_hud.Clear();
        m_hudPath.clear();
        return;
    }
    std::string err;
    if (m_hud.Load(path, &err)) {
        m_hudPath = path;
        m_hudPanel.SetPath(path);
    } else {
        m_log.Warning("Scene HUD '" + path + "' could not be loaded: " + err);
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
    if (!m_cube || !m_plane || !m_sphere || !m_capsule || !m_cylinder || !m_cone || !m_pyramid || !m_torus || !m_staircase) {
        m_log.Error("New scene failed: editor primitive meshes are not ready");
        return;
    }

    m_scene.BuildDefault(*m_cube, *m_plane, *m_sphere, *m_capsule, *m_cylinder, *m_cone, *m_pyramid, *m_torus, *m_staircase);
    std::error_code ec;
    std::filesystem::create_directories(m_project.ScenesRoot(), ec);
    std::filesystem::path newPath =
        std::filesystem::path(m_project.ScenesRoot()) / "Untitled.scene";
    for (int suffix = 2; std::filesystem::exists(newPath, ec); ++suffix) {
        newPath = std::filesystem::path(m_project.ScenesRoot())
            / ("Untitled_" + std::to_string(suffix) + ".scene");
    }
    m_project.SetScenePath(newPath.string());
    SetScenePathDraft(newPath.string());
    m_scene.MarkDirty();
    m_autosaveTimer = 0.0f;
    m_log.Info("Created new scene");
}

void EditorApp::PerformLoadSceneFromPath(const std::string& path) {
    if (path.empty()) {
        m_log.Warning("Load failed: scene path is empty");
        return;
    }
    if (!m_cube || !m_plane || !m_sphere || !m_capsule || !m_cylinder || !m_cone || !m_pyramid || !m_torus || !m_staircase) {
        m_log.Error("Load failed: editor meshes are not ready");
        return;
    }

    const std::string previousPath = m_project.ScenePath();
    m_project.SetScenePath(m_project.ResolveScenePath(path));
    if (m_runtime.LoadScene(m_scene, m_project, *m_cube, *m_plane, *m_sphere, *m_capsule, *m_cylinder, *m_cone, *m_pyramid, *m_torus, *m_staircase, m_log)) {
        m_project.AddRecentScene(m_project.ScenePath());
        m_project.Save(m_config);
        m_config.Save();
        m_autosaveTimer = 0.0f;
        SetScenePathDraft(m_project.ScenePath());
        SyncHudFromScene();
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
    if (!m_cube || !m_plane || !m_sphere || !m_capsule || !m_cylinder || !m_cone || !m_pyramid || !m_torus || !m_staircase) {
        m_log.Error("Load failed: editor meshes are not ready");
        return;
    }

    const std::string previousPath = m_project.ScenePath();
    m_project.SetScenePath(path);
    if (m_runtime.LoadScene(m_scene, m_project, *m_cube, *m_plane, *m_sphere, *m_capsule, *m_cylinder, *m_cone, *m_pyramid, *m_torus, *m_staircase, m_log)) {
        m_project.Save(m_config);
        m_config.Save();
        m_autosaveTimer = 0.0f;
        SetScenePathDraft(m_project.ScenePath());
        SyncHudFromScene();
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
    if (!m_cube || !m_plane || !m_sphere || !m_capsule || !m_cylinder || !m_cone || !m_pyramid || !m_torus || !m_staircase) {
        m_log.Error("Runtime scene validation failed: editor primitive meshes are not ready");
        return;
    }

    m_runtime.ValidateRuntimeScene(m_project, *m_cube, *m_plane, *m_sphere, *m_capsule, *m_cylinder, *m_cone, *m_pyramid, *m_torus, *m_staircase, m_log);
}

void EditorApp::TriggerAnimationPreviewAction() {
    const EditorScene::Object* selected = m_scene.SelectedObject();
    if (!selected || !selected->skeletalModel) {
        m_log.Warning("Animation action preview needs a selected skeletal model");
        return;
    }

    const int clip = std::max(m_animationActionClip, 0);
    const float fadeIn = std::max(m_animationActionFadeIn, 0.0f);
    const float fadeOut = std::max(m_animationActionFadeOut, 0.0f);
    const float speed = std::max(m_animationActionSpeed, 0.0f);
    const std::string maskRoot = m_animationActionMaskRoot.data();

    auto buildMask = [&](const engine::SkinnedModel& model, std::vector<float>* mask) {
        mask->clear();
        if (maskRoot.empty()) {
            return true;
        }
        const engine::Skeleton& skeleton = model.GetSkeleton();
        if (skeleton.Find(maskRoot) < 0) {
            return false;
        }
        *mask = engine::Animator::BuildMask(skeleton, maskRoot);
        return true;
    };

    if (m_mode == EditorMode::Play) {
        if (!m_playRegistry) {
            m_log.Warning("Animation action preview has no Play registry");
            return;
        }

        engine::ecs::Entity playEntity = engine::ecs::kNull;
        for (const auto& entry : m_playEntityNames) {
            if (entry.second == selected->name) {
                playEntity = entry.first;
                break;
            }
        }

        engine::AnimatedModel* animated = playEntity == engine::ecs::kNull
            ? nullptr
            : m_playRegistry->TryGet<engine::AnimatedModel>(playEntity);
        if (!animated || !animated->model || clip >= static_cast<int>(animated->model->AnimationCount())) {
            m_log.Warning("Selected Play entity cannot play that animation action clip");
            return;
        }

        std::vector<float> mask;
        if (!buildMask(*animated->model, &mask)) {
            m_log.Warning("Animation action mask root bone was not found: " + maskRoot);
            return;
        }

        animated->PlayAction(clip, std::move(mask), {}, fadeIn, fadeOut, speed);
        m_log.Info("Play animation action preview started");
        return;
    }

    std::string error;
    const engine::SkinnedModel* model = m_editAssets.LoadSkinnedModel(selected->modelAssetPath, &error);
    if (!model || clip >= static_cast<int>(model->AnimationCount())) {
        m_log.Warning("Selected edit model cannot play that animation action clip");
        return;
    }

    m_animationPreviewAction.entity = selected->entity;
    m_animationPreviewAction.clip = clip;
    m_animationPreviewAction.time = 0.0f;
    m_animationPreviewAction.fadeIn = fadeIn;
    m_animationPreviewAction.fadeOut = fadeOut;
    m_animationPreviewAction.speed = speed;
    if (!buildMask(*model, &m_animationPreviewAction.mask)) {
        m_log.Warning("Animation action mask root bone was not found: " + maskRoot);
        m_animationPreviewAction.active = false;
        return;
    }
    m_animationPreviewAction.active = true;
    m_log.Info("Edit animation action preview started");
}

void EditorApp::EnterPlayMode()
{
    RestoreCameraBeforeShake();
    m_cameraShake.Clear();
    m_cameraSequence.Stop();
    m_cameraDirector.SetStopped();
    m_cameraDirector.ClearEvents();
    m_cameraDirector.TakeCommands();
    m_cinematicSkipPrev = false;
    m_activeCinematicCues.clear();
    m_cameraSequencePaused = false;
    m_editSnapshot = m_scene.CreateSnapshot();
    m_editCameraBeforePlay = m_camera;
    m_physicsPaused = false;
    m_physicsStepRequested = false;
    m_physicsAccumulator = 0.0f;
    m_physicsStepsLastFrame = 0;
    m_physicsEventEnterCount = 0;
    m_physicsEventStayCount = 0;
    m_physicsEventExitCount = 0;
    m_physicsActionCount = 0;
    m_physicsEventRows.clear();
    m_physicsEventGuides.clear();
    m_playAnimationEvents.clear();
    m_playPhysics.ClearJoints();
    m_playEntityNames.clear();
    m_playTriggerActions.clear();
    m_playCameraZones.clear();
    m_playCameraZonesInside.clear();
    m_activePlayCameraZone = engine::ecs::kNull;
    m_playCameraOverride.reset();
    std::string error;
    if (!BuildPlayRuntimePreview(&error)) {
        m_editSnapshot.reset();
        m_editCameraBeforePlay.reset();
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
    const std::size_t rotatorCount = m_playRegistry
        ? ComponentCount<engine::ecs::Rotator>(*m_playRegistry)
        : 0;
    const std::size_t moverCount = m_playRegistry
        ? ComponentCount<engine::ecs::Mover>(*m_playRegistry)
        : 0;
    const std::size_t authoredRotatorCount = CountAuthoredRotators(m_scene);
    const std::size_t authoredMoverCount = CountAuthoredMovers(m_scene);
    const std::size_t frozenRotators = m_playRegistry
        ? CountRuntimeRotatorsWithFrozenRigidBody(*m_playRegistry)
        : 0;
    const PhysicsRuntimeStats physics = m_playRegistry
        ? CollectPhysicsRuntimeStats(*m_playRegistry)
        : PhysicsRuntimeStats{};

    m_mode = EditorMode::Play;
    if (const EditorScene::CameraPreset* preset = m_scene.PrimaryCameraPreset();
        preset && preset->useInPlay) {
        BeginCameraBlend(*preset);
    }
    // Lock the cursor so mouse movement drives the camera directly (no need to hold RMB).
    // Press ESC to free the cursor for the editor UI; ESC again re-captures it.
    GetWindow().SetCursorCaptured(true);
    m_playMouseCaptured = true;
    m_playCursorTogglePrev = false;
    m_log.Info("Play mode: runtime preview loaded, "
        + std::to_string(linearCount) + " linear, "
        + std::to_string(angularCount) + " angular, "
        + std::to_string(rotatorCount) + " rotators, "
        + std::to_string(authoredRotatorCount) + " authored rotators, "
        + std::to_string(authoredMoverCount) + " authored movers, "
        + std::to_string(m_playTriggerActions.size()) + " trigger actions, "
        + std::to_string(physics.rigidBodies) + " rigid bodies, "
        + std::to_string(physics.dynamicBodies) + " dynamic, "
        + std::to_string(physics.colliders) + " colliders, "
        + std::to_string(physics.staticColliders) + " static, "
        + std::to_string(physics.triggerColliders) + " triggers");
    if (authoredRotatorCount > 0 && rotatorCount == 0) {
        m_log.Warning("Play mode gameplay: scene has Rotator objects, but none reached the runtime registry");
    }
    if (authoredMoverCount > 0 && moverCount == 0) {
        m_log.Warning("Play mode gameplay: scene has Mover objects, but none reached the runtime registry");
    }
    if (frozenRotators > 0) {
        m_log.Warning("Play mode gameplay: "
            + std::to_string(frozenRotators)
            + " Rotator object(s) also have freeze rotation enabled");
    }
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
    if (m_playRegistry) engine::ShutdownScripts(*m_playRegistry);
    m_runtimeAudio.Stop();
    m_audio.StopAllSounds();
    m_audio.StopMusic();
    m_audio.DestroyAllSources();
    m_playAudioSources.clear();
    m_playRegistry.reset();
    m_playAssets.reset();
    m_playPlayerController.reset();
    m_playPlayerEntity = engine::ecs::kNull;
    m_playLockTarget = engine::ecs::kNull;
    m_playLockTogglePrev = false;
    // Give the cursor back to the editor.
    if (m_playMouseCaptured) {
        GetWindow().SetCursorCaptured(false);
    }
    m_playMouseCaptured = false;
    m_playCursorTogglePrev = false;
    m_physicsPaused = false;
    m_physicsStepRequested = false;
    m_physicsAccumulator = 0.0f;
    m_physicsStepsLastFrame = 0;
    m_physicsEventEnterCount = 0;
    m_physicsEventStayCount = 0;
    m_physicsEventExitCount = 0;
    m_physicsActionCount = 0;
    m_physicsEventRows.clear();
    m_physicsEventGuides.clear();
    m_playAnimationEvents.clear();
    m_playEntityNames.clear();
    m_playTriggerActions.clear();
    m_playCameraZones.clear();
    m_playCameraZonesInside.clear();
    m_activePlayCameraZone = engine::ecs::kNull;
    m_playCameraOverride.reset();
    m_playAgents.clear();
    m_cameraBlend.Cancel();
    m_cameraShake.Clear();
    m_cameraSequence.Stop();
    m_cameraDirector.SetStopped();
    m_cameraDirector.ClearEvents();
    m_cameraDirector.TakeCommands();
    m_cinematicSkipPrev = false;
    m_activeCinematicCues.clear();
    m_cameraSequencePaused = false;
    m_cameraBeforeShake.reset();

    if (m_editCameraBeforePlay) {
        m_camera = *m_editCameraBeforePlay;
    }
    m_editCameraBeforePlay.reset();

    if (!m_cube || !m_plane || !m_sphere || !m_capsule || !m_cylinder || !m_cone || !m_pyramid || !m_torus || !m_staircase) {
        m_log.Error("Could not restore edit scene");
        m_mode = EditorMode::Edit;
        m_editSnapshot.reset();
        return;
    }

    if (m_editSnapshot) {
        m_scene.RestoreFromSnapshot(*m_editSnapshot, *m_cube, *m_plane, *m_sphere, *m_capsule, *m_cylinder, *m_cone, *m_pyramid, *m_torus, *m_staircase);
    }
    m_editSnapshot.reset();
    m_mode = EditorMode::Edit;
    m_log.Info("Edit mode: restored scene from before Play");
}

bool EditorApp::BuildPlayRuntimePreview(std::string * error)
{
    if (!m_cube || !m_plane || !m_sphere || !m_capsule || !m_cylinder || !m_cone || !m_pyramid || !m_torus || !m_staircase) {
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
            *m_capsule,
            *m_cylinder,
            *m_cone,
            *m_pyramid,
            *m_torus,
            *m_staircase,
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

    m_playRegistry->view<engine::AnimatedModel>().each(
        [this](engine::ecs::Entity entity, engine::AnimatedModel& animated) {
            animated.onEvent = [this, entity](const std::string& name) {
                if (!name.empty()) {
                    if (m_playRegistry) {
                        m_runtimeAudio.ProcessAnimationEvent(*m_playRegistry, entity, name);
                        engine::ProcessParticleAnimationEvent(*m_playRegistry, entity, name);
                    }
                    m_playAnimationEvents.push_back(engine::ScriptAnimationEvent{
                        entity,
                        name
                    });
                }
            };
        }
    );

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
    ConfigurePlayPlayerController(playEntitiesByName);
    BuildPlayTriggerActions(playEntitiesByName);
    BuildPlayCameraZones(playEntitiesByName);
    BuildPlayAgents(playEntitiesByName);
    BuildPlayAudioSources();
    return true;
}

void EditorApp::BuildPlayAudioSources() {
    m_runtimeAudio.Stop();
    m_playAudioSources.clear();
    if (!m_audio.IsAvailable() || !m_playRegistry) return;

    m_runtimeAudio.Update(*m_playRegistry);
    engine::ecs::Pool<engine::ecs::AudioSource>* audioSources =
        m_playRegistry->TryPool<engine::ecs::AudioSource>();
    if (!audioSources) return;
    for (const engine::ecs::Entity entity : audioSources->dense) {
        const engine::ecs::AudioSource& audio = audioSources->Get(entity);
        const engine::ecs::RuntimeName* runtimeName =
            m_playRegistry->TryGet<engine::ecs::RuntimeName>(entity);
        const std::string name = runtimeName ? runtimeName->value : "RuntimeAudioSource";
        const auto source = m_runtimeAudio.SourceFor(entity);
        if (source == engine::AudioEngine::InvalidSource) {
            m_log.Warning("Audio source failed to load: " + name + " -> " + audio.path);
            continue;
        }
        m_playAudioSources.push_back({entity, source, name, audio.spatial});
    }
    if (!m_playAudioSources.empty()) {
        m_log.Info("Audio: " + std::to_string(m_playAudioSources.size()) + " managed source(s) active");
    }
}

void EditorApp::UpdatePlayAudioSources() {
    if (!m_playRegistry) return;
    m_runtimeAudio.Update(*m_playRegistry, m_dt);
    m_runtimeAudio.UpdateOcclusion(*m_playRegistry, m_playPhysics, m_camera.Position());
    for (PlayAudioSource& source : m_playAudioSources) {
        source.source = m_runtimeAudio.SourceFor(source.entity);
        if (const engine::ecs::AudioSource* audio =
                m_playRegistry->TryGet<engine::ecs::AudioSource>(source.entity)) {
            source.spatial = audio->spatial;
        }
    }
}

void EditorApp::BuildPlayAgents(const std::unordered_map<std::string, engine::ecs::Entity>& playEntitiesByName)
{
    m_playAgents.clear();
    m_playNavGrid = engine::ai::NavGrid{};
    m_playBtGraphCache.clear();
    if (!m_playRegistry) {
        return;
    }

    // Resolves a Subtree node's asset path to a loaded graph (cached for the session).
    // unordered_map keeps element references stable, so nested subtrees are safe.
    auto resolveSubtree = [this](const std::string& path) -> const engine::ai::BehaviorGraph* {
        if (path.empty()) return nullptr;
        const auto it = m_playBtGraphCache.find(path);
        if (it != m_playBtGraphCache.end()) return &it->second;
        engine::ai::BehaviorGraph sub;
        std::string err;
        if (!engine::ai::LoadBehaviorGraph(path, sub, &err)) {
            m_log.Warning("AI: subtree '" + path + "' failed to load: " + err);
            return nullptr;
        }
        return &m_playBtGraphCache.emplace(path, std::move(sub)).first->second;
    };

    for (const EditorScene::Object& object : m_scene.Objects()) {
        if (!object.navAgentEnabled) {
            continue;   // patrol points are optional (a static sentry just chases on sight)
        }
        const auto it = playEntitiesByName.find(object.name);
        if (it == playEntitiesByName.end()) {
            continue;
        }
        const engine::ecs::Entity entity = it->second;

        PlayAgent playAgent;
        playAgent.entity = entity;
        playAgent.name = object.name;
        playAgent.team = object.navAgentTeam;
        playAgent.autoTarget = object.navAgentAutoTarget;
        playAgent.brain.agent.maxSpeed = std::max(object.navAgentSpeed, 0.0f);
        playAgent.brain.agent.maxForce = std::max(object.navAgentMaxForce, 0.0f);
        playAgent.brain.reachRadius = std::max(object.navAgentReachRadius, 0.05f);
        playAgent.brain.repathInterval = std::max(object.navAgentRepathInterval, 0.05f);
        playAgent.brain.patrol = object.patrolPoints;
        playAgent.brain.vision.range = std::max(object.navAgentVisionRange, 0.0f);
        playAgent.brain.vision.halfAngleDegrees = object.navAgentVisionHalfAngle;
        if (!object.navAgentTargetName.empty()) {
            const auto target = playEntitiesByName.find(object.navAgentTargetName);
            if (target != playEntitiesByName.end()) {
                playAgent.targetEntity = target->second;
            }
        }
        glm::vec3 startPos(0.0f);
        if (const engine::ecs::Transform* t = m_playRegistry->TryGet<engine::ecs::Transform>(entity)) {
            startPos = t->position;
            playAgent.brain.SetPosition(startPos);
        }

        // M7: if the agent references a behaviour-tree asset, load it and drive the
        // agent from a data-driven tree instead of the built-in brain.
        if (!object.navAgentBrainAsset.empty()) {
            engine::ai::BehaviorGraph graph;
            std::string err;
            if (engine::ai::LoadBehaviorGraph(object.navAgentBrainAsset, graph, &err) && graph.IsValid()) {
                playAgent.useGraph = true;
                playAgent.ctx.agent.maxSpeed = std::max(object.navAgentSpeed, 0.0f);
                playAgent.ctx.agent.maxForce = std::max(object.navAgentMaxForce, 0.0f);
                playAgent.ctx.agent.position = startPos;
                playAgent.ctx.reachRadius = std::max(object.navAgentReachRadius, 0.05f);
                playAgent.ctx.repathInterval = std::max(object.navAgentRepathInterval, 0.05f);
                playAgent.ctx.patrol = object.patrolPoints;
                engine::ai::SeedBlackboard(graph.blackboard, playAgent.ctx.blackboard);
                playAgent.ctx.nodeStatus.assign(graph.nodes.size(), 0);   // debugger buffer
                playAgent.tree = engine::ai::BuildBehaviorTree(graph, resolveSubtree);
                m_log.Info("AI: '" + object.name + "' running behaviour tree " + object.navAgentBrainAsset);
            } else {
                m_log.Warning("AI: could not load brain '" + object.navAgentBrainAsset + "': " + err);
            }
        }

        m_playAgents.push_back(std::move(playAgent));
    }

    BakePlayNavGrid();   // chase/search pathfinding needs a grid of the static geometry
    if (m_useNavMesh) {
        BakePlayNavMesh();   // funnel-smoothed alternative (M6); agents use it when enabled
    }

    if (!m_playAgents.empty()) {
        m_log.Info("AI: " + std::to_string(m_playAgents.size()) + " nav agent(s) active" +
                   (m_useNavMesh ? " (navmesh)" : " (navgrid)"));
    }
}

void EditorApp::BakePlayNavGrid()
{
    m_playNavGrid = engine::ai::NavGrid{};
    if (!m_playRegistry || m_playAgents.empty()) {
        return;
    }

    // Static, solid box/sphere footprints become obstacles; overall bounds cover the
    // agents, their patrol points and every collider (plus a margin).
    struct Footprint { glm::vec2 center; glm::vec2 half; bool circle; float radius; };
    std::vector<Footprint> obstacles;
    glm::vec2 mn(1.0e9f), mx(-1.0e9f);
    float groundY = 0.0f;
    bool anyBounds = FindAuthoredNavBounds(m_scene, &mn, &mx, &groundY);
    const bool authoredBounds = anyBounds;
    auto extend = [&](const glm::vec2& lo, const glm::vec2& hi) {
        if (authoredBounds) return;
        mn = glm::min(mn, lo); mx = glm::max(mx, hi); anyBounds = true;
    };

    m_playRegistry->view<engine::ecs::Transform, engine::ecs::Collider>().each(
        [&](engine::ecs::Entity e, engine::ecs::Transform& t, engine::ecs::Collider& c) {
            const engine::ecs::RigidBody* rb = m_playRegistry->TryGet<engine::ecs::RigidBody>(e);
            const bool dynamic = rb && rb->invMass > 0.0f;
            const glm::vec2 pos(t.position.x, t.position.z);
            if (c.shape == engine::ecs::ColliderShape::Plane) {
                if (!authoredBounds) groundY = t.position.y;
                return;
            }
            if (c.shape == engine::ecs::ColliderShape::Box) {
                const glm::vec2 half(c.halfExtents.x, c.halfExtents.z);
                extend(pos - half, pos + half);
                if (!dynamic && !c.isTrigger) obstacles.push_back({pos, half, false, 0.0f});
            } else {   // Sphere or Capsule
                const glm::vec2 half(c.radius);
                extend(pos - half, pos + half);
                if (!dynamic && !c.isTrigger) obstacles.push_back({pos, half, true, c.radius});
            }
        });

    for (const PlayAgent& a : m_playAgents) {
        if (const engine::ecs::Transform* t = m_playRegistry->TryGet<engine::ecs::Transform>(a.entity)) {
            if (!authoredBounds) groundY = t->position.y;
            extend(glm::vec2(t->position.x, t->position.z), glm::vec2(t->position.x, t->position.z));
        }
        const std::vector<glm::vec3>& patrol = a.useGraph ? a.ctx.patrol : a.brain.patrol;
        for (const glm::vec3& w : patrol) {
            extend(glm::vec2(w.x, w.z), glm::vec2(w.x, w.z));
        }
    }
    if (!anyBounds) {
        return;
    }

    constexpr float kMargin = 5.0f, kCell = 0.5f, kAgentRadius = 0.4f;
    if (!authoredBounds) {
        mn -= glm::vec2(kMargin);
        mx += glm::vec2(kMargin);
    }
    const int w = std::clamp(static_cast<int>(std::ceil((mx.x - mn.x) / kCell)), 1, 512);
    const int h = std::clamp(static_cast<int>(std::ceil((mx.y - mn.y) / kCell)), 1, 512);
    engine::ai::NavGrid grid(w, h, kCell, glm::vec3(mn.x, groundY, mn.y));

    for (const Footprint& o : obstacles) {
        const glm::ivec2 rawC0 = grid.WorldToCell(glm::vec3(
            o.center.x - o.half.x - kAgentRadius, groundY,
            o.center.y - o.half.y - kAgentRadius));
        const glm::ivec2 rawC1 = grid.WorldToCell(glm::vec3(
            o.center.x + o.half.x + kAgentRadius, groundY,
            o.center.y + o.half.y + kAgentRadius));
        if (rawC1.x < 0 || rawC1.y < 0 || rawC0.x >= w || rawC0.y >= h) continue;
        const glm::ivec2 c0 = glm::clamp(rawC0, glm::ivec2(0), glm::ivec2(w - 1, h - 1));
        const glm::ivec2 c1 = glm::clamp(rawC1, glm::ivec2(0), glm::ivec2(w - 1, h - 1));
        for (int y = c0.y; y <= c1.y; ++y) {
            for (int x = c0.x; x <= c1.x; ++x) {
                const glm::vec3 wp = grid.CellToWorld(x, y);
                const glm::vec2 d(wp.x - o.center.x, wp.z - o.center.y);
                const bool blocked = o.circle
                    ? (glm::length(d) <= o.radius + kAgentRadius)
                    : (std::abs(d.x) <= o.half.x + kAgentRadius && std::abs(d.y) <= o.half.y + kAgentRadius);
                if (blocked) grid.SetObstacle(x, y);
            }
        }
    }

    m_playNavGrid = std::move(grid);
}

void EditorApp::BakePlayNavMesh()
{
    m_playNavMesh = engine::ai::NavMesh{};
    if (!m_playRegistry || m_playAgents.empty()) {
        return;
    }

    // Same static-collider set as the grid bake, but emitted as axis-aligned box
    // obstacles for NavMeshBuilder (it uses the XZ footprint and erodes by agentRadius,
    // so obstacles are passed raw — no pre-growing here).
    std::vector<engine::ai::NavObstacle> obstacles;
    glm::vec2 mn(1.0e9f), mx(-1.0e9f);
    float groundY = 0.0f;
    bool anyBounds = FindAuthoredNavBounds(m_scene, &mn, &mx, &groundY);
    const bool authoredBounds = anyBounds;
    auto extend = [&](const glm::vec2& lo, const glm::vec2& hi) {
        if (authoredBounds) return;
        mn = glm::min(mn, lo); mx = glm::max(mx, hi); anyBounds = true;
    };

    m_playRegistry->view<engine::ecs::Transform, engine::ecs::Collider>().each(
        [&](engine::ecs::Entity e, engine::ecs::Transform& t, engine::ecs::Collider& c) {
            const engine::ecs::RigidBody* rb = m_playRegistry->TryGet<engine::ecs::RigidBody>(e);
            const bool dynamic = rb && rb->invMass > 0.0f;
            const glm::vec2 pos(t.position.x, t.position.z);
            if (c.shape == engine::ecs::ColliderShape::Plane) {
                if (!authoredBounds) groundY = t.position.y;
                return;
            }
            glm::vec2 half;
            if (c.shape == engine::ecs::ColliderShape::Box) {
                half = glm::vec2(c.halfExtents.x, c.halfExtents.z);
            } else {   // Sphere or Capsule -> square footprint of its radius
                half = glm::vec2(c.radius);
            }
            extend(pos - half, pos + half);
            if (!dynamic && !c.isTrigger) {
                engine::ai::NavObstacle o;
                o.center = glm::vec3(pos.x, groundY, pos.y);
                o.halfExtents = glm::vec3(half.x, 0.5f, half.y);
                obstacles.push_back(o);
            }
        });

    for (const PlayAgent& a : m_playAgents) {
        if (const engine::ecs::Transform* t = m_playRegistry->TryGet<engine::ecs::Transform>(a.entity)) {
            if (!authoredBounds) groundY = t->position.y;
            extend(glm::vec2(t->position.x, t->position.z), glm::vec2(t->position.x, t->position.z));
        }
        const std::vector<glm::vec3>& patrol = a.useGraph ? a.ctx.patrol : a.brain.patrol;
        for (const glm::vec3& w : patrol) {
            extend(glm::vec2(w.x, w.z), glm::vec2(w.x, w.z));
        }
    }
    if (!anyBounds) {
        return;
    }

    constexpr float kMargin = 5.0f;
    if (!authoredBounds) {
        mn -= glm::vec2(kMargin);
        mx += glm::vec2(kMargin);
    }

    engine::ai::NavBuildConfig cfg;
    cfg.boundsMin = glm::vec3(mn.x, groundY, mn.y);
    cfg.boundsMax = glm::vec3(mx.x, groundY, mx.y);
    cfg.cellSize = 0.5f;
    cfg.agentRadius = 0.4f;
    m_playNavMesh = engine::ai::NavMeshBuilder::Build(cfg, obstacles);
}

void EditorApp::BakeEditorNavMesh()
{
    m_editorNavMesh = engine::ai::NavMesh{};
    std::vector<engine::ai::NavObstacle> obstacles;
    glm::vec2 mn(1.0e9f), mx(-1.0e9f);
    float groundY = 0.0f;
    bool anyBounds = FindAuthoredNavBounds(m_scene, &mn, &mx, &groundY);
    const bool authoredBounds = anyBounds;
    auto extend = [&](const glm::vec2& lo, const glm::vec2& hi) {
        if (authoredBounds) return;
        mn = glm::min(mn, lo);
        mx = glm::max(mx, hi);
        anyBounds = true;
    };

    for (const EditorScene::Object& object : m_scene.Objects()) {
        if (object.navMeshBoundsVolume) continue;
        const Transform* transform = m_scene.TryGetTransform(object.entity);
        if (!transform) continue;

        if (object.navAgentEnabled) {
            extend(glm::vec2(transform->position.x, transform->position.z),
                   glm::vec2(transform->position.x, transform->position.z));
            for (const glm::vec3& point : object.patrolPoints) {
                extend(glm::vec2(point.x, point.z), glm::vec2(point.x, point.z));
            }
        }
        if (!object.colliderEnabled || object.collider.isTrigger) continue;
        if (object.rigidBodyEnabled && object.rigidBody.invMass > 0.0f) continue;

        const engine::ecs::Collider& collider = object.collider;
        if (collider.shape == engine::ecs::ColliderShape::Plane) {
            if (!authoredBounds) groundY = transform->position.y;
            continue;
        }

        glm::vec2 half(0.5f);
        switch (collider.shape) {
        case engine::ecs::ColliderShape::Box:
        case engine::ecs::ColliderShape::Pyramid:
        case engine::ecs::ColliderShape::Staircase:
            half = glm::vec2(collider.halfExtents.x, collider.halfExtents.z);
            break;
        case engine::ecs::ColliderShape::Torus: {
            const float outer = collider.majorRadius + collider.minorRadius;
            half = glm::vec2(outer);
            break;
        }
        case engine::ecs::ColliderShape::Sphere:
        case engine::ecs::ColliderShape::Capsule:
        case engine::ecs::ColliderShape::Cylinder:
        case engine::ecs::ColliderShape::Cone:
            half = glm::vec2(collider.radius);
            break;
        case engine::ecs::ColliderShape::Plane:
            break;
        }
        half = glm::max(half, glm::vec2(0.001f));
        const glm::vec2 center(transform->position.x, transform->position.z);
        extend(center - half, center + half);
        engine::ai::NavObstacle obstacle;
        obstacle.center = glm::vec3(center.x, groundY, center.y);
        obstacle.halfExtents = glm::vec3(half.x, 0.5f, half.y);
        obstacles.push_back(obstacle);
    }

    if (!anyBounds) {
        m_log.Warning("Navigation preview needs a Nav Mesh Bounds Volume, collider, or Nav Agent");
        return;
    }
    if (!authoredBounds) {
        constexpr float margin = 5.0f;
        mn -= glm::vec2(margin);
        mx += glm::vec2(margin);
    }
    if (mx.x - mn.x < 0.05f || mx.y - mn.y < 0.05f) {
        m_log.Warning("Navigation preview bounds are too small");
        return;
    }

    engine::ai::NavBuildConfig config;
    config.boundsMin = glm::vec3(mn.x, groundY, mn.y);
    config.boundsMax = glm::vec3(mx.x, groundY, mx.y);
    config.cellSize = 0.5f;
    config.agentRadius = 0.4f;
    m_editorNavMesh = engine::ai::NavMeshBuilder::Build(config, obstacles);
    m_log.Info("Navigation preview rebuilt: " + std::to_string(m_editorNavMesh.polys.size())
        + " walkable polygon(s)");
}

void EditorApp::UpdateAI(float dt)
{
    if (!m_playRegistry || m_playAgents.empty()) {
        return;
    }
    for (PlayAgent& playAgent : m_playAgents) {
        if (!m_playRegistry->Valid(playAgent.entity)) {
            continue;
        }
        engine::ecs::Transform* t = m_playRegistry->TryGet<engine::ecs::Transform>(playAgent.entity);
        if (!t) {
            continue;
        }
        const engine::AnimatedModel* animated =
            m_playRegistry->TryGet<engine::AnimatedModel>(playAgent.entity);
        const bool movementLocked = animated && animated->BlocksMovement();
        const glm::vec3 lockedPosition = t->position;

        // Faction auto-targeting: acquire the nearest living agent on a different
        // non-zero team as this agent's chase target (re-evaluated each tick).
        if (playAgent.autoTarget && playAgent.team != 0) {
            engine::ecs::Entity best = engine::ecs::kNull;
            float bestDist = 1.0e18f;
            for (const PlayAgent& other : m_playAgents) {
                if (other.entity == playAgent.entity) continue;
                if (other.team == 0 || other.team == playAgent.team) continue;
                if (!m_playRegistry->Valid(other.entity)) continue;
                const engine::ecs::Transform* ot =
                    m_playRegistry->TryGet<engine::ecs::Transform>(other.entity);
                if (!ot) continue;
                if (const engine::Health* h = m_playRegistry->TryGet<engine::Health>(other.entity)) {
                    if (!h->alive) continue;
                }
                const float d = glm::length(ot->position - t->position);
                if (d < bestDist) { bestDist = d; best = other.entity; }
            }
            playAgent.targetEntity = best;
        }

        // Current pose + vision come from whichever brain drives this agent.
        const glm::vec3 agentPos    = playAgent.useGraph ? playAgent.ctx.agent.position
                                                         : playAgent.brain.Position();
        const glm::vec3 agentFacing = playAgent.useGraph ? playAgent.ctx.facing
                                                         : playAgent.brain.Facing();

        // Perception: can the agent see its chase target right now?
        glm::vec3 targetPos = agentPos;
        bool seesTarget = false;
        if (playAgent.targetEntity != engine::ecs::kNull && m_playRegistry->Valid(playAgent.targetEntity)) {
            if (const engine::ecs::Transform* tt =
                    m_playRegistry->TryGet<engine::ecs::Transform>(playAgent.targetEntity)) {
                targetPos = tt->position;
                // Offset the eye up and forward so the LOS ray clears the agent's own
                // collider (CanSee has no source-exclusion; keep agents small-collidered).
                glm::vec3 forward = agentFacing;
                forward.y = 0.0f;
                forward = (glm::dot(forward, forward) > 1.0e-6f) ? glm::normalize(forward)
                                                                 : glm::vec3(0.0f, 0.0f, 1.0f);
                const glm::vec3 eye = agentPos + glm::vec3(0.0f, 0.6f, 0.0f) + forward * 0.6f;
                seesTarget = engine::ai::CanSee(eye, forward, playAgent.brain.vision,
                                                targetPos, playAgent.targetEntity,
                                                m_playPhysics, *m_playRegistry);
            }
        }

        glm::vec3 facing;
        if (playAgent.useGraph) {
            // Data-driven behaviour tree drives the steering body.
            engine::ai::AgentContext& c = playAgent.ctx;
            c.dt = dt;
            c.targetPos = targetPos;
            c.seesTarget = seesTarget;
            c.grid = m_useNavMesh ? nullptr : &m_playNavGrid;
            c.mesh = m_useNavMesh ? &m_playNavMesh : nullptr;
            c.registry = &*m_playRegistry;   // let script nodes reach the ECS
            c.self = playAgent.entity;
            c.targetEntity = playAgent.targetEntity;
            c.steer = glm::vec3(0.0f);
            std::fill(c.nodeStatus.begin(), c.nodeStatus.end(), 0);   // reset per-frame debug status
            playAgent.tree.Tick(c, dt);
            if (movementLocked) {
                c.steer = glm::vec3(0.0f);
                c.agent.velocity = glm::vec3(0.0f);
                c.agent.position = lockedPosition;
            } else {
                engine::ai::Integrate(c.agent, c.steer, dt);
                if (glm::length(c.agent.velocity) > 1e-3f) {
                    c.facing = glm::normalize(c.agent.velocity);
                }
            }
            t->position = c.agent.position;
            facing = c.facing;
        } else {
            if (movementLocked) {
                playAgent.brain.SetPosition(lockedPosition);
                playAgent.brain.agent.velocity = glm::vec3(0.0f);
            } else {
                if (m_useNavMesh) {
                    playAgent.brain.Update(dt, targetPos, seesTarget, m_playNavMesh);
                } else {
                    playAgent.brain.Update(dt, targetPos, seesTarget, m_playNavGrid);
                }
            }
            t->position = playAgent.brain.Position();
            facing = playAgent.brain.Facing();
        }
        // Walk on terrain: snap the agent to the terrain surface beneath it, if any.
        bool overTerrain = false;
        const float surfaceY = TerrainSurfaceY(t->position.x, t->position.z, overTerrain);
        if (overTerrain) {
            t->position.y = surfaceY;
            if (playAgent.useGraph) {
                playAgent.ctx.agent.position.y = surfaceY;
            } else {
                playAgent.brain.SetPosition(glm::vec3(t->position.x, surfaceY, t->position.z));
            }
        }
        if (glm::dot(facing, facing) > 1e-6f) {
            const float yaw = std::atan2(facing.x, facing.z);
            t->rotation = glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f));
        }
    }

    // Crowd separation: gently push agents apart (in the XZ plane) so a group chasing
    // the same target doesn't collapse into one point. Position-based + clamped, so it
    // works the same for built-in and behaviour-tree agents without fighting steering.
    if (m_playAgents.size() > 1) {
        constexpr float kSepRadius = 1.2f;    // start pushing when closer than this
        constexpr float kSepGain = 0.5f;      // fraction of the overlap corrected per tick
        constexpr float kMaxMove = 0.10f;     // clamp per-tick nudge (metres)
        for (PlayAgent& a : m_playAgents) {
            if (!m_playRegistry->Valid(a.entity)) continue;
            engine::ecs::Transform* ta = m_playRegistry->TryGet<engine::ecs::Transform>(a.entity);
            if (!ta) continue;
            const engine::AnimatedModel* animated =
                m_playRegistry->TryGet<engine::AnimatedModel>(a.entity);
            if (animated && animated->BlocksMovement()) continue;
            glm::vec3 push(0.0f);
            for (const PlayAgent& b : m_playAgents) {
                if (b.entity == a.entity || !m_playRegistry->Valid(b.entity)) continue;
                const engine::ecs::Transform* tb = m_playRegistry->TryGet<engine::ecs::Transform>(b.entity);
                if (!tb) continue;
                glm::vec3 d = ta->position - tb->position;
                d.y = 0.0f;
                const float dist = glm::length(d);
                if (dist > 1.0e-4f && dist < kSepRadius) {
                    push += (d / dist) * (kSepRadius - dist);
                }
            }
            glm::vec3 delta = push * kSepGain;
            const float len = glm::length(delta);
            if (len > 1.0e-5f) {
                if (len > kMaxMove) delta *= kMaxMove / len;
                ta->position += delta;
                if (a.useGraph) a.ctx.agent.position = ta->position;   // keep AI position in sync
                else            a.brain.SetPosition(ta->position);
            }
        }
    }
}

void EditorApp::ConfigurePlayPlayerController(const std::unordered_map<std::string, engine::ecs::Entity> &playEntitiesByName)
{
    m_playPlayerController.reset();
    m_playPlayerEntity = engine::ecs::kNull;
    m_playLockTarget = engine::ecs::kNull;
    m_playLockTogglePrev = false;
    if (!m_playRegistry) {
        return;
    }

    for (const EditorScene::Object& object : m_scene.Objects()) {
        if (!object.playerControllerEnabled) {
            continue;
        }

        const auto found = playEntitiesByName.find(object.name);
        if (found == playEntitiesByName.end()) {
            continue;
        }

        const EditorScene::PlayerControllerSettings& settings = object.playerController;
        engine::PlayerController controller;
        controller.view = settings.firstPerson
            ? engine::PlayerController::View::FirstPerson
            : engine::PlayerController::View::ThirdPerson;
        controller.walkSpeed = settings.walkSpeed;
        controller.runSpeed = settings.runSpeed;
        controller.jumpSpeed = settings.jumpSpeed;
        controller.lookSensitivity = settings.lookSensitivity;
        controller.eyeHeight = settings.eyeHeight;
        controller.camDistance = settings.cameraDistance;
        controller.camTargetHeight = settings.cameraTargetHeight;
        controller.camCollision = settings.cameraCollision;
        controller.camProbeRadius = settings.cameraProbeRadius;
        controller.camCollisionPadding = settings.cameraCollisionPadding;
        controller.camReturnSpeed = settings.cameraReturnSpeed;
        controller.shoulderCamera = settings.shoulderCamera;
        controller.shoulderOffset = settings.shoulderOffset;
        controller.shoulderSwitchSpeed = settings.shoulderSwitchSpeed;
        controller.rightShoulder = settings.rightShoulder;
        controller.lockOnEnabled = settings.lockOnEnabled;
        controller.lockOnRange = settings.lockOnRange;
        controller.lockOnViewAngle = settings.lockOnViewAngle;
        controller.lockOnTargetHeight = settings.lockOnTargetHeight;
        controller.lockOnTrackingSpeed = settings.lockOnTrackingSpeed;
        controller.body.stepHeight = settings.stepHeight;
        controller.body.SetMaxSlopeDegrees(settings.maxSlopeDegrees);
        controller.SetCapsule(settings.capsuleRadius, settings.capsuleHeight);

        m_playPlayerEntity = found->second;
        if (const Transform* transform = m_playRegistry->TryGet<Transform>(m_playPlayerEntity)) {
            controller.SetPosition(transform->position);
        }
        if (m_playRegistry->Has<engine::ecs::Collider>(m_playPlayerEntity)) {
            m_playRegistry->Remove<engine::ecs::Collider>(m_playPlayerEntity);
        }
        if (m_playRegistry->Has<engine::ecs::RigidBody>(m_playPlayerEntity)) {
            m_playRegistry->Remove<engine::ecs::RigidBody>(m_playPlayerEntity);
        }

        m_playPlayerController = controller;
        m_log.Info("Play mode player: using " + object.name);
        return;
    }
}

void EditorApp::BuildPlayTriggerActions(const std::unordered_map<std::string, engine::ecs::Entity>& playEntitiesByName) {
    m_playTriggerActions.clear();
    for (const EditorScene::Object& object : m_scene.Objects()) {
        if (!object.colliderEnabled || !object.collider.isTrigger) {
            continue;
        }
        const bool hasTransformAction =
            object.triggerEnterMoverAction != EditorScene::TriggerActionMode::None
            || object.triggerEnterRotatorAction != EditorScene::TriggerActionMode::None
            || object.triggerExitMoverAction != EditorScene::TriggerActionMode::None
            || object.triggerExitRotatorAction != EditorScene::TriggerActionMode::None;
        const bool hasCameraAction =
            object.triggerEnterCameraAction != EditorScene::CameraSequenceTriggerAction::None
            || object.triggerExitCameraAction != EditorScene::CameraSequenceTriggerAction::None;
        if (!hasTransformAction && !hasCameraAction) {
            continue;
        }
        if (hasTransformAction && object.triggerTargetName.empty()) {
            continue;
        }
        if (object.triggerEnterMoverAction == EditorScene::TriggerActionMode::None
            && object.triggerEnterRotatorAction == EditorScene::TriggerActionMode::None
            && object.triggerExitMoverAction == EditorScene::TriggerActionMode::None
            && object.triggerExitRotatorAction == EditorScene::TriggerActionMode::None
            && !hasCameraAction) {
            continue;
        }

        const auto trigger = playEntitiesByName.find(object.name);
        const auto target = playEntitiesByName.find(object.triggerTargetName);
        if (trigger == playEntitiesByName.end()
            || (hasTransformAction && target == playEntitiesByName.end())) {
            continue;
        }

        PlayTriggerAction action;
        action.target = target != playEntitiesByName.end()
            ? target->second : engine::ecs::kNull;
        action.enterMoverAction = object.triggerEnterMoverAction;
        action.enterRotatorAction = object.triggerEnterRotatorAction;
        action.exitMoverAction = object.triggerExitMoverAction;
        action.exitRotatorAction = object.triggerExitRotatorAction;
        action.cameraSequenceName = object.triggerCameraSequenceName;
        action.enterCameraAction = object.triggerEnterCameraAction;
        action.exitCameraAction = object.triggerExitCameraAction;
        action.cameraLockInput = object.triggerCameraLockInput;
        action.cameraSkippable = object.triggerCameraSkippable;

        for (const EditorScene::Object& targetObject : m_scene.Objects()) {
            if (targetObject.name == object.triggerTargetName) {
                action.mover = targetObject.mover;
                action.rotator = targetObject.rotator;
                break;
            }
        }

        m_playTriggerActions[trigger->second] = action;
    }
}

void EditorApp::BuildPlayCameraZones(
    const std::unordered_map<std::string, engine::ecs::Entity>& playEntitiesByName) {
    m_playCameraZones.clear();
    m_playCameraZonesInside.clear();
    m_activePlayCameraZone = engine::ecs::kNull;

    for (const EditorScene::Object& object : m_scene.Objects()) {
        if (!object.cameraZoneEnabled || !object.colliderEnabled
            || !object.collider.isTrigger || object.cameraZonePresetName.empty()) {
            continue;
        }
        const auto trigger = playEntitiesByName.find(object.name);
        if (trigger == playEntitiesByName.end()) continue;
        const auto preset = std::find_if(
            m_scene.CameraPresets().begin(), m_scene.CameraPresets().end(),
            [&](const EditorScene::CameraPreset& camera) {
                return camera.name == object.cameraZonePresetName;
            });
        if (preset == m_scene.CameraPresets().end()) {
            m_log.Warning("Camera Zone '" + object.name
                          + "' references a missing preset: " + object.cameraZonePresetName);
            continue;
        }

        PlayCameraZone zone;
        zone.presetName = object.cameraZonePresetName;
        zone.restoreOnExit = object.cameraZoneRestoreOnExit;
        zone.priority = object.cameraZonePriority;
        zone.returnBlend = std::max(object.cameraZoneReturnBlend, 0.0f);
        m_playCameraZones[trigger->second] = std::move(zone);
    }
}

void EditorApp::ApplyPlayCameraZoneEvent(engine::ecs::Entity trigger,
                                         engine::ecs::Entity other,
                                         engine::CollisionEvent::Phase phase) {
    if (other != m_playPlayerEntity || m_playCameraZones.find(trigger) == m_playCameraZones.end()) {
        return;
    }
    if (phase == engine::CollisionEvent::Phase::Enter) {
        m_playCameraZonesInside.insert(trigger);
    } else if (phase == engine::CollisionEvent::Phase::Exit) {
        m_playCameraZonesInside.erase(trigger);
    } else {
        return;
    }
    RefreshPlayCameraZone();
}

void EditorApp::RefreshPlayCameraZone() {
    engine::ecs::Entity best = engine::ecs::kNull;
    int bestPriority = std::numeric_limits<int>::min();
    if (m_playCameraZonesInside.count(m_activePlayCameraZone) != 0) {
        best = m_activePlayCameraZone;
        bestPriority = m_playCameraZones.at(best).priority;
    }
    for (engine::ecs::Entity entity : m_playCameraZonesInside) {
        const auto it = m_playCameraZones.find(entity);
        if (it != m_playCameraZones.end() && it->second.priority > bestPriority) {
            best = entity;
            bestPriority = it->second.priority;
        }
    }
    if (best == m_activePlayCameraZone) return;

    const auto previous = m_playCameraZones.find(m_activePlayCameraZone);
    const bool restoreOnExit = previous == m_playCameraZones.end()
        || previous->second.restoreOnExit;
    const float returnBlend = previous == m_playCameraZones.end()
        ? 0.35f : previous->second.returnBlend;
    m_activePlayCameraZone = best;

    if (best != engine::ecs::kNull) {
        const PlayCameraZone& zone = m_playCameraZones.at(best);
        const auto preset = std::find_if(
            m_scene.CameraPresets().begin(), m_scene.CameraPresets().end(),
            [&](const EditorScene::CameraPreset& camera) {
                return camera.name == zone.presetName;
            });
        if (preset == m_scene.CameraPresets().end()) {
            m_log.Warning("Camera Zone preset was not found: " + zone.presetName);
            m_playCameraOverride.reset();
            m_activePlayCameraZone = engine::ecs::kNull;
            return;
        }
        m_playCameraOverride = *preset;
        BeginCameraBlend(*preset);
        m_log.Info("Camera Zone activated: " + zone.presetName);
        return;
    }

    if (!restoreOnExit) return;
    m_playCameraOverride.reset();
    if (const EditorScene::CameraPreset* primary = m_scene.PrimaryCameraPreset();
        primary && primary->useInPlay) {
        EditorScene::CameraPreset target = *primary;
        target.blendDuration = returnBlend;
        BeginCameraBlend(target);
        return;
    }

    if (m_playPlayerController) {
        EditorScene::CameraPreset playerCamera;
        playerCamera.position = m_playPlayerController->CameraPosition();
        playerCamera.target = m_playPlayerController->CameraTarget();
        playerCamera.fov = m_editCameraBeforePlay ? m_editCameraBeforePlay->fov : 45.0f;
        playerCamera.nearPlane = m_editCameraBeforePlay ? m_editCameraBeforePlay->nearPlane : 0.1f;
        playerCamera.farPlane = m_editCameraBeforePlay ? m_editCameraBeforePlay->farPlane : 3000.0f;
        playerCamera.blendDuration = returnBlend;
        playerCamera.blendEasing = static_cast<int>(engine::CameraBlend::Easing::SmoothStep);
        BeginCameraBlend(playerCamera);
    }
}

void EditorApp::ApplyPlayTriggerAction(engine::ecs::Entity trigger, engine::ecs::Entity, engine::CollisionEvent::Phase phase) {
    if (!m_playRegistry) {
        return;
    }

    const auto actionIt = m_playTriggerActions.find(trigger);
    if (actionIt == m_playTriggerActions.end()) {
        return;
    }

    const PlayTriggerAction& action = actionIt->second;
    const std::string triggerName = m_playEntityNames.count(trigger) ? m_playEntityNames[trigger] : "Trigger";
    const std::string targetName = m_playEntityNames.count(action.target) ? m_playEntityNames[action.target] : "Target";
    const EditorScene::TriggerActionMode moverAction = phase == engine::CollisionEvent::Phase::Exit
        ? action.exitMoverAction
        : action.enterMoverAction;
    const EditorScene::TriggerActionMode rotatorAction = phase == engine::CollisionEvent::Phase::Exit
        ? action.exitRotatorAction
        : action.enterRotatorAction;
    const EditorScene::CameraSequenceTriggerAction cameraAction =
        phase == engine::CollisionEvent::Phase::Exit
        ? action.exitCameraAction
        : action.enterCameraAction;

    if (moverAction != EditorScene::TriggerActionMode::None) {
        const bool hasMover = m_playRegistry->Has<engine::ecs::Mover>(action.target);
        bool shouldChange = false;
        const bool enableMover = TriggerActionShouldEnable(moverAction, hasMover, &shouldChange);
        if (shouldChange && !enableMover) {
            m_playRegistry->Remove<engine::ecs::Mover>(action.target);
            if (engine::ecs::RigidBody* body = m_playRegistry->TryGet<engine::ecs::RigidBody>(action.target)) {
                body->velocity = glm::vec3(0.0f);
            }
            m_log.Info("Trigger " + triggerName + " disabled Mover on " + targetName);
            PushPlayTriggerActionRow(triggerName, targetName, "Mover", false, phase);
        } else if (shouldChange && enableMover) {
            engine::ecs::Mover mover = action.mover;
            if (const Transform* transform = m_playRegistry->TryGet<Transform>(action.target)) {
                mover.origin = transform->position;
                mover.initialized = true;
            } else {
                mover.initialized = false;
            }
            m_playRegistry->Add<engine::ecs::Mover>(action.target, mover);
            m_log.Info("Trigger " + triggerName + " enabled Mover on " + targetName);
            PushPlayTriggerActionRow(triggerName, targetName, "Mover", true, phase);
        }
    }

    if (rotatorAction != EditorScene::TriggerActionMode::None) {
        const bool hasRotator = m_playRegistry->Has<engine::ecs::Rotator>(action.target);
        bool shouldChange = false;
        const bool enableRotator = TriggerActionShouldEnable(rotatorAction, hasRotator, &shouldChange);
        if (shouldChange && !enableRotator) {
            m_playRegistry->Remove<engine::ecs::Rotator>(action.target);
            if (engine::ecs::RigidBody* body = m_playRegistry->TryGet<engine::ecs::RigidBody>(action.target)) {
                body->angularVelocity = glm::vec3(0.0f);
            }
            m_log.Info("Trigger " + triggerName + " disabled Rotator on " + targetName);
        } else if (shouldChange && enableRotator) {
            m_playRegistry->Add<engine::ecs::Rotator>(action.target, action.rotator);
            m_log.Info("Trigger " + triggerName + " enabled Rotator on " + targetName);
        }
    }

    switch (cameraAction) {
    case EditorScene::CameraSequenceTriggerAction::Play:
        m_cameraDirector.Play(
            action.cameraSequenceName, action.cameraLockInput, action.cameraSkippable);
        m_log.Info("Trigger " + triggerName + " started camera sequence "
            + action.cameraSequenceName);
        break;
    case EditorScene::CameraSequenceTriggerAction::Stop:
        m_cameraDirector.Stop();
        break;
    case EditorScene::CameraSequenceTriggerAction::Skip:
        m_cameraDirector.Skip();
        break;
    case EditorScene::CameraSequenceTriggerAction::None:
    default:
        break;
    }

}

void EditorApp::PushPlayTriggerActionRow(const std::string& triggerName,
                                         const std::string& targetName,
                                         const std::string& componentName,
                                         bool enabled,
                                         engine::CollisionEvent::Phase phase) {
    EditorDockspace::PhysicsEventRow row;
    row.objectA = triggerName;
    row.objectB = targetName;
    row.phase = static_cast<int>(phase);
    row.trigger = true;
    row.action = true;
    row.text = std::string(CollisionPhaseName(phase))
        + " Action: "
        + triggerName
        + (enabled ? " enabled " : " disabled ")
        + componentName
        + " on "
        + targetName;
    m_physicsEventRows.push_back(row);
    ++m_physicsActionCount;
}

engine::ecs::Entity EditorApp::FindBestPlayLockTarget()
{
    if (!m_playRegistry || !m_playPlayerController) return engine::ecs::kNull;

    const glm::vec3 origin = m_playPlayerController->Position()
        + glm::vec3(0.0f, m_playPlayerController->camTargetHeight, 0.0f);
    const glm::vec3 forward = m_playPlayerController->LookDirection();
    const float range = std::max(m_playPlayerController->lockOnRange, 0.0f);
    const float maxAngle = std::clamp(
        m_playPlayerController->lockOnViewAngle, 0.0f, 180.0f);
    engine::ecs::Entity best = engine::ecs::kNull;
    float bestScore = std::numeric_limits<float>::max();

    m_playRegistry->view<engine::Health, Transform>().each(
        [&](engine::ecs::Entity entity, engine::Health& health, Transform& transform) {
            if (entity == m_playPlayerEntity || !health.alive || health.hp <= 0.0f) return;
            const glm::vec3 target = transform.position
                + glm::vec3(0.0f, m_playPlayerController->lockOnTargetHeight, 0.0f);
            const glm::vec3 delta = target - origin;
            const float distance = glm::length(delta);
            if (distance <= 0.0001f || distance > range) return;
            const float alignment = std::clamp(
                glm::dot(forward, delta / distance), -1.0f, 1.0f);
            const float angle = glm::degrees(std::acos(alignment));
            if (angle > maxAngle) return;
            const float score = distance + angle * 0.15f;
            if (score < bestScore) {
                best = entity;
                bestScore = score;
            }
        });
    return best;
}

void EditorApp::UpdatePlayLockOn(bool inputEnabled)
{
    if (!m_playRegistry || !m_playPlayerController
        || !m_playPlayerController->lockOnEnabled
        || m_playPlayerController->view != engine::PlayerController::View::ThirdPerson) {
        if (m_playPlayerController) m_playPlayerController->ClearLockOnTarget();
        m_playLockTarget = engine::ecs::kNull;
        m_playLockTogglePrev = false;
        return;
    }

    const bool toggleHeld = inputEnabled && GetWindow().IsKeyPressed(GLFW_KEY_T);
    if (toggleHeld && !m_playLockTogglePrev) {
        if (m_playLockTarget != engine::ecs::kNull) {
            m_playLockTarget = engine::ecs::kNull;
            m_log.Info("Camera lock-on released");
        } else {
            m_playLockTarget = FindBestPlayLockTarget();
            if (m_playLockTarget == engine::ecs::kNull) {
                m_log.Warning("Camera lock-on: no living target in range and view");
            } else {
                const auto name = m_playEntityNames.find(m_playLockTarget);
                m_log.Info("Camera locked on: "
                    + (name != m_playEntityNames.end() ? name->second : "target"));
            }
        }
    }
    m_playLockTogglePrev = toggleHeld;

    const Transform* targetTransform = m_playRegistry->TryGet<Transform>(m_playLockTarget);
    const engine::Health* targetHealth = m_playRegistry->TryGet<engine::Health>(m_playLockTarget);
    if (m_playLockTarget != engine::ecs::kNull
        && (!m_playRegistry->Valid(m_playLockTarget) || !targetTransform || !targetHealth
            || !targetHealth->alive || targetHealth->hp <= 0.0f)) {
        m_playLockTarget = engine::ecs::kNull;
        targetTransform = nullptr;
    }
    if (targetTransform) {
        const glm::vec3 target = targetTransform->position
            + glm::vec3(0.0f, m_playPlayerController->lockOnTargetHeight, 0.0f);
        const float distance = glm::length(target - m_playPlayerController->Position());
        if (distance > m_playPlayerController->lockOnRange * 1.25f) {
            m_playLockTarget = engine::ecs::kNull;
            m_playPlayerController->ClearLockOnTarget();
            m_log.Info("Camera lock-on released: target left range");
        } else {
            m_playPlayerController->SetLockOnTarget(target);
        }
    } else {
        m_playPlayerController->ClearLockOnTarget();
    }
}

void EditorApp::UpdatePlayPlayerController(float dt, bool inputEnabled)
{
    if (!m_playRegistry || !m_playPlayerController || m_playPlayerEntity == engine::ecs::kNull) {
        return;
    }

    engine::Window& window = GetWindow();
    engine::PlayerInput input;
    if (inputEnabled) {
        if (window.IsKeyPressed(GLFW_KEY_W)) input.moveForward += 1.0f;
        if (window.IsKeyPressed(GLFW_KEY_S)) input.moveForward -= 1.0f;
        if (window.IsKeyPressed(GLFW_KEY_D)) input.moveRight += 1.0f;
        if (window.IsKeyPressed(GLFW_KEY_A)) input.moveRight -= 1.0f;
        input.jump = window.IsKeyPressed(GLFW_KEY_SPACE);
        input.sprint = window.IsKeyPressed(GLFW_KEY_LEFT_SHIFT) || window.IsKeyPressed(GLFW_KEY_RIGHT_SHIFT);
        input.toggleView = window.IsKeyPressed(GLFW_KEY_V);
        input.toggleShoulder = window.IsKeyPressed(GLFW_KEY_Q);

        // With the cursor captured in play mode, mouse movement always drives the
        // camera (no need to hold RMB). Still honour the classic RMB/pinned look as
        // a fallback when the cursor has been freed (e.g. via ESC).
        const bool rightMouseDown = window.Native()
            && glfwGetMouseButton(window.Native(), GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
        if (m_playMouseCaptured || rightMouseDown || m_cameraController.MouseLookActive()) {
            input.lookYaw = window.MouseDeltaX();
            input.lookPitch = window.MouseDeltaY();
        }
    }

    UpdatePlayLockOn(inputEnabled);

    engine::AnimatedModel* animated =
        m_playRegistry->TryGet<engine::AnimatedModel>(m_playPlayerEntity);
    const bool movementLocked = animated && animated->BlocksMovement();
    if (animated) {
        const float moveMagnitude = std::min(glm::length(glm::vec2(input.moveForward, input.moveRight)), 1.0f);
        const float speed = movementLocked
            ? 0.0f
            : moveMagnitude * (input.sprint
                ? m_playPlayerController->runSpeed
                : m_playPlayerController->walkSpeed);
        animated->controller.SetParameter("Speed", speed);
    }

    m_playPlayerController->Update(*m_playRegistry, input, dt, !movementLocked);

    // Terrain floor: terrain is a mesh (no physics collider), so after the controller's
    // collider sweep, stand the capsule on the terrain surface when it's over one.
    {
        engine::CharacterController& body = m_playPlayerController->body;
        bool overTerrain = false;
        const float surfaceY = TerrainSurfaceY(body.position.x, body.position.z, overTerrain);
        if (overTerrain) {
            const float feet = body.position.y - body.height * 0.5f;
            if (feet <= surfaceY + 0.02f) {            // at/below the surface -> stand on it
                body.position.y = surfaceY + body.height * 0.5f;
                if (body.velocity.y < 0.0f) body.velocity.y = 0.0f;
                body.grounded = true;
            }
        }
    }

    // Water buoyancy: float the capsule on the wave surface, bobbing with the waves.
    // Runs after terrain so the player stands on whichever is higher (land vs water).
    {
        engine::CharacterController& body = m_playPlayerController->body;
        bool overWater = false;
        const float waterY = WaterSurfaceY(body.position.x, body.position.z, overWater);
        if (overWater) {
            const float feet = body.position.y - body.height * 0.5f;
            if (feet <= waterY) {                      // submerged -> buoy up to the surface
                body.position.y = waterY + body.height * 0.5f;
                if (body.velocity.y < 0.0f) body.velocity.y = 0.0f;
                body.grounded = true;
            }
        }
    }

    if (Transform* transform = m_playRegistry->TryGet<Transform>(m_playPlayerEntity)) {
        transform->position = m_playPlayerController->CapsulePosition();
        transform->rotation = m_playPlayerController->CapsuleRotation();
    }

    m_camera.SetPosition(m_playPlayerController->CameraPosition());
    m_camera.LookAt(m_playPlayerController->CameraTarget());
}

void EditorApp::ApplyManagedPlayCamera()
{
    const EditorScene::CameraPreset* preset = m_playCameraOverride
        ? &*m_playCameraOverride
        : m_scene.PrimaryCameraPreset();
    if (!preset || (!m_playCameraOverride && !preset->useInPlay)) return;

    engine::CameraPose pose;
    pose.position = preset->position;
    pose.target = preset->target;
    pose.fov = preset->fov;
    pose.nearPlane = preset->nearPlane;
    pose.farPlane = preset->farPlane;
    engine::CameraBlend::Apply(pose, m_camera);
}

void EditorApp::BeginCameraBlend(const EditorScene::CameraPreset& preset)
{
    engine::CameraPose target;
    target.position = preset.position;
    target.target = preset.target;
    target.fov = preset.fov;
    target.nearPlane = preset.nearPlane;
    target.farPlane = preset.farPlane;
    const auto easing = static_cast<engine::CameraBlend::Easing>(
        std::clamp(preset.blendEasing, 0, 3));
    m_cameraBlend.Start(engine::CameraBlend::FromCamera(m_camera),
                        target, preset.blendDuration, easing);
    engine::CameraBlend::Apply(m_cameraBlend.Current(), m_camera);
}

void EditorApp::UpdateCameraBlend(float dt)
{
    engine::CameraBlend::Apply(m_cameraBlend.Update(dt), m_camera);
}

void EditorApp::RestoreCameraBeforeShake()
{
    if (!m_cameraBeforeShake) return;
    engine::CameraBlend::Apply(*m_cameraBeforeShake, m_camera);
    m_cameraBeforeShake.reset();
}

void EditorApp::UpdateCameraShake(float dt)
{
    if (!m_cameraShake.Active()) return;
    m_cameraBeforeShake = engine::CameraBlend::FromCamera(m_camera);
    engine::CameraShake::Apply(m_cameraShake.Update(dt), m_camera);
}

void EditorApp::StartCameraSequence(const EditorScene::CameraSequence& sequence,
                                    bool lockInput, bool skippable)
{
    RestoreCameraBeforeShake();
    std::vector<engine::CameraSequenceShot> shots;
    shots.reserve(sequence.shots.size());
    for (const EditorScene::CameraSequenceShot& source : sequence.shots) {
        const auto preset = std::find_if(
            m_scene.CameraPresets().begin(), m_scene.CameraPresets().end(),
            [&](const EditorScene::CameraPreset& camera) {
                return camera.name == source.cameraName;
            });
        if (preset == m_scene.CameraPresets().end()) {
            m_log.Warning("Camera sequence '" + sequence.name
                + "' skipped missing camera: " + source.cameraName);
            continue;
        }
        engine::CameraSequenceShot shot;
        shot.pose.position = preset->position;
        shot.pose.target = preset->target;
        shot.pose.fov = preset->fov;
        shot.pose.nearPlane = preset->nearPlane;
        shot.pose.farPlane = preset->farPlane;
        shot.travelDuration = std::max(source.travelDuration, 0.0f);
        shot.holdDuration = std::max(source.holdDuration, 0.0f);
        shot.easing = static_cast<engine::CameraBlend::Easing>(
            std::clamp(source.easing, 0, 3));
        shot.path = static_cast<engine::CameraSequenceShot::Path>(
            std::clamp(source.pathMode, 0, 1));
        shot.eventName = source.eventName;
        shots.push_back(shot);
    }
    if (shots.empty()) {
        m_log.Warning("Camera sequence has no valid shots: " + sequence.name);
        return;
    }
    m_cameraBlend.Cancel();
    m_cameraSequence.Start(
        engine::CameraBlend::FromCamera(m_camera), std::move(shots), sequence.loop);
    m_activeCinematicCues = sequence.cues;
    std::sort(m_activeCinematicCues.begin(), m_activeCinematicCues.end(),
              [](const auto& a, const auto& b) { return a.time < b.time; });
    m_cameraSequencePaused = false;
    m_cameraDirector.SetPlaying(sequence.name, lockInput, skippable);
    m_log.Info("Camera sequence started: " + sequence.name);
}

void EditorApp::UpdateCameraSequence(float dt)
{
    if (m_cameraSequencePaused) {
        engine::CameraBlend::Apply(m_cameraSequence.Current(), m_camera);
        return;
    }
    const bool wasActive = m_cameraSequence.Active();
    const std::string sequenceName = m_cameraDirector.ActiveName();
    const float previousTime = m_cameraSequence.Time();
    engine::CameraBlend::Apply(m_cameraSequence.Update(dt), m_camera);
    const float currentTime = m_cameraSequence.Time();
    ExecuteCinematicCues(previousTime, currentTime, currentTime < previousTime);
    for (const std::string& eventName : m_cameraSequence.TakeEvents()) {
        m_cameraDirector.NotifyTimelineEvent(sequenceName, eventName);
        m_log.Info("Camera sequence event: " + eventName);
    }
    if (wasActive && !m_cameraSequence.Active()) {
        m_cameraDirector.NotifyFinished(sequenceName, false);
        m_activeCinematicCues.clear();
        m_log.Info("Camera sequence finished: " + sequenceName);
    }
}

void EditorApp::SkipActiveCameraSequence()
{
    if (!m_cameraSequence.Active() || !m_cameraDirector.Skippable()) return;
    const std::string sequenceName = m_cameraDirector.ActiveName();
    engine::CameraBlend::Apply(m_cameraSequence.SkipToEnd(), m_camera);
    m_cameraDirector.NotifyFinished(sequenceName, true);
    m_activeCinematicCues.clear();
    m_cameraSequencePaused = false;
    m_log.Info("Camera sequence skipped: " + sequenceName);
}

void EditorApp::ProcessCameraDirectorCommands()
{
    for (const engine::CameraSequenceCommand& command : m_cameraDirector.TakeCommands()) {
        if (command.type == engine::CameraSequenceCommand::Type::Stop) {
            m_cameraSequence.Stop();
            m_cameraDirector.SetStopped();
            m_activeCinematicCues.clear();
            m_cameraSequencePaused = false;
            m_log.Info("Camera sequence stopped");
            continue;
        }
        if (command.type == engine::CameraSequenceCommand::Type::Skip) {
            SkipActiveCameraSequence();
            continue;
        }
        const auto sequence = std::find_if(
            m_scene.CameraSequences().begin(), m_scene.CameraSequences().end(),
            [&](const EditorScene::CameraSequence& candidate) {
                return candidate.name == command.name;
            });
        if (sequence == m_scene.CameraSequences().end()) {
            m_log.Warning("Camera sequence was not found: " + command.name);
            continue;
        }
        StartCameraSequence(*sequence, command.lockInput, command.skippable);
    }
}

void EditorApp::ExecuteCinematicCues(float previousTime, float currentTime, bool wrapped)
{
    for (const EditorScene::CinematicCue& cue : m_activeCinematicCues) {
        const bool crossed = wrapped
            ? (cue.time > previousTime || cue.time <= currentTime)
            : ((cue.time > previousTime && cue.time <= currentTime)
               || (previousTime == 0.0f && cue.time == 0.0f && currentTime > 0.0f));
        if (crossed) ExecuteCinematicCue(cue);
    }
}

void EditorApp::ExecuteCinematicCue(const EditorScene::CinematicCue& cue)
{
    switch (cue.type) {
    case EditorScene::CinematicCueType::Event:
        m_cameraDirector.NotifyTimelineEvent(
            m_cameraDirector.ActiveName(), cue.name);
        if (!cue.name.empty()) m_log.Info("Cinematic event: " + cue.name);
        break;
    case EditorScene::CinematicCueType::Audio:
        if (!cue.assetPath.empty()) {
            m_audio.Play(cue.assetPath, 1.0f, std::max(cue.volume, 0.0f),
                         engine::AudioBus::SFX);
            m_log.Info("Cinematic audio: " + cue.assetPath);
        }
        break;
    case EditorScene::CinematicCueType::Animation: {
        if (!m_playRegistry) {
            m_log.Info("Animation cues execute in Play mode.");
            break;
        }
        engine::ecs::Entity target = engine::ecs::kNull;
        for (const auto& pair : m_playEntityNames) {
            if (pair.second == cue.targetObject) {
                target = pair.first;
                break;
            }
        }
        engine::AnimatedModel* animated = target == engine::ecs::kNull
            ? nullptr : m_playRegistry->TryGet<engine::AnimatedModel>(target);
        if (!animated || !animated->model) {
            m_log.Warning("Cinematic animation target is unavailable: " + cue.targetObject);
            break;
        }
        int clip = -1;
        const auto& animations = animated->model->Animations();
        for (std::size_t i = 0; i < animations.size(); ++i) {
            if (animations[i].name == cue.animationClip) {
                clip = static_cast<int>(i);
                break;
            }
        }
        if (clip < 0) {
            m_log.Warning("Cinematic animation clip was not found: " + cue.animationClip);
            break;
        }
        animated->PlayAction(clip);
        m_log.Info("Cinematic animation: " + cue.targetObject + " / " + cue.animationClip);
        break;
    }
    }
}

engine::ScriptInputState EditorApp::CapturePlayScriptInput(
    bool inputEnabled, bool includeFrameEdges)
{
    engine::ScriptInputState input;
    input.enabled = inputEnabled;
    input.physicsEvents = &m_playPhysics.Events();
    input.animationEvents = &m_playAnimationEvents;

    engine::Window& window = GetWindow();
    for (int key = GLFW_KEY_SPACE; key <= GLFW_KEY_LAST; ++key) {
        const bool down = window.IsKeyPressed(key);
        const bool wasDown = m_scriptKeyPrev[key];
        if (includeFrameEdges) {
            m_scriptKeyPrev[key] = down;
        }
        if (!inputEnabled) {
            continue;
        }
        if (down) {
            input.keysDown.insert(key);
        }
        if (includeFrameEdges && down && !wasDown) {
            input.keysPressed.insert(key);
        }
    }

    if (window.Native()) {
        for (int button = GLFW_MOUSE_BUTTON_1; button <= GLFW_MOUSE_BUTTON_LAST; ++button) {
            const bool down = glfwGetMouseButton(window.Native(), button) == GLFW_PRESS;
            const bool wasDown = m_scriptMousePrev[button];
            if (includeFrameEdges) {
                m_scriptMousePrev[button] = down;
            }
            if (!inputEnabled) {
                continue;
            }
            if (down) {
                input.mouseButtonsDown.insert(button);
            }
            if (includeFrameEdges && down && !wasDown) {
                input.mouseButtonsPressed.insert(button);
            }
        }
    }

    if (inputEnabled && includeFrameEdges) {
        input.mouseDeltaX = window.MouseDeltaX();
        input.mouseDeltaY = window.MouseDeltaY();
    }
    return input;
}

void EditorApp::StepPlayPhysics(float dt, bool inputEnabled)
{
    m_physicsStepsLastFrame = 0;
    m_physicsEventEnterCount = 0;
    m_physicsEventStayCount = 0;
    m_physicsEventExitCount = 0;
    m_physicsActionCount = 0;
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
        const engine::ScriptInputState scriptInput =
            CapturePlayScriptInput(inputEnabled, false);
        UpdatePlayPlayerController(step, inputEnabled);
        engine::FixedUpdateScripts(
            *m_playRegistry, step, &scriptInput, &m_runtimeAudio,
            &m_cameraShake, &m_cameraDirector);
        engine::ecs::UpdateGameplay(*m_playRegistry, step);
        engine::UpdateHealth(*m_playRegistry);
        engine::ecs::UpdateRuntimeMotion(*m_playRegistry, step);
        m_playAnimationEvents.clear();
        UpdateAI(step);
        engine::UpdateAnimations(*m_playRegistry, step);
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
    engine::ScriptInputState scriptInput;
    bool scriptInputCaptured = false;
    constexpr int kMaxPhysicsStepsPerFrame = 5;
    while (m_physicsAccumulator >= step && m_physicsStepsLastFrame < kMaxPhysicsStepsPerFrame) {
        if (!scriptInputCaptured) {
            scriptInput = CapturePlayScriptInput(inputEnabled, false);
            scriptInputCaptured = true;
        }
        UpdatePlayPlayerController(step, inputEnabled);
        engine::FixedUpdateScripts(
            *m_playRegistry, step, &scriptInput, &m_runtimeAudio,
            &m_cameraShake, &m_cameraDirector);
        engine::ecs::UpdateGameplay(*m_playRegistry, step);
        engine::UpdateHealth(*m_playRegistry);
        engine::ecs::UpdateRuntimeMotion(*m_playRegistry, step);
        UpdateAI(step);
        engine::UpdateAnimations(*m_playRegistry, step);
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

    if (m_playRegistry) {
        m_runtimeAudio.ProcessCollisionEvents(*m_playRegistry, m_playPhysics.Events());
        engine::ProcessParticleCollisionEvents(*m_playRegistry, m_playPhysics.Events());
    }

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
        if (event.trigger
            && (event.phase == engine::CollisionEvent::Phase::Enter
                || event.phase == engine::CollisionEvent::Phase::Exit)) {
            ApplyPlayTriggerAction(event.a, event.b, event.phase);
            ApplyPlayTriggerAction(event.b, event.a, event.phase);
            ApplyPlayCameraZoneEvent(event.a, event.b, event.phase);
            ApplyPlayCameraZoneEvent(event.b, event.a, event.phase);
        }

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
                guide.objectA = row.objectA;
                guide.objectB = row.objectB;
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
