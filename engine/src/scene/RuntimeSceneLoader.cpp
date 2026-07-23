#include "engine/scene/RuntimeSceneLoader.h"
#include "engine/assets/ParticleAsset.h"

#include "engine/ecs/Components.h"
#include "engine/ecs/Registry.h"
#include "engine/gameplay/GameplayComponents.h"
#include "engine/gameplay/Script.h"
#include "engine/graphics/DayNightCycle.h"
#include "engine/graphics/Mesh.h"
#include "engine/physics/PhysicsComponents.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace engine {
namespace {

const Mesh* MeshForPrimitive(const std::string& primitive, const RuntimeSceneLoader::PrimitiveMeshes& meshes) {
    if (primitive == "Empty") {
        return nullptr;
    }
    if (primitive == "Cube") {
        return meshes.cube;
    }
    if (primitive == "Plane") {
        return meshes.plane;
    }
    if (primitive == "Sphere") {
        return meshes.sphere;
    }
    if (primitive == "Capsule") {
        return meshes.capsule;
    }
    if (primitive == "Cylinder") {
        return meshes.cylinder;
    }
    if (primitive == "Cone") {
        return meshes.cone;
    }
    if (primitive == "Pyramid") return meshes.pyramid;
    if (primitive == "Torus") return meshes.torus;
    if (primitive == "Staircase") return meshes.staircase;
    return nullptr;
}

bool ParseLightType(const std::string& value, ecs::Light::Type* type) {
    if (value == "Directional") {
        *type = ecs::Light::Type::Directional;
        return true;
    }
    if (value == "Point") {
        *type = ecs::Light::Type::Point;
        return true;
    }
    if (value == "Spot") {
        *type = ecs::Light::Type::Spot;
        return true;
    }
    if (value == "Area") {
        *type = ecs::Light::Type::Area;
        return true;
    }
    return false;
}

ecs::Light EnvironmentSunLight(const RuntimeSceneLoader::Scene::Environment& environment) {
    const DayNightCycle::Sample sky = DayNightCycle::At(environment.timeOfDay);
    const glm::vec3 radiance = sky.keyLightColor * std::max(environment.sunIntensity, 0.0f);

    ecs::Light light;
    light.type = ecs::Light::Type::Directional;
    light.direction = sky.keyLightDirection;
    light.intensity = std::max(std::max(radiance.r, radiance.g), radiance.b);
    light.color = light.intensity > 0.0001f ? radiance / light.intensity : glm::vec3(1.0f);
    return light;
}

} // namespace

bool RuntimeSceneLoader::Load(const std::string &path, Scene *scene, std::string *error)
{
    if (!scene) {
        if (error) {
            *error = "Runtime scene output pointer was null.";
        }
        return false;
    }

    std::ifstream in(path);
    if (!in) {
        if (error) {
            *error = "Could not open runtime scene file for reading.";
        }
        return false;
    }

    std::string magic;
    int version = 0;
    in >> magic >> version;
    if (magic != "3DGRuntimeScene" || version < 1 || version > 61) {
        if (error) {
            *error = "Runtime scene file has an unknown format: "
                + magic + " " + std::to_string(version)
                + " (expected 3DGRuntimeScene 1..61).";
        }
        return false;
    }

    Scene loaded;
    std::string line;
    std::getline(in, line);
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }

        std::istringstream record(line);
        std::string recordType;
        record >> recordType;
        if (recordType == "skylight_occlusion" && version >= 54) {
            int enabled = 1;
            record >> enabled
                   >> loaded.environment.skylightOcclusionStrength
                   >> loaded.environment.minimumSkylight;
            if (!record) {
                if (error) *error = "Runtime scene contains invalid skylight occlusion settings.";
                return false;
            }
            loaded.environment.skylightOcclusion = enabled != 0;
            continue;
        }
        if (recordType == "clouds" && version >= 52) {
            int enabled = 1;
            record >> enabled
                   >> loaded.environment.cloudCoverage
                   >> loaded.environment.cloudDensity
                   >> loaded.environment.cloudScale
                   >> loaded.environment.cloudSoftness
                   >> loaded.environment.cloudWindSpeed
                   >> loaded.environment.cloudWindDirection
                   >> loaded.environment.cloudHorizonHeight
                   >> loaded.environment.cloudColor.r
                   >> loaded.environment.cloudColor.g
                   >> loaded.environment.cloudColor.b;
            if (!record) {
                if (error) *error = "Runtime scene contains an invalid clouds record.";
                return false;
            }
            loaded.environment.clouds = enabled != 0;
            if (version >= 53) {
                int shadows = 1;
                record >> shadows
                       >> loaded.environment.cloudShadowStrength
                       >> loaded.environment.cloudShadowScale;
                loaded.environment.cloudShadows = shadows != 0;
            }
            if (!record) {
                if (error) *error = "Runtime scene contains invalid cloud shadow settings.";
                return false;
            }
            continue;
        }
        if (recordType == "trigger_action" && version >= 51) {
            Scene::TriggerActionDesc action;
            int lockInput = 1, skippable = 1;
            record >> std::quoted(action.triggerName) >> std::quoted(action.targetName)
                   >> action.enterMover >> action.enterRotator
                   >> action.exitMover >> action.exitRotator
                   >> std::quoted(action.cameraSequence)
                   >> action.enterCamera >> action.exitCamera
                   >> lockInput >> skippable;
            if (!record || action.triggerName.empty()) {
                if (error) *error = "Runtime scene contains an invalid trigger action.";
                return false;
            }
            action.cameraLockInput = lockInput != 0;
            action.cameraSkippable = skippable != 0;
            loaded.triggerActions.push_back(std::move(action));
            continue;
        }
        if (recordType == "camera_zone" && version >= 51) {
            Scene::CameraZoneDesc zone;
            int restore = 1;
            record >> std::quoted(zone.triggerName) >> std::quoted(zone.presetName)
                   >> restore >> zone.priority >> zone.returnBlend;
            if (!record || zone.triggerName.empty() || zone.presetName.empty()) {
                if (error) *error = "Runtime scene contains an invalid camera zone.";
                return false;
            }
            zone.restoreOnExit = restore != 0;
            zone.returnBlend = std::max(zone.returnBlend, 0.0f);
            loaded.cameraZones.push_back(std::move(zone));
            continue;
        }
        if (recordType == "physics_joint" && version >= 51) {
            Scene::PhysicsJointDesc joint;
            int worldAnchor = 0, rope = 0;
            record >> joint.type >> std::quoted(joint.objectA) >> std::quoted(joint.objectB)
                   >> worldAnchor
                   >> joint.anchor.x >> joint.anchor.y >> joint.anchor.z
                   >> joint.restLength >> rope >> joint.stiffness >> joint.damping;
            if (!record || joint.objectA.empty()) {
                if (error) *error = "Runtime scene contains an invalid physics joint.";
                return false;
            }
            joint.type = std::clamp(joint.type, 0, 1);
            joint.worldAnchor = worldAnchor != 0;
            joint.rope = rope != 0;
            joint.restLength = std::max(joint.restLength, 0.0f);
            loaded.physicsJoints.push_back(std::move(joint));
            continue;
        }
        if (recordType == "terrain" && version >= 51) {
            Scene::TerrainDesc terrain;
            std::size_t heightCount = 0, paintCount = 0;
            record >> std::quoted(terrain.entityName)
                   >> terrain.resolution >> terrain.size >> terrain.maxHeight
                   >> terrain.seed >> terrain.octaves >> terrain.frequency
                   >> heightCount;
            terrain.heights.resize(heightCount);
            for (float& height : terrain.heights) record >> height;
            record >> paintCount;
            terrain.paint.resize(paintCount);
            for (unsigned char& paint : terrain.paint) {
                unsigned value = 0;
                record >> value;
                paint = static_cast<unsigned char>(std::min(value, 255u));
            }
            if (!record || terrain.entityName.empty()) {
                if (error) *error = "Runtime scene contains invalid terrain data.";
                return false;
            }
            terrain.resolution = std::clamp(terrain.resolution, 2, 1024);
            terrain.size = std::max(terrain.size, 0.01f);
            terrain.maxHeight = std::max(terrain.maxHeight, 0.0f);
            terrain.octaves = std::max(terrain.octaves, 1);
            loaded.terrains.push_back(std::move(terrain));
            continue;
        }
        if (recordType == "nav_bounds" && version >= 50) {
            Scene::NavBounds bounds;
            record >> bounds.position.x >> bounds.position.y >> bounds.position.z
                   >> bounds.scale.x >> bounds.scale.y >> bounds.scale.z
                   >> bounds.rotation.w >> bounds.rotation.x
                   >> bounds.rotation.y >> bounds.rotation.z;
            if (!record) {
                if (error) *error = "Runtime scene contains invalid navigation bounds.";
                return false;
            }
            bounds.scale = glm::max(glm::abs(bounds.scale), glm::vec3(0.001f));
            loaded.navBounds.push_back(std::move(bounds));
            continue;
        }
        if (recordType == "nav_agent" && version >= 50) {
            Scene::NavAgentDesc agent;
            int autoTarget = 0;
            std::size_t patrolCount = 0;
            record >> std::quoted(agent.entityName)
                   >> agent.speed >> agent.maxForce
                   >> agent.reachRadius >> agent.repathInterval
                   >> std::quoted(agent.targetName)
                   >> agent.visionRange >> agent.visionHalfAngle
                   >> std::quoted(agent.brainAsset)
                   >> agent.team >> autoTarget >> patrolCount;
            agent.autoTarget = autoTarget != 0;
            if (agent.brainAsset == "-") agent.brainAsset.clear();
            agent.patrolPoints.resize(patrolCount);
            for (glm::vec3& point : agent.patrolPoints)
                record >> point.x >> point.y >> point.z;
            if (version >= 57) {
                int movementMode = 0;
                record >> movementMode
                       >> agent.movementGravity
                       >> agent.movementMaxFallSpeed
                       >> agent.movementGroundProbe
                       >> agent.movementStepHeight
                       >> agent.movementMaxSlope;
                agent.movementMode = movementMode == static_cast<int>(ai::AiMovementMode::Flying)
                    ? ai::AiMovementMode::Flying : ai::AiMovementMode::Grounded;
            }
            if (!record || agent.entityName.empty()) {
                if (error) *error = "Runtime scene contains an invalid navigation agent.";
                return false;
            }
            agent.speed = std::max(agent.speed, 0.0f);
            agent.maxForce = std::max(agent.maxForce, 0.0f);
            agent.reachRadius = std::max(agent.reachRadius, 0.05f);
            agent.repathInterval = std::max(agent.repathInterval, 0.05f);
            agent.visionRange = std::max(agent.visionRange, 0.0f);
            agent.movementGravity = std::min(agent.movementGravity, 0.0f);
            agent.movementMaxFallSpeed = std::max(agent.movementMaxFallSpeed, 0.0f);
            agent.movementGroundProbe = std::max(agent.movementGroundProbe, 0.02f);
            agent.movementStepHeight = std::max(agent.movementStepHeight, 0.0f);
            agent.movementMaxSlope = std::clamp(agent.movementMaxSlope, 0.0f, 89.0f);
            loaded.navAgents.push_back(std::move(agent));
            continue;
        }
        if (recordType == "camera" && version >= 49) {
            Scene::CameraPreset camera;
            int primary = 0;
            int useInPlay = 0;
            record >> std::quoted(camera.name)
                   >> camera.position.x >> camera.position.y >> camera.position.z
                   >> camera.target.x >> camera.target.y >> camera.target.z
                   >> camera.fov >> camera.nearPlane >> camera.farPlane
                   >> camera.blendDuration >> camera.blendEasing
                   >> primary >> useInPlay;
            if (!record || camera.name.empty()) {
                if (error) *error = "Runtime scene contains an invalid camera preset.";
                return false;
            }
            camera.fov = std::clamp(camera.fov, 10.0f, 120.0f);
            camera.nearPlane = std::max(camera.nearPlane, 0.001f);
            camera.farPlane = std::max(camera.farPlane, camera.nearPlane);
            camera.blendDuration = std::max(camera.blendDuration, 0.0f);
            camera.blendEasing = std::clamp(camera.blendEasing, 0, 3);
            camera.primary = primary != 0;
            camera.useInPlay = useInPlay != 0;
            loaded.cameraPresets.push_back(std::move(camera));
            continue;
        }
        if (recordType == "camera_sequence" && version >= 49) {
            Scene::CameraSequence sequence;
            int loop = 0;
            std::size_t shotCount = 0;
            record >> std::quoted(sequence.name) >> loop >> shotCount;
            sequence.loop = loop != 0;
            sequence.shots.reserve(shotCount);
            for (std::size_t i = 0; i < shotCount; ++i) {
                Scene::CameraSequenceShot shot;
                record >> std::quoted(shot.cameraName)
                       >> shot.travelDuration >> shot.holdDuration
                       >> shot.easing >> shot.pathMode
                       >> std::quoted(shot.eventName);
                shot.travelDuration = std::max(shot.travelDuration, 0.0f);
                shot.holdDuration = std::max(shot.holdDuration, 0.0f);
                shot.easing = std::clamp(shot.easing, 0, 3);
                shot.pathMode = std::clamp(shot.pathMode, 0, 1);
                sequence.shots.push_back(std::move(shot));
            }
            std::size_t cueCount = 0;
            record >> cueCount;
            sequence.cues.reserve(cueCount);
            for (std::size_t i = 0; i < cueCount; ++i) {
                Scene::CinematicCue cue;
                record >> cue.type >> cue.time
                       >> std::quoted(cue.name)
                       >> std::quoted(cue.assetPath)
                       >> std::quoted(cue.targetObject)
                       >> std::quoted(cue.animationClip)
                       >> cue.volume;
                cue.type = std::clamp(cue.type, 0, 2);
                cue.time = std::max(cue.time, 0.0f);
                cue.volume = std::max(cue.volume, 0.0f);
                sequence.cues.push_back(std::move(cue));
            }
            if (!record || sequence.name.empty()) {
                if (error) *error = "Runtime scene contains an invalid camera sequence.";
                return false;
            }
            loaded.cameraSequences.push_back(std::move(sequence));
            continue;
        }
        if (recordType == "post_effect" && version >= 46) {
            Scene::Environment::PostProcessEffect effect;
            int enabled = 1;
            std::size_t parameterCount = 0;
            record >> std::quoted(effect.shaderPath)
                   >> enabled >> parameterCount;
            effect.enabled = enabled != 0;
            for (std::size_t i = 0; i < parameterCount; ++i) {
                Scene::Environment::PostProcessParameter parameter;
                record >> std::quoted(parameter.name)
                       >> parameter.type
                       >> std::quoted(parameter.value);
                effect.parameters.push_back(std::move(parameter));
            }
            if (!record) {
                if (error) *error =
                    "Runtime scene contains an invalid post-process effect.";
                return false;
            }
            loaded.environment.postProcessEffects.push_back(std::move(effect));
            continue;
        }
        if (recordType == "material_overrides") {
            std::string entityName;
            std::size_t count = 0;
            record >> std::quoted(entityName) >> count;
            auto entity = std::find_if(loaded.entities.begin(), loaded.entities.end(),
                [&](const EntityDesc& candidate) {
                    return candidate.name == entityName;
                });
            for (std::size_t i = 0; i < count; ++i) {
                std::string name, value;
                record >> std::quoted(name) >> std::quoted(value);
                if (entity != loaded.entities.end())
                    entity->materialParameterOverrides[name] = value;
            }
            continue;
        }
        if (recordType == "player_controller" && version >= 48) {
            std::string entityName;
            record >> std::quoted(entityName);
            int firstPerson = 0, cameraCollision = 1, shoulderCamera = 0, rightShoulder = 1, lockOnEnabled = 0;
            PlayerControllerDesc pc;
            record >> firstPerson
                   >> pc.walkSpeed >> pc.runSpeed >> pc.jumpSpeed
                   >> pc.lookSensitivity
                   >> pc.capsuleRadius >> pc.capsuleHeight >> pc.eyeHeight
                   >> pc.cameraDistance >> pc.cameraTargetHeight
                   >> cameraCollision
                   >> pc.cameraProbeRadius >> pc.cameraCollisionPadding >> pc.cameraReturnSpeed
                   >> shoulderCamera
                   >> pc.shoulderOffset >> pc.shoulderSwitchSpeed
                   >> rightShoulder
                   >> lockOnEnabled
                   >> pc.lockOnRange >> pc.lockOnViewAngle
                   >> pc.lockOnTargetHeight >> pc.lockOnTrackingSpeed
                   >> pc.maxSlopeDegrees >> pc.stepHeight;
            pc.firstPerson = firstPerson != 0;
            pc.cameraCollision = cameraCollision != 0;
            pc.shoulderCamera = shoulderCamera != 0;
            pc.rightShoulder = rightShoulder != 0;
            pc.lockOnEnabled = lockOnEnabled != 0;
            auto entity = std::find_if(loaded.entities.begin(), loaded.entities.end(),
                [&](const EntityDesc& candidate) { return candidate.name == entityName; });
            if (entity != loaded.entities.end()) {
                entity->playerControllerEnabled = true;
                entity->playerController = pc;
            }
            continue;
        }
        if (recordType == "environment") {
            if (version < 5) {
                continue;
            }

            int fog = 1;
            int driveSun = 1;
            int physicsBroadPhase = loaded.environment.physicsBroadPhase ? 1 : 0;
            int physicsAllowSleeping = loaded.environment.physicsAllowSleeping ? 1 : 0;
            record >> loaded.environment.timeOfDay
                   >> loaded.environment.skyLightIntensity;
            if (version >= 6) {
                record >> driveSun
                       >> loaded.environment.sunIntensity;
            }
            record >> fog
                   >> loaded.environment.fogDensity
                   >> loaded.environment.fogHeight
                   >> loaded.environment.fogHeightFalloff;
            if (version >= 9) {
                record >> loaded.environment.physicsGravity.x
                       >> loaded.environment.physicsGravity.y
                       >> loaded.environment.physicsGravity.z
                       >> loaded.environment.physicsSolverIterations
                       >> physicsBroadPhase
                       >> loaded.environment.physicsCellSize
                       >> loaded.environment.physicsRestitutionThreshold
                       >> physicsAllowSleeping
                       >> loaded.environment.physicsSleepLinearVelocity;
                if (version >= 10) {
                    record >> loaded.environment.physicsSleepAngularVelocity;
                }
                record >> loaded.environment.physicsTimeToSleep;
                if (!record) {
                    record.clear();
                    loaded.environment.physicsGravity = glm::vec3(0.0f, -9.81f, 0.0f);
                    loaded.environment.physicsSolverIterations = 4;
                    physicsBroadPhase = 1;
                    loaded.environment.physicsCellSize = 2.0f;
                    loaded.environment.physicsRestitutionThreshold = 0.5f;
                    physicsAllowSleeping = 1;
                    loaded.environment.physicsSleepLinearVelocity = 0.06f;
                    loaded.environment.physicsSleepAngularVelocity = 0.15f;
                    loaded.environment.physicsTimeToSleep = 0.5f;
                }
            }
            if (version >= 47) {
                std::string hud;
                record >> std::quoted(hud);
                if (record) loaded.environment.hudAsset = hud;
                else record.clear();
            }
            if (version >= 56) {
                record >> loaded.environment.shadowDistance;
                loaded.environment.shadowDistance = std::clamp(
                    loaded.environment.shadowDistance, 10.0f, 5000.0f);
            }
            loaded.environment.driveSunLight = driveSun != 0;
            loaded.environment.fog = fog != 0;
            loaded.environment.physicsBroadPhase = physicsBroadPhase != 0;
            loaded.environment.physicsAllowSleeping = physicsAllowSleeping != 0;
            continue;
        }

        if (recordType == "light") {
            if (version < 4) {
                if (error) {
                    *error = "Runtime scene file contains a light record, but the file version is "
                        + std::to_string(version) + " (expected 4 or higher).";
                }
                return false;
            }
            Scene::LightDesc desc;
            std::string typeName;
            record >> std::quoted(desc.name) >> typeName
                   >> desc.position.x >> desc.position.y >> desc.position.z
                   >> desc.light.color.r >> desc.light.color.g >> desc.light.color.b
                   >> desc.light.intensity
                   >> desc.light.direction.x >> desc.light.direction.y >> desc.light.direction.z
                   >> desc.light.innerAngle >> desc.light.outerAngle >> desc.light.range >> desc.light.sourceRadius;

            if (!record || !ParseLightType(typeName, &desc.light.type)) {
                if (error) {
                    *error = "Runtime scene contains an invalid light record.";
                }
                return false;
            }

            loaded.lights.push_back(desc);
            continue;
        }
        if (recordType != "entity") {
            continue;
        }

        EntityDesc entity;
        record >> entity.primitive >> std::quoted(entity.name)
               >> entity.position.x >> entity.position.y >> entity.position.z
               >> entity.scale.x >> entity.scale.y >> entity.scale.z
               >> entity.rotation.w >> entity.rotation.x >> entity.rotation.y >> entity.rotation.z
               >> entity.color.r >> entity.color.g >> entity.color.b;

        if (version >= 2) {
            record >> std::quoted(entity.modelPath) >> std::quoted(entity.materialPath);
            if (entity.modelPath == "-") {
                entity.modelPath.clear();
            }
            if (entity.materialPath == "-") {
                entity.materialPath.clear();
            }
        }
        if (version >= 60) {
            record >> entity.modelOrientationEuler.x
                   >> entity.modelOrientationEuler.y
                   >> entity.modelOrientationEuler.z;
        }
        if (version >= 61) {
            record >> entity.modelOffsetPosition.x
                   >> entity.modelOffsetPosition.y
                   >> entity.modelOffsetPosition.z
                   >> entity.modelOffsetScale.x
                   >> entity.modelOffsetScale.y
                   >> entity.modelOffsetScale.z;
        }
        if (version >= 17) {
            int skeletalModel = 0;
            int animationAutoplay = 1;
            int animationLoop = 1;
            record >> skeletalModel
                   >> entity.animationClipIndex
                   >> std::quoted(entity.animationClipName)
                   >> animationAutoplay
                   >> animationLoop
                   >> entity.animationSpeed;
            entity.skeletalModel = skeletalModel != 0;
            entity.animationClipIndex = std::max(entity.animationClipIndex, 0);
            if (entity.animationClipName == "-") {
                entity.animationClipName.clear();
            }
            entity.animationAutoplay = animationAutoplay != 0;
            entity.animationLoop = animationLoop != 0;
            entity.animationSpeed = std::max(entity.animationSpeed, 0.0f);
        }
        if (version >= 18) {
            int animationLocomotionEnabled = 0;
            record >> animationLocomotionEnabled
                   >> entity.animationIdleClipIndex
                   >> std::quoted(entity.animationIdleClipName)
                   >> entity.animationWalkClipIndex
                   >> std::quoted(entity.animationWalkClipName)
                   >> entity.animationRunClipIndex
                   >> std::quoted(entity.animationRunClipName)
                   >> entity.animationWalkAt
                   >> entity.animationRunAt;
            entity.animationLocomotionEnabled = animationLocomotionEnabled != 0;
            entity.animationIdleClipIndex = std::max(entity.animationIdleClipIndex, 0);
            entity.animationWalkClipIndex = std::max(entity.animationWalkClipIndex, 0);
            entity.animationRunClipIndex = std::max(entity.animationRunClipIndex, 0);
            if (entity.animationIdleClipName == "-") {
                entity.animationIdleClipName.clear();
            }
            if (entity.animationWalkClipName == "-") {
                entity.animationWalkClipName.clear();
            }
            if (entity.animationRunClipName == "-") {
                entity.animationRunClipName.clear();
            }
            entity.animationWalkAt = std::max(entity.animationWalkAt, 0.0f);
            entity.animationRunAt = std::max(entity.animationRunAt, entity.animationWalkAt);
        }
        if (version >= 19) {
            std::size_t eventCount = 0;
            record >> eventCount;
            for (std::size_t i = 0; i < eventCount; ++i) {
                AnimationEventDesc event;
                record >> event.clipIndex
                       >> event.time
                       >> std::quoted(event.name);
                event.clipIndex = std::max(event.clipIndex, 0);
                event.time = std::max(event.time, 0.0f);
                if (event.name == "-") {
                    event.name.clear();
                }
                entity.animationEvents.push_back(event);
            }
        }
        if (version >= 20) {
            std::size_t profileCount = 0;
            record >> profileCount;
            for (std::size_t i = 0; i < profileCount; ++i) {
                AnimationActionProfileDesc profile;
                record >> std::quoted(profile.name)
                       >> profile.clipIndex
                       >> std::quoted(profile.clipName)
                       >> std::quoted(profile.maskRootBone)
                       >> profile.fadeIn
                       >> profile.fadeOut
                       >> profile.speed;
                if (profile.name == "-") {
                    profile.name.clear();
                }
                if (profile.clipName == "-") {
                    profile.clipName.clear();
                }
                if (profile.maskRootBone == "-") {
                    profile.maskRootBone.clear();
                }
                profile.clipIndex = std::max(profile.clipIndex, 0);
                profile.fadeIn = std::max(profile.fadeIn, 0.0f);
                profile.fadeOut = std::max(profile.fadeOut, 0.0f);
                profile.speed = std::max(profile.speed, 0.0f);
                entity.animationActionProfiles.push_back(profile);
            }
        }
        if (version >= 21) {
            std::size_t stateCount = 0;
            record >> stateCount;
            for (std::size_t i = 0; i < stateCount; ++i) {
                AnimationStateDesc state;
                int loop = 1;
                record >> std::quoted(state.name)
                       >> state.clipIndex
                       >> std::quoted(state.clipName)
                       >> loop
                       >> state.speed;
                if (version >= 24) {
                    int rootMotion = 0;
                    record >> state.blendClipIndex
                           >> std::quoted(state.blendClipName)
                           >> std::quoted(state.blendParameter)
                           >> state.blendMin
                           >> state.blendMax
                           >> rootMotion;
                    if (state.blendClipName == "-") state.blendClipName.clear();
                    if (state.blendParameter == "-") state.blendParameter.clear();
                    state.rootMotion = rootMotion != 0;
                    if (version >= 58) {
                        if (version >= 59) {
                            int is2D = 0, synchronize = 1;
                            record >> is2D >> std::quoted(state.blendParameterY) >> synchronize;
                            if (state.blendParameterY == "-") state.blendParameterY.clear();
                            state.blendSpace2D = is2D != 0;
                            state.synchronizeBlendSpace = synchronize != 0;
                        }
                        std::size_t sampleCount = 0;
                        record >> sampleCount;
                        for (std::size_t sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex) {
                            AnimationStateDesc::BlendSampleDesc sample;
                            record >> sample.clipIndex >> std::quoted(sample.clipName) >> sample.value;
                            if (version >= 59) record >> sample.valueY;
                            if (sample.clipName == "-") sample.clipName.clear();
                            sample.clipIndex = std::max(sample.clipIndex, 0);
                            state.blendSamples.push_back(std::move(sample));
                        }
                    }
                }
                if (state.name == "-") {
                    state.name.clear();
                }
                if (state.clipName == "-") {
                    state.clipName.clear();
                }
                state.clipIndex = std::max(state.clipIndex, 0);
                state.loop = loop != 0;
                state.speed = std::max(state.speed, 0.0f);
                entity.animationStates.push_back(state);
            }

            if (version >= 24) {
                std::size_t parameterCount = 0;
                record >> parameterCount;
                for (std::size_t i = 0; i < parameterCount; ++i) {
                    AnimationParameterDesc parameter;
                    record >> std::quoted(parameter.name) >> parameter.type >> parameter.defaultValue;
                    if (parameter.name == "-") parameter.name.clear();
                    parameter.type = std::clamp(parameter.type, 0, 2);
                    entity.animationParameters.push_back(parameter);
                }
            }

            std::size_t transitionCount = 0;
            record >> transitionCount;
            for (std::size_t i = 0; i < transitionCount; ++i) {
                AnimationTransitionDesc transition;
                record >> std::quoted(transition.fromState)
                       >> std::quoted(transition.toState)
                       >> std::quoted(transition.parameter)
                       >> transition.compare
                       >> transition.threshold
                       >> transition.fade;
                if (version >= 22) {
                    record >> transition.exitTime;
                }
                if (version >= 23) {
                    record >> transition.priority
                           >> transition.canInterrupt;
                }
                if (transition.fromState == "-") {
                    transition.fromState.clear();
                }
                if (transition.toState == "-") {
                    transition.toState.clear();
                }
                if (transition.parameter == "-") {
                    transition.parameter.clear();
                }
                transition.compare = std::clamp(transition.compare, 0, 3);
                transition.fade = std::max(transition.fade, 0.0f);
                transition.exitTime = std::clamp(transition.exitTime, 0.0f, 1.0f);
                entity.animationTransitions.push_back(transition);
            }
        }
        if (version >= 3) {
            record >> entity.linearVelocity.x >> entity.linearVelocity.y >> entity.linearVelocity.z
                   >> entity.angularVelocityAxis.x >> entity.angularVelocityAxis.y >> entity.angularVelocityAxis.z
                   >> entity.angularVelocityRadians;
        }
        if (version >= 7) {
            int linearVelocityEnabled = 0;
            int angularVelocityEnabled = 0;
            record >> linearVelocityEnabled >> angularVelocityEnabled;
            entity.linearVelocityEnabled = linearVelocityEnabled != 0;
            entity.angularVelocityEnabled = angularVelocityEnabled != 0;
        } else {
            entity.linearVelocityEnabled = glm::dot(entity.linearVelocity, entity.linearVelocity) > 0.0f;
            entity.angularVelocityEnabled = entity.angularVelocityRadians != 0.0f
                && glm::dot(entity.angularVelocityAxis, entity.angularVelocityAxis) > 0.0f;
        }
        if (version >= 8) {
            int rigidBodyEnabled = 0;
            int rigidBodyUseGravity = entity.rigidBody.useGravity ? 1 : 0;
            int rigidBodyAllowSleep = entity.rigidBody.allowSleep ? 1 : 0;
            int rigidBodyCcd = entity.rigidBody.ccd ? 1 : 0;
            int rigidBodyFreezeRotation = entity.rigidBody.freezeRotation ? 1 : 0;
            int colliderEnabled = 0;
            int colliderShape = static_cast<int>(ecs::ColliderShape::Sphere);
            int colliderTrigger = entity.collider.isTrigger ? 1 : 0;
            record >> rigidBodyEnabled
                >> entity.rigidBody.velocity.x >> entity.rigidBody.velocity.y >> entity.rigidBody.velocity.z
                >> entity.rigidBody.invMass
                >> rigidBodyUseGravity
                >> rigidBodyAllowSleep
                >> rigidBodyCcd;
            if (version >= 10) {
                record >> rigidBodyFreezeRotation;
            }
            record >> colliderEnabled
                   >> colliderShape
                   >> entity.collider.radius;
            if (version >= 13) {
                record >> entity.collider.halfHeight;
            }
            if (version >= 25) {
                record >> entity.collider.majorRadius >> entity.collider.minorRadius >> entity.collider.steps;
            }
            record
                   >> entity.collider.halfExtents.x >> entity.collider.halfExtents.y >> entity.collider.halfExtents.z
                   >> entity.collider.planeNormal.x >> entity.collider.planeNormal.y >> entity.collider.planeNormal.z
                   >> entity.collider.planeOffset
                   >> entity.collider.restitution
                   >> entity.collider.friction
                   >> colliderTrigger;
            entity.rigidBodyEnabled = rigidBodyEnabled != 0;
            entity.rigidBody.useGravity = rigidBodyUseGravity != 0;
            entity.rigidBody.allowSleep = rigidBodyAllowSleep != 0;
            entity.rigidBody.ccd = rigidBodyCcd != 0;
            entity.rigidBody.freezeRotation = version >= 10 && rigidBodyFreezeRotation != 0;
            entity.colliderEnabled = colliderEnabled != 0;
            entity.collider.isTrigger = colliderTrigger != 0;
            if (colliderShape >= static_cast<int>(ecs::ColliderShape::Sphere)
                && colliderShape <= static_cast<int>(ecs::ColliderShape::Staircase))
                entity.collider.shape = static_cast<ecs::ColliderShape>(colliderShape);
            else
                entity.collider.shape = ecs::ColliderShape::Sphere;
        }
        if (version >= 55) {
            record >> entity.collider.layer >> entity.collider.mask;
        }
        if (version >= 11) {
            int rotatorEnabled = 0;
            record >> rotatorEnabled
                   >> entity.rotator.axis.x >> entity.rotator.axis.y >> entity.rotator.axis.z
                   >> entity.rotator.radiansPerSecond;
            entity.rotatorEnabled = rotatorEnabled != 0;
        }
        if (version >= 12) {
            int moverEnabled = 0;
            record >> moverEnabled
                   >> entity.mover.axis.x >> entity.mover.axis.y >> entity.mover.axis.z
                   >> entity.mover.distance
                   >> entity.mover.speed
                   >> entity.mover.phase;
            entity.moverEnabled = moverEnabled != 0;
        }
        if (version >= 16) {
            int healthEnabled = 0;
            int healthAlive = entity.health.alive ? 1 : 0;
            record >> healthEnabled
                   >> entity.health.hp
                   >> entity.health.maxHp
                   >> healthAlive;
            entity.healthEnabled = healthEnabled != 0;
            entity.health.maxHp = std::max(entity.health.maxHp, 1.0f);
            entity.health.hp = std::clamp(entity.health.hp, 0.0f, entity.health.maxHp);
            entity.health.alive = healthAlive != 0 && entity.health.hp > 0.0f;
            entity.health.justDied = false;
        }
        if (version >= 14) {
            int scriptEnabled = 0;
            record >> scriptEnabled
                   >> std::quoted(entity.scriptClassName)
                   >> std::quoted(entity.scriptPath);
            if (entity.scriptClassName == "-") {
                entity.scriptClassName.clear();
            }
            if (entity.scriptPath == "-") {
                entity.scriptPath.clear();
            }
            entity.scriptEnabled = scriptEnabled != 0;
            if (version >= 15) {
                std::size_t scriptFieldCount = 0;
                record >> scriptFieldCount;
                for (std::size_t i = 0; i < scriptFieldCount; ++i) {
                    ScriptField field;
                    int fieldType = 0;
                    record >> std::quoted(field.name) >> fieldType >> std::quoted(field.value);
                    if (field.name == "-") {
                        field.name.clear();
                    }
                    if (field.value == "-") {
                        field.value.clear();
                    }
                    if (fieldType == static_cast<int>(ScriptField::Type::Int)) {
                        field.type = ScriptField::Type::Int;
                    } else if (fieldType == static_cast<int>(ScriptField::Type::Bool)) {
                        field.type = ScriptField::Type::Bool;
                    } else if (fieldType == static_cast<int>(ScriptField::Type::String)) {
                        field.type = ScriptField::Type::String;
                    } else {
                        field.type = ScriptField::Type::Float;
                    }
                    entity.scriptFields.push_back(field);
                }
            }
        }
        if (version >= 26) {
            int audioSourceEnabled = 0;
            int audioSpatial = 1;
            int audioLoop = 0;
            int audioAutoplay = 1;
            record >> audioSourceEnabled
                   >> std::quoted(entity.audioSource.path)
                   >> entity.audioSource.volume
                   >> entity.audioSource.pitch
                   >> audioSpatial
                   >> audioLoop
                   >> audioAutoplay
                   >> entity.audioSource.minDistance
                   >> entity.audioSource.maxDistance
                   >> entity.audioSource.rolloff;
            if (entity.audioSource.path == "-") entity.audioSource.path.clear();
            entity.audioSourceEnabled = audioSourceEnabled != 0;
            entity.audioSource.volume = std::max(entity.audioSource.volume, 0.0f);
            entity.audioSource.pitch = std::max(entity.audioSource.pitch, 0.01f);
            entity.audioSource.spatial = audioSpatial != 0;
            entity.audioSource.loop = audioLoop != 0;
            entity.audioSource.autoplay = audioAutoplay != 0;
            entity.audioSource.minDistance = std::max(entity.audioSource.minDistance, 0.01f);
            entity.audioSource.maxDistance = std::max(entity.audioSource.maxDistance,
                                                       entity.audioSource.minDistance);
            entity.audioSource.rolloff = std::max(entity.audioSource.rolloff, 0.0f);
        }
        if (version >= 27) {
            int enterAudioAction = 0;
            int exitAudioAction = 0;
            record >> std::quoted(entity.triggerAudio.targetName) >> enterAudioAction >> exitAudioAction;
            if (entity.triggerAudio.targetName == "-") entity.triggerAudio.targetName.clear();
            enterAudioAction = std::clamp(enterAudioAction,
                static_cast<int>(ecs::AudioAction::None), static_cast<int>(ecs::AudioAction::Stop));
            exitAudioAction = std::clamp(exitAudioAction,
                static_cast<int>(ecs::AudioAction::None), static_cast<int>(ecs::AudioAction::Stop));
            entity.triggerAudio.onEnter = static_cast<ecs::AudioAction>(enterAudioAction);
            entity.triggerAudio.onExit = static_cast<ecs::AudioAction>(exitAudioAction);
            entity.triggerAudioEnabled = !entity.triggerAudio.targetName.empty()
                && (entity.triggerAudio.onEnter != ecs::AudioAction::None
                    || entity.triggerAudio.onExit != ecs::AudioAction::None);
        }
        if (version >= 28) {
            int audioBus = static_cast<int>(AudioBus::SFX);
            record >> audioBus;
            audioBus = std::clamp(audioBus, static_cast<int>(AudioBus::Master),
                                  static_cast<int>(AudioBus::Ambient));
            entity.audioSource.bus = static_cast<AudioBus>(audioBus);
        }
        if (version >= 44) {
            record >> entity.audioSource.dopplerFactor
                   >> entity.audioSource.coneInnerAngle
                   >> entity.audioSource.coneOuterAngle
                   >> entity.audioSource.coneOuterGain
                   >> entity.audioSource.occlusion
                   >> entity.audioSource.priority;
            entity.audioSource.dopplerFactor = std::max(entity.audioSource.dopplerFactor, 0.0f);
            entity.audioSource.coneInnerAngle =
                std::clamp(entity.audioSource.coneInnerAngle, 0.0f, 360.0f);
            entity.audioSource.coneOuterAngle =
                std::clamp(entity.audioSource.coneOuterAngle,
                    entity.audioSource.coneInnerAngle, 360.0f);
            entity.audioSource.coneOuterGain =
                std::clamp(entity.audioSource.coneOuterGain, 0.0f, 1.0f);
            entity.audioSource.occlusion = std::clamp(entity.audioSource.occlusion, 0.0f, 1.0f);
            entity.audioSource.priority = std::clamp(entity.audioSource.priority, 0, 100);
        }
        if (version >= 29) {
            int particleEnabled = 0;
            int shape = static_cast<int>(entity.particleSystem.config.shape);
            int blend = static_cast<int>(entity.particleSystem.config.blend);
            int autoplay = 1;
            int loop = 1;
            int localSpace = 1;
            int prewarm = 0;
            EmitterConfig& particle = entity.particleSystem.config;
            record >> particleEnabled
                   >> particle.rate >> particle.maxParticles
                   >> shape >> particle.shapeRadius
                   >> particle.direction.x >> particle.direction.y >> particle.direction.z
                   >> particle.coneAngleDeg
                   >> particle.speedMin >> particle.speedMax
                   >> particle.lifeMin >> particle.lifeMax
                   >> particle.gravity.x >> particle.gravity.y >> particle.gravity.z
                   >> particle.drag
                   >> particle.startColor.r >> particle.startColor.g
                   >> particle.startColor.b >> particle.startColor.a
                   >> particle.endColor.r >> particle.endColor.g
                   >> particle.endColor.b >> particle.endColor.a
                   >> particle.startSize >> particle.endSize
                   >> blend >> autoplay >> loop
                   >> entity.particleSystem.duration
                   >> entity.particleSystem.startDelay
                   >> entity.particleSystem.simulationSpeed
                   >> localSpace
                   >> entity.particleSystem.burstCount
                   >> entity.particleSystem.burstInterval;
            if (version >= 30) record >> prewarm;
            if (version >= 31) {
                int useSizeCurve = 0, useColorCurve = 0, textureLoop = 1;
                record >> particle.rotationMinDeg >> particle.rotationMaxDeg
                       >> particle.angularVelocityMinDeg >> particle.angularVelocityMaxDeg
                       >> useSizeCurve >> useColorCurve;
                for (float& key : particle.sizeCurve) record >> key;
                for (float& key : particle.colorCurve) record >> key;
                record >> std::quoted(particle.texturePath) >> particle.textureColumns >> particle.textureRows
                       >> particle.textureFps >> textureLoop;
                if (particle.texturePath == "-") particle.texturePath.clear();
                particle.useSizeCurve = useSizeCurve != 0;
                particle.useColorCurve = useColorCurve != 0;
                particle.textureLoop = textureLoop != 0;
            }
            if (version >= 32) {
                int cullingEnabled = 1;
                record >> cullingEnabled >> particle.boundsRadius;
                particle.cullingEnabled = cullingEnabled != 0;
            }
            shape = std::clamp(shape, static_cast<int>(EmitShape::Point),
                               static_cast<int>(EmitShape::Cone));
            blend = std::clamp(blend, static_cast<int>(ParticleBlend::Additive),
                               static_cast<int>(ParticleBlend::Alpha));
            particle.shape = static_cast<EmitShape>(shape);
            particle.blend = static_cast<ParticleBlend>(blend);
            particle.rate = std::max(particle.rate, 0.0f);
            particle.maxParticles = std::max(particle.maxParticles, 1);
            particle.shapeRadius = std::max(particle.shapeRadius, 0.0f);
            if (particle.speedMin > particle.speedMax) std::swap(particle.speedMin, particle.speedMax);
            particle.lifeMin = std::max(particle.lifeMin, 0.001f);
            particle.lifeMax = std::max(particle.lifeMax, particle.lifeMin);
            particle.drag = std::max(particle.drag, 0.0f);
            particle.textureColumns = std::max(particle.textureColumns, 1);
            particle.textureRows = std::max(particle.textureRows, 1);
            particle.textureFps = std::max(particle.textureFps, 0.0f);
            particle.boundsRadius = std::max(particle.boundsRadius, 0.01f);
            entity.particleSystemEnabled = particleEnabled != 0;
            entity.particleSystem.autoplay = autoplay != 0;
            entity.particleSystem.loop = loop != 0;
            entity.particleSystem.prewarm = prewarm != 0;
            entity.particleSystem.duration = std::max(entity.particleSystem.duration, 0.0f);
            entity.particleSystem.startDelay = std::max(entity.particleSystem.startDelay, 0.0f);
            entity.particleSystem.simulationSpeed = std::max(entity.particleSystem.simulationSpeed, 0.0f);
            entity.particleSystem.localSpace = localSpace != 0;
            if (version >= 33) {
                int enterParticleAction = 0;
                int exitParticleAction = 0;
                record >> std::quoted(entity.triggerParticle.targetName) >> enterParticleAction >> exitParticleAction;
                if (entity.triggerParticle.targetName == "-") entity.triggerParticle.targetName.clear();
                enterParticleAction = std::clamp(enterParticleAction, 0,
                    static_cast<int>(ParticleAction::Clear));
                exitParticleAction = std::clamp(exitParticleAction, 0,
                    static_cast<int>(ParticleAction::Clear));
                entity.triggerParticle.onEnter = static_cast<ParticleAction>(enterParticleAction);
                entity.triggerParticle.onExit = static_cast<ParticleAction>(exitParticleAction);
                entity.triggerParticleEnabled = !entity.triggerParticle.targetName.empty()
                    && (entity.triggerParticle.onEnter != ParticleAction::None
                        || entity.triggerParticle.onExit != ParticleAction::None);
            }
            if (version >= 34) {
                int collisionEnabled = 0;
                int collisionResponse = 0;
                record >> collisionEnabled >> collisionResponse >> particle.collisionRadius
                       >> particle.collisionBounce >> particle.collisionFriction
                       >> particle.collisionLifetimeLoss;
                particle.collisionEnabled = collisionEnabled != 0;
                particle.collisionResponse = static_cast<ParticleCollisionResponse>(
                    std::clamp(collisionResponse, 0, 1));
                particle.collisionRadius = std::max(particle.collisionRadius, 0.0f);
                particle.collisionBounce = std::max(particle.collisionBounce, 0.0f);
                particle.collisionFriction = std::clamp(particle.collisionFriction, 0.0f, 1.0f);
                particle.collisionLifetimeLoss = std::clamp(
                    particle.collisionLifetimeLoss, 0.0f, 1.0f);
            }
            if (version >= 35) {
                int trailsEnabled = 0;
                record >> trailsEnabled >> particle.trailSegments >> particle.trailLength
                       >> particle.trailWidth >> particle.trailOpacity;
                particle.trailsEnabled = trailsEnabled != 0;
                particle.trailSegments = std::clamp(particle.trailSegments, 2, 16);
                particle.trailLength = std::max(particle.trailLength, 0.001f);
                particle.trailWidth = std::max(particle.trailWidth, 0.0f);
                particle.trailOpacity = std::clamp(particle.trailOpacity, 0.0f, 1.0f);
            }
            if (version >= 36) {
                std::size_t layerCount = 0;
                record >> layerCount;
                layerCount = std::min<std::size_t>(layerCount, 64);
                entity.particleEffect.layers.reserve(layerCount);
                for (std::size_t layerIndex = 0; layerIndex < layerCount; ++layerIndex) {
                    ParticleEffectLayer layer;
                    int layerEnabled = 1;
                    record >> std::quoted(layer.name) >> std::quoted(layer.assetPath) >> layerEnabled
                           >> layer.offset.x >> layer.offset.y >> layer.offset.z;
                    layer.enabled = layerEnabled != 0;
                    if (!layer.assetPath.empty() && layer.assetPath != "-") {
                        std::string ignored;
                        LoadParticleAsset(layer.assetPath, &layer.system, &ignored);
                    }
                    entity.particleEffect.layers.push_back(std::move(layer));
                }
                entity.particleEffectEnabled = !entity.particleEffect.layers.empty();
            }
            if (version >= 37) {
                int renderMode = 0, meshShape = 0, align = 1;
                record >> renderMode >> meshShape >> std::quoted(particle.meshPath)
                       >> particle.meshScale >> align;
                particle.renderMode = static_cast<ParticleRenderMode>(std::clamp(renderMode, 0, 1));
                particle.meshShape = static_cast<ParticleMeshShape>(std::clamp(meshShape, 0, 4));
                particle.meshAlignToVelocity = align != 0;
                if (particle.meshPath == "-") particle.meshPath.clear();
                particle.meshScale = std::max(particle.meshScale, 0.001f);
            }
            if (version >= 38) {
                int backend = 0;
                record >> backend;
                particle.simulationBackend = static_cast<ParticleSimulationBackend>(
                    std::clamp(backend, 0, 2));
            }
            if (version >= 39) {
                std::size_t moduleCount = 0;
                record >> moduleCount;
                if (moduleCount > 32) {
                    if (error) *error = "Runtime particle module stack is too large.";
                    return false;
                }
                particle.modules.clear();
                for (std::size_t moduleIndex = 0; moduleIndex < moduleCount; ++moduleIndex) {
                    int type = 0, enabledValue = 1;
                    record >> type >> enabledValue;
                    ParticleModule module;
                    module.type = static_cast<ParticleModuleType>(std::clamp(type, 0,
                        static_cast<int>(ParticleModuleType::Renderer)));
                    module.enabled = enabledValue != 0;
                    if (version >= 40) {
                        int initialized = 0;
                        record >> module.instanceId >> std::quoted(module.name) >> initialized
                               >> module.vectorValue.x >> module.vectorValue.y
                               >> module.vectorValue.z >> module.valueA;
                        if (version >= 41) record >> module.valueB >> module.valueC >> module.valueD;
                        if (version >= 42) {
                            int curveEnabled = 0;
                            record >> module.colorValueA.r >> module.colorValueA.g
                                   >> module.colorValueA.b >> module.colorValueA.a
                                   >> module.colorValueB.r >> module.colorValueB.g
                                   >> module.colorValueB.b >> module.colorValueB.a;
                            for (float& key : module.curveValues) record >> key;
                            record >> curveEnabled;
                            module.curveEnabled = curveEnabled != 0;
                        }
                        if (version >= 43) {
                            int stage = 0;
                            record >> stage;
                            module.stage = static_cast<ParticleModuleStage>(std::clamp(stage, 0, 2));
                        }
                        module.parametersInitialized = initialized != 0;
                    }
                    if (type >= 0 && type <= static_cast<int>(ParticleModuleType::Renderer))
                        particle.modules.push_back(std::move(module));
                }
                NormalizeParticleModuleStack(particle, version >= 42);
            } else {
                NormalizeParticleModuleStack(particle, false);
            }
            entity.particleSystem.burstCount = std::max(entity.particleSystem.burstCount, 0);
            entity.particleSystem.burstInterval = std::max(entity.particleSystem.burstInterval, 0.0f);
        }

        if (!record) {
            if (error) {
                *error = "Runtime scene contains an invalid entity record.";
            }
            return false;
        }

        loaded.entities.push_back(entity);
    }

    *scene = loaded;
    return true;
}

bool RuntimeSceneLoader::Instantiate(const Scene &scene, ecs::Registry &registry, const PrimitiveMeshes &meshes, std::vector<ecs::Entity> *created, std::string *error)
{
    if (created) {
        created->clear();
    }

    for (const EntityDesc& desc : scene.entities) {
        const Mesh* mesh = MeshForPrimitive(desc.primitive, meshes);
        const bool emptyObject = desc.primitive == "Empty";
        if (!mesh && !emptyObject) {
            if (error) {
                *error = "Runtime scene references unsupported primitive: " + desc.primitive;
            }
            return false;
        }

        const ecs::Entity entity = registry.Create();
        registry.Add<ecs::RuntimeName>(entity, ecs::RuntimeName{desc.name});
        registry.Add<ecs::Transform>(entity, ecs::Transform{
            desc.position,
            desc.scale,
            desc.rotation
        });
        if (mesh) {
            registry.Add<ecs::MeshRenderer>(entity, ecs::MeshRenderer{
                mesh,
                desc.color
            });
        }
        if (!desc.modelPath.empty() && desc.skeletalModel) {
            std::vector<ecs::SkinnedModelAsset::Notify> notifies;
            notifies.reserve(desc.animationEvents.size());
            for (const AnimationEventDesc& event : desc.animationEvents) {
                if (event.name.empty()) {
                    continue;
                }
                notifies.push_back(ecs::SkinnedModelAsset::Notify{
                    event.clipIndex,
                    event.time,
                    event.name
                });
            }
            std::vector<ecs::SkinnedModelAsset::ActionProfile> actionProfiles;
            actionProfiles.reserve(desc.animationActionProfiles.size());
            for (const AnimationActionProfileDesc& profile : desc.animationActionProfiles) {
                if (profile.name.empty()) {
                    continue;
                }
                actionProfiles.push_back(ecs::SkinnedModelAsset::ActionProfile{
                    profile.name,
                    profile.clipIndex,
                    profile.clipName,
                    profile.maskRootBone,
                    profile.fadeIn,
                    profile.fadeOut,
                    profile.speed
                });
            }
            std::vector<ecs::SkinnedModelAsset::AnimationState> states;
            states.reserve(desc.animationStates.size());
            for (const AnimationStateDesc& state : desc.animationStates) {
                if (state.name.empty()) {
                    continue;
                }
                ecs::SkinnedModelAsset::AnimationState runtimeState{
                    state.name,
                    state.clipIndex,
                    state.clipName,
                    state.loop,
                    state.speed,
                    state.blendClipIndex,
                    state.blendClipName,
                    state.blendParameter,
                    state.blendMin,
                    state.blendMax,
                    state.rootMotion
                };
                for (const auto& sample : state.blendSamples) {
                    runtimeState.blendSamples.push_back(
                        {sample.clipIndex, sample.clipName, sample.value, sample.valueY});
                }
                runtimeState.blendParameterY = state.blendParameterY;
                runtimeState.blendSpace2D = state.blendSpace2D;
                runtimeState.synchronizeBlendSpace = state.synchronizeBlendSpace;
                states.push_back(std::move(runtimeState));
            }
            std::vector<ecs::SkinnedModelAsset::AnimationParameter> parameters;
            parameters.reserve(desc.animationParameters.size());
            for (const AnimationParameterDesc& parameter : desc.animationParameters) {
                if (!parameter.name.empty()) {
                    parameters.push_back({parameter.name, parameter.type, parameter.defaultValue});
                }
            }
            auto findState = [&](const std::string& name) {
                for (std::size_t i = 0; i < states.size(); ++i) {
                    if (states[i].name == name) {
                        return static_cast<int>(i);
                    }
                }
                return -1;
            };
            std::vector<ecs::SkinnedModelAsset::AnimationTransition> transitions;
            transitions.reserve(desc.animationTransitions.size());
            for (const AnimationTransitionDesc& transition : desc.animationTransitions) {
                const int from = transition.fromState.empty() ? -1 : findState(transition.fromState);
                const int to = findState(transition.toState);
                if (from < -1 || to < 0 || from == to) {
                    continue;
                }
                transitions.push_back(ecs::SkinnedModelAsset::AnimationTransition{
                    from,
                    to,
                    transition.parameter,
                    transition.compare,
                    transition.threshold,
                    transition.fade,
                    transition.exitTime,
                    transition.priority,
                    transition.canInterrupt
                });
            }
            registry.Add<ecs::SkinnedModelAsset>(entity, ecs::SkinnedModelAsset{
                desc.modelPath,
                desc.modelOrientationEuler,
                desc.modelOffsetPosition,
                desc.modelOffsetScale,
                desc.animationClipIndex,
                desc.animationClipName,
                desc.animationAutoplay,
                desc.animationLoop,
                desc.animationSpeed,
                desc.animationLocomotionEnabled,
                desc.animationIdleClipIndex,
                desc.animationWalkClipIndex,
                desc.animationRunClipIndex,
                desc.animationIdleClipName,
                desc.animationWalkClipName,
                desc.animationRunClipName,
                desc.animationWalkAt,
                desc.animationRunAt,
                std::move(notifies),
                std::move(actionProfiles),
                std::move(states),
                std::move(parameters),
                std::move(transitions)
            });
        } else if (!desc.modelPath.empty()) {
            // Non-skeletal model -> static model asset. A skeletal character must NOT
            // also get a ModelAsset, or it renders twice (once skinned + upright via
            // the render offset, once static at the raw transform).
            registry.Add<ecs::ModelAsset>(entity, ecs::ModelAsset{desc.modelPath});
        }
        if (!desc.materialPath.empty()) {
            ecs::MaterialAsset material;
            material.path = desc.materialPath;
            material.parameterOverrides = desc.materialParameterOverrides;
            registry.Add<ecs::MaterialAsset>(entity, std::move(material));
        }

        if (desc.linearVelocityEnabled) {
            registry.Add<ecs::LinearVelocity>(entity, ecs::LinearVelocity{desc.linearVelocity});
        }

        if (desc.angularVelocityEnabled &&
            glm::dot(desc.angularVelocityAxis, desc.angularVelocityAxis) > 0.0f) {
            registry.Add<ecs::AngularVelocity>(entity, ecs::AngularVelocity{
                desc.angularVelocityAxis,
                desc.angularVelocityRadians
            });
        }
        if (desc.rigidBodyEnabled) {
            registry.Add<ecs::RigidBody>(entity, desc.rigidBody);
        }
        if (desc.colliderEnabled) {
            registry.Add<ecs::Collider>(entity, desc.collider);
        }
        if (desc.rotatorEnabled) {
            registry.Add<ecs::Rotator>(entity, desc.rotator);
        }
        if (desc.moverEnabled) {
            ecs::Mover mover = desc.mover;
            mover.origin = desc.position;
            mover.initialized = true;
            registry.Add<ecs::Mover>(entity, mover);
        }
        if (desc.healthEnabled) {
            registry.Add<Health>(entity, desc.health);
        }
        if (desc.scriptEnabled && !desc.scriptClassName.empty()) {
            NativeScriptComponent script;
            script.enabled = true;
            script.className = desc.scriptClassName;
            script.sourcePath = desc.scriptPath;
            script.fields = desc.scriptFields;
            registry.Add<NativeScriptComponent>(entity, std::move(script));
        }
        if (desc.audioSourceEnabled && !desc.audioSource.path.empty()) {
            registry.Add<ecs::AudioSource>(entity, desc.audioSource);
        }
        if (desc.triggerAudioEnabled) {
            registry.Add<ecs::TriggerAudioAction>(entity, desc.triggerAudio);
        }
        if (desc.particleSystemEnabled) {
            ParticleSystemComponent system = desc.particleSystem;
            system.enabled = true;
            system.initialized = false;
            system.playing = false;
            system.emitter.Clear();
            registry.Add<ParticleSystemComponent>(entity, std::move(system));
        }
        if (desc.particleEffectEnabled) {
            ParticleEffectComponent effect = desc.particleEffect;
            for (ParticleEffectLayer& layer : effect.layers) {
                layer.system.initialized = false;
                layer.system.playing = false;
                layer.system.emitter.Clear();
            }
            registry.Add<ParticleEffectComponent>(entity, std::move(effect));
        }
        if (desc.triggerParticleEnabled) {
            registry.Add<TriggerParticleAction>(entity, desc.triggerParticle);
        }

        if (created) {
            created->push_back(entity);
        }
    }

    bool environmentSunApplied = false;
    for (const Scene::LightDesc& desc : scene.lights) {
        ecs::Light light = desc.light;
        if (scene.environment.driveSunLight
            && light.type == ecs::Light::Type::Directional
            && !environmentSunApplied) {
            light = EnvironmentSunLight(scene.environment);
            environmentSunApplied = true;
        }

        const ecs::Entity entity = registry.Create();
        registry.Add<ecs::RuntimeName>(entity, ecs::RuntimeName{desc.name});
        registry.Add<ecs::Transform>(entity, ecs::Transform{
            desc.position,
            glm::vec3(1.0f),
            glm::quat(1.0f, 0.0f, 0.0f, 0.0f)
        });
        registry.Add<ecs::Light>(entity, light);
        if (created) {
            created->push_back(entity);
        }
    }

    if (scene.environment.driveSunLight && !environmentSunApplied) {
        const ecs::Entity entity = registry.Create();
        registry.Add<ecs::RuntimeName>(entity, ecs::RuntimeName{"EnvironmentSun"});
        registry.Add<ecs::Transform>(entity, ecs::Transform{});
        registry.Add<ecs::Light>(entity, EnvironmentSunLight(scene.environment));
        if (created) {
            created->push_back(entity);
        }
    }

    return true;
}

} // namespace engine
