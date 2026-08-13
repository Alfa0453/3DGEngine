#include "EditorApp.h"

#include <glad/glad.h>
#include <engine/ecs/Registry.h>
#include <engine/ecs/RuntimeSystems.h>
#include <engine/gameplay/GameplayComponents.h>
#include <engine/gameplay/GameplaySystems.h>
#include <engine/gameplay/RagdollSystem.h>
#include <engine/gameplay/GameMode.h>
#include <engine/gameplay/Script.h>
#include <engine/gameplay/SaveGame.h>
#include <engine/assets/SkeletalAsset.h>
#include <engine/assets/TextureAsset.h>
#include <engine/ecs/Systems.h>
#include <engine/graphics/Model.h>
#include <engine/graphics/Primitives.h>
#include <engine/graphics/Texture.h>
#include <engine/animation/Animator.h>
#include <engine/ai/NavMeshBuilder.h>
#include <engine/ai/BtScript.h>
#include <engine/ai/AgentCollision.h>
#include <engine/ai/AiMovement.h>
#include <engine/graphics/ImageDecode.h>
#include <engine/graphics/GrassField.h>
#include <engine/math/Spline.h>
#include <engine/assets/MaterialAssetLoader.h>
#include <engine/assets/ShaderAsset.h>
#include <engine/assets/ShaderGraphCompiler.h>
#include <engine/assets/FoliageAsset.h>
#include <engine/assets/CaveAsset.h>

#include "GameBtScripts.h"
#include "EditorBranding.h"
#include "EditorScriptTools.h"
#include "EditorGeneratedScriptTools.h"
#include "AnimationGraphBuilder.h"
#include "NativeDialog.h"
#include <game/GameModule.h>
#include "ParticleAsset.h"

#include <GLFW/glfw3.h>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <imgui.h>

#include <algorithm>
#include <limits>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <system_error>
#include <unordered_set>
#include <utility>

using engine::ecs::Entity;
using engine::ecs::MeshRenderer;
using engine::ecs::Transform;

namespace {

EditorAssets::Type EditorAssetTypeFor(engine::AssetType type) {
    using A = engine::AssetType;
    using E = EditorAssets::Type;
    switch (type) {
    case A::StaticMesh: return E::Model;
    case A::SkeletalMesh: return E::SkeletalModel;
    case A::Skeleton: return E::Skeleton;
    case A::Animation: return E::Animation;
    case A::Material: return E::Material;
    case A::Texture: return E::Texture;
    case A::Audio: return E::Audio;
    case A::Shader: return E::Shader;
    case A::Particle: return E::Particle;
    case A::ParticleEffect: return E::ParticleEffect;
    case A::Hud: return E::Hud;
    case A::Character: return E::Character;
    case A::AnimationClip: return E::AnimationClip;
    case A::AnimationGraph: return E::AnimationGraph;
    case A::BehaviorTree: return E::BehaviorGraph;
    case A::Scene: return E::Scene;
    case A::Script: return E::Script;
    case A::Terrain: return E::Terrain;
    case A::World: return E::World;
    case A::Foliage: return E::Foliage;
    case A::Ragdoll: return E::Ragdoll;
    case A::AnimationRetarget: return E::AnimationRetarget;
    case A::Ability: return E::Ability;
    case A::Prefab: return E::Prefab;
    case A::Weather: return E::Weather;
    case A::Building: return E::Building;
    case A::Road: return E::Road;
    case A::ScatterGraph: return E::ScatterGraph;
    case A::Biome: return E::Biome;
    case A::DayNightTimeline: return E::DayNightTimeline;
    case A::Cave: return E::Cave;
    case A::Font:
    case A::Unknown: return E::Other;
    }
    return E::Other;
}

engine::WindowProps MakeEditorWindowProps(const engine::Config& config) {
    engine::WindowProps props;
    props.title = "3DG Editor";
    props.width = config.GetInt("window.width", 1440);
    props.height = config.GetInt("window.height", 900);
    props.vsync = config.GetBool("window.vsync", true);
    return props;
}

struct MaterialForgeDeploySignal {
    std::string token;
    std::string operation;
    std::string editorAction = "refresh";
    std::string materialId;
    std::string materialPath;
};

bool ReadMaterialForgeDeploySignal(
    const std::filesystem::path& path,
    MaterialForgeDeploySignal* signal) {
    std::ifstream input(path, std::ios::binary);
    std::string magic;
    unsigned version = 0;
    if (!(input >> magic >> version)
        || magic != "3DG_MATERIAL_FORGE_DEPLOY"
        || version < 1 || version > 2) {
        return false;
    }
    input.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    MaterialForgeDeploySignal parsed;
    if (!std::getline(input, parsed.token)
        || !std::getline(input, parsed.operation)) {
        return false;
    }
    if (version == 1) {
        if (!std::getline(input, parsed.materialPath)) return false;
    } else if (!std::getline(input, parsed.editorAction)
        || !std::getline(input, parsed.materialId)
        || !std::getline(input, parsed.materialPath)) {
        return false;
    }
    if (parsed.token.empty() || parsed.materialPath.empty()
        || (parsed.operation != "created" && parsed.operation != "updated")
        || (parsed.editorAction != "refresh"
            && parsed.editorAction != "apply_selected")) {
        return false;
    }
    *signal = std::move(parsed);
    return true;
}

std::string MaterialForgeSingleLine(std::string value) {
    std::replace(value.begin(), value.end(), '\r', ' ');
    std::replace(value.begin(), value.end(), '\n', ' ');
    return value;
}

bool WriteMaterialForgeAcknowledgement(
    const std::filesystem::path& signalPath,
    const MaterialForgeDeploySignal& signal,
    const std::string& status,
    int appliedCount,
    const std::string& message,
    std::string* error) {
    const std::filesystem::path acknowledgementPath =
        signalPath.parent_path() / "deploy.ack";
    const std::filesystem::path temporary =
        acknowledgementPath.string() + ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            *error = "Could not write Material Forge editor acknowledgement";
            return false;
        }
        output << "3DG_MATERIAL_FORGE_ACK 1\n"
               << MaterialForgeSingleLine(signal.token) << '\n'
               << MaterialForgeSingleLine(status) << '\n'
               << std::max(0, appliedCount) << '\n'
               << MaterialForgeSingleLine(message) << '\n'
               << MaterialForgeSingleLine(signal.materialPath) << '\n';
        output.close();
        if (!output) {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            *error = "Could not complete Material Forge editor acknowledgement";
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::remove(acknowledgementPath, ec);
    ec.clear();
    std::filesystem::rename(temporary, acknowledgementPath, ec);
    if (ec) {
        const std::string messageText = ec.message();
        std::filesystem::remove(temporary, ec);
        *error = "Could not commit Material Forge editor acknowledgement: "
            + messageText;
        return false;
    }
    return true;
}

bool IsSafeMaterialForgeApplyPath(const std::filesystem::path& path,
                                  const std::filesystem::path& content) {
    std::error_code ec;
    const std::filesystem::path canonicalContent =
        std::filesystem::weakly_canonical(content, ec);
    if (ec) return false;
    ec.clear();
    const std::filesystem::path canonicalMaterial =
        std::filesystem::weakly_canonical(path, ec);
    if (ec || canonicalMaterial.extension() != ".3dgmat"
        || !std::filesystem::is_regular_file(canonicalMaterial, ec)) {
        return false;
    }
    ec.clear();
    const std::filesystem::path relative =
        std::filesystem::relative(canonicalMaterial, canonicalContent, ec);
    if (ec || relative.empty() || relative.is_absolute()) return false;
    return std::none_of(relative.begin(), relative.end(),
        [](const std::filesystem::path& part) { return part == ".."; });
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

engine::ProceduralSky::CloudSettings SkyClouds(const EditorScene::Environment& environment) {
    engine::ProceduralSky::CloudSettings clouds;
    clouds.enabled = environment.clouds;
    clouds.coverage = environment.cloudCoverage;
    clouds.density = environment.cloudDensity;
    clouds.scale = environment.cloudScale;
    clouds.softness = environment.cloudSoftness;
    clouds.windSpeed = environment.cloudWindSpeed;
    clouds.windDirectionDegrees = environment.cloudWindDirection;
    clouds.horizonHeight = environment.cloudHorizonHeight;
    clouds.color = environment.cloudColor;
    return clouds;
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

// Defined here (not defaulted in the header) so the unique_ptr<GrassField> members in
// m_grass are destroyed where engine::GrassField is a complete type.
EditorApp::~EditorApp() = default;

void EditorApp::OnInit()
{
    m_autoCompileScripts = m_config.GetBool("scripting.auto_compile_on_save", true);
    const editor::branding::WindowIcon& editorIcon = editor::branding::Icon();
    GetWindow().SetIcon(editorIcon.width, editorIcon.height, editorIcon.rgba.data());

    m_renderer.Init();
    m_renderer.SetClearColor({0.08f, 0.09f, 0.11f, 1.0f});

    // Surface script crashes (a script that threw and was auto-disabled) in the Console
    // instead of only stderr, so failures are visible while iterating.
    engine::SetScriptErrorHandler([this](const std::string& message) {
        m_log.Error("Script: " + message);
    });

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
                vec3 linearColor = max(
                    ambient + diffuse + specular + emissiveColor, vec3(0.0));
                // Imported models share the same linear material values as the
                // PBR renderer. Convert to display space here as well; otherwise
                // dark but valid materials appear almost black.
                vec3 mapped = linearColor / (linearColor + vec3(1.0));
                mapped = pow(mapped, vec3(1.0 / 2.2));
                FragColor = vec4(mapped, 1.0);
            }
        )glsl"
    );
    m_pbrRenderer.emplace();
    m_foliageRenderer.emplace();
    m_particleRenderer.emplace();
    m_skinnedRenderer.emplace();
    m_sky.emplace();
    m_text.emplace();

    {
        // If a project was opened last time, reopen it; otherwise fall back to the
        // legacy single project stored directly in editor.cfg.
        const std::string currentProject = m_config.GetString("editor.current_project", "");
        std::error_code projEc;
        if (!currentProject.empty() && std::filesystem::is_regular_file(currentProject, projEc)) {
            std::string projErr;
            if (m_project.OpenProjectFile(currentProject, m_projectConfig, &projErr)) {
                m_hasProjectFile = true;
                m_log.Info("Opened project: " + m_project.ProjectName() + " (" + currentProject + ")");
            } else {
                m_log.Error("Could not open project '" + currentProject + "': " + projErr);
                m_project.Load(m_config);
            }
        } else {
            m_project.Load(m_config);
        }
    }
    LoadPackagingSettings();
    SetScenePathDraft(m_project.ScenePath());
    m_content.Refresh(m_assets, m_project, m_log);
    LoadProjectAssetRegistry();
    // Drag-and-drop from the OS file explorer: opens an Import Settings popup listing each
    // dropped file with its detected type; the actual import runs when the user confirms.
    GetWindow().SetDropCallback([this](const std::vector<std::string>& paths) {
        BeginImportDialog(paths);
    });
    m_materialMaker.SetOutputDirectory(m_project.AssetRoot());
    m_behaviorGraph.SetOutputDirectory(m_project.AssetRoot());
    // Rebuild the shared game module's generated script header from ONLY this project's
    // list, so scripts authored in other projects are not compiled into this one.
    {
        std::string regenError;
        if (!EditorGeneratedScriptTools::RegenerateGeneratedScripts(
                m_project.AssetRoot(), &regenError)) {
            m_log.Info("Project scripts: " + regenError);
        }
    }
    engine::ai::RegisterExampleBtScripts();   // built-in example scripts (idempotent)
    RegisterGameBtScripts();                  // legacy: editor/src/GameBtScripts.cpp
    RegisterGameModule();                     // engine/manual scripts
    LoadProjectScriptModule(false);           // active project's compiled native scripts
    {
        std::error_code ec;
        const std::filesystem::path projectRoot =
            std::filesystem::absolute(m_project.AssetRoot(), ec).parent_path();
        const std::string scriptBuildStatus =
            EditorScriptTools::ReadLastBuildStatus(projectRoot);
        if (scriptBuildStatus.rfind("success", 0) == 0) {
            m_log.Info("Script compilation completed successfully");
        } else if (scriptBuildStatus.rfind("failed", 0) == 0) {
            m_log.Error("Script compilation failed. Open the Script Editor to view the build log.");
        }
        std::filesystem::remove(
            projectRoot / "Intermediate" / "Scripts" / "script_compile.status", ec);
    }
    bool restoredLastScene = false;
    std::error_code savedSceneError;
    if (m_project.HasLastSavedScene()
        && std::filesystem::is_regular_file(m_project.LastSavedScenePath(), savedSceneError)) {
        restoredLastScene = m_runtime.LoadScene(
            m_scene, m_project, *m_cube, *m_plane, *m_sphere, *m_capsule,
            *m_cylinder, *m_cone, *m_pyramid, *m_torus, *m_staircase, m_log);
        if (restoredLastScene) {
            m_project.MarkCurrentSceneSaved();
            m_project.AddRecentScene(m_project.ScenePath());
            PersistProject();
            SyncHudFromScene();
            m_log.Info("Restored last saved scene: " + m_project.ScenePath());
        }
    }
    if (!restoredLastScene) {
        m_scene.BuildDefault(*m_cube, *m_plane, *m_sphere, *m_capsule, *m_cylinder,
            *m_cone, *m_pyramid, *m_torus, *m_staircase);
        if (m_project.HasLastSavedScene()) {
            m_log.Warning("Last saved scene was unavailable; opened the default scene instead");
        } else if (m_project.HasProjectFile()) {
            // Launcher-created projects begin with an empty project file. Save
            // their generated starter level immediately, matching the in-editor
            // Create Project workflow.
            SaveScene();
        }
    }
    m_imgui.Init(GetWindow());
}

void EditorApp::OnUpdate(float dt)
{
    UpdatePackageBuild();
    UpdateScriptAutoReload(dt);
    UpdateMaterialForgeDeployments(dt);
    if (m_mode == EditorMode::Play) {
        m_playPhysics.SetDebugTracing(m_showGameplayTraces);
        m_playPhysics.ClearDebugTraces();
    }
    // A script requested a scene change during Play (deferred to a safe point so we don't
    // tear down the play registry mid-update). Exit Play, load the target scene, and resume
    // Play in it if the load succeeded -- mirroring the packaged runtime's scene transition.
    if (!m_playSceneLoadRequest.empty()) {
        const std::string requested = m_playSceneLoadRequest;
        m_playSceneLoadRequest.clear();
        if (m_mode == EditorMode::Play) ExitPlayMode();
        const std::string before = m_project.ScenePath();
        RequestLoadSceneFromPath(requested);
        if (m_project.ScenePath() != before) {
            m_log.Info("Script scene load: " + requested);
            EnterPlayMode();   // continue playing in the new scene
        }
        return;
    }

    engine::Window& window = GetWindow();
    RestoreCameraBeforeShake();
    m_elapsed += dt;
    m_dt = dt;
    float playDt = dt;
    if (m_mode == EditorMode::Play) {
        engine::GameMode::Instance().UpdateUnscaledTime(dt);
        playDt = engine::GameMode::Instance().ScaleDelta(dt);
    }
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

    const bool playInputEnabled =
        m_scene.GetGameModeSettings().playerInputEnabled
        && !keyboardCaptured
        && !m_cameraDirector.InputLocked();
    if (m_mode == EditorMode::Play && m_playRegistry
        && !m_physicsPaused
        && engine::GameMode::Instance().IsPlaying()) {
        const engine::ScriptInputState scriptInput =
            CapturePlayScriptInput(playInputEnabled, true);
        engine::UpdateScripts(
            *m_playRegistry, playDt, &scriptInput, &m_runtimeAudio,
            &m_cameraShake, &m_cameraDirector, &engine::GameMode::Instance(),
            &m_playPhysics);
        auto& dayNight = engine::DayNightTimelineRuntime::Instance();
        dayNight.Tick(playDt);
        if (dayNight.Loaded()) {
            const auto key=dayNight.Sample();auto environment=m_scene.GetEnvironment();
            environment.timeOfDay=dayNight.Time();environment.skyIntensity=key.skyIntensity;
            environment.skyLightIntensity=key.skyLightIntensity;environment.sunIntensity=key.sunIntensity;
            environment.cloudCoverage=key.cloudCoverage;environment.cloudDensity=key.cloudDensity;
            environment.cloudWindSpeed=key.cloudWindSpeed;environment.cloudWindDirection=key.cloudWindDirection;
            environment.cloudColor=key.cloudColor;environment.fogDensity=key.fogDensity;
            environment.fogHeight=key.fogHeight;environment.fogHeightFalloff=key.fogHeightFalloff;
            environment.fogColor=key.fogColor;m_scene.SetEnvironment(environment);
        }
        // Full game save/load requests. In the editor a load restores state onto the
        // current play scene (matching entities by name) rather than reloading a scene.
        for (const engine::ScriptSaveGameRequest& request
             : engine::ConsumeScriptSaveGameRequests()) {
            if (request.load) {
                engine::SaveGame save;
                if (engine::SaveGame::LoadFromFile(engine::SaveSlotPath(request.slot), save)) {
                    engine::ApplySaveGame(*m_playRegistry, save);
                    m_log.Info("Loaded save slot " + std::to_string(request.slot));
                }
            } else {
                engine::SaveGame save = engine::CaptureSaveGame(
                    *m_playRegistry, m_project.ScenePath(),
                    request.displayName.empty() ? "Slot " + std::to_string(request.slot)
                                                : request.displayName,
                    0.0f);
                if (save.SaveToFile(engine::SaveSlotPath(request.slot)))
                    m_log.Info("Saved to slot " + std::to_string(request.slot));
            }
        }
        // Scene transition requested from a script: defer to the top of the next frame
        // (handled above) so we don't reload while the play registry is mid-update. Keep
        // the last request if several are queued.
        if (std::string requestedScene = engine::ConsumeScriptSceneLoadRequest();
            !requestedScene.empty()) {
            m_playSceneLoadRequest = std::move(requestedScene);
        }
        // Level streaming is a packaged-world feature; the editor Play preview builds a
        // single in-memory scene, so surface a one-time note instead of silently dropping.
        for (const engine::ScriptLevelStreamRequest& request
             : engine::ConsumeScriptLevelStreamRequests()) {
            if (!m_warnedEditorLevelStreaming) {
                m_log.Warning("Level streaming ('" + request.level
                    + "') runs in packaged worlds; ignored in editor Play. Use Play in a "
                      "packaged build to test streaming.");
                m_warnedEditorLevelStreaming = true;
            }
        }
        // Camera events are frame events. Every script has now had one opportunity
        // to observe them; fixed updates receive held input and simulation events.
        m_cameraDirector.ClearEvents();
    }
    // A script may have started a hit stop above. Re-evaluate before advancing
    // the remaining world systems so the impact freezes this same frame.
    if (m_mode == EditorMode::Play)
        playDt = engine::GameMode::Instance().ScaleDelta(dt);
    StepPlayPhysics(playDt, playInputEnabled);
    ProcessCameraDirectorCommands();
    if (m_mode == EditorMode::Play) {
        if (m_cameraBlend.Active()) UpdateCameraBlend(playDt);
        else ApplyManagedPlayCamera();
    }
    if (m_mode == EditorMode::Play && m_playRegistry)
        engine::UpdateParticleSystems(*m_playRegistry, playDt);
    else if (m_mode == EditorMode::Edit)
        UpdateEditParticlePreviews(dt);
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
    if (m_foliagePaint) {
        HandleFoliagePaint();
    } else if (m_terrainSculpt) {
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
        UpdateCameraSequence(
            m_mode == EditorMode::Play ? playDt : dt);
    }
    // Keep free and gameplay cameras above the terrain. Rising terrain pushes the
    // camera out immediately; moving downhill releases smoothly to avoid a snap.
    if (!m_cameraBlend.Active() && !m_cameraSequence.Active()) {
        bool overTerrain = false;
        const glm::vec3 desired = m_camera.Position();
        const float surfaceY = TerrainSurfaceY(desired.x, desired.z, overTerrain);
        m_camera.SetPosition(m_terrainCameraConstraint.Resolve(
            desired, surfaceY, overTerrain,
            m_mode == EditorMode::Play ? playDt : dt));
    } else {
        m_terrainCameraConstraint.Reset();
    }
    UpdateCameraShake(m_mode == EditorMode::Play ? playDt : dt);
    m_audio.SetListener(m_camera.Position(), m_camera.Front());
    
    m_transformController.UpdateKeyboardShortcuts(window,
        m_scene,
        m_gizmo,
        m_mode == EditorMode::Edit && !keyboardCaptured && !m_cameraController.MouseLookActive() && !controlDown,
        dt);
}

void EditorApp::UpdateMaterialForgeDeployments(float dt) {
    m_materialForgeDeployPoll -= dt;
    if (m_materialForgeDeployPoll > 0.0f) return;
    m_materialForgeDeployPoll = 0.35f;

    std::error_code ec;
    const std::filesystem::path content =
        std::filesystem::absolute(m_project.AssetRoot(), ec).lexically_normal();
    if (ec) return;
    const std::filesystem::path signalPath =
        content.parent_path() / "Intermediate" / "MaterialForge" / "deploy.signal";
    const std::string signalPathString = signalPath.generic_string();
    MaterialForgeDeploySignal signal;

    if (signalPathString != m_materialForgeDeploySignalPath) {
        m_materialForgeDeploySignalPath = signalPathString;
        m_materialForgeDeployToken.clear();
        if (ReadMaterialForgeDeploySignal(signalPath, &signal))
            m_materialForgeDeployToken = signal.token;
        return;
    }
    if (!ReadMaterialForgeDeploySignal(signalPath, &signal)
        || signal.token == m_materialForgeDeployToken) {
        return;
    }
    m_materialForgeDeployToken = signal.token;

    LoadProjectAssetRegistry();
    m_content.Refresh(m_assets, m_project, m_log);
    m_editAssets.Clear();
    ClearEditParticlePreviews();
    m_editTextureLoadErrors.clear();
    m_editModelLoadErrors.clear();
    m_terrainMaterialSurfaces.clear();

    if (m_mode == EditorMode::Play && m_playAssets && m_playRegistry) {
        m_playAssets->Clear();
        const engine::RuntimeAssetManager::ResolveReport report =
            m_playAssets->ResolveRegistryAssets(*m_playRegistry);
        for (const std::string& reloadError : report.errors)
            m_log.Warning("Material Forge live refresh: " + reloadError);
    }

    std::string acknowledgementStatus = "refreshed";
    std::string acknowledgementMessage =
        "3DGEditor refreshed Content and runtime material caches";
    int appliedCount = 0;
    if (signal.editorAction == "apply_selected") {
        engine::AssetHandle materialId;
        bool validMaterial =
            engine::AssetHandle::Parse(signal.materialId, &materialId)
            && IsSafeMaterialForgeApplyPath(signal.materialPath, content);
        if (validMaterial) {
            ec.clear();
            const std::filesystem::path relativeMaterial =
                std::filesystem::relative(signal.materialPath, content, ec);
            const engine::AssetRegistryEntry* registered =
                ec ? nullptr : m_assetRegistry.Find(materialId);
            const std::string expectedVirtualPath = ec ? std::string{}
                : engine::AssetRegistry::NormalizeVirtualPath(
                    relativeMaterial.generic_string());
            validMaterial = registered
                && registered->type == engine::AssetType::Material
                && registered->virtualPath == expectedVirtualPath;
        }
        if (!validMaterial) {
            acknowledgementStatus = "rejected";
            acknowledgementMessage =
                "3DGEditor rejected an invalid or unregistered material apply request";
            m_log.Warning("Material Forge apply request was invalid and was ignored");
        } else if ((appliedCount = m_scene.SetSelectedMaterialAssetToSelection(
                        signal.materialPath, materialId)) > 0) {
            acknowledgementStatus =
                m_mode == EditorMode::Play ? "deferred" : "applied";
            const std::string objectText = std::to_string(appliedCount)
                + (appliedCount == 1 ? " selected object" : " selected objects");
            acknowledgementMessage = m_mode == EditorMode::Play
                ? "Applied to " + objectText
                  + " in the edit scene; visible after exiting Play"
                : "Applied to " + objectText;
            m_log.Info(m_mode == EditorMode::Play
                ? "Material Forge applied the material to " + objectText
                  + " in the edit scene; it will appear after exiting Play"
                : "Material Forge applied the material to " + objectText);
        } else {
            acknowledgementStatus = "no_selection";
            acknowledgementMessage = "No unlocked object was selected in 3DGEditor";
            m_log.Warning(
                "Material Forge deployed the material, but no unlocked object was selected");
        }
    }

    std::string acknowledgementError;
    if (!WriteMaterialForgeAcknowledgement(
            signalPath, signal, acknowledgementStatus, appliedCount,
            acknowledgementMessage, &acknowledgementError)) {
        m_log.Warning(acknowledgementError);
    }

    m_log.Info(
        std::string("Material Forge ")
        + (signal.operation == "updated" ? "updated: " : "deployed: ")
        + signal.materialPath);
}

void EditorApp::OnRender()
{
    const engine::Window& window = GetWindow();
    const glm::mat4 viewProj = m_camera.ProjectionMatrix(window.AspectRatio()) * m_camera.ViewMatrix();
    const EditorScene::Environment& environment = m_scene.GetEnvironment();
    const bool underwaterPost = UpdateUnderwaterState(m_camera, m_dt);

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
    const bool  useHdrPost = environment.ssr || wantScale || hasGraphPost || underwaterPost;
    const int   sw = std::max(1, static_cast<int>(std::lround(window.Width()  * scale)));
    const int   sh = std::max(1, static_cast<int>(std::lround(window.Height() * scale)));
    m_renderW = window.Width();
    m_renderH = window.Height();

    const auto cpuRenderStart = std::chrono::high_resolution_clock::now();
    // The overlay toggle also controls query/counter work, so hiding the
    // profiler removes its per-frame overhead rather than only hiding its UI.
    m_gpuProfiler.SetEnabled(m_showProfiler);
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
        m_underwaterVisuals.blend = m_underwaterBlend;
        m_postProcess->underwater = m_underwaterVisuals;
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
    DrawFoliage(m_camera, GetWindow().AspectRatio());        // batched trees/bushes/rocks
    DrawGrass(m_camera, GetWindow().AspectRatio());          // opaque grass on terrain (before water)
    CaptureWaterSceneBuffers();                              // copy opaque colour/depth once for all water
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
        if (m_playRegistry && m_text) {
            engine::DrawWorldHealthBars(
                *m_text, *m_playRegistry, viewProj,
                window.Width(), window.Height(), m_playPlayerEntity);
        }
        DrawPlayHud();
    }

    const auto uiCpuStart = std::chrono::high_resolution_clock::now();
    m_gpuProfiler.Begin("UI");
    // A captured GLFW cursor still reports a virtual screen position. Never pass
    // that position to ImGui: camera-look capture owns the mouse exclusively, so
    // panel fields cannot hover, focus, drag, or change until capture is released.
    {
        ImGuiIO& io = ImGui::GetIO();
        const bool captured = m_playMouseCaptured
            || m_cameraController.MouseLookActive();
        if (captured) io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
        else          io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
        io.MouseDrawCursor = false;
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
    engine::SetScriptErrorHandler(nullptr);   // drop the 'this'-capturing sink
    // Clear script factories before the loaded module (their DLL) unloads, so the static
    // ScriptRegistry singleton doesn't destroy DLL-owned callables after FreeLibrary.
    engine::ScriptRegistry::Instance().Clear();
    engine::ai::BtScriptRegistry::Instance().Clear();
    m_scriptModule.Unload();
    m_projectScriptClasses.clear();
    m_projectBtScriptClasses.clear();
    m_projectScriptStageSlot = -1;
    ResetScriptAutoReloadWatcher();
    m_imgui.Shutdown();
    if (m_hasProjectFile) {
        m_project.Save(m_projectConfig);
        m_projectConfig.Save();
        m_config.Set("editor.current_project", m_project.ProjectFilePath());
    } else {
        m_project.Save(m_config);
    }
    m_config.Set("window.vsync", GetWindow().IsVSync());
    m_config.Set("window.fullscreen", GetWindow().IsFullscreen());
    m_config.Save();
}

void EditorApp::DrawEditModeModels(const glm::mat4 & viewProj)
{
    if (!m_modelShader && !m_skinnedRenderer) {
        m_editAnimationPoses.clear();
        return;
    }

    std::vector<Entity> activePoseEntities;
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
            activePoseEntities.push_back(object.entity);
            const engine::SkinnedModel* model = m_editAssets.LoadSkinnedModel(object.modelAssetPath, &error);
            if (!model) {
                if (!m_editModelLoadErrors[object.modelAssetPath]) {
                    m_log.Error("Could not load edit skinned model: " + error);
                    m_editModelLoadErrors[object.modelAssetPath] = true;
                }
                continue;
            }
            // Merge the separate FBX clips so the edit preview animates too. The plain
            // model above stays cached, so selection picking (which looks it up by path)
            // still resolves the mesh bounds.
            if (!object.animationSources.empty()) {
                std::vector<engine::RuntimeAssetManager::SkinnedAnimationSource> sources;
                sources.reserve(object.animationSources.size());
                for (const EditorScene::AnimationSource& s : object.animationSources) {
                    sources.push_back({s.file, s.clipName, s.stripRootMotion, s.sourceClipName});
                }
                if (const engine::SkinnedModel* merged =
                        m_editAssets.LoadSkinnedModel(object.modelAssetPath, sources, &error)) {
                    model = merged;
                }
            }

            engine::AnimatedModel animated;
            animated.SetModel(model);
            // Render-only orientation offset: rotate the mesh upright AND re-centre it
            // on the object origin (where the capsule is centred), so the standing model
            // lines up with the collider. The object transform is untouched, so the
            // collider/controller stay upright. Only applied when an offset is set.
            animated.renderOffset = engine::MakeModelRenderOffset(
                object.modelOffsetPosition, object.modelOrientationEuler,
                object.modelOffsetScale, model->Center());
            // Resolve socketed attachments (weapons/shields) for the edit preview.
            for (const EditorScene::ModelAttachment& a : object.modelAttachments) {
                if (a.modelPath.empty()) continue;
                std::string attachError;
                if (const engine::Model* attachModel =
                        m_editAssets.LoadModel(a.modelPath, &attachError)) {
                    engine::ModelAttachment resolved;
                    resolved.model = attachModel;
                    resolved.bone = model->GetSkeleton().Find(a.boneName);
                    resolved.boneBind = resolved.bone >= 0
                        ? glm::inverse(model->GetSkeleton().bones[static_cast<std::size_t>(resolved.bone)].offset)
                        : glm::mat4(1.0f);
                    resolved.localOffset = engine::MakeAttachmentOffset(a.position, a.eulerDegrees, a.scale);
                    if (!a.materialPath.empty()) {
                        std::string matError;
                        if (const engine::RuntimeMaterialAsset* mat =
                                m_editAssets.LoadMaterial(a.materialPath, &matError)) {
                            resolved.tint = mat->material.albedo;
                            if (!mat->albedoMapPath.empty()) {
                                resolved.albedoOverride = m_editAssets.LoadTexture(mat->albedoMapPath, &matError);
                            }
                        }
                    }
                    animated.attachments.push_back(resolved);
                }
            }
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
                    editor::BuildAnimationController(animated.controller,
                        object.animationStates, object.animationParameters,
                        object.animationTransitions, resolveClip, clipSeconds);
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
            // In the editor the character holds a paused idle by default; only advance
            // when the user turns on "Animate Characters in Editor".
            if (object.animationAutoplay && m_previewSceneAnimations) {
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
            const bool actionPreview =
                m_animationPreviewAction.active
                && m_animationPreviewAction.entity == object.entity
                && m_animationPreviewAction.clip >= 0
                && m_animationPreviewAction.clip < static_cast<int>(model->AnimationCount());
            const bool advancingPreview =
                object.animationAutoplay && m_previewSceneAnimations;
            const EditorScene::Object* selectedObject =
                m_scene.SelectedObject();
            const bool selectedForEditing =
                selectedObject && selectedObject->entity == object.entity;
            const auto cachedPose = m_editAnimationPoses.find(object.entity);
            if (!actionPreview && !advancingPreview
                && !selectedForEditing
                && cachedPose != m_editAnimationPoses.end()) {
                // A paused editor preview has the same pose until its animation
                // settings change. Reuse it instead of walking the complete
                // skeleton and rebuilding bone matrices every frame.
                if (engine::AnimatedModel* preview =
                        previewRegistry.TryGet<engine::AnimatedModel>(previewEntity)) {
                    preview->pose = cachedPose->second;
                }
            } else if (actionPreview) {
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
                                            transform->Model() * preview->renderOffset,
                                            m_camera,
                                            window.AspectRatio(),
                                            sky.keyLightDirection,
                                            sky.keyLightColor * environment.sunIntensity,
                                            sky.ambient * environment.skyLightIntensity);
                }
                // Draw socketed attachments (weapons/shields) on the animated bones.
                if (m_modelShader && !preview->attachments.empty()) {
                    m_modelShader->Bind();
                    m_modelShader->SetMat4("uViewProj", viewProj);
                    m_modelShader->SetVec3("uLightPos", m_camera.Position() + glm::vec3(-4.0f, 6.0f, 4.0f));
                    m_modelShader->SetVec3("uLightColor", glm::vec3(1.0f));
                    m_modelShader->SetVec3("uViewPos", m_camera.Position());
                    engine::DrawAnimatedModelAttachments(
                        *preview, transform->Model() * preview->renderOffset, *m_modelShader);
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

    for (auto it = m_editAnimationPoses.begin();
         it != m_editAnimationPoses.end();) {
        if (std::find(activePoseEntities.begin(), activePoseEntities.end(),
                      it->first) == activePoseEntities.end()) {
            it = m_editAnimationPoses.erase(it);
        } else {
            ++it;
        }
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
    if (m_scene.SelectedIndices().empty()) {
        return;
    }

    const engine::Window& window = GetWindow();
    const glm::vec2 viewportSize(static_cast<float>(window.Width()), static_cast<float>(window.Height()));
    const int primaryIndex = m_scene.SelectedIndex();

    // Outline one object (model / terrain / mesh) in the given highlight colour.
    auto drawOutline = [&](const EditorScene::Object& object, const Transform& transform,
                           const glm::vec3& color) {
        if (!object.modelAssetPath.empty()) {
            std::string error;
            if (object.skeletalModel && m_skinnedOutlineShader) {
                if (const engine::SkinnedModel* model =
                        m_editAssets.LoadSkinnedModel(object.modelAssetPath, &error)) {
                    std::vector<glm::mat4> bindPose;
                    const auto found = m_editAnimationPoses.find(object.entity);
                    const std::vector<glm::mat4>* pose =
                        found != m_editAnimationPoses.end() ? &found->second : nullptr;
                    if (!pose) {
                        engine::Animator::ComputeBindPose(model->GetSkeleton(), bindPose);
                        pose = &bindPose;
                    }
                    m_skinnedOutlineShader->Bind();
                    m_skinnedOutlineShader->SetMat4("uViewProj", viewProj);
                    m_skinnedOutlineShader->SetVec2("uViewportSize", viewportSize);
                    const glm::mat4 renderOffset = engine::MakeModelRenderOffset(
                        object.modelOffsetPosition, object.modelOrientationEuler,
                        object.modelOffsetScale, model->Center());
                    m_viewport.DrawSelectedSkinnedModelOutline(m_renderer, *m_skinnedOutlineShader,
                        transform, *model, *pose, color, 2.0f, renderOffset);
                }
                return;
            }
            if (const engine::Model* model = m_editAssets.LoadModel(object.modelAssetPath, &error)) {
                m_outlineShader->Bind();
                m_outlineShader->SetMat4("uViewProj", viewProj);
                m_outlineShader->SetVec2("uViewportSize", viewportSize);
                m_viewport.DrawSelectedModelOutline(m_renderer, *m_outlineShader, transform, *model, color, 2.0f);
            }
            return;
        }
        if (object.isTerrain) {
            const auto terrainIt = m_terrains.find(object.entity);
            if (terrainIt != m_terrains.end() && terrainIt->second.terrain.Ready()) {
                m_outlineShader->Bind();
                m_outlineShader->SetMat4("uViewProj", viewProj);
                m_outlineShader->SetVec2("uViewportSize", viewportSize);
                m_viewport.DrawSelectedMeshOutline(m_renderer, *m_outlineShader, transform,
                                                   terrainIt->second.terrain.GetMesh(), color, 2.0f);
            }
            return;
        }
        if (const MeshRenderer* renderer = m_scene.TryGetMeshRenderer(object.entity);
            renderer && renderer->mesh) {
            m_outlineShader->Bind();
            m_outlineShader->SetMat4("uViewProj", viewProj);
            m_outlineShader->SetVec2("uViewportSize", viewportSize);
            m_viewport.DrawSelectedMeshOutline(m_renderer, *m_outlineShader, transform, *renderer->mesh, color, 2.0f);
        }
    };

    for (int index : m_scene.SelectedIndices()) {
        if (index < 0 || index >= static_cast<int>(m_scene.Objects().size())) continue;
        const EditorScene::Object& object = m_scene.Objects()[static_cast<std::size_t>(index)];
        const Transform* transform = m_scene.TryGetTransform(object.entity);
        if (!transform || !object.visible || object.navMeshBoundsVolume
            || object.primitive == EditorScene::Primitive::Empty) {
            continue;
        }

        const bool isPrimary = (index == primaryIndex);
        const glm::vec3 color = object.locked
            ? glm::vec3(1.0f, 0.28f, 0.08f)                     // locked: red
            : (isPrimary ? glm::vec3(1.0f, 0.55f, 0.05f)       // active: orange
                         : glm::vec3(1.0f, 0.82f, 0.30f));      // also-selected: amber

        // River geometry has a dedicated boundary highlight (scene-selection based, so
        // only the primary draws it); other objects use the generic outline.
        if (object.isWater && !object.waterFlowSpline.empty()) {
            if (isPrimary) m_viewport.DrawSelectedRiverBoundary(m_scene, viewProj);
            continue;
        }
        drawOutline(object, *transform, color);
    }
}

void EditorApp::DrawEditorOverlay()
{
    // Frame profiler overlay: CPU render cost + per-pass GPU timings (previous frame).
    if (m_showProfiler) {
        if (ImGui::Begin("Profiler", &m_showProfiler,
                         ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing)) {
            const ImGuiIO& io = ImGui::GetIO();

            // Record this frame's real time into the rolling history.
            const float frameMs = io.DeltaTime * 1000.0f;
            m_frameMsHistory[static_cast<std::size_t>(m_frameMsHead)] = frameMs;
            m_frameMsHead = (m_frameMsHead + 1) % kFrameHistory;

            // Window stats (min / avg / max) over the recorded history.
            float mn = 1.0e9f, mx = 0.0f, sum = 0.0f;
            int samples = 0;
            for (float ms : m_frameMsHistory) {
                if (ms <= 0.0f) continue;
                mn = std::min(mn, ms);
                mx = std::max(mx, ms);
                sum += ms;
                ++samples;
            }
            const float avg = samples > 0 ? sum / static_cast<float>(samples) : 0.0f;
            if (samples == 0) mn = 0.0f;

            ImGui::Text("FPS: %.0f  (%.2f ms/frame)", io.Framerate, frameMs);
            // Frame-time graph: spikes reveal hitches (streaming, GC, big uploads).
            const float plotMax = std::max(mx * 1.1f, 16.7f);
            ImGui::PlotLines("##frametime", m_frameMsHistory.data(), kFrameHistory,
                             m_frameMsHead, nullptr, 0.0f, plotMax, ImVec2(220.0f, 45.0f));
            ImGui::Text("min %.2f  avg %.2f  max %.2f ms", mn, avg, mx);
            // 1% low: the worst-1% (99th percentile) frame time as an FPS figure — a far
            // better "felt smoothness" measure than average FPS (spikes show here first).
            if (samples > 4) {
                std::vector<float> sorted;
                sorted.reserve(static_cast<std::size_t>(samples));
                for (float ms : m_frameMsHistory) if (ms > 0.0f) sorted.push_back(ms);
                std::sort(sorted.begin(), sorted.end());
                const std::size_t idx = std::min(sorted.size() - 1,
                    static_cast<std::size_t>(sorted.size() * 0.99f));
                const float p99 = sorted[idx];
                ImGui::Text("1%% low: %.0f fps  (%.2f ms)",
                            p99 > 0.0f ? 1000.0f / p99 : 0.0f, p99);
            }
            ImGui::Separator();

            ImGui::Text("CPU render: %.2f ms", m_cpuFrameMs);
            ImGui::Text("  scene submit: %.2f ms", m_cpuSceneMs);
            ImGui::Text("  ui build:     %.2f ms", m_cpuUiMs);
            ImGui::Text("Draw calls: %d", m_gpuProfiler.DrawCalls());
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

            // Scene counts: what the frame is actually pushing through.
            ImGui::Separator();
            int objectCount = 0, visibleCount = 0, lightCount = 0;
            int modelCount = 0, terrainCount = 0, waterCount = 0, foliageCount = 0,
                particleCount = 0, skinnedCount = 0;
            std::size_t triangles = 0, vertices = 0;
            for (const EditorScene::Object& object : m_scene.Objects()) {
                ++objectCount;
                if (object.light) ++lightCount;
                if (object.isTerrain) ++terrainCount;
                if (object.isWater) ++waterCount;
                if (object.isFoliage) ++foliageCount;
                if (object.particleSystemEnabled) ++particleCount;
                if (object.skeletalModel) ++skinnedCount;
                else if (!object.modelAssetPath.empty()) ++modelCount;
                if (!object.visible) continue;
                ++visibleCount;
                // Triangle/vertex load from primitive mesh renderers (terrain is added
                // separately below; models/skinned use their own geometry). A quick read
                // of how heavy the visible scene is.
                const bool primitive = object.modelAssetPath.empty() && !object.isTerrain
                    && !object.isWater && !object.isFoliage && !object.skeletalModel
                    && !object.light;
                if (primitive) {
                    if (const MeshRenderer* mr = m_scene.TryGetMeshRenderer(object.entity)) {
                        if (mr->mesh) {
                            triangles += mr->mesh->TriangleCount();
                            vertices += mr->mesh->VertexCount();
                        }
                    }
                }
            }
            for (const auto& entry : m_terrains) {
                triangles += entry.second.terrain.GetMesh().TriangleCount();
                vertices += entry.second.terrain.GetMesh().VertexCount();
            }
            ImGui::Text("Objects: %d  (visible %d)", objectCount, visibleCount);
            ImGui::Text("Lights:  %d", lightCount);
            ImGui::Text("Triangles: %zu   Vertices: %zu", triangles, vertices);
            // Scene composition — where the frame's geometry cost is going.
            ImGui::Text("Models %d  Skinned %d  Terrain %d", modelCount, skinnedCount,
                        terrainCount);
            ImGui::Text("Water %d  Foliage %d  Particles %d", waterCount, foliageCount,
                        particleCount);
            if (m_mode == EditorMode::Play) {
                ImGui::Text("Play entities: %d",
                            m_playRegistry ? static_cast<int>(m_playEntityNames.size()) : 0);
            }

            // GPU memory (NVIDIA NVX / driver-exposed). Silently skipped on GPUs that do
            // not report it (the query sets GL_INVALID_ENUM, which we swallow).
            constexpr unsigned int kNvxTotalVidMem   = 0x9048;
            constexpr unsigned int kNvxCurrentAvail  = 0x9049;
            GLint vramTotalKb = 0, vramAvailKb = 0;
            while (glGetError() != GL_NO_ERROR) {}   // clear any prior error
            glGetIntegerv(kNvxTotalVidMem, &vramTotalKb);
            glGetIntegerv(kNvxCurrentAvail, &vramAvailKb);
            if (glGetError() == GL_NO_ERROR && vramTotalKb > 0) {
                ImGui::Separator();
                ImGui::Text("VRAM: %d / %d MB",
                            (vramTotalKb - vramAvailKb) / 1024, vramTotalKb / 1024);
            }
        }
        ImGui::End();
    }

    m_audio.SetListener(m_camera.Position(), m_camera.Front());
    EditorDockspace::Context dockspaceContext;
    dockspaceContext.panels = &m_panels;
    dockspaceContext.config = &m_config;
    dockspaceContext.scene = &m_scene;
    dockspaceContext.assets = &m_assets;
    if (!m_dependencyAssetOpenPath.empty()) {
        if (m_dependencyAssetOpenType == EditorAssets::Type::Scene)
            dockspaceContext.sceneAssetOpenRequested = m_dependencyAssetOpenPath;
        else if (m_dependencyAssetOpenType == EditorAssets::Type::BehaviorGraph)
            dockspaceContext.behaviorGraphAssetOpenRequested = m_dependencyAssetOpenPath;
        else {
            dockspaceContext.editorAssetOpenRequested = m_dependencyAssetOpenPath;
            dockspaceContext.editorAssetOpenType = m_dependencyAssetOpenType;
        }
        m_dependencyAssetOpenPath.clear();
    }
    dockspaceContext.dragDrop = &m_dragDrop;
    dockspaceContext.project = &m_project;
    dockspaceContext.log = &m_log;
    dockspaceContext.gizmo = &m_gizmo;
    dockspaceContext.camera = &m_camera;
    dockspaceContext.selectedSplinePoint = &m_selectedSplinePoint;
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
    dockspaceContext.autoCompileScripts = &m_autoCompileScripts;
    dockspaceContext.scriptBuildRunning = m_scriptBuildRunning;
    dockspaceContext.scriptBuildStatus = &m_scriptBuildStatus;
    dockspaceContext.physicsPaused = m_physicsPaused;
    dockspaceContext.physicsFixedTimestep = m_physicsFixedTimestep;
    dockspaceContext.physicsAccumulator = m_physicsAccumulator;
    dockspaceContext.physicsStepsLastFrame = m_physicsStepsLastFrame;
    dockspaceContext.physicsEventEnterCount = m_physicsEventEnterCount;
    dockspaceContext.physicsEventStayCount = m_physicsEventStayCount;
    dockspaceContext.physicsEventExitCount = m_physicsEventExitCount;
    dockspaceContext.physicsActionCount = m_physicsActionCount;
    dockspaceContext.physicsEventRows = &m_physicsEventRows;
    // These builders copy authored animation states, transitions, events, and
    // action profiles. Do that work only while the corresponding panel is open.
    if (m_panels.IsOpen(EditorPanels::Panel::GameplayDebug)) {
        dockspaceContext.gameplayDebug = BuildGameplayDebugState();
    }
    if (m_panels.IsOpen(EditorPanels::Panel::AnimationPreview)) {
        dockspaceContext.animationPreview = BuildAnimationPreviewState();
    }
    dockspaceContext.showPhysicsEventGuides = &m_showPhysicsEventGuides;
    dockspaceContext.showAiDebug = &m_showAiDebug;
    dockspaceContext.useNavMesh = &m_useNavMesh;
    dockspaceContext.vsync = GetWindow().IsVSync();
    // Terrain sculpting and painting are owned by Terrain Creator rather than
    // the selected scene object's Inspector.
    m_terrainSculpt = false;
    dockspaceContext.terrainSculpt = nullptr;
    dockspaceContext.terrainSculptMode = nullptr;
    dockspaceContext.terrainPaintLayer = nullptr;
    dockspaceContext.terrainBrushRadius = nullptr;
    dockspaceContext.terrainBrushStrength = nullptr;
    dockspaceContext.foliagePaint = &m_foliagePaint;
    dockspaceContext.foliageErase = &m_foliageErase;
    // Auto-lock the Hierarchy's selection while the user is working in the viewport: a
    // gizmo drag in progress, or terrain/foliage paint modes active.
    dockspaceContext.viewportInteractionActive =
        m_mouse.GizmoActiveFor(GLFW_MOUSE_BUTTON_LEFT)
        || m_terrainSculpt || m_foliagePaint;
    dockspaceContext.foliageBrushRadius = &m_foliageBrushRadius;
    dockspaceContext.foliagePaintDensity = &m_foliagePaintDensity;
    dockspaceContext.foliageTypeIndex = &m_foliageTypeIndex;
    dockspaceContext.showNavigationPreview = &m_showNavigationPreview;
    dockspaceContext.showGrid = &m_showGrid;
    dockspaceContext.previewAnimations = &m_previewSceneAnimations;
    dockspaceContext.playFootIK = &m_playFootIK;
    dockspaceContext.showParticleDebug = &m_showParticleDebug;
    dockspaceContext.particleDebugSelectedOnly = &m_particleDebugSelectedOnly;
    dockspaceContext.particleDebugShapes = &m_particleDebugShapes;
    dockspaceContext.particleDebugDirections = &m_particleDebugDirections;
    dockspaceContext.particleDebugBounds = &m_particleDebugBounds;
    dockspaceContext.particleDebugCullingState = &m_particleDebugCullingState;
    dockspaceContext.navigationPreviewPolygons = static_cast<int>(m_editorNavMesh.polys.size());
    dockspaceContext.physicsEventGuidesSelectedOnly = &m_physicsEventGuidesSelectedOnly;
    dockspaceContext.showGameplayTraces = &m_showGameplayTraces;
    dockspaceContext.physicsEventGuidesTriggersOnly = &m_physicsEventGuidesTriggersOnly;
    dockspaceContext.physicsEventGuidesEnterExitOnly = &m_physicsEventGuidesEnterExitOnly;
    dockspaceContext.scenePathBuffer = m_scenePathDraft.data();
    dockspaceContext.scenePathBufferSize = m_scenePathDraft.size();
    dockspaceContext.projectNameBuffer = m_projectNameDraft.data();
    dockspaceContext.projectNameBufferSize = m_projectNameDraft.size();
    dockspaceContext.projectLocationBuffer = m_projectLocationDraft.data();
    dockspaceContext.projectLocationBufferSize = m_projectLocationDraft.size();
    dockspaceContext.openProjectBuffer = m_openProjectDraft.data();
    dockspaceContext.openProjectBufferSize = m_openProjectDraft.size();
    dockspaceContext.packageOutputBuffer = m_packageOutputDraft.data();
    dockspaceContext.packageOutputBufferSize = m_packageOutputDraft.size();
    dockspaceContext.packageConfiguration = &m_packageConfiguration;
    dockspaceContext.packageStaticRuntime = &m_packageStaticRuntime;
    dockspaceContext.packageCleanOutput = &m_packageCleanOutput;
    dockspaceContext.packageCreateZip = &m_packageCreateZip;
    dockspaceContext.packageBuildRunning = m_packageBuildRunning;
    dockspaceContext.packageBuildStatus = &m_packageBuildStatus;
    dockspaceContext.fps = m_fps;
    if (m_particleRenderer) {
        const engine::ParticleRenderer::Stats& stats = m_particleRenderer->GetStats();
        dockspaceContext.particleDrawCalls = stats.drawCalls;
        dockspaceContext.particleCulledEmitters = stats.culledEmitters;
        dockspaceContext.particleRenderedCount = stats.particles;
        dockspaceContext.particleCpuMilliseconds = stats.cpuMilliseconds;
        dockspaceContext.particleGpuMilliseconds = stats.gpuMilliseconds;
    }
    if (!m_animationAssetPreviewPath.empty()) {
        dockspaceContext.animationPreviewTime = &m_animationAssetPreviewTime;
    } else if (const EditorScene::Object* selected = m_scene.SelectedObject()) {
        dockspaceContext.animationPreviewTime = &m_animationPreviewTimes[selected->entity];
    }
    dockspaceContext.animationPreviewAssetPath = &m_animationAssetPreviewPath;
    dockspaceContext.animationPreviewMeshPath = &m_animationAssetPreviewMeshPath;
    dockspaceContext.animationAssetPlaying = &m_animationAssetPreviewPlaying;
    dockspaceContext.animationAssetLoop = &m_animationAssetPreviewLoop;
    dockspaceContext.animationAssetStripRootMotion =
        &m_animationAssetPreviewStripRootMotion;
    dockspaceContext.animationAssetSpeed = &m_animationAssetPreviewSpeed;
    dockspaceContext.animationAssetYaw = &m_animationAssetPreviewYaw;
    dockspaceContext.animationAssetPitch = &m_animationAssetPreviewPitch;
    dockspaceContext.animationAssetZoom = &m_animationAssetPreviewZoom;
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
    m_dockspace.Draw(dockspaceContext);
    DrawImportDialog();   // drag-and-drop Import Settings popup (when files were dropped)
    if (dockspaceContext.animationAssetRestartRequested)
        m_animationAssetPreviewRestartRequested = true;
    if (dockspaceContext.animationAssetRefreshRequested)
        m_animationAssetPreviewRefreshRequested = true;
    if (dockspaceContext.animationAssetOpenInClipEditorRequested
        && !m_animationAssetPreviewPath.empty()) {
        m_clipEditor.QueueSource(m_animationAssetPreviewPath,
                                 m_animationAssetPreviewMeshPath);
        m_panels.SetOpen(EditorPanels::Panel::ClipEditor, true);
    }
    if (dockspaceContext.scriptCompileAndRestartRequested) {
        if (m_mode != EditorMode::Edit) {
            m_log.Warning("Exit Play mode before compiling scripts");
        } else {
            if (m_scene.IsDirty()) SaveScene();
            if (m_scene.IsDirty()) {
                m_log.Error("Script compile cancelled because the scene could not be saved");
            } else {
                std::error_code ec;
                const std::filesystem::path projectRoot =
                    std::filesystem::absolute(m_project.AssetRoot(), ec).parent_path();
                std::string error;
                if (EditorScriptTools::LaunchCompileAndRestart(
                        projectRoot, "Debug", &error)) {
                    m_log.Info("Script compiler started; building the project-owned script module");
                    GetWindow().SetShouldClose(true);
                } else {
                    m_log.Error("Script compiler: " + error);
                }
            }
        }
    }
    if (dockspaceContext.scriptHotReloadRequested) {
        HotReloadScripts();
    }
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
        engine::AudioMixerPreset preset =
            m_audio.CaptureMixerPreset("Project Mixer");
        if (!engine::SaveAudioMixerPreset(dockspaceContext.audioMixerPresetPath.data(),
                preset, &error))
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
    DrawCharacterEditorPanel();
    DrawClipEditorPanel();
    DrawGraphEditorPanel();
    DrawMeshEditorPanel();
    DrawDecalPlacementPanel();
    DrawTerrainCreatorPanel();
    DrawModularPlacementPanel();
    DrawPrefabPalettePanel();
    DrawRoomBuilderPanel();
    DrawScatterPaintPanel();
    DrawArrayToolPanel();
    DrawMeasurementPanel();
    DrawLevelValidationPanel();
    DrawOptimizationAuditorPanel();
    DrawLightingAnalysisPanel();
    DrawRagdollPhysicsPanel();
    DrawAnimationRetargetingPanel();
    DrawAbilityEditorPanel();
    DrawRuntimePropertyInspectorPanel();
    DrawAssetDependencyViewerPanel();
    DrawWeatherEditorPanel();
    DrawProceduralBuildingPanel();
    DrawRoadGeneratorPanel();
    DrawLevelInstancePanel();
    DrawWorldPartitionPanel();
    DrawProceduralScatterGraphPanel();
    DrawBiomeEditorPanel();
    DrawDayNightTimelinePanel();
    DrawCaveTunnelPanel();
    DrawLevelVariantPanel();
    DrawLevelLayersPanel();
    DrawViewportBookmarksPanel();
    DrawBlockoutPanel();
    DrawAlignmentPanel();
    DrawSplineBuilderPanel();
    DrawPrefabEditorPanel();
    DrawScriptDebugPanel();
    DrawViewportPanel();
    DrawWorldEditorPanel();
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
    if (dockspaceContext.browseProjectLocationRequested) {
        const std::string dir = editor::PickFolderDialog("Choose project location");
        if (!dir.empty()) {
            std::memset(m_projectLocationDraft.data(), 0, m_projectLocationDraft.size());
            std::snprintf(m_projectLocationDraft.data(), m_projectLocationDraft.size(), "%s", dir.c_str());
        }
    }
    if (dockspaceContext.newProjectRequested) {
        if (m_projectLocationDraft[0] == '\0') {
            // No location chosen yet -> ask for one now via the native picker.
            const std::string dir = editor::PickFolderDialog("Choose project location");
            if (!dir.empty()) {
                std::snprintf(m_projectLocationDraft.data(), m_projectLocationDraft.size(), "%s", dir.c_str());
            }
        }
        if (m_projectLocationDraft[0] != '\0') {
            NewProject(m_projectLocationDraft.data(), m_projectNameDraft.data());
        } else {
            m_log.Warning("New project cancelled: no location chosen");
        }
    }
    if (dockspaceContext.browseOpenProjectRequested) {
        const std::string file = editor::OpenFileDialog("Open project", "3DG Project", "3dgproject");
        if (!file.empty()) {
            OpenProjectFromPath(file);
        }
    }
    if (dockspaceContext.openProjectRequested) {
        OpenProjectFromPath(m_openProjectDraft.data());
    }
    if (dockspaceContext.browsePackageOutputRequested) {
        const std::string dir = editor::PickFolderDialog("Choose package output folder");
        if (!dir.empty()) {
            std::memset(m_packageOutputDraft.data(), 0, m_packageOutputDraft.size());
            std::snprintf(m_packageOutputDraft.data(), m_packageOutputDraft.size(),
                "%s", dir.c_str());
            PersistPackagingSettings();
        }
    }
    if (dockspaceContext.packageSettingsChanged) {
        PersistPackagingSettings();
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
    if (!dockspaceContext.behaviorGraphAssetOpenRequested.empty()) {
        m_panels.SetOpen(EditorPanels::Panel::BehaviorGraph, true);
        if (m_behaviorGraph.LoadFromFile(dockspaceContext.behaviorGraphAssetOpenRequested)) {
            m_log.Info("Opened behavior tree: "
                + dockspaceContext.behaviorGraphAssetOpenRequested);
        } else {
            m_log.Error(m_behaviorGraph.StatusMessage());
        }
    }
    if (!dockspaceContext.editorAssetOpenRequested.empty()) {
        const std::string& path = dockspaceContext.editorAssetOpenRequested;
        switch (dockspaceContext.editorAssetOpenType) {
        case EditorAssets::Type::Material:
            m_panels.SetOpen(EditorPanels::Panel::MaterialMaker, true);
            if (m_materialMaker.LoadFromFile(path)) {
                m_log.Info("Opened material: " + path);
            } else {
                m_log.Error("Material Maker could not open: " + path);
            }
            break;
        case EditorAssets::Type::Shader:
            if (std::filesystem::path(path).extension() == ".3dgshader") {
                m_panels.SetOpen(EditorPanels::Panel::ShaderEditor, true);
                m_shaderEditor.QueueOpen(path);
                m_log.Info("Opening shader: " + path);
            } else {
                m_log.Warning("Only .3dgshader assets open in the Shader Editor");
            }
            break;
        case EditorAssets::Type::Particle:
        case EditorAssets::Type::ParticleEffect:
            m_panels.SetOpen(EditorPanels::Panel::ParticleEditor, true);
            m_particleEditor.RequestOpen(path);
            m_log.Info("Opening particle asset: " + path);
            break;
        case EditorAssets::Type::Hud: {
            std::string error;
            if (m_hud.Load(path, &error)) {
                m_hudPath = path;
                m_hudPanel.SetPath(path);
                m_hudPanel.SetSelected(-1);
                m_panels.SetOpen(EditorPanels::Panel::Hud, true);
                m_log.Info("Opened HUD: " + path);
            } else {
                m_log.Error("HUD load failed: " + error);
            }
            break;
        }
        case EditorAssets::Type::Character:
            m_panels.SetOpen(EditorPanels::Panel::CharacterEditor, true);
            m_characterEditor.QueueOpen(path);
            m_log.Info("Opening character: " + path);
            break;
        case EditorAssets::Type::AnimationClip:
            m_panels.SetOpen(EditorPanels::Panel::ClipEditor, true);
            m_clipEditor.QueueOpen(path);
            m_log.Info("Opening animation clip: " + path);
            break;
        case EditorAssets::Type::AnimationGraph:
            m_panels.SetOpen(EditorPanels::Panel::GraphEditor, true);
            m_graphEditor.QueueOpen(path);
            m_log.Info("Opening animation graph: " + path);
            break;
        case EditorAssets::Type::Prefab: {
            std::string prefabError;
            if (m_prefabAsset.Load(path, &prefabError)) {
                m_prefabPath = path;
                m_panels.SetOpen(EditorPanels::Panel::Prefab, true);
                m_log.Info("Opened prefab: " + path);
            } else {
                m_log.Error("Prefab load failed: " + prefabError);
            }
            break;
        }
        case EditorAssets::Type::SkeletalModel:
        case EditorAssets::Type::Model:
            m_panels.SetOpen(EditorPanels::Panel::MeshEditor, true);
            m_meshEditor.QueueOpen(path);
            m_log.Info("Opening mesh: " + path);
            break;
        case EditorAssets::Type::Skeleton:
        case EditorAssets::Type::Animation:
            m_panels.SetOpen(EditorPanels::Panel::AnimationPreview, true);
            OpenAnimationAssetPreview(path, dockspaceContext.editorAssetOpenType);
            m_log.Info("Opened Animation Preview for " + path);
            break;
        case EditorAssets::Type::Audio:
            m_panels.SetOpen(EditorPanels::Panel::AudioEditor, true);
            break;
        case EditorAssets::Type::World: {
            std::string error;
            m_worldAuthoringPath = path;
            if (engine::LoadWorldManifest(path, &m_worldAuthoring, &error)) {
                m_panels.SetOpen(EditorPanels::Panel::WorldEditor, true);
                m_worldStatus = "Loaded " + path;
                m_log.Info("Opened world: " + path);
            } else {
                m_log.Error("World load failed: " + error);
            }
            break;
        }
        case EditorAssets::Type::Foliage: {
            const EditorScene::Object* sel = m_scene.SelectedObject();
            if (sel && sel->isFoliage) {
                m_scene.SetSelectedFoliageAsset(path);
                m_editAssets.ResolveRegistryAssets(m_scene.Registry());
                m_log.Info("Assigned foliage asset to '" + sel->name
                    + "'. Edit types and paint in the Inspector's Foliage section.");
            } else {
                m_log.Info("Select a Foliage actor first (Add > Foliage), then this asset "
                    "assigns to it. Edit types and paint from the Inspector.");
            }
            break;
        }
        case EditorAssets::Type::ScatterGraph:
            m_panels.SetOpen(EditorPanels::Panel::ProceduralScatterGraph, true);
            m_proceduralScatterGraph.QueueOpen(path);
            m_log.Info("Opening procedural scatter graph: " + path);
            break;
        case EditorAssets::Type::Biome:
            m_panels.SetOpen(EditorPanels::Panel::BiomeEditor, true);
            m_biomeEditor.QueueOpen(path);
            m_log.Info("Opening biome: " + path);
            break;
        case EditorAssets::Type::DayNightTimeline:
            m_panels.SetOpen(EditorPanels::Panel::DayNightTimeline, true);
            m_dayNightTimeline.QueueOpen(path);
            m_log.Info("Opening day/night timeline: " + path);
            break;
        case EditorAssets::Type::Cave:
            m_panels.SetOpen(EditorPanels::Panel::CaveTunnel, true);
            m_caveTunnel.QueueOpen(path);
            m_log.Info("Opening cave asset: " + path);
            break;
        case EditorAssets::Type::Terrain:
            m_panels.SetOpen(EditorPanels::Panel::TerrainCreator, true);
            m_terrainCreator.QueueOpen(path);
            m_log.Info("Opening terrain: " + path);
            break;
        case EditorAssets::Type::Ragdoll:
            m_panels.SetOpen(EditorPanels::Panel::RagdollPhysics, true);
            m_ragdollPhysics.QueueOpen(path);
            m_log.Info("Opening ragdoll physics asset: " + path);
            break;
        case EditorAssets::Type::AnimationRetarget:
            m_panels.SetOpen(EditorPanels::Panel::AnimationRetargeting, true);
            m_animationRetargeting.QueueOpen(path);
            m_log.Info("Opening animation retarget profile: " + path);
            break;
        case EditorAssets::Type::Ability:
            m_panels.SetOpen(EditorPanels::Panel::AbilityEditor, true);
            m_abilityEditor.QueueOpen(path);
            m_log.Info("Opening ability: " + path);
            break;
        case EditorAssets::Type::Weather:
            m_panels.SetOpen(EditorPanels::Panel::WeatherEditor, true);
            m_weatherEditor.QueueOpen(path);
            m_log.Info("Opening weather: " + path);
            break;
        case EditorAssets::Type::Building:
            m_panels.SetOpen(EditorPanels::Panel::ProceduralBuilding, true);
            m_proceduralBuilding.QueueOpen(path);
            m_log.Info("Opening procedural building: " + path);
            break;
        case EditorAssets::Type::Road:
            m_panels.SetOpen(EditorPanels::Panel::RoadGenerator, true);
            m_roadGenerator.QueueOpen(path);
            m_log.Info("Opening road: " + path);
            break;
        case EditorAssets::Type::Texture:
            m_log.Info("Texture selected; drag it to a material texture slot to use it");
            break;
        case EditorAssets::Type::Scene:
        case EditorAssets::Type::BehaviorGraph:
        case EditorAssets::Type::Script:
        case EditorAssets::Type::Other:
            break;
        }
    }
    if (dockspaceContext.exportRuntimeRequested) {
        ExportRuntimeScene();
    }
    if (dockspaceContext.cookProjectRequested) {
        CookProject();
    }
    if (dockspaceContext.packageProjectRequested) {
        PackageProject();
    }
    if (dockspaceContext.validateRuntimeRequested) {
        ValidateRuntimeScene();
    }
    if (dockspaceContext.physicsPauseToggleRequested && m_mode == EditorMode::Play) {
        m_physicsPaused = !m_physicsPaused;
        if (m_physicsPaused) {
            engine::GameMode::Instance().Pause();
        } else {
            engine::GameMode::Instance().Resume();
        }
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
    if (dockspaceContext.addEmptyRequested) {
        AddEmpty();
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
    if (dockspaceContext.addFoliageRequested && m_cube) {
        m_scene.AddFoliage(*m_cube);
        m_foliagePaint = false;
        m_log.Info("Added foliage actor");
    }
    if (dockspaceContext.addWaterRequested) {
        AddWater(dockspaceContext.addWaterPreset);
    }
    if (dockspaceContext.addSplineRequested) {
        AddSpline(dockspaceContext.addSplineType);
    }
    if (dockspaceContext.addRiverForSelectedSplineRequested) {
        const EditorScene::Object* spline = m_scene.SelectedObject();
        if (spline && spline->isSpline && spline->splineType == 1) {
            const std::string splineName = spline->name;
            AddWater(3, false);
            m_scene.SetSelectedWaterFlowSpline(splineName);
            m_log.Info("Created river water using " + splineName);
        }
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
    if (dockspaceContext.mergeSelectedRequested) {
        MergeSelectedToSingleMesh();
    }
    if (dockspaceContext.deleteSelectedRequested) {
        DeleteSelected();
    }
    if (dockspaceContext.frameSelectedRequested) {
        FrameSelected();
    }

// The pre-ImGui text overlay is intentionally disabled. The dockspace is now
// the editor's only panel system in both Edit and Play modes. Keep the gameplay
// HUD path (DrawPlayHud) separate; authored game UI must continue to render.
#if 0
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
            const char* primitiveType = selected->primitive == EditorScene::Primitive::Empty ? "Empty"
                : selected->primitive == EditorScene::Primitive::Plane ? "Plane"
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
    m_text->Text("W move   E rotate   R scale   G uniform scale   Ctrl+R reset   F5 save   F7 export",
        24.0f, static_cast<float>(height) - 34.0f, 1.2f, muted);

    m_text->End();
#endif
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

    engine::HudContext previewContext;
    previewContext.floats = m_hudFloats;
    previewContext.strings = m_hudStrings;
    const engine::GameMode& gameMode = engine::GameMode::Instance();
    previewContext.floats["fps"] = m_fps;
    previewContext.floats["score"] = static_cast<float>(gameMode.Score());
    previewContext.floats["time"] = gameMode.Elapsed();
    previewContext.strings["score"] = std::to_string(gameMode.Score());
    previewContext.strings["gamestate"] = engine::GameMode::StateName(gameMode.State());
    previewContext.strings["gamemessage"] = gameMode.Message();
    if (m_mode == EditorMode::Play && m_playRegistry
        && m_playPlayerEntity != engine::ecs::kNull) {
        if (const engine::Health* health =
                m_playRegistry->TryGet<engine::Health>(m_playPlayerEntity)) {
            previewContext.hasHealth = true;
            previewContext.health = health->hp;
            previewContext.maxHealth = health->maxHp;
            previewContext.healthFraction = health->maxHp > 0.0f
                ? health->hp / health->maxHp : 0.0f;
            previewContext.alive = health->alive;
        }
    }

    bool open = true;
    const HudEditorPanel::Result r = m_hudPanel.Draw(
        m_hud, &open, m_hudImageChoices, texLookup, &previewContext);

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
        if (!m_hudPath.empty()
            && std::filesystem::path(m_hudPath).lexically_normal()
                != std::filesystem::path(r.path).lexically_normal())
            m_hud.assetId = {};
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

void EditorApp::DrawCharacterEditorPanel() {
    if (!m_panels.IsOpen(EditorPanels::Panel::CharacterEditor)) return;
    bool open = true;
    bool assetSaved = false;
    std::string message;
    m_characterEditor.Draw(m_scene, m_project.AssetRoot(), &open,
        &assetSaved, &message, m_dt);
    m_panels.SetOpen(EditorPanels::Panel::CharacterEditor, open);
    if (assetSaved) {
        std::string error;
        if (!m_assets.Refresh(m_project.AssetRoot(), &error)) m_log.Warning(error);
    }
    if (m_characterEditor.ConsumeAddToSceneRequest()) {
        // Drop the character a few units in front of the camera so it's visible.
        glm::vec3 spawn = m_camera.Position() + m_camera.Front() * 6.0f;
        spawn.y = 0.0f;
        AddCharacterToScene(m_characterEditor.Asset(), spawn, m_characterEditor.Path());
    }
    if (!message.empty()) m_log.Info(message);
}

void EditorApp::DrawClipEditorPanel() {
    if (!m_panels.IsOpen(EditorPanels::Panel::ClipEditor)) return;
    bool open = true;
    bool assetSaved = false;
    std::string message;
    m_clipEditor.Draw(m_project.AssetRoot(), &open, &assetSaved, &message, m_dt);
    m_panels.SetOpen(EditorPanels::Panel::ClipEditor, open);
    if (assetSaved) {
        std::string error;
        if (!m_assets.Refresh(m_project.AssetRoot(), &error)) m_log.Warning(error);
    }
    if (!message.empty()) m_log.Info(message);
}

void EditorApp::DrawGraphEditorPanel() {
    if (!m_panels.IsOpen(EditorPanels::Panel::GraphEditor)) return;
    bool open = true;
    bool assetSaved = false;
    std::string message;
    m_graphEditor.SetPreferredPreviewMesh(
        m_animationAssetPreviewMeshPath);
    m_graphEditor.Draw(m_project.AssetRoot(), &open, &assetSaved, &message, m_dt);
    m_panels.SetOpen(EditorPanels::Panel::GraphEditor, open);
    if (assetSaved) {
        std::string error;
        if (!m_assets.Refresh(m_project.AssetRoot(), &error)) m_log.Warning(error);
    }
    if (!message.empty()) m_log.Info(message);
}

void EditorApp::DrawMeshEditorPanel() {
    if (!m_panels.IsOpen(EditorPanels::Panel::MeshEditor)) return;
    bool open = true;
    bool assetSaved = false;
    std::string message;
    m_meshEditor.Draw(&open, &assetSaved, &message);
    m_panels.SetOpen(EditorPanels::Panel::MeshEditor, open);
    if (assetSaved) {
        std::string error;
        if (m_meshEditor.IsSkeletal())
            m_editAssets.ReloadSkinnedModel(m_meshEditor.Path(), &error);
        else
            m_editAssets.ReloadModel(m_meshEditor.Path(), &error);
        if (!error.empty()) m_log.Warning("Mesh reload: " + error);
        error.clear();
        if (!m_assets.Refresh(m_project.AssetRoot(), &error)) m_log.Warning(error);
    }
    if (!message.empty()) m_log.Info(message);
}

void EditorApp::DrawDecalPlacementPanel() {
    if (!m_panels.IsOpen(EditorPanels::Panel::DecalPlacement)) return;
    bool open = true;
    std::string selectedPath;
    if (const EditorAssets::Asset* selected = m_assets.SelectedAsset())
        selectedPath = selected->relativePath;
    m_decalPlacement.Draw(&open,
        m_assets.ContentAssetPaths(EditorAssets::Type::Material),
        m_assets.ContentAssetPaths(EditorAssets::Type::Texture), selectedPath);
    m_panels.SetOpen(EditorPanels::Panel::DecalPlacement, open);
}

void EditorApp::DrawTerrainCreatorPanel() {
    if (!m_panels.IsOpen(EditorPanels::Panel::TerrainCreator)) return;
    bool open = true;
    bool assetSaved = false;
    std::string message;
    m_terrainCreator.Draw(m_project.AssetRoot(), m_assets, &open,
                          &assetSaved, &message, m_dt);
    m_panels.SetOpen(EditorPanels::Panel::TerrainCreator, open);
    if (assetSaved) {
        std::string error;
        if (!m_assets.Refresh(m_project.AssetRoot(), &error)) m_log.Warning(error);
    }

    engine::TerrainAssetData asset;
    std::string sourcePath;
    if (m_terrainCreator.ConsumeAddToLevel(&asset, &sourcePath)) {
        if (!m_plane) {
            m_log.Error("Terrain placement failed: plane mesh is not ready");
        } else {
            m_scene.AddPlane(*m_plane);
            m_scene.SetSelectedName(asset.name);
            m_scene.SetSelectedTerrain(true, asset.resolution, asset.size,
                                       asset.maxHeight, static_cast<int>(asset.seed),
                                       asset.octaves, asset.frequency);
            m_scene.SetSelectedTerrainHeights(asset.heights);
            m_scene.SetSelectedTerrainPaint(asset.paint);
            for (int layer = 1; layer <= 5; ++layer)
                m_scene.SetSelectedTerrainLayerMaterial(layer,
                                                        asset.layerMaterials[layer]);
            m_scene.SetSelectedTerrainGrass(asset.grassEnabled, asset.grassDensity,
                asset.grassHeight, asset.grassWindStrength, asset.grassWindSpeed,
                asset.grassBaseColor, asset.grassTipColor,
                asset.grassRandomizeHeight, asset.grassMinHeightScale,
                asset.grassMaxHeightScale);
            if (const auto* placedTransform = m_scene.SelectedTransform()) {
                engine::ecs::Transform centeredTransform = *placedTransform;
                const float halfSize = std::max(asset.size, 0.01f) * 0.5f;
                centeredTransform.position.x = -halfSize;
                centeredTransform.position.z = -halfSize;
                m_scene.SetSelectedTransform(centeredTransform);
            }
            m_log.Info("Added terrain asset to level: " + sourcePath);
        }
    }
    if (m_terrainCreator.ConsumeApplyToSelected(&asset, &sourcePath)) {
        const EditorScene::Object* selected = m_scene.SelectedObject();
        const engine::ecs::Transform* selectedTransform = m_scene.SelectedTransform();
        if (!selected || !selected->isTerrain) {
            m_log.Warning("Apply terrain failed: select a landscape in the level first");
        } else if (selected->locked || !selectedTransform) {
            m_log.Warning("Apply terrain failed: the selected landscape is locked or unavailable");
        } else {
            const Entity terrainEntity = selected->entity;
            const float oldHalfSize = std::max(selected->terrainSize, 0.01f) * 0.5f;
            const glm::vec3 centerWorld = glm::vec3(selectedTransform->Model()
                * glm::vec4(oldHalfSize, 0.0f, oldHalfSize, 1.0f));

            bool applied = m_scene.SetSelectedTerrain(
                true, asset.resolution, asset.size, asset.maxHeight,
                static_cast<int>(asset.seed), asset.octaves, asset.frequency);
            applied = m_scene.SetSelectedTerrainHeights(asset.heights) && applied;
            applied = m_scene.SetSelectedTerrainPaint(asset.paint) && applied;
            for (int layer = 1; layer <= 5; ++layer)
                applied = m_scene.SetSelectedTerrainLayerMaterial(
                    layer, asset.layerMaterials[layer]) && applied;
            applied = m_scene.SetSelectedTerrainGrass(
                asset.grassEnabled, asset.grassDensity, asset.grassHeight,
                asset.grassWindStrength, asset.grassWindSpeed,
                asset.grassBaseColor, asset.grassTipColor,
                asset.grassRandomizeHeight, asset.grassMinHeightScale,
                asset.grassMaxHeightScale) && applied;
            m_scene.SetSelectedTerrainGrassStyle({});

            if (engine::ecs::Transform* updated = m_scene.SelectedTransform()) {
                engine::ecs::Transform centered = *updated;
                const float newHalfSize = std::max(asset.size, 0.01f) * 0.5f;
                const glm::vec3 localCenter(newHalfSize, 0.0f, newHalfSize);
                centered.position = centerWorld
                    - centered.rotation * (centered.scale * localCenter);
                // No transform change is also a successful outcome when the
                // landscape size and center stayed the same.
                m_scene.SetSelectedTransform(centered);
            }

            // Force regeneration even when dimensions did not change; the
            // sculpted height and paint arrays may still be completely new.
            m_terrains.erase(terrainEntity);
            m_grass.erase(terrainEntity);
            if (applied) {
                m_log.Info("Applied Terrain Creator changes to selected landscape"
                    + (sourcePath.empty() ? std::string{} : ": " + sourcePath));
            } else {
                m_log.Warning("Terrain was only partially applied to the selected landscape");
            }
        }
    }
    if (!message.empty()) m_log.Info(message);
}

void EditorApp::DrawModularPlacementPanel() {
    if (!m_panels.IsOpen(EditorPanels::Panel::ModularPlacement)) return;
    bool open = true;
    const ModularPlacementPanel::Result result =
        m_modularPlacement.Draw(m_project.AssetRoot(), &open);
    m_panels.SetOpen(EditorPanels::Panel::ModularPlacement, open);
    if (!open || !m_modularPlacement.PlacementActive())
        m_hasLastModulePaintPosition = false;

    if (result.placeInFrontRequested) {
        glm::vec3 position = m_camera.Position() + m_camera.Front() * 6.0f;
        position = m_modularPlacement.SnapPosition(position);
        PlaceSelectedModule(position, glm::vec3(0.0f, 1.0f, 0.0f));
    }
    if (result.replaceSelectedRequested) ReplaceSelectionWithModule();
}

bool EditorApp::ComputeModularPlacement(float viewportX, float viewportY,
                                        glm::vec3* position, glm::vec3* normal) {
    if (!position || !normal) return false;
    const engine::Window& window = GetWindow();
    const glm::mat4 viewProj =
        m_camera.ProjectionMatrix(window.AspectRatio()) * m_camera.ViewMatrix();
    glm::vec3 hitPosition(0.0f), hitNormal(0.0f, 1.0f, 0.0f);
    const int hitObject = m_modularPlacement.SurfaceSnap()
        ? m_viewport.PickSceneObject(m_scene, m_editAssets, viewportX, viewportY,
              viewProj, window.Width(), window.Height(), &hitPosition, &hitNormal)
        : -1;
    if (hitObject < 0) {
        hitPosition = m_viewport.SceneDropPosition(viewportX, viewportY, viewProj,
                                                   window.Width(), window.Height());
        hitNormal = glm::vec3(0.0f, 1.0f, 0.0f);
    }

    hitPosition = m_modularPlacement.SnapPosition(hitPosition);
    const ModularPlacementPanel::AssetChoice* choice = m_modularPlacement.SelectedAsset();
    if (choice && m_modularPlacement.OffsetByBounds()) {
        glm::vec3 minimum(-0.5f), maximum(0.5f);
        std::string modelPath = choice->path;
        if (choice->kind == ModularPlacementPanel::AssetKind::Prefab) {
            PrefabAsset prefab;
            std::string error;
            if (prefab.Load(choice->path, &error) && !prefab.object.modelAssetPath.empty())
                modelPath = prefab.object.modelAssetPath;
            else
                modelPath.clear();
        }
        if (!modelPath.empty()) {
            std::string error;
            if (const engine::Model* model = m_editAssets.LoadModel(modelPath, &error)) {
                minimum = model->Min();
                maximum = model->Max();
            }
        }
        const glm::vec3 center = (minimum + maximum) * 0.5f;
        const glm::vec3 extent = glm::max((maximum - minimum) * 0.5f, glm::vec3(0.0f));
        const glm::mat3 rotation = glm::mat3_cast(glm::angleAxis(
            glm::radians(m_modularPlacement.RotationDegrees()), glm::vec3(0, 1, 0)));
        const glm::vec3 n = glm::dot(hitNormal, hitNormal) > 1.0e-8f
            ? glm::normalize(hitNormal) : glm::vec3(0, 1, 0);
        const float support =
            std::abs(glm::dot(n, rotation[0])) * extent.x
            + std::abs(glm::dot(n, rotation[1])) * extent.y
            + std::abs(glm::dot(n, rotation[2])) * extent.z;
        hitPosition += n * (support - glm::dot(n, rotation * center));
    }
    *position = hitPosition;
    *normal = hitNormal;
    return true;
}

bool EditorApp::PlaceSelectedModule(const glm::vec3& position,
                                    const glm::vec3& surfaceNormal) {
    (void)surfaceNormal; // Reserved for optional align-to-normal placement.
    const ModularPlacementPanel::AssetChoice* choice = m_modularPlacement.SelectedAsset();
    if (!choice || !m_cube) return false;

    engine::ecs::Transform transform;
    transform.position = position;
    transform.rotation = glm::angleAxis(
        glm::radians(m_modularPlacement.RotationDegrees()), glm::vec3(0, 1, 0));
    bool created = false;
    if (choice->kind == ModularPlacementPanel::AssetKind::Model) {
        created = m_scene.AddModel(choice->path, *m_cube, transform);
        if (!created) {
            m_log.Warning("Modular placement failed: could not add " + choice->name);
            return false;
        }
    } else {
        PrefabAsset prefab;
        std::string error;
        if (!prefab.Load(choice->path, &error)) {
            m_log.Error("Modular placement failed: " + error);
            return false;
        }
        AddPrefabToScene(prefab, position, choice->path);
        created = m_scene.SelectedObject() != nullptr;
        if (created) m_scene.SetSelectedTransform(transform);
    }

    if (created) {
        const int selectedIndex = m_scene.SelectedIndex();
        std::string uniqueName = choice->name;
        int suffix = 2;
        auto nameExists = [&](const std::string& candidate) {
            for (int i = 0; i < static_cast<int>(m_scene.Objects().size()); ++i) {
                if (i != selectedIndex && m_scene.Objects()[static_cast<std::size_t>(i)].name == candidate)
                    return true;
            }
            return false;
        };
        while (nameExists(uniqueName)) uniqueName = choice->name + "_" + std::to_string(suffix++);
        m_scene.SetSelectedName(uniqueName);
        m_log.Info("Placed module: " + uniqueName);
    }
    return created;
}

void EditorApp::ReplaceSelectionWithModule() {
    const ModularPlacementPanel::AssetChoice* choice = m_modularPlacement.SelectedAsset();
    if (!choice || !m_scene.SelectedObject()) {
        m_log.Warning("Replace module: select a scene object and a palette asset first");
        return;
    }
    if (m_scene.SelectedLocked()) {
        m_log.Warning("Replace module: selected object is locked");
        return;
    }

    bool replaced = false;
    if (choice->kind == ModularPlacementPanel::AssetKind::Model) {
        replaced = m_scene.SetSelectedModelAsset(choice->path);
        if (replaced) {
            m_scene.SetSelectedAnimationSettings(
                false, 0, std::string(), false, false, 1.0f);
        }
    } else {
        PrefabAsset prefab;
        std::string error;
        if (!prefab.Load(choice->path, &error)) {
            m_log.Error("Replace module failed: " + error);
            return;
        }
        const engine::Mesh* mesh = m_cube ? &*m_cube : nullptr;
        switch (prefab.object.primitive) {
        case EditorScene::Primitive::Plane: if (m_plane) mesh = &*m_plane; break;
        case EditorScene::Primitive::Sphere: if (m_sphere) mesh = &*m_sphere; break;
        case EditorScene::Primitive::Capsule: if (m_capsule) mesh = &*m_capsule; break;
        case EditorScene::Primitive::Cylinder: if (m_cylinder) mesh = &*m_cylinder; break;
        case EditorScene::Primitive::Cone: if (m_cone) mesh = &*m_cone; break;
        case EditorScene::Primitive::Pyramid: if (m_pyramid) mesh = &*m_pyramid; break;
        case EditorScene::Primitive::Torus: if (m_torus) mesh = &*m_torus; break;
        case EditorScene::Primitive::Staircase: if (m_staircase) mesh = &*m_staircase; break;
        default: break;
        }
        if (mesh) m_scene.SetSelectedPrimitive(prefab.object.primitive, *mesh);
        replaced = prefab.Apply(m_scene);
        if (replaced) m_scene.SetSelectedPrefabAssetPath(choice->path, prefab.assetId);
    }
    if (replaced) m_log.Info("Replaced selected object with: " + choice->name);
    else m_log.Warning("Replace module did not change the selected object");
}

void EditorApp::DrawPrefabPalettePanel() {
    if (!m_panels.IsOpen(EditorPanels::Panel::PrefabPalette)) return;
    bool open = true;
    const PrefabPalettePanel::Result result =
        m_prefabPalette.Draw(m_project.AssetRoot(), &open);
    m_panels.SetOpen(EditorPanels::Panel::PrefabPalette, open);
    if (result.placeInFrontRequested) {
        const PrefabPalettePanel::Placement placement = m_prefabPalette.NextPlacement();
        if (!placement.path.empty()) {
            glm::vec3 position = m_camera.Position() + m_camera.Front() * 6.0f;
            position = m_prefabPalette.SnapPosition(position);
            PlacePalettePrefab(placement, position);
        }
    }
    if (result.replaceSelectedRequested) ReplaceSelectionWithPalettePrefab();
}

bool EditorApp::ComputePrefabPalettePlacement(
    float viewportX, float viewportY,
    const PrefabPalettePanel::Placement& placement,
    glm::vec3* position, glm::vec3* normal) {
    if (!position || !normal || placement.path.empty()) return false;
    const engine::Window& window = GetWindow();
    const glm::mat4 viewProj =
        m_camera.ProjectionMatrix(window.AspectRatio()) * m_camera.ViewMatrix();
    glm::vec3 hitPosition(0.0f), hitNormal(0.0f, 1.0f, 0.0f);
    const int hitObject = m_prefabPalette.SurfaceSnap()
        ? m_viewport.PickSceneObject(m_scene, m_editAssets, viewportX, viewportY,
              viewProj, window.Width(), window.Height(), &hitPosition, &hitNormal)
        : -1;
    if (hitObject < 0) {
        hitPosition = m_viewport.SceneDropPosition(viewportX, viewportY, viewProj,
                                                   window.Width(), window.Height());
        hitNormal = glm::vec3(0, 1, 0);
    }
    hitPosition = m_prefabPalette.SnapPosition(hitPosition);

    if (m_prefabPalette.OffsetByBounds()) {
        glm::vec3 minimum(-0.5f), maximum(0.5f);
        PrefabAsset prefab;
        std::string error;
        if (prefab.Load(placement.path, &error) && !prefab.object.modelAssetPath.empty()) {
            if (const engine::Model* model =
                    m_editAssets.LoadModel(prefab.object.modelAssetPath, &error)) {
                minimum = model->Min();
                maximum = model->Max();
            }
        }
        const glm::vec3 center = (minimum + maximum) * 0.5f * placement.uniformScale;
        const glm::vec3 extent = glm::max((maximum - minimum) * 0.5f
                                           * placement.uniformScale, glm::vec3(0.0f));
        const glm::mat3 rotation = glm::mat3_cast(glm::angleAxis(
            glm::radians(placement.yawDegrees), glm::vec3(0, 1, 0)));
        const glm::vec3 n = glm::dot(hitNormal, hitNormal) > 1.0e-8f
            ? glm::normalize(hitNormal) : glm::vec3(0, 1, 0);
        const float support =
            std::abs(glm::dot(n, rotation[0])) * extent.x
            + std::abs(glm::dot(n, rotation[1])) * extent.y
            + std::abs(glm::dot(n, rotation[2])) * extent.z;
        hitPosition += n * (support - glm::dot(n, rotation * center));
    }
    *position = hitPosition;
    *normal = hitNormal;
    return true;
}

bool EditorApp::PlacePalettePrefab(const PrefabPalettePanel::Placement& placement,
                                   const glm::vec3& position) {
    if (placement.path.empty()) return false;
    PrefabAsset prefab;
    std::string error;
    if (!prefab.Load(placement.path, &error)) {
        m_log.Error("Prefab Palette: " + error);
        return false;
    }
    AddPrefabToScene(prefab, position, placement.path);
    if (!m_scene.SelectedObject()) return false;

    engine::ecs::Transform transform = *m_scene.SelectedTransform();
    transform.position = position;
    transform.rotation = glm::angleAxis(glm::radians(placement.yawDegrees),
                                         glm::vec3(0, 1, 0));
    transform.scale *= placement.uniformScale;
    m_scene.SetSelectedTransform(transform);

    const int selectedIndex = m_scene.SelectedIndex();
    std::string uniqueName = placement.name;
    int suffix = 2;
    auto nameExists = [&](const std::string& candidate) {
        for (int i = 0; i < static_cast<int>(m_scene.Objects().size()); ++i) {
            if (i != selectedIndex && m_scene.Objects()[static_cast<std::size_t>(i)].name == candidate)
                return true;
        }
        return false;
    };
    while (nameExists(uniqueName)) uniqueName = placement.name + "_" + std::to_string(suffix++);
    m_scene.SetSelectedName(uniqueName);
    m_log.Info("Prefab Palette placed: " + uniqueName);
    return true;
}

void EditorApp::ReplaceSelectionWithPalettePrefab() {
    if (!m_scene.SelectedObject() || m_scene.SelectedLocked()) {
        m_log.Warning("Prefab Palette: select an unlocked scene object first");
        return;
    }
    const PrefabPalettePanel::Placement placement = m_prefabPalette.NextPlacement();
    if (placement.path.empty()) return;
    PrefabAsset prefab;
    std::string error;
    if (!prefab.Load(placement.path, &error)) {
        m_log.Error("Prefab Palette: " + error);
        return;
    }
    const engine::Mesh* mesh = m_cube ? &*m_cube : nullptr;
    switch (prefab.object.primitive) {
    case EditorScene::Primitive::Plane: if (m_plane) mesh = &*m_plane; break;
    case EditorScene::Primitive::Sphere: if (m_sphere) mesh = &*m_sphere; break;
    case EditorScene::Primitive::Capsule: if (m_capsule) mesh = &*m_capsule; break;
    case EditorScene::Primitive::Cylinder: if (m_cylinder) mesh = &*m_cylinder; break;
    case EditorScene::Primitive::Cone: if (m_cone) mesh = &*m_cone; break;
    case EditorScene::Primitive::Pyramid: if (m_pyramid) mesh = &*m_pyramid; break;
    case EditorScene::Primitive::Torus: if (m_torus) mesh = &*m_torus; break;
    case EditorScene::Primitive::Staircase: if (m_staircase) mesh = &*m_staircase; break;
    default: break;
    }
    if (mesh) m_scene.SetSelectedPrimitive(prefab.object.primitive, *mesh);
    if (prefab.Apply(m_scene)) {
        m_scene.SetSelectedPrefabAssetPath(placement.path, prefab.assetId);
        m_log.Info("Prefab Palette replaced selection with: " + placement.name);
    } else {
        m_log.Warning("Prefab Palette could not replace the selected object");
    }
}

void EditorApp::DrawRoomBuilderPanel() {
    if (!m_panels.IsOpen(EditorPanels::Panel::RoomBuilder)) return;
    bool open = true;
    const RoomBuilderPanel::Result result =
        m_roomBuilder.Draw(m_project.AssetRoot(), &open);
    m_panels.SetOpen(EditorPanels::Panel::RoomBuilder, open);
    if (result.deleteExistingRequested) {
        const int removed = DeleteGeneratedRoom(m_roomBuilder.RoomName());
        m_log.Info("Room Builder removed " + std::to_string(removed) + " piece(s)");
    }
    if (result.generateRequested) GenerateRoom();
}

void EditorApp::DrawProceduralBuildingPanel() {
    if (!m_panels.IsOpen(EditorPanels::Panel::ProceduralBuilding)) return;
    bool open = true;
    const ProceduralBuildingPanel::Result result =
        m_proceduralBuilding.Draw(m_project.AssetRoot(), &open);
    m_panels.SetOpen(EditorPanels::Panel::ProceduralBuilding, open);
    if (result.assetListChanged) {
        std::string error;
        if (!m_assets.Refresh(m_project.AssetRoot(), &error)) m_log.Warning(error);
    }
    if (result.deleteExistingRequested) {
        const int removed = DeleteGeneratedProceduralBuilding(
            m_proceduralBuilding.BuildingName());
        m_log.Info("Procedural Building removed " + std::to_string(removed) + " piece(s)");
    }
    if (result.generateRequested) GenerateProceduralBuilding();
}

void EditorApp::DrawRoadGeneratorPanel() {
    if (!m_panels.IsOpen(EditorPanels::Panel::RoadGenerator)) return;
    bool open = true;
    const RoadGeneratorPanel::Result result =
        m_roadGenerator.Draw(m_scene, m_project.AssetRoot(), &open);
    m_panels.SetOpen(EditorPanels::Panel::RoadGenerator, open);
    if (result.assetsChanged) {
        std::string error;
        if (!m_assets.Refresh(m_project.AssetRoot(), &error)) m_log.Warning(error);
    }
    if (result.remove) {
        const int removed = DeleteGeneratedRoad(m_roadGenerator.Name());
        m_log.Info("Road Generator removed " + std::to_string(removed) + " piece(s)");
    }
    if (result.generate) GenerateRoad();
}

void EditorApp::DrawLevelInstancePanel() {
    if (!m_panels.IsOpen(EditorPanels::Panel::LevelInstances)) return;
    bool open = true;
    const auto result = m_levelInstances.Draw(
        m_worldAuthoring, m_project.AssetRoot(), m_project.ScenePath(),
        static_cast<int>(m_scene.SelectedIndices().size()), &open);
    m_panels.SetOpen(EditorPanels::Panel::LevelInstances, open);

    if (m_worldAuthoringPath.empty())
        m_worldAuthoringPath = (std::filesystem::path(m_project.AssetRoot())
            / "GameAssets" / "Worlds" / "World.3dgworld").string();
    if (m_worldAuthoring.persistentScenePath.empty())
        m_worldAuthoring.persistentScenePath = m_project.ScenePath();
    if (!m_worldAuthoring.id.Valid()) m_worldAuthoring.id = engine::AssetHandle::Generate();

    if (result.createFromSelection) {
        const std::vector<int> selected = m_scene.SelectedIndices();
        const EditorScene::Snapshot source = m_scene.CreateSnapshot();
        EditorScene::Snapshot isolated;
        isolated.environment = source.environment;
        isolated.gameMode = source.gameMode;
        glm::vec3 pivot(0.0f); int valid = 0;
        for (int index : selected) if (index >= 0 && index < static_cast<int>(source.objects.size())) {
            pivot += source.objects[static_cast<std::size_t>(index)].transform.position; ++valid;
        }
        if (valid > 0) pivot /= static_cast<float>(valid);
        std::unordered_set<std::string> names;
        for (int index : selected) if (index >= 0 && index < static_cast<int>(source.objects.size())) {
            auto item = source.objects[static_cast<std::size_t>(index)];
            names.insert(item.object.name); item.transform.position -= pivot;
            isolated.objects.push_back(std::move(item));
        }
        for (const auto& joint : source.joints)
            if (names.count(joint.objectA) && (joint.worldAnchor || names.count(joint.objectB)))
                isolated.joints.push_back(joint);
        isolated.selectedIndex = isolated.objects.empty() ? -1 : 0;
        std::error_code ec;
        std::filesystem::create_directories(
            std::filesystem::path(result.selectionScenePath).parent_path(), ec);
        std::string error;
        EditorScene level;
        if (ec || isolated.objects.empty()) {
            m_log.Warning(ec ? "Level Instance: could not create the level folder"
                             : "Level Instance: select at least one object");
        } else {
            level.RestoreFromSnapshot(isolated, *m_cube, *m_plane, *m_sphere, *m_capsule,
                *m_cylinder, *m_cone, *m_pyramid, *m_torus, *m_staircase);
            if (!level.Save(result.selectionScenePath, &error)) {
                m_log.Error("Level Instance export failed: " + error);
            } else {
                engine::LevelRef instance; instance.scenePath = result.selectionScenePath;
                instance.worldTransform = glm::translate(glm::mat4(1.0f), pivot);
                m_worldAuthoring.levels.push_back(instance);
                if (result.removeSelection) { m_scene.SelectIndices(selected); m_scene.DeleteSelected(); }
                std::string refreshError;
                if (!m_assets.Refresh(m_project.AssetRoot(), &refreshError)) m_log.Warning(refreshError);
                m_log.Info("Created linked level instance: " + result.selectionScenePath);
            }
        }
    }

    if (result.openSource >= 0 && result.openSource < static_cast<int>(m_worldAuthoring.levels.size()))
        RequestLoadSceneFromPath(m_worldAuthoring.levels[static_cast<std::size_t>(result.openSource)].scenePath);

    if (result.breakInstance >= 0 && result.breakInstance < static_cast<int>(m_worldAuthoring.levels.size())) {
        const engine::LevelRef instance = m_worldAuthoring.levels[static_cast<std::size_t>(result.breakInstance)];
        EditorScene linked; std::string error;
        if (!linked.Load(instance.scenePath, *m_cube, *m_plane, *m_sphere, *m_capsule,
                         *m_cylinder, *m_cone, *m_pyramid, *m_torus, *m_staircase, &error)) {
            m_log.Error("Break level instance failed: " + error);
        } else {
            EditorScene::Snapshot destination = m_scene.CreateSnapshot();
            EditorScene::Snapshot addition = linked.CreateSnapshot();
            std::unordered_set<std::string> names;
            for (const auto& item : destination.objects) names.insert(item.object.name);
            std::unordered_map<std::string, std::string> renamed;
            for (auto& item : addition.objects) {
                const std::string oldName=item.object.name; std::string unique=oldName; int suffix=2;
                while(names.count(unique))unique=oldName+"_"+std::to_string(suffix++);
                names.insert(unique);renamed[oldName]=unique;item.object.name=unique;
                const glm::mat4 placed=instance.worldTransform*item.transform.Model();
                glm::vec3 skew,translation,scale;glm::vec4 perspective;glm::quat rotation;
                if(glm::decompose(placed,scale,rotation,translation,skew,perspective)){
                    item.transform.position=translation;item.transform.rotation=glm::normalize(rotation);item.transform.scale=scale;
                }
                destination.objects.push_back(std::move(item));
            }
            for(auto joint:addition.joints){if(renamed.count(joint.objectA))joint.objectA=renamed[joint.objectA];if(renamed.count(joint.objectB))joint.objectB=renamed[joint.objectB];destination.joints.push_back(std::move(joint));}
            m_scene.ApplySnapshotUndoable(destination,*m_cube,*m_plane,*m_sphere,*m_capsule,*m_cylinder,*m_cone,*m_pyramid,*m_torus,*m_staircase);
            m_worldAuthoring.levels.erase(m_worldAuthoring.levels.begin()+result.breakInstance);
            m_log.Info("Broke level instance into editable scene objects");
        }
    }

    if (result.saveWorld) {
        std::error_code ec; std::filesystem::create_directories(std::filesystem::path(m_worldAuthoringPath).parent_path(),ec);
        std::string error;
        if(!ec&&engine::SaveWorldManifest(m_worldAuthoringPath,m_worldAuthoring,&error)){
            m_log.Info("Saved world instances: "+m_worldAuthoringPath);
            std::string refreshError;if(!m_assets.Refresh(m_project.AssetRoot(),&refreshError))m_log.Warning(refreshError);
        } else m_log.Error("World save failed: "+(ec?ec.message():error));
    }
}

bool EditorApp::CreatePartitionCellFromSelection(const std::string& path, int cellX, int cellZ) {
    const std::vector<int> selected = m_scene.SelectedIndices();
    const EditorScene::Snapshot source = m_scene.CreateSnapshot();
    EditorScene::Snapshot isolated;
    isolated.environment = source.environment;
    isolated.gameMode = source.gameMode;
    glm::vec3 pivot(0.0f); int valid = 0;
    for (int index : selected) if (index >= 0 && index < static_cast<int>(source.objects.size())) {
        pivot += source.objects[static_cast<std::size_t>(index)].transform.position; ++valid;
    }
    if (valid == 0) { m_log.Warning("World Partition: select one or more loose actors"); return false; }
    pivot /= static_cast<float>(valid);
    std::unordered_set<std::string> names;
    for (int index : selected) if (index >= 0 && index < static_cast<int>(source.objects.size())) {
        auto item = source.objects[static_cast<std::size_t>(index)];
        names.insert(item.object.name); item.transform.position -= pivot;
        isolated.objects.push_back(std::move(item));
    }
    for (const auto& joint : source.joints)
        if (names.count(joint.objectA) && (joint.worldAnchor || names.count(joint.objectB)))
            isolated.joints.push_back(joint);
    isolated.selectedIndex = 0;
    std::error_code ec; std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    if (ec) { m_log.Error("World Partition could not create the cell folder: " + ec.message()); return false; }
    EditorScene cellScene;
    cellScene.RestoreFromSnapshot(isolated,*m_cube,*m_plane,*m_sphere,*m_capsule,
        *m_cylinder,*m_cone,*m_pyramid,*m_torus,*m_staircase);
    std::string error;
    if (!cellScene.Save(path,&error)) { m_log.Error("World Partition cell export failed: "+error); return false; }
    engine::LevelRef level; level.scenePath=path;level.worldTransform=glm::translate(glm::mat4(1),pivot);
    level.partitionX=cellX;level.partitionZ=cellZ;level.loadRadius=m_worldAuthoring.partition.defaultLoadRadius;
    level.unloadRadius=m_worldAuthoring.partition.defaultUnloadRadius;
    m_worldAuthoring.levels.push_back(level);
    m_scene.SelectIndices(selected);m_scene.DeleteSelected();
    std::string refreshError;if(!m_assets.Refresh(m_project.AssetRoot(),&refreshError))m_log.Warning(refreshError);
    m_log.Info("Created partition cell level: "+path);return true;
}

void EditorApp::DrawWorldPartitionPanel() {
    if (!m_panels.IsOpen(EditorPanels::Panel::WorldPartition)) return;
    bool open=true;
    const auto result=m_worldPartition.Draw(m_worldAuthoring,m_project.AssetRoot(),
        static_cast<int>(m_scene.SelectedIndices().size()),&open);
    m_panels.SetOpen(EditorPanels::Panel::WorldPartition,open);
    if(m_worldAuthoringPath.empty())m_worldAuthoringPath=(std::filesystem::path(m_project.AssetRoot())/"GameAssets"/"Worlds"/"World.3dgworld").string();
    if(m_worldAuthoring.persistentScenePath.empty())m_worldAuthoring.persistentScenePath=m_project.ScenePath();
    if(!m_worldAuthoring.id.Valid())m_worldAuthoring.id=engine::AssetHandle::Generate();
    if(result.createCellFromSelection)CreatePartitionCellFromSelection(result.cellScenePath,result.cellX,result.cellZ);
    if(result.saveWorld){std::error_code ec;std::filesystem::create_directories(std::filesystem::path(m_worldAuthoringPath).parent_path(),ec);std::string error;
        if(!ec&&engine::SaveWorldManifest(m_worldAuthoringPath,m_worldAuthoring,&error)){m_log.Info("Saved partitioned world: "+m_worldAuthoringPath);std::string refreshError;if(!m_assets.Refresh(m_project.AssetRoot(),&refreshError))m_log.Warning(refreshError);}
        else m_log.Error("World partition save failed: "+(ec?ec.message():error));}
}

void EditorApp::DrawProceduralScatterGraphPanel() {
    if (!m_panels.IsOpen(EditorPanels::Panel::ProceduralScatterGraph)) return;
    bool open = true;
    auto result = m_proceduralScatterGraph.Draw(m_project.AssetRoot(), &open);
    m_panels.SetOpen(EditorPanels::Panel::ProceduralScatterGraph, open);
    if (result.refreshAssets) {
        std::string error;
        if (!m_assets.Refresh(m_project.AssetRoot(), &error)) m_log.Warning(error);
        else m_log.Info("Saved scatter graph: " + m_proceduralScatterGraph.Path());
    }
    if (!result.bakeRequested || !m_cube) return;

    const auto surface = [this](float x, float z) {
        engine::ScatterSurfaceSample sample;
        bool center = false, left = false, right = false, back = false, front = false;
        sample.height = TerrainSurfaceY(x, z, center);
        if (!center) { sample.height = 0.0f; sample.normal = {0, 1, 0}; return sample; }
        const float step = 0.25f;
        const float yl = TerrainSurfaceY(x-step,z,left), yr = TerrainSurfaceY(x+step,z,right);
        const float yb = TerrainSurfaceY(x,z-step,back), yf = TerrainSurfaceY(x,z+step,front);
        if(left&&right&&back&&front)
            sample.normal=glm::normalize(glm::vec3(yl-yr,2.0f*step,yb-yf));
        return sample;
    };
    const std::vector<engine::ScatterPlacement> placements =
        engine::EvaluateScatterGraph(m_proceduralScatterGraph.Graph(), surface);
    if (placements.empty()) { m_log.Warning("Scatter graph produced no placements."); return; }

    if (result.bakeTarget == ProceduralScatterGraphPanel::BakeTarget::EditableObjects) {
        bool first = true; int placed = 0;
        for (const auto& placement : placements) {
            engine::ecs::Transform transform;
            transform.position = placement.position;
            transform.rotation = placement.rotation;
            transform.scale = placement.scale;
            m_scene.SuppressUndo(!first);
            if (m_scene.AddModel(placement.meshPath, *m_cube, transform)) {
                first = false; ++placed;
            }
        }
        m_scene.SuppressUndo(false);
        m_log.Info("Scatter graph baked " + std::to_string(placed)
            + " editable object(s).");
        return;
    }

    engine::FoliageAssetData palette;
    // SaveFoliageAsset receives the asset by value, so generate the identity here
    // as well. The scene actor must retain the exact ID written to the palette.
    palette.header.id = engine::AssetHandle::Generate();
    palette.name = m_proceduralScatterGraph.Graph().name + " Baked Foliage";
    std::unordered_map<std::string, std::uint32_t> typeByPath;
    for (const auto& placement : placements) {
        if (typeByPath.count(placement.meshPath)) continue;
        const std::uint32_t index = static_cast<std::uint32_t>(palette.types.size());
        typeByPath[placement.meshPath] = index;
        engine::FoliageTypeAsset type;
        type.name = std::filesystem::path(placement.meshPath).stem().string();
        type.meshPath = placement.meshPath; type.meshId = placement.meshId;
        palette.types.push_back(std::move(type));
    }
    std::filesystem::path palettePath(m_proceduralScatterGraph.Path());
    palettePath.replace_extension(".3dgfoliage");
    std::string error;
    if (!engine::SaveFoliageAsset(palettePath.string(), palette, &error)) {
        m_log.Error("Scatter foliage palette save failed: " + error); return;
    }
    m_scene.AddFoliage(*m_cube);
    m_scene.SetSelectedFoliageAsset(palettePath.string(), palette.header.id);
    int placed = 0;
    for (const auto& placement : placements) {
        const glm::vec3 degrees = glm::degrees(glm::eulerAngles(placement.rotation));
        if (m_scene.AddSelectedFoliageInstance(placement.position, degrees,
                placement.scale, typeByPath[placement.meshPath])) ++placed;
    }
    m_editAssets.ResolveRegistryAssets(m_scene.Registry());
    if (!m_assets.Refresh(m_project.AssetRoot(), &error)) m_log.Warning(error);
    m_log.Info("Scatter graph baked " + std::to_string(placed)
        + " foliage instance(s) in one batched actor.");
}

void EditorApp::DrawBiomeEditorPanel() {
    if (!m_panels.IsOpen(EditorPanels::Panel::BiomeEditor)) return;
    bool open = true;
    const BiomeEditorPanel::Result result = m_biomeEditor.Draw(
        m_assets, m_project.AssetRoot(), &open);
    m_panels.SetOpen(EditorPanels::Panel::BiomeEditor, open);
    if (result.saved) {
        std::string error;
        if (!m_assets.Refresh(m_project.AssetRoot(), &error)) m_log.Warning(error);
    }
    if (!result.message.empty()) m_log.Info(result.message);
    if (!result.applyRequested || !m_cube || !m_plane) return;

    if (m_biomeEditor.Path().empty()) {
        m_log.Warning("Save the biome asset before applying it to a landscape.");
        return;
    }

    const EditorScene::Object* selected = m_scene.SelectedObject();
    if (!selected || !selected->isTerrain) {
        m_log.Warning("Biome application requires a selected landscape.");
        return;
    }
    const engine::BiomeAssetData& biome = m_biomeEditor.Biome();
    const std::string terrainName = selected->name;
    const int terrainResolution = selected->terrainRes;
    const float terrainSize = selected->terrainSize;
    const float terrainMaxHeight = std::max(selected->terrainMaxHeight, 0.001f);
    const engine::ecs::Transform terrainTransform = *m_scene.SelectedTransform();

    for (int layer = 0; layer < 5; ++layer) {
        const std::string material = layer < static_cast<int>(biome.layers.size())
            ? biome.layers[static_cast<std::size_t>(layer)].materialPath : std::string{};
        m_scene.SetSelectedTerrainLayerMaterial(layer + 1, material);
    }

    if (!biome.weatherPath.empty()) {
        engine::WeatherAssetData weather;
        std::string error;
        if (engine::LoadWeatherAsset(biome.weatherPath, &weather, &error)) {
            auto environment = m_scene.GetEnvironment();
            environment.timeOfDay = weather.timeOfDay;
            environment.skyLightIntensity = weather.skyLightIntensity;
            environment.sunIntensity = weather.sunIntensity;
            environment.clouds = weather.clouds;
            environment.cloudCoverage = weather.cloudCoverage;
            environment.cloudDensity = weather.cloudDensity;
            environment.cloudScale = weather.cloudScale;
            environment.cloudSoftness = weather.cloudSoftness;
            environment.cloudWindSpeed = weather.cloudWindSpeed;
            environment.cloudWindDirection = weather.cloudWindDirection;
            environment.cloudColor = weather.cloudColor;
            environment.cloudShadows = weather.cloudShadows;
            environment.cloudShadowStrength = weather.cloudShadowStrength;
            environment.fog = weather.fog;
            environment.fogColor = weather.fogColor;
            environment.fogDensity = weather.fogDensity;
            environment.fogHeight = weather.fogHeight;
            environment.fogHeightFalloff = weather.fogHeightFalloff;
            m_scene.SetEnvironment(environment);
        } else m_log.Warning("Biome weather could not be loaded: " + error);
    }

    const std::string prefix = "Biome_" + biome.name;
    std::vector<int> oldGenerated;
    for (int i = 0; i < static_cast<int>(m_scene.Objects().size()); ++i) {
        const std::string& name = m_scene.Objects()[static_cast<std::size_t>(i)].name;
        if (name == prefix + "_Foliage" || name == prefix + "_Water"
            || name == prefix + "_Particles" || name == prefix + "_Audio")
            oldGenerated.push_back(i);
    }
    if (!oldGenerated.empty()) { m_scene.SelectIndices(oldGenerated); m_scene.DeleteSelected(); }
    for (int i = 0; i < static_cast<int>(m_scene.Objects().size()); ++i)
        if (m_scene.Objects()[static_cast<std::size_t>(i)].name == terrainName) {
            m_scene.SelectIndex(i); break;
        }

    const glm::vec3 center(terrainTransform.position.x + terrainSize * 0.5f,
                           terrainTransform.position.y,
                           terrainTransform.position.z + terrainSize * 0.5f);
    const auto surface = [this, &biome, terrainTransform, terrainMaxHeight](float x, float z) {
        engine::BiomeSurfaceSample sample;
        bool centerHit = false, left = false, right = false, back = false, front = false;
        const float worldHeight = TerrainSurfaceY(x, z, centerHit);
        sample.height = worldHeight - terrainTransform.position.y;
        const float step = 0.25f;
        const float yl = TerrainSurfaceY(x-step,z,left), yr = TerrainSurfaceY(x+step,z,right);
        const float yb = TerrainSurfaceY(x,z-step,back), yf = TerrainSurfaceY(x,z+step,front);
        if (centerHit && left && right && back && front)
            sample.normal = glm::normalize(glm::vec3(yl-yr, 2.0f*step, yb-yf));
        sample.normalizedHeight = std::clamp(
            (worldHeight - terrainTransform.position.y) / terrainMaxHeight, 0.0f, 1.0f);
        const float moistureNoise = std::sin(x * 0.071f + z * 0.043f) * 0.15f;
        const float temperatureNoise = std::cos(x * 0.037f - z * 0.061f) * 0.12f;
        sample.moisture = std::clamp(biome.moisture + moistureNoise, 0.0f, 1.0f);
        sample.temperature = std::clamp(biome.temperature + temperatureNoise, 0.0f, 1.0f);
        return sample;
    };
    engine::BiomeAssetData evaluated = biome;
    evaluated.previewWorldSize = terrainSize;
    const auto placements = engine::EvaluateBiome(evaluated, surface, center);

    if (!placements.empty()) {
        engine::FoliageAssetData palette;
        palette.header.id = engine::AssetHandle::Generate();
        palette.name = biome.name + " Biome Foliage";
        std::unordered_map<std::size_t, std::uint32_t> typeByRule;
        for (const auto& placement : placements) {
            if (typeByRule.count(placement.foliageRule)) continue;
            const auto& rule = biome.foliage[placement.foliageRule];
            const std::uint32_t typeIndex = static_cast<std::uint32_t>(palette.types.size());
            typeByRule[placement.foliageRule] = typeIndex;
            engine::FoliageTypeAsset type;
            type.name = rule.name; type.meshPath = rule.meshPath; type.meshId = rule.meshId;
            type.castShadows = rule.castShadows;
            palette.types.push_back(std::move(type));
        }
        std::filesystem::path palettePath = m_biomeEditor.Path();
        palettePath.replace_extension(".3dgfoliage");
        std::string error;
        if (engine::SaveFoliageAsset(palettePath.string(), palette, &error)) {
            m_scene.AddFoliage(*m_cube);
            m_scene.SetSelectedName(prefix + "_Foliage");
            m_scene.SetSelectedFoliageAsset(palettePath.string(), palette.header.id);
            m_scene.SetSelectedFoliageTerrainOwner(terrainName);
            for (const auto& placement : placements)
                m_scene.AddSelectedFoliageInstance(placement.position,
                    glm::degrees(glm::eulerAngles(placement.rotation)), placement.scale,
                    typeByRule[placement.foliageRule]);
        } else m_log.Warning("Biome foliage save failed: " + error);
    }

    if (biome.waterEnabled) {
        AddWater(1, false);
        m_scene.SetSelectedName(prefix + "_Water");
        engine::ecs::Transform waterTransform;
        waterTransform.position = {center.x, terrainTransform.position.y + biome.waterLevel, center.z};
        m_scene.SetSelectedTransform(waterTransform);
        m_scene.SetSelectedWater(terrainSize, std::clamp(terrainResolution, 32, 256),
            waterTransform.position.y, {0.13f,0.42f,0.38f}, {0.02f,0.12f,0.14f},
            {0.52f,0.66f,0.70f}, 0.66f, 5.0f, 0.5f, 320.0f);
        if (!biome.waterMaterialPath.empty())
            m_scene.SetSelectedMaterialAsset(biome.waterMaterialPath, biome.waterMaterialId);
    }
    if (!biome.particlePath.empty()) {
        engine::ParticleSystemComponent particles; std::string error;
        if (particle_asset::Load(biome.particlePath, &particles, &error)) {
            engine::ecs::Transform transform; transform.position = center;
            m_scene.AddParticleSystem(*m_cube, transform, biome.particlePath, particles);
            m_scene.SetSelectedName(prefix + "_Particles");
        } else m_log.Warning("Biome particles could not be loaded: " + error);
    }
    if (!biome.ambientAudioPath.empty()) {
        m_scene.AddEmpty(*m_cube); m_scene.SetSelectedName(prefix + "_Audio");
        engine::ecs::Transform transform; transform.position = center;
        m_scene.SetSelectedTransform(transform);
        m_scene.SetSelectedAudioSource(true, biome.ambientAudioPath, 1.0f, 1.0f,
            true, true, true, 2.0f, terrainSize, 1.0f, engine::AudioBus::Ambient);
    }
    m_editAssets.ResolveRegistryAssets(m_scene.Registry());
    std::string refreshError;
    if (!m_assets.Refresh(m_project.AssetRoot(), &refreshError)) m_log.Warning(refreshError);
    m_log.Info("Applied biome '" + biome.name + "' to landscape '" + terrainName
        + "' with " + std::to_string(placements.size()) + " foliage instances.");
}

void EditorApp::DrawDayNightTimelinePanel() {
    if (!m_panels.IsOpen(EditorPanels::Panel::DayNightTimeline)) return;
    bool open = true;
    const auto result = m_dayNightTimeline.Draw(
        m_scene, m_assets, m_project.AssetRoot(), &open);
    m_panels.SetOpen(EditorPanels::Panel::DayNightTimeline, open);
    if (result.saved) {
        std::string error;
        if (!m_assets.Refresh(m_project.AssetRoot(), &error)) m_log.Warning(error);
    }
    if (!result.message.empty()) m_log.Info(result.message);
}

void EditorApp::DrawCaveTunnelPanel() {
    if (!m_panels.IsOpen(EditorPanels::Panel::CaveTunnel)) return;
    bool open = true;
    const CaveTunnelPanel::Result result = m_caveTunnel.Draw(
        m_scene, m_assets, m_project.AssetRoot(), &open);
    m_panels.SetOpen(EditorPanels::Panel::CaveTunnel, open);
    if (result.build) GenerateCaveTunnel();
    if (result.remove) {
        const int removed = DeleteGeneratedCaveTunnel(m_caveTunnel.Cave().name);
        m_log.Info("Removed " + std::to_string(removed) + " generated cave objects");
    }
    if (result.saved) {
        std::string error;
        if (!m_assets.Refresh(m_project.AssetRoot(), &error)) m_log.Warning(error);
    }
    if (!result.message.empty()) m_log.Info(result.message);
}

int EditorApp::DeleteGeneratedCaveTunnel(const std::string& caveName) {
    if (caveName.empty()) return 0;
    const std::string prefix = "Cave_" + caveName + "_";
    std::vector<int> indices;
    for (int i = 0; i < static_cast<int>(m_scene.Objects().size()); ++i)
        if (m_scene.Objects()[static_cast<std::size_t>(i)].name.rfind(prefix, 0) == 0)
            indices.push_back(i);
    if (indices.empty()) return 0;
    m_scene.SelectIndices(indices);
    return m_scene.DeleteSelected() ? static_cast<int>(indices.size()) : 0;
}

void EditorApp::GenerateCaveTunnel() {
    if (!m_cube) return;
    engine::CaveAssetData cave = m_caveTunnel.Cave();
    std::string error;
    if (cave.terrainEntrances && !cave.closed && cave.points.size() >= 2) {
        bool overTerrain = false;
        float y = TerrainSurfaceY(cave.points.front().x, cave.points.front().z, overTerrain);
        if (overTerrain) cave.points.front().y = y - cave.height * 0.15f;
        y = TerrainSurfaceY(cave.points.back().x, cave.points.back().z, overTerrain);
        if (overTerrain) cave.points.back().y = y - cave.height * 0.15f;
    }
    engine::NormalizeCaveAsset(cave);
    if (!engine::ValidateCaveAsset(cave, &error)) {
        m_log.Warning("Cave build failed: " + error); return;
    }
    if (m_caveTunnel.Path().empty()) {
        m_log.Warning("Save the cave asset before building it"); return;
    }

    engine::StaticMeshAssetData mesh;
    engine::CaveGenerationStats stats;
    if (!engine::BuildCaveStaticMesh(cave, &mesh, &stats, &error)) {
        m_log.Warning("Cave build failed: " + error); return;
    }
    std::filesystem::path meshPath(m_caveTunnel.Path());
    meshPath.replace_filename(meshPath.stem().string() + "_Baked.3dgmesh");
    engine::StaticMeshAssetData existing;
    if (engine::LoadStaticMeshAsset(meshPath.string(), &existing, nullptr)
        && existing.header.id.Valid()) mesh.header.id = existing.header.id;
    if (!engine::SaveStaticMeshAsset(meshPath.string(), mesh, &error)) {
        m_log.Warning("Cave mesh save failed: " + error); return;
    }
    cave.bakedMeshPath = meshPath.string();
    cave.bakedMeshId = mesh.header.id;
    if (!engine::SaveCaveAsset(m_caveTunnel.Path(), cave, &error)) {
        m_log.Warning("Cave asset update failed: " + error); return;
    }

    DeleteGeneratedCaveTunnel(cave.name);
    engine::ecs::Transform identity;
    if (!m_scene.AddModel(meshPath.string(), *m_cube, identity)) {
        m_log.Warning("Could not add the baked cave mesh to the level"); return;
    }
    m_scene.SetSelectedName("Cave_" + cave.name + "_Interior");
    if (!cave.wallMaterialPath.empty())
        m_scene.SetSelectedMaterialAsset(cave.wallMaterialPath, cave.wallMaterialId);

    const engine::Spline curve(cave.points, cave.closed);
    const float length = curve.Length();
    const int spans = std::max(1, static_cast<int>(std::ceil(
        length / std::max(cave.sampleSpacing * 2.0f, 0.5f))));
    engine::ecs::Collider box = engine::ecs::Collider::MakeBox(glm::vec3(0.5f));
    box.layer = engine::ecs::CollisionLayer::WorldStatic;
    box.mask = engine::ecs::CollisionLayer::All;
    bool firstPiece = true;
    int colliderCount = 0;
    auto addCollider = [&](const std::string& suffix, const glm::vec3& position,
                           const glm::vec3& scale, const glm::quat& rotation) {
        m_scene.SuppressUndo(!firstPiece);
        engine::ecs::Transform transform;
        transform.position = position; transform.scale = scale; transform.rotation = rotation;
        m_scene.AddConfiguredPrimitive(EditorScene::Primitive::Cube, *m_cube, transform,
            &box, "Cave_" + cave.name + "_Collision_" + suffix);
        m_scene.ToggleSelectVisible();
        if (firstPiece) { firstPiece = false; m_scene.SuppressUndo(true); }
        ++colliderCount;
    };
    for (int i = 0; i < spans; ++i) {
        const float d0 = length * static_cast<float>(i) / spans;
        const float d1 = length * static_cast<float>(i + 1) / spans;
        const glm::vec3 a = curve.PositionAtDistance(d0);
        const glm::vec3 b = curve.PositionAtDistance(d1);
        const glm::vec3 center = (a + b) * 0.5f;
        glm::vec3 forward = b - a;
        const float spanLength = std::max(glm::length(forward), 0.05f);
        forward /= spanLength;
        glm::vec3 up(0.0f, 1.0f, 0.0f);
        if (std::abs(glm::dot(up, forward)) > 0.98f) up = glm::vec3(1,0,0);
        const glm::vec3 right = glm::normalize(glm::cross(up, forward));
        up = glm::normalize(glm::cross(forward, right));
        const glm::quat rotation = glm::quat_cast(glm::mat3(right, up, forward));
        const float t = std::max(cave.wallThickness, 0.05f);
        const std::string n = std::to_string(i);
        if (cave.createNavigation || cave.createCollision)
            addCollider(n + "_Floor", center - up * (cave.height * 0.5f),
                {cave.width, t, spanLength + t}, rotation);
        if (cave.createCollision) {
            addCollider(n + "_Ceiling", center + up * (cave.height * 0.5f),
                {cave.width, t, spanLength + t}, rotation);
            addCollider(n + "_Left", center - right * (cave.width * 0.5f),
                {t, cave.height, spanLength + t}, rotation);
            addCollider(n + "_Right", center + right * (cave.width * 0.5f),
                {t, cave.height, spanLength + t}, rotation);
        }
    }
    m_scene.SuppressUndo(false);
    m_editAssets.ResolveRegistryAssets(m_scene.Registry());
    std::string refreshError;
    if (!m_assets.Refresh(m_project.AssetRoot(), &refreshError)) m_log.Warning(refreshError);
    m_log.Info("Built cave '" + cave.name + "': " + std::to_string(stats.triangles)
        + " triangles, " + std::to_string(colliderCount) + " hidden collision pieces");
}

int EditorApp::DeleteGeneratedRoad(const std::string& roadName) {
    if (roadName.empty()) return 0;
    const std::string prefix = "Road_" + roadName + "_";
    std::vector<int> indices;
    for (int i = 0; i < static_cast<int>(m_scene.Objects().size()); ++i)
        if (m_scene.Objects()[static_cast<std::size_t>(i)].name.rfind(prefix, 0) == 0)
            indices.push_back(i);
    if (indices.empty()) return 0;
    m_scene.SelectIndices(indices);
    return m_scene.DeleteSelected() ? static_cast<int>(indices.size()) : 0;
}

void EditorApp::GenerateRoad() {
    if (!m_cube) return;
    const EditorScene::Object* splineObject = nullptr;
    for (const EditorScene::Object& object : m_scene.Objects())
        if (object.isSpline && object.name == m_roadGenerator.SplineName()
            && object.splinePoints.size() >= 2) { splineObject = &object; break; }
    if (!splineObject) { m_log.Warning("Road Generator: selected spline is unavailable"); return; }
    const std::string roadName = m_roadGenerator.Name();
    if (roadName.empty()) return;
    std::vector<RoadGeneratorPanel::Part> parts =
        m_roadGenerator.GenerateParts(splineObject->splinePoints, splineObject->splineClosed);
    if (parts.empty()) { m_log.Warning("Road Generator produced no geometry"); return; }
    if (m_roadGenerator.ReplaceExisting()) DeleteGeneratedRoad(roadName);

    const engine::Spline curve(splineObject->splinePoints, splineObject->splineClosed);
    engine::ecs::Collider box = engine::ecs::Collider::MakeBox(glm::vec3(.5f));
    box.layer = engine::ecs::CollisionLayer::WorldStatic;
    box.mask = engine::ecs::CollisionLayer::All;
    bool first = true;
    int created = 0;
    for (RoadGeneratorPanel::Part& part : parts) {
        if (m_roadGenerator.ConformTerrain()) {
            const glm::vec3 curvePoint = curve.ClosestPoint(part.position);
            bool overTerrain = false;
            const float terrainY = TerrainSurfaceY(part.position.x, part.position.z, overTerrain);
            if (overTerrain) part.position.y += terrainY + m_roadGenerator.TerrainOffset() - curvePoint.y;
        }
        m_scene.SuppressUndo(!first);
        engine::ecs::Transform transform;
        transform.position = part.position;
        transform.scale = part.scale;
        transform.rotation = part.rotation;
        const bool collidable = m_roadGenerator.CreateColliders()
            && part.surface != RoadGeneratorPanel::Surface::Marking;
        m_scene.AddConfiguredPrimitive(EditorScene::Primitive::Cube, *m_cube,
            transform, collidable ? &box : nullptr,
            "Road_" + roadName + "_" + part.suffix);
        if (first) { first = false; m_scene.SuppressUndo(true); }
        const std::string& material = m_roadGenerator.MaterialFor(part.surface);
        if (!material.empty()) m_scene.SetSelectedMaterialAsset(material);
        ++created;
    }
    m_scene.SuppressUndo(false);
    m_log.Info("Road Generator created " + roadName + " ("
        + std::to_string(created) + " editable pieces)");
}

int EditorApp::DeleteGeneratedProceduralBuilding(const std::string& buildingName) {
    if (buildingName.empty()) return 0;
    const std::string prefix = "Building_" + buildingName + "_";
    std::vector<int> indices;
    for (int i = 0; i < static_cast<int>(m_scene.Objects().size()); ++i) {
        if (m_scene.Objects()[static_cast<std::size_t>(i)].name.rfind(prefix, 0) == 0)
            indices.push_back(i);
    }
    if (indices.empty()) return 0;
    m_scene.SelectIndex(indices.front());
    for (std::size_t i = 1; i < indices.size(); ++i) m_scene.ToggleSelection(indices[i]);
    return m_scene.DeleteSelected() ? static_cast<int>(indices.size()) : 0;
}

void EditorApp::GenerateProceduralBuilding() {
    if (!m_cube) return;
    const std::string buildingName = m_proceduralBuilding.BuildingName();
    if (buildingName.empty()) return;
    const std::vector<ProceduralBuildingPanel::Part> parts =
        m_proceduralBuilding.GenerateParts();
    if (parts.empty()) {
        m_log.Warning("Procedural Building has an invalid or empty footprint");
        return;
    }
    if (m_proceduralBuilding.ReplaceExisting())
        DeleteGeneratedProceduralBuilding(buildingName);

    engine::ecs::Collider collider = engine::ecs::Collider::MakeBox(glm::vec3(0.5f));
    collider.layer = engine::ecs::CollisionLayer::WorldStatic;
    collider.mask = engine::ecs::CollisionLayer::All;
    const engine::ecs::Collider* colliderPtr = m_proceduralBuilding.CreateColliders()
        ? &collider : nullptr;
    bool first = true;
    int created = 0;
    for (const ProceduralBuildingPanel::Part& part : parts) {
        m_scene.SuppressUndo(!first);
        engine::ecs::Transform transform;
        transform.position = part.position;
        transform.scale = part.scale;
        transform.rotation = glm::angleAxis(part.yawRadians, glm::vec3(0.0f, 1.0f, 0.0f));
        m_scene.AddConfiguredPrimitive(EditorScene::Primitive::Cube, *m_cube,
            transform, colliderPtr, "Building_" + buildingName + "_" + part.suffix);
        if (first) { first = false; m_scene.SuppressUndo(true); }
        if (!part.materialPath.empty()) m_scene.SetSelectedMaterialAsset(part.materialPath);
        ++created;
    }
    m_scene.SuppressUndo(false);
    m_log.Info("Procedural Building generated " + buildingName + " ("
        + std::to_string(created) + " editable pieces)");
}

int EditorApp::DeleteGeneratedRoom(const std::string& roomName) {
    if (roomName.empty()) return 0;
    const std::string prefix = roomName + "_";
    std::vector<int> indices;
    for (int i = 0; i < static_cast<int>(m_scene.Objects().size()); ++i) {
        const std::string& name = m_scene.Objects()[static_cast<std::size_t>(i)].name;
        if (name.rfind(prefix, 0) == 0) indices.push_back(i);
    }
    if (indices.empty()) return 0;
    m_scene.SelectIndex(indices.front());
    for (std::size_t i = 1; i < indices.size(); ++i) m_scene.ToggleSelection(indices[i]);
    return m_scene.DeleteSelected() ? static_cast<int>(indices.size()) : 0;
}

void EditorApp::GenerateRoom() {
    if (!m_cube || !m_roomBuilder.HasRoom()) return;
    const std::string roomName = m_roomBuilder.RoomName();
    if (roomName.empty()) return;
    if (m_roomBuilder.ReplaceExisting()) DeleteGeneratedRoom(roomName);

    const glm::vec3 a = m_roomBuilder.FirstCorner();
    const glm::vec3 b = m_roomBuilder.SecondCorner();
    const float minX = std::min(a.x, b.x), maxX = std::max(a.x, b.x);
    const float minZ = std::min(a.z, b.z), maxZ = std::max(a.z, b.z);
    const float width = std::max(maxX - minX, 0.1f);
    const float depth = std::max(maxZ - minZ, 0.1f);
    const float baseY = a.y;
    const float wallHeight = m_roomBuilder.WallHeight();
    const float thickness = std::min(m_roomBuilder.WallThickness(),
                                     std::min(width, depth) * 0.45f);
    const float centerX = (minX + maxX) * 0.5f;
    const float centerZ = (minZ + maxZ) * 0.5f;
    engine::ecs::Collider collider = engine::ecs::Collider::MakeBox(glm::vec3(0.5f));
    collider.layer = engine::ecs::CollisionLayer::WorldStatic;
    collider.mask = engine::ecs::CollisionLayer::All;
    const engine::ecs::Collider* colliderPtr = m_roomBuilder.CreateColliders()
        ? &collider : nullptr;

    bool firstPiece = true;
    int pieceCount = 0;
    auto addPiece = [&](const std::string& suffix, const glm::vec3& position,
                        const glm::vec3& scale) {
        if (scale.x <= 0.001f || scale.y <= 0.001f || scale.z <= 0.001f) return;
        m_scene.SuppressUndo(!firstPiece);
        engine::ecs::Transform transform;
        transform.position = position;
        transform.scale = scale;
        m_scene.AddConfiguredPrimitive(EditorScene::Primitive::Cube, *m_cube,
                                       transform, colliderPtr, roomName + "_" + suffix);
        if (firstPiece) {
            firstPiece = false;
            m_scene.SuppressUndo(true);
        }
        if (!m_roomBuilder.MaterialPath().empty())
            m_scene.SetSelectedMaterialAsset(m_roomBuilder.MaterialPath());
        ++pieceCount;
    };

    const float floorThickness = m_roomBuilder.FloorThickness();
    if (m_roomBuilder.CreateFloor()) {
        addPiece("Floor", {centerX, baseY - floorThickness * 0.5f, centerZ},
                 {width, floorThickness, depth});
    }
    if (m_roomBuilder.CreateCeiling()) {
        addPiece("Ceiling", {centerX, baseY + wallHeight + floorThickness * 0.5f, centerZ},
                 {width, floorThickness, depth});
    }

    auto horizontalWall = [&](const std::string& name, float z, bool doorway) {
        if (!doorway) {
            addPiece(name, {centerX, baseY + wallHeight * 0.5f, z},
                     {width, wallHeight, thickness});
            return;
        }
        const float openingWidth = std::clamp(
            m_roomBuilder.DoorWidth(), thickness, std::max(thickness, width - thickness * 2.0f));
        if (width <= openingWidth + thickness * 1.5f) {
            addPiece(name, {centerX, baseY + wallHeight * 0.5f, z},
                     {width, wallHeight, thickness});
            return;
        }
        const float start = minX, end = maxX;
        const float doorCenter = std::clamp(centerX + m_roomBuilder.DoorOffset(),
            start + openingWidth * 0.5f + thickness,
            end - openingWidth * 0.5f - thickness);
        const float doorMin = doorCenter - openingWidth * 0.5f;
        const float doorMax = doorCenter + openingWidth * 0.5f;
        addPiece(name + "_Left", {(start + doorMin) * 0.5f, baseY + wallHeight * 0.5f, z},
                 {doorMin - start, wallHeight, thickness});
        addPiece(name + "_Right", {(doorMax + end) * 0.5f, baseY + wallHeight * 0.5f, z},
                 {end - doorMax, wallHeight, thickness});
        const float doorHeight = std::min(m_roomBuilder.DoorHeight(), wallHeight - 0.05f);
        const float headerHeight = wallHeight - doorHeight;
        addPiece(name + "_Header", {doorCenter, baseY + doorHeight + headerHeight * 0.5f, z},
                 {openingWidth, headerHeight, thickness});
    };
    auto verticalWall = [&](const std::string& name, float x, bool doorway) {
        if (!doorway) {
            addPiece(name, {x, baseY + wallHeight * 0.5f, centerZ},
                     {thickness, wallHeight, depth});
            return;
        }
        const float openingWidth = std::clamp(
            m_roomBuilder.DoorWidth(), thickness, std::max(thickness, depth - thickness * 2.0f));
        if (depth <= openingWidth + thickness * 1.5f) {
            addPiece(name, {x, baseY + wallHeight * 0.5f, centerZ},
                     {thickness, wallHeight, depth});
            return;
        }
        const float start = minZ, end = maxZ;
        const float doorCenter = std::clamp(centerZ + m_roomBuilder.DoorOffset(),
            start + openingWidth * 0.5f + thickness,
            end - openingWidth * 0.5f - thickness);
        const float doorMin = doorCenter - openingWidth * 0.5f;
        const float doorMax = doorCenter + openingWidth * 0.5f;
        addPiece(name + "_Left", {x, baseY + wallHeight * 0.5f, (start + doorMin) * 0.5f},
                 {thickness, wallHeight, doorMin - start});
        addPiece(name + "_Right", {x, baseY + wallHeight * 0.5f, (doorMax + end) * 0.5f},
                 {thickness, wallHeight, end - doorMax});
        const float doorHeight = std::min(m_roomBuilder.DoorHeight(), wallHeight - 0.05f);
        const float headerHeight = wallHeight - doorHeight;
        addPiece(name + "_Header", {x, baseY + doorHeight + headerHeight * 0.5f, doorCenter},
                 {thickness, headerHeight, openingWidth});
    };

    const bool door = m_roomBuilder.DoorEnabled();
    horizontalWall("Wall_North", maxZ, door && m_roomBuilder.DoorWall() == 0);
    horizontalWall("Wall_South", minZ, door && m_roomBuilder.DoorWall() == 1);
    verticalWall("Wall_East", maxX, door && m_roomBuilder.DoorWall() == 2);
    verticalWall("Wall_West", minX, door && m_roomBuilder.DoorWall() == 3);

    if (m_roomBuilder.CreateCornerPosts()) {
        addPiece("Corner_NW", {minX, baseY + wallHeight * 0.5f, maxZ},
                 {thickness, wallHeight, thickness});
        addPiece("Corner_NE", {maxX, baseY + wallHeight * 0.5f, maxZ},
                 {thickness, wallHeight, thickness});
        addPiece("Corner_SW", {minX, baseY + wallHeight * 0.5f, minZ},
                 {thickness, wallHeight, thickness});
        addPiece("Corner_SE", {maxX, baseY + wallHeight * 0.5f, minZ},
                 {thickness, wallHeight, thickness});
    }
    m_scene.SuppressUndo(false);
    m_log.Info("Room Builder generated " + roomName + " ("
        + std::to_string(pieceCount) + " editable pieces)");
}

void EditorApp::DrawScatterPaintPanel() {
    if (!m_panels.IsOpen(EditorPanels::Panel::ScatterPaint)) return;
    bool open = true;
    const ScatterPaintPanel::Result result =
        m_scatterPaint.Draw(m_project.AssetRoot(), &open);
    m_panels.SetOpen(EditorPanels::Panel::ScatterPaint, open);
    if (result.clearAllRequested) {
        const int removed = ClearPaintedScatter();
        m_log.Info("Scatter & Paint removed " + std::to_string(removed) + " object(s)");
    }
}

void EditorApp::PaintScatterStamp(const glm::vec3& center, const glm::vec3& normal,
                                  bool projectToTerrain) {
    const ScatterPaintPanel::AssetChoice* asset = m_scatterPaint.SelectedAsset();
    if (!asset || !m_cube || !m_scatterPaint.SlopeAllowed(normal)) return;
    const std::vector<ScatterPaintPanel::StampPoint> points =
        m_scatterPaint.MakeStamp(center, normal);
    if (points.empty()) return;

    glm::vec3 minimum(-0.5f), maximum(0.5f);
    std::string error;
    if (const engine::Model* model = m_editAssets.LoadModel(asset->path, &error)) {
        minimum = model->Min();
        maximum = model->Max();
    }
    const glm::vec3 n = glm::dot(normal, normal) > 1.0e-8f
        ? glm::normalize(normal) : glm::vec3(0, 1, 0);
    bool first = true;
    int placed = 0;
    for (const ScatterPaintPanel::StampPoint& sourcePoint : points) {
        ScatterPaintPanel::StampPoint point = sourcePoint;
        if (projectToTerrain) {
            bool overTerrain = false;
            const float terrainY = TerrainSurfaceY(point.position.x, point.position.z,
                                                   overTerrain);
            if (overTerrain) point.position.y = terrainY + m_scatterPaint.HeightOffset();
        }
        bool spaced = true;
        if (m_scatterPaint.MinimumSpacing() > 0.0f) {
            for (const EditorScene::Object& object : m_scene.Objects()) {
                if (object.name.rfind("Scatter_", 0) != 0) continue;
                const engine::ecs::Transform* existing = m_scene.TryGetTransform(object.entity);
                if (existing && glm::distance(existing->position, point.position)
                    < m_scatterPaint.MinimumSpacing()) {
                    spaced = false;
                    break;
                }
            }
        }
        if (!spaced) continue;

        engine::ecs::Transform transform;
        transform.position = point.position;
        transform.rotation = point.rotation;
        transform.scale = glm::vec3(point.uniformScale);
        if (m_scatterPaint.KeepOutsideSurface()) {
            const glm::vec3 centerLocal = (minimum + maximum) * 0.5f * point.uniformScale;
            const glm::vec3 extent = glm::max((maximum - minimum) * 0.5f
                                               * point.uniformScale, glm::vec3(0.0f));
            const glm::mat3 rotation = glm::mat3_cast(point.rotation);
            const float support = std::abs(glm::dot(n, rotation[0])) * extent.x
                + std::abs(glm::dot(n, rotation[1])) * extent.y
                + std::abs(glm::dot(n, rotation[2])) * extent.z;
            transform.position += n * (support - glm::dot(n, rotation * centerLocal));
        }

        m_scene.SuppressUndo(!first);
        if (!m_scene.AddModel(asset->path, *m_cube, transform)) continue;
        if (first) {
            first = false;
            m_scene.SuppressUndo(true);
        }
        std::string name = "Scatter_" + asset->name;
        const std::string base = name;
        int suffix = 1;
        auto exists = [&](const std::string& candidate) {
            for (const EditorScene::Object& object : m_scene.Objects())
                if (object.name == candidate) return true;
            return false;
        };
        while (exists(name)) name = base + "_" + std::to_string(++suffix);
        m_scene.SetSelectedName(name);
        ++placed;
    }
    m_scene.SuppressUndo(false);
    if (placed > 0) m_log.Info("Scatter & Paint placed " + std::to_string(placed)
                                + " " + asset->name + " object(s)");
}

int EditorApp::EraseScatterAt(const glm::vec3& center, float radius) {
    std::vector<int> indices;
    const float radiusSquared = radius * radius;
    for (int i = 0; i < static_cast<int>(m_scene.Objects().size()); ++i) {
        const EditorScene::Object& object = m_scene.Objects()[static_cast<std::size_t>(i)];
        if (object.name.rfind("Scatter_", 0) != 0 || object.locked) continue;
        const engine::ecs::Transform* transform = m_scene.TryGetTransform(object.entity);
        if (!transform) continue;
        const glm::vec3 delta = transform->position - center;
        if (glm::dot(delta, delta) <= radiusSquared) indices.push_back(i);
    }
    if (indices.empty()) return 0;
    m_scene.SelectIndex(indices.front());
    for (std::size_t i = 1; i < indices.size(); ++i) m_scene.ToggleSelection(indices[i]);
    return m_scene.DeleteSelected() ? static_cast<int>(indices.size()) : 0;
}

int EditorApp::ClearPaintedScatter() {
    std::vector<int> indices;
    for (int i = 0; i < static_cast<int>(m_scene.Objects().size()); ++i) {
        const EditorScene::Object& object = m_scene.Objects()[static_cast<std::size_t>(i)];
        if (!object.locked && object.name.rfind("Scatter_", 0) == 0) indices.push_back(i);
    }
    if (indices.empty()) return 0;
    m_scene.SelectIndex(indices.front());
    for (std::size_t i = 1; i < indices.size(); ++i) m_scene.ToggleSelection(indices[i]);
    return m_scene.DeleteSelected() ? static_cast<int>(indices.size()) : 0;
}

void EditorApp::DrawArrayToolPanel() {
    if (!m_panels.IsOpen(EditorPanels::Panel::ArrayTool)) return;
    bool open = true;
    const ArrayToolPanel::Result result = m_arrayTool.Draw(m_scene, &open);
    m_panels.SetOpen(EditorPanels::Panel::ArrayTool, open);
    if (result.deleteGeneratedRequested) {
        const int removed = DeleteGeneratedArray(m_arrayTool.GroupName());
        m_log.Info("Array Tool removed " + std::to_string(removed) + " generated object(s)");
    }
    if (result.createRequested) GenerateObjectArray();
}

void EditorApp::DrawMeasurementPanel() {
    if (!m_panels.IsOpen(EditorPanels::Panel::Measurement)) return;
    bool open = true;
    m_measurementPanel.Draw(&open);
    m_panels.SetOpen(EditorPanels::Panel::Measurement, open);
}

void EditorApp::DrawLevelValidationPanel() {
    if (!m_panels.IsOpen(EditorPanels::Panel::LevelValidation)) return;
    bool open = true;
    m_levelValidation.Draw(m_scene, m_project.AssetRoot(), &open);
    m_panels.SetOpen(EditorPanels::Panel::LevelValidation, open);
}

void EditorApp::DrawOptimizationAuditorPanel() {
    if (!m_panels.IsOpen(EditorPanels::Panel::OptimizationAuditor)) return;
    bool open = true;
    m_optimizationAuditor.Draw(
        m_scene, m_editAssets, m_project.AssetRoot(), &open);
    m_panels.SetOpen(EditorPanels::Panel::OptimizationAuditor, open);
    if (m_optimizationAuditor.ConsumeFrameRequest() >= 0) FrameSelected();
}

void EditorApp::DrawLightingAnalysisPanel() {
    if (!m_panels.IsOpen(EditorPanels::Panel::LightingAnalysis)) return;
    bool open = true;
    const LightingAnalysisPanel::Result result = m_lightingAnalysis.Draw(
        m_scene, m_editAssets, m_project.AssetRoot(), &open);
    m_panels.SetOpen(EditorPanels::Panel::LightingAnalysis, open);
    if (result.frameObject >= 0
        && result.frameObject < static_cast<int>(m_scene.Objects().size())) {
        m_scene.SelectIndex(result.frameObject);
        FrameSelected();
    }
}

void EditorApp::DrawRagdollPhysicsPanel() {
    if (!m_panels.IsOpen(EditorPanels::Panel::RagdollPhysics)) return;
    bool open = true;
    bool saved = false;
    std::string message;
    m_ragdollPhysics.Draw(m_scene, m_editAssets, m_project.AssetRoot(),
                          &open, &saved, &message);
    m_panels.SetOpen(EditorPanels::Panel::RagdollPhysics, open);
    if (saved) {
        std::string error;
        if (!m_assets.Refresh(m_project.AssetRoot(), &error)) m_log.Warning(error);
    }
    if (!message.empty()) m_log.Info(message);
}

void EditorApp::DrawAnimationRetargetingPanel() {
    if (!m_panels.IsOpen(EditorPanels::Panel::AnimationRetargeting)) return;
    bool open = true;
    bool changed = false;
    std::string message;
    m_animationRetargeting.Draw(m_assets, m_project.AssetRoot(), &open,
                                &changed, &message);
    m_panels.SetOpen(EditorPanels::Panel::AnimationRetargeting, open);
    if (changed) {
        std::string error;
        if (!m_assets.Refresh(m_project.AssetRoot(), &error)) m_log.Warning(error);
    }
    if (!message.empty()) m_log.Info(message);
}

void EditorApp::DrawAbilityEditorPanel() {
    if (!m_panels.IsOpen(EditorPanels::Panel::AbilityEditor)) return;
    bool open = true;
    bool changed = false;
    std::string message;
    m_abilityEditor.Draw(m_assets, m_project.AssetRoot(), &open, &changed, &message);
    m_panels.SetOpen(EditorPanels::Panel::AbilityEditor, open);
    if (changed) {
        std::string error;
        if (!m_assets.Refresh(m_project.AssetRoot(), &error)) m_log.Warning(error);
    }
    if (!message.empty()) m_log.Info(message);
}

void EditorApp::DrawRuntimePropertyInspectorPanel() {
    if (!m_panels.IsOpen(EditorPanels::Panel::RuntimePropertyInspector)) return;
    bool open = true;
    const bool wasPaused = m_physicsPaused;
    m_runtimePropertyInspector.Draw(
        (m_mode == EditorMode::Play && m_playRegistry) ? &*m_playRegistry : nullptr,
        m_playEntityNames, m_physicsPaused, m_physicsStepRequested, &open);
    if (m_mode == EditorMode::Play && wasPaused != m_physicsPaused) {
        if (m_physicsPaused) engine::GameMode::Instance().Pause();
        else engine::GameMode::Instance().Resume();
    }
    m_panels.SetOpen(EditorPanels::Panel::RuntimePropertyInspector, open);
}

void EditorApp::DrawAssetDependencyViewerPanel() {
    if (!m_panels.IsOpen(EditorPanels::Panel::AssetDependencyViewer)) return;
    bool open = true;
    const AssetDependencyViewerPanel::Result result = m_assetDependencyViewer.Draw(
        m_assetRegistry, m_project.AssetRoot(), &open);
    m_panels.SetOpen(EditorPanels::Panel::AssetDependencyViewer, open);
    if (result.synchronizeRegistry) {
        LoadProjectAssetRegistry();
        m_assetDependencyViewer.Invalidate();
    }
    if (!result.revealRelativePath.empty()) {
        std::string error;
        if (m_assets.RevealAsset(result.revealRelativePath, &error))
            m_panels.SetOpen(EditorPanels::Panel::Assets, true);
        else if (!error.empty()) m_log.Warning("Asset reveal: " + error);
    }
    if (!result.openRelativePath.empty()) {
        const EditorAssets::Type type = EditorAssetTypeFor(result.openType);
        if (type == EditorAssets::Type::Script || type == EditorAssets::Type::Other) {
            std::string error;
            if (m_assets.RevealAsset(result.openRelativePath, &error))
                m_panels.SetOpen(EditorPanels::Panel::Assets, true);
            else if (!error.empty()) m_log.Warning("Asset open: " + error);
        } else {
            m_dependencyAssetOpenType = type;
            m_dependencyAssetOpenPath =
                (std::filesystem::path(m_project.AssetRoot()) /
                 std::filesystem::path(result.openRelativePath)).lexically_normal().string();
        }
    }
    if (!result.message.empty()) m_log.Info(result.message);
}

void EditorApp::DrawWeatherEditorPanel() {
    if (!m_panels.IsOpen(EditorPanels::Panel::WeatherEditor)) return;
    bool open = true;
    const WeatherEditorPanel::Result result = m_weatherEditor.Draw(
        m_scene, m_assets, m_project.AssetRoot(), &open);
    m_panels.SetOpen(EditorPanels::Panel::WeatherEditor, open);
    if (result.saved) {
        std::string error;
        if (!m_assets.Refresh(m_project.AssetRoot(), &error)) m_log.Warning(error);
        LoadProjectAssetRegistry();
        m_assetDependencyViewer.Invalidate();
    }
    if (!result.message.empty()) m_log.Info(result.message);
}

void EditorApp::DrawLevelVariantPanel() {
    if (!m_panels.IsOpen(EditorPanels::Panel::LevelVariants)) return;
    bool open = true;
    const LevelVariantPanel::Result result =
        m_levelVariants.Draw(m_project.AssetRoot(), &open);
    m_panels.SetOpen(EditorPanels::Panel::LevelVariants, open);
    if (result.action == LevelVariantPanel::Action::None) return;
    if (!m_cube || !m_plane || !m_sphere || !m_capsule || !m_cylinder
        || !m_cone || !m_pyramid || !m_torus || !m_staircase) {
        m_levelVariants.SetStatus("Editor meshes are not ready.", true);
        return;
    }

    auto loadVariant = [&](const std::string& path, EditorScene* output,
                           std::string* error) {
        return output && output->Load(path, *m_cube, *m_plane, *m_sphere,
            *m_capsule, *m_cylinder, *m_cone, *m_pyramid, *m_torus,
            *m_staircase, error);
    };
    auto refreshWithStatus = [&](const std::string& status, bool error) {
        m_levelVariants.Refresh(m_project.AssetRoot());
        m_levelVariants.SetStatus(status, error);
        if (error) m_log.Warning("Level Variant: " + status);
        else m_log.Info("Level Variant: " + status);
    };

    std::error_code ec;
    switch (result.action) {
    case LevelVariantPanel::Action::Capture:
    case LevelVariantPanel::Action::Overwrite: {
        if (result.targetPath.empty()) break;
        if (result.action == LevelVariantPanel::Action::Capture
            && std::filesystem::exists(result.targetPath, ec)) {
            refreshWithStatus("A variant with that name already exists. Use Overwrite.", true);
            break;
        }
        std::filesystem::create_directories(
            std::filesystem::path(result.targetPath).parent_path(), ec);
        std::string error;
        if (m_scene.Save(result.targetPath, &error, false))
            refreshWithStatus("Captured "
                + std::filesystem::path(result.targetPath).stem().string() + ".", false);
        else refreshWithStatus("Capture failed: " + error, true);
        break;
    }
    case LevelVariantPanel::Action::Restore: {
        EditorScene variant;
        std::string error;
        if (!loadVariant(result.sourcePath, &variant, &error)) {
            refreshWithStatus("Restore failed: " + error, true);
            break;
        }
        m_scene.ApplySnapshotUndoable(variant.CreateSnapshot(), *m_cube, *m_plane,
            *m_sphere, *m_capsule, *m_cylinder, *m_cone, *m_pyramid,
            *m_torus, *m_staircase);
        m_terrains.clear();
        m_waters.clear();
        m_editAssets.ResolveRegistryAssets(m_scene.Registry());
        refreshWithStatus("Restored "
            + std::filesystem::path(result.sourcePath).stem().string()
            + ". Use Undo to return to the previous layout.", false);
        break;
    }
    case LevelVariantPanel::Action::Compare: {
        EditorScene variant;
        std::string error;
        if (!loadVariant(result.sourcePath, &variant, &error)) {
            refreshWithStatus("Compare failed: " + error, true);
            break;
        }
        int added = 0, removed = 0, modified = 0;
        auto findByName = [](const EditorScene& scene, const std::string& name)
            -> const EditorScene::Object* {
            for (const EditorScene::Object& object : scene.Objects())
                if (object.name == name) return &object;
            return nullptr;
        };
        for (const EditorScene::Object& object : variant.Objects()) {
            const EditorScene::Object* current = findByName(m_scene, object.name);
            if (!current) { ++added; continue; }
            const Transform* a = variant.TryGetTransform(object.entity);
            const Transform* b = m_scene.TryGetTransform(current->entity);
            bool changed = !a || !b;
            if (a && b) {
                changed = glm::distance(a->position, b->position) > 0.0001f
                    || glm::distance(a->scale, b->scale) > 0.0001f
                    || std::abs(glm::dot(a->rotation, b->rotation)) < 0.99999f;
            }
            changed = changed || object.visible != current->visible
                || object.modelAssetPath != current->modelAssetPath
                || object.materialAssetPath != current->materialAssetPath
                || object.colliderEnabled != current->colliderEnabled
                || object.scriptEnabled != current->scriptEnabled
                || object.scriptPath != current->scriptPath;
            if (changed) ++modified;
        }
        for (const EditorScene::Object& object : m_scene.Objects())
            if (!findByName(variant, object.name)) ++removed;
        refreshWithStatus("Comparison: variant adds " + std::to_string(added)
            + ", removes " + std::to_string(removed) + ", and modifies "
            + std::to_string(modified) + " named object(s).", false);
        break;
    }
    case LevelVariantPanel::Action::Duplicate: {
        if (result.targetPath.empty() || result.targetPath == result.sourcePath) {
            refreshWithStatus("Choose a different name for the duplicate.", true);
            break;
        }
        if (std::filesystem::exists(result.targetPath, ec)) {
            refreshWithStatus("The duplicate target name already exists.", true);
            break;
        }
        std::filesystem::copy_file(result.sourcePath, result.targetPath,
                                   std::filesystem::copy_options::none, ec);
        refreshWithStatus(ec ? "Duplicate failed: " + ec.message() : "Variant duplicated.",
                          static_cast<bool>(ec));
        break;
    }
    case LevelVariantPanel::Action::Rename: {
        if (result.targetPath.empty() || result.targetPath == result.sourcePath) {
            refreshWithStatus("Choose a different name for the variant.", true);
            break;
        }
        if (std::filesystem::exists(result.targetPath, ec)) {
            refreshWithStatus("The rename target already exists.", true);
            break;
        }
        std::filesystem::rename(result.sourcePath, result.targetPath, ec);
        refreshWithStatus(ec ? "Rename failed: " + ec.message() : "Variant renamed.",
                          static_cast<bool>(ec));
        break;
    }
    case LevelVariantPanel::Action::Delete:
        if (!std::filesystem::remove(result.sourcePath, ec) || ec)
            refreshWithStatus("Delete failed: " + ec.message(), true);
        else refreshWithStatus("Variant deleted.", false);
        break;
    case LevelVariantPanel::Action::None:
        break;
    }
}

void EditorApp::DrawLevelLayersPanel() {
    if (!m_panels.IsOpen(EditorPanels::Panel::LevelLayers)) return;
    bool open = true;
    m_levelLayers.Draw(m_scene, &open);
    m_panels.SetOpen(EditorPanels::Panel::LevelLayers, open);
}

void EditorApp::DrawViewportBookmarksPanel() {
    if (!m_panels.IsOpen(EditorPanels::Panel::ViewportBookmarks)) return;
    bool open = true;
    const ViewportBookmarksPanel::Result result = m_viewportBookmarks.Draw(m_scene, &open);
    m_panels.SetOpen(EditorPanels::Panel::ViewportBookmarks, open);
    using Action = ViewportBookmarksPanel::Action;
    if (result.action == Action::None) return;
    if (result.action == Action::FrameSelected) {
        FrameSelected();
        return;
    }

    auto capture = [&](const std::string& name, float blendDuration) {
        EditorScene::ViewportBookmark bookmark;
        bookmark.name = name;
        bookmark.position = m_camera.Position();
        bookmark.target = m_camera.Position() + m_camera.Front() * 10.0f;
        bookmark.fov = m_camera.fov;
        bookmark.blendDuration = blendDuration;
        return bookmark;
    };
    if (result.action == Action::Capture) {
        m_scene.AddViewportBookmark(capture(result.name, result.blendDuration));
        m_log.Info("Captured viewport bookmark: " + result.name);
        return;
    }
    if (result.index >= m_scene.ViewportBookmarks().size()) return;
    const EditorScene::ViewportBookmark current = m_scene.ViewportBookmarks()[result.index];
    if (result.action == Action::Visit) {
        EditorScene::CameraPreset target;
        target.position = current.position;
        target.target = current.target;
        target.fov = current.fov;
        target.nearPlane = m_camera.nearPlane;
        target.farPlane = m_camera.farPlane;
        target.blendDuration = result.blendDuration;
        target.blendEasing = static_cast<int>(engine::CameraBlend::Easing::SmoothStep);
        BeginCameraBlend(target);
    } else if (result.action == Action::Overwrite) {
        EditorScene::ViewportBookmark updated = capture(current.name, result.blendDuration);
        m_scene.SetViewportBookmark(result.index, updated);
        m_log.Info("Updated viewport bookmark: " + current.name);
    } else if (result.action == Action::Rename) {
        EditorScene::ViewportBookmark renamed = current;
        renamed.name = result.name;
        renamed.blendDuration = result.blendDuration;
        m_scene.SetViewportBookmark(result.index, renamed);
    } else if (result.action == Action::Delete) {
        m_scene.RemoveViewportBookmark(result.index);
        m_log.Info("Deleted viewport bookmark: " + current.name);
    }
}

void EditorApp::DrawBlockoutPanel() {
    if (!m_panels.IsOpen(EditorPanels::Panel::Blockout)) return;
    bool open = true;
    const BlockoutPanel::Result result =
        m_blockoutPanel.Draw(m_project.AssetRoot(), &open);
    m_panels.SetOpen(EditorPanels::Panel::Blockout, open);
    if (result.deleteRequested) {
        const int removed = DeleteGeneratedBlockout(m_blockoutPanel.GroupName());
        m_log.Info("Blockout removed " + std::to_string(removed) + " piece(s)");
    }
    if (result.createRequested) GenerateBlockout();
}

void EditorApp::DrawAlignmentPanel() {
    if (!m_panels.IsOpen(EditorPanels::Panel::Alignment)) return;
    bool open = true;
    m_alignmentPanel.Draw(m_scene, &open);
    m_panels.SetOpen(EditorPanels::Panel::Alignment, open);
}

void EditorApp::DrawSplineBuilderPanel() {
    if (!m_panels.IsOpen(EditorPanels::Panel::SplineBuilder)) return;
    bool open = true;
    const SplineBuilderPanel::Result result =
        m_splineBuilder.Draw(m_scene, m_project.AssetRoot(), &open);
    m_panels.SetOpen(EditorPanels::Panel::SplineBuilder, open);
    if (result.remove) {
        const int removed = DeleteGeneratedSplineBuild(m_splineBuilder.GroupName());
        m_log.Info("Spline Builder removed " + std::to_string(removed) + " piece(s)");
    }
    if (result.generate) GenerateSplineBuild();
}

int EditorApp::DeleteGeneratedSplineBuild(const std::string& groupName) {
    if (groupName.empty()) return 0;
    const std::string prefix = "SplineBuild_" + groupName + "_";
    std::vector<int> indices;
    for (int i = 0; i < static_cast<int>(m_scene.Objects().size()); ++i)
        if (m_scene.Objects()[static_cast<std::size_t>(i)].name.rfind(prefix, 0) == 0)
            indices.push_back(i);
    if (indices.empty()) return 0;
    m_scene.SelectIndices(indices);
    return m_scene.DeleteSelected() ? static_cast<int>(indices.size()) : 0;
}

void EditorApp::GenerateSplineBuild() {
    if (!m_cube) return;
    const EditorScene::Object* splineObject = nullptr;
    for (const EditorScene::Object& object : m_scene.Objects())
        if (object.isSpline && object.name == m_splineBuilder.SplineName()
            && object.splinePoints.size() >= 2) { splineObject = &object; break; }
    if (!splineObject) {
        m_log.Warning("Spline Builder: selected spline is unavailable");
        return;
    }
    const std::string group = m_splineBuilder.GroupName();
    if (group.empty()) return;
    const std::vector<glm::vec3> splinePoints = splineObject->splinePoints;
    const bool closed = splineObject->splineClosed;
    if (m_splineBuilder.ReplaceExisting()) DeleteGeneratedSplineBuild(group);
    const engine::Spline spline(splinePoints, closed);
    const float length = spline.Length();
    if (length <= 0.001f) return;
    const int spans = std::clamp(
        static_cast<int>(std::ceil(length / std::max(m_splineBuilder.Spacing(), 0.1f))), 1, 2048);
    const std::string prefix = "SplineBuild_" + group + "_";

    engine::ecs::Collider boxCollider = engine::ecs::Collider::MakeBox(glm::vec3(0.5f));
    boxCollider.layer = engine::ecs::CollisionLayer::WorldStatic;
    boxCollider.mask = engine::ecs::CollisionLayer::All;
    const engine::ecs::Collider* collider = m_splineBuilder.CreateColliders() ? &boxCollider : nullptr;
    bool first = true;
    int generated = 0;
    auto orientation = [](glm::vec3 tangent) {
        if (glm::dot(tangent, tangent) < 1.0e-8f) tangent = glm::vec3(0, 0, 1);
        tangent = glm::normalize(tangent);
        const float yaw = std::atan2(tangent.x, tangent.z);
        const float pitch = -std::asin(std::clamp(tangent.y, -1.0f, 1.0f));
        return glm::angleAxis(yaw, glm::vec3(0, 1, 0))
            * glm::angleAxis(pitch, glm::vec3(1, 0, 0));
    };
    auto addCube = [&](const std::string& suffix, const glm::vec3& position,
                       const glm::vec3& scale, const glm::quat& rotation) {
        m_scene.SuppressUndo(!first);
        engine::ecs::Transform transform;
        transform.position = position;
        transform.scale = glm::max(scale, glm::vec3(0.01f));
        transform.rotation = rotation;
        m_scene.AddConfiguredPrimitive(EditorScene::Primitive::Cube, *m_cube,
            transform, collider, prefix + suffix);
        if (first) { first = false; m_scene.SuppressUndo(true); }
        if (!m_splineBuilder.MaterialPath().empty())
            m_scene.SetSelectedMaterialAsset(m_splineBuilder.MaterialPath());
        ++generated;
    };

    const float offset = m_splineBuilder.VerticalOffset();
    using Mode = SplineBuilderPanel::Mode;
    if (m_splineBuilder.CurrentMode() == Mode::Road) {
        for (int i = 0; i < spans; ++i) {
            const float d0 = length * static_cast<float>(i) / static_cast<float>(spans);
            const float d1 = length * static_cast<float>(i + 1) / static_cast<float>(spans);
            const glm::vec3 a = spline.PositionAtDistance(d0);
            const glm::vec3 b = spline.PositionAtDistance(d1);
            const float segmentLength = glm::length(b - a);
            addCube("Road_" + std::to_string(i + 1),
                (a + b) * 0.5f + glm::vec3(0, offset - m_splineBuilder.Thickness() * 0.5f, 0),
                {m_splineBuilder.Width(), m_splineBuilder.Thickness(), segmentLength * 1.04f},
                orientation(b - a));
        }
    } else if (m_splineBuilder.CurrentMode() == Mode::Fence) {
        const int postCount = closed ? spans : spans + 1;
        for (int i = 0; i < postCount; ++i) {
            const float distance = length * static_cast<float>(i) / static_cast<float>(spans);
            const glm::vec3 position = spline.PositionAtDistance(distance);
            addCube("Post_" + std::to_string(i + 1),
                position + glm::vec3(0, offset + m_splineBuilder.Height() * 0.5f, 0),
                {m_splineBuilder.PostSize(), m_splineBuilder.Height(), m_splineBuilder.PostSize()},
                orientation(spline.TangentAtDistance(distance)));
        }
        for (int i = 0; i < spans; ++i) {
            const float d0 = length * static_cast<float>(i) / static_cast<float>(spans);
            const float d1 = length * static_cast<float>(i + 1) / static_cast<float>(spans);
            const glm::vec3 a = spline.PositionAtDistance(d0);
            const glm::vec3 b = spline.PositionAtDistance(d1);
            for (int rail = 0; rail < m_splineBuilder.RailCount(); ++rail) {
                const float fraction = static_cast<float>(rail + 1)
                    / static_cast<float>(m_splineBuilder.RailCount() + 1);
                addCube("Rail_" + std::to_string(rail + 1) + "_" + std::to_string(i + 1),
                    (a + b) * 0.5f + glm::vec3(0, offset + m_splineBuilder.Height() * fraction, 0),
                    {m_splineBuilder.Thickness(), m_splineBuilder.Thickness(), glm::length(b - a) * 1.04f},
                    orientation(b - a));
            }
        }
    } else {
        const int itemCount = closed ? spans : spans + 1;
        for (int i = 0; i < itemCount; ++i) {
            const float distance = length * static_cast<float>(i) / static_cast<float>(spans);
            engine::ecs::Transform transform;
            transform.position = spline.PositionAtDistance(distance) + glm::vec3(0, offset, 0);
            transform.scale = glm::vec3(m_splineBuilder.PropScale());
            if (m_splineBuilder.AlignProps())
                transform.rotation = orientation(spline.TangentAtDistance(distance));
            m_scene.SuppressUndo(!first);
            if (!m_scene.AddModel(m_splineBuilder.ModelPath(), *m_cube, transform)) continue;
            if (first) { first = false; m_scene.SuppressUndo(true); }
            m_scene.SetSelectedName(prefix + "Prop_" + std::to_string(i + 1));
            if (collider) {
                m_scene.SetSelectedColliderEnabled(true);
                m_scene.SetSelectedCollider(*collider);
            }
            ++generated;
        }
    }
    m_scene.SuppressUndo(false);
    m_log.Info("Spline Builder generated " + group + " ("
        + std::to_string(generated) + " editable piece(s))");
}

int EditorApp::DeleteGeneratedBlockout(const std::string& groupName) {
    if (groupName.empty()) return 0;
    const std::string prefix = "Blockout_" + groupName + "_";
    std::vector<int> indices;
    for (int i = 0; i < static_cast<int>(m_scene.Objects().size()); ++i)
        if (m_scene.Objects()[static_cast<std::size_t>(i)].name.rfind(prefix, 0) == 0)
            indices.push_back(i);
    if (indices.empty()) return 0;
    m_scene.SelectIndices(indices);
    return m_scene.DeleteSelected() ? static_cast<int>(indices.size()) : 0;
}

void EditorApp::GenerateBlockout() {
    if (!m_cube) return;
    const std::string group = m_blockoutPanel.GroupName();
    if (group.empty()) return;
    if (m_blockoutPanel.ReplaceExisting()) DeleteGeneratedBlockout(group);

    glm::vec3 base = m_blockoutPanel.ManualPosition();
    if (m_blockoutPanel.PlacementMode() == BlockoutPanel::Placement::ViewportCursor)
        base = SceneDropPosition();
    else if (m_blockoutPanel.PlacementMode() == BlockoutPanel::Placement::SelectedObject) {
        if (const engine::ecs::Transform* selected = m_scene.SelectedTransform())
            base = selected->position;
        else {
            m_log.Warning("Blockout: select an object or choose another placement mode");
            return;
        }
    }

    const glm::vec3 dimensions = glm::max(m_blockoutPanel.Dimensions(), glm::vec3(0.05f));
    const float yaw = glm::radians(m_blockoutPanel.Yaw());
    const glm::quat yawRotation = glm::angleAxis(yaw, glm::vec3(0, 1, 0));
    auto rotateOffset = [&](const glm::vec3& value) { return yawRotation * value; };
    engine::ecs::Collider collider = engine::ecs::Collider::MakeBox(glm::vec3(0.5f));
    collider.layer = engine::ecs::CollisionLayer::WorldStatic;
    collider.mask = engine::ecs::CollisionLayer::All;
    const engine::ecs::Collider* colliderPtr = m_blockoutPanel.CreateCollider() ? &collider : nullptr;
    const std::string prefix = "Blockout_" + group + "_";
    bool first = true;
    int count = 0;
    auto addPiece = [&](const std::string& suffix, const glm::vec3& localPosition,
                        const glm::vec3& scale, const glm::quat& localRotation = glm::quat(1,0,0,0)) {
        m_scene.SuppressUndo(!first);
        engine::ecs::Transform transform;
        transform.position = base + rotateOffset(localPosition);
        transform.scale = glm::max(scale, glm::vec3(0.01f));
        transform.rotation = yawRotation * localRotation;
        m_scene.AddConfiguredPrimitive(EditorScene::Primitive::Cube, *m_cube,
            transform, colliderPtr, prefix + suffix);
        if (first) { first = false; m_scene.SuppressUndo(true); }
        if (!m_blockoutPanel.MaterialPath().empty())
            m_scene.SetSelectedMaterialAsset(m_blockoutPanel.MaterialPath());
        ++count;
    };

    using Shape = BlockoutPanel::Shape;
    const Shape shape = m_blockoutPanel.CurrentShape();
    if (shape == Shape::Wall || shape == Shape::Floor || shape == Shape::Platform) {
        addPiece(shape == Shape::Wall ? "Wall" : shape == Shape::Floor ? "Floor" : "Platform",
                 {0, dimensions.y * 0.5f, 0}, dimensions);
    } else if (shape == Shape::Ramp) {
        const float slope = std::atan2(dimensions.y, dimensions.z);
        const float length = std::sqrt(dimensions.y * dimensions.y + dimensions.z * dimensions.z);
        addPiece("Ramp", {0, dimensions.y * 0.5f, 0},
                 {dimensions.x, std::max(0.08f, std::min(dimensions.y, dimensions.z) * 0.08f), length},
                 glm::angleAxis(-slope, glm::vec3(1, 0, 0)));
    } else if (shape == Shape::Doorway) {
        const float openingWidth = std::clamp(m_blockoutPanel.DoorWidth(), 0.1f, dimensions.x - 0.05f);
        const float openingHeight = std::clamp(m_blockoutPanel.DoorHeight(), 0.1f, dimensions.y - 0.05f);
        const float sideWidth = (dimensions.x - openingWidth) * 0.5f;
        const float headerHeight = dimensions.y - openingHeight;
        addPiece("Door_Left", {-(openingWidth + sideWidth) * 0.5f, dimensions.y * 0.5f, 0},
                 {sideWidth, dimensions.y, dimensions.z});
        addPiece("Door_Right", {(openingWidth + sideWidth) * 0.5f, dimensions.y * 0.5f, 0},
                 {sideWidth, dimensions.y, dimensions.z});
        addPiece("Door_Header", {0, openingHeight + headerHeight * 0.5f, 0},
                 {openingWidth, headerHeight, dimensions.z});
    } else if (shape == Shape::Stairs) {
        const int steps = std::clamp(m_blockoutPanel.StepCount(), 2, 64);
        const float stepDepth = dimensions.z / static_cast<float>(steps);
        const float stepHeight = dimensions.y / static_cast<float>(steps);
        for (int i = 0; i < steps; ++i) {
            const float height = stepHeight * static_cast<float>(i + 1);
            const float z = -dimensions.z * 0.5f + stepDepth * (static_cast<float>(i) + 0.5f);
            addPiece("Step_" + std::to_string(i + 1), {0, height * 0.5f, z},
                     {dimensions.x, height, stepDepth});
        }
    }
    m_scene.SuppressUndo(false);
    m_log.Info("Blockout generated " + group + " (" + std::to_string(count) + " editable piece(s))");
}

int EditorApp::DeleteGeneratedArray(const std::string& groupName) {
    if (groupName.empty()) return 0;
    const std::string prefix = "Array_" + groupName + "_";
    std::vector<int> indices;
    for (int i = 0; i < static_cast<int>(m_scene.Objects().size()); ++i) {
        const EditorScene::Object& object = m_scene.Objects()[static_cast<std::size_t>(i)];
        if (!object.locked && object.name.rfind(prefix, 0) == 0) indices.push_back(i);
    }
    if (indices.empty()) return 0;
    m_scene.SelectIndex(indices.front());
    for (std::size_t i = 1; i < indices.size(); ++i) m_scene.ToggleSelection(indices[i]);
    return m_scene.DeleteSelected() ? static_cast<int>(indices.size()) : 0;
}

void EditorApp::GenerateObjectArray() {
    if (!m_cube || !m_plane || !m_sphere || !m_capsule || !m_cylinder
        || !m_cone || !m_pyramid || !m_torus || !m_staircase) {
        m_log.Error("Array Tool failed: editor meshes are not ready");
        return;
    }
    const EditorScene::Object* selected = m_scene.SelectedObject();
    const engine::ecs::Transform* sourceTransform = m_scene.SelectedTransform();
    if (!selected || !sourceTransform || selected->locked) {
        m_log.Warning("Array Tool: select an unlocked scene object first");
        return;
    }
    const std::string groupName = m_arrayTool.GroupName();
    if (groupName.empty()) return;
    const std::string prefix = "Array_" + groupName + "_";
    if (selected->name.rfind(prefix, 0) == 0 && m_arrayTool.ReplaceExisting()) {
        m_log.Warning("Array Tool: the source belongs to the group being replaced; select the original source");
        return;
    }

    const engine::ecs::Entity sourceEntity = selected->entity;
    const std::vector<engine::ecs::Transform> transforms =
        m_arrayTool.BuildTransforms(*sourceTransform, m_scene);
    if (transforms.empty()) {
        m_log.Warning("Array Tool: the current layout produced no copies");
        return;
    }

    int removed = 0;
    if (m_arrayTool.ReplaceExisting()) removed = DeleteGeneratedArray(groupName);
    int sourceIndex = -1;
    for (int i = 0; i < static_cast<int>(m_scene.Objects().size()); ++i) {
        if (m_scene.Objects()[static_cast<std::size_t>(i)].entity == sourceEntity) {
            sourceIndex = i;
            break;
        }
    }
    if (sourceIndex < 0) {
        m_log.Error("Array Tool: source object was removed before duplication");
        return;
    }
    m_scene.SelectIndex(sourceIndex);

    bool first = removed == 0;
    int created = 0;
    for (std::size_t i = 0; i < transforms.size(); ++i) {
        m_scene.SuppressUndo(!first);
        if (!m_scene.DuplicateSelected(*m_cube, *m_plane, *m_sphere, *m_capsule,
                *m_cylinder, *m_cone, *m_pyramid, *m_torus, *m_staircase)) {
            m_scene.SuppressUndo(false);
            break;
        }
        if (first) {
            first = false;
            m_scene.SuppressUndo(true);
        }
        m_scene.SetSelectedTransform(transforms[i]);
        std::string index = std::to_string(i + 1);
        while (index.size() < 3) index.insert(index.begin(), '0');
        m_scene.SetSelectedName(prefix + index);
        ++created;
    }
    m_scene.SuppressUndo(false);
    m_log.Info("Array Tool generated " + std::to_string(created)
        + " editable copies in group " + groupName);
}

void EditorApp::DrawViewportPanel() {
    if (!m_panels.IsOpen(EditorPanels::Panel::Viewport)) {
        m_sceneViewValid = false;
        return;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    // No close button (nullptr) and no collapse arrow: the Viewport is always present.
    const bool visible = ImGui::Begin(EditorPanels::Name(EditorPanels::Panel::Viewport), nullptr,
                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
                 | ImGuiWindowFlags_NoCollapse);

    if (visible) {
        // The default framebuffer is 4x MSAA (GLFW_SAMPLES=4). A multisample resolve blit
        // is only legal when source and destination rectangles are the SAME size, so the
        // FBO matches the window and we blit 1:1 with GL_NEAREST; ImGui::Image scales it.
        const int srcW = std::max(1, GetWindow().Width());
        const int srcH = std::max(1, GetWindow().Height());
        if (!m_viewportFbo) {
            m_viewportFbo.emplace(srcW, srcH, GL_RGBA8, false);
        } else if (m_viewportFbo->Width() != srcW || m_viewportFbo->Height() != srcH) {
            m_viewportFbo->Resize(srcW, srcH);
        }

        // The scene is already in the default framebuffer this frame (this runs during the
        // ImGui build phase, before ImGui renders). Resolve it into the FBO.
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_viewportFbo->FboId());
        glBlitFramebuffer(0, 0, srcW, srcH, 0, 0, srcW, srcH,
                          GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        // Fill the panel with no letterbox bars. The panel usually has a different aspect
        // than the window render, so centre-crop the texture to the panel's aspect. With a
        // fixed vertical FOV this matches a native panel-aspect render when the panel is
        // narrower than the window (the common case), so there's no distortion.
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const float dispW = std::max(1.0f, avail.x);
        const float dispH = std::max(1.0f, avail.y);
        const float winAspect   = static_cast<float>(srcW) / static_cast<float>(srcH);
        const float panelAspect = dispW / dispH;
        float cropU = 1.0f, cropV = 1.0f;
        if (panelAspect > winAspect) cropV = winAspect / panelAspect;   // crop top/bottom
        else                         cropU = panelAspect / winAspect;   // crop left/right
        const float u0 = (1.0f - cropU) * 0.5f, u1 = 1.0f - u0;
        const float vLo = (1.0f - cropV) * 0.5f, vHi = 1.0f - vLo;
        const ImVec2 imgMin = ImGui::GetCursorScreenPos();
        ImGui::Image((ImTextureID)(std::intptr_t)m_viewportFbo->ColorTexture(),
                     ImVec2(dispW, dispH), ImVec2(u0, vHi), ImVec2(u1, vLo));  // crop + flip V

        // Record the image rect (main-window pixel space) so scene input routes here.
        const ImGuiViewport* mainVp = ImGui::GetMainViewport();
        m_sceneViewValid = (ImGui::GetWindowViewport() == mainVp);
        m_sceneViewX = imgMin.x - mainVp->Pos.x;
        m_sceneViewY = imgMin.y - mainVp->Pos.y;
        m_sceneViewW = dispW;
        m_sceneViewH = dispH;
        m_sceneViewHovered = ImGui::IsItemHovered();
    } else {
        m_sceneViewValid = false;
    }

    ImGui::End();
    ImGui::PopStyleVar();
}

bool EditorApp::RemapViewportMouse(float winX, float winY, float& outX, float& outY) {
    if (!m_sceneViewValid || m_sceneViewW <= 0.0f || m_sceneViewH <= 0.0f) {
        outX = winX;
        outY = winY;
        return false;
    }
    // The panel centre-crops the window render to its aspect, so map a panel-local point
    // into the visible window-render pixel region (keeps picking in window space).
    const engine::Window& window = GetWindow();
    const float srcW = static_cast<float>(window.Width());
    const float srcH = static_cast<float>(window.Height());
    const float winAspect   = srcW / srcH;
    const float panelAspect = m_sceneViewW / m_sceneViewH;
    float cropU = 1.0f, cropV = 1.0f;
    if (panelAspect > winAspect) cropV = winAspect / panelAspect;
    else                         cropU = panelAspect / winAspect;
    const float cropX0 = (1.0f - cropU) * 0.5f * srcW;
    const float cropY0 = (1.0f - cropV) * 0.5f * srcH;
    const float lx = (winX - m_sceneViewX) / m_sceneViewW;   // 0..1 across the panel
    const float ly = (winY - m_sceneViewY) / m_sceneViewH;
    outX = cropX0 + lx * cropU * srcW;
    outY = cropY0 + ly * cropV * srcH;
    return true;
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
    const engine::GameMode& gameMode = engine::GameMode::Instance();
    ctx.floats["score"] = static_cast<float>(gameMode.Score());
    ctx.floats["time"] = gameMode.Elapsed();
    ctx.strings["score"] = std::to_string(gameMode.Score());
    ctx.strings["gamestate"] = engine::GameMode::StateName(gameMode.State());
    ctx.strings["gamemessage"] = gameMode.Message();

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
        case engine::HudButtonAction::RestartPlay:
            if (m_scene.GetGameModeSettings().allowRestart) {
                ExitPlayMode();
                EnterPlayMode();
            }
            break;
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

void EditorApp::OpenAnimationAssetPreview(
    const std::string& path, EditorAssets::Type type) {
    m_animationAssetPreviewPath = path;
    m_animationAssetPreviewType = type;
    m_animationAssetPreviewTime = 0.0f;
    m_animationAssetPreviewPlaying = true;
    m_animationAssetPreviewRestartRequested = false;
    m_animationPreviewAssets.Clear();
    if (type == EditorAssets::Type::SkeletalModel)
        m_animationAssetPreviewMeshPath = path;
    RefreshAnimationAssetPreviewChoices();
}

void EditorApp::RefreshAnimationAssetPreviewChoices() {
    m_animationAssetPreviewMeshes.clear();
    engine::AssetHandle targetSkeleton;
    std::string error;
    const std::filesystem::path contentRoot(m_project.AssetRoot());
    if (m_animationAssetPreviewType == EditorAssets::Type::Animation) {
        engine::AnimationAssetData animation;
        if (engine::LoadAnimationAsset(
                m_animationAssetPreviewPath, &animation, &error))
            targetSkeleton = animation.skeletonId;
    } else if (m_animationAssetPreviewType == EditorAssets::Type::Skeleton) {
        engine::SkeletonAssetData skeleton;
        if (engine::LoadSkeletonAsset(
                m_animationAssetPreviewPath, &skeleton, &error))
            targetSkeleton = skeleton.header.id;
    } else if (m_animationAssetPreviewType == EditorAssets::Type::SkeletalModel) {
        engine::SkeletalMeshAssetData mesh;
        if (engine::LoadSkeletalMeshAsset(
                m_animationAssetPreviewPath, &mesh, &error))
            targetSkeleton = mesh.skeletonId;
    }

    if (targetSkeleton.Valid()) {
        const std::string id = targetSkeleton.ToString();
        if (m_animationPreferredRigBySkeleton.find(id)
            == m_animationPreferredRigBySkeleton.end()) {
            const std::string key = "editor.animation_preview_rig." + id;
            const engine::Config& config = m_hasProjectFile
                ? m_projectConfig : m_config;
            const std::string stored = config.GetString(key, {});
            if (!stored.empty()) {
                std::filesystem::path path(stored);
                if (path.is_relative()) path = contentRoot / path;
                m_animationPreferredRigBySkeleton[id]
                    = path.lexically_normal().string();
            }
        }
    }
    for (const std::string& relative :
         m_assets.ContentAssetPaths(EditorAssets::Type::SkeletalModel)) {
        const std::string path = (contentRoot / relative).lexically_normal().string();
        engine::SkeletalMeshAssetData mesh;
        std::string meshError;
        EditorDockspace::AnimationPreviewState::AssetChoice choice;
        choice.path = path;
        choice.displayName = std::filesystem::path(path).filename().string();
        if (!engine::LoadSkeletalMeshAsset(path, &mesh, &meshError)) {
            choice.reason = meshError;
        } else if (!targetSkeleton.Valid()) {
            choice.compatible = true;
            choice.reason = "No skeleton filter";
        } else if (mesh.skeletonId == targetSkeleton) {
            choice.compatible = true;
            choice.reason = "Exact skeleton match";
        } else {
            choice.reason = "Different skeleton";
        }
        m_animationAssetPreviewMeshes.push_back(std::move(choice));
    }

    std::sort(m_animationAssetPreviewMeshes.begin(),
              m_animationAssetPreviewMeshes.end(),
        [](const auto& a, const auto& b) {
            if (a.compatible != b.compatible) return a.compatible > b.compatible;
            return a.displayName < b.displayName;
        });

    const auto selected = std::find_if(
        m_animationAssetPreviewMeshes.begin(),
        m_animationAssetPreviewMeshes.end(),
        [&](const auto& choice) {
            return choice.compatible
                && std::filesystem::path(choice.path).lexically_normal()
                    == std::filesystem::path(
                        m_animationAssetPreviewMeshPath).lexically_normal();
        });
    if (selected == m_animationAssetPreviewMeshes.end()) {
        m_animationAssetPreviewMeshPath.clear();
        if (targetSkeleton.Valid()) {
            const auto remembered = m_animationPreferredRigBySkeleton.find(
                targetSkeleton.ToString());
            if (remembered != m_animationPreferredRigBySkeleton.end()) {
                const auto valid = std::find_if(
                    m_animationAssetPreviewMeshes.begin(),
                    m_animationAssetPreviewMeshes.end(),
                    [&](const auto& choice) {
                        return choice.compatible
                            && choice.path == remembered->second;
                    });
                if (valid != m_animationAssetPreviewMeshes.end())
                    m_animationAssetPreviewMeshPath = valid->path;
            }
        }
        if (m_animationAssetPreviewMeshPath.empty()) {
            const auto first = std::find_if(
                m_animationAssetPreviewMeshes.begin(),
                m_animationAssetPreviewMeshes.end(),
                [](const auto& choice) { return choice.compatible; });
            if (first != m_animationAssetPreviewMeshes.end())
                m_animationAssetPreviewMeshPath = first->path;
        }
    }
    if (targetSkeleton.Valid() && !m_animationAssetPreviewMeshPath.empty()) {
        const std::string id = targetSkeleton.ToString();
        const auto existing = m_animationPreferredRigBySkeleton.find(id);
        if (existing == m_animationPreferredRigBySkeleton.end()
            || existing->second != m_animationAssetPreviewMeshPath) {
            m_animationPreferredRigBySkeleton[id]
                = m_animationAssetPreviewMeshPath;
            std::error_code ec;
            std::filesystem::path stored = std::filesystem::relative(
                m_animationAssetPreviewMeshPath, contentRoot, ec);
            if (ec) stored = m_animationAssetPreviewMeshPath;
            const std::string key = "editor.animation_preview_rig." + id;
            if (m_hasProjectFile) {
                m_projectConfig.Set(key, stored.generic_string());
                m_projectConfig.Save();
            } else {
                m_config.Set(key, stored.generic_string());
                m_config.Save();
            }
        }
    }
}

unsigned int EditorApp::RenderAnimationAssetPreview(
    const engine::SkinnedModel& model, int clipIndex,
    float durationSeconds) {
    constexpr int size = 420;
    if (!m_animationAssetPreviewFbo)
        m_animationAssetPreviewFbo.emplace(size, size, GL_RGBA8, true);
    if (!m_animationAssetPreviewRenderer)
        m_animationAssetPreviewRenderer =
            std::make_unique<engine::SkinnedRenderer>();

    if (m_animationAssetPreviewRestartRequested) {
        m_animationAssetPreviewTime = 0.0f;
        m_animationAssetPreviewRestartRequested = false;
    }
    if (m_animationAssetPreviewPlaying && durationSeconds > 0.0f) {
        m_animationAssetPreviewTime +=
            std::max(m_dt, 0.0f) * std::max(m_animationAssetPreviewSpeed, 0.0f);
        if (m_animationAssetPreviewLoop)
            m_animationAssetPreviewTime =
                std::fmod(m_animationAssetPreviewTime, durationSeconds);
        else
            m_animationAssetPreviewTime =
                std::min(m_animationAssetPreviewTime, durationSeconds);
    }
    if (clipIndex >= 0
        && clipIndex < static_cast<int>(model.Animations().size()))
        engine::Animator::ComputePose(
            model.GetSkeleton(),
            model.Animations()[static_cast<std::size_t>(clipIndex)],
            m_animationAssetPreviewTime,
            m_animationAssetPreviewPose);
    else
        engine::Animator::ComputeBindPose(
            model.GetSkeleton(), m_animationAssetPreviewPose);

    GLint oldFbo = 0;
    GLint oldViewport[4]{};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oldFbo);
    glGetIntegerv(GL_VIEWPORT, oldViewport);
    const GLboolean depthWas = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean cullWas = glIsEnabled(GL_CULL_FACE);
    m_animationAssetPreviewFbo->Bind();
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glClearColor(0.055f, 0.070f, 0.095f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (!model.SubMeshes().empty()) {
        const float radius = std::max(model.BoundingRadius(), 0.001f);
        glm::mat4 transform(1.0f);
        transform = glm::rotate(transform,
            glm::radians(m_animationAssetPreviewYaw), glm::vec3(0, 1, 0));
        transform = glm::rotate(transform,
            glm::radians(m_animationAssetPreviewPitch), glm::vec3(1, 0, 0));
        const glm::vec3 bounds = model.Max() - model.Min();
        if (bounds.z > bounds.y * 1.25f)
            transform = glm::rotate(
                transform, glm::radians(-90.0f), glm::vec3(1, 0, 0));
        transform = glm::scale(transform,
            glm::vec3((0.9f * m_animationAssetPreviewZoom) / radius));
        transform = glm::translate(transform, -model.Center());
        engine::Camera camera(glm::vec3(0.0f, 0.0f, 2.5f));
        camera.LookAt(glm::vec3(0.0f));
        m_animationAssetPreviewRenderer->Draw(
            model, m_animationAssetPreviewPose, transform, camera, 1.0f,
            glm::normalize(glm::vec3(0.45f, -1.0f, -0.35f)),
            glm::vec3(1.0f, 0.96f, 0.90f), glm::vec3(0.16f));
    }

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(oldFbo));
    glViewport(oldViewport[0], oldViewport[1], oldViewport[2], oldViewport[3]);
    if (depthWas) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (cullWas) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    return m_animationAssetPreviewFbo->ColorTexture();
}

EditorDockspace::AnimationPreviewState
EditorApp::BuildAnimationAssetPreviewState() {
    EditorDockspace::AnimationPreviewState state;
    state.hasSelection = true;
    state.assetMode = true;
    state.assetPath = m_animationAssetPreviewPath;
    state.previewMeshPath = m_animationAssetPreviewMeshPath;
    state.selectedName = std::filesystem::path(
        m_animationAssetPreviewPath).filename().string();
    state.assetIsAnimation =
        m_animationAssetPreviewType == EditorAssets::Type::Animation;
    state.assetIsSkeleton =
        m_animationAssetPreviewType == EditorAssets::Type::Skeleton;
    state.skeletalModel = true;
    state.previewMeshes = m_animationAssetPreviewMeshes;
    state.assetPlaying = m_animationAssetPreviewPlaying;
    state.assetStripRootMotion = m_animationAssetPreviewStripRootMotion;
    state.loop = m_animationAssetPreviewLoop;
    state.playbackSpeed = m_animationAssetPreviewSpeed;
    state.previewTime = m_animationAssetPreviewTime;

    if (m_animationAssetPreviewRefreshRequested) {
        m_animationAssetPreviewRefreshRequested = false;
        m_animationPreviewAssets.Clear();
        RefreshAnimationAssetPreviewChoices();
        state.previewMeshes = m_animationAssetPreviewMeshes;
        state.previewMeshPath = m_animationAssetPreviewMeshPath;
    }

    engine::AssetHandle skeletonId;
    std::string clipName;
    if (state.assetIsAnimation) {
        engine::AnimationAssetData animation;
        if (!engine::LoadAnimationAsset(
                m_animationAssetPreviewPath, &animation, &state.loadError))
            return state;
        skeletonId = animation.skeletonId;
        if (!animation.clips.empty())
            clipName = animation.clips.front().animation.name;
    } else if (state.assetIsSkeleton) {
        engine::SkeletonAssetData skeleton;
        if (!engine::LoadSkeletonAsset(
                m_animationAssetPreviewPath, &skeleton, &state.loadError))
            return state;
        skeletonId = skeleton.header.id;
    } else {
        engine::SkeletalMeshAssetData mesh;
        if (!engine::LoadSkeletalMeshAsset(
                m_animationAssetPreviewPath, &mesh, &state.loadError))
            return state;
        skeletonId = mesh.skeletonId;
    }
    state.skeletonId = skeletonId.ToString();

    if (m_animationAssetPreviewMeshPath.empty()) {
        state.loadError = "No compatible preview mesh is available for this skeleton.";
        return state;
    }

    engine::SkeletalMeshAssetData meshData;
    if (!engine::LoadSkeletalMeshAsset(
            m_animationAssetPreviewMeshPath, &meshData, &state.loadError))
        return state;
    if (meshData.skeletonId != skeletonId) {
        state.loadError = "The selected preview mesh references a different skeleton.";
        return state;
    }

    const engine::SkinnedModel* model = nullptr;
    if (state.assetIsAnimation) {
        std::vector<engine::RuntimeAssetManager::SkinnedAnimationSource> source{
            {m_animationAssetPreviewPath,
             clipName.empty() ? std::string("Preview") : clipName,
             m_animationAssetPreviewStripRootMotion,
             clipName}
        };
        model = m_animationPreviewAssets.LoadSkinnedModel(
            m_animationAssetPreviewMeshPath, source, &state.loadError);
    } else {
        model = m_animationPreviewAssets.LoadSkinnedModel(
            m_animationAssetPreviewMeshPath, &state.loadError);
    }
    if (!model) return state;
    state.modelLoaded = true;
    state.modelPath = m_animationAssetPreviewMeshPath;

    for (const engine::Animation& clip : model->Animations()) {
        const float ticks = clip.ticksPerSecond > 0.0f
            ? clip.ticksPerSecond : 25.0f;
        state.clips.push_back({clip.name,
            clip.duration > 0.0f ? clip.duration / ticks : 0.0f});
    }
    int clipIndex = state.clips.empty() ? -1
        : static_cast<int>(state.clips.size()) - 1;
    if (!clipName.empty()) {
        for (std::size_t i = 0; i < state.clips.size(); ++i)
            if (state.clips[i].name == clipName) {
                clipIndex = static_cast<int>(i);
                break;
            }
    }
    state.defaultClipIndex = std::max(clipIndex, 0);
    state.defaultClipName = clipIndex >= 0
        ? state.clips[static_cast<std::size_t>(clipIndex)].name
        : std::string{};
    state.previewDuration = clipIndex >= 0
        ? state.clips[static_cast<std::size_t>(clipIndex)].durationSeconds
        : 0.0f;

    const engine::Skeleton& skeleton = model->GetSkeleton();
    for (std::size_t i = 0; i < skeleton.bones.size(); ++i) {
        const engine::Bone& bone = skeleton.bones[i];
        int depth = 0;
        for (int parent = bone.parent;
             parent >= 0 && parent < static_cast<int>(skeleton.bones.size());
             parent = skeleton.bones[static_cast<std::size_t>(parent)].parent)
            ++depth;
        state.bones.push_back({bone.name, bone.parent, depth});
    }

    if (state.assetIsAnimation) {
        engine::AnimationAssetData animation;
        std::string ignored;
        if (engine::LoadAnimationAsset(
                m_animationAssetPreviewPath, &animation, &ignored)
            && !animation.clips.empty()) {
            for (const std::string& channel :
                 animation.clips.front().channelBoneNames) {
                if (skeleton.Find(channel) >= 0) ++state.matchedChannels;
                else ++state.missingChannels;
            }
        }
    }
    state.compatibilitySummary = state.assetIsAnimation
        ? std::to_string(state.matchedChannels) + " animation channels matched, "
            + std::to_string(state.missingChannels) + " missing"
        : "Exact skeleton identity match";
    state.previewTexture = RenderAnimationAssetPreview(
        *model, clipIndex, state.previewDuration);
    state.previewTime = m_animationAssetPreviewTime;
    return state;
}

EditorDockspace::AnimationPreviewState EditorApp::BuildAnimationPreviewState() {
    if (!m_animationAssetPreviewPath.empty())
        return BuildAnimationAssetPreviewState();
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
    if (selected->navAgentEnabled) {
        auto addMovementDefinition = [&](const char* name,
                                         EditorScene::AnimationParameter::Type type,
                                         float defaultValue) {
            const auto existing = std::find_if(
                state.parameterDefinitions.begin(), state.parameterDefinitions.end(),
                [&](const EditorScene::AnimationParameter& parameter) {
                    return parameter.name == name;
                });
            if (existing == state.parameterDefinitions.end()) {
                state.parameterDefinitions.push_back({name, type, defaultValue});
            }
        };
        addMovementDefinition("Speed", EditorScene::AnimationParameter::Type::Float, 0.0f);
        addMovementDefinition("VerticalSpeed", EditorScene::AnimationParameter::Type::Float, 0.0f);
        addMovementDefinition("IsGrounded", EditorScene::AnimationParameter::Type::Bool, 1.0f);
        addMovementDefinition("IsFalling", EditorScene::AnimationParameter::Type::Bool, 0.0f);
        addMovementDefinition("IsCrouching", EditorScene::AnimationParameter::Type::Bool, 0.0f);
        addMovementDefinition("IsSwimming", EditorScene::AnimationParameter::Type::Bool, 0.0f);
        addMovementDefinition("IsFlying", EditorScene::AnimationParameter::Type::Bool, 0.0f);
    }
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
    for (const EditorScene::AnimationParameter& parameter : state.parameterDefinitions) {
        const auto found = m_animationPreviewParameters.find(parameter.name);
        addPreviewParameter(parameter.name,
            found == m_animationPreviewParameters.end() ? parameter.defaultValue : found->second,
            parameter.type);
    }
    for (const EditorScene::AnimationStateTransition& transition : selected->animationTransitions) {
        const std::string name = transition.parameter.empty() ? std::string("Speed") : transition.parameter;
        const auto found = m_animationPreviewParameters.find(name);
        auto type = EditorScene::AnimationParameter::Type::Float;
        for (const EditorScene::AnimationParameter& definition : state.parameterDefinitions) {
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
    if (m_mode == EditorMode::Edit && controlDown && Pressed(GLFW_KEY_R)) {
        m_scene.ResetSelectedTransform();
        m_log.Info("Reset selected transform");
    }
    const bool gizmoShortcutAllowed = m_mode == EditorMode::Edit
        && !controlDown
        && !m_cameraController.MouseLookActive();
    if (gizmoShortcutAllowed && Pressed(GLFW_KEY_W)) {
        m_gizmo.SetMode(EditorGizmo::Mode::Translate);
        m_log.Info("Gizmo mode: Move (W)");
    }
    if (gizmoShortcutAllowed && Pressed(GLFW_KEY_E)) {
        m_gizmo.SetMode(EditorGizmo::Mode::Rotate);
        m_log.Info("Gizmo mode: Rotate (E)");
    }
    if (gizmoShortcutAllowed && Pressed(GLFW_KEY_R)) {
        m_gizmo.SetMode(EditorGizmo::Mode::Scale);
        m_log.Info("Gizmo mode: Scale (R)");
    }
    if (gizmoShortcutAllowed && Pressed(GLFW_KEY_G)) {
        m_gizmo.SetMode(EditorGizmo::Mode::Scale);
        m_gizmo.SetAxis(EditorGizmo::Axis::All);
        m_log.Info("Gizmo mode: Uniform Scale (G)");
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
    {
        const engine::Window& window = GetWindow();
        DrawEnvironmentSky(m_camera.ViewMatrix(),
                           m_camera.ProjectionMatrix(window.AspectRatio()), sky, true);
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
        lighting.skylightOcclusion = environment.skylightOcclusion;
        lighting.skylightOcclusionStrength = environment.skylightOcclusionStrength;
        lighting.minimumSkylight = environment.minimumSkylight;
        lighting.cloudShadows = environment.clouds && environment.cloudShadows;
        lighting.cloudShadowStrength = environment.cloudShadowStrength;
        lighting.cloudShadowScale = environment.cloudShadowScale;
        lighting.cloudCoverage = environment.cloudCoverage;
        lighting.cloudDensity = environment.cloudDensity;
        lighting.cloudSoftness = environment.cloudSoftness;
        lighting.cloudWindSpeed = environment.cloudWindSpeed;
        lighting.cloudWindDirectionDegrees = environment.cloudWindDirection;
        lighting.tonemap = !m_renderingHdrPreview;
        lighting.fog = environment.fog;
        lighting.fogColor = sky.horizon;
        lighting.fogDensity = environment.fogDensity;
        lighting.fogHeight = environment.fogHeight;
        lighting.fogHeightFalloff = environment.fogHeightFalloff;
        m_skinnedRenderer->DrawScene(
            *m_playRegistry, m_camera, window.AspectRatio(), lighting);

        // Socketed attachments (weapons/shields) ride the animated bones.
        if (m_modelShader) {
            m_modelShader->Bind();
            m_modelShader->SetMat4("uViewProj", viewProj);
            m_modelShader->SetVec3("uLightPos", m_camera.Position() + glm::vec3(-4.0f, 6.0f, 4.0f));
            m_modelShader->SetVec3("uLightColor", glm::vec3(1.0f));
            m_modelShader->SetVec3("uViewPos", m_camera.Position());
            m_playRegistry->view<Transform, engine::AnimatedModel>().each(
                [&](Entity, Transform& t, engine::AnimatedModel& am) {
                    if (am.attachments.empty()) return;
                    engine::DrawAnimatedModelAttachments(am, t.Model() * am.renderOffset, *m_modelShader);
                });
        }
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

    if (m_shader && m_cube && m_mode == EditorMode::Play
        && m_showGameplayTraces && !m_playPhysics.DebugTraces().empty()) {
        std::vector<EditorViewport::GameplayTraceGuide> traceGuides;
        traceGuides.reserve(m_playPhysics.DebugTraces().size());
        for (const engine::DebugTrace& trace : m_playPhysics.DebugTraces()) {
            traceGuides.push_back(EditorViewport::GameplayTraceGuide{
                trace.start, trace.end, trace.radius,
                static_cast<int>(trace.type), trace.hit});
        }
        m_viewport.DrawGameplayTraceGuides(
            m_renderer, *m_shader, *m_cube, traceGuides, viewProj);
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
            if (guide.hasTarget) {
                if (const Transform* targetTransform =
                        m_playRegistry->TryGet<Transform>(playAgent.targetEntity)) {
                    guide.targetPosition = targetTransform->position;
                } else {
                    guide.hasTarget = false;
                }
            }
            for (const EditorScene::Object& object : m_scene.Objects()) {
                if (object.name == playAgent.name) {
                    guide.visionRange = object.navAgentVisionRange;
                    guide.visionHalfAngleDeg = object.navAgentVisionHalfAngle;
                    break;
                }
            }
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

void EditorApp::UpdateEditParticlePreviews(float dt)
{
    std::unordered_set<Entity> activeSceneEntities;

    auto syncPreviewSystem = [](engine::ParticleSystemComponent& preview,
                                const engine::ParticleSystemComponent& authored,
                                bool newlyCreated) {
        preview.config = authored.config;
        preview.enabled = authored.enabled;
        preview.prewarm = authored.prewarm;
        preview.duration = authored.duration;
        preview.simulationSpeed = authored.simulationSpeed;
        preview.localSpace = authored.localSpace;
        preview.burstCount = authored.burstCount;
        preview.burstInterval = authored.burstInterval;

        // Authoring previews must remain observable even when the gameplay asset is
        // configured for script-only playback or as a one-shot. These overrides live
        // only in the preview registry; the saved scene values are never modified.
        preview.autoplay = true;
        preview.loop = true;
        preview.startDelay = 0.0f;
        if (newlyCreated) {
            preview.initialized = false;
            preview.playing = preview.enabled;
        }
    };

    for (const EditorScene::Object& object : m_scene.Objects()) {
        const bool hasSystem = object.particleSystemEnabled;
        const bool hasEffect = !object.particleEffectLayers.empty();
        if (!hasSystem && !hasEffect) continue;

        const Transform* authoredTransform = m_scene.TryGetTransform(object.entity);
        if (!authoredTransform) continue;
        activeSceneEntities.insert(object.entity);

        Entity previewEntity = engine::ecs::kNull;
        auto previewIt = m_editParticlePreviewEntities.find(object.entity);
        if (previewIt == m_editParticlePreviewEntities.end()
            || !m_editParticlePreviewRegistry.Valid(previewIt->second)) {
            previewEntity = m_editParticlePreviewRegistry.Create();
            m_editParticlePreviewEntities[object.entity] = previewEntity;
            m_editParticlePreviewRegistry.Add<Transform>(previewEntity, *authoredTransform);
        } else {
            previewEntity = previewIt->second;
            *m_editParticlePreviewRegistry.TryGet<Transform>(previewEntity) =
                *authoredTransform;
        }

        if (hasSystem) {
            engine::ParticleSystemComponent authored;
            authored.config = object.particleConfig;
            authored.enabled = true;
            authored.autoplay = object.particleAutoplay;
            authored.loop = object.particleLoop;
            authored.prewarm = object.particlePrewarm;
            authored.duration = object.particleDuration;
            authored.startDelay = object.particleStartDelay;
            authored.simulationSpeed = object.particleSimulationSpeed;
            authored.localSpace = object.particleLocalSpace;
            authored.burstCount = object.particleBurstCount;
            authored.burstInterval = object.particleBurstInterval;

            engine::ParticleSystemComponent* preview =
                m_editParticlePreviewRegistry.TryGet<engine::ParticleSystemComponent>(
                    previewEntity);
            if (!preview) {
                engine::ParticleSystemComponent created;
                syncPreviewSystem(created, authored, true);
                m_editParticlePreviewRegistry.Add<engine::ParticleSystemComponent>(
                    previewEntity, std::move(created));
            } else {
                syncPreviewSystem(*preview, authored, false);
            }
        } else {
            m_editParticlePreviewRegistry.Remove<engine::ParticleSystemComponent>(
                previewEntity);
        }

        if (hasEffect) {
            engine::ParticleEffectComponent* preview =
                m_editParticlePreviewRegistry.TryGet<engine::ParticleEffectComponent>(
                    previewEntity);
            if (!preview) {
                engine::ParticleEffectComponent created;
                created.enabled = true;
                created.layers = object.particleEffectLayers;
                for (engine::ParticleEffectLayer& layer : created.layers) {
                    const engine::ParticleSystemComponent authored = layer.system;
                    layer.system = {};
                    syncPreviewSystem(layer.system, authored, true);
                }
                m_editParticlePreviewRegistry.Add<engine::ParticleEffectComponent>(
                    previewEntity, std::move(created));
            } else {
                preview->enabled = true;
                if (preview->layers.size() != object.particleEffectLayers.size()) {
                    preview->layers = object.particleEffectLayers;
                    for (engine::ParticleEffectLayer& layer : preview->layers) {
                        const engine::ParticleSystemComponent authored = layer.system;
                        layer.system = {};
                        syncPreviewSystem(layer.system, authored, true);
                    }
                } else {
                    for (std::size_t i = 0; i < preview->layers.size(); ++i) {
                        engine::ParticleEffectLayer& layer = preview->layers[i];
                        const engine::ParticleEffectLayer& authored =
                            object.particleEffectLayers[i];
                        layer.name = authored.name;
                        layer.assetPath = authored.assetPath;
                        layer.offset = authored.offset;
                        layer.enabled = authored.enabled;
                        syncPreviewSystem(layer.system, authored.system, false);
                    }
                }
            }
        } else {
            m_editParticlePreviewRegistry.Remove<engine::ParticleEffectComponent>(
                previewEntity);
        }
    }

    for (auto it = m_editParticlePreviewEntities.begin();
         it != m_editParticlePreviewEntities.end();) {
        if (activeSceneEntities.find(it->first) == activeSceneEntities.end()) {
            m_editParticlePreviewRegistry.Destroy(it->second);
            it = m_editParticlePreviewEntities.erase(it);
        } else {
            ++it;
        }
    }

    if (!m_editParticlePreviewEntities.empty()) {
        engine::UpdateParticleSystems(
            m_editParticlePreviewRegistry, std::max(dt, 0.0f));
    }
}

void EditorApp::DrawEditParticlePreviews()
{
    if (!m_particleRenderer || m_editParticlePreviewEntities.empty()) return;

    const engine::Window& window = GetWindow();
    m_particleRenderer->ResetStats();
    m_editParticlePreviewRegistry.view<engine::ParticleSystemComponent>().each(
        [&](Entity, engine::ParticleSystemComponent& system) {
            ResolveParticleGraphShader(system, m_editAssets);
            m_particleRenderer->Draw(
                system, m_camera, window.AspectRatio());
        });
    m_editParticlePreviewRegistry.view<engine::ParticleEffectComponent>().each(
        [&](Entity, engine::ParticleEffectComponent& effect) {
            if (!effect.enabled) return;
            for (engine::ParticleEffectLayer& layer : effect.layers) {
                if (!layer.enabled) continue;
                ResolveParticleGraphShader(layer.system, m_editAssets);
                m_particleRenderer->Draw(
                    layer.system, m_camera, window.AspectRatio());
            }
        });
}

void EditorApp::ClearEditParticlePreviews()
{
    m_editParticlePreviewRegistry = engine::ecs::Registry{};
    m_editParticlePreviewEntities.clear();
}

void EditorApp::DrawEditScene(const glm::mat4 & viewProj)
{
    const EditorScene::Environment& environment = m_scene.GetEnvironment();
    const engine::DayNightCycle::Sample sky = engine::DayNightCycle::At(environment.timeOfDay);
    {
        const engine::Window& window = GetWindow();
        DrawEnvironmentSky(m_camera.ViewMatrix(),
                           m_camera.ProjectionMatrix(window.AspectRatio()), sky, true);
    }

    if (m_pbrRenderer) {
        engine::ecs::Registry pbrRegistry;
        const std::vector<EditorScene::Object>& objects = m_scene.Objects();
        const EditorScene::Object* selectedObject = m_scene.SelectedObject();
        bool environmentSunApplied = false;

        for (const EditorScene::Object& object : objects) {
            if (!object.visible || object.navMeshBoundsVolume || object.isWater
                || object.primitive == EditorScene::Primitive::Empty
                || !object.modelAssetPath.empty()) {
                continue;
            }

            if (object.isTerrain) {
                continue;   // terrain meshes are added by AddTerrainMeshes() below
            }
            if (object.isSpline) {
                continue;   // splines have no mesh; drawn as a curve by DrawSplines()
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
            if (object.decal) {
                material.blendMode = engine::ecs::PbrMaterial::BlendMode::Transparent;
                material.opacity *= object.decalOpacity;
                material.roughness = std::max(material.roughness, 0.35f);
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
        m_pbrRenderer->Render(pbrRegistry, m_camera, window.AspectRatio(), m_renderW, m_renderH, options);
    }
    DrawEditModeModels(viewProj);
    DrawEditParticlePreviews();
    if (m_showGrid) {
        m_viewport.DrawWorldGrid(viewProj);
    }
    if (m_panels.IsOpen(EditorPanels::Panel::LightingAnalysis)
        && m_lightingAnalysis.OverlayEnabled()) {
        std::vector<EditorViewport::LightingAnalysisGuide> guides;
        guides.reserve(m_lightingAnalysis.OverlayCells().size());
        for (const auto& cell : m_lightingAnalysis.OverlayCells())
            guides.push_back({cell.position, cell.value, cell.warning});
        m_viewport.DrawLightingAnalysisOverlay(guides,
            static_cast<int>(m_lightingAnalysis.Mode()),
            m_lightingAnalysis.CellSize(), viewProj);
    }
    if (m_showNavigationPreview && m_shader && m_cube) {
        m_viewport.DrawEditorNavMeshOverlay(m_renderer, *m_shader, *m_cube, m_editorNavMesh, viewProj);
    }
    DrawSelectionOutline(viewProj);
    DrawSplines(viewProj);   // spline curves + control-point handles
    if (m_shader && m_cube) {
        m_viewport.DrawNavMeshBoundsGuides(m_renderer, *m_shader, *m_cube, m_scene, viewProj);
        m_viewport.DrawAudioSourceGuides(m_renderer, *m_shader, *m_cube, m_scene, viewProj);
        // Unreal-style forward arrow so characters can be aimed "forward" when placed.
        m_viewport.DrawCharacterFacingArrows(m_renderer, *m_shader, *m_cube, m_scene, viewProj);
        if (m_showCameraRails) {
            m_viewport.DrawCameraSequenceGuides(
                m_renderer, *m_shader, *m_cube, m_scene, viewProj);
        }
    }
    if (m_showParticleDebug) {
        m_viewport.DrawParticleSystemGuides(
            m_scene, viewProj, m_particleDebugSelectedOnly,
            m_particleDebugShapes, m_particleDebugDirections,
            m_particleDebugBounds, m_particleDebugCullingState);
    }
    if (m_shader && m_cube && environment.showLightGuides) {
        m_viewport.DrawSelectedLightGuide(m_renderer, *m_shader, *m_cube, m_scene, viewProj, environment.selectedLightGuideOnly);
    }
    if (environment.showPhysicsGuides) {
        m_viewport.DrawPhysicsColliderGuides(
            m_scene, viewProj, environment.selectedPhysicsGuideOnly);
    }
    if (m_terrainSculpt && m_terrainBrushHoverValid) {
        const EditorScene::Object* selected = m_scene.SelectedObject();
        if (selected && selected->isTerrain) {
            const auto terrain = m_terrains.find(selected->entity);
            const Transform* transform = m_scene.TryGetTransform(selected->entity);
            if (terrain != m_terrains.end() && transform) {
                m_viewport.DrawTerrainBrushGuide(
                    terrain->second.terrain, *transform,
                    m_terrainBrushCenterLocal, m_terrainBrushRadius,
                    m_terrainSculptMode, m_terrainBrushApplying, viewProj);
            }
        }
    }
    if (m_foliagePaint && m_foliageBrushHoverValid) {
        m_viewport.DrawFoliageBrushGuide(
            m_foliageBrushRing, m_foliageBrushCenterWorld,
            m_foliageErase, m_foliageBrushApplying, viewProj);
    }
    if (m_panels.IsOpen(EditorPanels::Panel::RoomBuilder)
        && m_roomBuilder.HasFirstCorner()) {
        m_viewport.DrawRoomBuilderGuide(
            m_roomBuilder.FirstCorner(), m_roomBuilder.PreviewCorner(),
            m_roomBuilder.WallHeight(), viewProj);
    }
    if (m_panels.IsOpen(EditorPanels::Panel::ProceduralBuilding)) {
        m_viewport.DrawBuildingFootprintGuide(
            m_proceduralBuilding.Footprint(), m_proceduralBuilding.BaseHeight(),
            m_proceduralBuilding.TotalHeight(), viewProj);
    }
    if (m_panels.IsOpen(EditorPanels::Panel::Blockout)) {
        glm::vec3 base = m_blockoutPanel.ManualPosition();
        if (m_blockoutPanel.PlacementMode() == BlockoutPanel::Placement::ViewportCursor)
            base = SceneDropPosition();
        else if (m_blockoutPanel.PlacementMode() == BlockoutPanel::Placement::SelectedObject) {
            if (const engine::ecs::Transform* selected = m_scene.SelectedTransform())
                base = selected->position;
        }
        m_viewport.DrawBlockoutPreview(base, m_blockoutPanel.Dimensions(),
                                       m_blockoutPanel.Yaw(), viewProj);
    }
    if (m_panels.IsOpen(EditorPanels::Panel::ScatterPaint)
        && m_scatterPaint.BrushActive() && m_hasScatterBrushHit) {
        m_viewport.DrawScatterBrush(
            m_scatterBrushPosition, m_scatterBrushNormal, m_scatterPaint.Radius(),
            !m_scatterPaint.Painting(), viewProj);
    }
    if (m_panels.IsOpen(EditorPanels::Panel::ArrayTool)) {
        const EditorScene::Object* selected = m_scene.SelectedObject();
        const engine::ecs::Transform* source = m_scene.SelectedTransform();
        if (selected && source && !selected->locked) {
            const std::vector<engine::ecs::Transform> preview =
                m_arrayTool.BuildTransforms(*source, m_scene);
            std::vector<glm::vec3> positions;
            positions.reserve(preview.size());
            for (const engine::ecs::Transform& transform : preview)
                positions.push_back(transform.position);
            m_viewport.DrawArrayPreview(source->position, positions, viewProj);
        }
    }
    if (m_panels.IsOpen(EditorPanels::Panel::SplineBuilder)) {
        const std::vector<glm::vec3> points = m_splineBuilder.PreviewPoints(m_scene);
        if (!points.empty()) {
            std::vector<glm::vec3> rest(points.begin() + 1, points.end());
            m_viewport.DrawArrayPreview(points.front(), rest, viewProj);
        }
    }
    if (m_panels.IsOpen(EditorPanels::Panel::RoadGenerator)) {
        for (const EditorScene::Object& object : m_scene.Objects()) {
            if (!object.isSpline || object.name != m_roadGenerator.SplineName()
                || object.splinePoints.size() < 2) continue;
            const std::vector<RoadGeneratorPanel::Part> preview =
                m_roadGenerator.GenerateParts(object.splinePoints, object.splineClosed);
            std::vector<glm::vec3> centers;
            for (const RoadGeneratorPanel::Part& part : preview)
                if (part.surface == RoadGeneratorPanel::Surface::Road
                    && part.suffix.rfind("Surface_", 0) == 0)
                    centers.push_back(part.position);
            if (centers.size() > 1) {
                std::vector<glm::vec3> rest(centers.begin() + 1, centers.end());
                m_viewport.DrawArrayPreview(centers.front(), rest, viewProj);
            }
            break;
        }
    }
    if (m_panels.IsOpen(EditorPanels::Panel::Measurement)) {
        std::vector<EditorViewport::MeasurementGuide> guides;
        const auto& measurements = m_measurementPanel.Measurements();
        guides.reserve(measurements.size() + 1);
        for (int i = 0; i < static_cast<int>(measurements.size()); ++i) {
            const MeasurementPanel::Measurement& measurement =
                measurements[static_cast<std::size_t>(i)];
            if (!measurement.visible) continue;
            guides.push_back({measurement.a, measurement.b,
                measurement.type == MeasurementPanel::Type::Box,
                i == m_measurementPanel.SelectedIndex()});
        }
        if (m_measurementPanel.Capturing() && m_measurementPanel.HasFirstPoint()) {
            guides.push_back({m_measurementPanel.FirstPoint(),
                m_measurementPanel.PreviewPoint(),
                m_measurementPanel.CaptureType() == MeasurementPanel::Type::Box, true});
        }
        m_viewport.DrawMeasurementGuides(guides, viewProj);
    }
    if (m_shader && m_cube && environment.showPhysicsGuides) {
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
        const glm::vec3* splinePivot = nullptr;
        if (const EditorScene::Object* selected = m_scene.SelectedObject();
            selected && selected->isSpline
            && (m_gizmo.CurrentMode() == EditorGizmo::Mode::Translate
                || m_gizmo.CurrentMode() == EditorGizmo::Mode::Rotate)
            && m_selectedSplinePoint >= 0
            && m_selectedSplinePoint < static_cast<int>(selected->splinePoints.size())) {
            splinePivot = &selected->splinePoints[static_cast<std::size_t>(m_selectedSplinePoint)];
        }
        m_viewport.DrawSceneGizmo(m_renderer, *m_shader, *m_cube, *m_cone, m_scene, m_gizmo,
            viewProj, m_camera, GetWindow().Height(), splinePivot);
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

void EditorApp::EnsureImportedSky(const EditorScene::Environment& environment) {
    if (environment.skyMode != 1 || environment.skyTexturePath.empty()) {
        return;   // procedural mode — nothing to load (keep any cached sky for quick toggle)
    }
    if (m_importedSky && m_importedSkyPath == environment.skyTexturePath) {
        return;   // already loaded
    }
    try {
        m_importedSky.emplace(
            engine::Skybox::FromEquirectangular(environment.skyTexturePath, 1024));
        m_importedSkyPath = environment.skyTexturePath;
        m_log.Info("Loaded sky: " + environment.skyTexturePath);
    } catch (const std::exception& e) {
        m_importedSky.reset();
        m_importedSkyPath.clear();
        if (!m_editTextureLoadErrors[environment.skyTexturePath]) {
            m_editTextureLoadErrors[environment.skyTexturePath] = true;
            m_log.Warning(std::string("Sky import failed: ") + e.what());
        }
    }
}

void EditorApp::DrawEnvironmentSky(const glm::mat4& view, const glm::mat4& projection,
                                   const engine::DayNightCycle::Sample& sky, bool tonemap) {
    const EditorScene::Environment& environment = m_scene.GetEnvironment();
    EnsureImportedSky(environment);
    if (environment.skyMode == 1 && m_importedSky) {
        m_importedSky->Draw(view, projection, tonemap,
                            glm::radians(environment.skyRotation),
                            std::max(environment.skyIntensity, 0.0f));
    } else if (m_sky) {
        m_sky->Draw(view, projection, sky, tonemap, SkyClouds(environment));
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

    EnsureImportedSky(environment);

    // Re-bake the IBL when the sky source changes (imported path/rotation/intensity/mode)
    // or, for the procedural sky, as the day/night shifts.
    std::string signature = "proc";
    if (environment.skyMode == 1 && m_importedSky) {
        signature = "img|" + environment.skyTexturePath + "|"
            + std::to_string(environment.skyRotation) + "|"
            + std::to_string(environment.skyIntensity);
    }
    const bool skyChanged = signature != m_lastSkySignature;
    const bool dayChanged = (environment.skyMode != 1)
        && std::abs(sky.dayFactor - m_lastIblDay) > 0.04f;
    if (skyChanged || dayChanged) {
        m_ibl->Generate([&](const glm::mat4& view, const glm::mat4& projection) {
            DrawEnvironmentSky(view, projection, sky, false);
        });
        m_lastIblDay = sky.dayFactor;
        m_lastSkySignature = signature;
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
    options.skylightOcclusion = environment.skylightOcclusion;
    options.skylightOcclusionStrength = environment.skylightOcclusionStrength;
    options.minimumSkylight = environment.minimumSkylight;
    options.pointShadows = environment.pointShadows;
    options.spotShadows = environment.spotShadows;
    options.directionalShadows = environment.directionalShadows;
    options.shadowSoftness = environment.shadowSoftness;
    options.shadowDistance = environment.shadowDistance;
    options.cloudShadows = environment.clouds && environment.cloudShadows;
    options.cloudShadowStrength = environment.cloudShadowStrength;
    options.cloudShadowScale = environment.cloudShadowScale;
    options.cloudCoverage = environment.cloudCoverage;
    options.cloudDensity = environment.cloudDensity;
    options.cloudSoftness = environment.cloudSoftness;
    options.cloudWindSpeed = environment.cloudWindSpeed;
    options.cloudWindDirectionDegrees = environment.cloudWindDirection;
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

    // Asset selection and drag creation are owned by the ImGui Assets panel.
    // This handler only tracks and completes a drag that the panel started.
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
    // Suppress object selection while the right mouse button is held: RMB drives camera
    // movement, so a left-click during it should not pick/deselect. This is a hard guard
    // in addition to MouseLookActive(), which only latches when the RMB press started on
    // a viewport point (misses cases like starting the drag over a gizmo or panel edge).
    const bool rightDown = window.Native()
        && glfwGetMouseButton(window.Native(), GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    if (m_cameraController.MouseLookActive() || rightDown
        || m_mode != EditorMode::Edit || !window.Native()) {
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
    float px = x, py = y;                 // px,py = scene render-pixel space (panel-aware)
    RemapViewportMouse(x, y, px, py);

    const bool activeLeftGizmo =  m_mouse.GizmoActiveFor(GLFW_MOUSE_BUTTON_LEFT);
    if (m_dragDrop.HasPayload() || (!IsViewportDropPosition(x, y) && !activeLeftGizmo)) {
        return;
    }

    const glm::mat4 viewProj = m_camera.ProjectionMatrix(window.AspectRatio()) * m_camera.ViewMatrix();

    if (m_panels.IsOpen(EditorPanels::Panel::Measurement)
        && m_measurementPanel.Capturing()) {
        glm::vec3 hitPosition(0.0f), hitNormal(0.0f, 1.0f, 0.0f);
        const int hit = m_viewport.PickSceneObject(
            m_scene, m_editAssets, px, py, viewProj, window.Width(), window.Height(),
            &hitPosition, &hitNormal);
        if (hit < 0) {
            hitPosition = m_viewport.SceneDropPosition(
                px, py, viewProj, window.Width(), window.Height());
        } else if (hit < static_cast<int>(m_scene.Objects().size())
            && m_scene.Objects()[static_cast<std::size_t>(hit)].isTerrain) {
            bool overTerrain = false;
            const float terrainY = TerrainSurfaceY(
                hitPosition.x, hitPosition.z, overTerrain);
            if (overTerrain) hitPosition.y = terrainY;
        }
        m_measurementPanel.SetHoverPoint(hitPosition);
        if (left.pressed) m_measurementPanel.CapturePoint(hitPosition);
        return;
    }

    if (m_panels.IsOpen(EditorPanels::Panel::DecalPlacement)
        && m_decalPlacement.PlacementActive()) {
        glm::vec3 hitPosition(0.0f), hitNormal(0.0f, 1.0f, 0.0f);
        const int hit = m_viewport.PickSceneObject(
            m_scene, m_editAssets, px, py, viewProj, window.Width(), window.Height(),
            &hitPosition, &hitNormal, "Decal_");
        const bool valid = hit >= 0;
        m_decalPlacement.SetHover(hitPosition, hitNormal, valid);
        if (left.pressed && valid && m_plane) {
            const auto& decal = m_decalPlacement.Current();
            m_scene.AddDecal(*m_plane, hitPosition, hitNormal, decal.size,
                             decal.rotationDegrees, decal.surfaceOffset,
                             decal.opacity, decal.materialPath);
            m_log.Info("Placed decal: " + decal.materialPath);
        }
        return;
    }

    if (m_panels.IsOpen(EditorPanels::Panel::ScatterPaint)
        && m_scatterPaint.BrushActive()) {
        glm::vec3 hitPosition(0.0f), hitNormal(0.0f, 1.0f, 0.0f);
        const int hit = m_viewport.PickSceneObject(
            m_scene, m_editAssets, px, py, viewProj, window.Width(), window.Height(),
            &hitPosition, &hitNormal, "Scatter_");
        if (hit < 0) {
            hitPosition = m_viewport.SceneDropPosition(
                px, py, viewProj, window.Width(), window.Height());
            hitNormal = glm::vec3(0, 1, 0);
        }
        const bool terrainHit = hit >= 0
            && hit < static_cast<int>(m_scene.Objects().size())
            && m_scene.Objects()[static_cast<std::size_t>(hit)].isTerrain;
        if (terrainHit) {
            bool overTerrain = false;
            const float terrainSurfaceY = TerrainSurfaceY(
                hitPosition.x, hitPosition.z, overTerrain);
            if (overTerrain) {
                hitPosition.y = terrainSurfaceY;
                const float sample = 0.25f;
                bool leftOver = false, rightOver = false, backOver = false, frontOver = false;
                const float leftY = TerrainSurfaceY(hitPosition.x - sample, hitPosition.z, leftOver);
                const float rightY = TerrainSurfaceY(hitPosition.x + sample, hitPosition.z, rightOver);
                const float backY = TerrainSurfaceY(hitPosition.x, hitPosition.z - sample, backOver);
                const float frontY = TerrainSurfaceY(hitPosition.x, hitPosition.z + sample, frontOver);
                if (leftOver && rightOver && backOver && frontOver) {
                    hitNormal = glm::normalize(glm::vec3(
                        leftY - rightY, sample * 2.0f, backY - frontY));
                }
            }
        }
        m_scatterBrushPosition = hitPosition;
        m_scatterBrushNormal = hitNormal;
        m_hasScatterBrushHit = true;
        if (left.pressed) m_hasLastScatterStrokePosition = false;
        if (left.released) m_hasLastScatterStrokePosition = false;
        if (left.down) {
            const bool spaced = !m_hasLastScatterStrokePosition
                || glm::distance(hitPosition, m_lastScatterStrokePosition)
                   >= m_scatterPaint.StrokeSpacing();
            if (spaced) {
                if (m_scatterPaint.Painting())
                    PaintScatterStamp(hitPosition, hitNormal, terrainHit);
                else EraseScatterAt(hitPosition, m_scatterPaint.Radius());
                m_lastScatterStrokePosition = hitPosition;
                m_hasLastScatterStrokePosition = true;
            }
        }
        return;
    }

    if (m_panels.IsOpen(EditorPanels::Panel::RoomBuilder)
        && m_roomBuilder.CapturingOutline()) {
        const glm::vec3 point = m_viewport.SceneDropPosition(
            px, py, viewProj, window.Width(), window.Height());
        m_roomBuilder.SetHoverPoint(point);
        if (left.pressed) m_roomBuilder.CapturePoint(point);
        return;
    }

    if (m_panels.IsOpen(EditorPanels::Panel::PrefabPalette)
        && m_prefabPalette.PlacementActive()) {
        if (left.pressed) {
            const PrefabPalettePanel::Placement placement = m_prefabPalette.NextPlacement();
            glm::vec3 placementPosition(0.0f), surfaceNormal(0.0f, 1.0f, 0.0f);
            if (ComputePrefabPalettePlacement(
                    px, py, placement, &placementPosition, &surfaceNormal)) {
                PlacePalettePrefab(placement, placementPosition);
            }
        }
        return;
    }

    // Modular placement owns left-clicks while active. This prevents the same click
    // from selecting an object or starting a gizmo drag underneath the placement tool.
    if (m_panels.IsOpen(EditorPanels::Panel::ModularPlacement)
        && m_modularPlacement.PlacementActive()) {
        if (left.pressed) m_hasLastModulePaintPosition = false;
        if (left.released) m_hasLastModulePaintPosition = false;
        const bool wantsPlacement = left.pressed
            || (m_modularPlacement.PaintMode() && left.down);
        if (wantsPlacement) {
            glm::vec3 placementPosition(0.0f), surfaceNormal(0.0f, 1.0f, 0.0f);
            if (ComputeModularPlacement(px, py, &placementPosition, &surfaceNormal)) {
                const bool spaced = !m_hasLastModulePaintPosition
                    || glm::distance(placementPosition, m_lastModulePaintPosition)
                       >= m_modularPlacement.PaintSpacing();
                if (!m_modularPlacement.PaintMode() || spaced) {
                    if (PlaceSelectedModule(placementPosition, surfaceNormal)) {
                        m_lastModulePaintPosition = placementPosition;
                        m_hasLastModulePaintPosition = true;
                    }
                }
            }
        }
        return;
    }

    if (left.pressed) {
        const EditorScene::Object* selected = m_scene.SelectedObject();
        const glm::vec3* splinePivot = nullptr;
        if (selected && selected->isSpline
            && (m_gizmo.CurrentMode() == EditorGizmo::Mode::Translate
                || m_gizmo.CurrentMode() == EditorGizmo::Mode::Rotate)
            && m_selectedSplinePoint >= 0
            && m_selectedSplinePoint < static_cast<int>(selected->splinePoints.size())) {
            splinePivot = &selected->splinePoints[static_cast<std::size_t>(m_selectedSplinePoint)];
        }
        if (m_viewport.PickGizmoHandle(m_gizmo, m_scene, px, py, viewProj,
                window.Width(), window.Height(), m_camera, splinePivot)) {
            m_mouse.BeginGizmoDrag(GLFW_MOUSE_BUTTON_LEFT, px, py);
            m_transformController.BeginGizmoDrag();
            m_scene.BeginTransformEdit();
            m_log.Info(std::string("Mouse gizmo: ") + m_gizmo.ModeName() + " " + m_gizmo.AxisName());
        } else if (const int point = m_viewport.PickSplinePoint(
                       m_scene, px, py, viewProj, window.Width(), window.Height()); point >= 0) {
            m_selectedSplinePoint = point;
            m_gizmo.SetMode(EditorGizmo::Mode::Translate);
            m_log.Info("Selected spline point " + std::to_string(point + 1));
        } else {
            const int picked = m_viewport.PickSceneObject(m_scene, m_editAssets, px, py, viewProj, window.Width(), window.Height());
            const bool shiftHeld = window.IsKeyPressed(GLFW_KEY_LEFT_SHIFT)
                || window.IsKeyPressed(GLFW_KEY_RIGHT_SHIFT);
            if (picked >= 0) {
                // Shift+click adds/removes the object from the multi-selection; a plain
                // click replaces the whole selection with just this object.
                if (shiftHeld) m_scene.ToggleSelection(picked);
                else m_scene.SelectIndex(picked);
                // A control point was handled above. Clicking the curve/object itself
                // returns to whole-spline transform editing, even when it was already selected.
                m_selectedSplinePoint = -1;
                if (const EditorScene::Object* pickedObject = m_scene.SelectedObject()) {
                    m_log.Info((shiftHeld ? "Toggled selection: " : "Selected ")
                        + pickedObject->name + " ("
                        + std::to_string(m_scene.SelectedIndices().size()) + " selected)");
                }
            } else if (!shiftHeld && m_scene.SelectedObject()) {
                // Plain click on empty space clears; Shift+click on empty keeps the group.
                m_scene.Deselect();
                m_selectedSplinePoint = -1;
                m_log.Info("Deselected object");
            }
        }
    }

    if (left.down && m_mouse.GizmoActiveFor(GLFW_MOUSE_BUTTON_LEFT)) {
        const float dx = px - m_mouse.GizmoLastX();
        const float dy = py - m_mouse.GizmoLastY();
        const float pixels = ProjectGizmoDrag(dx, dy, viewProj, window.Width(), window.Height());

        if (pixels != 0.0f) {
            const EditorScene::Object* selected = m_scene.SelectedObject();
            if (selected && selected->isSpline
                && (m_gizmo.CurrentMode() == EditorGizmo::Mode::Translate
                    || m_gizmo.CurrentMode() == EditorGizmo::Mode::Rotate)
                && m_selectedSplinePoint >= 0
                && m_selectedSplinePoint < static_cast<int>(selected->splinePoints.size())) {
                m_transformController.ApplySplinePointGizmoDrag(
                    m_scene, static_cast<std::size_t>(m_selectedSplinePoint), m_gizmo, pixels);
            } else {
                m_transformController.ApplyGizmoDrag(m_scene, m_gizmo, pixels);
            }
        }

        m_mouse.UpdateGizmoLast(px, py);
    }

    if (left.released && m_mouse.GizmoActiveFor(GLFW_MOUSE_BUTTON_LEFT)) {
        m_scene.EndTransformEdit();
        m_transformController.EndGizmoDrag();
        m_mouse.EndGizmoDrag();
        m_log.Info("Mouse gizmo edit complete");
    }
}

bool EditorApp::AverageImageColor(const std::string& relativePath, glm::vec3& outColor)
{
    // Only PNG/JPEG can be decoded to CPU pixels here; other formats (e.g. TGA) fall
    // back to the material base colour.
    std::string ext;
    if (const auto dot = relativePath.find_last_of('.'); dot != std::string::npos) {
        ext = relativePath.substr(dot + 1);
        for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (ext != "png" && ext != "jpg" && ext != "jpeg") return false;

    const std::filesystem::path full = std::filesystem::path(m_assets.RootPath()) / relativePath;
    engine::image::Image img;
    try {
        img = (ext == "png") ? engine::image::DecodePNG(full.string())
                             : engine::image::DecodeJPEG(full.string());
    } catch (...) {
        return false;
    }
    if (img.rgba.size() < 4 || img.width <= 0 || img.height <= 0) return false;

    glm::vec3 sum(0.0f);
    std::size_t count = 0;
    const std::size_t pixels = img.rgba.size() / 4;
    const std::size_t stride = std::max<std::size_t>(1, pixels / 4096);   // ~4k samples
    for (std::size_t p = 0; p < pixels; p += stride) {
        const std::size_t i = p * 4;
        sum.r += img.rgba[i]; sum.g += img.rgba[i + 1]; sum.b += img.rgba[i + 2];
        ++count;
    }
    if (count == 0) return false;
    outColor = sum / (255.0f * static_cast<float>(count));
    return true;
}

engine::TerrainLayerSurface EditorApp::TerrainLayerMaterialSurface(const std::string& materialPath)
{
    const auto cached = m_terrainMaterialSurfaces.find(materialPath);
    if (cached != m_terrainMaterialSurfaces.end()) return cached->second;

    engine::TerrainLayerSurface surface;
    surface.albedo = glm::vec3(0.6f);
    std::string error;
    const std::filesystem::path materialFile = std::filesystem::path(materialPath).is_absolute()
        ? std::filesystem::path(materialPath)
        : std::filesystem::path(m_assets.RootPath()) / materialPath;
    if (const engine::RuntimeMaterialAsset* mat =
            m_editAssets.LoadMaterial(materialFile.string(), &error)) {
        surface.albedo = mat->material.albedo;
        surface.ao = mat->material.ao;
        surface.roughness = mat->material.roughness;
        surface.metallic = mat->material.metallic;
    }
    surface.albedo = glm::clamp(surface.albedo, glm::vec3(0.0f), glm::vec3(1.0f));
    surface.ao = glm::clamp(surface.ao, 0.0f, 1.0f);
    surface.roughness = glm::clamp(surface.roughness, 0.04f, 1.0f);
    surface.metallic = glm::clamp(surface.metallic, 0.0f, 1.0f);
    m_terrainMaterialSurfaces[materialPath] = surface;
    return surface;
}

engine::TerrainLayerTexture EditorApp::TerrainLayerMaterialTexture(
    const std::string& materialPath) {
    const auto cached = m_terrainMaterialTextures.find(materialPath);
    if (cached != m_terrainMaterialTextures.end()) return cached->second;

    engine::TerrainLayerTexture result;
    const std::filesystem::path materialFile = std::filesystem::path(materialPath).is_absolute()
        ? std::filesystem::path(materialPath)
        : std::filesystem::path(m_assets.RootPath()) / materialPath;
    std::string error;
    const engine::RuntimeMaterialAsset* material =
        m_editAssets.LoadMaterial(materialFile.string(), &error);
    if (!material) {
        m_terrainMaterialTextures[materialPath] = result;
        return result;
    }
    result.tiling = glm::max(material->material.uvScale * 8.0f, glm::vec2(0.001f));

    auto readPixels = [this](const std::string& path, std::vector<std::uint8_t>* pixels,
                             int* width, int* height) {
        if (path.empty() || !pixels || !width || !height) return false;
        const std::filesystem::path file = std::filesystem::path(path).is_absolute()
            ? std::filesystem::path(path)
            : std::filesystem::path(m_assets.RootPath()) / path;
        std::string extension = file.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (extension == ".3dgtex") {
            engine::TextureAssetData texture;
            std::string loadError;
            if (!engine::LoadTextureAsset(file.string(), &texture, &loadError)) return false;
            *width = static_cast<int>(texture.width);
            *height = static_cast<int>(texture.height);
            *pixels = std::move(texture.rgba);
            return !pixels->empty();
        }
        try {
            engine::image::Image image;
            if (extension == ".png") image = engine::image::DecodePNG(file.string());
            else if (extension == ".jpg" || extension == ".jpeg")
                image = engine::image::DecodeJPEG(file.string());
            else return false;
            *width = image.width;
            *height = image.height;
            *pixels = std::move(image.rgba);
            return !pixels->empty();
        } catch (...) {
            return false;
        }
    };
    readPixels(material->albedoMapPath, &result.albedoRgba,
               &result.albedoWidth, &result.albedoHeight);
    readPixels(material->metalRoughMapPath, &result.ormRgba,
               &result.ormWidth, &result.ormHeight);
    m_terrainMaterialTextures[materialPath] = result;
    return result;
}

void EditorApp::AddTerrainMeshes(engine::ecs::Registry& pbrRegistry)
{
    // Terrain and grass are generated render caches rather than scene objects.
    // Remove entries whose owning landscape no longer exists before drawing;
    // otherwise deleting a landscape leaves its last grass field visible.
    std::unordered_set<Entity> liveTerrains;
    std::unordered_set<Entity> liveGrassFields;
    for (const EditorScene::Object& object : m_scene.Objects()) {
        if (!object.isTerrain) continue;
        liveTerrains.insert(object.entity);
        if (object.visible && object.grassEnabled)
            liveGrassFields.insert(object.entity);
    }
    for (auto it = m_terrains.begin(); it != m_terrains.end();) {
        if (liveTerrains.find(it->first) == liveTerrains.end())
            it = m_terrains.erase(it);
        else ++it;
    }
    for (auto it = m_grass.begin(); it != m_grass.end();) {
        if (liveGrassFields.find(it->first) == liveGrassFields.end())
            it = m_grass.erase(it);
        else ++it;
    }

    for (const EditorScene::Object& object : m_scene.Objects()) {
        if (!object.isTerrain || !object.visible) {
            continue;
        }
        const Transform* t = m_scene.TryGetTransform(object.entity);
        TerrainCache& tc = m_terrains[object.entity];
        const bool needsGen = tc.terrain.Map().h.empty() ||
            tc.res != object.terrainRes || tc.size != object.terrainSize ||
            tc.maxHeight != object.terrainMaxHeight || tc.seed != object.terrainSeed ||
            tc.octaves != object.terrainOctaves || tc.frequency != object.terrainFrequency;
        if (needsGen) {
            const bool haveHeights = static_cast<int>(object.terrainHeights.size()) ==
                                     object.terrainRes * object.terrainRes;
            if (haveHeights) {
                engine::Heightmap hm;
                hm.res = object.terrainRes;
                hm.size = object.terrainSize;
                hm.origin = glm::vec3(0.0f);
                hm.maxHeight = object.terrainMaxHeight;
                hm.h = object.terrainHeights;
                tc.terrain.SetHeightmap(hm);
            } else {
                tc.terrain.Generate(std::max(object.terrainRes, 2), object.terrainSize,
                                    glm::vec3(0.0f), object.terrainMaxHeight,
                                    static_cast<unsigned>(object.terrainSeed),
                                    std::max(object.terrainOctaves, 1), object.terrainFrequency);
            }
            if (static_cast<int>(object.terrainPaint.size()) == object.terrainRes * object.terrainRes) {
                tc.terrain.SetPaint(object.terrainPaint);   // overlay painted layers
            }
            tc.res = object.terrainRes; tc.size = object.terrainSize;
            tc.maxHeight = object.terrainMaxHeight; tc.seed = object.terrainSeed;
            tc.octaves = object.terrainOctaves; tc.frequency = object.terrainFrequency;
        }
        // If generation somehow produced no mesh/albedo, skip rather than referencing
        // empty resources.
        if (!tc.terrain.Ready()) {
            continue;
        }

        // Paint-layer palette: default colours, overridden by any assigned material's
        // representative colour. SetLayerColors only rebuilds the albedo when it changes.
        engine::TerrainLayerSurface layerSurfaces[6];
        engine::TerrainLayerTexture layerTextures[6];
        engine::DefaultTerrainLayerSurfaces(layerSurfaces);
        for (int layer = 1; layer <= 5; ++layer) {
            const std::string& matPath = object.terrainLayerMaterials[static_cast<std::size_t>(layer - 1)];
            if (!matPath.empty()) {
                layerSurfaces[layer] = TerrainLayerMaterialSurface(matPath);
                layerTextures[layer] = TerrainLayerMaterialTexture(matPath);
            }
        }
        tc.terrain.SetLayerSurfaces(layerSurfaces);
        tc.terrain.SetLayerTextures(layerTextures);

        // Grass: instanced blades on the painted grass layer. Rebuild the scatter only
        // when the terrain/paint/density/position change (cheap signature).
        if (object.grassEnabled) {
            std::unique_ptr<engine::GrassField>& slot = m_grass[object.entity];
            if (!slot) slot = std::make_unique<engine::GrassField>();
            engine::GrassField& grass = *slot;
            engine::GrassConfig gcfg;
            gcfg.density = object.grassDensity;
            gcfg.bladeHeight = object.grassHeight;
            gcfg.randomizeHeight = object.grassRandomizeHeight;
            gcfg.minHeightScale = object.grassMinHeightScale;
            gcfg.maxHeightScale = object.grassMaxHeightScale;
            gcfg.windStrength = object.grassWindStrength;
            gcfg.windSpeed = object.grassWindSpeed;
            gcfg.baseColor = object.grassBaseColor;
            gcfg.tipColor = object.grassTipColor;
            gcfg.grassLayer = 1;   // grass paints on layer 1

            const engine::Heightmap& hm = tc.terrain.Map();
            const std::vector<std::uint8_t>& paint = tc.terrain.Paint();
            const glm::vec3 worldOrigin = t ? t->position : glm::vec3(0.0f);
            auto fbits = [](float f) { unsigned b = 0; std::memcpy(&b, &f, sizeof(float)); return static_cast<std::size_t>(b); };
            std::size_t sig = 1469598103934665603ull;
            auto mix = [&sig](std::size_t v) { sig = (sig ^ v) * 1099511628211ull; };
            mix(static_cast<std::size_t>(hm.res));
            mix(fbits(hm.size)); mix(fbits(hm.maxHeight));   // NOT the active settings -- those
            mix(object.grassRandomizeHeight ? 1u : 0u);
            mix(fbits(object.grassMinHeightScale));
            mix(fbits(object.grassMaxHeightScale));
            mix(fbits(worldOrigin.x)); mix(fbits(worldOrigin.y)); mix(fbits(worldOrigin.z));  // only bake at paint time
            mix(paint.size());
            const std::size_t pstride = std::max<std::size_t>(1, paint.size() / 512);
            for (std::size_t k = 0; k < paint.size(); k += pstride) mix(paint[k]);
            const std::size_t hstride = std::max<std::size_t>(1, hm.h.size() / 512);
            for (std::size_t k = 0; k < hm.h.size(); k += hstride) mix(fbits(hm.h[k]));
            // Per-region style: rebuild when the style map or palette changes.
            const std::vector<unsigned char>& styleMap = object.terrainGrassStyle;
            mix(styleMap.size());
            const std::size_t sstride = std::max<std::size_t>(1, styleMap.size() / 512);
            for (std::size_t k = 0; k < styleMap.size(); k += sstride) mix(styleMap[k]);
            mix(object.terrainGrassPalette.size());
            for (const auto& e : object.terrainGrassPalette) {
                mix(fbits(e.density)); mix(fbits(e.height));
                mix(fbits(e.base.r)); mix(fbits(e.tip.r));
            }

            // Convert the editor palette to engine styles.
            std::vector<engine::GrassStyle> palette;
            palette.reserve(object.terrainGrassPalette.size());
            for (const auto& e : object.terrainGrassPalette) {
                engine::GrassStyle gs;
                gs.density = e.density; gs.bladeHeight = e.height; gs.base = e.base; gs.tip = e.tip;
                palette.push_back(gs);
            }
            if (grass.Signature() != sig) {
                grass.Build(hm, paint, styleMap, palette, worldOrigin, gcfg);
                grass.SetSignature(sig);
            } else {
                grass.SetConfig(gcfg);   // wind/width are live uniforms
            }
        } else {
            m_grass.erase(object.entity);   // disabled -> drop the field
        }

        engine::ecs::PbrMaterial mat;
        mat.albedo = glm::vec3(1.0f);
        // Generated ORM already contains the final per-layer values. Unit scalar
        // factors keep those channels intact in the generic PBR shader.
        mat.ao = 1.0f;
        mat.roughness = 1.0f;
        mat.metallic = 1.0f;
        mat.albedoMap = &tc.terrain.Albedo();
        mat.metalRoughMap = &tc.terrain.SurfaceMap();
        const Entity e = pbrRegistry.Create();
        pbrRegistry.Add<Transform>(e, t ? *t : Transform{});
        pbrRegistry.Add<engine::ecs::MeshPBR>(e, engine::ecs::MeshPBR{&tc.terrain.GetMesh(), mat});
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
        const glm::vec3 scale = t ? glm::abs(t->scale) : glm::vec3(1.0f);
        const float sx = std::max(scale.x, 0.0001f);
        const float sy = std::max(scale.y, 0.0001f);
        const float sz = std::max(scale.z, 0.0001f);
        const float lx = (worldX - base.x) / sx;
        const float lz = (worldZ - base.z) / sz;
        const float size = it->second.terrain.Map().size;
        if (lx < 0.0f || lz < 0.0f || lx > size || lz > size) {
            continue;   // outside this terrain's footprint
        }
        const float localY = it->second.terrain.HeightAt(lx, lz);
        const float y = base.y + localY * sy;
        if (!over || y > best) { best = y; over = true; }   // highest terrain wins on overlap
    }
    return best;
}

// Name of the highest terrain object whose footprint contains (worldX, worldZ), or empty.
// Mirrors the footprint test in TerrainSurfaceY so painted foliage can be bound to its ground.
std::string EditorApp::TerrainNameAt(float worldX, float worldZ)
{
    std::string name;
    float best = 0.0f;
    bool found = false;
    for (const EditorScene::Object& object : m_scene.Objects()) {
        if (!object.isTerrain) continue;
        const auto it = m_terrains.find(object.entity);
        if (it == m_terrains.end() || it->second.terrain.Map().h.empty()) continue;
        const Transform* t = m_scene.TryGetTransform(object.entity);
        const glm::vec3 base = t ? t->position : glm::vec3(0.0f);
        const glm::vec3 scale = t ? glm::abs(t->scale) : glm::vec3(1.0f);
        const float sx = std::max(scale.x, 0.0001f);
        const float sy = std::max(scale.y, 0.0001f);
        const float sz = std::max(scale.z, 0.0001f);
        const float lx = (worldX - base.x) / sx;
        const float lz = (worldZ - base.z) / sz;
        const float size = it->second.terrain.Map().size;
        if (lx < 0.0f || lz < 0.0f || lx > size || lz > size) continue;
        const float y = base.y + it->second.terrain.HeightAt(lx, lz) * sy;
        if (!found || y > best) { best = y; found = true; name = object.name; }
    }
    return name;
}

void EditorApp::HandleFoliagePaint()
{
    m_foliageBrushHoverValid = false;
    m_foliageBrushApplying = false;
    m_foliageBrushRing.clear();
    m_foliageStrokeCooldown = std::max(0.0f, m_foliageStrokeCooldown - m_dt);
    if (m_mode != EditorMode::Edit || m_cameraController.MouseLookActive()) return;
    const EditorScene::Object* selected = m_scene.SelectedObject();
    if (!selected || !selected->isFoliage || selected->foliageAssetPath.empty()) return;

    engine::Window& window = GetWindow();
    if (!window.Native()) return;
    double cx = 0.0, cy = 0.0;
    glfwGetCursorPos(window.Native(), &cx, &cy);
    if (!IsViewportDropPosition(static_cast<float>(cx), static_cast<float>(cy))) return;
    glm::vec3 center = SceneDropPosition();
    bool overTerrain = false;
    const float terrainY = TerrainSurfaceY(center.x, center.z, overTerrain);
    if (overTerrain) center.y = terrainY;
    center.y += 0.025f;
    m_foliageBrushCenterWorld = center;
    m_foliageBrushHoverValid = true;
    constexpr int ringSegments = 96;
    m_foliageBrushRing.reserve(ringSegments);
    for (int i = 0; i < ringSegments; ++i) {
        const float angleValue = glm::two_pi<float>() * static_cast<float>(i)
            / static_cast<float>(ringSegments);
        glm::vec3 point(center.x + std::cos(angleValue) * m_foliageBrushRadius,
                        center.y,
                        center.z + std::sin(angleValue) * m_foliageBrushRadius);
        bool ringOverTerrain = false;
        const float ringY = TerrainSurfaceY(point.x, point.z, ringOverTerrain);
        if (ringOverTerrain) point.y = ringY + 0.025f;
        m_foliageBrushRing.push_back(point);
    }
    m_foliageBrushApplying =
        glfwGetMouseButton(window.Native(), GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    if (!m_foliageBrushApplying) return;

    if (m_foliageErase) {
        if (m_foliageStrokeCooldown <= 0.0f) {
            m_scene.EraseSelectedFoliageInstances(center, m_foliageBrushRadius);
            m_foliageStrokeCooldown = 0.04f;
        }
        return;
    }
    if (m_foliageStrokeCooldown > 0.0f) return;

    std::string error;
    const engine::FoliageAssetData* asset =
        m_editAssets.LoadFoliage(selected->foliageAssetPath, &error);
    if (!asset || asset->types.empty()) {
        if (!error.empty()) m_log.Warning("Foliage paint: " + error);
        m_foliagePaint = false;
        return;
    }
    const int typeIndex = std::clamp(
        m_foliageTypeIndex, 0, static_cast<int>(asset->types.size()) - 1);
    const engine::FoliageTypeAsset& type = asset->types[static_cast<std::size_t>(typeIndex)];
    const float radius = std::max(m_foliageBrushRadius, 0.1f);
    const float area = glm::pi<float>() * radius * radius;
    const int attempts = std::clamp(static_cast<int>(std::ceil(
        area * type.density * std::max(m_foliagePaintDensity, 0.01f) * 0.025f)), 1, 64);

    static std::mt19937 random{0x3D6F11A6u};
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    std::uniform_real_distribution<float> angle(0.0f, glm::two_pi<float>());
    const Transform* owner = m_scene.TryGetTransform(selected->entity);
    const glm::mat4 ownerModel = owner ? owner->Model() : glm::mat4(1.0f);
    for (int i = 0; i < attempts; ++i) {
        const float a = angle(random);
        const float d = std::sqrt(unit(random)) * radius;
        glm::vec3 position(center.x + std::cos(a) * d, center.y,
                           center.z + std::sin(a) * d);
        bool sampleOverTerrain = false;
        const float sampleY = TerrainSurfaceY(position.x, position.z, sampleOverTerrain);
        if (sampleOverTerrain) position.y = sampleY;
        if (position.y < type.minimumWorldHeight || position.y > type.maximumWorldHeight) continue;

        glm::vec3 surfaceNormal(0.0f, 1.0f, 0.0f);
        if (sampleOverTerrain) {
            constexpr float probe = 0.2f;
            bool leftOver = false, rightOver = false, backOver = false, frontOver = false;
            const float left = TerrainSurfaceY(position.x - probe, position.z, leftOver);
            const float right = TerrainSurfaceY(position.x + probe, position.z, rightOver);
            const float back = TerrainSurfaceY(position.x, position.z - probe, backOver);
            const float front = TerrainSurfaceY(position.x, position.z + probe, frontOver);
            if (leftOver && rightOver && backOver && frontOver) {
                surfaceNormal = glm::normalize(glm::vec3(
                    left - right, probe * 2.0f, back - front));
            }
        }
        const float slope = glm::degrees(std::acos(
            std::clamp(surfaceNormal.y, 0.0f, 1.0f)));
        if (slope < type.minimumSlopeDegrees || slope > type.maximumSlopeDegrees) continue;

        bool tooClose = false;
        const float spacing2 = type.minimumSpacing * type.minimumSpacing;
        for (const engine::ecs::FoliageInstance& existing : selected->foliageInstances) {
            if (!existing.enabled || existing.typeIndex != static_cast<std::uint32_t>(typeIndex)) continue;
            const glm::vec3 world = glm::vec3(ownerModel * glm::vec4(existing.position, 1.0f));
            const glm::vec2 delta(world.x - position.x, world.z - position.z);
            if (glm::dot(delta, delta) < spacing2) { tooClose = true; break; }
        }
        if (tooClose) continue;

        const glm::vec3 scale = glm::mix(type.minScale, type.maxScale, unit(random));
        glm::vec3 rotation = glm::mix(type.minRotation,
                                      type.maxRotation, unit(random));
        if (type.randomYaw) rotation.y += unit(random) * 360.0f;
        if (type.alignToSurface) {
            rotation.x += glm::degrees(std::atan2(surfaceNormal.z, surfaceNormal.y));
            rotation.z -= glm::degrees(std::atan2(surfaceNormal.x, surfaceNormal.y));
        }
        m_scene.AddSelectedFoliageInstance(position, rotation, scale,
                                           static_cast<std::uint32_t>(typeIndex));
    }
    // Bind this foliage to the terrain under the brush so deleting the terrain also
    // removes the grass painted onto it (grass "belongs" to its ground).
    if (overTerrain && selected->foliageTerrainOwner.empty()) {
        const std::string terrainName = TerrainNameAt(center.x, center.z);
        if (!terrainName.empty()) m_scene.SetSelectedFoliageTerrainOwner(terrainName);
    }
    // Resolve after each stroke so a freshly assigned foliage asset becomes visible.
    m_editAssets.ResolveRegistryAssets(m_scene.Registry());
    m_foliageStrokeCooldown = 0.08f;
}

void EditorApp::HandleTerrainSculpt()
{
    m_terrainBrushHoverValid = false;
    m_terrainBrushApplying = false;
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
    const auto it = m_terrains.find(sel->entity);
    if (it == m_terrains.end() || it->second.terrain.Map().h.empty()) {
        return;
    }

    const glm::vec3 hit = SceneDropPosition();   // world XZ under the cursor (ground plane)
    const engine::ecs::Transform* t = m_scene.TryGetTransform(sel->entity);
    const glm::vec3 base = t ? t->position : glm::vec3(0.0f);

    engine::Heightmap& hm = it->second.terrain.MutableMap();
    const int   res  = hm.res;
    if (res < 2) return;
    const float cell = hm.size / static_cast<float>(res - 1);
    const float lx = hit.x - base.x;   // brush centre in terrain-local XZ
    const float lz = hit.z - base.z;
    const float radius = std::max(m_terrainBrushRadius, 0.1f);
    if (lx + radius < 0.0f || lz + radius < 0.0f
        || lx - radius > hm.size || lz - radius > hm.size) {
        return;
    }

    m_terrainBrushCenterLocal = glm::vec2(lx, lz);
    m_terrainBrushHoverValid = true;
    m_terrainBrushApplying =
        glfwGetMouseButton(window.Native(), GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    if (!m_terrainBrushApplying) return;

    const float delta  = m_terrainBrushStrength * m_dt;
    const int gi = static_cast<int>(std::round(lx / cell));
    const int gj = static_cast<int>(std::round(lz / cell));
    const int rad = static_cast<int>(std::ceil(radius / cell));
    // --- Paint mode: set the per-vertex paint layer within the brush. ---
    if (m_terrainSculptMode == 4) {
        std::vector<unsigned char> paint = it->second.terrain.Paint();
        if (static_cast<int>(paint.size()) != res * res) paint.assign(static_cast<std::size_t>(res) * res, 0);
        const unsigned char layer = static_cast<unsigned char>(std::clamp(m_terrainPaintLayer, 0, 5));

        // Painting the Grass layer (1) freezes the CURRENT grass settings into a style slot
        // and stamps it, so later setting changes don't touch grass already painted.
        const bool stampGrass = (layer == 1);
        unsigned char grassSlot = 0;
        std::vector<unsigned char> style;
        if (stampGrass) {
            grassSlot = static_cast<unsigned char>(std::clamp(m_scene.EnsureActiveGrassStyleSlot(), 0, 255));
            style = m_scene.SelectedTerrainGrassStyle();
            if (static_cast<int>(style.size()) != res * res) style.assign(static_cast<std::size_t>(res) * res, 0);
        }
        for (int j = gj - rad; j <= gj + rad; ++j) {
            for (int i = gi - rad; i <= gi + rad; ++i) {
                if (i < 0 || j < 0 || i >= res || j >= res) continue;
                const float vx = i * cell, vz = j * cell;
                const float d = std::sqrt((vx - lx) * (vx - lx) + (vz - lz) * (vz - lz));
                if (d > radius) continue;
                const std::size_t idx = static_cast<std::size_t>(j) * res + i;
                paint[idx] = layer;
                if (stampGrass) style[idx] = grassSlot;
            }
        }
        it->second.terrain.SetPaint(paint);       // rebuild albedo with painted layers
        m_scene.SetSelectedTerrainPaint(std::move(paint));
        if (stampGrass) m_scene.SetSelectedTerrainGrassStyle(std::move(style));
        return;
    }
    // --- Erase mode: clear paint (layer 0) within the brush, which also removes grass
    //     (grass grows only where the Grass layer is painted). ---
    if (m_terrainSculptMode == 5) {
        std::vector<unsigned char> paint = it->second.terrain.Paint();
        if (static_cast<int>(paint.size()) != res * res) return;   // nothing painted yet
        std::vector<unsigned char> style = m_scene.SelectedTerrainGrassStyle();
        const bool haveStyle = static_cast<int>(style.size()) == res * res;
        for (int j = gj - rad; j <= gj + rad; ++j) {
            for (int i = gi - rad; i <= gi + rad; ++i) {
                if (i < 0 || j < 0 || i >= res || j >= res) continue;
                const float vx = i * cell, vz = j * cell;
                const float d = std::sqrt((vx - lx) * (vx - lx) + (vz - lz) * (vz - lz));
                if (d > radius) continue;
                const std::size_t idx = static_cast<std::size_t>(j) * res + i;
                paint[idx] = 0;
                if (haveStyle) style[idx] = 0;
            }
        }
        it->second.terrain.SetPaint(paint);
        m_scene.SetSelectedTerrainPaint(std::move(paint));
        if (haveStyle) m_scene.SetSelectedTerrainGrassStyle(std::move(style));
        return;
    }

    // --- Sculpt modes: modify the heightmap. ---
    const int dirtyMinI = std::max(gi - rad, 0);
    const int dirtyMaxI = std::min(gi + rad, res - 1);
    const int dirtyMinJ = std::max(gj - rad, 0);
    const int dirtyMaxJ = std::min(gj + rad, res - 1);
    if (dirtyMinI > dirtyMaxI || dirtyMinJ > dirtyMaxJ) return;

    // Smoothing needs an immutable one-cell neighborhood. Copy only that local
    // patch instead of duplicating a million-float 1024 terrain every frame.
    const int srcMinI = std::max(dirtyMinI - 1, 0);
    const int srcMaxI = std::min(dirtyMaxI + 1, res - 1);
    const int srcMinJ = std::max(dirtyMinJ - 1, 0);
    const int srcMaxJ = std::min(dirtyMaxJ + 1, res - 1);
    const int srcWidth = srcMaxI - srcMinI + 1;
    std::vector<float> smoothSource;
    if (m_terrainSculptMode == 2) {
        smoothSource.reserve(static_cast<std::size_t>(srcWidth) * (srcMaxJ - srcMinJ + 1));
        for (int j = srcMinJ; j <= srcMaxJ; ++j) {
            const auto begin = hm.h.begin() + static_cast<std::ptrdiff_t>(j * res + srcMinI);
            smoothSource.insert(smoothSource.end(), begin, begin + srcWidth);
        }
    }
    const auto sourceHeight = [&](int i, int j) {
        if (smoothSource.empty()) return hm.h[static_cast<std::size_t>(j) * res + i];
        return smoothSource[static_cast<std::size_t>(j - srcMinJ) * srcWidth + (i - srcMinI)];
    };
    float centerH = 0.0f;
    if (gi >= 0 && gj >= 0 && gi < res && gj < res)
        centerH = hm.h[static_cast<std::size_t>(gj) * res + gi];

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
                                sum += sourceHeight(ni, nj); ++n;
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

    it->second.terrain.CommitHeightRegion(
        dirtyMinI, dirtyMinJ, dirtyMaxI, dirtyMaxJ);
    m_scene.UpdateSelectedTerrainHeightRegion(
        hm.h, res, dirtyMinI, dirtyMinJ, dirtyMaxI, dirtyMaxJ);
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
    float px = x, py = y;                 // px,py = scene render-pixel space (panel-aware)
    RemapViewportMouse(x, y, px, py);

    if (right.pressed
        && !m_mouse.GizmoActive()
        && !m_dragDrop.HasPayload()
        && m_scene.SelectedObject()
        && !m_scene.SelectedLocked()
        && IsViewportDropPosition(x, y)) {
        m_mouse.BeginGizmoDrag(GLFW_MOUSE_BUTTON_RIGHT, px, py);
        m_transformController.BeginGizmoDrag();
        m_scene.BeginTransformEdit();
        m_log.Info(std::string("Mouse gizmo: ") + m_gizmo.ModeName() + " " + m_gizmo.AxisName());
    }

    if (right.down && m_mouse.GizmoActiveFor(GLFW_MOUSE_BUTTON_RIGHT)) {
        const float dx = px - m_mouse.GizmoLastX();
        const float dy = py - m_mouse.GizmoLastY();
        const glm::mat4 viewProj = m_camera.ProjectionMatrix(window.AspectRatio()) * m_camera.ViewMatrix();
        const float pixels = ProjectGizmoDrag(dx, dy, viewProj, window.Width(), window.Height());

        if (pixels != 0.0f) {
            const EditorScene::Object* selected = m_scene.SelectedObject();
            if (selected && selected->isSpline
                && (m_gizmo.CurrentMode() == EditorGizmo::Mode::Translate
                    || m_gizmo.CurrentMode() == EditorGizmo::Mode::Rotate)
                && m_selectedSplinePoint >= 0
                && m_selectedSplinePoint < static_cast<int>(selected->splinePoints.size())) {
                m_transformController.ApplySplinePointGizmoDrag(
                    m_scene, static_cast<std::size_t>(m_selectedSplinePoint), m_gizmo, pixels);
            } else {
                m_transformController.ApplyGizmoDrag(m_scene, m_gizmo, pixels);
            }
        }

        m_mouse.UpdateGizmoLast(px, py);
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
    glm::vec3 pivot = transform->position;
    if (selected && selected->isSpline
        && (m_gizmo.CurrentMode() == EditorGizmo::Mode::Translate
            || m_gizmo.CurrentMode() == EditorGizmo::Mode::Rotate)
        && m_selectedSplinePoint >= 0
        && m_selectedSplinePoint < static_cast<int>(selected->splinePoints.size())) {
        pivot = selected->splinePoints[static_cast<std::size_t>(m_selectedSplinePoint)];
    }
    if (!m_viewport.ProjectWorldToScreen(pivot, viewProj,
            viewportWidth, viewportHeight, &centerScreen)
        || !m_viewport.ProjectWorldToScreen(pivot + axis, viewProj,
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
    } else if (payload.typeName == "Character") {
        CharacterAsset character;
        std::string error;
        if (!character.Load(payload.path, &error)) {
            m_log.Error("Character drop failed: " + error);
            m_dragDrop.Clear();
            return;
        }
        AddCharacterToScene(character, SceneDropPosition(), payload.path);
    } else if (payload.typeName == "Prefab") {
        PrefabAsset prefab;
        std::string error;
        if (!prefab.Load(payload.path, &error)) {
            m_log.Error("Prefab drop failed: " + error);
            m_dragDrop.Clear();
            return;
        }
        AddPrefabToScene(prefab, SceneDropPosition(), payload.path);
    } else {
        m_log.Warning("Asset type cannot be dropped on the scene yet");
    }
    m_dragDrop.Clear();
}

void EditorApp::AddCharacterToScene(const CharacterAsset& character, const glm::vec3& position,
                                   const std::string& assetPath)
{
    if (!m_cube) {
        m_log.Error("Character add failed: editor meshes are not ready");
        return;
    }

    // A character carries a capsule collider + player controller that must stay
    // world-upright, so we do NOT bake a model up-axis rotation into the object
    // transform (that would tilt the capsule). Instead we auto-size it, and — if the
    // rig is Z-up — set a render-only model orientation (applied further below), which
    // stands the mesh up while the collider stays vertical.
    Transform transform;
    transform.position = position;
    // If the character asset already carries a model offset (the user set one in the
    // Character Editor), respect it and skip the Z-up auto-detect below.
    const bool assetHasOffset =
        glm::dot(character.modelOffsetPosition, character.modelOffsetPosition) > 1e-8f
        || glm::dot(character.modelOrientationEuler, character.modelOrientationEuler) > 1e-4f
        || glm::dot(character.modelOffsetScale - glm::vec3(1.0f),
                    character.modelOffsetScale - glm::vec3(1.0f)) > 1e-8f;
    bool modelIsZUp = false;
    if (!character.modelAssetPath.empty()) {
        std::string modelError;
        if (const engine::SkinnedModel* skinned =
                m_editAssets.LoadSkinnedModel(character.modelAssetPath, &modelError)) {
            const glm::vec3 size = skinned->Max() - skinned->Min();
            const float height = std::max(std::max(size.y, size.z), 0.001f);   // Y or Z = up axis
            transform.scale = glm::vec3(1.8f / height);
            modelIsZUp = !assetHasOffset && size.z > size.y * 1.25f;   // taller along Z than deep along Y -> Z-up
            // A Z-up rig gets a render offset that re-centres the mesh on the object
            // origin, so its feet sit ~half its (scaled) height below the origin. Lift
            // the spawn by that half-height so it stands on the drop surface.
            if (modelIsZUp) {
                transform.position.y += 0.9f;   // half of the 1.8 auto-scaled height
            }
        }
    }

    // Create the object (from the model when present, else an empty) so it is the
    // current selection, then stamp the full character setup onto it.
    bool created = false;
    if (!character.modelAssetPath.empty()) {
        created = m_scene.AddModel(character.modelAssetPath, *m_cube, transform);
    }
    if (!created) {
        m_scene.AddEmpty(*m_cube);
        created = m_scene.SelectedObject() != nullptr;
    }
    if (!created) {
        m_log.Warning("Character add failed: could not create a scene object");
        return;
    }

    if (character.Apply(m_scene)) {
        m_editModelLoadErrors.erase(character.modelAssetPath);
        // Stand up a Z-up rig via the render-only orientation (collider stays vertical).
        // Skipped when the asset already carries its own offset (Apply set it above).
        if (modelIsZUp) {
            m_scene.SetSelectedModelOffset(glm::vec3(0.0f), glm::vec3(-90.0f, 0.0f, 0.0f), glm::vec3(1.0f));
        }
        // Link the object to its source asset so Character Editor edits can live-sync.
        if (!assetPath.empty()) {
            m_scene.SetSelectedCharacterAssetPath(
                assetPath, character.assetId);
        }
        m_log.Info("Added character to scene: "
            + (character.name.empty() ? std::string("Character") : character.name));
    } else {
        m_log.Warning("Character add failed: the new object is locked");
    }
}

glm::vec3 EditorApp::SceneDropPosition()
{
    const engine::Window& window = GetWindow();
    const glm::mat4 viewProj = m_camera.ProjectionMatrix(window.AspectRatio()) * m_camera.ViewMatrix();

    double cursorX = 0.0;
    double cursorY = 0.0;
    glfwGetCursorPos(window.Native(), &cursorX, &cursorY);

    float px = static_cast<float>(cursorX);
    float py = static_cast<float>(cursorY);
    RemapViewportMouse(px, py, px, py);   // panel-aware: map to scene render-pixel space

    return m_viewport.SceneDropPosition(px, py, viewProj,
        window.Width(),
        window.Height());
}

bool EditorApp::IsViewportDropPosition(float x, float y)
{
    const engine::Window& window = GetWindow();

    // New model: the Viewport panel owns the scene. Route interaction there when the
    // cursor is over its image (ImGui reports WantCaptureMouse for the panel, but that
    // is exactly where we DO want scene input). A mouse-driven asset drag drops by
    // geometry alone so a momentary hover loss doesn't cancel it.
    if (m_sceneViewValid && m_sceneViewW > 0.0f && m_sceneViewH > 0.0f) {
        const bool inImage = x >= m_sceneViewX && x < m_sceneViewX + m_sceneViewW
                          && y >= m_sceneViewY && y < m_sceneViewY + m_sceneViewH;
        if (m_dragDrop.IsMouseDriven()) return inImage;
        return m_sceneViewHovered && inImage;
    }

    // Legacy passthrough model: the scene is the window background behind the panels.
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse && !m_dragDrop.IsMouseDriven()) {
        return false;
    }
    return m_viewport.ContainsPoint(x, y, window.Width(), window.Height());
}

void EditorApp::AddEmpty()
{
    if (!m_cube) {
        m_log.Error("Add failed: placeholder mesh is not ready");
        return;
    }
    m_scene.AddEmpty(*m_cube);
    m_log.Info("Added empty object");
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
    if (!m_plane) {
        m_log.Error("Add failed: plane mesh is not ready");
        return;
    }
    m_scene.AddPlane(*m_plane);   // a plane object that becomes terrain (mesh is replaced)
    m_scene.SetSelectedTerrain(true, 128, 64.0f, 8.0f, 1337, 5, 2.0f);
    m_log.Info("Added terrain");
}

void EditorApp::AddWater(int preset, bool createRiverSpline) {
    if (!m_plane) {
        m_log.Error("Add failed: plane mesh is not ready");
        return;
    }
    // Per-type look + motion. size/res, colours, transparency/fresnel/spec/shininess,
    // then surface motion: seaHeight, seaChoppy, seaSpeed, seaFreq, foam.
    struct Preset {
        const char* name;
        float size; int res;
        glm::vec3 shallow, deep, reflection;
        float transparency, fresnel, specular, shininess;
        float seaHeight, seaChoppy, seaSpeed, seaFreq, foam;
    };
    Preset p;
    switch (preset) {
        case 1: // Lake -- small, calm, still, green-tinted
            p = {"Lake", 45.0f, 140,
                 {0.13f, 0.42f, 0.38f}, {0.02f, 0.12f, 0.14f}, {0.52f, 0.66f, 0.70f},
                 0.66f, 5.0f, 0.5f, 320.0f,
                 0.16f, 1.5f, 0.35f, 0.14f, 0.10f};
            break;
        case 2: // Ocean -- vast, choppy, deep blue, foamy whitecaps
            p = {"Ocean", 220.0f, 200,
                 {0.12f, 0.55f, 0.62f}, {0.01f, 0.09f, 0.19f}, {0.55f, 0.72f, 0.92f},
                 0.82f, 5.0f, 0.95f, 420.0f,
                 0.95f, 4.2f, 1.0f, 0.085f, 0.95f};
            break;
        case 3: // River -- medium, faster flow, moderate ripples
            p = {"River", 70.0f, 160,
                 {0.14f, 0.46f, 0.44f}, {0.03f, 0.17f, 0.21f}, {0.50f, 0.66f, 0.78f},
                 0.72f, 5.0f, 0.7f, 340.0f,
                 0.26f, 2.3f, 1.7f, 0.13f, 0.35f};
            break;
        default: // Generic water
            p = {"Water", 80.0f, 160,
                 {0.10f, 0.42f, 0.50f}, {0.02f, 0.10f, 0.18f}, {0.55f, 0.72f, 0.92f},
                 0.74f, 5.0f, 0.8f, 400.0f,
                 0.55f, 3.2f, 0.8f, 0.10f, 0.55f};
            break;
    }
    int createdSplineIndex = -1;
    std::string createdSplineName;
    if (preset == 3 && createRiverSpline) {
        AddSpline(1);
        createdSplineIndex = m_scene.SelectedIndex();
        if (const EditorScene::Object* spline = m_scene.SelectedObject()) {
            createdSplineName = spline->name;
        }
    }

    m_scene.AddPlane(*m_plane);   // a plane object that becomes a water body (rendered by the water pass)
    m_scene.SetSelectedWater(p.size, p.res, 0.0f, p.shallow, p.deep, p.reflection,
                             p.transparency, p.fresnel, p.specular, p.shininess);
    m_scene.SetSelectedWaterWaves(p.seaHeight, p.seaChoppy, p.seaSpeed, p.seaFreq, p.foam, preset);
    if (!createdSplineName.empty()) {
        m_scene.SetSelectedWaterFlowSpline(createdSplineName);
        m_scene.SetSelectedWaterRiverWidth(8.0f);
        // Leave the new spline ready for point editing; the linked water ribbon is
        // already visible and updates on every drag.
        m_scene.SelectIndex(createdSplineIndex);
        m_selectedSplinePoint = 1;
        m_gizmo.SetMode(EditorGizmo::Mode::Translate);
    }
    m_log.Info(std::string("Added ") + p.name);
}

void EditorApp::AddSpline(int type) {
    if (!m_plane) {
        m_log.Error("Add failed: plane mesh is not ready");
        return;
    }
    type = std::clamp(type, 0, 2);
    m_scene.AddPlane(*m_plane);          // gives the spline an entity + transform anchor
    Transform anchor;
    anchor.position = SceneDropPosition();
    m_scene.SetSelectedTransform(anchor);
    m_scene.SetSelectedSpline(true, false, type);

    glm::vec3 forward = m_camera.Front();
    forward.y = 0.0f;
    if (glm::dot(forward, forward) < 1.0e-5f) forward = glm::vec3(0.0f, 0.0f, -1.0f);
    else forward = glm::normalize(forward);
    const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
    const glm::vec3 c = anchor.position;
    std::vector<glm::vec3> points;
    if (type == 1) {
        points = {c - forward * 7.5f, c - forward * 2.5f + right * 1.5f,
                  c + forward * 2.5f - right * 1.2f, c + forward * 7.5f};
        m_scene.SetSelectedName("RiverSpline");
    } else if (type == 2) {
        points = {c - forward * 6.0f, c - forward * 2.0f,
                  c + forward * 2.0f, c + forward * 6.0f};
        m_scene.SetSelectedName("CameraRail");
    } else {
        points = {c - forward * 6.0f, c, c + forward * 6.0f};
        m_scene.SetSelectedName("PathSpline");
    }
    m_scene.SetSelectedSplinePoints(points);
    m_selectedSplinePoint = 1;
    m_gizmo.SetMode(EditorGizmo::Mode::Translate);
    m_log.Info(std::string("Added ") + (type == 1 ? "river spline" : type == 2 ? "camera rail" : "path spline"));
}

void EditorApp::DrawSplines(const glm::mat4& viewProj) {
    if (!m_shader || !m_cube) return;
    m_viewport.DrawSplineGuides(m_renderer, *m_shader, *m_cube, m_scene, viewProj,
                                m_selectedSplinePoint);
}

namespace {
// Defined further down; declared here so DrawWaterBodies (below) can place contact-foam
// rings at each object's waterline.
void ColliderFootprint(const engine::ecs::Collider& c, const glm::vec3& scale,
                       float& horizRadius, float& halfY);
}

void EditorApp::DrawGrass(const engine::Camera& camera, float aspect) {
    if (m_grass.empty()) return;
    const EditorScene::Environment& env = m_scene.GetEnvironment();
    const engine::DayNightCycle::Sample sky = engine::DayNightCycle::At(env.timeOfDay);
    const glm::vec3 sunDir   = sky.keyLightDirection;
    const glm::vec3 sunColor = sky.keyLightColor * env.sunIntensity;
    const glm::vec3 ambient  = sky.ambient * env.skyLightIntensity;

    // Interactors: objects that push/flatten the grass as they move through it. Each is
    // (worldX, worldY, worldZ, radius). Play mode uses physics bodies (incl. the player);
    // edit mode uses placed props so you can see grass part around them.
    std::vector<glm::vec4> interactors;
    interactors.reserve(engine::GrassField::kMaxInteractors);
    auto addInteractor = [&](const glm::vec3& pos, float radius) {
        if (static_cast<int>(interactors.size()) >= engine::GrassField::kMaxInteractors) return;
        interactors.emplace_back(pos.x, pos.y, pos.z, std::max(radius, 0.2f));
    };
    if (m_mode == EditorMode::Play && m_playRegistry) {
        // The player (character controller) is the main thing walking through grass, and
        // may not carry an ECS collider, so add it explicitly.
        if (m_playPlayerController) {
            const engine::CharacterController& b = m_playPlayerController->body;
            addInteractor(b.position, b.radius + 0.5f);
        }
        m_playRegistry->view<engine::ecs::Transform, engine::ecs::Collider>().each(
            [&](Entity /*e*/, engine::ecs::Transform& tr, engine::ecs::Collider& c) {
                if (c.isTrigger) return;
                float hr = 0.5f, hy = 0.5f;
                ColliderFootprint(c, tr.scale, hr, hy);
                addInteractor(tr.position, hr + 0.4f);   // margin so grass parts around it
            });
    } else {
        for (const EditorScene::Object& o : m_scene.Objects()) {
            if (o.isTerrain || o.isWater || o.isSpline || o.light ||
                o.navMeshBoundsVolume || !o.visible) {
                continue;
            }
            const engine::ecs::Transform* ot = m_scene.TryGetTransform(o.entity);
            if (!ot) continue;
            const glm::vec3 s = glm::abs(ot->scale);
            addInteractor(ot->position, 0.5f * std::max(s.x, s.z) + 0.4f);
        }
    }
    const glm::vec4* iptr = interactors.empty() ? nullptr : interactors.data();
    const int icount = static_cast<int>(interactors.size());

    for (auto& entry : m_grass) {
        if (!entry.second) continue;
        entry.second->Update(m_dt);
        entry.second->Draw(camera, aspect, sunDir, sunColor, ambient, iptr, icount);
    }
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
        cfg.depthFadeDistance = object.waterDepthFadeDistance;
        cfg.shorelineFoamWidth = object.waterShoreFoamWidth;
        cfg.shorelineFoamStrength = object.waterShoreFoamStrength;
        cfg.refractionStrength = object.waterRefractionStrength;
        cfg.reflectionRoughness = object.waterReflectionRoughness;
        cfg.environmentReflectionStrength = object.waterEnvironmentReflectionStrength;
        cfg.absorptionStrength = object.waterAbsorptionStrength;
        cfg.causticsStrength = object.waterCausticsStrength;
        cfg.causticsScale = object.waterCausticsScale;
        cfg.maxRenderDistance = object.waterMaxRenderDistance;
        cfg.seaHeight = object.waterSeaHeight;
        cfg.seaChoppy = object.waterSeaChoppy;
        cfg.seaSpeed  = object.waterSeaSpeed;
        cfg.seaFreq   = object.waterSeaFreq;
        cfg.foamAmount = object.waterFoam;

        // Directional flow: a river can follow a named spline. Sample the spline tangent
        // nearest the patch centre and scroll the waves along it.
        if (!object.waterFlowSpline.empty()) {
            for (const EditorScene::Object& s : m_scene.Objects()) {
                if (!s.isSpline || s.name != object.waterFlowSpline || s.splinePoints.size() < 2) continue;
                const engine::ecs::SplineComponent* scripted = nullptr;
                if (m_mode == EditorMode::Play && m_playRegistry) {
                    // Play runs against a cloned registry. Resolve the spline by its
                    // runtime name so script edits affect the rendered river immediately
                    // without modifying the saved editor scene.
                    for (const auto& entry : m_playEntityNames) {
                        if (entry.second == s.name) {
                            scripted = m_playRegistry->TryGet<engine::ecs::SplineComponent>(entry.first);
                            if (scripted) break;
                        }
                    }
                } else {
                    scripted = m_scene.Registry().TryGet<engine::ecs::SplineComponent>(s.entity);
                }
                const std::vector<glm::vec3>& points = scripted ? scripted->points : s.splinePoints;
                const std::vector<glm::vec3>& rotations = scripted ? scripted->rotations : s.splinePointRotations;
                if (points.size() < 2) continue;
                const bool closed = scripted ? scripted->closed : s.splineClosed;
                engine::Spline spline(points, closed);
                cfg.splinePoints = points;
                cfg.splinePointRotations = rotations;
                cfg.splineClosed = closed;
                cfg.riverWidth = object.waterRiverWidth;
                glm::vec3 boundsMin = points.front();
                glm::vec3 boundsMax = boundsMin;
                for (const glm::vec3& point : points) {
                    boundsMin = glm::min(boundsMin, point);
                    boundsMax = glm::max(boundsMax, point);
                }
                cfg.center = (boundsMin + boundsMax) * 0.5f;
                cfg.size = std::max(boundsMax.x - boundsMin.x,
                                    boundsMax.z - boundsMin.z) + cfg.riverWidth;
                glm::vec3 tangent(0.0f, 0.0f, 1.0f);
                spline.ClosestPoint(cfg.center, nullptr, &tangent);
                const glm::vec2 dir(tangent.x, tangent.z);
                const float len = glm::length(dir);
                if (len > 1.0e-4f) {
                    cfg.flowDir = dir / len;
                    cfg.flowStrength = std::max(object.waterSeaSpeed, 0.5f);
                }
                break;
            }
        }

        // Custom water shader: load (and hot-reload) the fragment GLSL from disk. The
        // engine prepends the water declaration prelude, so the file only holds helpers
        // + main(). On a compile error Water falls back to the built-in look.
        bool waterShaderReloaded = false;
        if (!object.waterShaderPath.empty()) {
            std::error_code sec;
            const std::filesystem::file_time_type mtime =
                std::filesystem::last_write_time(object.waterShaderPath, sec);
            auto cached = m_waterShaderCache.find(object.waterShaderPath);
            if (sec) {
                if (!m_editTextureLoadErrors[object.waterShaderPath]) {
                    m_editTextureLoadErrors[object.waterShaderPath] = true;
                    m_log.Warning("Water shader not found: " + object.waterShaderPath);
                }
            } else if (cached == m_waterShaderCache.end() || cached->second.first != mtime) {
                std::string source;
                if (std::filesystem::path(object.waterShaderPath).extension()
                        == ".3dgshader") {
                    // Node-graph shader authored in the Shader Editor: generate its GLSL
                    // and adapt it into a water fragment body.
                    engine::ShaderAsset asset;
                    std::string genError;
                    if (engine::LoadShaderAsset(object.waterShaderPath, &asset, &genError)) {
                        source = engine::GenerateWaterFragmentBody(asset, &genError);
                    }
                    if (source.empty() && !genError.empty()) {
                        m_log.Warning("Water graph shader '" + object.waterShaderPath
                                      + "': " + genError);
                    }
                } else {
                    std::ifstream file(object.waterShaderPath, std::ios::binary);
                    source.assign(std::istreambuf_iterator<char>(file),
                                  std::istreambuf_iterator<char>());
                }
                cached = m_waterShaderCache.insert_or_assign(
                    object.waterShaderPath, std::make_pair(mtime, std::move(source))).first;
                waterShaderReloaded = true;
            }
            if (cached != m_waterShaderCache.end()) cfg.customFragmentSource = cached->second.second;
        }

        auto res = m_waters.try_emplace(object.entity, cfg);
        if (!res.second) res.first->second.SetConfig(cfg);
        res.first->second.Update(m_dt);

        // Report a bad custom shader once per reload; the water keeps the built-in look.
        if (waterShaderReloaded) {
            const std::string& shaderErr = res.first->second.CustomShaderError();
            if (!shaderErr.empty())
                m_log.Warning("Water shader '" + object.waterShaderPath + "': " + shaderErr);
            else
                m_log.Info("Water shader loaded: " + object.waterShaderPath);
        }

        // Gather objects piercing this water's surface so the shader can draw a foam
        // ring where they meet the water. Each contact = (worldX, worldZ, radius, strength).
        std::vector<glm::vec4> contacts;
        contacts.reserve(engine::Water::kMaxContacts);
        engine::Water& liveWater = res.first->second;
        auto consider = [&](const glm::vec3& pos, float horizRadius, float halfY) {
            if (static_cast<int>(contacts.size()) >= engine::Water::kMaxContacts) return;
            if (!liveWater.ContainsXZ(pos.x, pos.z, horizRadius)) return;
            const float surfaceY = liveWater.HeightAt(pos.x, pos.z);
            const float band = halfY + 0.6f;
            const float dy = std::abs(pos.y - surfaceY);
            if (dy > band) return;                                                 // not touching the surface
            const float strength = glm::clamp(1.0f - dy / band, 0.0f, 1.0f);
            contacts.emplace_back(pos.x, pos.z, std::max(horizRadius, 0.15f), strength);
        };
        if (m_mode == EditorMode::Play && m_playRegistry) {
            // Physical bodies (have a collider) that pierce the surface.
            m_playRegistry->view<engine::ecs::Transform, engine::ecs::Collider>().each(
                [&](Entity /*e*/, engine::ecs::Transform& tr, engine::ecs::Collider& c) {
                    if (c.isTrigger) return;
                    float hr = 0.5f, hy = 0.5f;
                    ColliderFootprint(c, tr.scale, hr, hy);
                    consider(tr.position, hr, hy);
                });
        } else {
            for (const EditorScene::Object& other : m_scene.Objects()) {
                if (other.isWater || other.isTerrain || other.light ||
                    other.navMeshBoundsVolume || !other.visible) {
                    continue;   // only solid props get a waterline ring
                }
                const engine::ecs::Transform* ot = m_scene.TryGetTransform(other.entity);
                if (!ot) continue;
                const glm::vec3 s = glm::abs(ot->scale);
                consider(ot->position, 0.5f * std::max(s.x, s.z), 0.5f * s.y);
            }
        }

        res.first->second.Draw(camera, aspect, sunDir, sunColor, ambient,
                               contacts.empty() ? nullptr : contacts.data(),
                               static_cast<int>(contacts.size()),
                               m_waterSceneCopy ? m_waterSceneCopy->ColorTexture() : 0,
                               m_waterSceneCopy ? m_waterSceneCopy->DepthTexture() : 0,
                               m_renderW, m_renderH,
                               env.ibl && m_ibl ? &*m_ibl : nullptr);
    }
}

void EditorApp::DrawFoliage(const engine::Camera& camera, float aspect) {
    if (!m_foliageRenderer) return;
    const EditorScene::Environment& env = m_scene.GetEnvironment();
    const engine::DayNightCycle::Sample sky = engine::DayNightCycle::At(env.timeOfDay);
    engine::ecs::Registry& registry =
        (m_mode == EditorMode::Play && m_playRegistry) ? *m_playRegistry : m_scene.Registry();
    m_foliageRenderer->Draw(registry, camera, aspect,
        sky.keyLightDirection,
        sky.keyLightColor * env.sunIntensity,
        sky.ambient * env.skyLightIntensity,
        static_cast<float>(glfwGetTime()));   // drives wind sway
}

void EditorApp::CaptureWaterSceneBuffers() {
    const bool hasVisibleWater = std::any_of(
        m_scene.Objects().begin(), m_scene.Objects().end(),
        [](const EditorScene::Object& object) {
            return object.isWater && object.visible;
        });
    if (!hasVisibleWater || m_renderW <= 0 || m_renderH <= 0) return;

    if (!m_waterSceneCopy) {
        // Float colour preserves the HDR scene before the transparent water pass.
        m_waterSceneCopy.emplace(m_renderW, m_renderH, GL_RGBA16F, true);
    } else if (m_waterSceneCopy->Width() != m_renderW
               || m_waterSceneCopy->Height() != m_renderH) {
        m_waterSceneCopy->Resize(m_renderW, m_renderH);
    }

    GLint previousRead = 0, previousDraw = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousRead);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousDraw);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previousDraw));
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_waterSceneCopy->FboId());
    glBlitFramebuffer(0, 0, m_renderW, m_renderH,
                      0, 0, m_renderW, m_renderH,
                      GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previousRead));
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(previousDraw));
    glViewport(0, 0, m_renderW, m_renderH);
}

float EditorApp::WaterSurfaceY(float worldX, float worldZ, bool& over) {
    // Highest water surface at this XZ, if the point is over a water patch. Used to
    // float the player on the waves. Reads the cached engine::Water (built by
    // DrawWaterBodies) so the height matches what's rendered.
    over = false;
    float best = 0.0f;
    for (const EditorScene::Object& object : m_scene.Objects()) {
        if (!object.isWater || !object.visible) continue;
        const auto it = m_waters.find(object.entity);
        if (it != m_waters.end() && !it->second.ContainsXZ(worldX, worldZ)) continue;
        const engine::ecs::Transform* t = m_scene.TryGetTransform(object.entity);
        if (it == m_waters.end()) {
            const float cx = t ? t->position.x : 0.0f, cz = t ? t->position.z : 0.0f;
            const float half = object.waterSize * 0.5f;
            if (std::abs(worldX - cx) > half || std::abs(worldZ - cz) > half) continue;
        }
        const float y = (it != m_waters.end())
            ? it->second.HeightAt(worldX, worldZ)
            : (t ? t->position.y : object.waterLevel);
        if (!over || y > best) { best = y; over = true; }
    }
    return best;
}

bool EditorApp::UpdateUnderwaterState(const engine::Camera& camera, float dt) {
    const glm::vec3 position = camera.Position();
    const EditorScene::Object* containing = nullptr;
    float highestSurface = -std::numeric_limits<float>::max();
    for (const EditorScene::Object& object : m_scene.Objects()) {
        if (!object.isWater || !object.visible) continue;
        const engine::ecs::Transform* transform = m_scene.TryGetTransform(object.entity);
        const glm::vec3 center = transform
            ? transform->position : glm::vec3(0.0f, object.waterLevel, 0.0f);
        const auto water = m_waters.find(object.entity);
        if (water != m_waters.end() && !water->second.ContainsXZ(position.x, position.z)) continue;
        if (water == m_waters.end()) {
            const float half = object.waterSize * 0.5f;
            if (std::abs(position.x - center.x) > half
                || std::abs(position.z - center.z) > half) continue;
        }
        const float surface = water != m_waters.end()
            ? water->second.HeightAt(position.x, position.z) : center.y;
        if (position.y < surface && surface > highestSurface) {
            highestSurface = surface;
            containing = &object;
        }
    }

    const float target = containing ? 1.0f : 0.0f;
    const float speed = containing
        ? containing->waterUnderwaterTransitionSpeed : 3.5f;
    const float response = 1.0f - std::exp(-std::max(dt, 0.0f) * speed);
    m_underwaterBlend += (target - m_underwaterBlend) * response;
    if (target == 0.0f && m_underwaterBlend < 0.001f) m_underwaterBlend = 0.0f;

    if (containing) {
        m_underwaterVisuals.tint = containing->waterUnderwaterTint;
        m_underwaterVisuals.fogDensity = containing->waterUnderwaterFogDensity;
        m_underwaterVisuals.distortion = containing->waterUnderwaterDistortion;
        m_underwaterVisuals.causticsStrength = containing->waterCausticsStrength * 0.8f;
        m_underwaterVisuals.causticsScale = containing->waterCausticsScale * 4.5f;
    }
    return containing != nullptr || m_underwaterBlend > 0.001f;
}

namespace {
// Characteristic vertical half-extent + total volume of a collider (scaled by the
// object's transform), used to compute how submerged a floating body is and how much
// water it displaces. Both drive the buoyancy force in ApplyWaterBuoyancy.
float BuoyancyShapeMetrics(const engine::ecs::Collider& c, const glm::vec3& scale, float& outVolume) {
    const float sx = std::abs(scale.x), sy = std::abs(scale.y), sz = std::abs(scale.z);
    const float sVol = sx * sy * sz;
    constexpr float kPi = 3.14159265f;
    float halfY = 0.5f;
    float vol   = 1.0f;
    switch (c.shape) {
        case engine::ecs::ColliderShape::Sphere:
            halfY = c.radius * sy;
            vol   = (4.0f / 3.0f) * kPi * c.radius * c.radius * c.radius * sVol;
            break;
        case engine::ecs::ColliderShape::Box:
            halfY = c.halfExtents.y * sy;
            vol   = 8.0f * c.halfExtents.x * c.halfExtents.y * c.halfExtents.z * sVol;
            break;
        case engine::ecs::ColliderShape::Pyramid:
            halfY = c.halfExtents.y * sy;
            vol   = (8.0f * c.halfExtents.x * c.halfExtents.y * c.halfExtents.z) * (1.0f / 3.0f) * sVol;
            break;
        case engine::ecs::ColliderShape::Staircase:
            halfY = c.halfExtents.y * sy;
            vol   = 8.0f * c.halfExtents.x * c.halfExtents.y * c.halfExtents.z * 0.5f * sVol;
            break;
        case engine::ecs::ColliderShape::Capsule:
            halfY = (c.halfHeight + c.radius) * sy;
            vol   = (kPi * c.radius * c.radius * (2.0f * c.halfHeight)
                     + (4.0f / 3.0f) * kPi * c.radius * c.radius * c.radius) * sVol;
            break;
        case engine::ecs::ColliderShape::Cylinder:
            halfY = c.halfHeight * sy;
            vol   = kPi * c.radius * c.radius * (2.0f * c.halfHeight) * sVol;
            break;
        case engine::ecs::ColliderShape::Cone:
            halfY = c.halfHeight * sy;
            vol   = (1.0f / 3.0f) * kPi * c.radius * c.radius * (2.0f * c.halfHeight) * sVol;
            break;
        case engine::ecs::ColliderShape::Torus:
            halfY = c.minorRadius * sy;
            vol   = 2.0f * kPi * kPi * c.majorRadius * c.minorRadius * c.minorRadius * sVol;
            break;
        default:
            halfY = 0.5f * sy;
            vol   = sVol;
            break;
    }
    outVolume = std::max(vol, 1.0e-4f);
    return std::max(halfY, 0.05f);
}

// Horizontal radius + vertical half-extent of a collider (scaled), used to place the
// water contact-foam ring at an object's waterline.
void ColliderFootprint(const engine::ecs::Collider& c, const glm::vec3& scale,
                       float& horizRadius, float& halfY) {
    const float sx = std::abs(scale.x), sy = std::abs(scale.y), sz = std::abs(scale.z);
    const float sh = std::max(sx, sz);
    switch (c.shape) {
        case engine::ecs::ColliderShape::Sphere:
            horizRadius = c.radius * sh;             halfY = c.radius * sy; break;
        case engine::ecs::ColliderShape::Box:
        case engine::ecs::ColliderShape::Pyramid:
        case engine::ecs::ColliderShape::Staircase:
            horizRadius = std::max(c.halfExtents.x * sx, c.halfExtents.z * sz);
            halfY = c.halfExtents.y * sy; break;
        case engine::ecs::ColliderShape::Capsule:
            horizRadius = c.radius * sh;             halfY = (c.halfHeight + c.radius) * sy; break;
        case engine::ecs::ColliderShape::Cylinder:
        case engine::ecs::ColliderShape::Cone:
            horizRadius = c.radius * sh;             halfY = c.halfHeight * sy; break;
        case engine::ecs::ColliderShape::Torus:
            horizRadius = (c.majorRadius + c.minorRadius) * sh; halfY = c.minorRadius * sy; break;
        default:
            horizRadius = 0.5f * sh;                 halfY = 0.5f * sy; break;
    }
    horizRadius = std::max(horizRadius, 0.1f);
    halfY = std::max(halfY, 0.05f);
}
} // namespace

void EditorApp::ApplyWaterBuoyancy(float dt) {
    if (!m_playRegistry || m_waters.empty()) {
        return;
    }
    using engine::ecs::Transform;
    using engine::ecs::RigidBody;
    using engine::ecs::Collider;

    const float g = std::max(std::abs(m_playPhysics.gravity.y), 0.01f);
    // Water "density": buoyant force = kWaterDensity * g * displacedVolume * submersion.
    // A body floats when kWaterDensity * volume > mass and sinks otherwise, so raising an
    // object's Mass (inspector) makes it ride lower / sink; a big light object floats high.
    constexpr float kWaterDensity = 3.0f;
    constexpr float kLinearDrag   = 2.5f;   // water resistance: damps drift + bobbing
    constexpr float kAngularDrag  = 2.5f;   // damps spin so floating props settle

    m_playRegistry->view<Transform, RigidBody>().each(
        [&](Entity entity, Transform& transform, RigidBody& body) {
            if (body.invMass <= 0.0f || body.kinematic) {
                return;   // static / kinematic bodies are unaffected by buoyancy
            }
            bool over = false;
            const float waterY = WaterSurfaceY(transform.position.x, transform.position.z, over);
            if (!over) {
                return;
            }
            float volume = 1.0f;
            float halfHeight = 0.5f;
            if (const Collider* collider = m_playRegistry->TryGet<Collider>(entity)) {
                halfHeight = BuoyancyShapeMetrics(*collider, transform.scale, volume);
            } else {
                const glm::vec3 s = glm::abs(transform.scale);
                volume = std::max(s.x * s.y * s.z, 1.0e-4f);
                halfHeight = std::max(0.5f * s.y, 0.05f);
            }
            const float bottom = transform.position.y - halfHeight;
            if (bottom >= waterY) {
                return;   // wholly above the surface
            }
            const float submersion = glm::clamp((waterY - bottom) / (2.0f * halfHeight), 0.0f, 1.0f);
            // Archimedes lift (up); the solver applies gravity (down) separately.
            body.AddForce(glm::vec3(0.0f, kWaterDensity * g * volume * submersion, 0.0f));
            // Water resistance while submerged.
            const float lin = 1.0f / (1.0f + dt * kLinearDrag * submersion);
            body.velocity *= lin;
            body.angularVelocity *= 1.0f / (1.0f + dt * kAngularDrag * submersion);
            body.sleeping = false;   // keep bobbing on the waves
            body.sleepTimer = 0.0f;
        });
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
    if (!m_capsule) {
        m_log.Error("Add failed: capsule mesh is not ready");
        return;
    }

    m_scene.AddCapsule(*m_capsule);
    m_scene.SetSelectedName("PlayerStart");

    EditorScene::PlayerControllerSettings player;
    player.firstPerson = false;
    player.cameraMode = 0;
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
    // The shared capsule primitive is authored at radius 0.4 and height 1.8,
    // matching the controller defaults. Express later default changes as ratios
    // so the visible PlayerStart capsule remains aligned with its controller body.
    transform.scale = glm::vec3(
        player.capsuleRadius / 0.4f,
        player.capsuleHeight / 1.8f,
        player.capsuleRadius / 0.4f);
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
    std::vector<Entity> deletedTerrainEntities;
    for (int index : m_scene.SelectedIndices()) {
        if (index < 0 || index >= static_cast<int>(m_scene.Objects().size())) continue;
        const EditorScene::Object& object =
            m_scene.Objects()[static_cast<std::size_t>(index)];
        if (object.isTerrain && !object.locked)
            deletedTerrainEntities.push_back(object.entity);
    }
    if (m_scene.DeleteSelected()) {
        for (Entity entity : deletedTerrainEntities) {
            m_grass.erase(entity);
            m_terrains.erase(entity);
        }
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
        m_project.MarkCurrentSceneSaved();
        m_project.AddRecentScene(m_project.ScenePath());
        PersistProject();
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
        m_project.MarkCurrentSceneSaved();
        m_project.AddRecentScene(m_project.ScenePath());
        PersistProject();
        m_autosaveTimer = 0.0f;
        SetScenePathDraft(m_project.ScenePath());
        m_content.Refresh(m_assets, m_project, m_log);
    }
}

void EditorApp::PersistProject() {
    // Project settings go to the project file when one is active, otherwise to the
    // legacy editor.cfg. Either way editor.cfg is written so window + the current
    // project pointer persist.
    if (m_hasProjectFile) {
        m_project.Save(m_projectConfig);
        m_projectConfig.Save();
        m_config.Set("editor.current_project", m_project.ProjectFilePath());
    } else {
        m_project.Save(m_config);
    }
    m_config.Save();
}

void EditorApp::LoadProjectAssetRegistry() {
    m_assets.SetAssetRegistry(&m_assetRegistry);
    const std::string registryPath =
        engine::AssetRegistry::DefaultRegistryPath(m_project.AssetRoot());
    std::error_code ec;
    std::string error;

    if (std::filesystem::is_regular_file(registryPath, ec)
        && m_assetRegistry.Load(registryPath, &error)) {
        // Reconcile both native binaries and authored assets before validation.
        // This repairs older registries whose materials reference valid native
        // textures that were copied or moved inside Content.
        if (!m_assetRegistry.SynchronizeAuthoredAssets(
                m_project.AssetRoot(), &error)
            || !m_assetRegistry.Save(registryPath, &error)) {
            m_log.Warning(
                "Asset registry synchronization failed: " + error);
            error.clear();
        }
        m_log.Info("Loaded asset registry: "
                   + std::to_string(m_assetRegistry.Entries().size()) + " native asset(s)");
    } else {
        if (!error.empty()) {
            m_log.Warning("Asset registry could not be loaded; rebuilding it: " + error);
        }
        if (!m_assetRegistry.RebuildFromContent(m_project.AssetRoot(), &error)) {
            m_assetRegistry.Clear();
            m_log.Error("Asset registry rebuild failed: " + error);
            return;
        }
        if (!m_assetRegistry.Save(registryPath, &error)) {
            m_log.Error("Asset registry save failed: " + error);
            return;
        }
        m_log.Info("Created asset registry: "
                   + std::to_string(m_assetRegistry.Entries().size()) + " native asset(s)");
    }

    for (const engine::AssetRegistryIssue& issue : m_assetRegistry.Validate()) {
        // Name the offending asset by its content path (not just its GUID) so the
        // warning is actionable.
        std::string who = issue.asset.ToString();
        if (const engine::AssetRegistryEntry* entry = m_assetRegistry.Find(issue.asset);
            entry && !entry->virtualPath.empty()) {
            who = entry->virtualPath + " (" + issue.asset.ToString() + ")";
        }
        const std::string message = "Asset registry " + who + ": " + issue.message;
        if (issue.severity == engine::AssetRegistryIssue::Severity::Error) {
            m_log.Error(message);
        } else {
            m_log.Warning(message);
        }
    }
}

void EditorApp::BeginImportDialog(const std::vector<std::string>& paths) {
    m_pendingImports.clear();
    for (const std::string& path : paths) {
        std::error_code ec;
        if (std::filesystem::is_directory(path, ec)) {
            m_log.Warning("Drag-drop: skipped folder (drop the files inside): " + path);
            continue;
        }
        PendingImportFile file;
        file.path = path;
        const std::filesystem::path fs(path);
        file.name = fs.filename().string();
        file.extension = fs.extension().string();
        for (char& c : file.extension) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        file.isModel = file.extension == ".obj" || file.extension == ".fbx"
            || file.extension == ".gltf" || file.extension == ".glb" || file.extension == ".dae"
            || file.extension == ".ply" || file.extension == ".stl";
        file.isTexture = file.extension == ".png" || file.extension == ".jpg"
            || file.extension == ".jpeg" || file.extension == ".tga" || file.extension == ".bmp"
            || file.extension == ".hdr";
        file.modelMode = m_importGlobalModelMode;
        if (file.isModel) {
            // Peek the source so the default mode matches its content: a skeleton -> Skeletal
            // (or Animation-only when there is no mesh), otherwise Static.
            engine::ModelSourceInfo info;
            if (engine::InspectModelSource(path, &info)) {
                file.inspected = true;
                file.hasBones = info.hasBones;
                file.meshCount = static_cast<int>(info.meshCount);
                file.animationCount = static_cast<int>(info.animationCount);
                if (info.IsSkeletal())
                    file.modelMode = (info.meshCount == 0) ? 3 /*Animation*/ : 2 /*Skeletal*/;
                else
                    file.modelMode = 1 /*Static*/;
            }
        }
        if (file.isTexture) {
            // Normal / mask / roughness maps are linear data; default those off sRGB by name.
            const std::string lowerName = [&] {
                std::string n = file.name;
                for (char& c : n) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                return n;
            }();
            const bool linearMap =
                lowerName.find("normal") != std::string::npos
                || lowerName.find("_n.") != std::string::npos
                || lowerName.find("rough") != std::string::npos
                || lowerName.find("metal") != std::string::npos
                || lowerName.find("_orm") != std::string::npos
                || lowerName.find("mask") != std::string::npos
                || lowerName.find("height") != std::string::npos
                || lowerName.find("ao") != std::string::npos;
            file.srgb = !linearMap;
        }
        m_pendingImports.push_back(std::move(file));
    }
    if (m_pendingImports.empty()) return;
    // Default the target folder to the browser's current folder.
    std::snprintf(m_importFolderBuffer.data(), m_importFolderBuffer.size(), "%s",
                  m_assets.CurrentFolder().c_str());
    m_importDialogRequested = true;
}

void EditorApp::DrawImportDialog() {
    if (m_importDialogRequested) {
        ImGui::OpenPopup("Import Settings");
        m_importDialogRequested = false;
    }
    ImGui::SetNextWindowSize(ImVec2(560.0f, 0.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Import Settings", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::Text("Importing %zu file(s) into Content:", m_pendingImports.size());
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("Target Folder", m_importFolderBuffer.data(), m_importFolderBuffer.size());
    ImGui::TextDisabled("Relative to Content (blank = Content root).");

    const char* modes[] = { "Automatic", "Static Mesh", "Skeletal Mesh", "Animation Only" };
    int modelCount = 0;
    for (const PendingImportFile& f : m_pendingImports) if (f.isModel) ++modelCount;
    if (modelCount > 0) {
        ImGui::Separator();
        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::Combo("Model import (all)", &m_importGlobalModelMode, modes, 4)) {
            for (PendingImportFile& f : m_pendingImports)
                if (f.isModel) f.modelMode = m_importGlobalModelMode;
        }
    }

    bool hasAnimationImports = false;

    for (const PendingImportFile& f : m_pendingImports) {
        if (f.isModel && f.modelMode == 3 /*Animation Only*/) {
            hasAnimationImports = true;
            break;
        }
    }

    if (hasAnimationImports) {
        ImGui::Separator();
        ImGui::TextUnformatted("Animation Import Settings");

        auto& skeletalSettings = m_assets.SkeletalImportSettings();
        const std::vector<std::string> skeletons = m_assets.ContentAssetPaths(EditorAssets::Type::Skeleton);
        const std::string skeletonLabel = skeletalSettings.reuseSkeletonPath.empty()
            ? std::string("Select Skeleton")
            : skeletalSettings.reuseSkeletonPath;

        ImGui::SetNextItemWidth(420.0f);
        if (ImGui::BeginCombo("Skeleton", skeletonLabel.c_str())) {
            for (const std::string& path : skeletons) {
                const bool selected = skeletalSettings.reuseSkeletonPath == path;
                if (ImGui::Selectable(path.c_str(), selected)) {
                    skeletalSettings.reuseSkeletonPath = path;

                    // we are reusing an existing skeleton.
                    skeletalSettings.importSkeleton = false;
                }

                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }

            ImGui::EndCombo();
        }

        if (skeletalSettings.reuseSkeletonPath.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.25f, 1.0f), "Select a skeleton for the animation files.");
        }
        else {
            ImGui::TextDisabled("All animations Only files will use this skeleton.");
        }
    }

    ImGui::Separator();
    ImGui::BeginChild("ImportList", ImVec2(0.0f, 240.0f), true);
    for (std::size_t i = 0; i < m_pendingImports.size(); ++i) {
        PendingImportFile& f = m_pendingImports[i];
        ImGui::PushID(static_cast<int>(i));
        const char* type = f.isModel ? "Model"
            : f.isTexture ? "Texture"
            : (f.extension == ".wav" || f.extension == ".mp3" || f.extension == ".ogg"
               || f.extension == ".flac") ? "Audio"
            : "Other";
        ImGui::TextColored(ImVec4(0.65f, 0.8f, 1.0f, 1.0f), "[%s]", type);
        ImGui::SameLine();
        ImGui::TextUnformatted(f.name.c_str());
        if (f.isModel) {
            // Detected-content summary so the chosen mode is explainable.
            if (f.inspected) {
                char detail[96];
                std::snprintf(detail, sizeof(detail), "%s%d mesh, %d anim",
                              f.hasBones ? "skeleton, " : "", f.meshCount, f.animationCount);
                ImGui::SameLine();
                ImGui::TextDisabled("(%s)", detail);
            }
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 190.0f);
            ImGui::SetNextItemWidth(190.0f);
            ImGui::Combo("##mode", &f.modelMode, modes, 4);
        } else if (f.isTexture) {
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 190.0f);
            ImGui::Checkbox("sRGB", &f.srgb);
            ImGui::SameLine();
            ImGui::Checkbox("Smooth", &f.smooth);
        }
        ImGui::PopID();
    }
    ImGui::EndChild();

    ImGui::Separator();

    bool canImport = true;
    bool requiresSkeleton = false;

    for (const PendingImportFile& f : m_pendingImports) {
        if (f.isModel && f.modelMode == 3 /*Animation Only*/) {
            requiresSkeleton = true;
            break;
        }
    }

    if (requiresSkeleton && m_assets.SkeletalImportSettings().reuseSkeletonPath.empty()) {
        canImport = false;
    }

    if (!canImport) {
        ImGui::BeginDisabled();
    }

    if (ImGui::Button("Import", ImVec2(120.0f, 0.0f))) {
        const std::string folder = m_importFolderBuffer.data();
        int imported = 0;
        for (const PendingImportFile& f : m_pendingImports) {
            const auto mode = static_cast<EditorAssets::ModelImportMode>(
                f.isModel ? f.modelMode : 0);
            if (f.isModel && f.modelMode == 3) {
                auto& skeletalSettings = m_assets.SkeletalImportSettings();
                // Animation-only import.
                skeletalSettings.importSkeletalMesh = false;
                // We selected an existing engine skeleon.
                if (!skeletalSettings.reuseSkeletonPath.empty()) {
                    skeletalSettings.importSkeleton = false;
                }
            }
            if (f.isTexture) {   // per-file sRGB / filtering applied to this import
                m_assets.TextureImportSettings().srgb = f.srgb;
                m_assets.TextureImportSettings().smooth = f.smooth;
            }
            std::string error;
            if (m_assets.ImportAssetToFolder(f.path, folder, mode, &error)) {
                m_log.Info(m_assets.LastImportMessage());
                ++imported;
            } else {
                m_log.Error(error);
            }
        }
        if (imported > 0) {
            m_content.Refresh(m_assets, m_project, m_log);
            LoadProjectAssetRegistry();
            m_log.Info("Imported " + std::to_string(imported)
                       + (imported == 1 ? " file" : " files"));
        }
        m_pendingImports.clear();
        ImGui::CloseCurrentPopup();
    }

    if (!canImport) {
        ImGui::EndDisabled();
    }

    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
        m_pendingImports.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void EditorApp::NewProject(const std::string& location, const std::string& name) {
    if (!m_cube || !m_plane || !m_sphere || !m_capsule || !m_cylinder || !m_cone
        || !m_pyramid || !m_torus || !m_staircase) {
        m_log.Error("New project failed: editor meshes are not ready");
        return;
    }
    const std::string trimmedName = name.empty() ? std::string("NewProject") : name;
    const std::string loc = location.empty() ? std::string(".") : location;
    const std::filesystem::path projectDir = std::filesystem::path(loc) / trimmedName;

    std::string err;
    if (!m_project.CreateProject(projectDir.string(), trimmedName, &err)) {
        m_log.Error("New project failed: " + err);
        return;
    }

    // Switch the editor onto the new project's config + asset root.
    m_projectConfig = engine::Config(m_project.ProjectFilePath());
    m_hasProjectFile = true;
    m_config.Set("editor.current_project", m_project.ProjectFilePath());
    m_config.Save();
    LoadPackagingSettings();

    m_materialMaker.SetOutputDirectory(m_project.AssetRoot());
    m_behaviorGraph.SetOutputDirectory(m_project.AssetRoot());
    m_content.Refresh(m_assets, m_project, m_log);
    LoadProjectAssetRegistry();
    // Generate an empty project-owned module description and detach the previous
    // project's factories before continuing in the new project.
    {
        std::string regenError;
        if (!EditorGeneratedScriptTools::RegenerateGeneratedScripts(
                m_project.AssetRoot(), &regenError)) {
            m_log.Info("Project scripts: " + regenError);
        }
    }
    engine::ScriptRegistry::Instance().Clear();
    engine::ai::BtScriptRegistry::Instance().Clear();
    m_scriptModule.Unload();
    m_projectScriptClasses.clear();
    m_projectBtScriptClasses.clear();
    m_projectScriptStageSlot = -1;
    ResetScriptAutoReloadWatcher();
    engine::ai::RegisterExampleBtScripts();
    RegisterGameBtScripts();
    RegisterGameModule();
    LoadProjectScriptModule(false);

    // Start the project from a clean default scene and save it into the project.
    m_scene.BuildDefault(*m_cube, *m_plane, *m_sphere, *m_capsule, *m_cylinder,
        *m_cone, *m_pyramid, *m_torus, *m_staircase);
    SetScenePathDraft(m_project.ScenePath());
    SaveScene();          // writes Main.scene and persists the project
    SyncHudFromScene();
    m_log.Info("Created project '" + trimmedName + "' at " + projectDir.lexically_normal().string());
}

void EditorApp::OpenProjectFromPath(const std::string& projectFile) {
    if (!m_cube || !m_plane || !m_sphere || !m_capsule || !m_cylinder || !m_cone
        || !m_pyramid || !m_torus || !m_staircase) {
        m_log.Error("Open project failed: editor meshes are not ready");
        return;
    }
    if (projectFile.empty()) {
        m_log.Warning("Open project: path is empty");
        return;
    }

    std::string err;
    if (!m_project.OpenProjectFile(projectFile, m_projectConfig, &err)) {
        m_log.Error("Open project failed: " + err);
        return;
    }
    m_hasProjectFile = true;
    m_config.Set("editor.current_project", m_project.ProjectFilePath());
    m_config.Save();
    LoadPackagingSettings();

    m_materialMaker.SetOutputDirectory(m_project.AssetRoot());
    m_behaviorGraph.SetOutputDirectory(m_project.AssetRoot());
    m_content.Refresh(m_assets, m_project, m_log);
    LoadProjectAssetRegistry();
    SetScenePathDraft(m_project.ScenePath());
    // Regenerate and load this project's independent native-script module.
    {
        std::string regenError;
        if (!EditorGeneratedScriptTools::RegenerateGeneratedScripts(
                m_project.AssetRoot(), &regenError)) {
            m_log.Info("Project scripts: " + regenError);
        }
    }
    engine::ScriptRegistry::Instance().Clear();
    engine::ai::BtScriptRegistry::Instance().Clear();
    m_scriptModule.Unload();
    m_projectScriptClasses.clear();
    m_projectBtScriptClasses.clear();
    m_projectScriptStageSlot = -1;
    ResetScriptAutoReloadWatcher();
    engine::ai::RegisterExampleBtScripts();
    RegisterGameBtScripts();
    RegisterGameModule();
    LoadProjectScriptModule(false);

    std::error_code ec;
    if (m_project.HasLastSavedScene()
        && std::filesystem::is_regular_file(m_project.LastSavedScenePath(), ec)) {
        LoadScene();
    } else {
        m_scene.BuildDefault(*m_cube, *m_plane, *m_sphere, *m_capsule, *m_cylinder,
            *m_cone, *m_pyramid, *m_torus, *m_staircase);
        SetScenePathDraft(m_project.ScenePath());
        SaveScene();
    }
    SyncHudFromScene();
    m_log.Info("Opened project: " + m_project.ProjectName());
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
        m_project.MarkCurrentSceneSaved();
        m_project.AddRecentScene(m_project.ScenePath());
        PersistProject();
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

    ClearEditParticlePreviews();
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

    ClearEditParticlePreviews();
    const std::string previousPath = m_project.ScenePath();
    m_project.SetScenePath(m_project.ResolveScenePath(path));
    if (m_runtime.LoadScene(m_scene, m_project, *m_cube, *m_plane, *m_sphere, *m_capsule, *m_cylinder, *m_cone, *m_pyramid, *m_torus, *m_staircase, m_log)) {
        m_project.MarkCurrentSceneSaved();
        m_project.AddRecentScene(m_project.ScenePath());
        PersistProject();
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
        m_project.MarkCurrentSceneSaved();
        m_project.AddRecentScene(m_project.ScenePath());
        PersistProject();
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

void EditorApp::CookProject()
{
    // Capture any newly saved authored asset dependencies before walking the
    // graph. Refresh is non-destructive and leaves imported native entries intact.
    m_content.Refresh(m_assets, m_project, m_log);

    // The script iterate loop now rebuilds only the editor, so bring the standalone player
    // up to date here (synchronously) to guarantee the packaged build ships current
    // scripts. A build failure is non-fatal to cooking but is surfaced to the user.
    std::error_code ec;
    const std::filesystem::path projectRoot =
        std::filesystem::absolute(m_project.AssetRoot(), ec).parent_path();
    std::string buildError;
    m_log.Info("Building standalone player with current scripts...");
    if (EditorScriptTools::BuildTarget(projectRoot, "Debug", "player", &buildError)) {
        m_log.Info("Player build complete");
    } else {
        m_log.Warning("Player build failed; packaged scripts may be stale: " + buildError);
    }

    m_runtime.CookProject(m_scene, m_project, m_assetRegistry, m_log);
}

void EditorApp::LoadPackagingSettings()
{
    std::error_code ec;
    const std::filesystem::path projectRoot = m_project.HasProjectFile()
        ? std::filesystem::path(m_project.ProjectFilePath()).parent_path()
        : std::filesystem::absolute(m_project.AssetRoot(), ec).parent_path();
    engine::Config& config = m_hasProjectFile ? m_projectConfig : m_config;
    const std::string defaultOutput =
        (projectRoot / "Build" / "Packages").lexically_normal().string();
    const std::string output = config.GetString("package.output", defaultOutput);
    std::memset(m_packageOutputDraft.data(), 0, m_packageOutputDraft.size());
    std::snprintf(m_packageOutputDraft.data(), m_packageOutputDraft.size(),
        "%s", output.c_str());
    m_packageConfiguration = std::clamp(
        config.GetInt("package.configuration", 0), 0, 2);
    m_packageStaticRuntime = config.GetBool("package.static_runtime", true);
    m_packageCleanOutput = config.GetBool("package.clean_output", true);
    m_packageCreateZip = config.GetBool("package.create_zip", true);
    m_packageBuildStatus = "Ready to package";
}

void EditorApp::PersistPackagingSettings()
{
    engine::Config& config = m_hasProjectFile ? m_projectConfig : m_config;
    config.Set("package.output", std::string(m_packageOutputDraft.data()));
    config.Set("package.configuration", m_packageConfiguration);
    config.Set("package.static_runtime", m_packageStaticRuntime);
    config.Set("package.clean_output", m_packageCleanOutput);
    config.Set("package.create_zip", m_packageCreateZip);
    if (m_hasProjectFile) m_project.Save(config);
    config.Save();
}

void EditorApp::PackageProject()
{
    if (m_packageBuildRunning) {
        m_log.Warning("A package build is already running");
        return;
    }
    if (m_mode == EditorMode::Play) {
        m_log.Warning("Stop Play mode before packaging");
        return;
    }
    if (m_packageOutputDraft[0] == '\0') {
        m_log.Warning("Choose a package output folder first");
        return;
    }

    if (m_scene.IsDirty()) {
        SaveScene();
        if (m_scene.IsDirty()) {
            m_log.Error("Package cancelled because the current scene could not be saved");
            return;
        }
    }

    m_content.Refresh(m_assets, m_project, m_log);
    if (!m_runtime.CookProject(m_scene, m_project, m_assetRegistry, m_log)) {
        m_packageBuildStatus = "Packaging stopped: project cook failed";
        return;
    }

    PersistPackagingSettings();
    std::error_code ec;
    const std::filesystem::path projectRoot = m_project.HasProjectFile()
        ? std::filesystem::path(m_project.ProjectFilePath()).parent_path()
        : std::filesystem::absolute(m_project.AssetRoot(), ec).parent_path();
    std::string folderName = m_project.ProjectName();
    for (char& c : folderName) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') c = '_';
    }
    if (folderName.empty()) folderName = "Game";
    const std::filesystem::path cookedRoot =
        projectRoot / "Build" / "Cooked" / folderName;
    std::filesystem::path outputRoot(m_packageOutputDraft.data());
    if (outputRoot.is_relative()) outputRoot = projectRoot / outputRoot;

    static const char* configurations[] = {"Release", "RelWithDebInfo", "Debug"};
    const std::string configuration =
        configurations[std::clamp(m_packageConfiguration, 0, 2)];
    const bool staticRuntime = m_packageStaticRuntime;
    const bool cleanOutput = m_packageCleanOutput;
    const bool createZip = m_packageCreateZip;
    const std::string projectName = m_project.ProjectName();

    m_packageBuildRunning = true;
    m_packageBuildStatus = "Cook complete. Building " + configuration
        + " player in the background...";
    m_log.Info(m_packageBuildStatus);
    m_packageBuildFuture = std::async(std::launch::async,
        [projectRoot, cookedRoot, outputRoot, projectName, configuration,
         staticRuntime, cleanOutput, createZip]() {
            PackageBuildResult result;
            result.success = EditorScriptTools::PackageProject(
                projectRoot, cookedRoot, outputRoot, projectName, configuration,
                staticRuntime, cleanOutput, createZip, &result.artifact,
                &result.error);
            return result;
        });
}

void EditorApp::UpdatePackageBuild()
{
    if (!m_packageBuildRunning || !m_packageBuildFuture.valid()) return;
    if (m_packageBuildFuture.wait_for(std::chrono::seconds(0))
        != std::future_status::ready) return;

    try {
        PackageBuildResult result = m_packageBuildFuture.get();
        m_packageBuildRunning = false;
        if (result.success) {
            m_packageBuildStatus = "Package complete: " + result.artifact.string();
            m_log.Info(m_packageBuildStatus);
        } else {
            m_packageBuildStatus = "Package failed: " + result.error;
            m_log.Error(m_packageBuildStatus);
        }
    } catch (const std::exception& exception) {
        m_packageBuildRunning = false;
        m_packageBuildStatus = std::string("Package failed: ") + exception.what();
        m_log.Error(m_packageBuildStatus);
    }
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
    ClearEditParticlePreviews();
    RestoreCameraBeforeShake();
    m_cameraShake.Clear();
    m_cameraSequence.Stop();
    m_cameraDirector.SetStopped();
    m_cameraDirector.ClearEvents();
    m_cameraDirector.TakeCommands();
    engine::GameMode::Instance().Reset();
    const EditorScene::GameModeSettings& gameModeSettings =
        m_scene.GetGameModeSettings();
    engine::GameMode::Instance().loseOnPlayerDeath =
        gameModeSettings.loseOnPlayerDeath;
    engine::GameMode::Instance().SetScore(gameModeSettings.initialScore);
    m_hudFloats.clear();
    m_hudStrings.clear();
    m_cinematicSkipPrev = false;
    m_activeCinematicCues.clear();
    m_cameraSequencePaused = false;
    m_editSnapshot = m_scene.CreateSnapshot();
    m_editCameraBeforePlay = m_camera;
    m_physicsPaused = gameModeSettings.startPaused;
    if (gameModeSettings.startPaused) {
        engine::GameMode::Instance().Pause();
    }
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
    if (m_playRegistry) {
        m_runtimePropertyInspector.BeginPlay(*m_playRegistry, m_playEntityNames);
    }
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
    engine::SetScriptExecutionPaused(false);
    m_runtimePropertyInspector.EndPlay();
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
                        engine::QueueScriptAnimationEvent(*m_playRegistry, entity, name);
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
    m_playSoundField.Clear();
    m_prevPlayerPosValid = false;
    m_prevHp.clear();
    m_squadAlerts.clear();
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
        playAgent.movement.mode = object.navMovementMode;
        playAgent.movement.gravity = object.navMovementGravity;
        playAgent.movement.maxFallSpeed = object.navMovementMaxFallSpeed;
        playAgent.movement.groundProbeDistance = object.navMovementGroundProbe;
        playAgent.movement.stepHeight = object.navMovementStepHeight;
        playAgent.movement.maxSlopeDegrees = object.navMovementMaxSlope;
        playAgent.brain.agent.maxSpeed = std::max(object.navAgentSpeed, 0.0f);
        playAgent.brain.agent.maxForce = std::max(object.navAgentMaxForce, 0.0f);
        playAgent.brain.reachRadius = std::max(object.navAgentReachRadius, 0.05f);
        playAgent.brain.repathInterval = std::max(object.navAgentRepathInterval, 0.05f);
        playAgent.brain.patrol = object.patrolPoints;
        playAgent.brain.vision.range = std::max(object.navAgentVisionRange, 0.0f);
        playAgent.brain.vision.halfAngleDegrees = object.navAgentVisionHalfAngle;
        // Hearing is omnidirectional; authored per agent in the inspector.
        playAgent.hearingRange = std::max(object.navAgentHearingRange, 0.0f);
        playAgent.brain.hearingRange = playAgent.hearingRange;
        playAgent.squadAlertRadius = std::max(object.navAgentSquadAlertRadius, 0.0f);
        playAgent.squadForgetTime = std::max(object.navAgentSquadForgetTime, 0.1f);
        if (!object.navAgentTargetName.empty()) {
            const auto target = playEntitiesByName.find(object.navAgentTargetName);
            if (target != playEntitiesByName.end()) {
                playAgent.configuredTargetEntity = target->second;
                playAgent.targetEntity = target->second;
            } else {
                m_log.Warning("AI: '" + object.name + "' chase target '"
                    + object.navAgentTargetName + "' was not found in the Play scene");
            }
        }
        glm::vec3 startPos(0.0f);
        glm::vec3 startFacing(0.0f, 0.0f, -1.0f);
        if (const engine::ecs::Transform* t = m_playRegistry->TryGet<engine::ecs::Transform>(entity)) {
            startPos = t->position;
            startFacing = t->rotation * glm::vec3(0.0f, 0.0f, -1.0f);
            startFacing.y = 0.0f;
            if (glm::dot(startFacing, startFacing) <= 1.0e-6f) {
                startFacing = glm::vec3(0.0f, 0.0f, -1.0f);
            } else {
                startFacing = glm::normalize(startFacing);
            }
            playAgent.brain.SetPosition(startPos);
            playAgent.brain.SetFacing(startFacing);
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
                playAgent.ctx.facing = startFacing;
                playAgent.ctx.reachRadius = std::max(object.navAgentReachRadius, 0.05f);
                playAgent.ctx.repathInterval = std::max(object.navAgentRepathInterval, 0.05f);
                playAgent.ctx.patrol = object.patrolPoints;
                engine::ai::SeedBlackboard(graph.blackboard, playAgent.ctx.blackboard);
                playAgent.ctx.nodeStatus.assign(graph.nodes.size(), 0);   // debugger buffer
                playAgent.tree = engine::ai::BuildBehaviorTree(graph, resolveSubtree);
                m_log.Info("AI: '" + object.name + "' running behaviour tree " + object.navAgentBrainAsset);
                for (const engine::ai::BtGraphNode& node : graph.nodes) {
                    if (node.type == engine::ai::BtNodeType::ScriptTask
                        && !node.script.empty()
                        && !engine::ai::BtScriptRegistry::Instance().Has(node.script)) {
                        m_log.Warning("AI: behavior task script '" + node.script
                            + "' is not registered; rebuild and restart the editor");
                    }
                    for (const engine::ai::BtAttachment& attachment : node.decorators) {
                        if (attachment.type == engine::ai::BtNodeType::ScriptDecorator
                            && !attachment.script.empty()
                            && !engine::ai::BtScriptRegistry::Instance().Has(attachment.script)) {
                            m_log.Warning("AI: behavior decorator script '" + attachment.script
                                + "' is not registered; rebuild and restart the editor");
                        }
                    }
                    for (const engine::ai::BtAttachment& attachment : node.services) {
                        if (attachment.type == engine::ai::BtNodeType::ScriptService
                            && !attachment.script.empty()
                            && !engine::ai::BtScriptRegistry::Instance().Has(attachment.script)) {
                            m_log.Warning("AI: behavior service script '" + attachment.script
                                + "' is not registered; rebuild and restart the editor");
                        }
                    }
                }
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
    auto isGameplayActor = [&](engine::ecs::Entity entity,
                               const engine::ecs::Collider& collider) {
        if (entity == m_playPlayerEntity) return true;
        for (const PlayAgent& agent : m_playAgents) {
            if (agent.entity == entity) return true;
        }
        constexpr std::uint32_t actorLayers =
            engine::ecs::CollisionLayer::Player
            | engine::ecs::CollisionLayer::Enemy
            | engine::ecs::CollisionLayer::Collectible
            | engine::ecs::CollisionLayer::Projectile
            | engine::ecs::CollisionLayer::Trigger;
        return (collider.layer & actorLayers) != 0;
    };

    m_playRegistry->view<engine::ecs::Transform, engine::ecs::Collider>().each(
        [&](engine::ecs::Entity e, engine::ecs::Transform& t, engine::ecs::Collider& c) {
            // Navigation is baked from permanent world geometry. Characters and
            // other gameplay actors move at runtime and must not carve blocked
            // cells underneath themselves or at the current chase destination.
            if (isGameplayActor(e, c)) return;
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
    auto isGameplayActor = [&](engine::ecs::Entity entity,
                               const engine::ecs::Collider& collider) {
        if (entity == m_playPlayerEntity) return true;
        for (const PlayAgent& agent : m_playAgents) {
            if (agent.entity == entity) return true;
        }
        constexpr std::uint32_t actorLayers =
            engine::ecs::CollisionLayer::Player
            | engine::ecs::CollisionLayer::Enemy
            | engine::ecs::CollisionLayer::Collectible
            | engine::ecs::CollisionLayer::Projectile
            | engine::ecs::CollisionLayer::Trigger;
        return (collider.layer & actorLayers) != 0;
    };

    m_playRegistry->view<engine::ecs::Transform, engine::ecs::Collider>().each(
        [&](engine::ecs::Entity e, engine::ecs::Transform& t, engine::ecs::Collider& c) {
            if (isGameplayActor(e, c)) return;
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
        if (object.navAgentEnabled || object.playerControllerEnabled) continue;
        constexpr std::uint32_t actorLayers =
            engine::ecs::CollisionLayer::Player
            | engine::ecs::CollisionLayer::Enemy
            | engine::ecs::CollisionLayer::Collectible
            | engine::ecs::CollisionLayer::Projectile
            | engine::ecs::CollisionLayer::Trigger;
        if ((object.collider.layer & actorLayers) != 0) continue;
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

    // Hearing: age transient noises, then emit a "footstep" noise when the player
    // moves fast enough. Sneaking slowly stays quiet; running carries farther.
    m_playSoundField.Update(dt);
    if (m_playPlayerEntity != engine::ecs::kNull && m_playRegistry->Valid(m_playPlayerEntity)) {
        if (const engine::ecs::Transform* pt =
                m_playRegistry->TryGet<engine::ecs::Transform>(m_playPlayerEntity)) {
            if (m_prevPlayerPosValid && dt > 1.0e-4f) {
                const float speed = glm::length(pt->position - m_prevPlayerPos) / dt;
                if (speed > 2.0f) {
                    const float radius   = glm::clamp(speed * 2.0f, 6.0f, 18.0f);
                    const float loudness = glm::clamp(speed / 6.0f, 0.2f, 1.0f);
                    m_playSoundField.Emit(pt->position, radius, loudness, 0.4f);
                }
            }
            m_prevPlayerPos = pt->position;
            m_prevPlayerPosValid = true;
        }
    }

    // Combat noise: any entity whose HP dropped this frame emits a loud noise at its
    // position, so gunfire/melee draws nearby guards (covers player and agent attacks).
    {
        auto emitOnDamage = [&](engine::ecs::Entity entity) {
            if (entity == engine::ecs::kNull || !m_playRegistry->Valid(entity)) return;
            const engine::Health* h = m_playRegistry->TryGet<engine::Health>(entity);
            const engine::ecs::Transform* tr =
                m_playRegistry->TryGet<engine::ecs::Transform>(entity);
            if (!h || !tr) return;
            const auto prev = m_prevHp.find(entity);
            if (prev != m_prevHp.end() && h->hp < prev->second - 0.01f) {
                m_playSoundField.Emit(tr->position, 22.0f, 1.0f, 0.5f);   // combat is loud
            }
            m_prevHp[entity] = h->hp;
        };
        emitOnDamage(m_playPlayerEntity);
        for (const PlayAgent& a : m_playAgents) emitOnDamage(a.entity);
    }

    // Squad coordination + de-escalation: an agent that saw its target raises its
    // team's alert to full and stores the target's last-known position. The alert
    // decays over the sighter's authored forget time, so teammates keep converging on
    // that spot for a while after everyone loses sight, then calm back to patrol.
    for (auto& kv : m_squadAlerts) {
        SquadAlert& alert = kv.second;
        alert.level -= dt / std::max(alert.forget, 0.1f);
        if (alert.level <= 0.0f) { alert.level = 0.0f; alert.valid = false; }
    }
    for (const PlayAgent& a : m_playAgents) {
        if (!a.perceivesTarget || a.team == 0) continue;
        SquadAlert& alert = m_squadAlerts[a.team];
        alert.level = 1.0f;                      // fresh sighting -> full alert
        alert.poi = a.perceivedTargetPos;
        alert.valid = true;
        alert.forget = a.squadForgetTime;        // this sighter sets how long it lingers
    }

    // Flanking: group engaged agents by the target they share and assign each a slot,
    // so multiple pursuers surround the target from spread angles instead of stacking
    // on the same point. "Engaged" = saw the target, or its squad alert is still live.
    std::unordered_map<engine::ecs::Entity, std::vector<int>> pursuers;
    for (int i = 0; i < static_cast<int>(m_playAgents.size()); ++i) {
        PlayAgent& a = m_playAgents[static_cast<std::size_t>(i)];
        a.flankSlot = 0; a.flankCount = 1;   // reset; default solo (no offset)
        if (a.targetEntity == engine::ecs::kNull) continue;
        const auto alertIt = m_squadAlerts.find(a.team);
        const bool alerted = a.team != 0 && alertIt != m_squadAlerts.end() && alertIt->second.valid;
        if (a.perceivesTarget || alerted) pursuers[a.targetEntity].push_back(i);
    }
    for (const auto& kv : pursuers) {
        const std::vector<int>& group = kv.second;
        for (std::size_t s = 0; s < group.size(); ++s) {
            PlayAgent& a = m_playAgents[static_cast<std::size_t>(group[s])];
            a.flankSlot = static_cast<int>(s);
            a.flankCount = static_cast<int>(group.size());
        }
    }

    // Snapshot potential targets once per frame so faction auto-targeting is a cheap
    // arithmetic scan instead of N^2 registry lookups (a TryGet per agent pair).
    struct TargetCandidate {
        engine::ecs::Entity entity;
        int team;
        glm::vec3 position;
        bool alive;
    };
    std::vector<TargetCandidate> targetCandidates;
    targetCandidates.reserve(m_playAgents.size());
    for (const PlayAgent& a : m_playAgents) {
        if (a.team == 0 || !m_playRegistry->Valid(a.entity)) continue;
        const engine::ecs::Transform* at =
            m_playRegistry->TryGet<engine::ecs::Transform>(a.entity);
        if (!at) continue;
        const engine::Health* h = m_playRegistry->TryGet<engine::Health>(a.entity);
        targetCandidates.push_back({a.entity, a.team, at->position, !h || h->alive});
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

        // An explicitly assigned scene target always wins. Faction auto-targeting
        // is a fallback for agents that do not have a configured Chase Target.
        // This also permits the player to be targeted without requiring the player
        // character to carry a Nav Agent component.
        if (playAgent.configuredTargetEntity != engine::ecs::kNull
            && m_playRegistry->Valid(playAgent.configuredTargetEntity)) {
            playAgent.targetEntity = playAgent.configuredTargetEntity;
        } else if (playAgent.autoTarget && playAgent.team != 0) {
            engine::ecs::Entity best = engine::ecs::kNull;
            float bestDistSq = 1.0e36f;
            for (const TargetCandidate& candidate : targetCandidates) {
                if (candidate.entity == playAgent.entity
                    || candidate.team == playAgent.team || !candidate.alive) continue;
                const glm::vec3 delta = candidate.position - t->position;
                const float distSq = glm::dot(delta, delta);
                if (distSq < bestDistSq) { bestDistSq = distSq; best = candidate.entity; }
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
                                                                 : glm::vec3(0.0f, 0.0f, -1.0f);
                const glm::vec3 eye = agentPos + glm::vec3(0.0f, 0.6f, 0.0f) + forward * 0.6f;
                seesTarget = engine::ai::CanSee(eye, forward, playAgent.brain.vision,
                                                targetPos, playAgent.targetEntity,
                                                m_playPhysics, *m_playRegistry,
                                                playAgent.entity);
            }
        }

        // Remember this frame's sighting so teammates can be alerted next frame.
        playAgent.perceivesTarget = seesTarget;
        if (seesTarget) playAgent.perceivedTargetPos = targetPos;

        // Hearing + squad alerts: if the agent can't see its target, the loudest noise
        // it can hear -- or a teammate's callout about a spotted target -- becomes a
        // point of interest. Graph agents read it via the HeardNoise?/Investigate
        // nodes; the built-in brain routes to it through its search state.
        if (!seesTarget) {
            // LoudestAudible writes this when it returns true, but initialize it
            // defensively so the contract is explicit to both the compiler and any
            // future sound-field implementation.
            glm::vec3 pointOfInterest = agentPos;
            bool alerted = playAgent.hearingRange > 0.0f
                && m_playSoundField.LoudestAudible(agentPos, playAgent.hearingRange, &pointOfInterest);
            if (!alerted && playAgent.team != 0) {
                const auto it = m_squadAlerts.find(playAgent.team);
                if (it != m_squadAlerts.end() && it->second.valid
                    && glm::length(it->second.poi - agentPos) <= playAgent.squadAlertRadius) {
                    pointOfInterest = it->second.poi;   // converge on the squad's last-known target
                    alerted = true;
                }
            }
            playAgent.ctx.heardNoise = alerted;
            if (alerted) {
                playAgent.ctx.heardPosition = pointOfInterest;
                if (!playAgent.useGraph) playAgent.brain.Hear(pointOfInterest);
            }
        } else {
            playAgent.ctx.heardNoise = false;
        }

        glm::vec3 movementTarget = targetPos;
        if (!playAgent.movement.IsFlying()) movementTarget.y = agentPos.y;

        // Flanking: aim at a slot on a ring around the target (at attack distance) so
        // squadmates encircle it instead of piling onto the same spot. Solo agents
        // (flankCount == 1) keep aiming straight at the target.
        if (playAgent.flankCount > 1) {
            const float ang = (static_cast<float>(playAgent.flankSlot)
                               / static_cast<float>(playAgent.flankCount)) * 6.2831853f;
            const float ring = std::max(playAgent.brain.reachRadius, 1.0f);
            movementTarget.x = targetPos.x + ring * std::cos(ang);
            movementTarget.z = targetPos.z + ring * std::sin(ang);
        }
        const bool hasValidTarget = playAgent.targetEntity != engine::ecs::kNull
            && m_playRegistry->Valid(playAgent.targetEntity);
        const bool targetWithinReach = !playAgent.useGraph && hasValidTarget
            && glm::length(glm::vec2(targetPos.x - agentPos.x,
                                     targetPos.z - agentPos.z))
                <= playAgent.brain.reachRadius;
        glm::vec3 facing;
        if (playAgent.useGraph) {
            // Data-driven behaviour tree drives the steering body.
            engine::ai::AgentContext& c = playAgent.ctx;
            c.dt = dt;
            c.targetPos = movementTarget;
            c.seesTarget = seesTarget;
            c.grid = m_useNavMesh ? nullptr : &m_playNavGrid;
            c.mesh = m_useNavMesh ? &m_playNavMesh : nullptr;
            c.registry = &*m_playRegistry;   // let script nodes reach the ECS
            c.self = playAgent.entity;
            c.targetEntity = playAgent.targetEntity;
            c.steer = glm::vec3(0.0f);
            std::fill(c.nodeStatus.begin(), c.nodeStatus.end(), 0);   // reset per-frame debug status
            playAgent.tree.Tick(c, dt);
            if (movementLocked || targetWithinReach) {
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
            if (movementLocked || targetWithinReach) {
                playAgent.brain.SetPosition(lockedPosition);
                playAgent.brain.agent.velocity = glm::vec3(0.0f);
            } else {
                if (m_useNavMesh) {
                    playAgent.brain.Update(dt, movementTarget, seesTarget, m_playNavMesh);
                } else {
                    playAgent.brain.Update(dt, movementTarget, seesTarget, m_playNavGrid);
                }
            }
            t->position = playAgent.brain.Position();
            facing = playAgent.brain.Facing();
        }
        const glm::vec3 requestedPosition = t->position;
        bool overTerrain = false;
        const float surfaceY = TerrainSurfaceY(
            requestedPosition.x, requestedPosition.z, overTerrain);
        t->position = engine::ai::MoveAiAgent(
            m_playPhysics, *m_playRegistry, playAgent.entity,
            lockedPosition, requestedPosition, dt, playAgent.movement,
            overTerrain, surfaceY);
        glm::vec3 resolvedVelocity = dt > 1.0e-6f
            ? (t->position - lockedPosition) / dt : glm::vec3(0.0f);
        resolvedVelocity.y = 0.0f;
        if (playAgent.useGraph) {
            playAgent.ctx.agent.position = t->position;
            playAgent.ctx.agent.velocity = resolvedVelocity;
        } else {
            playAgent.brain.SetPosition(t->position);
            playAgent.brain.agent.velocity = resolvedVelocity;
        }
        if (engine::AnimatedModel* animatedModel =
                m_playRegistry->TryGet<engine::AnimatedModel>(playAgent.entity)) {
            engine::ai::UpdateAiAnimationParameters(
                *animatedModel, playAgent.movement, resolvedVelocity);
        }
        // Visual Behavior Trees control target focus explicitly through their
        // Focus Target / Clear Focus tasks. The built-in brain retains automatic
        // close-range focus so its behaviour remains useful without a graph.
        const float horizontalSpeed = glm::length(glm::vec2(
            resolvedVelocity.x, resolvedVelocity.z));
        const bool graphFocus = playAgent.useGraph
            && playAgent.ctx.focusTarget && hasValidTarget;
        const bool builtInFocus = !playAgent.useGraph
            && (seesTarget || targetWithinReach)
            && (movementLocked || targetWithinReach || horizontalSpeed <= 0.15f);
        if (graphFocus || builtInFocus) {
            glm::vec3 toTarget = targetPos - t->position;
            toTarget.y = 0.0f;
            if (glm::dot(toTarget, toTarget) > 1.0e-6f) {
                facing = glm::normalize(toTarget);
                if (playAgent.useGraph) {
                    playAgent.ctx.facing = facing;
                }
            }
        }
        if (glm::dot(facing, facing) > 1e-6f) {
            // Characters face object-local -Z. Add 180 degrees so that axis, rather
            // than local +Z, is rotated onto the world-space AI facing direction.
            const float yaw = std::atan2(facing.x, facing.z) + glm::pi<float>();
            glm::quat targetRotation =
                glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f));
            // Keep quaternion interpolation on the shortest hemisphere and use
            // exponential damping so AI turns consistently at any frame rate.
            if (glm::dot(t->rotation, targetRotation) < 0.0f) {
                targetRotation = -targetRotation;
            }
            constexpr float kAiTurnResponse = 9.0f;
            const float turnAlpha =
                1.0f - std::exp(-kAiTurnResponse * std::max(dt, 0.0f));
            t->rotation = glm::normalize(
                glm::slerp(t->rotation, targetRotation, turnAlpha));
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
                ta->position = engine::ai::MoveAgentWithCollision(
                    m_playPhysics, *m_playRegistry, a.entity,
                    ta->position, ta->position + delta);
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

    const EditorScene::GameModeSettings& gameMode =
        m_scene.GetGameModeSettings();
    bool configuredPlayerExists = false;
    if (!gameMode.playerObjectName.empty()) {
        for (const EditorScene::Object& candidate : m_scene.Objects()) {
            if (candidate.playerControllerEnabled
                && candidate.name == gameMode.playerObjectName) {
                configuredPlayerExists = true;
                break;
            }
        }
    }
    for (const EditorScene::Object& object : m_scene.Objects()) {
        if (!object.playerControllerEnabled) {
            continue;
        }
        if (configuredPlayerExists
            && object.name != gameMode.playerObjectName) {
            continue;
        }

        const auto found = playEntitiesByName.find(object.name);
        if (found == playEntitiesByName.end()) {
            continue;
        }

        const EditorScene::PlayerControllerSettings& settings = object.playerController;
        engine::PlayerController controller;
        const int authoredCameraMode =
            settings.firstPerson ? 1 : std::clamp(settings.cameraMode, 0, 3);
        const int cameraMode = gameMode.cameraOverride
            ? std::clamp(gameMode.cameraMode, 0, 3)
            : authoredCameraMode;
        controller.view = cameraMode == 1 ? engine::PlayerController::View::FirstPerson
                        : cameraMode == 2 ? engine::PlayerController::View::Isometric
                        : cameraMode == 3 ? engine::PlayerController::View::Platformer
                                          : engine::PlayerController::View::ThirdPerson;
        controller.walkSpeed = settings.walkSpeed;
        controller.runSpeed = settings.runSpeed;
        controller.jumpSpeed = settings.jumpSpeed;
        controller.crouchSpeed = settings.crouchSpeed;
        controller.crouchedHeight = settings.crouchedHeight;
        controller.swimSpeed = settings.swimSpeed;
        controller.swimVerticalSpeed = settings.swimVerticalSpeed;
        controller.lookSensitivity = settings.lookSensitivity;
        controller.eyeHeight = settings.eyeHeight;
        controller.camDistance = settings.cameraDistance;
        controller.camTargetHeight = settings.cameraTargetHeight;
        controller.SetIsometricView(settings.isometricYaw, settings.isometricPitch,
                                    settings.isometricDistance);
        // Platformer reuses the fixed-camera distance (isometric) for its side offset,
        // with its own authored axis yaw.
        controller.platformerDistance = settings.isometricDistance;
        controller.platformerYaw = settings.platformerYaw;
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
        controller.facingMode = settings.facingMode == 1
            ? engine::PlayerController::FacingMode::MovementDirection
            : engine::PlayerController::FacingMode::CameraRelative;
        controller.turnSpeed = settings.turnSpeed;
        controller.body.stepHeight = settings.stepHeight;
        controller.body.SetMaxSlopeDegrees(settings.maxSlopeDegrees);
        controller.SetCapsule(settings.capsuleRadius, settings.capsuleHeight);

        m_playPlayerEntity = found->second;
        if (const Transform* transform = m_playRegistry->TryGet<Transform>(m_playPlayerEntity)) {
            controller.SetPosition(transform->position);
        }
        engine::ecs::Collider playerProxy = engine::ecs::Collider::MakeCapsuleFromHeight(
            controller.body.radius, controller.body.height);
        playerProxy.isTrigger = true;
        playerProxy.layer = engine::ecs::CollisionLayer::Player;
        playerProxy.mask = engine::ecs::CollisionLayer::All;
        m_playRegistry->Add<engine::ecs::Collider>(m_playPlayerEntity, playerProxy);
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
        input.crouch = window.IsKeyPressed(GLFW_KEY_LEFT_CONTROL)
            || window.IsKeyPressed(GLFW_KEY_RIGHT_CONTROL)
            || window.IsKeyPressed(GLFW_KEY_C);
        input.toggleShoulder = window.IsKeyPressed(GLFW_KEY_Q);

        // With the cursor captured in play mode, mouse movement always drives the
        // camera (no need to hold RMB). Still honour the classic RMB/pinned look as
        // a fallback when the cursor has been freed (e.g. via ESC).
        const bool rightMouseDown = window.Native()
            && glfwGetMouseButton(window.Native(), GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
        if (m_playMouseCaptured || rightMouseDown
            || m_cameraController.MouseLookActive()) {
            input.lookYaw = window.MouseDeltaX();
            input.lookPitch = window.MouseDeltaY();
        }
    }

    UpdatePlayLockOn(inputEnabled);

    bool overWater = false;
    const float waterY = WaterSurfaceY(
        m_playPlayerController->body.position.x,
        m_playPlayerController->body.position.z, overWater);
    m_playPlayerController->SetWaterSurface(overWater, waterY);

    engine::AnimatedModel* animated =
        m_playRegistry->TryGet<engine::AnimatedModel>(m_playPlayerEntity);
    const bool movementLocked = animated && animated->BlocksMovement();
    if (animated) {
        const float moveMagnitude = std::min(glm::length(glm::vec2(input.moveForward, input.moveRight)), 1.0f);
        const bool sprinting = input.sprint
            && m_playPlayerController->body.grounded && !input.jump
            && !input.crouch && !m_playPlayerController->Swimming();
        const float speed = movementLocked
            ? 0.0f
            : moveMagnitude * (sprinting
                ? m_playPlayerController->runSpeed
                : m_playPlayerController->walkSpeed);
        const float previousSpeed = animated->controller.Parameter("Speed", 0.0f);
        const float invDt = dt > 0.0001f ? 1.0f / dt : 0.0f;
        animated->controller.SetParameter("Speed", speed);
        animated->controller.SetParameter("Direction",
            moveMagnitude > 0.001f ? glm::degrees(std::atan2(input.moveRight, input.moveForward)) : 0.0f);
        animated->controller.SetParameter("Acceleration", (speed - previousSpeed) * invDt);
        animated->controller.SetParameter("Deceleration", std::max(previousSpeed - speed, 0.0f) * invDt);
        animated->controller.SetParameter("TurnRate", input.lookYaw * invDt);
        animated->controller.SetBoolParameter("IsMoving", speed > 0.05f);
        animated->controller.SetBoolParameter("IsStopping", previousSpeed > 0.05f && speed <= 0.05f);
        animated->controller.SetParameter("VerticalSpeed", m_playPlayerController->body.velocity.y);
        animated->controller.SetBoolParameter("IsGrounded", m_playPlayerController->body.grounded);
        animated->controller.SetBoolParameter("IsFalling", !m_playPlayerController->body.grounded
            && m_playPlayerController->body.velocity.y < 0.0f);
    }

    m_playPlayerController->Update(*m_playRegistry, input, dt, !movementLocked);

    if (animated) {
        const float moveMagnitude = std::min(
            glm::length(glm::vec2(input.moveForward, input.moveRight)), 1.0f);
        const bool sprinting = input.sprint && m_playPlayerController->Grounded()
            && !input.jump && !m_playPlayerController->Crouching()
            && !m_playPlayerController->Swimming();
        const float movementSpeed = movementLocked ? 0.0f : moveMagnitude
            * (m_playPlayerController->Swimming() ? m_playPlayerController->swimSpeed
             : m_playPlayerController->Crouching() ? m_playPlayerController->crouchSpeed
             : sprinting ? m_playPlayerController->runSpeed
                         : m_playPlayerController->walkSpeed);
        animated->controller.SetParameter("Speed", movementSpeed);
        animated->controller.SetBoolParameter("IsMoving", movementSpeed > 0.05f);
        animated->controller.SetParameter("VerticalSpeed", m_playPlayerController->body.velocity.y);
        animated->controller.SetBoolParameter("IsGrounded", m_playPlayerController->Grounded());
        animated->controller.SetBoolParameter("IsFalling", !m_playPlayerController->Grounded()
            && !m_playPlayerController->Swimming()
            && m_playPlayerController->body.velocity.y < 0.0f);
        animated->controller.SetBoolParameter("IsCrouching", m_playPlayerController->Crouching());
        animated->controller.SetBoolParameter("IsSwimming", m_playPlayerController->Swimming());
    }

    // Terrain floor: terrain is a mesh (no physics collider), so after the controller's
    // collider sweep, stand the capsule on the terrain surface when it's over one.
    {
        engine::CharacterController& body = m_playPlayerController->body;
        bool overTerrain = false;
        const float surfaceY = TerrainSurfaceY(body.position.x, body.position.z, overTerrain);
        if (overTerrain && !m_playPlayerController->Swimming()) {
            const float feet = body.position.y - body.height * 0.5f;
            if (feet <= surfaceY + 0.02f) {            // at/below the surface -> stand on it
                body.position.y = surfaceY + body.height * 0.5f;
                if (body.velocity.y < 0.0f) body.velocity.y = 0.0f;
                body.grounded = true;
            }
        }
    }

    if (Transform* transform = m_playRegistry->TryGet<Transform>(m_playPlayerEntity)) {
        transform->position = m_playPlayerController->CapsulePosition();
        transform->rotation = m_playPlayerController->CapsuleRotation();
    }
    if (engine::ecs::Collider* proxy =
            m_playRegistry->TryGet<engine::ecs::Collider>(m_playPlayerEntity)) {
        proxy->shape = engine::ecs::ColliderShape::Capsule;
        proxy->radius = std::max(m_playPlayerController->body.radius, 0.01f);
        proxy->halfHeight = std::max(
            m_playPlayerController->body.height * 0.5f - proxy->radius, 0.0f);
    }

    m_camera.SetPosition(m_playPlayerController->CameraPosition());
    m_camera.LookAt(m_playPlayerController->CameraTarget());
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
            &m_cameraShake, &m_cameraDirector, &engine::GameMode::Instance(),
            &m_playPhysics);
        engine::ecs::UpdateGameplay(*m_playRegistry, step);
        engine::ecs::UpdateRuntimeMotion(*m_playRegistry, step);
        m_playAnimationEvents.clear();
        UpdateAI(step);
        engine::UpdateAbilities(*m_playRegistry, step);
        engine::UpdateProjectilesInPlace(*m_playRegistry, step);
        engine::UpdateHealth(*m_playRegistry);
        engine::UpdateRagdollsBeforePhysics(*m_playRegistry, m_playPhysics);
        ConfigurePlayFootIK();
        engine::UpdateAnimations(*m_playRegistry, step);
        ApplyWaterBuoyancy(step);
        m_playPhysics.Step(*m_playRegistry, step);
        engine::UpdateRagdollsAfterPhysics(*m_playRegistry, m_playPhysics, step);
        CapturePlayPhysicsEvents();
        engine::GameMode::Instance().Update(
            *m_playRegistry, m_playPlayerEntity, step);
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
            &m_cameraShake, &m_cameraDirector, &engine::GameMode::Instance(),
            &m_playPhysics);
        engine::ecs::UpdateGameplay(*m_playRegistry, step);
        engine::ecs::UpdateRuntimeMotion(*m_playRegistry, step);
        UpdateAI(step);
        engine::UpdateAbilities(*m_playRegistry, step);
        engine::UpdateProjectilesInPlace(*m_playRegistry, step);
        engine::UpdateHealth(*m_playRegistry);
        engine::UpdateRagdollsBeforePhysics(*m_playRegistry, m_playPhysics);
        ConfigurePlayFootIK();
        engine::UpdateAnimations(*m_playRegistry, step);
        ApplyWaterBuoyancy(step);
        m_playPhysics.Step(*m_playRegistry, step);
        engine::UpdateRagdollsAfterPhysics(*m_playRegistry, m_playPhysics, step);
        CapturePlayPhysicsEvents();
        engine::GameMode::Instance().Update(
            *m_playRegistry, m_playPlayerEntity, step);
        m_physicsAccumulator -= step;
        ++m_physicsStepsLastFrame;
    }

    if (m_physicsStepsLastFrame == kMaxPhysicsStepsPerFrame && m_physicsAccumulator >= step) {
        m_physicsAccumulator = 0.0f;
        m_log.Warning("Play physics skipped accumulated time to keep the editor responsive");
    }
}

void EditorApp::ConfigurePlayFootIK()
{
    if (!m_playRegistry) return;
    m_playRegistry->view<engine::AnimatedModel>().each(
        [&](engine::ecs::Entity entity, engine::AnimatedModel& am) {
            // Per-character enable comes from the character asset (set when the AnimatedModel
            // was built). The global View toggle is a force-on override for quick testing; it
            // never turns an asset-authored character off. (Restart Play to clear a forced-on.)
            if (m_playFootIK) am.footIK.enabled = true;
            if (am.footIK.enabled && !am.footIK.groundQuery) {
                // Downward scene ray that ignores the character's own collider.
                am.footIK.groundQuery =
                    [this, entity](const glm::vec3& origin, const glm::vec3& down, float maxDist,
                                   glm::vec3& hitPos, glm::vec3& hitNormal) {
                        engine::Ray ray{origin, down};
                        const engine::RaycastHit hit = m_playPhysics.Raycast(
                            *m_playRegistry, ray, maxDist, 0xFFFFFFFFu, entity);
                        if (!hit.hit) return false;
                        hitPos = hit.point;
                        hitNormal = hit.normal;
                        return true;
                    };
            }
        });
}

void EditorApp::CapturePlayPhysicsEvents()
{
    constexpr std::size_t kMaxRecentEvents = 32;

    if (m_playRegistry) {
        m_runtimeAudio.ProcessCollisionEvents(*m_playRegistry, m_playPhysics.Events());
        engine::ProcessParticleCollisionEvents(*m_playRegistry, m_playPhysics.Events());
        engine::QueueScriptCollisionEvents(*m_playRegistry, m_playPhysics.Events());
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
