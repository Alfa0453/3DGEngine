#include "engine/scene/RuntimeSceneLoader.h"

#include "engine/ecs/Components.h"
#include "engine/ecs/Registry.h"
#include "engine/gameplay/GameplayComponents.h"
#include "engine/gameplay/Script.h"
#include "engine/graphics/DayNightCycle.h"
#include "engine/graphics/Mesh.h"
#include "engine/physics/PhysicsComponents.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace engine {
namespace {

const Mesh* MeshForPrimitive(const std::string& primitive, const RuntimeSceneLoader::PrimitiveMeshes& meshes) {
    if (primitive == "Cube") {
        return meshes.cube;
    }
    if (primitive == "Plane") {
        return meshes.plane;
    }
    if (primitive == "Sphere") {
        return meshes.sphere;
    }
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
    if (magic != "3DGRuntimeScene" || version < 1 || version > 21) {
        if (error) {
            *error = "Runtime scene file has an unknown format: "
                + magic + " " + std::to_string(version)
                + " (expected 3DGRuntimeScene 1..21).";
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
            record >> desc.name >> typeName
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
        record >> entity.primitive >> entity.name
               >> entity.position.x >> entity.position.y >> entity.position.z
               >> entity.scale.x >> entity.scale.y >> entity.scale.z
               >> entity.rotation.w >> entity.rotation.x >> entity.rotation.y >> entity.rotation.z
               >> entity.color.r >> entity.color.g >> entity.color.b;

        if (version >= 2) {
            record >> entity.modelPath >> entity.materialPath;
            if (entity.modelPath == "-") {
                entity.modelPath.clear();
            }
            if (entity.materialPath == "-") {
                entity.materialPath.clear();
            }
        }
        if (version >= 17) {
            int skeletalModel = 0;
            int animationAutoplay = 1;
            int animationLoop = 1;
            record >> skeletalModel
                   >> entity.animationClipIndex
                   >> entity.animationClipName
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
                   >> entity.animationIdleClipName
                   >> entity.animationWalkClipIndex
                   >> entity.animationWalkClipName
                   >> entity.animationRunClipIndex
                   >> entity.animationRunClipName
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
                       >> event.name;
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
                record >> profile.name
                       >> profile.clipIndex
                       >> profile.clipName
                       >> profile.maskRootBone
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
                record >> state.name
                       >> state.clipIndex
                       >> state.clipName
                       >> loop
                       >> state.speed;
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

            std::size_t transitionCount = 0;
            record >> transitionCount;
            for (std::size_t i = 0; i < transitionCount; ++i) {
                AnimationTransitionDesc transition;
                record >> transition.fromState
                       >> transition.toState
                       >> transition.parameter
                       >> transition.compare
                       >> transition.threshold
                       >> transition.fade;
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
            if (colliderShape == static_cast<int>(ecs::ColliderShape::Plane)) {
                entity.collider.shape = ecs::ColliderShape::Plane;
            } else if (colliderShape == static_cast<int>(ecs::ColliderShape::Box)) {
                entity.collider.shape = ecs::ColliderShape::Box;
            } else if (colliderShape == static_cast<int>(ecs::ColliderShape::Capsule)) {
                entity.collider.shape = ecs::ColliderShape::Capsule;
            } else {
                entity.collider.shape = ecs::ColliderShape::Sphere;
            }
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
                   >> entity.scriptClassName
                   >> entity.scriptPath;
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
                    record >> field.name >> fieldType >> field.value;
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
        if (!mesh) {
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
        registry.Add<ecs::MeshRenderer>(entity, ecs::MeshRenderer{
            mesh,
            desc.color
        });
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
                states.push_back(ecs::SkinnedModelAsset::AnimationState{
                    state.name,
                    state.clipIndex,
                    state.clipName,
                    state.loop,
                    state.speed
                });
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
                const int from = findState(transition.fromState);
                const int to = findState(transition.toState);
                if (from < 0 || to < 0 || from == to) {
                    continue;
                }
                transitions.push_back(ecs::SkinnedModelAsset::AnimationTransition{
                    from,
                    to,
                    transition.parameter,
                    transition.compare,
                    transition.threshold,
                    transition.fade
                });
            }
            registry.Add<ecs::SkinnedModelAsset>(entity, ecs::SkinnedModelAsset{
                desc.modelPath,
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
                std::move(notifies)
            });
        } else if (!desc.modelPath.empty()) {
            registry.Add<ecs::ModelAsset>(entity, ecs::ModelAsset{desc.modelPath});
        }
        if (!desc.modelPath.empty()) {
            registry.Add<ecs::ModelAsset>(entity, ecs::ModelAsset{desc.modelPath});
        }
        if (!desc.materialPath.empty()) {
            registry.Add<ecs::MaterialAsset>(entity, ecs::MaterialAsset{desc.materialPath});
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
