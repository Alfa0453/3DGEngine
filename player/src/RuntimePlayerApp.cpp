#include "RuntimePlayerApp.h"
#include "GameScripts.h"

#include <engine/graphics/Primitives.h>
#include <engine/gameplay/Script.h>
#include <engine/gameplay/SaveGame.h>
#include <engine/gameplay/LuaScript.h>
#include <engine/gameplay/GameplaySystems.h>
#include <engine/gameplay/RagdollSystem.h>
#include <engine/gameplay/InteractionSystem.h>
#include <engine/gameplay/PortalSystem.h>
#include <engine/gameplay/CombatSystem.h>
#include <engine/gameplay/SpawnSystem.h>
#include <engine/gameplay/GameplayComponents.h>
#include <engine/gameplay/GameMode.h>
#include <engine/ai/BtScript.h>
#include <engine/ecs/RuntimeSystems.h>
#include <engine/ai/NavMeshBuilder.h>
#include <engine/ai/AgentCollision.h>
#include <engine/ai/AiMovement.h>
#include <engine/ai/Perception.h>
#include <engine/ai/Steering.h>
#include <engine/ecs/Systems.h>          // ECS render/update compatibility helpers
#include <engine/animation/AnimatedModel.h>
#include <engine/assets/ShaderAsset.h>
#include <engine/assets/ShaderGraphCompiler.h>
#include <engine/assets/TextureAsset.h>
#include <engine/assets/DayNightTimelineAsset.h>
#include <engine/graphics/ImageDecode.h>
#include <engine/graphics/EnvironmentLighting.h>
#include <engine/graphics/PostProcessVolume.h>
#include <engine/graphics/LightingScalability.h>
#include <engine/core/Paths.h>

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <vector>

using engine::ecs::Entity;
using engine::ecs::Transform;
using engine::ecs::MeshRenderer;
using engine::ecs::MeshPBR;
using engine::ecs::PbrMaterial;
using engine::RuntimeSceneLoader;

namespace {
engine::EnvironmentLightingState ResolveEnvironment(
    const RuntimeSceneLoader::Scene::Environment& e,
    const engine::DayNightCycle::Sample& sample) {
    engine::AtmosphereParameters atmosphere;
    atmosphere.rayleighDensity=e.atmosphereRayleigh;
    atmosphere.rayleighScaleHeightKm=e.atmosphereRayleighHeight;
    atmosphere.mieDensity=e.atmosphereMie;
    atmosphere.mieScaleHeightKm=e.atmosphereMieHeight;
    atmosphere.mieAnisotropy=e.atmosphereMieAnisotropy;
    atmosphere.ozone=e.atmosphereOzone;
    atmosphere.intensity=e.atmosphereIntensity;
    atmosphere.sunAngularDiameterDegrees=e.sunAngularDiameter;
    atmosphere.sunDiskIntensity=e.sunDiskIntensity;
    engine::EnvironmentCloudParameters clouds;
    clouds.enabled=e.clouds; clouds.coverage=e.cloudCoverage;
    clouds.density=e.cloudDensity; clouds.albedo=e.cloudColor;
    engine::NightEnvironment night;
    night.stars=e.stars; night.starIntensity=e.starIntensity;
    night.moon=e.moon; night.moonRadiance=e.moonColor*e.moonIntensity;
    night.moonAngularDiameterDegrees=e.moonAngularDiameter; night.moonPhase=e.moonPhase;
    night.moonGiContribution=e.moonGiContribution;
    engine::EnvironmentEnergyParameters energy;
    energy.dayIntensity=e.dayEnvironmentIntensity;
    energy.twilightIntensity=e.twilightEnvironmentIntensity;
    energy.nightIntensity=e.nightEnvironmentIntensity;
    energy.nightReflectionIntensity=e.nightReflectionIntensity;
    energy.nightFogScattering=e.nightFogScattering;
    energy.nightCloudAmbient=e.nightCloudAmbient;
    auto state=engine::ResolveEnvironmentLighting(e.timeOfDay,sample,atmosphere,clouds,night,
        energy,static_cast<engine::EnvironmentQuality>(std::clamp(e.environmentQuality,0,3)));
    state.sunRadiance*=e.sunIntensity; state.ambientRadiance*=e.skyLightIntensity;
    const glm::vec3 authoredMoon=e.moon
        ?glm::max(e.moonColor*e.moonIntensity*e.moonPhase*sample.nightFactor,glm::vec3(0.0f))
        :glm::vec3(0.0f);
    state.keyLightRadiance=glm::dot(authoredMoon,authoredMoon)>glm::dot(state.sunRadiance,state.sunRadiance)
        ?authoredMoon*e.moonGiContribution:state.sunRadiance;
    return state;
}
glm::vec3 EnvironmentKeyRadiance(const RuntimeSceneLoader::Scene::Environment& e,
                                 const engine::DayNightCycle::Sample& sample) {
    const glm::vec3 sun=sample.sunRadiance*std::max(e.sunIntensity,0.0f);
    const glm::vec3 moon=e.moon
        ?glm::max(e.moonColor*e.moonIntensity*e.moonPhase*sample.nightFactor,glm::vec3(0.0f))
        :glm::vec3(0.0f);
    return sun+moon;
}
float MaxLightComponent(const glm::vec3& value){return std::max({value.x,value.y,value.z});}
std::vector<engine::PostProcess::VolumetricLight> GatherVolumetricLights(
    engine::ecs::Registry& registry,const glm::vec3& cameraPosition,int maximumLights) {
    struct Candidate{float score;engine::PostProcess::VolumetricLight light;};std::vector<Candidate> candidates;
    registry.view<engine::ecs::Transform,engine::ecs::Light>().each(
        [&](Entity,engine::ecs::Transform& transform,engine::ecs::Light& light) {
            if(!light.affectVolumetricFog || light.type==engine::ecs::Light::Type::Directional
                || light.type==engine::ecs::Light::Type::Area) return;
            const float distance=glm::distance(cameraPosition,transform.position);if(distance>light.range+220.0f)return;
            engine::PostProcess::VolumetricLight volumeLight;
            volumeLight.position=transform.position; volumeLight.direction=glm::normalize(light.direction);
            volumeLight.radiance=glm::max(light.color*light.intensity,glm::vec3(0.0f));
            volumeLight.range=std::max(light.range,0.01f);
            volumeLight.outerCos=light.type==engine::ecs::Light::Type::Spot
                ? std::cos(glm::radians(light.outerAngle)):-1.0f;
            const float luminance=glm::dot(volumeLight.radiance,glm::vec3(0.2126f,0.7152f,0.0722f));
            candidates.push_back({float(light.volumetricPriority)*1000.0f+luminance/(1.0f+distance*distance),volumeLight});
        });
    const std::size_t keep=std::min<std::size_t>(candidates.size(),static_cast<std::size_t>(std::clamp(maximumLights,0,16)));
    if(keep<candidates.size())std::partial_sort(candidates.begin(),candidates.begin()+keep,candidates.end(),[](const Candidate&a,const Candidate&b){return a.score>b.score;});
    std::vector<engine::PostProcess::VolumetricLight> result;result.reserve(keep);for(std::size_t i=0;i<keep;++i)result.push_back(candidates[i].light);
    return result;
}
std::vector<engine::PostProcess::LocalFogVolume> GatherLocalFogVolumes(
    engine::ecs::Registry& registry) {
    std::vector<engine::PostProcess::LocalFogVolume> result;
    registry.view<engine::ecs::Transform,engine::ecs::LocalFogVolume>().each(
        [&](Entity,engine::ecs::Transform& transform,engine::ecs::LocalFogVolume& authored) {
            if(!authored.enabled || result.size()>=8) return;
            engine::PostProcess::LocalFogVolume fog;
            fog.position=transform.position;
            fog.boxExtents=glm::max(authored.boxExtents*glm::abs(transform.scale),glm::vec3(0.001f));
            const glm::vec3 absoluteScale=glm::abs(transform.scale);
            fog.radius=authored.radius*std::max(absoluteScale.x,std::max(absoluteScale.y,absoluteScale.z));
            fog.blendDistance=authored.blendDistance; fog.density=authored.density;
            fog.albedo=authored.albedo; fog.extinction=authored.extinction;
            fog.anisotropy=authored.anisotropy;
            fog.sphere=authored.shape==engine::ecs::LocalFogVolume::Shape::Sphere;
            result.push_back(fog);
        });
    return result;
}
engine::WindowProps MakeProps(engine::Config& cfg) {
    engine::WindowProps p;
    p.title  = "3DGEngine — Runtime Player";
    p.width  = cfg.GetInt("window.width", 1280);
    p.height = cfg.GetInt("window.height", 720);
    p.vsync  = cfg.GetBool("window.vsync", true);
    return p;
}

engine::ProceduralSky::CloudSettings SkyClouds(
    const RuntimeSceneLoader::Scene::Environment& environment) {
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

engine::WaterConfig RuntimeWaterConfig(
    const RuntimeSceneLoader::Scene::WaterDesc& water) {
    engine::WaterConfig config;
    config.center = water.center;
    config.size = water.size;
    config.resolution = water.resolution;
    config.shallowColor = water.shallow;
    config.deepColor = water.deep;
    config.reflectionColor = water.reflection;
    config.transparency = water.transparency;
    config.fresnelPower = water.fresnel;
    config.specularStrength = water.specular;
    config.shininess = water.shininess;
    config.seaHeight = water.seaHeight;
    config.seaChoppy = water.seaChoppy;
    config.seaSpeed = water.seaSpeed;
    config.seaFreq = water.seaFreq;
    config.foamAmount = water.foam;
    config.flowDir = water.flowDir;
    config.flowStrength = water.flowStrength;
    config.riverWidth = water.riverWidth;
    config.splineClosed = water.splineClosed;
    config.splinePoints = water.splinePoints;
    config.splinePointRotations = water.splinePointRotations;
    config.depthFadeDistance = water.depthFadeDistance;
    config.shorelineFoamWidth = water.shoreFoamWidth;
    config.shorelineFoamStrength = water.shoreFoamStrength;
    config.refractionStrength = water.refractionStrength;
    config.reflectionRoughness = water.reflectionRoughness;
    config.environmentReflectionStrength = water.environmentReflectionStrength;
    config.absorptionStrength = water.absorptionStrength;
    config.causticsStrength = water.causticsStrength;
    config.causticsScale = water.causticsScale;
    config.maxRenderDistance = water.maxRenderDistance;
    if (!water.shaderPath.empty()) {
        if (std::filesystem::path(water.shaderPath).extension() == ".3dgshader") {
            engine::ShaderAsset asset;
            std::string error;
            if (engine::LoadShaderAsset(water.shaderPath, &asset, &error)) {
                config.customFragmentSource =
                    engine::GenerateWaterFragmentBody(asset, &error);
            }
        } else {
            std::ifstream file(water.shaderPath, std::ios::binary);
            if (file) {
                config.customFragmentSource.assign(
                    std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
            }
        }
    }
    return config;
}

void RuntimeColliderMetrics(const engine::ecs::Collider& collider,
                            const glm::vec3& scale, float& radius,
                            float& halfY, float& volume) {
    constexpr float pi = 3.14159265f;
    const glm::vec3 s = glm::abs(scale);
    const float horizontal = std::max(s.x, s.z);
    const float scaleVolume = std::max(s.x * s.y * s.z, 0.0001f);
    radius = 0.5f * horizontal;
    halfY = 0.5f * s.y;
    volume = scaleVolume;
    switch (collider.shape) {
    case engine::ecs::ColliderShape::Sphere:
        radius = collider.radius * horizontal;
        halfY = collider.radius * s.y;
        volume = (4.0f / 3.0f) * pi * std::pow(collider.radius, 3.0f) * scaleVolume;
        break;
    case engine::ecs::ColliderShape::Box:
    case engine::ecs::ColliderShape::Pyramid:
    case engine::ecs::ColliderShape::Staircase:
        radius = std::max(collider.halfExtents.x * s.x, collider.halfExtents.z * s.z);
        halfY = collider.halfExtents.y * s.y;
        volume = 8.0f * collider.halfExtents.x * collider.halfExtents.y
                 * collider.halfExtents.z * scaleVolume;
        if (collider.shape == engine::ecs::ColliderShape::Pyramid) volume /= 3.0f;
        if (collider.shape == engine::ecs::ColliderShape::Staircase) volume *= 0.5f;
        break;
    case engine::ecs::ColliderShape::Capsule:
        radius = collider.radius * horizontal;
        halfY = (collider.halfHeight + collider.radius) * s.y;
        volume = (pi * collider.radius * collider.radius * (2.0f * collider.halfHeight)
                 + (4.0f / 3.0f) * pi * std::pow(collider.radius, 3.0f)) * scaleVolume;
        break;
    case engine::ecs::ColliderShape::Cylinder:
    case engine::ecs::ColliderShape::Cone:
        radius = collider.radius * horizontal;
        halfY = collider.halfHeight * s.y;
        volume = pi * collider.radius * collider.radius
                 * (2.0f * collider.halfHeight) * scaleVolume;
        if (collider.shape == engine::ecs::ColliderShape::Cone) volume /= 3.0f;
        break;
    case engine::ecs::ColliderShape::Torus:
        radius = (collider.majorRadius + collider.minorRadius) * horizontal;
        halfY = collider.minorRadius * s.y;
        volume = 2.0f * pi * pi * collider.majorRadius
                 * collider.minorRadius * collider.minorRadius * scaleVolume;
        break;
    default: break;
    }
    radius = std::max(radius, 0.1f);
    halfY = std::max(halfY, 0.05f);
    volume = std::max(volume, 0.0001f);
}
} // namespace

RuntimePlayerApp::RuntimePlayerApp(engine::Config& config, std::string scenePath)
    : engine::Application(MakeProps(config)),
      m_config(config),
      m_scenePath(std::move(scenePath)),
      m_runtimeAudio(m_audio) {}

void RuntimePlayerApp::OnInit() {
    // Register scripts before anything can instantiate them. Built-in example BT
    // scripts + the game's own scripts (player/src/GameScripts.cpp).
    engine::ai::RegisterExampleBtScripts();
    RegisterGameScripts();

    // Cooked games place the project-owned native script module beside player.exe.
    // Development runs may point directly into Content, so also walk up to the project
    // root and check its Binaries folder.
    {
        std::vector<std::filesystem::path> candidates;
#if defined(_WIN32)
        candidates.emplace_back(std::filesystem::path(engine::ExecutableDir())
            / "game_scripts.dll");
        std::error_code moduleEc;
        std::filesystem::path cursor =
            std::filesystem::absolute(m_scenePath, moduleEc).parent_path();
        for (int depth = 0; depth < 8 && !cursor.empty(); ++depth) {
            candidates.emplace_back(cursor / "Binaries" / "game_scripts.dll");
            const std::filesystem::path parent = cursor.parent_path();
            if (parent == cursor) break;
            cursor = parent;
        }
#endif
        for (const std::filesystem::path& candidate : candidates) {
            std::error_code existsEc;
            if (!std::filesystem::is_regular_file(candidate, existsEc)) continue;
            std::string moduleError;
            if (!m_projectScriptModule.Load(candidate.string(),
                    engine::ScriptRegistry::Instance(),
                    engine::ai::BtScriptRegistry::Instance(), &moduleError)) {
                std::fprintf(stderr, "[Scripts] %s\n", moduleError.c_str());
            } else {
                std::fprintf(stdout, "[Scripts] Loaded project module: %s\n",
                             candidate.string().c_str());
            }
            break;
        }
    }

    m_renderer.Init();

    // Primitive meshes the runtime scene loader maps names onto.
    m_cube.emplace(engine::primitives::Cube());
    m_plane.emplace(engine::primitives::Plane(1.0f, 12.0f));
    m_sphere.emplace(engine::primitives::Sphere(24));
    m_capsule.emplace(engine::primitives::Capsule());
    m_cylinder.emplace(engine::primitives::Cylinder());
    m_cone.emplace(engine::primitives::Cone());
    m_pyramid.emplace(engine::primitives::Pyramid());
    m_torus.emplace(engine::primitives::Torus());
    m_staircase.emplace(engine::primitives::Staircase());

    m_pbr.emplace(2048);
    m_foliageRenderer.emplace();
    // Default shader for imported static models (Blinn-Phong; DrawModel binds the
    // material maps). Matches the editor's play-mode model pass.
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
                if (uHasDiffuse == 1) base *= texture(uDiffuseTex, vUV).rgb;
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
                if (uHasSpecular == 1) specularColor *= texture(uSpecularTex, vUV).rgb;
                vec3 emissiveColor = uEmissive;
                if (uHasEmissive == 1) emissiveColor *= texture(uEmissiveTex, vUV).rgb;
                vec3 ambient = base * uLightColor * 0.18;
                vec3 diffuse = base * uLightColor * diffuseAmount;
                vec3 specular = specularColor * uLightColor * specularAmount;
                FragColor = vec4(ambient + diffuse + specular + emissiveColor, 1.0);
            }
        )glsl");
    m_skinnedRenderer.emplace();
    m_sky.emplace();
    m_post.emplace(GetWindow().Width(), GetWindow().Height());
    m_text.emplace();
    m_particleRenderer.emplace();

    LoadScene();
    ConfigurePhysics();

    // Imported (marketplace) equirectangular sky, when the scene uses one.
    if (m_scene.environment.skyMode == 1 && !m_scene.environment.skyTexturePath.empty()) {
        try {
            m_importedSky.emplace(engine::Skybox::FromEquirectangular(
                m_scene.environment.skyTexturePath, 1024));
        } catch (const std::exception&) {
            m_importedSky.reset();
        }
    }

    // Image-based lighting baked from the active sky at the scene's time of day.
    m_ibl.emplace(256);
    m_ibl->Generate([&](const glm::mat4& v, const glm::mat4& p) {
        DrawEnvironmentSky(v, p, false);
    });

    glfwGetCursorPos(GetWindow().Native(), &m_lastMouseX, &m_lastMouseY);
    if (HasPlayer() && !m_paused) SetPlayCursor(true);   // FPS-style mouse-look
}

void RuntimePlayerApp::PostProcessLoadedEntities(const std::vector<Entity>& entities) {
    for (Entity entity : entities) {
        if (!m_registry.Valid(entity)) continue;

        // Animation-event routing (audio + particles + event log).
        if (engine::AnimatedModel* animated = m_registry.TryGet<engine::AnimatedModel>(entity)) {
            animated->onEvent = [this, entity](const std::string& name) {
                if (name.empty()) return;
                m_runtimeAudio.ProcessAnimationEvent(m_registry, entity, name);
                engine::ProcessParticleAnimationEvent(m_registry, entity, name);
                engine::QueueScriptAnimationEvent(m_registry, entity, name);
                m_animationEvents.push_back({entity, name});
            };
        }

        // The loader adds MeshRenderer (mesh + authored colour); the PBR renderer draws
        // MeshPBR. Give each plain drawable its resolved PBR material, or a default from
        // the authored colour. Static models + skinned characters draw via their own
        // passes, so skip those; skip anything already converted (idempotent).
        if (m_registry.Has<MeshRenderer>(entity)
            && !m_registry.Has<engine::ecs::LoadedModelAsset>(entity)
            && !m_registry.Has<engine::AnimatedModel>(entity)
            && !m_registry.Has<MeshPBR>(entity)) {
            const MeshRenderer& mr = m_registry.Get<MeshRenderer>(entity);
            MeshPBR mesh;
            mesh.mesh = mr.mesh;
            if (const engine::ecs::LoadedMaterialAsset* lm =
                    m_registry.TryGet<engine::ecs::LoadedMaterialAsset>(entity)) {
                mesh.material = lm->material;
                mesh.customShader = lm->shader;
                mesh.shaderParameters = lm->shaderParameters;
                mesh.shaderParameterTypes = lm->shaderParameterTypes;
                mesh.shaderTextures = lm->shaderTextures;
            } else {
                mesh.material.albedo = mr.color;
                mesh.material.metallic = 0.0f;
                mesh.material.roughness = 0.6f;
            }
            m_registry.Add<MeshPBR>(entity, std::move(mesh));
        }
    }
}

void RuntimePlayerApp::RebuildResidentScene() {
    m_scene = m_persistentScene;
    const auto append = [](auto& destination, const auto& source) {
        destination.insert(destination.end(), source.begin(), source.end());
    };
    for (const auto& [index, scene] : m_streamedScenes) {
        (void)index;
        append(m_scene.navBounds, scene.navBounds);
        append(m_scene.navAgents, scene.navAgents);
        append(m_scene.triggerActions, scene.triggerActions);
        append(m_scene.cameraZones, scene.cameraZones);
        append(m_scene.physicsJoints, scene.physicsJoints);
        append(m_scene.terrains, scene.terrains);
        append(m_scene.waters, scene.waters);
        append(m_scene.splines, scene.splines);
        append(m_scene.foliage, scene.foliage);
        append(m_scene.cameraPresets, scene.cameraPresets);
        append(m_scene.cameraSequences, scene.cameraSequences);
        append(m_scene.entities, scene.entities);
        append(m_scene.lights, scene.lights);
    }
}

void RuntimePlayerApp::RebuildResidentSystems() {
    RebuildResidentScene();
    BuildTerrains();
    BuildWaters();
    BuildRuntimeLevelFeatures();
    BuildAI();
}

void RuntimePlayerApp::ActivateStreamedLevel(
    std::size_t levelIndex,
    const RuntimeSceneLoader::Scene& scene,
    const std::vector<Entity>& entities) {
    PostProcessLoadedEntities(entities);
    if (const auto state = m_streamedScriptStates.find(levelIndex);
        state != m_streamedScriptStates.end()) {
        engine::RestoreScriptPersistentStates(m_registry, entities, state->second);
    }
    m_entityCount += entities.size();
    m_streamedScenes[levelIndex] = scene;
    RebuildResidentSystems();
}

void RuntimePlayerApp::PrepareStreamedLevelUnload(
    std::size_t levelIndex,
    const RuntimeSceneLoader::Scene&,
    const std::vector<Entity>& entities) {
    auto& scriptState = m_streamedScriptStates[levelIndex];
    engine::CaptureScriptPersistentStates(m_registry, entities, scriptState);
    engine::ShutdownScripts(m_registry, entities);
    m_entityCount = entities.size() > m_entityCount
        ? 0 : m_entityCount - entities.size();
    for (Entity entity : entities) {
        m_runtimeAudio.Stop(entity);
        m_prevHp.erase(entity);
        m_cameraZonesInside.erase(entity);
        m_triggerActions.erase(entity);
        m_cameraZones.erase(entity);
        if (entity == m_lockTarget) m_lockTarget = engine::ecs::kNull;
        if (entity == m_hudHealthEntity && entity != m_playerEntity)
            m_hudHealthEntity = m_playerEntity;
        if (entity == m_playerEntity) {
            m_playerController.reset();
            m_playerEntity = engine::ecs::kNull;
        }
    }
}

void RuntimePlayerApp::FinishStreamedLevelUnload(std::size_t levelIndex) {
    m_streamedScenes.erase(levelIndex);
    RebuildResidentSystems();
}

void RuntimePlayerApp::LoadScene() {
    m_runtimeWarnings.clear();
    m_dynamicGi.Reset();
    m_lightingProbeGrid.Reset();
    m_loadedLightingData.reset();
    m_dynamicGiConfigured = false;
    m_dynamicGiFrame = 0;
    if (m_scenePath.empty()) {
        m_loadError = "No scene specified. Pass a .3dgscene or .3dgworld path on the command line.";
        return;
    }

    std::string err;
    std::string bootScenePath = m_scenePath;
    m_streamingEnabled = false;
    m_lastStreamingError.clear();

    // A .3dgworld boots its always-resident persistent level as the main scene; the
    // streamed levels are driven by the LevelStreamingManager after setup.
    {
        std::string ext = std::filesystem::path(m_scenePath).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ext == ".3dgworld") {
            if (!engine::LoadWorldManifest(m_scenePath, &m_worldManifest, &err)) {
                m_loadError = "World load failed: " + err;
                return;
            }
            m_worldDir = std::filesystem::path(m_scenePath).parent_path().string();
            bootScenePath = m_worldDir.empty()
                ? m_worldManifest.persistentScenePath
                : (std::filesystem::path(m_worldDir) / m_worldManifest.persistentScenePath).string();
            m_streamingEnabled = true;
        }
    }

    if (!RuntimeSceneLoader::Load(bootScenePath, &m_scene, &err)) {
        m_loadError = "Load failed: " + err;
        return;
    }
    m_persistentScene = m_scene;
    m_streamedScenes.clear();
    m_streamedScriptStates.clear();

    RuntimeSceneLoader::PrimitiveMeshes meshes;
    meshes.cube      = &*m_cube;
    meshes.plane     = &*m_plane;
    meshes.sphere    = &*m_sphere;
    meshes.capsule   = &*m_capsule;
    meshes.cylinder  = &*m_cylinder;
    meshes.cone      = &*m_cone;
    meshes.pyramid   = &*m_pyramid;
    meshes.torus     = &*m_torus;
    meshes.staircase = &*m_staircase;
    m_primitiveMeshes = meshes;   // stashed so streamed levels can instantiate each frame

    std::vector<Entity> created;
    if (!RuntimeSceneLoader::Instantiate(m_scene, m_registry, meshes, &created, &err)) {
        m_loadError = "Instantiate failed: " + err;
        return;
    }
    m_entityCount = created.size();
    m_sample = engine::DayNightCycle::At(m_scene.environment.timeOfDay);
    // Baked lighting is a scene resource, not a Dynamic-GI implementation
    // detail. Load it for every runtime scene so editor play and packaged play
    // bind the same probe grid even when Dynamic GI is disabled.
    if (!m_scene.environment.lightingBuildAsset.empty()) {
        std::filesystem::path lightingPath(m_scene.environment.lightingBuildAsset);
        if (!lightingPath.is_absolute() && !std::filesystem::exists(lightingPath))
            lightingPath = std::filesystem::path(bootScenePath).parent_path() / lightingPath;
        lightingPath = lightingPath.lexically_normal();
        engine::LightingBuildData data;
        std::string lightingError;
        if (engine::LoadLightingBuildData(lightingPath.string(), &data, &lightingError)) {
            if (m_lightingProbeGrid.Upload(data, &lightingError)) {
                m_loadedLightingData = std::move(data);
            } else {
                m_runtimeWarnings.push_back("Lighting upload: " + lightingError);
            }
        } else {
            m_runtimeWarnings.push_back("Lighting load: " + lightingError
                                        + " (" + lightingPath.string() + ")");
        }
    }
    if (!m_scene.environment.dayNightTimelinePath.empty()) {
        std::filesystem::path timelinePath(m_scene.environment.dayNightTimelinePath);
        if (!timelinePath.is_absolute() && !std::filesystem::exists(timelinePath))
            timelinePath = std::filesystem::path(bootScenePath).parent_path() / timelinePath;
        std::string timelineError;
        auto& timeline = engine::DayNightTimelineRuntime::Instance();
        if (!timeline.Load(timelinePath.lexically_normal().string(), &timelineError))
            m_runtimeWarnings.push_back("Day/night timeline: " + timelineError);
        else if (m_scene.environment.dayNightTimelineAutoplay) timeline.Play();
        else timeline.Pause();
    }

    // Resolve authored asset references (materials/models/skinned) into loaded GPU
    // assets: MaterialAsset -> LoadedMaterialAsset, etc. Asset paths resolve against
    // the working directory (run from the content root, or bundle assets to match).
    const engine::RuntimeAssetManager::ResolveReport report = m_assets.ResolveRegistryAssets(m_registry);
    m_assetErrors = static_cast<int>(report.errors.size());
    for (const std::string& assetError : report.errors)
        m_runtimeWarnings.push_back("Asset: " + assetError);
    m_assets.RebuildFoliageCollisionProxies(m_registry);

    // Per-entity setup (animation-event hookup + MeshPBR conversion). Factored so a
    // streamed level can run the same setup on just its new entities.
    PostProcessLoadedEntities(created);

    SetupPlayer();

    // Bind the HUD's health widgets to the player if it has Health, else the first
    // entity that has a Health component.
    m_hudHealthEntity = engine::ecs::kNull;
    if (m_playerEntity != engine::ecs::kNull && m_registry.Has<engine::Health>(m_playerEntity)) {
        m_hudHealthEntity = m_playerEntity;
    } else {
        m_registry.view<Transform, engine::Health>().each([&](Entity e, Transform&, engine::Health&) {
            if (m_hudHealthEntity == engine::ecs::kNull) m_hudHealthEntity = e;
        });
    }

    m_sceneDir = std::filesystem::path(m_scenePath).parent_path().string();
    BuildTerrains();
    BuildWaters();
    BuildRuntimeLevelFeatures();
    BuildAI();
    ValidateRuntimeScene();
    LoadHud();

    engine::GameMode::Instance().Reset();
    engine::GameMode::Instance().loseOnPlayerDeath =
        m_scene.gameMode.loseOnPlayerDeath;
    engine::GameMode::Instance().SetScore(m_scene.gameMode.initialScore);
    m_paused = m_scene.gameMode.startPaused;
    if (m_paused) {
        engine::GameMode::Instance().Pause();
    }

    // Wire streaming for a world: the manager loads/unloads the streamed levels around
    // the viewer, running the same per-entity setup on each newly-activated level.
    if (m_streamingEnabled) {
        m_streaming.Configure(m_worldManifest, m_worldDir);
        m_streaming.SetActivateHook([this](
            std::size_t index,
            const RuntimeSceneLoader::Scene& scene,
            const std::vector<Entity>& newEntities) {
            ActivateStreamedLevel(index, scene, newEntities);
        });
        m_streaming.SetBeforeDeactivateHook([this](
            std::size_t index,
            const RuntimeSceneLoader::Scene& scene,
            const std::vector<Entity>& entities) {
            PrepareStreamedLevelUnload(index, scene, entities);
        });
        m_streaming.SetDeactivateHook([this](std::size_t index) {
            FinishStreamedLevelUnload(index);
        });
    }

    m_simReady = true;
}

void RuntimePlayerApp::LoadHud() {
    m_hudLoaded = false;
    const std::string& rel = m_scene.environment.hudAsset;
    if (rel.empty()) return;

    std::string err;
    // Try relative to the scene file first, then as an absolute/CWD-relative path.
    const std::string beside = m_sceneDir.empty()
        ? rel : (std::filesystem::path(m_sceneDir) / rel).string();
    if (m_hud.Load(beside, &err) || m_hud.Load(rel, &err)) {
        m_hudLoaded = true;
    }
}

unsigned int RuntimePlayerApp::HudTextureId(const std::string& relPath) {
    if (relPath.empty()) return 0;
    const std::string full = m_sceneDir.empty()
        ? relPath : (std::filesystem::path(m_sceneDir) / relPath).string();
    const engine::Texture* tex = m_assets.LoadTexture(full);
    return tex ? tex->ID() : 0u;
}

void RuntimePlayerApp::ConfigurePhysics() {
    const RuntimeSceneLoader::Scene::Environment& env = m_scene.environment;
    m_physics.gravity                = env.physicsGravity;
    m_physics.solverIterations       = std::max(env.physicsSolverIterations, 12);   // floor for solver convergence (Pass-3)
    m_physics.broadPhase             = env.physicsBroadPhase;
    m_physics.cellSize               = env.physicsCellSize;
    m_physics.restitutionThreshold   = std::max(env.physicsRestitutionThreshold, 1.0f);   // floor: avoid resting jitter
    m_physics.allowSleeping          = env.physicsAllowSleeping;
    m_physics.sleepLinearVelocity    = env.physicsSleepLinearVelocity;
    m_physics.sleepAngularVelocity   = env.physicsSleepAngularVelocity;
    m_physics.timeToSleep            = env.physicsTimeToSleep;
}

std::vector<engine::LightingTriangle> RuntimePlayerApp::GatherLightingTriangles() const {
    std::vector<engine::LightingTriangle> triangles;
    std::unordered_map<std::string, std::shared_ptr<const engine::LightingTextureData>> textureCache;
    auto cpuTexture = [&](const std::string& authoredPath) {
        if (authoredPath.empty()) return std::shared_ptr<const engine::LightingTextureData>{};
        std::filesystem::path path(authoredPath);
        if (!path.is_absolute()) path = std::filesystem::path(m_sceneDir) / path;
        const std::string key = path.lexically_normal().string();
        if (const auto found = textureCache.find(key); found != textureCache.end()) return found->second;
        engine::TextureAssetData source;
        std::shared_ptr<const engine::LightingTextureData> result;
        if (engine::LoadTextureAsset(key, &source, nullptr)) {
            auto loaded = std::make_shared<engine::LightingTextureData>();
            loaded->width = source.width; loaded->height = source.height;
            loaded->srgb = source.srgb; loaded->repeat = true;
            loaded->rgba = std::move(source.rgba); result = std::move(loaded);
        }
        textureCache.emplace(key, result);
        return result;
    };
    auto entityTexture = [&](Entity entity) {
        if (const auto* asset = const_cast<engine::ecs::Registry&>(m_registry)
                .TryGet<engine::ecs::MaterialAsset>(entity)) {
            if (!asset->albedoPath.empty()) return cpuTexture(asset->albedoPath);
            if (!asset->path.empty()) {
                std::filesystem::path materialPath(asset->path);
                if (!materialPath.is_absolute())
                    materialPath = std::filesystem::path(m_sceneDir) / materialPath;
                engine::RuntimeMaterialAsset material;
                if (engine::LoadMaterialAssetFile(
                        materialPath.lexically_normal().string(), &material, nullptr)) {
                    std::filesystem::path albedo(material.albedoMapPath);
                    if (!albedo.empty() && !albedo.is_absolute())
                        albedo = materialPath.parent_path() / albedo;
                    return cpuTexture(albedo.lexically_normal().string());
                }
            }
        }
        return std::shared_ptr<const engine::LightingTextureData>{};
    };
    auto appendMesh = [&](const engine::Mesh& mesh, const glm::mat4& model,
                          const glm::vec3& albedo, const glm::vec3& emissive,
                          float metallic, std::uint64_t entityId,
                          std::uint32_t materialSlot,
                          const std::shared_ptr<const engine::LightingTextureData>& baseColorTexture) {
        const auto vertices = mesh.ReadbackVertices();
        const auto indices = mesh.ReadbackIndices();
        const std::size_t stride = mesh.VertexStrideFloats();
        if (stride < 3 || indices.size() < 3) return;
        auto point = [&](std::uint32_t index) {
            const std::size_t base = static_cast<std::size_t>(index) * stride;
            return base + 2 < vertices.size()
                ? glm::vec3(model * glm::vec4(vertices[base], vertices[base + 1], vertices[base + 2], 1.0f))
                : glm::vec3(0.0f);
        };
        const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(model)));
        auto normal = [&](std::uint32_t index) {
            const std::size_t base = static_cast<std::size_t>(index) * stride;
            if (stride < 6 || base + 5 >= vertices.size()) return glm::vec3(0.0f);
            const glm::vec3 value = normalMatrix * glm::vec3(vertices[base + 3], vertices[base + 4], vertices[base + 5]);
            return glm::dot(value, value) > 1e-10f ? glm::normalize(value) : glm::vec3(0.0f);
        };
        auto uv = [&](std::uint32_t index) {
            const std::size_t base = static_cast<std::size_t>(index) * stride;
            return stride >= 8 && base + 7 < vertices.size()
                ? glm::vec2(vertices[base + 6], vertices[base + 7]) : glm::vec2(0.0f);
        };
        triangles.reserve(triangles.size() + indices.size() / 3);
        for (std::size_t i = 0; i + 2 < indices.size(); i += 3) {
            const std::uint32_t a = indices[i], b = indices[i + 1], c = indices[i + 2];
            engine::LightingTriangle triangle;
            triangle.a = point(a); triangle.b = point(b); triangle.c = point(c);
            triangle.albedo = glm::clamp(albedo, glm::vec3(0.0f), glm::vec3(1.0f));
            triangle.emissive = glm::max(emissive, glm::vec3(0.0f));
            triangle.metallic = glm::clamp(metallic, 0.0f, 1.0f);
            triangle.normalA = normal(a); triangle.normalB = normal(b); triangle.normalC = normal(c);
            triangle.uvA = uv(a); triangle.uvB = uv(b); triangle.uvC = uv(c);
            triangle.baseColorTexture = baseColorTexture;
            triangle.entityId = entityId; triangle.materialSlot = materialSlot;
            triangles.push_back(std::move(triangle));
        }
    };
    const_cast<engine::ecs::Registry&>(m_registry).view<engine::ecs::Transform, engine::ecs::MeshPBR>().each(
        [&](Entity entity, engine::ecs::Transform& transform, engine::ecs::MeshPBR& renderer) {
            if (renderer.mesh) appendMesh(*renderer.mesh, transform.Model(),
                                          renderer.material.albedo, renderer.material.emissive,
                                          renderer.material.metallic,
                                          static_cast<std::uint64_t>(entity), 0u,
                                          entityTexture(entity));
        });
    const_cast<engine::ecs::Registry&>(m_registry).view<engine::ecs::Transform, engine::ecs::LoadedModelAsset>().each(
        [&](Entity entity, engine::ecs::Transform& transform, engine::ecs::LoadedModelAsset& loaded) {
            if (!loaded.model) return;
            const auto* overrideMaterial = const_cast<engine::ecs::Registry&>(m_registry)
                .TryGet<engine::ecs::LoadedMaterialAsset>(entity);
            const auto overrideTexture = entityTexture(entity);
            for (const engine::SubMesh& submesh : loaded.model->SubMeshes()) {
                glm::vec3 albedo(0.8f), emissive(0.0f); float metallic = 0.0f;
                if (submesh.material >= 0
                    && static_cast<std::size_t>(submesh.material) < loaded.model->Materials().size()) {
                    const engine::Material& material = loaded.model->Materials()[submesh.material];
                    albedo = material.diffuse; emissive = material.emissive; metallic = material.metallic;
                }
                if (overrideMaterial) {
                    albedo = overrideMaterial->material.albedo;
                    emissive = overrideMaterial->material.emissive;
                    metallic = overrideMaterial->material.metallic;
                }
                const auto baseColorTexture = overrideMaterial ? overrideTexture
                    : (submesh.material >= 0
                       && static_cast<std::size_t>(submesh.material)
                           < loaded.model->Materials().size()
                        ? cpuTexture(loaded.model->Materials()[
                              static_cast<std::size_t>(submesh.material)].diffuseMapPath)
                        : std::shared_ptr<const engine::LightingTextureData>{});
                appendMesh(submesh.mesh, transform.Model(), albedo, emissive, metallic,
                           static_cast<std::uint64_t>(entity),
                           submesh.material >= 0 ? static_cast<std::uint32_t>(submesh.material) : 0u,
                           baseColorTexture);
            }
        });
    return triangles;
}

void RuntimePlayerApp::UpdateDynamicGi(const engine::Camera& camera) {
    const auto& env = m_scene.environment;
    if (!env.dynamicGiEnabled || env.dynamicGiQuality <= 0) {
        if (m_dynamicGi.Ready()) m_dynamicGi.Reset();
        m_dynamicGiConfigured = false;
        return;
    }
    const engine::DirectionalSkyRadiance sky =
        ResolveEnvironment(env, m_sample).ToDirectionalSkyRadiance();
    if (!m_dynamicGiConfigured) {
        engine::DynamicIrradianceSettings settings;
        settings.enabled = true;
        settings.quality = static_cast<engine::DynamicGiQuality>(std::clamp(env.dynamicGiQuality, 0, 3));
        settings.probeSpacing = env.dynamicGiProbeSpacing;
        settings.raysPerProbe = static_cast<std::uint32_t>(std::max(env.dynamicGiRaysPerProbe, 8));
        settings.probesPerFrame = static_cast<std::uint32_t>(std::max(env.dynamicGiProbesPerFrame, 1));
        settings.maxGiRaysPerFrame = static_cast<std::uint32_t>(std::max(env.dynamicGiMaxRaysPerFrame, 8));
        settings.maxRayDistance = env.dynamicGiMaxRayDistance;
        settings.hysteresis = env.dynamicGiHysteresis;
        settings.intensity = env.dynamicGiIntensity;
        settings.relocation = env.dynamicGiRelocation;
        settings.classification = env.dynamicGiClassification;
        settings.visibilityWeighting = env.dynamicGiVisibilityWeighting;
        settings.approximateMultiBounce = env.dynamicGiMultiBounce;
        settings.multiBounceStrength = env.dynamicGiMultiBounceStrength;
        std::vector<engine::LightingTriangle> geometry = GatherLightingTriangles();
        if (!m_loadedLightingData && !geometry.empty()) {
            glm::vec3 minimum(std::numeric_limits<float>::max());
            glm::vec3 maximum(-std::numeric_limits<float>::max());
            for (const auto& triangle : geometry) {
                minimum = glm::min(minimum, glm::min(triangle.a, glm::min(triangle.b, triangle.c)));
                maximum = glm::max(maximum, glm::max(triangle.a, glm::max(triangle.b, triangle.c)));
            }
            settings.boundsMin = minimum - glm::vec3(settings.probeSpacing);
            settings.boundsMax = maximum + glm::vec3(settings.probeSpacing);
        }
        std::string error;
        if (!m_dynamicGi.Configure(settings, sky,
                                   m_loadedLightingData ? &*m_loadedLightingData : nullptr, &error)) {
            m_runtimeWarnings.push_back("Dynamic GI: " + error); return;
        }
        m_dynamicGi.SetSceneGeometry(geometry);
        if (!m_dynamicGi.InitializeGpu(&error)) {
            m_runtimeWarnings.push_back("Dynamic GI upload: " + error);
            m_dynamicGi.Reset(); return;
        }
        m_dynamicGiConfigured = true;
    }
    std::vector<engine::DynamicGiLight> lights;
    m_registry.view<engine::ecs::Transform, engine::ecs::Light>().each(
        [&](Entity, engine::ecs::Transform& transform, engine::ecs::Light& light) {
            if (!light.affectDynamicGi || light.type == engine::ecs::Light::Type::Directional
                || light.type == engine::ecs::Light::Type::Area) return;
            engine::DynamicGiLight dynamic;
            dynamic.type = light.type == engine::ecs::Light::Type::Spot
                ? engine::DynamicGiLight::Type::Spot : engine::DynamicGiLight::Type::Point;
            dynamic.position = transform.position;
            dynamic.direction = glm::normalize(light.direction);
            dynamic.radiance = glm::max(light.color * light.intensity, glm::vec3(0.0f));
            dynamic.range = std::max(light.range, 0.01f);
            dynamic.innerCos = std::cos(glm::radians(light.innerAngle));
            dynamic.outerCos = std::cos(glm::radians(light.outerAngle));
            lights.push_back(dynamic);
        });
    m_dynamicGi.Update(camera.Position(), sky, lights, ++m_dynamicGiFrame);
}

void RuntimePlayerApp::RestartScene() {
    m_underwaterBlend = 0.0f;
    m_underwaterAudio = false;
    m_underwaterVisuals.blend = 0.0f;
    m_runtimeAudio.ApplySnapshot(engine::AudioSnapshotPreset::Default, 0.1f);
    engine::ShutdownScripts(m_registry);
    // OnDestroy handlers may queue requests; they belong to the scene being
    // discarded and must not leak into the freshly loaded scene/world.
    (void)engine::ConsumeScriptSceneLoadRequest();
    (void)engine::ConsumeScriptLevelStreamRequests();
    m_runtimeAudio.Stop();
    m_cameraShake.Clear();
    m_cameraSequence.Stop();
    m_cameraDirector.SetStopped();
    m_cameraDirector.ClearEvents();
    m_cameraDirector.TakeCommands();
    m_activeCinematicCues.clear();
    m_animationEvents.clear();
    m_registry = engine::ecs::Registry{};
    m_simReady = false;
    m_hudLoaded = false;
    m_paused = false;
    m_loadError.clear();
    LoadScene();
    ConfigurePhysics();
    SetPlayCursor(HasPlayer() && !m_paused);
}

void RuntimePlayerApp::SaveToSlot(int slot, const std::string& name) {
    engine::SaveGame save = engine::CaptureSaveGame(
        m_registry, m_scenePath,
        name.empty() ? ("Slot " + std::to_string(slot)) : name, 0.0f);
    if (save.SaveToFile(engine::SaveSlotPath(slot))) {
        m_saveToastText = "Saved: " + save.displayName;
        m_saveToastTime = 2.5f;
    }
}

bool RuntimePlayerApp::LoadFromSlot(int slot) {
    engine::SaveGame save;
    if (!engine::SaveGame::LoadFromFile(engine::SaveSlotPath(slot), save)) return false;
    if (!save.scenePath.empty()) {
        std::filesystem::path scene(save.scenePath);
        if (!scene.is_absolute()) scene = std::filesystem::path(m_sceneDir) / scene;
        m_scenePath = scene.lexically_normal().string();
        RestartScene();
    }
    engine::ApplySaveGame(m_registry, save);
    return true;
}

void RuntimePlayerApp::CloseLoadMenu() {
    if (!m_loadMenuOpen) return;
    m_loadMenuOpen = false;
    m_paused = m_pausedBeforeMenu;
    if (!m_paused) engine::GameMode::Instance().Resume();
    if (HasPlayer()) SetPlayCursor(!m_paused);
}

bool RuntimePlayerApp::UpdateSaveLoadMenu() {
    engine::Window& w = GetWindow();

    // F5 quicksave to slot 0 (edge-detected; disabled while the menu is open).
    const bool quickSave = w.IsKeyPressed(GLFW_KEY_F5);
    if (quickSave && !m_quickSavePrev && !m_loadMenuOpen) SaveToSlot(0, "Quicksave");
    m_quickSavePrev = quickSave;

    // F9 opens / closes the load menu.
    const bool toggle = w.IsKeyPressed(GLFW_KEY_F9);
    if (toggle && !m_loadMenuKeyPrev) {
        if (m_loadMenuOpen) {
            CloseLoadMenu();
        } else {
            m_loadMenuOpen = true;
            m_loadMenuSlots = engine::ListSaveSlots(kSaveSlotCount);
            m_loadMenuSelection = 0;
            m_pausedBeforeMenu = m_paused;
            m_paused = true;
            engine::GameMode::Instance().Pause();
            if (HasPlayer()) SetPlayCursor(false);
        }
    }
    m_loadMenuKeyPrev = toggle;

    if (!m_loadMenuOpen) return false;

    const int slotCount = static_cast<int>(m_loadMenuSlots.size());
    const bool up = w.IsKeyPressed(GLFW_KEY_UP);
    if (up && !m_menuUpPrev && m_loadMenuSelection > 0) --m_loadMenuSelection;
    m_menuUpPrev = up;
    const bool down = w.IsKeyPressed(GLFW_KEY_DOWN);
    if (down && !m_menuDownPrev && m_loadMenuSelection + 1 < slotCount) ++m_loadMenuSelection;
    m_menuDownPrev = down;

    const bool inRange = m_loadMenuSelection >= 0 && m_loadMenuSelection < slotCount;
    const bool del = w.IsKeyPressed(GLFW_KEY_DELETE);
    if (del && !m_menuDeletePrev && inRange && m_loadMenuSlots[m_loadMenuSelection].exists) {
        engine::DeleteSaveSlot(m_loadMenuSlots[m_loadMenuSelection].slot);
        m_loadMenuSlots = engine::ListSaveSlots(kSaveSlotCount);
    }
    m_menuDeletePrev = del;

    const bool enter = w.IsKeyPressed(GLFW_KEY_ENTER);
    bool loaded = false;
    if (enter && !m_menuEnterPrev && inRange && m_loadMenuSlots[m_loadMenuSelection].exists) {
        const int slot = m_loadMenuSlots[m_loadMenuSelection].slot;
        CloseLoadMenu();
        loaded = LoadFromSlot(slot);
    }
    m_menuEnterPrev = enter;
    return loaded;
}

void RuntimePlayerApp::DrawSaveLoadMenu(int screenW, int screenH) {
    if (!m_text) return;
    if (!m_loadMenuOpen) {
        if (m_saveToastTime > 0.0f)
            m_text->Text(m_saveToastText, 24.0f, screenH - 40.0f, 1.0f,
                         glm::vec3(0.7f, 1.0f, 0.7f));
        return;
    }
    const float x = screenW * 0.5f - 200.0f;
    float y = screenH * 0.26f;
    m_text->Text("LOAD GAME", x, y, 1.8f, glm::vec3(1.0f, 0.9f, 0.5f));
    y += 48.0f;
    for (int i = 0; i < static_cast<int>(m_loadMenuSlots.size()); ++i) {
        const engine::SaveSlotInfo& info = m_loadMenuSlots[i];
        const bool selected = (i == m_loadMenuSelection);
        std::string line = (selected ? "> " : "   ");
        line += "Slot " + std::to_string(info.slot) + ":  ";
        line += info.exists
            ? (info.displayName.empty() ? std::string("(saved)") : info.displayName)
            : std::string("(empty)");
        const glm::vec3 color = selected ? glm::vec3(1.0f, 1.0f, 0.6f)
                              : info.exists ? glm::vec3(0.85f) : glm::vec3(0.45f);
        m_text->Text(line, x, y, 1.1f, color);
        y += 30.0f;
    }
    y += 20.0f;
    m_text->Text("Up/Down: select    Enter: load    Del: erase    F9/Esc: close",
                 x, y, 0.75f, glm::vec3(0.7f));
}

void RuntimePlayerApp::DrawHudOverlay() {
    if (!m_hudLoaded || !m_text || m_hud.widgets.empty()) return;
    engine::Window& w = GetWindow();

    engine::HudContext ctx;
    if (m_hudHealthEntity != engine::ecs::kNull && m_registry.Has<engine::Health>(m_hudHealthEntity)) {
        const engine::Health& h = m_registry.Get<engine::Health>(m_hudHealthEntity);
        ctx.hasHealth = true;
        ctx.health = h.hp;
        ctx.maxHealth = h.maxHp;
        ctx.healthFraction = h.maxHp > 0.0f ? h.hp / h.maxHp : 0.0f;
        ctx.alive = h.alive;
        ctx.floats["hp"] = h.hp;
        ctx.floats["maxhp"] = h.maxHp;
    }
    ctx.floats["fps"] = m_fps;

    // GameMode values HUD widgets can bind to: NamedFloat "score"/"time", or
    // NamedString "score"/"gamestate"/"gamemessage".
    const engine::GameMode& gm = engine::GameMode::Instance();
    ctx.floats["score"] = static_cast<float>(gm.Score());
    ctx.floats["time"]  = gm.Elapsed();
    ctx.strings["score"]       = std::to_string(gm.Score());
    ctx.strings["gamestate"]   = engine::GameMode::StateName(gm.State());
    ctx.strings["gamemessage"] = gm.Message();

    ctx.textureLookup = [this](const std::string& r) { return HudTextureId(r); };

    // Cursor + click edge for buttons. With a player, the cursor is captured for
    // mouse-look and only freed while paused; in free-fly, it's free unless RMB-look.
    GLFWwindow* win = w.Native();
    const bool rmb = win && glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    ctx.cursorActive = HasPlayer() ? m_paused : !rmb;
    ctx.cursorX = w.MouseX();
    ctx.cursorY = w.MouseY();
    const bool down = win && glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    ctx.mousePressed = down && !m_hudMousePrev && ctx.cursorActive;
    m_hudMousePrev = down;

    const engine::HudDrawResult r = engine::DrawHud(*m_text, m_hud, ctx, w.Width(), w.Height());
    switch (r.clickedAction) {
        case engine::HudButtonAction::ExitPlay:    w.SetShouldClose(true); break;
        case engine::HudButtonAction::RestartPlay:
            if (m_scene.gameMode.allowRestart) RestartScene();
            break;
        default: break;
    }
}

void RuntimePlayerApp::SetupPlayer() {
    m_playerController.reset();
    m_playerEntity = engine::ecs::kNull;

    // Prefer an entity carrying authored player-controller settings (runtime v48+);
    // fall back to the "PlayerStart" name convention with default tunables so scenes
    // exported before v48 still spawn a player.
    const engine::RuntimeSceneLoader::EntityDesc* desc = nullptr;
    std::string playerName = "PlayerStart";
    if (!m_scene.gameMode.playerObjectName.empty()) {
        for (const auto& d : m_scene.entities) {
            if (d.playerControllerEnabled
                && d.name == m_scene.gameMode.playerObjectName) {
                desc = &d;
                playerName = d.name;
                break;
            }
        }
        // Keep renamed/removed player selections recoverable instead of leaving
        // the game without a controller.
        if (!desc) {
            for (const auto& d : m_scene.entities) {
                if (d.playerControllerEnabled) {
                    desc = &d;
                    playerName = d.name;
                    break;
                }
            }
        }
    } else {
        for (const auto& d : m_scene.entities) {
            if (d.playerControllerEnabled) {
                desc = &d;
                playerName = d.name;
                break;
            }
        }
    }

    Entity found = engine::ecs::kNull;
    m_registry.view<Transform, engine::ecs::RuntimeName>().each(
        [&](Entity e, Transform&, engine::ecs::RuntimeName& n) {
            if (found == engine::ecs::kNull && n.value == playerName) found = e;
        });
    if (found == engine::ecs::kNull) return;

    engine::PlayerController controller;
    if (desc) {
        const engine::RuntimeSceneLoader::PlayerControllerDesc& pc = desc->playerController;
        const int authoredCameraMode =
            pc.firstPerson ? 1 : std::clamp(pc.cameraMode, 0, 3);
        const int cameraMode = m_scene.gameMode.cameraOverride
            ? std::clamp(m_scene.gameMode.cameraMode, 0, 3)
            : authoredCameraMode;
        controller.view = cameraMode == 1
            ? engine::PlayerController::View::FirstPerson
            : cameraMode == 2
                ? engine::PlayerController::View::Isometric
                : cameraMode == 3
                    ? engine::PlayerController::View::Platformer
                    : engine::PlayerController::View::ThirdPerson;
        controller.walkSpeed          = pc.walkSpeed;
        controller.runSpeed           = pc.runSpeed;
        controller.jumpSpeed          = pc.jumpSpeed;
        controller.crouchSpeed        = pc.crouchSpeed;
        controller.crouchedHeight     = pc.crouchedHeight;
        controller.swimSpeed          = pc.swimSpeed;
        controller.swimVerticalSpeed  = pc.swimVerticalSpeed;
        controller.lookSensitivity    = pc.lookSensitivity;
        controller.eyeHeight          = pc.eyeHeight;
        controller.camDistance        = pc.cameraDistance;
        controller.camTargetHeight    = pc.cameraTargetHeight;
        controller.SetIsometricView(
            pc.isometricYaw, pc.isometricPitch, pc.isometricDistance);
        controller.platformerDistance = pc.isometricDistance;   // side offset for platformer
        controller.platformerYaw      = pc.platformerYaw;       // authored side-view axis
        controller.camCollision       = pc.cameraCollision;
        controller.camProbeRadius     = pc.cameraProbeRadius;
        controller.camCollisionPadding = pc.cameraCollisionPadding;
        controller.camReturnSpeed     = pc.cameraReturnSpeed;
        controller.shoulderCamera     = pc.shoulderCamera;
        controller.shoulderOffset     = pc.shoulderOffset;
        controller.shoulderSwitchSpeed = pc.shoulderSwitchSpeed;
        controller.rightShoulder      = pc.rightShoulder;
        controller.lockOnEnabled      = pc.lockOnEnabled;
        controller.lockOnRange        = pc.lockOnRange;
        controller.lockOnViewAngle    = pc.lockOnViewAngle;
        controller.lockOnTargetHeight = pc.lockOnTargetHeight;
        controller.lockOnTrackingSpeed = pc.lockOnTrackingSpeed;
        controller.facingMode = pc.facingMode == 1
            ? engine::PlayerController::FacingMode::MovementDirection
            : engine::PlayerController::FacingMode::CameraRelative;
        controller.turnSpeed = pc.turnSpeed;
        controller.body.stepHeight    = pc.stepHeight;
        controller.body.SetMaxSlopeDegrees(pc.maxSlopeDegrees);
        controller.SetCapsule(pc.capsuleRadius, pc.capsuleHeight);
    } else {
        controller.SetCapsule(0.4f, 1.8f);
    }
    if (m_scene.gameMode.cameraOverride) {
        const int cameraMode = std::clamp(m_scene.gameMode.cameraMode, 0, 3);
        controller.view = cameraMode == 1
            ? engine::PlayerController::View::FirstPerson
            : cameraMode == 2
                ? engine::PlayerController::View::Isometric
                : cameraMode == 3
                    ? engine::PlayerController::View::Platformer
                    : engine::PlayerController::View::ThirdPerson;
    }

    if (const Transform* t = m_registry.TryGet<Transform>(found)) {
        controller.SetPosition(t->position);
    }
    // The controller owns movement, while this trigger-only proxy lets coins,
    // camera zones, and scripts receive overlap events without pushing the player.
    engine::ecs::Collider playerProxy = engine::ecs::Collider::MakeCapsuleFromHeight(
        controller.body.radius, controller.body.height);
    playerProxy.isTrigger = true;
    playerProxy.layer = engine::ecs::CollisionLayer::Player;
    playerProxy.mask = engine::ecs::CollisionLayer::All;
    m_registry.Add<engine::ecs::Collider>(found, playerProxy);
    if (m_registry.Has<engine::ecs::RigidBody>(found)) m_registry.Remove<engine::ecs::RigidBody>(found);

    m_playerEntity = found;
    m_playerController = controller;
}

void RuntimePlayerApp::BuildTerrains() {
    m_terrains.clear();
    m_terrains.reserve(m_scene.terrains.size());
    for (const RuntimeSceneLoader::Scene::TerrainDesc& desc : m_scene.terrains) {
        const Entity entity = FindNamedEntity(desc.entityName);
        Transform* transform = m_registry.TryGet<Transform>(entity);
        if (!transform) continue;
        m_terrains.emplace_back();
        RuntimeTerrain& runtime = m_terrains.back();
        runtime.entity = entity;
        const bool sculpted =
            desc.heights.size() == static_cast<std::size_t>(desc.resolution * desc.resolution);
        if (sculpted) {
            engine::Heightmap heightmap;
            heightmap.res = desc.resolution;
            heightmap.size = desc.size;
            heightmap.maxHeight = desc.maxHeight;
            heightmap.h = desc.heights;
            runtime.terrain.SetHeightmap(heightmap);
        } else {
            runtime.terrain.Generate(
                desc.resolution, desc.size, glm::vec3(0.0f), desc.maxHeight,
                static_cast<unsigned>(desc.seed), desc.octaves, desc.frequency);
        }
        if (desc.paint.size() ==
            static_cast<std::size_t>(desc.resolution * desc.resolution))
            runtime.terrain.SetPaint(desc.paint);

        engine::TerrainLayerSurface layerSurfaces[6];
        engine::TerrainLayerTexture layerTextures[6];
        engine::DefaultTerrainLayerSurfaces(layerSurfaces);
        for (int layer = 1; layer <= 5; ++layer) {
            const std::string& path = desc.layerMaterials[static_cast<std::size_t>(layer - 1)];
            if (path.empty()) continue;
            std::string materialError;
            if (const engine::RuntimeMaterialAsset* loaded =
                    m_assets.LoadMaterial(path, &materialError)) {
                layerSurfaces[layer].albedo = loaded->material.albedo;
                layerSurfaces[layer].ao = loaded->material.ao;
                layerSurfaces[layer].roughness = loaded->material.roughness;
                layerSurfaces[layer].metallic = loaded->material.metallic;
                layerTextures[layer].tiling = glm::max(
                    loaded->material.uvScale * 8.0f, glm::vec2(0.001f));
                auto resolveTexture = [this](const std::string& texturePath) {
                    std::filesystem::path file(texturePath);
                    if (file.is_absolute() || std::filesystem::exists(file)) return file;
                    file = std::filesystem::path(m_sceneDir).parent_path() / texturePath;
                    if (std::filesystem::exists(file)) return file;
                    return std::filesystem::path(texturePath);
                };
                auto readPixels = [&resolveTexture](
                    const std::string& texturePath, std::vector<std::uint8_t>* pixels,
                    int* width, int* height) {
                    if (texturePath.empty()) return;
                    const std::filesystem::path file = resolveTexture(texturePath);
                    std::string extension = file.extension().string();
                    std::transform(extension.begin(), extension.end(), extension.begin(),
                        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    if (extension == ".3dgtex") {
                        engine::TextureAssetData texture;
                        std::string ignored;
                        if (engine::LoadTextureAsset(file.string(), &texture, &ignored)) {
                            *width = static_cast<int>(texture.width);
                            *height = static_cast<int>(texture.height);
                            *pixels = std::move(texture.rgba);
                        }
                        return;
                    }
                    try {
                        engine::image::Image image;
                        if (extension == ".png") image = engine::image::DecodePNG(file.string());
                        else if (extension == ".jpg" || extension == ".jpeg")
                            image = engine::image::DecodeJPEG(file.string());
                        else return;
                        *width = image.width;
                        *height = image.height;
                        *pixels = std::move(image.rgba);
                    } catch (...) {}
                };
                readPixels(loaded->albedoMapPath, &layerTextures[layer].albedoRgba,
                           &layerTextures[layer].albedoWidth,
                           &layerTextures[layer].albedoHeight);
                readPixels(loaded->metalRoughMapPath, &layerTextures[layer].ormRgba,
                           &layerTextures[layer].ormWidth,
                           &layerTextures[layer].ormHeight);
            } else if (!materialError.empty()) {
                m_runtimeWarnings.push_back(
                    "Terrain material '" + path + "': " + materialError);
            }
        }
        runtime.terrain.SetLayerSurfaces(layerSurfaces);
        runtime.terrain.SetLayerTextures(layerTextures);

        engine::ecs::PbrMaterial material;
        material.albedo = glm::vec3(1.0f);
        material.ao = 1.0f;
        material.roughness = 1.0f;
        material.metallic = 1.0f;
        material.albedoMap = &runtime.terrain.Albedo();
        material.metalRoughMap = &runtime.terrain.SurfaceMap();
        m_registry.Add<MeshPBR>(
            entity, MeshPBR{&runtime.terrain.GetMesh(), material});
    }
}

float RuntimePlayerApp::TerrainSurfaceY(float x, float z, bool& over) const {
    over = false;
    float best = -1.0e9f;
    for (const RuntimeTerrain& runtime : m_terrains) {
        const Transform* transform = m_registry.TryGet<Transform>(runtime.entity);
        if (!transform) continue;
        const engine::Heightmap& map = runtime.terrain.Map();
        const glm::vec3 scale = glm::abs(transform->scale);
        const float sx = std::max(scale.x, 0.0001f);
        const float sy = std::max(scale.y, 0.0001f);
        const float sz = std::max(scale.z, 0.0001f);
        const float localX = (x - transform->position.x) / sx;
        const float localZ = (z - transform->position.z) / sz;
        if (localX < map.origin.x || localZ < map.origin.z ||
            localX > map.origin.x + map.size || localZ > map.origin.z + map.size)
            continue;
        best = std::max(
            best, transform->position.y + runtime.terrain.HeightAt(localX, localZ) * sy);
        over = true;
    }
    return over ? best : 0.0f;
}

void RuntimePlayerApp::BuildWaters() {
    m_waters.clear();
    m_waters.reserve(m_scene.waters.size());
    for (const RuntimeSceneLoader::Scene::WaterDesc& desc : m_scene.waters)
        m_waters.emplace_back(desc, RuntimeWaterConfig(desc));
}

float RuntimePlayerApp::WaterSurfaceY(float x, float z, bool& over) const {
    over = false;
    float highest = -std::numeric_limits<float>::max();
    for (const RuntimeWater& runtime : m_waters) {
        if (!runtime.water.ContainsXZ(x, z)) continue;
        highest = std::max(highest, runtime.water.HeightAt(x, z));
        over = true;
    }
    return over ? highest : 0.0f;
}

void RuntimePlayerApp::ApplyWaterBuoyancy(float dt) {
    if (m_waters.empty()) return;
    constexpr float density = 3.0f;
    constexpr float linearDrag = 2.5f;
    constexpr float angularDrag = 2.5f;
    const float gravity = std::max(std::abs(m_physics.gravity.y), 0.01f);
    m_registry.view<Transform, engine::ecs::RigidBody>().each(
        [&](Entity entity, Transform& transform, engine::ecs::RigidBody& body) {
            if (body.invMass <= 0.0f || body.kinematic) return;
            bool over = false;
            const float surface = WaterSurfaceY(
                transform.position.x, transform.position.z, over);
            if (!over) return;
            float radius = 0.5f, halfY = 0.5f, volume = 1.0f;
            if (const engine::ecs::Collider* collider =
                    m_registry.TryGet<engine::ecs::Collider>(entity)) {
                RuntimeColliderMetrics(*collider, transform.scale, radius, halfY, volume);
            } else {
                const glm::vec3 scale = glm::abs(transform.scale);
                halfY = std::max(scale.y * 0.5f, 0.05f);
                volume = std::max(scale.x * scale.y * scale.z, 0.0001f);
            }
            const float bottom = transform.position.y - halfY;
            if (bottom >= surface) return;
            const float submerged = glm::clamp(
                (surface - bottom) / (2.0f * halfY), 0.0f, 1.0f);
            body.AddForce(glm::vec3(0.0f, density * gravity * volume * submerged, 0.0f));
            body.velocity *= 1.0f / (1.0f + dt * linearDrag * submerged);
            body.angularVelocity *= 1.0f / (1.0f + dt * angularDrag * submerged);
            body.sleeping = false;
            body.sleepTimer = 0.0f;
        });
}

void RuntimePlayerApp::CaptureWaterSceneBuffers() {
    if (m_waters.empty()) return;
    const int width = std::max(GetWindow().Width(), 1);
    const int height = std::max(GetWindow().Height(), 1);
    if (!m_waterSceneCopy) {
        m_waterSceneCopy.emplace(width, height, GL_RGBA16F, true);
    } else if (m_waterSceneCopy->Width() != width
               || m_waterSceneCopy->Height() != height) {
        m_waterSceneCopy->Resize(width, height);
    }
    GLint previousRead = 0, previousDraw = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousRead);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousDraw);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previousDraw));
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_waterSceneCopy->FboId());
    glBlitFramebuffer(0, 0, width, height, 0, 0, width, height,
                      GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previousRead));
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(previousDraw));
    glViewport(0, 0, width, height);
}

void RuntimePlayerApp::DrawEnvironmentSky(const glm::mat4& view, const glm::mat4& projection,
                                         bool tonemap) {
    const RuntimeSceneLoader::Scene::Environment& env = m_scene.environment;
    if (env.skyMode == 1 && m_importedSky) {
        m_importedSky->Draw(view, projection, tonemap,
                            glm::radians(env.skyRotation), std::max(env.skyIntensity, 0.0f));
    } else if (m_sky) {
        m_sky->Draw(view, projection, ResolveEnvironment(env, m_sample),
                    tonemap, SkyClouds(env));
    }
}

void RuntimePlayerApp::DrawWaters(const engine::Camera& camera, float aspect) {
    if (m_waters.empty()) return;
    const auto& environment = m_scene.environment;
    const glm::vec3 sunColor = EnvironmentKeyRadiance(environment,m_sample);
    const glm::vec3 ambient = ResolveEnvironment(environment,m_sample).ambientRadiance;
    std::vector<glm::vec4> contacts;
    contacts.reserve(engine::Water::kMaxContacts);
    for (RuntimeWater& runtime : m_waters) {
        contacts.clear();
        if (!runtime.desc.splineName.empty()) {
            const Entity splineEntity = FindNamedEntity(runtime.desc.splineName);
            if (const engine::ecs::SplineComponent* spline =
                    m_registry.TryGet<engine::ecs::SplineComponent>(splineEntity);
                spline && spline->points.size() >= 2) {
                engine::WaterConfig updated = runtime.water.Config();
                updated.splinePoints = spline->points;
                updated.splinePointRotations = spline->rotations;
                updated.splineClosed = spline->closed;
                glm::vec3 boundsMin = spline->points.front();
                glm::vec3 boundsMax = boundsMin;
                for (const glm::vec3& point : spline->points) {
                    boundsMin = glm::min(boundsMin, point);
                    boundsMax = glm::max(boundsMax, point);
                }
                updated.center = (boundsMin + boundsMax) * 0.5f;
                updated.size = std::max(boundsMax.x - boundsMin.x,
                    boundsMax.z - boundsMin.z) + updated.riverWidth;
                runtime.water.SetConfig(updated);
            }
        }
        const engine::WaterConfig& config = runtime.water.Config();
        m_registry.view<Transform, engine::ecs::Collider>().each(
            [&](Entity, Transform& transform, engine::ecs::Collider& collider) {
                if (collider.isTrigger
                    || contacts.size() >= engine::Water::kMaxContacts) return;
                float radius = 0.5f, halfY = 0.5f, volume = 1.0f;
                RuntimeColliderMetrics(collider, transform.scale, radius, halfY, volume);
                if (!runtime.water.ContainsXZ(
                        transform.position.x, transform.position.z, radius)) return;
                const float band = halfY + 0.6f;
                const float dy = std::abs(transform.position.y - config.center.y);
                if (dy > band) return;
                contacts.emplace_back(
                    transform.position.x, transform.position.z, radius,
                    glm::clamp(1.0f - dy / band, 0.0f, 1.0f));
            });
        runtime.water.Draw(
            camera, aspect, m_sample.keyLightDirection, sunColor, ambient,
            contacts.empty() ? nullptr : contacts.data(),
            static_cast<int>(contacts.size()),
            m_waterSceneCopy ? m_waterSceneCopy->ColorTexture() : 0,
            m_waterSceneCopy ? m_waterSceneCopy->DepthTexture() : 0,
            GetWindow().Width(), GetWindow().Height(), m_ibl ? &*m_ibl : nullptr);
    }
}

void RuntimePlayerApp::UpdateUnderwaterState(const engine::Camera& camera, float dt) {
    const glm::vec3 position = camera.Position();
    const RuntimeWater* containing = nullptr;
    float highest = -std::numeric_limits<float>::max();
    for (const RuntimeWater& runtime : m_waters) {
        const engine::WaterConfig& config = runtime.water.Config();
        if (!runtime.water.ContainsXZ(position.x, position.z)) continue;
        const float surface = runtime.water.HeightAt(position.x, position.z);
        if (position.y < surface && surface > highest) {
            highest = surface;
            containing = &runtime;
        }
    }
    const float target = containing ? 1.0f : 0.0f;
    const float speed = containing
        ? containing->desc.underwaterTransitionSpeed : 3.5f;
    m_underwaterBlend += (target - m_underwaterBlend)
        * (1.0f - std::exp(-std::max(dt, 0.0f) * speed));
    if (!containing && m_underwaterBlend < 0.001f) m_underwaterBlend = 0.0f;
    if (containing) {
        m_underwaterVisuals.tint = containing->desc.underwaterTint;
        m_underwaterVisuals.fogDensity = containing->desc.underwaterFogDensity;
        m_underwaterVisuals.distortion = containing->desc.underwaterDistortion;
        m_underwaterVisuals.causticsStrength = containing->desc.causticsStrength * 0.8f;
        m_underwaterVisuals.causticsScale = containing->desc.causticsScale * 4.5f;
    }
    m_underwaterVisuals.blend = m_underwaterBlend;
    if (m_post) m_post->underwater = m_underwaterVisuals;

    const bool submerged = containing != nullptr;
    if (submerged != m_underwaterAudio) {
        m_runtimeAudio.ApplySnapshot(
            submerged ? engine::AudioSnapshotPreset::Underwater
                      : engine::AudioSnapshotPreset::Default,
            0.35f);
        m_underwaterAudio = submerged;
    }
}

void RuntimePlayerApp::BuildRuntimeLevelFeatures() {
    m_triggerActions.clear();
    m_cameraZones.clear();
    m_cameraZonesInside.clear();
    m_activeCameraZone = engine::ecs::kNull;
    m_zoneCameraPose.reset();
    m_zoneCameraBlend.Cancel();
    m_physics.ClearJoints();

    for (const auto& desc : m_scene.triggerActions) {
        const Entity trigger = FindNamedEntity(desc.triggerName);
        if (trigger == engine::ecs::kNull) continue;
        RuntimeTriggerAction action;
        action.target = FindNamedEntity(desc.targetName);
        action.enterMover = desc.enterMover;
        action.enterRotator = desc.enterRotator;
        action.exitMover = desc.exitMover;
        action.exitRotator = desc.exitRotator;
        action.cameraSequence = desc.cameraSequence;
        action.enterCamera = desc.enterCamera;
        action.exitCamera = desc.exitCamera;
        action.cameraLockInput = desc.cameraLockInput;
        action.cameraSkippable = desc.cameraSkippable;
        if (const engine::ecs::Mover* mover =
            m_registry.TryGet<engine::ecs::Mover>(action.target))
            action.mover = *mover;
        else if (const auto source = std::find_if(
            m_scene.entities.begin(), m_scene.entities.end(),
            [&](const auto& entity) { return entity.name == desc.targetName; });
            source != m_scene.entities.end())
            action.mover = source->mover;
        if (const engine::ecs::Rotator* rotator =
            m_registry.TryGet<engine::ecs::Rotator>(action.target))
            action.rotator = *rotator;
        else if (const auto source = std::find_if(
            m_scene.entities.begin(), m_scene.entities.end(),
            [&](const auto& entity) { return entity.name == desc.targetName; });
            source != m_scene.entities.end())
            action.rotator = source->rotator;
        m_triggerActions.emplace(trigger, std::move(action));
    }
    for (const auto& desc : m_scene.cameraZones) {
        const Entity trigger = FindNamedEntity(desc.triggerName);
        if (trigger == engine::ecs::kNull) continue;
        m_cameraZones.emplace(
            trigger, RuntimeCameraZone{
                desc.presetName, desc.restoreOnExit, desc.priority, desc.returnBlend});
    }
    // Bind helpers: map an authored world pivot / hinge axis into a body's local frame at load
    // (rotation-only), matching the editor's play-start joint build and RagdollSystem.
    const auto localPointOf = [this](Entity e, const glm::vec3& worldPoint) {
        if (const auto* t = m_registry.TryGet<engine::ecs::Transform>(e))
            return glm::inverse(t->rotation) * (worldPoint - t->position);
        return worldPoint;
    };
    const auto localAxisOf = [this](Entity e, const glm::vec3& worldAxis) {
        if (const auto* t = m_registry.TryGet<engine::ecs::Transform>(e))
            return glm::inverse(t->rotation) * worldAxis;
        return worldAxis;
    };
    const auto applyMotorAndBreak = [this](const auto& j) {
        if (!m_physics.HasJoints()) return;
        engine::Joint& created = m_physics.LastJoint();
        created.breakImpulse = std::max(j.breakImpulse, 0.0f);
        if (j.type == 3 && j.motorEnabled) {   // Hinge
            created.motorEnabled = true;
            created.motorTargetVelocity = glm::radians(j.motorTargetVelocity);
            created.motorMaxTorque = std::max(j.motorMaxTorque, 0.0f);
        }
    };

    for (const auto& joint : m_scene.physicsJoints) {
        const Entity a = FindNamedEntity(joint.objectA);
        const Entity b = FindNamedEntity(joint.objectB);
        if (a == engine::ecs::kNull || (!joint.worldAnchor && b == engine::ecs::kNull))
            continue;
        const glm::vec3 worldAxis = (glm::dot(joint.axis, joint.axis) > 1.0e-6f)
            ? glm::normalize(joint.axis) : glm::vec3(0.0f, 1.0f, 0.0f);
        if (joint.type == 1) {           // Spring
            if (joint.worldAnchor)
                m_physics.AddSpringJointToWorld(
                    a, joint.anchor, joint.restLength, joint.stiffness, joint.damping);
            else
                m_physics.AddSpringJoint(
                    a, b, joint.restLength, joint.stiffness, joint.damping);
        } else if (joint.type == 2) {    // Ball
            if (joint.worldAnchor) {
                m_physics.AddBallJointToWorld(a, joint.anchor, localPointOf(a, joint.anchor));
            } else {
                m_physics.AddBallJoint(a, b, localPointOf(a, joint.anchor), localPointOf(b, joint.anchor),
                                       joint.collideConnected, joint.angularLimit, glm::radians(joint.maxAngle));
            }
            applyMotorAndBreak(joint);
        } else if (joint.type == 3) {    // Hinge
            if (joint.worldAnchor) {
                m_physics.AddHingeJointToWorld(a, joint.anchor, localPointOf(a, joint.anchor),
                                               localAxisOf(a, worldAxis), worldAxis);
            } else {
                m_physics.AddHingeJoint(a, b, localPointOf(a, joint.anchor), localPointOf(b, joint.anchor),
                                        localAxisOf(a, worldAxis), localAxisOf(b, worldAxis),
                                        joint.collideConnected, joint.angularLimit,
                                        glm::radians(joint.minAngle), glm::radians(joint.maxAngle));
            }
            applyMotorAndBreak(joint);
        } else if (joint.worldAnchor) {  // Distance
            m_physics.AddDistanceJointToWorld(
                a, joint.anchor, joint.restLength, joint.rope);
        } else {
            m_physics.AddDistanceJoint(a, b, joint.restLength, joint.rope);
        }
    }
}

void RuntimePlayerApp::ValidateRuntimeScene() {
    auto warn = [this](std::string message) {
        m_runtimeWarnings.push_back(std::move(message));
    };
    for (const auto& agent : m_scene.navAgents) {
        if (FindNamedEntity(agent.entityName) == engine::ecs::kNull)
            warn("AI agent entity not found: " + agent.entityName);
        if (!agent.targetName.empty() &&
            FindNamedEntity(agent.targetName) == engine::ecs::kNull)
            warn("AI target not found: " + agent.entityName + " -> " + agent.targetName);
        if (!agent.brainAsset.empty()) {
            std::filesystem::path path(agent.brainAsset);
            if (!path.is_absolute()) path = std::filesystem::path(m_sceneDir) / path;
            if (!std::filesystem::exists(path))
                warn("Behavior tree not found: " + path.string());
        }
    }
    for (const auto& action : m_scene.triggerActions) {
        if (FindNamedEntity(action.triggerName) == engine::ecs::kNull)
            warn("Trigger entity not found: " + action.triggerName);
        if (!action.targetName.empty() &&
            FindNamedEntity(action.targetName) == engine::ecs::kNull)
            warn("Trigger target not found: " + action.targetName);
    }
    for (const auto& entity : m_scene.entities) {
        if (entity.scriptEnabled && !entity.scriptClassName.empty()
            && !engine::IsLuaScriptPath(entity.scriptPath) &&
            !engine::ScriptRegistry::Instance().Has(entity.scriptClassName))
            warn("Script class is not registered: " + entity.scriptClassName +
                 " (entity " + entity.name + ")");
        for (const auto& script : entity.additionalScripts) {
            if (script.enabled && !script.className.empty()
                && !engine::IsLuaScriptPath(script.path)
                && !engine::ScriptRegistry::Instance().Has(script.className)) {
                warn("Script class is not registered: " + script.className
                     + " (entity " + entity.name + ")");
            }
        }
    }
    for (const auto& zone : m_scene.cameraZones) {
        const bool presetExists = std::any_of(
            m_scene.cameraPresets.begin(), m_scene.cameraPresets.end(),
            [&](const auto& camera) { return camera.name == zone.presetName; });
        if (!presetExists)
            warn("Camera zone preset not found: " + zone.presetName);
    }
    if (!m_scene.navAgents.empty() && m_scene.navBounds.empty())
        warn("AI has no authored Nav Mesh Bounds Volume; generated fallback bounds are in use");
    for (const std::string& warning : m_runtimeWarnings)
        std::fprintf(stderr, "[Runtime warning] %s\n", warning.c_str());
}

void RuntimePlayerApp::BuildAI() {
    m_agents.clear();
    m_behaviorGraphCache.clear();

    auto resolveAssetPath = [this](const std::string& path) {
        if (path.empty()) return std::string{};
        const std::filesystem::path authored(path);
        if (authored.is_absolute() || std::filesystem::exists(authored))
            return authored.lexically_normal().string();
        return (std::filesystem::path(m_sceneDir) / authored).lexically_normal().string();
    };
    auto resolveSubtree = [this, resolveAssetPath](
        const std::string& path) -> const engine::ai::BehaviorGraph* {
        const std::string resolved = resolveAssetPath(path);
        if (resolved.empty()) return nullptr;
        if (const auto found = m_behaviorGraphCache.find(resolved);
            found != m_behaviorGraphCache.end())
            return &found->second;
        engine::ai::BehaviorGraph graph;
        if (!engine::ai::LoadBehaviorGraph(resolved, graph) || !graph.IsValid())
            return nullptr;
        return &m_behaviorGraphCache.emplace(resolved, std::move(graph)).first->second;
    };

    for (const RuntimeSceneLoader::Scene::NavAgentDesc& desc : m_scene.navAgents) {
        const Entity entity = FindNamedEntity(desc.entityName);
        if (entity == engine::ecs::kNull) continue;

        RuntimeAgent runtime;
        runtime.entity = entity;
        runtime.targetEntity = FindNamedEntity(desc.targetName);
        runtime.name = desc.entityName;
        runtime.team = desc.team;
        runtime.autoTarget = desc.autoTarget;
        runtime.movement.mode = desc.movementMode;
        runtime.movement.gravity = desc.movementGravity;
        runtime.movement.maxFallSpeed = desc.movementMaxFallSpeed;
        runtime.movement.groundProbeDistance = desc.movementGroundProbe;
        runtime.movement.stepHeight = desc.movementStepHeight;
        runtime.movement.maxSlopeDegrees = desc.movementMaxSlope;
        runtime.brain.agent.maxSpeed = desc.speed;
        runtime.brain.agent.maxForce = desc.maxForce;
        runtime.brain.reachRadius = desc.reachRadius;
        runtime.brain.repathInterval = desc.repathInterval;
        runtime.brain.vision.range = desc.visionRange;
        runtime.brain.vision.halfAngleDegrees = desc.visionHalfAngle;
        runtime.brain.hearingRange = desc.hearingRange;
        runtime.hearingRange = desc.hearingRange;
        runtime.brain.patrol = desc.patrolPoints;

        glm::vec3 position(0.0f);
        if (const Transform* transform = m_registry.TryGet<Transform>(entity))
            position = transform->position;
        runtime.brain.SetPosition(position);

        const std::string graphPath = resolveAssetPath(desc.brainAsset);
        engine::ai::BehaviorGraph graph;
        if (!graphPath.empty() &&
            engine::ai::LoadBehaviorGraph(graphPath, graph) && graph.IsValid()) {
            runtime.useGraph = true;
            runtime.context.agent.maxSpeed = desc.speed;
            runtime.context.agent.maxForce = desc.maxForce;
            runtime.context.agent.position = position;
            runtime.context.reachRadius = desc.reachRadius;
            runtime.context.repathInterval = desc.repathInterval;
            runtime.context.patrol = desc.patrolPoints;
            engine::ai::SeedBlackboard(graph.blackboard, runtime.context.blackboard);
            runtime.tree = engine::ai::BuildBehaviorTree(graph, resolveSubtree);
        }
        m_agents.push_back(std::move(runtime));
    }
    BakeNavigation();
}

void RuntimePlayerApp::BakeNavigation() {
    m_navMesh = engine::ai::NavMesh{};
    if (m_agents.empty()) return;

    std::vector<engine::ai::NavObstacle> obstacles;
    glm::vec2 boundsMin(1.0e9f), boundsMax(-1.0e9f);
    float groundY = 0.0f;
    bool haveBounds = false;

    for (const RuntimeSceneLoader::Scene::NavBounds& bounds : m_scene.navBounds) {
        const glm::mat4 model = glm::translate(glm::mat4(1.0f), bounds.position) *
                                glm::mat4_cast(bounds.rotation) *
                                glm::scale(glm::mat4(1.0f), bounds.scale);
        for (int corner = 0; corner < 8; ++corner) {
            const glm::vec3 local((corner & 1) ? 0.5f : -0.5f,
                                  (corner & 4) ? 0.5f : -0.5f,
                                  (corner & 2) ? 0.5f : -0.5f);
            const glm::vec3 world = glm::vec3(model * glm::vec4(local, 1.0f));
            boundsMin = glm::min(boundsMin, glm::vec2(world.x, world.z));
            boundsMax = glm::max(boundsMax, glm::vec2(world.x, world.z));
            groundY = haveBounds ? std::min(groundY, world.y) : world.y;
            haveBounds = true;
        }
    }
    const bool authoredBounds = haveBounds;
    auto extend = [&](const glm::vec2& low, const glm::vec2& high) {
        if (authoredBounds) return;
        boundsMin = glm::min(boundsMin, low);
        boundsMax = glm::max(boundsMax, high);
        haveBounds = true;
    };

    m_registry.view<Transform, engine::ecs::Collider>().each(
        [&](Entity entity, Transform& transform, engine::ecs::Collider& collider) {
            const engine::ecs::RigidBody* body =
                m_registry.TryGet<engine::ecs::RigidBody>(entity);
            if ((body && body->invMass > 0.0f) || collider.isTrigger) return;
            if (collider.shape == engine::ecs::ColliderShape::Plane) {
                if (!authoredBounds) groundY = transform.position.y;
                return;
            }
            glm::vec2 half(collider.radius);
            switch (collider.shape) {
            case engine::ecs::ColliderShape::Box:
            case engine::ecs::ColliderShape::Pyramid:
            case engine::ecs::ColliderShape::Staircase:
                half = glm::vec2(collider.halfExtents.x, collider.halfExtents.z);
                break;
            case engine::ecs::ColliderShape::Torus:
                half = glm::vec2(collider.majorRadius + collider.minorRadius);
                break;
            default:
                break;
            }
            const glm::vec2 center(transform.position.x, transform.position.z);
            extend(center - half, center + half);
            engine::ai::NavObstacle obstacle;
            obstacle.center = glm::vec3(center.x, groundY, center.y);
            obstacle.halfExtents = glm::vec3(half.x, 0.5f, half.y);
            obstacles.push_back(obstacle);
        });

    for (const RuntimeAgent& agent : m_agents) {
        if (const Transform* transform = m_registry.TryGet<Transform>(agent.entity)) {
            const glm::vec2 p(transform->position.x, transform->position.z);
            extend(p, p);
        }
        const auto& patrol = agent.useGraph ? agent.context.patrol : agent.brain.patrol;
        for (const glm::vec3& point : patrol) {
            const glm::vec2 p(point.x, point.z);
            extend(p, p);
        }
    }
    if (!haveBounds) return;
    if (!authoredBounds) {
        boundsMin -= glm::vec2(5.0f);
        boundsMax += glm::vec2(5.0f);
    }
    engine::ai::NavBuildConfig config;
    config.boundsMin = glm::vec3(boundsMin.x, groundY, boundsMin.y);
    config.boundsMax = glm::vec3(boundsMax.x, groundY, boundsMax.y);
    config.cellSize = 0.5f;
    config.agentRadius = 0.4f;
    m_navMesh = engine::ai::NavMeshBuilder::Build(config, obstacles);
}

void RuntimePlayerApp::UpdateAI(float dt) {
    if (m_agents.empty()) return;

    // Hearing: age transient noises, emit a footstep noise when the player moves fast,
    // and a loud combat noise wherever an entity's HP dropped this frame.
    m_soundField.Update(dt);
    if (m_playerEntity != engine::ecs::kNull) {
        if (const Transform* pt = m_registry.TryGet<Transform>(m_playerEntity)) {
            if (m_prevPlayerPosValid && dt > 1.0e-4f) {
                const float speed = glm::length(pt->position - m_prevPlayerPos) / dt;
                if (speed > 2.0f) {
                    const float radius   = glm::clamp(speed * 2.0f, 6.0f, 18.0f);
                    const float loudness = glm::clamp(speed / 6.0f, 0.2f, 1.0f);
                    m_soundField.Emit(pt->position, radius, loudness, 0.4f);
                }
            }
            m_prevPlayerPos = pt->position;
            m_prevPlayerPosValid = true;
        }
    }
    {
        auto emitOnDamage = [&](engine::ecs::Entity entity) {
            if (entity == engine::ecs::kNull) return;
            const engine::Health* h = m_registry.TryGet<engine::Health>(entity);
            const Transform* tr = m_registry.TryGet<Transform>(entity);
            if (!h || !tr) return;
            const auto prev = m_prevHp.find(entity);
            if (prev != m_prevHp.end() && h->hp < prev->second - 0.01f)
                m_soundField.Emit(tr->position, 22.0f, 1.0f, 0.5f);
            m_prevHp[entity] = h->hp;
        };
        emitOnDamage(m_playerEntity);
        for (const RuntimeAgent& a : m_agents) emitOnDamage(a.entity);
    }

    // Snapshot potential targets once per frame so auto-targeting is a cheap arithmetic
    // scan instead of N^2 registry lookups (a TryGet<Transform>/<Health> per agent pair).
    struct TargetCandidate {
        engine::ecs::Entity entity;
        int team;
        glm::vec3 position;
        bool alive;
    };
    std::vector<TargetCandidate> targetCandidates;
    targetCandidates.reserve(m_agents.size());
    for (const RuntimeAgent& a : m_agents) {
        if (a.team == 0) continue;   // team 0 = neutral, never an auto-target
        const Transform* t = m_registry.TryGet<Transform>(a.entity);
        if (!t) continue;
        const engine::Health* h = m_registry.TryGet<engine::Health>(a.entity);
        targetCandidates.push_back({a.entity, a.team, t->position, !h || h->alive});
    }

    for (RuntimeAgent& agent : m_agents) {
        Transform* transform = m_registry.TryGet<Transform>(agent.entity);
        if (!transform) continue;
        const engine::AnimatedModel* animated =
            m_registry.TryGet<engine::AnimatedModel>(agent.entity);
        const bool movementLocked = animated && animated->BlocksMovement();
        const glm::vec3 lockedPosition = transform->position;

        if (agent.autoTarget && agent.team != 0) {
            Entity nearest = engine::ecs::kNull;
            float nearestDistanceSq = 1.0e36f;
            for (const TargetCandidate& candidate : targetCandidates) {
                if (candidate.entity == agent.entity
                    || candidate.team == agent.team || !candidate.alive) continue;
                const glm::vec3 delta = candidate.position - transform->position;
                const float distanceSq = glm::dot(delta, delta);
                if (distanceSq < nearestDistanceSq) {
                    nearestDistanceSq = distanceSq;
                    nearest = candidate.entity;
                }
            }
            agent.targetEntity = nearest;
        }

        const glm::vec3 position = agent.useGraph
            ? agent.context.agent.position : agent.brain.Position();
        glm::vec3 facing = agent.useGraph
            ? agent.context.facing : agent.brain.Facing();
        glm::vec3 targetPosition = position;
        bool seesTarget = false;
        if (const Transform* target =
            m_registry.TryGet<Transform>(agent.targetEntity)) {
            targetPosition = target->position;
            glm::vec3 forward(facing.x, 0.0f, facing.z);
            forward = glm::dot(forward, forward) > 1.0e-6f
                ? glm::normalize(forward) : glm::vec3(0.0f, 0.0f, 1.0f);
            const glm::vec3 eye =
                position + glm::vec3(0.0f, 0.6f, 0.0f) + forward * 0.6f;
            seesTarget = engine::ai::CanSee(
                eye, forward, agent.brain.vision, targetPosition,
                agent.targetEntity, m_physics, m_registry);
        }

        // Hearing: if the agent can't see its target, the loudest audible noise becomes
        // a point of interest -- graph agents via HeardNoise?/Investigate, the built-in
        // brain via its search state.
        if (agent.hearingRange > 0.0f && !seesTarget) {
            glm::vec3 noisePos;
            if (m_soundField.LoudestAudible(position, agent.hearingRange, &noisePos)) {
                agent.context.heardNoise = true;
                agent.context.heardPosition = noisePos;
                if (!agent.useGraph) agent.brain.Hear(noisePos);
            } else {
                agent.context.heardNoise = false;
            }
        } else {
            agent.context.heardNoise = false;
        }

        glm::vec3 movementTarget = targetPosition;
        if (!agent.movement.IsFlying()) movementTarget.y = position.y;
        if (agent.useGraph) {
            engine::ai::AgentContext& context = agent.context;
            context.dt = dt;
            context.targetPos = movementTarget;
            context.seesTarget = seesTarget;
            context.mesh = &m_navMesh;
            context.registry = &m_registry;
            context.self = agent.entity;
            context.targetEntity = agent.targetEntity;
            context.steer = glm::vec3(0.0f);
            agent.tree.Tick(context, dt);
            if (movementLocked) {
                context.steer = glm::vec3(0.0f);
                context.agent.velocity = glm::vec3(0.0f);
                context.agent.position = lockedPosition;
            } else {
                engine::ai::Integrate(context.agent, context.steer, dt);
                if (glm::length(context.agent.velocity) > 1.0e-3f)
                    context.facing = glm::normalize(context.agent.velocity);
            }
            transform->position = context.agent.position;
            facing = context.facing;
        } else {
            if (movementLocked) {
                agent.brain.SetPosition(lockedPosition);
                agent.brain.agent.velocity = glm::vec3(0.0f);
            } else {
                agent.brain.Update(dt, movementTarget, seesTarget, m_navMesh);
            }
            transform->position = agent.brain.Position();
            facing = agent.brain.Facing();
        }
        const glm::vec3 requestedPosition = transform->position;
        bool overTerrain = false;
        const float terrainY = TerrainSurfaceY(
            requestedPosition.x, requestedPosition.z, overTerrain);
        transform->position = engine::ai::MoveAiAgent(
            m_physics, m_registry, agent.entity, lockedPosition,
            requestedPosition, dt, agent.movement, overTerrain, terrainY);
        glm::vec3 resolvedVelocity = dt > 1.0e-6f
            ? (transform->position - lockedPosition) / dt : glm::vec3(0.0f);
        resolvedVelocity.y = 0.0f;
        if (agent.useGraph) {
            agent.context.agent.position = transform->position;
            agent.context.agent.velocity = resolvedVelocity;
        } else {
            agent.brain.SetPosition(transform->position);
            agent.brain.agent.velocity = resolvedVelocity;
        }
        if (engine::AnimatedModel* animatedModel =
                m_registry.TryGet<engine::AnimatedModel>(agent.entity)) {
            engine::ai::UpdateAiAnimationParameters(
                *animatedModel, agent.movement, resolvedVelocity);
        }
        if (glm::dot(facing, facing) > 1.0e-6f) {
            const float yaw = std::atan2(facing.x, facing.z);
            transform->rotation =
                glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f));
        }
    }

    constexpr float separationRadius = 1.2f;
    for (RuntimeAgent& agent : m_agents) {
        Transform* transform = m_registry.TryGet<Transform>(agent.entity);
        if (!transform) continue;
        const engine::AnimatedModel* animated =
            m_registry.TryGet<engine::AnimatedModel>(agent.entity);
        if (animated && animated->BlocksMovement()) continue;
        glm::vec3 push(0.0f);
        for (const RuntimeAgent& other : m_agents) {
            if (other.entity == agent.entity) continue;
            const Transform* otherTransform =
                m_registry.TryGet<Transform>(other.entity);
            if (!otherTransform) continue;
            glm::vec3 delta = transform->position - otherTransform->position;
            delta.y = 0.0f;
            const float distance = glm::length(delta);
            if (distance > 1.0e-4f && distance < separationRadius)
                push += (delta / distance) * (separationRadius - distance);
        }
        glm::vec3 move = push * 0.5f;
        const float length = glm::length(move);
        if (length > 0.10f) move *= 0.10f / length;
        transform->position = engine::ai::MoveAgentWithCollision(
            m_physics, m_registry, agent.entity,
            transform->position, transform->position + move);
        if (agent.useGraph) agent.context.agent.position = transform->position;
        else agent.brain.SetPosition(transform->position);
    }
}

void RuntimePlayerApp::UpdateLockOn(bool inputEnabled) {
    if (!m_playerController || !m_playerController->lockOnEnabled ||
        m_playerController->view != engine::PlayerController::View::ThirdPerson) {
        if (m_playerController) m_playerController->ClearLockOnTarget();
        m_lockTarget = engine::ecs::kNull;
        m_lockTogglePrev = false;
        return;
    }
    const bool toggle = inputEnabled && GetWindow().IsKeyPressed(GLFW_KEY_T);
    if (toggle && !m_lockTogglePrev) {
        if (m_lockTarget != engine::ecs::kNull) {
            m_lockTarget = engine::ecs::kNull;
        } else {
            const glm::vec3 origin = m_playerController->Position() +
                glm::vec3(0.0f, m_playerController->camTargetHeight, 0.0f);
            const glm::vec3 forward = m_playerController->LookDirection();
            float bestScore = 1.0e18f;
            m_registry.view<engine::Health, Transform>().each(
                [&](Entity entity, engine::Health& health, Transform& target) {
                    if (entity == m_playerEntity || !health.alive || health.hp <= 0.0f)
                        return;
                    const glm::vec3 point = target.position +
                        glm::vec3(0.0f, m_playerController->lockOnTargetHeight, 0.0f);
                    const glm::vec3 delta = point - origin;
                    const float distance = glm::length(delta);
                    if (distance <= 0.0001f ||
                        distance > m_playerController->lockOnRange) return;
                    const float angle = glm::degrees(std::acos(std::clamp(
                        glm::dot(forward, delta / distance), -1.0f, 1.0f)));
                    if (angle > m_playerController->lockOnViewAngle) return;
                    const float score = distance + angle * 0.15f;
                    if (score < bestScore) {
                        bestScore = score;
                        m_lockTarget = entity;
                    }
                });
        }
    }
    m_lockTogglePrev = toggle;

    const Transform* target = m_registry.TryGet<Transform>(m_lockTarget);
    const engine::Health* health = m_registry.TryGet<engine::Health>(m_lockTarget);
    if (!target || !health || !health->alive || health->hp <= 0.0f) {
        m_lockTarget = engine::ecs::kNull;
        m_playerController->ClearLockOnTarget();
        return;
    }
    const glm::vec3 point = target->position +
        glm::vec3(0.0f, m_playerController->lockOnTargetHeight, 0.0f);
    if (glm::length(point - m_playerController->Position()) >
        m_playerController->lockOnRange * 1.25f) {
        m_lockTarget = engine::ecs::kNull;
        m_playerController->ClearLockOnTarget();
    } else {
        m_playerController->SetLockOnTarget(point);
    }
}

void RuntimePlayerApp::ProcessLevelPhysicsEvents() {
    auto applyComponent = [&](Entity target, int mode, bool mover,
                              const RuntimeTriggerAction& action) {
        if (target == engine::ecs::kNull || mode == 0) return;
        const bool present = mover
            ? m_registry.Has<engine::ecs::Mover>(target)
            : m_registry.Has<engine::ecs::Rotator>(target);
        const bool enable = mode == 1 || (mode == 3 && !present);
        const bool disable = mode == 2 || (mode == 3 && present);
        if (disable) {
            if (mover) m_registry.Remove<engine::ecs::Mover>(target);
            else m_registry.Remove<engine::ecs::Rotator>(target);
        } else if (enable) {
            if (mover) {
                engine::ecs::Mover value = action.mover;
                if (const Transform* transform = m_registry.TryGet<Transform>(target)) {
                    value.origin = transform->position;
                    value.initialized = true;
                }
                m_registry.Add<engine::ecs::Mover>(target, value);
            } else {
                m_registry.Add<engine::ecs::Rotator>(target, action.rotator);
            }
        }
    };
    auto applyOne = [&](Entity trigger, Entity other, engine::CollisionEvent::Phase phase) {
        if (const auto found = m_triggerActions.find(trigger);
            found != m_triggerActions.end() &&
            phase != engine::CollisionEvent::Phase::Stay) {
            const RuntimeTriggerAction& action = found->second;
            const bool exit = phase == engine::CollisionEvent::Phase::Exit;
            applyComponent(action.target, exit ? action.exitMover : action.enterMover,
                           true, action);
            applyComponent(action.target, exit ? action.exitRotator : action.enterRotator,
                           false, action);
            const int camera = exit ? action.exitCamera : action.enterCamera;
            if (camera == 1)
                m_cameraDirector.Play(
                    action.cameraSequence, action.cameraLockInput, action.cameraSkippable);
            else if (camera == 2) m_cameraDirector.Stop();
            else if (camera == 3) m_cameraDirector.Skip();
        }
        if (other == m_playerEntity) {
            if (m_cameraZones.find(trigger) != m_cameraZones.end()) {
                if (phase == engine::CollisionEvent::Phase::Enter)
                    m_cameraZonesInside.insert(trigger);
                else if (phase == engine::CollisionEvent::Phase::Exit)
                    m_cameraZonesInside.erase(trigger);
                else return;
                RefreshCameraZone();
            }
        }
    };
    for (const engine::CollisionEvent& event : m_physics.Events()) {
        applyOne(event.a, event.b, event.phase);
        applyOne(event.b, event.a, event.phase);
    }
}

void RuntimePlayerApp::RefreshCameraZone() {
    Entity best = engine::ecs::kNull;
    int priority = std::numeric_limits<int>::min();
    for (Entity trigger : m_cameraZonesInside) {
        const auto found = m_cameraZones.find(trigger);
        if (found != m_cameraZones.end() && found->second.priority > priority) {
            best = trigger;
            priority = found->second.priority;
        }
    }
    if (best == m_activeCameraZone) return;
    const auto previous = m_cameraZones.find(m_activeCameraZone);
    const bool restore = previous == m_cameraZones.end() || previous->second.restoreOnExit;
    const float returnBlend =
        previous == m_cameraZones.end() ? 0.35f : previous->second.returnBlend;
    const engine::CameraPose from = engine::CameraBlend::FromCamera(BuildCamera());
    m_activeCameraZone = best;

    if (best != engine::ecs::kNull) {
        const RuntimeCameraZone& zone = m_cameraZones.at(best);
        const auto preset = std::find_if(
            m_scene.cameraPresets.begin(), m_scene.cameraPresets.end(),
            [&](const auto& camera) { return camera.name == zone.presetName; });
        if (preset == m_scene.cameraPresets.end()) return;
        engine::CameraPose target;
        target.position = preset->position;
        target.target = preset->target;
        target.fov = preset->fov;
        target.nearPlane = preset->nearPlane;
        target.farPlane = preset->farPlane;
        m_zoneCameraPose = target;
        m_zoneCameraBlend.Start(
            from, target, preset->blendDuration,
            static_cast<engine::CameraBlend::Easing>(preset->blendEasing));
    } else if (restore) {
        m_zoneCameraPose.reset();
        m_zoneCameraBlend.Cancel();
        const engine::Camera base = BuildCamera();
        m_zoneCameraBlend.Start(
            from, engine::CameraBlend::FromCamera(base), returnBlend,
            engine::CameraBlend::Easing::SmoothStep);
    }
}

void RuntimePlayerApp::GatherPlayerInput() {
    engine::Window& w = GetWindow();
    engine::PlayerInput in;
    if (w.IsKeyPressed(GLFW_KEY_W)) in.moveForward += 1.0f;
    if (w.IsKeyPressed(GLFW_KEY_S)) in.moveForward -= 1.0f;
    if (w.IsKeyPressed(GLFW_KEY_D)) in.moveRight += 1.0f;
    if (w.IsKeyPressed(GLFW_KEY_A)) in.moveRight -= 1.0f;
    in.jump   = w.IsKeyPressed(GLFW_KEY_SPACE);
    in.sprint = w.IsKeyPressed(GLFW_KEY_LEFT_SHIFT) || w.IsKeyPressed(GLFW_KEY_RIGHT_SHIFT);
    in.crouch = w.IsKeyPressed(GLFW_KEY_LEFT_CONTROL)
        || w.IsKeyPressed(GLFW_KEY_RIGHT_CONTROL)
        || w.IsKeyPressed(GLFW_KEY_C);
    // Mouse-look only while the cursor is captured (i.e. playing, not paused).
    if (!m_paused) {
        in.lookYaw   = w.MouseDeltaX();
        in.lookPitch = w.MouseDeltaY();
    }
    m_playerInput = in;
    m_lookPending = true;
}

void RuntimePlayerApp::SetPlayCursor(bool captured) {
    GetWindow().SetCursorCaptured(captured);
}

void RuntimePlayerApp::UpdateFreeCamera(float dt) {
    engine::Window& w = GetWindow();
    GLFWwindow* win = w.Native();
    if (!win) return;

    // Mouse look while the right button is held.
    const bool rmb = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    double mx = 0.0, my = 0.0;
    glfwGetCursorPos(win, &mx, &my);
    if (rmb) {
        if (!m_looking) { m_lastMouseX = mx; m_lastMouseY = my; m_looking = true; }
        const float sens = 0.0025f;
        m_camYaw   += static_cast<float>(mx - m_lastMouseX) * sens;
        m_camPitch -= static_cast<float>(my - m_lastMouseY) * sens;
        m_camPitch = glm::clamp(m_camPitch, -1.5f, 1.5f);
        m_lastMouseX = mx; m_lastMouseY = my;
    } else {
        m_looking = false;
    }

    const glm::vec3 fwd(std::cos(m_camPitch) * std::cos(m_camYaw),
                        std::sin(m_camPitch),
                        std::cos(m_camPitch) * std::sin(m_camYaw));
    const glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0.0f, 1.0f, 0.0f)));
    const float speed = (w.IsKeyPressed(GLFW_KEY_LEFT_SHIFT) ? 18.0f : 6.0f) * dt;
    if (w.IsKeyPressed(GLFW_KEY_W)) m_camPos += fwd * speed;
    if (w.IsKeyPressed(GLFW_KEY_S)) m_camPos -= fwd * speed;
    if (w.IsKeyPressed(GLFW_KEY_D)) m_camPos += right * speed;
    if (w.IsKeyPressed(GLFW_KEY_A)) m_camPos -= right * speed;
    if (w.IsKeyPressed(GLFW_KEY_E)) m_camPos.y += speed;
    if (w.IsKeyPressed(GLFW_KEY_Q)) m_camPos.y -= speed;
}

engine::ScriptInputState RuntimePlayerApp::CaptureScriptInput(
    bool enabled, bool includeFrameEdges) {
    engine::ScriptInputState input;
    input.enabled = enabled;
    input.physicsEvents = &m_physics.Events();
    input.animationEvents = &m_animationEvents;

    engine::Window& window = GetWindow();
    for (int key = GLFW_KEY_SPACE; key <= GLFW_KEY_LAST; ++key) {
        const bool down = window.IsKeyPressed(key);
        const bool wasDown = m_scriptKeyPrev[key];
        if (includeFrameEdges) m_scriptKeyPrev[key] = down;
        if (!enabled) continue;
        if (down) input.keysDown.insert(key);
        if (includeFrameEdges && down && !wasDown) input.keysPressed.insert(key);
    }

    if (window.Native()) {
        for (int button = GLFW_MOUSE_BUTTON_1; button <= GLFW_MOUSE_BUTTON_LAST; ++button) {
            const bool down =
                glfwGetMouseButton(window.Native(), button) == GLFW_PRESS;
            const bool wasDown = m_scriptMousePrev[button];
            if (includeFrameEdges) m_scriptMousePrev[button] = down;
            if (!enabled) continue;
            if (down) input.mouseButtonsDown.insert(button);
            if (includeFrameEdges && down && !wasDown)
                input.mouseButtonsPressed.insert(button);
        }
    }

    if (enabled && includeFrameEdges) {
        input.mouseDeltaX = window.MouseDeltaX();
        input.mouseDeltaY = window.MouseDeltaY();
    }
    return input;
}

engine::ecs::Entity RuntimePlayerApp::FindNamedEntity(const std::string& name) const {
    if (name.empty()) return engine::ecs::kNull;
    engine::ecs::Entity found = engine::ecs::kNull;
    const_cast<engine::ecs::Registry&>(m_registry)
        .view<engine::ecs::RuntimeName>().each(
            [&](Entity entity, engine::ecs::RuntimeName& runtimeName) {
                if (found == engine::ecs::kNull && runtimeName.value == name)
                    found = entity;
            });
    return found;
}

engine::Camera RuntimePlayerApp::BuildCamera() const {
    engine::Camera camera(m_camPos);
    const auto primary = std::find_if(
        m_scene.cameraPresets.begin(), m_scene.cameraPresets.end(),
        [](const auto& preset) { return preset.primary && preset.useInPlay; });
    if (primary != m_scene.cameraPresets.end()) {
        camera = engine::Camera(primary->position);
        camera.LookAt(primary->target);
        camera.fov = primary->fov;
        camera.nearPlane = primary->nearPlane;
        camera.farPlane = primary->farPlane;
    } else if (HasPlayer()) {
        camera = engine::Camera(m_playerController->CameraPosition());
        camera.LookAt(m_playerController->CameraTarget());
    } else {
        const glm::vec3 forward(
            std::cos(m_camPitch) * std::cos(m_camYaw),
            std::sin(m_camPitch),
            std::cos(m_camPitch) * std::sin(m_camYaw));
        camera.LookAt(m_camPos + forward);
    }

    if (m_zoneCameraPose)
        engine::CameraBlend::Apply(*m_zoneCameraPose, camera);
    if (m_zoneCameraBlend.Active())
        engine::CameraBlend::Apply(m_zoneCameraBlend.Current(), camera);
    if (m_cameraSequence.Active() || m_cameraDirector.Playing()) {
        engine::CameraBlend::Apply(m_cameraSequence.Current(), camera);
    }
    engine::CameraShake::Apply(m_cameraShakeSample, camera);
    return camera;
}

void RuntimePlayerApp::ProcessCameraCommands() {
    for (const engine::CameraSequenceCommand& command : m_cameraDirector.TakeCommands()) {
        if (command.type == engine::CameraSequenceCommand::Type::Stop) {
            m_cameraSequence.Stop();
            m_cameraDirector.SetStopped();
            m_activeCinematicCues.clear();
            continue;
        }
        if (command.type == engine::CameraSequenceCommand::Type::Skip) {
            if (m_cameraSequence.Active()) {
                const std::string name = m_cameraDirector.ActiveName();
                m_cameraSequence.SkipToEnd();
                m_cameraDirector.NotifyFinished(name, true);
                m_activeCinematicCues.clear();
            }
            continue;
        }

        const auto sequence = std::find_if(
            m_scene.cameraSequences.begin(), m_scene.cameraSequences.end(),
            [&](const auto& candidate) { return candidate.name == command.name; });
        if (sequence == m_scene.cameraSequences.end()) continue;

        std::vector<engine::CameraSequenceShot> shots;
        shots.reserve(sequence->shots.size());
        for (const auto& source : sequence->shots) {
            const auto camera = std::find_if(
                m_scene.cameraPresets.begin(), m_scene.cameraPresets.end(),
                [&](const auto& preset) { return preset.name == source.cameraName; });
            if (camera == m_scene.cameraPresets.end()) continue;
            engine::CameraSequenceShot shot;
            shot.pose.position = camera->position;
            shot.pose.target = camera->target;
            shot.pose.fov = camera->fov;
            shot.pose.nearPlane = camera->nearPlane;
            shot.pose.farPlane = camera->farPlane;
            shot.travelDuration = source.travelDuration;
            shot.holdDuration = source.holdDuration;
            shot.easing = static_cast<engine::CameraBlend::Easing>(source.easing);
            shot.path =
                static_cast<engine::CameraSequenceShot::Path>(source.pathMode);
            shot.eventName = source.eventName;
            shots.push_back(std::move(shot));
        }
        if (shots.empty()) continue;

        m_cameraSequence.Start(
            engine::CameraBlend::FromCamera(BuildCamera()),
            std::move(shots), sequence->loop);
        m_activeCinematicCues = sequence->cues;
        std::sort(
            m_activeCinematicCues.begin(), m_activeCinematicCues.end(),
            [](const auto& a, const auto& b) { return a.time < b.time; });
        m_cameraDirector.SetPlaying(
            sequence->name, command.lockInput, command.skippable);
    }
}

void RuntimePlayerApp::ExecuteCinematicCue(
    const engine::RuntimeSceneLoader::Scene::CinematicCue& cue) {
    if (cue.type == 0) {
        m_cameraDirector.NotifyTimelineEvent(
            m_cameraDirector.ActiveName(), cue.name);
        return;
    }
    if (cue.type == 1) {
        if (!cue.assetPath.empty())
            m_audio.Play(cue.assetPath, 1.0f, cue.volume, engine::AudioBus::SFX);
        return;
    }

    const Entity target = FindNamedEntity(cue.targetObject);
    engine::AnimatedModel* animated =
        target == engine::ecs::kNull
            ? nullptr : m_registry.TryGet<engine::AnimatedModel>(target);
    if (!animated || !animated->model) return;
    const auto& animations = animated->model->Animations();
    for (std::size_t i = 0; i < animations.size(); ++i) {
        if (animations[i].name == cue.animationClip) {
            animated->PlayAction(static_cast<int>(i));
            return;
        }
    }
}

void RuntimePlayerApp::ExecuteCinematicCues(
    float previousTime, float currentTime, bool wrapped) {
    for (const auto& cue : m_activeCinematicCues) {
        const bool crossed = wrapped
            ? (cue.time > previousTime || cue.time <= currentTime)
            : ((cue.time > previousTime && cue.time <= currentTime)
               || (previousTime == 0.0f && cue.time == 0.0f && currentTime > 0.0f));
        if (crossed) ExecuteCinematicCue(cue);
    }
}

void RuntimePlayerApp::UpdateCameraSequence(float dt) {
    if (!m_cameraSequence.Active()) return;
    const bool wasActive = m_cameraSequence.Active();
    const std::string sequenceName = m_cameraDirector.ActiveName();
    const float previousTime = m_cameraSequence.Time();
    m_cameraSequence.Update(dt);
    const float currentTime = m_cameraSequence.Time();
    ExecuteCinematicCues(previousTime, currentTime, currentTime < previousTime);
    for (const std::string& eventName : m_cameraSequence.TakeEvents())
        m_cameraDirector.NotifyTimelineEvent(sequenceName, eventName);
    if (wasActive && !m_cameraSequence.Active()) {
        m_cameraDirector.NotifyFinished(sequenceName, false);
        m_activeCinematicCues.clear();
    }
}

void RuntimePlayerApp::OnUpdate(float dt) {
    engine::Window& w = GetWindow();
    m_dt = dt;
    engine::GameMode& gameMode = engine::GameMode::Instance();
    // Hit-stop duration uses real time so a zero-dilation freeze can recover.
    gameMode.UpdateUnscaledTime(dt);
    float gameDt = gameMode.ScaleDelta(dt);
    if (m_simReady && !m_paused)
        for (RuntimeWater& water : m_waters) water.water.Update(gameDt);
    if (dt > 0.0f) m_fps = m_fps * 0.92f + (1.0f / dt) * 0.08f;
    if (m_saveToastTime > 0.0f) m_saveToastTime -= dt;

    // Esc closes the load menu if it is open, otherwise quits (edge-detected so a held
    // key does not close the menu and immediately quit on the next frame).
    const bool escDown = w.IsKeyPressed(GLFW_KEY_ESCAPE);
    if (escDown && !m_escPrev) {
        if (m_loadMenuOpen) CloseLoadMenu();
        else w.SetShouldClose(true);
    }
    m_escPrev = escDown;

    // In-game save (F5) / load menu (F9). A load reloads the world, so bail this frame.
    if (UpdateSaveLoadMenu()) return;

    // P toggles pause (edge-detected). Pausing frees the cursor so the HUD's menu
    // buttons become clickable; resuming re-captures it for mouse-look.
    const bool pauseDown = w.IsKeyPressed(GLFW_KEY_P);
    if (m_scene.gameMode.allowPause && pauseDown && !m_pausePrev) {
        m_paused = !m_paused;
        if (m_paused) {
            engine::GameMode::Instance().Pause();
        } else {
            engine::GameMode::Instance().Resume();
        }
        if (HasPlayer()) SetPlayCursor(!m_paused);
    }
    m_pausePrev = pauseDown;

    // Game over / victory: free the cursor for the end screen and allow a restart.
    if (gameMode.IsOver()) {
        if (HasPlayer()) SetPlayCursor(false);
        if (m_scene.gameMode.allowRestart && w.IsKeyPressed(GLFW_KEY_R)) {
            RestartScene();
        }
    }

    const bool skipDown =
        m_cameraDirector.Skippable() && w.IsKeyPressed(GLFW_KEY_ENTER);
    if (skipDown && !m_cinematicSkipPrev) m_cameraDirector.Skip();
    m_cinematicSkipPrev = skipDown;

    // Advance an already-playing cinematic before scripts so its shot/timeline
    // events are frame events: scripts observe them once below, then they are
    // cleared before any fixed updates run.
    if (m_simReady && !m_paused) UpdateCameraSequence(gameDt);
    if (m_simReady && !m_paused && m_zoneCameraBlend.Active())
        m_zoneCameraBlend.Update(gameDt);

    const bool inputEnabled =
        m_scene.gameMode.playerInputEnabled
        && !m_cameraDirector.InputLocked();
    if (m_simReady && !m_paused) {
        const engine::ScriptInputState input =
            CaptureScriptInput(inputEnabled, true);
        engine::UpdateScripts(
            m_registry, gameDt, &input, &m_runtimeAudio,
            &m_cameraShake, &m_cameraDirector, &gameMode, &m_physics);
        auto& dayNight = engine::DayNightTimelineRuntime::Instance();
        dayNight.Tick(gameDt);
        if (dayNight.Loaded()) {
            const auto key = dayNight.Sample(); auto& environment = m_scene.environment;
            environment.timeOfDay=dayNight.Time();environment.skyIntensity=key.skyIntensity;
            environment.skyLightIntensity=key.skyLightIntensity;environment.sunIntensity=key.sunIntensity;
            environment.cloudCoverage=key.cloudCoverage;environment.cloudDensity=key.cloudDensity;
            environment.cloudWindSpeed=key.cloudWindSpeed;environment.cloudWindDirection=key.cloudWindDirection;
            environment.cloudColor=key.cloudColor;environment.fogDensity=key.fogDensity;
            environment.fogHeight=key.fogHeight;environment.fogHeightFalloff=key.fogHeightFalloff;
            m_sample=engine::DayNightCycle::At(environment.timeOfDay);
            if (key.ambientAudioPath != m_dayNightAmbientPath) {
                if (m_dayNightAmbientSource != engine::AudioEngine::InvalidSource)
                    m_audio.DestroySource(m_dayNightAmbientSource);
                m_dayNightAmbientSource = engine::AudioEngine::InvalidSource;
                m_dayNightAmbientPath = key.ambientAudioPath;
                if (!m_dayNightAmbientPath.empty()) {
                    m_dayNightAmbientSource = m_audio.CreateSource(
                        m_dayNightAmbientPath, false, true, true, engine::AudioBus::Ambient);
                    m_audio.PlaySource(m_dayNightAmbientSource, true);
                }
            }
        }
        if (std::string requestedScene = engine::ConsumeScriptSceneLoadRequest();
            !requestedScene.empty()) {
            (void)engine::ConsumeScriptLevelStreamRequests();
            std::filesystem::path requested(requestedScene);
            if (!requested.is_absolute())
                requested = std::filesystem::path(m_sceneDir) / requested;
            m_scenePath = requested.lexically_normal().string();
            RestartScene();
            return;
        }
        // Full game save/load to a numbered slot (script-driven). A load reloads the saved
        // scene and restores the snapshot; see SaveToSlot / LoadFromSlot.
        for (const engine::ScriptSaveGameRequest& request
             : engine::ConsumeScriptSaveGameRequests()) {
            if (request.load) { LoadFromSlot(request.slot); return; }
            SaveToSlot(request.slot, request.displayName);
        }
        for (const engine::ScriptLevelStreamRequest& request
             : engine::ConsumeScriptLevelStreamRequests()) {
            if (!m_streamingEnabled) {
                m_runtimeWarnings.push_back(
                    "Level streaming request ignored: current startup asset is not a world.");
                continue;
            }
            std::size_t match = m_streaming.Levels().size();
            for (std::size_t i = 0; i < m_streaming.Levels().size(); ++i) {
                const std::filesystem::path candidate(
                    m_streaming.Levels()[i].ref.scenePath);
                if (request.level == m_streaming.Levels()[i].ref.scenePath
                    || request.level == candidate.filename().string()
                    || request.level == candidate.stem().string()) {
                    match = i;
                    break;
                }
            }
            if (match == m_streaming.Levels().size()) {
                m_runtimeWarnings.push_back(
                    "Unknown streamed level: " + request.level);
                continue;
            }
            const bool ok = request.load
                ? m_streaming.LoadLevel(
                    match, m_registry, m_assets, m_primitiveMeshes)
                : m_streaming.UnloadLevel(match, m_registry);
            if (!ok) {
                m_runtimeWarnings.push_back(
                    "Level streaming request failed: " + request.level
                    + " (" + m_streaming.LastError() + ")");
            }
        }
        // Script callbacks may have changed dilation or started a hit stop.
        gameDt = gameMode.ScaleDelta(dt);
        m_cameraDirector.ClearEvents();
        m_animationEvents.clear();
        ProcessCameraCommands();
        engine::UpdateParticleSystems(m_registry, gameDt);

        m_cameraShakeSample = {};
        if (m_cameraShake.Active())
            m_cameraShakeSample = m_cameraShake.Update(gameDt);

        const engine::Camera camera = BuildCamera();
        m_audio.SetListener(camera.Position(), camera.Front());
        m_runtimeAudio.Update(m_registry, dt);
        m_runtimeAudio.UpdateOcclusion(m_registry, m_physics, camera.Position());
        m_audio.UpdateMixer(dt);
    }

    if (HasPlayer()) {
        if (inputEnabled) {
            GatherPlayerInput();      // consumed at the fixed step
        } else {
            m_playerInput = {};
            m_lookPending = false;
        }
    } else {
        if (inputEnabled) UpdateFreeCamera(dt);
    }

    // Stream levels in/out around the viewer once per frame (at most one (de)activation
    // per call). Runs after input, before render, so the registry is consistent.
    if (m_simReady && m_streamingEnabled) {
        const glm::vec3 viewer = HasPlayer()
            ? m_playerController->Position() : BuildCamera().Position();
        m_streaming.Update(viewer, m_registry, m_assets, m_primitiveMeshes);
        if (!m_streaming.LastError().empty()
            && m_streaming.LastError() != m_lastStreamingError) {
            m_lastStreamingError = m_streaming.LastError();
            m_runtimeWarnings.push_back("Streaming: " + m_lastStreamingError);
        } else if (m_streaming.LastError().empty()) {
            m_lastStreamingError.clear();
        }
    }
}

void RuntimePlayerApp::OnFixedUpdate(float h) {
    if (!m_simReady || m_paused) return;
    const float gameStep =
        engine::GameMode::Instance().ScaleDelta(h);
    if (gameStep <= 0.000001f) return;
    // Keep physics alive after a win/loss so a newly activated death ragdoll can
    // fall and settle instead of freezing on its first frame.
    if (!engine::GameMode::Instance().IsPlaying()) {
        engine::UpdateRagdollsBeforePhysics(m_registry, m_physics);
        m_physics.Step(m_registry, gameStep);
        engine::UpdateRagdollsAfterPhysics(m_registry, m_physics, gameStep);
        return;
    }
    const bool inputEnabled =
        m_scene.gameMode.playerInputEnabled
        && !m_cameraDirector.InputLocked();

    // Player capsule first (moves via its own kinematic sweep against colliders).
    // Apply the frame's mouse-look only once even if several fixed steps run.
    if (HasPlayer()) {
        UpdateLockOn(inputEnabled);
        engine::PlayerInput in = m_playerInput;
        if (!m_lookPending) { in.lookYaw = 0.0f; in.lookPitch = 0.0f; }
        m_lookPending = false;
        engine::AnimatedModel* animated =
            m_registry.TryGet<engine::AnimatedModel>(m_playerEntity);
        const bool movementLocked = animated && animated->BlocksMovement();
        bool overWater = false;
        const float waterSurface = WaterSurfaceY(
            m_playerController->body.position.x,
            m_playerController->body.position.z, overWater);
        m_playerController->SetWaterSurface(overWater, waterSurface);
        if (animated) {
            const float moveMagnitude = std::min(
                glm::length(glm::vec2(in.moveForward, in.moveRight)), 1.0f);
            const bool sprinting = in.sprint
                && m_playerController->body.grounded && !in.jump
                && !in.crouch && !m_playerController->Swimming();
            const float movementSpeed = movementLocked ? 0.0f : moveMagnitude *
                (sprinting ? m_playerController->runSpeed : m_playerController->walkSpeed);
            const float previousSpeed = animated->controller.Parameter("Speed", 0.0f);
            const float invStep =
                gameStep > 0.0001f ? 1.0f / gameStep : 0.0f;
            animated->controller.SetParameter("Speed", movementSpeed);
            animated->controller.SetParameter("Direction", moveMagnitude > 0.001f
                ? glm::degrees(std::atan2(in.moveRight, in.moveForward)) : 0.0f);
            animated->controller.SetParameter("Acceleration", (movementSpeed - previousSpeed) * invStep);
            animated->controller.SetParameter("Deceleration",
                std::max(previousSpeed - movementSpeed, 0.0f) * invStep);
            animated->controller.SetParameter("TurnRate", in.lookYaw * invStep);
            animated->controller.SetBoolParameter("IsMoving", movementSpeed > 0.05f);
            animated->controller.SetBoolParameter("IsStopping",
                previousSpeed > 0.05f && movementSpeed <= 0.05f);
            animated->controller.SetParameter("VerticalSpeed", m_playerController->body.velocity.y);
            animated->controller.SetBoolParameter("IsGrounded", m_playerController->body.grounded);
            animated->controller.SetBoolParameter("IsFalling", !m_playerController->body.grounded
                && m_playerController->body.velocity.y < 0.0f);
        }
        m_playerController->Update(
            m_registry, in, gameStep, !movementLocked);
        if (animated) {
            const float moveMagnitude = std::min(
                glm::length(glm::vec2(in.moveForward, in.moveRight)), 1.0f);
            const bool sprinting = in.sprint && m_playerController->Grounded()
                && !in.jump && !m_playerController->Crouching()
                && !m_playerController->Swimming();
            const float movementSpeed = movementLocked ? 0.0f : moveMagnitude
                * (m_playerController->Swimming() ? m_playerController->swimSpeed
                 : m_playerController->Crouching() ? m_playerController->crouchSpeed
                 : sprinting ? m_playerController->runSpeed
                             : m_playerController->walkSpeed);
            animated->controller.SetParameter("Speed", movementSpeed);
            animated->controller.SetBoolParameter("IsMoving", movementSpeed > 0.05f);
            animated->controller.SetParameter("VerticalSpeed", m_playerController->body.velocity.y);
            animated->controller.SetBoolParameter("IsGrounded", m_playerController->Grounded());
            animated->controller.SetBoolParameter("IsFalling", !m_playerController->Grounded()
                && !m_playerController->Swimming()
                && m_playerController->body.velocity.y < 0.0f);
            animated->controller.SetBoolParameter("IsCrouching", m_playerController->Crouching());
            animated->controller.SetBoolParameter("IsSwimming", m_playerController->Swimming());
        }
        if (Transform* t = m_registry.TryGet<Transform>(m_playerEntity)) {
            bool overTerrain = false;
            const float surfaceY = TerrainSurfaceY(
                m_playerController->body.position.x,
                m_playerController->body.position.z, overTerrain);
            if (overTerrain && !m_playerController->Swimming()) {
                engine::CharacterController& body = m_playerController->body;
                const float feet = body.position.y - body.height * 0.5f;
                if (feet <= surfaceY + 0.02f) {
                    body.position.y = surfaceY + body.height * 0.5f;
                    if (body.velocity.y < 0.0f) body.velocity.y = 0.0f;
                    body.grounded = true;
                }
            }
            t->position = m_playerController->CapsulePosition();
            t->rotation = m_playerController->CapsuleRotation();
            // The kinematic CharacterController is authoritative for player
            // collision. Keep the trigger proxy (used by projectiles, overlaps,
            // and scripts) exactly the same capsule every fixed step; authored
            // scene collider dimensions must never drift from the controller.
            if (engine::ecs::Collider* proxy =
                    m_registry.TryGet<engine::ecs::Collider>(m_playerEntity)) {
                proxy->shape = engine::ecs::ColliderShape::Capsule;
                proxy->radius = std::max(m_playerController->body.radius, 0.01f);
                proxy->halfHeight = std::max(
                    m_playerController->body.height * 0.5f - proxy->radius, 0.0f);
                proxy->isTrigger = true;
                proxy->layer = engine::ecs::CollisionLayer::Player;
                proxy->mask = engine::ecs::CollisionLayer::All;
            }
        }
    }

    const engine::ScriptInputState input =
        CaptureScriptInput(inputEnabled, false);
    engine::FixedUpdateScripts(
        m_registry, gameStep, &input, &m_runtimeAudio,
        &m_cameraShake, &m_cameraDirector, &engine::GameMode::Instance(),
        &m_physics);
    UpdateAI(gameStep);
    engine::UpdateAbilities(m_registry, gameStep);
    engine::UpdateCombat(m_registry, gameStep);
    engine::UpdateSpawnManagers(m_registry, gameStep, m_playerEntity);
    engine::UpdateProjectilesInPlace(m_registry, gameStep);
    engine::ecs::UpdateGameplay(
        m_registry, gameStep);                         // rotators + movers
    engine::UpdateInteractions(m_registry, gameStep); // doors, gates, lifts + platforms
    engine::UpdatePortals(m_registry, gameStep);
    engine::UpdateHealth(m_registry);
    engine::UpdateRagdollsBeforePhysics(m_registry, m_physics);
    engine::ecs::UpdateRuntimeMotion(
        m_registry, gameStep);                         // linear/angular velocity
    // Give every foot-IK-enabled character a scene ground raycast (ignoring its own
    // collider). Set once; enable comes baked from the character asset.
    m_registry.view<engine::AnimatedModel>().each(
        [&](engine::ecs::Entity e, engine::AnimatedModel& am) {
            if (am.footIK.enabled && !am.footIK.groundQuery) {
                am.footIK.groundQuery =
                    [this, e](const glm::vec3& origin, const glm::vec3& down, float maxDist,
                              glm::vec3& hitPos, glm::vec3& hitNormal) {
                        engine::Ray ray{origin, down};
                        const engine::RaycastHit hit =
                            m_physics.Raycast(m_registry, ray, maxDist, 0xFFFFFFFFu, e);
                        if (!hit.hit) return false;
                        hitPos = hit.point;
                        hitNormal = hit.normal;
                        return true;
                    };
            }
        });
    engine::UpdateAnimations(m_registry, gameStep);
    ApplyWaterBuoyancy(gameStep);
    m_physics.Step(m_registry, gameStep);
    engine::UpdateRagdollsAfterPhysics(m_registry, m_physics, gameStep);
    ProcessLevelPhysicsEvents();
    m_runtimeAudio.ProcessCollisionEvents(m_registry, m_physics.Events());
    engine::ProcessParticleCollisionEvents(m_registry, m_physics.Events());
    engine::QueueScriptCollisionEvents(m_registry, m_physics.Events());

    // Evaluate game rules (built-in: lose when the player dies). Scripts can also
    // drive it directly via engine::GameMode::Instance().
    engine::GameMode::Instance().Update(
        m_registry, m_playerEntity, gameStep);
}

void RuntimePlayerApp::OnRender() {
    engine::Window& w = GetWindow();
    const float aspect = w.AspectRatio();
    m_post->Resize(w.Width(), w.Height());

    engine::Camera cam = BuildCamera();
    if (HasPlayer() && !m_zoneCameraPose && !m_zoneCameraBlend.Active()
        && !m_cameraSequence.Active() && !m_cameraDirector.Playing()) {
        bool overTerrain = false;
        const glm::vec3 desired = cam.Position();
        const float surfaceY = TerrainSurfaceY(desired.x, desired.z, overTerrain);
        cam.SetPosition(m_terrainCameraConstraint.Resolve(
            desired, surfaceY, overTerrain, m_dt));
    } else {
        m_terrainCameraConstraint.Reset();
    }
    UpdateUnderwaterState(cam, m_dt);
    const RuntimeSceneLoader::Scene::Environment& env = m_scene.environment;
    UpdateDynamicGi(cam);
    m_post->settings.autoExposure = env.autoExposure;
    m_post->settings.exposure = std::exp2(env.exposureCompensationEV);
    m_post->settings.minEV = env.exposureMinEV;
    m_post->settings.maxEV = env.exposureMaxEV;
    m_post->settings.exposureCompensationEV = env.exposureCompensationEV;
    m_post->settings.adaptationSpeedUp = env.exposureSpeedUp;
    m_post->settings.adaptationSpeedDown = env.exposureSpeedDown;
    m_post->settings.preserveNightDarkness = env.preserveNightDarkness;
    m_post->settings.nightExposureLimitEV = env.nightExposureLimitEV;
    m_post->settings.bloom = env.bloom;
    m_post->settings.bloomThreshold = env.bloomThreshold;
    m_post->settings.bloomKnee = env.bloomKnee;
    m_post->settings.bloomStrength = env.bloomStrength;
    m_post->settings.temperature = env.colorTemperature;
    m_post->settings.tint = env.colorTint;
    m_post->settings.saturation = env.colorSaturation;
    m_post->settings.contrast = env.colorContrast;
    m_post->settings.lift = env.colorLift;
    m_post->settings.gamma = env.colorGamma;
    m_post->settings.gain = env.colorGain;
    m_post->settings.lutIntensity = env.colorLutIntensity;
    const engine::Texture* colorLut = nullptr;
    if (!env.colorLutPath.empty()) {
        std::string lutError;
        colorLut = m_assets.LoadTexture(env.colorLutPath, &lutError);
    }
    m_post->SetColorLut(colorLut);
    m_post->volumetrics.enabled = env.volumetricFog;
    m_post->volumetrics.density = env.fogDensity;
    const auto resolvedEnvironment = ResolveEnvironment(env,m_sample);
    if (env.skyMode != 1
        && std::abs(resolvedEnvironment.environmentIntensity
                    - m_lastIblEnvironmentEnergy) > 0.03f) {
        m_ibl->Generate([&](const glm::mat4& view, const glm::mat4& projection) {
            DrawEnvironmentSky(view, projection, false);
        });
        m_lastIblEnvironmentEnergy = resolvedEnvironment.environmentIntensity;
    }
    m_post->volumetrics.scattering = env.volumetricScattering
        * glm::mix(1.0f,env.nightFogScattering,resolvedEnvironment.nightFactor);
    m_post->volumetrics.extinction = env.volumetricExtinction;
    m_post->volumetrics.anisotropy = env.volumetricAnisotropy;
    m_post->volumetrics.baseHeight = env.fogHeight;
    m_post->volumetrics.heightFalloff = env.fogHeightFalloff;
    m_post->volumetrics.startDistance = env.volumetricStartDistance;
    m_post->volumetrics.maxDistance = env.volumetricMaxDistance;
    const auto& lightingProfile=engine::GetLightingQualityProfile(
        static_cast<engine::LightingQuality>(std::clamp(env.environmentQuality,0,3)));
    engine::ApplyLightingQuality(lightingProfile,nullptr,nullptr,
                                 m_ssgi?&*m_ssgi:nullptr,&m_reflectionProbes,&*m_post);
    m_post->SetVolumetricCamera(glm::inverse(
        cam.ProjectionMatrix(aspect)*cam.ViewMatrix()),cam.Position(),
        ResolveEnvironment(env,m_sample));
    engine::ApplyPostProcessVolumes(m_registry,cam.Position(),*m_post);
    m_post->BeginScene();
    m_renderer.Clear();
    if (env.ssgiEnabled) {
        if (!m_ssao) m_ssao.emplace(w.Width(), w.Height());
        m_ssao->Resize(w.Width(), w.Height());
        m_ssao->Generate(m_registry, cam, aspect, w.Width(), w.Height());
    }
    engine::PbrRenderer::Options opt;
    engine::ApplyLightingQuality(lightingProfile,&opt,nullptr,
                                 m_ssgi?&*m_ssgi:nullptr,&m_reflectionProbes,&*m_post);
    m_reflectionProbes.Sync(m_registry,cam.Position());
    opt.ambient = resolvedEnvironment.ambientRadiance;
    opt.ibl = &*m_ibl;
    opt.globalIblIntensity = env.skyMode == 1
        ? resolvedEnvironment.environmentIntensity : 1.0f;
    opt.globalReflectionIntensity = glm::mix(1.0f,
        env.nightReflectionIntensity,m_sample.nightFactor);
    opt.reflectionProbes = &m_reflectionProbes;
    opt.lightingGrid = m_dynamicGi.GpuReady() ? &m_dynamicGi.Grid()
        : (m_lightingProbeGrid.Valid() ? &m_lightingProbeGrid : nullptr);
    opt.localProbeInfluence = env.localProbeInfluence;
    opt.specularOcclusionStrength = env.specularOcclusionStrength;
    opt.lightingDebugMode = env.lightingDebugMode;
    opt.probeVisibilityWeighting = m_dynamicGi.GpuReady() && env.dynamicGiVisibilityWeighting;
    opt.probeVisibilityMaxDistance = env.dynamicGiMaxRayDistance;
    opt.skylightOcclusion = env.skylightOcclusion;
    opt.skylightOcclusionStrength = env.skylightOcclusionStrength;
    opt.minimumSkylight = env.minimumSkylight;
    opt.fog = env.fog;
    opt.fogDensity = env.fogDensity;
    opt.fogHeight = env.fogHeight;
    opt.fogHeightFalloff = env.fogHeightFalloff;
    opt.fogColor = ResolveEnvironment(env, m_sample).SampleEnvironmentRadiance(
        glm::normalize(glm::vec3(1.0f, 0.04f, 0.0f)));
    opt.cloudShadows = env.clouds && env.cloudShadows
        && MaxLightComponent(m_sample.sunRadiance) > 0.001f;
    opt.cloudShadowStrength = env.cloudShadowStrength;
    opt.cloudShadowScale = env.cloudShadowScale;
    opt.cloudCoverage = env.cloudCoverage;
    opt.cloudDensity = env.cloudDensity;
    opt.cloudSoftness = env.cloudSoftness;
    opt.cloudWindSpeed = env.cloudWindSpeed;
    opt.cloudWindDirectionDegrees = env.cloudWindDirection;
    opt.shadowDistance = env.shadowDistance;
    // RM3: shadow parity — directional (sun) + point + spot shadows are all on by
    // default in Options, matching the editor's play view.
    // Let animated characters cast sun shadows too (skinned depth into the cascade).
    if (m_skinnedRenderer) {
        opt.shadowCasters = [this](const glm::mat4& lightViewProjection) {
            m_skinnedRenderer->DrawSceneDepth(m_registry, lightViewProjection);
        };
    }
    m_pbr->Render(m_registry, cam, aspect, w.Width(), w.Height(), opt);
    if (m_foliageRenderer) {
        m_foliageRenderer->Draw(m_registry, cam, aspect,
            m_sample.keyLightDirection,
            EnvironmentKeyRadiance(env,m_sample),
            resolvedEnvironment.ambientRadiance,
            static_cast<float>(glfwGetTime()));   // drives wind sway
    }

    // Skinned pass: animated characters (AnimatedModel), lit to match the PBR world.
    // Runs after PbrRenderer so the cascade/IBL textures it samples are ready.
    if (m_skinnedRenderer) {
        engine::SkinnedLighting lighting;
        lighting.sunDir = m_sample.keyLightDirection;
        lighting.sunColor = EnvironmentKeyRadiance(env,m_sample);
        lighting.ambient = resolvedEnvironment.ambientRadiance;
        lighting.cascade = &m_pbr->Cascade();
        lighting.ibl = &*m_ibl;
        lighting.globalIblIntensity = env.skyMode == 1
            ? resolvedEnvironment.environmentIntensity : 1.0f;
        lighting.globalReflectionIntensity = glm::mix(1.0f,
            env.nightReflectionIntensity,m_sample.nightFactor);
        lighting.reflectionProbes = &m_reflectionProbes;
        lighting.lightingGrid = m_dynamicGi.GpuReady() ? &m_dynamicGi.Grid()
            : (m_lightingProbeGrid.Valid() ? &m_lightingProbeGrid : nullptr);
        lighting.localProbeInfluence = env.localProbeInfluence;
        lighting.specularOcclusionStrength = env.specularOcclusionStrength;
        lighting.lightingDebugMode = env.lightingDebugMode;
        lighting.probeVisibilityWeighting = m_dynamicGi.GpuReady() && env.dynamicGiVisibilityWeighting;
        lighting.probeVisibilityMaxDistance = env.dynamicGiMaxRayDistance;
        lighting.skylightOcclusion = env.skylightOcclusion;
        lighting.skylightOcclusionStrength = env.skylightOcclusionStrength;
        lighting.minimumSkylight = env.minimumSkylight;
        lighting.shadowBlockerSamples=lightingProfile.shadowBlockerSamples;
        lighting.shadowFilterSamples=lightingProfile.shadowFilterSamples;
        lighting.cloudShadows = env.clouds && env.cloudShadows
            && MaxLightComponent(m_sample.sunRadiance) > 0.001f;
        lighting.cloudShadowStrength = env.cloudShadowStrength;
        lighting.cloudShadowScale = env.cloudShadowScale;
        lighting.cloudCoverage = env.cloudCoverage;
        lighting.cloudDensity = env.cloudDensity;
        lighting.cloudSoftness = env.cloudSoftness;
        lighting.cloudWindSpeed = env.cloudWindSpeed;
        lighting.cloudWindDirectionDegrees = env.cloudWindDirection;
        lighting.tonemap = true;
        lighting.fog = env.fog;
        lighting.fogColor = ResolveEnvironment(env, m_sample).SampleEnvironmentRadiance(
            glm::normalize(glm::vec3(1.0f, 0.04f, 0.0f)));
        lighting.fogDensity = env.fogDensity;
        lighting.fogHeight = env.fogHeight;
        lighting.fogHeightFalloff = env.fogHeightFalloff;
        m_skinnedRenderer->DrawScene(m_registry, cam, aspect, lighting);

        // Socketed attachments (weapons/shields) ride the animated bones.
        if (m_modelShader) {
            const glm::mat4 viewProj = cam.ProjectionMatrix(aspect) * cam.ViewMatrix();
            m_modelShader->Bind();
            m_modelShader->SetMat4("uViewProj", viewProj);
            m_modelShader->SetVec3("uLightPos", cam.Position() + glm::vec3(-4.0f, 6.0f, 4.0f));
            m_modelShader->SetVec3("uLightColor", glm::vec3(1.0f));
            m_modelShader->SetVec3("uViewPos", cam.Position());
            m_registry.view<engine::ecs::Transform, engine::AnimatedModel>().each(
                [&](Entity, engine::ecs::Transform& t, engine::AnimatedModel& am) {
                    if (am.attachments.empty()) return;
                    engine::DrawAnimatedModelAttachments(am, t.Model() * am.renderOffset, *m_modelShader);
                });
        }
    }

    DrawEnvironmentSky(cam.ViewMatrix(), cam.ProjectionMatrix(aspect), false);
    CaptureWaterSceneBuffers();
    DrawWaters(cam, aspect);
    if (m_particleRenderer) {
        m_particleRenderer->ResetStats();
        m_registry.view<engine::ParticleSystemComponent>().each(
            [&](Entity, engine::ParticleSystemComponent& system) {
                m_particleRenderer->Draw(system, cam, aspect);
            });
        m_registry.view<engine::ParticleEffectComponent>().each(
            [&](Entity, engine::ParticleEffectComponent& effect) {
                if (!effect.enabled) return;
                for (engine::ParticleEffectLayer& layer : effect.layers)
                    if (layer.enabled) m_particleRenderer->Draw(layer.system, cam, aspect);
            });
    }
    if (env.ssgiEnabled && m_ssao) {
        if (!m_ssgi) m_ssgi.emplace(w.Width(), w.Height());
        m_ssgi->Resize(w.Width(), w.Height());
        m_ssgi->rayLength = env.ssgiRayLength;
        m_ssgi->steps = std::min(env.ssgiSteps, lightingProfile.ssgiSteps);
        m_ssgi->thickness = env.ssgiThickness;
        m_ssgi->intensity = std::min(env.ssgiIntensity, lightingProfile.ssgiIntensity);
        m_ssgi->Generate(m_post->HdrColor(), m_ssao->PositionTexture(),
                         m_ssao->NormalTexture(), cam.ProjectionMatrix(aspect));
        m_post->SetIndirectTexture(m_ssgi->Texture(), m_ssgi->intensity);
    } else {
        m_post->SetIndirectTexture(0, 0.0f);
    }
    m_post->SetIndirectDebug(env.lightingDebugMode == 18);
    m_post->SetLightingDebugPassthrough(env.lightingDebugMode != 0
                                        && env.lightingDebugMode != 18);
    m_post->SetVolumetricDirectionalShadow(
        m_pbr ? &m_pbr->Cascade() : nullptr,
        cam.ViewMatrix());
    m_post->SetVolumetricLights(GatherVolumetricLights(
        m_registry,cam.Position(),lightingProfile.maxVolumetricLights));
    m_post->SetLocalFogVolumes(GatherLocalFogVolumes(m_registry));
    m_post->RenderToScreen(w.Width(), w.Height(), m_dt);

    engine::DrawWorldHealthBars(
        *m_text, m_registry,
        cam.ProjectionMatrix(aspect) * cam.ViewMatrix(),
        w.Width(), w.Height(), m_playerEntity);

    // Game HUD (the scene's .hud), drawn on the presented scene.
    DrawHudOverlay();

    // Status overlay.
    const int ww = w.Width(), hh = w.Height();
    m_text->Begin(ww, hh);
    if (!m_loadError.empty()) {
        m_text->Text("RUNTIME PLAYER", 24.0f, 22.0f, 2.0f, glm::vec3(1.0f, 0.9f, 0.5f));
        m_text->Text(m_loadError, 24.0f, 60.0f, 1.4f, glm::vec3(1.0f, 0.5f, 0.45f));
        m_text->Text("Usage: player <scene.3dgscene>", 24.0f, 88.0f, 1.3f, glm::vec3(0.75f));
    } else {
        char buf[192];
        std::snprintf(buf, sizeof(buf), "RUNTIME PLAYER   %zu entities   %.0f fps   %s",
                      m_entityCount, m_fps, m_paused ? "PAUSED" : "running");
        m_text->Text(buf, 24.0f, 22.0f, 2.0f,
                     m_paused ? glm::vec3(1.0f, 0.8f, 0.4f) : glm::vec3(1.0f));
        m_text->Text(m_scenePath, 24.0f, 54.0f, 1.2f, glm::vec3(0.7f));

        // Diagnostic: scripts the scene references but that aren't registered in
        // GameScripts.cpp won't run. Surface the count so it's not a silent failure.
        int missing = 0;
        m_registry.view<engine::NativeScriptComponent>().each([&](Entity, engine::NativeScriptComponent& s) {
            if (s.enabled && s.missingFactory) ++missing;
        });
        float warnY = 80.0f;
        if (missing > 0) {
            char warn[128];
            std::snprintf(warn, sizeof(warn),
                          "%d script(s) not registered - add them in GameScripts.cpp", missing);
            m_text->Text(warn, 24.0f, warnY, 1.2f, glm::vec3(1.0f, 0.55f, 0.4f));
            warnY += 24.0f;
        }
        if (m_assetErrors > 0) {
            char warn[128];
            std::snprintf(warn, sizeof(warn),
                          "%d asset(s) failed to load - run from the content root", m_assetErrors);
            m_text->Text(warn, 24.0f, warnY, 1.2f, glm::vec3(1.0f, 0.55f, 0.4f));
        }
    }
    const char* controls = HasPlayer()
        ? "WASD move   Space jump   Shift sprint   V view   mouse look   P pause   F5 save   F9 load   Esc quit"
        : "WASD move   Q/E down/up   hold RMB look   Shift sprint   P pause   F5 save   F9 load   Esc quit";
    m_text->Text(controls, 24.0f, static_cast<float>(hh) - 32.0f, 1.3f, glm::vec3(0.72f));

    // End screen: centered VICTORY / GAME OVER with score + restart prompt.
    const engine::GameMode& gm = engine::GameMode::Instance();
    if (gm.IsOver()) {
        const float fw = static_cast<float>(ww), fh = static_cast<float>(hh);
        const char* title = gm.IsWon() ? "VICTORY" : "GAME OVER";
        const glm::vec3 col = gm.IsWon() ? glm::vec3(0.5f, 1.0f, 0.6f) : glm::vec3(1.0f, 0.45f, 0.4f);
        const float tw = m_text->Measure(title, 4.0f);
        m_text->Text(title, (fw - tw) * 0.5f, fh * 0.5f - 60.0f, 4.0f, col);
        if (!gm.Message().empty()) {
            const float mw = m_text->Measure(gm.Message(), 1.6f);
            m_text->Text(gm.Message(), (fw - mw) * 0.5f, fh * 0.5f - 6.0f, 1.6f, glm::vec3(0.9f));
        }
        char sc[80];
        std::snprintf(sc, sizeof(sc), "Score %d    Press R to restart", gm.Score());
        const float sw = m_text->Measure(sc, 1.6f);
        m_text->Text(sc, (fw - sw) * 0.5f, fh * 0.5f + 26.0f, 1.6f, glm::vec3(0.85f));
    }
    DrawSaveLoadMenu(ww, hh);   // overlays the slot list + save toast when active
    m_text->End();
}

void RuntimePlayerApp::OnShutdown() {
    engine::ShutdownScripts(m_registry);   // OnDestroy() + release script instances
    engine::ScriptRegistry::Instance().Clear();
    engine::ai::BtScriptRegistry::Instance().Clear();
    m_projectScriptModule.Unload();
    m_runtimeAudio.Stop();
    m_audio.StopAllSounds();
    m_audio.StopMusic();
    m_audio.DestroyAllSources();
    m_dayNightAmbientSource = engine::AudioEngine::InvalidSource;
    m_dayNightAmbientPath.clear();
    m_config.Set("window.vsync", GetWindow().IsVSync());
    m_config.Save();
}
