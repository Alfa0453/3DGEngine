#include "RuntimePlayerApp.h"
#include "GameScripts.h"

#include <engine/graphics/Primitives.h>
#include <engine/gameplay/Script.h>
#include <engine/gameplay/GameplaySystems.h>
#include <engine/gameplay/GameplayComponents.h>
#include <engine/gameplay/GameMode.h>
#include <engine/ai/BtScript.h>
#include <engine/ecs/RuntimeSystems.h>
#include <engine/ai/NavMeshBuilder.h>
#include <engine/ai/Perception.h>
#include <engine/ai/Steering.h>
#include <engine/ecs/Systems.h>          // RenderLoadedModels
#include <engine/animation/AnimatedModel.h>

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <vector>

using engine::ecs::Entity;
using engine::ecs::Transform;
using engine::ecs::MeshRenderer;
using engine::ecs::MeshPBR;
using engine::ecs::PbrMaterial;
using engine::RuntimeSceneLoader;

namespace {
engine::WindowProps MakeProps(engine::Config& cfg) {
    engine::WindowProps p;
    p.title  = "3DGEngine — Runtime Player";
    p.width  = cfg.GetInt("window.width", 1280);
    p.height = cfg.GetInt("window.height", 720);
    p.vsync  = cfg.GetBool("window.vsync", true);
    return p;
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

    // Image-based lighting baked from the sky at the scene's time of day.
    m_ibl.emplace(256);
    m_ibl->Generate([&](const glm::mat4& v, const glm::mat4& p) { m_sky->Draw(v, p, m_sample, false); });

    glfwGetCursorPos(GetWindow().Native(), &m_lastMouseX, &m_lastMouseY);
    if (HasPlayer() && !m_paused) SetPlayCursor(true);   // FPS-style mouse-look
}

void RuntimePlayerApp::LoadScene() {
    m_runtimeWarnings.clear();
    if (m_scenePath.empty()) {
        m_loadError = "No scene specified. Pass a .3dgscene path on the command line.";
        return;
    }

    std::string err;
    if (!RuntimeSceneLoader::Load(m_scenePath, &m_scene, &err)) {
        m_loadError = "Load failed: " + err;
        return;
    }

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

    std::vector<Entity> created;
    if (!RuntimeSceneLoader::Instantiate(m_scene, m_registry, meshes, &created, &err)) {
        m_loadError = "Instantiate failed: " + err;
        return;
    }
    m_entityCount = created.size();
    m_sample = engine::DayNightCycle::At(m_scene.environment.timeOfDay);

    // Resolve authored asset references (materials/models/skinned) into loaded GPU
    // assets: MaterialAsset -> LoadedMaterialAsset, etc. Asset paths resolve against
    // the working directory (run from the content root, or bundle assets to match).
    const engine::RuntimeAssetManager::ResolveReport report = m_assets.ResolveRegistryAssets(m_registry);
    m_assetErrors = static_cast<int>(report.errors.size());
    for (const std::string& assetError : report.errors)
        m_runtimeWarnings.push_back("Asset: " + assetError);

    m_registry.view<engine::AnimatedModel>().each(
        [this](Entity entity, engine::AnimatedModel& animated) {
            animated.onEvent = [this, entity](const std::string& name) {
                if (name.empty()) return;
                m_runtimeAudio.ProcessAnimationEvent(m_registry, entity, name);
                engine::ProcessParticleAnimationEvent(m_registry, entity, name);
                m_animationEvents.push_back({entity, name});
            };
        });

    // The loader adds MeshRenderer (mesh + authored colour); the PBR renderer draws
    // MeshPBR. Give each drawable its resolved PBR material, or a default material
    // from the authored colour when it has none. (Static models + skinned meshes get
    // dedicated render passes in a later milestone; they draw as placeholders here.)
    std::vector<Entity> drawables;
    m_registry.view<Transform, MeshRenderer>().each([&](Entity e, Transform&, MeshRenderer&) {
        drawables.push_back(e);
    });
    for (Entity e : drawables) {
        // Imported static models + animated characters are drawn by their own
        // passes (RenderLoadedModels / SkinnedRenderer), not as a placeholder mesh.
        if (m_registry.Has<engine::ecs::LoadedModelAsset>(e)) continue;
        if (m_registry.Has<engine::AnimatedModel>(e)) continue;
        const MeshRenderer& mr = m_registry.Get<MeshRenderer>(e);
        MeshPBR mesh;
        mesh.mesh = mr.mesh;
        if (const engine::ecs::LoadedMaterialAsset* lm = m_registry.TryGet<engine::ecs::LoadedMaterialAsset>(e)) {
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
        m_registry.Add<MeshPBR>(e, std::move(mesh));
    }

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
    BuildRuntimeLevelFeatures();
    BuildAI();
    ValidateRuntimeScene();
    LoadHud();

    engine::GameMode::Instance().Reset();   // fresh run: Playing, score 0, clock 0
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
    m_physics.solverIterations       = env.physicsSolverIterations;
    m_physics.broadPhase             = env.physicsBroadPhase;
    m_physics.cellSize               = env.physicsCellSize;
    m_physics.restitutionThreshold   = env.physicsRestitutionThreshold;
    m_physics.allowSleeping          = env.physicsAllowSleeping;
    m_physics.sleepLinearVelocity    = env.physicsSleepLinearVelocity;
    m_physics.sleepAngularVelocity   = env.physicsSleepAngularVelocity;
    m_physics.timeToSleep            = env.physicsTimeToSleep;
}

void RuntimePlayerApp::RestartScene() {
    engine::ShutdownScripts(m_registry);
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
        case engine::HudButtonAction::RestartPlay: RestartScene(); break;
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
    for (const auto& d : m_scene.entities) {
        if (d.playerControllerEnabled) { desc = &d; playerName = d.name; break; }
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
        controller.view = pc.firstPerson ? engine::PlayerController::View::FirstPerson
                                         : engine::PlayerController::View::ThirdPerson;
        controller.walkSpeed          = pc.walkSpeed;
        controller.runSpeed           = pc.runSpeed;
        controller.jumpSpeed          = pc.jumpSpeed;
        controller.lookSensitivity    = pc.lookSensitivity;
        controller.eyeHeight          = pc.eyeHeight;
        controller.camDistance        = pc.cameraDistance;
        controller.camTargetHeight    = pc.cameraTargetHeight;
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
        controller.body.stepHeight    = pc.stepHeight;
        controller.body.SetMaxSlopeDegrees(pc.maxSlopeDegrees);
        controller.SetCapsule(pc.capsuleRadius, pc.capsuleHeight);
    } else {
        controller.SetCapsule(0.4f, 1.8f);
    }

    if (const Transform* t = m_registry.TryGet<Transform>(found)) {
        controller.SetPosition(t->position);
    }
    // The controller owns a kinematic capsule; drop the entity's physics so the
    // solver doesn't fight it (mirrors the editor's play setup).
    if (m_registry.Has<engine::ecs::Collider>(found))  m_registry.Remove<engine::ecs::Collider>(found);
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

        engine::ecs::PbrMaterial material;
        material.albedo = glm::vec3(1.0f);
        material.roughness = 0.92f;
        material.albedoMap = &runtime.terrain.Albedo();
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
        const float localX = x - transform->position.x;
        const float localZ = z - transform->position.z;
        if (localX < map.origin.x || localZ < map.origin.z ||
            localX > map.origin.x + map.size || localZ > map.origin.z + map.size)
            continue;
        best = std::max(
            best, transform->position.y + runtime.terrain.HeightAt(localX, localZ));
        over = true;
    }
    return over ? best : 0.0f;
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
    for (const auto& joint : m_scene.physicsJoints) {
        const Entity a = FindNamedEntity(joint.objectA);
        const Entity b = FindNamedEntity(joint.objectB);
        if (a == engine::ecs::kNull || (!joint.worldAnchor && b == engine::ecs::kNull))
            continue;
        if (joint.type == 1) {
            if (joint.worldAnchor)
                m_physics.AddSpringJointToWorld(
                    a, joint.anchor, joint.restLength, joint.stiffness, joint.damping);
            else
                m_physics.AddSpringJoint(
                    a, b, joint.restLength, joint.stiffness, joint.damping);
        } else if (joint.worldAnchor) {
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
        if (entity.scriptEnabled && !entity.scriptClassName.empty() &&
            !engine::ScriptRegistry::Instance().Has(entity.scriptClassName))
            warn("Script class is not registered: " + entity.scriptClassName +
                 " (entity " + entity.name + ")");
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
        runtime.brain.agent.maxSpeed = desc.speed;
        runtime.brain.agent.maxForce = desc.maxForce;
        runtime.brain.reachRadius = desc.reachRadius;
        runtime.brain.repathInterval = desc.repathInterval;
        runtime.brain.vision.range = desc.visionRange;
        runtime.brain.vision.halfAngleDegrees = desc.visionHalfAngle;
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
    for (RuntimeAgent& agent : m_agents) {
        Transform* transform = m_registry.TryGet<Transform>(agent.entity);
        if (!transform) continue;
        const engine::AnimatedModel* animated =
            m_registry.TryGet<engine::AnimatedModel>(agent.entity);
        const bool movementLocked = animated && animated->BlocksMovement();
        const glm::vec3 lockedPosition = transform->position;

        if (agent.autoTarget && agent.team != 0) {
            Entity nearest = engine::ecs::kNull;
            float nearestDistance = 1.0e18f;
            for (const RuntimeAgent& other : m_agents) {
                if (other.entity == agent.entity || other.team == 0 ||
                    other.team == agent.team) continue;
                const Transform* otherTransform =
                    m_registry.TryGet<Transform>(other.entity);
                if (!otherTransform) continue;
                if (const engine::Health* health =
                    m_registry.TryGet<engine::Health>(other.entity);
                    health && !health->alive) continue;
                const float distance =
                    glm::length(otherTransform->position - transform->position);
                if (distance < nearestDistance) {
                    nearestDistance = distance;
                    nearest = other.entity;
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

        if (agent.useGraph) {
            engine::ai::AgentContext& context = agent.context;
            context.dt = dt;
            context.targetPos = targetPosition;
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
                agent.brain.Update(dt, targetPosition, seesTarget, m_navMesh);
            }
            transform->position = agent.brain.Position();
            facing = agent.brain.Facing();
        }
        if (glm::dot(facing, facing) > 1.0e-6f) {
            const float yaw = std::atan2(facing.x, facing.z);
            transform->rotation =
                glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f));
        }
        bool overTerrain = false;
        const float terrainY =
            TerrainSurfaceY(transform->position.x, transform->position.z, overTerrain);
        if (overTerrain) {
            transform->position.y = terrainY;
            if (agent.useGraph) agent.context.agent.position.y = terrainY;
            else agent.brain.SetPosition(transform->position);
        }
    }

    constexpr float separationRadius = 1.2f;
    for (RuntimeAgent& agent : m_agents) {
        Transform* transform = m_registry.TryGet<Transform>(agent.entity);
        if (!transform) continue;
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
        transform->position += move;
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
    in.toggleView = w.IsKeyPressed(GLFW_KEY_V);
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
    if (dt > 0.0f) m_fps = m_fps * 0.92f + (1.0f / dt) * 0.08f;
    if (w.IsKeyPressed(GLFW_KEY_ESCAPE)) w.SetShouldClose(true);

    // P toggles pause (edge-detected). Pausing frees the cursor so the HUD's menu
    // buttons become clickable; resuming re-captures it for mouse-look.
    const bool pauseDown = w.IsKeyPressed(GLFW_KEY_P);
    if (pauseDown && !m_pausePrev) {
        m_paused = !m_paused;
        if (HasPlayer()) SetPlayCursor(!m_paused);
    }
    m_pausePrev = pauseDown;

    // Game over / victory: free the cursor for the end screen and allow a restart.
    if (engine::GameMode::Instance().IsOver()) {
        if (HasPlayer()) SetPlayCursor(false);
        if (w.IsKeyPressed(GLFW_KEY_R)) RestartScene();
    }

    const bool skipDown =
        m_cameraDirector.Skippable() && w.IsKeyPressed(GLFW_KEY_ENTER);
    if (skipDown && !m_cinematicSkipPrev) m_cameraDirector.Skip();
    m_cinematicSkipPrev = skipDown;

    // Advance an already-playing cinematic before scripts so its shot/timeline
    // events are frame events: scripts observe them once below, then they are
    // cleared before any fixed updates run.
    if (m_simReady && !m_paused) UpdateCameraSequence(dt);
    if (m_simReady && !m_paused && m_zoneCameraBlend.Active())
        m_zoneCameraBlend.Update(dt);

    const bool inputEnabled = !m_cameraDirector.InputLocked();
    if (m_simReady && !m_paused) {
        const engine::ScriptInputState input =
            CaptureScriptInput(inputEnabled, true);
        engine::UpdateScripts(
            m_registry, dt, &input, &m_runtimeAudio,
            &m_cameraShake, &m_cameraDirector);
        if (std::string requestedScene = engine::ConsumeScriptSceneLoadRequest();
            !requestedScene.empty()) {
            std::filesystem::path requested(requestedScene);
            if (!requested.is_absolute())
                requested = std::filesystem::path(m_sceneDir) / requested;
            m_scenePath = requested.lexically_normal().string();
            RestartScene();
            return;
        }
        m_cameraDirector.ClearEvents();
        m_animationEvents.clear();
        ProcessCameraCommands();
        engine::UpdateParticleSystems(m_registry, dt);

        m_cameraShakeSample = {};
        if (m_cameraShake.Active())
            m_cameraShakeSample = m_cameraShake.Update(dt);

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
}

void RuntimePlayerApp::OnFixedUpdate(float h) {
    // Freeze the simulation while paused or once the run is over/won (GameMode).
    if (!m_simReady || m_paused || !engine::GameMode::Instance().IsPlaying()) return;
    const bool inputEnabled = !m_cameraDirector.InputLocked();

    // Player capsule first (moves via its own kinematic sweep against colliders).
    // Apply the frame's mouse-look only once even if several fixed steps run.
    if (HasPlayer()) {
        UpdateLockOn(inputEnabled);
        engine::PlayerInput in = m_playerInput;
        if (!m_lookPending) { in.lookYaw = 0.0f; in.lookPitch = 0.0f; }
        m_lookPending = false;
        m_playerController->Update(m_registry, in, h, true);
        if (Transform* t = m_registry.TryGet<Transform>(m_playerEntity)) {
            bool overTerrain = false;
            const float surfaceY = TerrainSurfaceY(
                m_playerController->body.position.x,
                m_playerController->body.position.z, overTerrain);
            if (overTerrain) {
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
        }
    }

    const engine::ScriptInputState input =
        CaptureScriptInput(inputEnabled, false);
    engine::FixedUpdateScripts(
        m_registry, h, &input, &m_runtimeAudio,
        &m_cameraShake, &m_cameraDirector);
    UpdateAI(h);
    engine::ecs::UpdateGameplay(m_registry, h);        // rotators + movers
    engine::UpdateHealth(m_registry);
    engine::ecs::UpdateRuntimeMotion(m_registry, h);   // linear/angular velocity
    engine::UpdateAnimations(m_registry, h);
    m_physics.Step(m_registry, h);
    ProcessLevelPhysicsEvents();
    m_runtimeAudio.ProcessCollisionEvents(m_registry, m_physics.Events());
    engine::ProcessParticleCollisionEvents(m_registry, m_physics.Events());

    // Evaluate game rules (built-in: lose when the player dies). Scripts can also
    // drive it directly via engine::GameMode::Instance().
    engine::GameMode::Instance().Update(m_registry, m_playerEntity, h);
}

void RuntimePlayerApp::OnRender() {
    engine::Window& w = GetWindow();
    const float aspect = w.AspectRatio();
    m_post->Resize(w.Width(), w.Height());

    engine::Camera cam = BuildCamera();

    m_post->BeginScene();
    m_renderer.Clear();
    const RuntimeSceneLoader::Scene::Environment& env = m_scene.environment;
    engine::PbrRenderer::Options opt;
    opt.ambient = m_sample.ambient + glm::vec3(0.04f);
    opt.ibl = &*m_ibl;
    opt.fog = env.fog;
    opt.fogDensity = env.fogDensity;
    opt.fogHeight = env.fogHeight;
    opt.fogHeightFalloff = env.fogHeightFalloff;
    // RM3: shadow parity — directional (sun) + point + spot shadows are all on by
    // default in Options, matching the editor's play view.
    // Let animated characters cast sun shadows too (skinned depth into the cascade).
    if (m_skinnedRenderer) {
        opt.shadowCasters = [this](const glm::mat4& lightViewProjection) {
            m_skinnedRenderer->DrawSceneDepth(m_registry, lightViewProjection);
        };
    }
    m_pbr->Render(m_registry, cam, aspect, w.Width(), w.Height(), opt);

    // Static-model pass: imported models (LoadedModelAsset) via their own shader.
    if (m_modelShader) {
        const glm::mat4 viewProj = cam.ProjectionMatrix(aspect) * cam.ViewMatrix();
        const float lightIntensity =
            std::max({m_sample.keyLightColor.x, m_sample.keyLightColor.y, m_sample.keyLightColor.z})
            * env.sunIntensity;
        m_modelShader->Bind();
        m_modelShader->SetMat4("uViewProj", viewProj);
        m_modelShader->SetVec3("uLightPos", cam.Position() + glm::vec3(-4.0f, 6.0f, 4.0f));
        m_modelShader->SetVec3("uLightColor", glm::vec3(1.0f));
        m_modelShader->SetVec3("uViewPos", cam.Position());
        engine::ecs::RenderLoadedModels(m_registry, *m_modelShader, viewProj,
                                        m_sample.keyLightDirection, lightIntensity);
    }

    // Skinned pass: animated characters (AnimatedModel), lit to match the PBR world.
    // Runs after PbrRenderer so the cascade/IBL textures it samples are ready.
    if (m_skinnedRenderer) {
        engine::SkinnedLighting lighting;
        lighting.sunDir = m_sample.keyLightDirection;
        lighting.sunColor = m_sample.keyLightColor * env.sunIntensity;
        lighting.ambient = m_sample.ambient * env.skyLightIntensity;
        lighting.cascade = &m_pbr->Cascade();
        lighting.ibl = &*m_ibl;
        lighting.tonemap = true;
        lighting.fog = env.fog;
        lighting.fogColor = m_sample.horizon;
        lighting.fogDensity = env.fogDensity;
        lighting.fogHeight = env.fogHeight;
        lighting.fogHeightFalloff = env.fogHeightFalloff;
        m_skinnedRenderer->DrawScene(m_registry, cam, aspect, lighting);
    }

    m_sky->Draw(cam.ViewMatrix(), cam.ProjectionMatrix(aspect), m_sample, false);
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
    m_post->RenderToScreen(w.Width(), w.Height(), m_dt);

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
        ? "WASD move   Space jump   Shift sprint   V view   mouse look   P pause   Esc quit"
        : "WASD move   Q/E down/up   hold RMB look   Shift sprint   P pause   Esc quit";
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
    m_text->End();
}

void RuntimePlayerApp::OnShutdown() {
    engine::ShutdownScripts(m_registry);   // OnDestroy() + release script instances
    m_runtimeAudio.Stop();
    m_audio.StopAllSounds();
    m_audio.StopMusic();
    m_audio.DestroyAllSources();
    m_config.Set("window.vsync", GetWindow().IsVSync());
    m_config.Save();
}
