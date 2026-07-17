#include "EditorScene.h"

#include <engine/graphics/Mesh.h>

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <cstddef>
#include <filesystem>
#include <glm/gtc/quaternion.hpp>

using engine::ecs::Entity;
using engine::ecs::Light;
using engine::ecs::MeshRenderer;
using engine::ecs::RigidBody;
using engine::ecs::Collider;
using engine::ecs::Transform;

namespace {

// TEMPORARY terrain crash tracing (shares terrain_debug.log with EditorApp.cpp).
void TerrainTrace(const std::string& msg) {
    std::ofstream f("D:/C++_Projects/3DGEngine/terrain_debug.log", std::ios::app);
    if (f.is_open()) { f << msg << std::endl; }
}

const char* PrimitiveName(EditorScene::Primitive primitive) {
    switch (primitive) {
    case EditorScene::Primitive::Plane: return "Plane";
    case EditorScene::Primitive::Cube: return "Cube";
    case EditorScene::Primitive::Sphere: return "Sphere";
    case EditorScene::Primitive::Capsule: return "Capsule";
    case EditorScene::Primitive::Cylinder: return "Cylinder";
    case EditorScene::Primitive::Cone: return "Cone";
    case EditorScene::Primitive::Pyramid: return "Pyramid";
    case EditorScene::Primitive::Torus: return "Torus";
    case EditorScene::Primitive::Staircase: return "Staircase";
    }
    return "Cube";
}

const char* LightTypeName(Light::Type type) {
    switch (type) {
    case Light::Type::Directional:  return "Directional";
    case Light::Type::Point:        return "Point";
    case Light::Type::Spot:         return "Spot";
    case Light::Type::Area:         return "Area";
    }
    return "Point";
}

bool ParseLightType(const std::string& value, Light::Type* type) {
    if (value == "Directional") {
        *type = Light::Type::Directional;
        return true;
    }
    if (value == "Point") {
        *type = Light::Type::Point;
        return true;
    }
    if (value == "Spot") {
        *type = Light::Type::Spot;
        return true;
    }
    if (value == "Area") {
        *type = Light::Type::Area;
        return true;
    }
    return false;
}

bool ParsePrimitive(const std::string& value, EditorScene::Primitive* primitive) {
    if (value == "Plane") {
        *primitive = EditorScene::Primitive::Plane;
        return true;
    }
    if (value == "Cube") {
        *primitive = EditorScene::Primitive::Cube;
        return true;
    }
    if (value == "Sphere") {
        *primitive = EditorScene::Primitive::Sphere;
        return true;
    }
    if (value == "Capsule") {
        *primitive = EditorScene::Primitive::Capsule;
        return true;
    }
    if (value == "Cylinder") {
        *primitive = EditorScene::Primitive::Cylinder;
        return true;
    }
    if (value == "Cone") {
        *primitive = EditorScene::Primitive::Cone;
        return true;
    }
    if (value == "Pyramid") { *primitive = EditorScene::Primitive::Pyramid; return true; }
    if (value == "Torus") { *primitive = EditorScene::Primitive::Torus; return true; }
    if (value == "Staircase") { *primitive = EditorScene::Primitive::Staircase; return true; }
    return false;
}

EditorScene::TriggerActionMode TriggerActionModeFromInt(int value) {
    switch (value) {
    case static_cast<int>(EditorScene::TriggerActionMode::Enable):
        return EditorScene::TriggerActionMode::Enable;
    case static_cast<int>(EditorScene::TriggerActionMode::Disable):
        return EditorScene::TriggerActionMode::Disable;
    case static_cast<int>(EditorScene::TriggerActionMode::Toggle):
        return EditorScene::TriggerActionMode::Toggle;
    default:
        return EditorScene::TriggerActionMode::None;
    }
}

engine::ecs::AudioAction AudioActionFromInt(int value) {
    if (value >= static_cast<int>(engine::ecs::AudioAction::None)
        && value <= static_cast<int>(engine::ecs::AudioAction::Stop)) {
        return static_cast<engine::ecs::AudioAction>(value);
    }
    return engine::ecs::AudioAction::None;
}

const char* PhysicsJointTypeName(EditorScene::PhysicsJoint::Type type) {
    switch (type) {
    case EditorScene::PhysicsJoint::Type::Distance: return "Distance";
    case EditorScene::PhysicsJoint::Type::Spring: return "Spring";
    }
    return "Distance";
}

bool ParsePhysicsJointType(const std::string& value, EditorScene::PhysicsJoint::Type* type) {
    if (value == "Distance" || value == "Rope") {
        *type = EditorScene::PhysicsJoint::Type::Distance;
        return true;
    }
    if (value == "Spring") {
        *type = EditorScene::PhysicsJoint::Type::Spring;
        return true;
    }
    return false;
}

const engine::Mesh& MeshFor(EditorScene::Primitive primitive, const engine::Mesh& cube,
                            const engine::Mesh& plane, const engine::Mesh& sphere,
                            const engine::Mesh& capsule, const engine::Mesh& cylinder,
                            const engine::Mesh& cone, const engine::Mesh& pyramid,
                            const engine::Mesh& torus, const engine::Mesh& staircase){
    if (primitive == EditorScene::Primitive::Staircase) return staircase;
    if (primitive == EditorScene::Primitive::Torus) return torus;
    if (primitive == EditorScene::Primitive::Pyramid) return pyramid;
    if (primitive == EditorScene::Primitive::Cone)
        return cone;
    if (primitive == EditorScene::Primitive::Cylinder)
        return cylinder;
    if (primitive == EditorScene::Primitive::Capsule)
    {
        return capsule;
    }
    if (primitive == EditorScene::Primitive::Sphere)
    {
        return sphere;
    }
    if (primitive == EditorScene::Primitive::Plane)
    {
        return plane;
    }

    return cube;
}

const glm::vec3 kPalette[] = {
    {0.83f, 0.20f, 0.24f},
    {0.20f, 0.55f, 0.92f},
    {0.32f, 0.73f, 0.45f},
    {0.78f, 0.48f, 0.18f},
    {0.68f, 0.42f, 0.82f},
    {0.82f, 0.78f, 0.42f},
    {0.34f, 0.37f, 0.41f}
};

const char* StoredPath(const std::string& path) {
    return path.empty() ? "-" : path.c_str();
}

} // namespace

void EditorScene::BuildDefault(const engine::Mesh & cube, const engine::Mesh & plane, const engine::Mesh & sphere,
                               const engine::Mesh &, const engine::Mesh &, const engine::Mesh &,
                               const engine::Mesh &, const engine::Mesh &, const engine::Mesh &)
{
    Clear();

    Transform ground;
    ground.position = glm::vec3(0.0f, -0.5f, 0.0f);
    ground.scale = glm::vec3(8.0f, 1.0f, 8.0f);
    CreateObject("Ground", Primitive::Plane, plane, ground, glm::vec3(0.34f, 0.37f, 0.41f));

    const glm::vec3 positions[] = {
        {-2.0f, 0.25f, 0.0f},
        {0.0f, 0.25f, 0.0f},
        {2.0f, 0.25f, 0.0f}
    };
    const glm::vec3 colors[] = {
        {0.83f, 0.20f, 0.24f},
        {0.20f, 0.55f, 0.92f},
        {0.32f, 0.73f, 0.45f}
    };

    for (int i = 0; i < 3; ++i) {
        Transform cubeTransform;
        cubeTransform.position = positions[i];
        cubeTransform.scale = glm::vec3(0.9f);
        CreateObject("Cube_" + std::to_string(i + 1), Primitive::Cube, cube, cubeTransform, colors[i]);
    }

    AddDirectionalLight(sphere);
    AddPointLight(sphere);

    m_selectedIndex = 1;
    m_dirty = false;
    ClearHistory();
}

bool EditorScene::Save(const std::string & path, std::string * error, bool markClean)
{
    std::ofstream out(path);
    if (!out) {
        if (error) *error = "Could not open scene file for writing.";
        return false;
    }

    out << "3DGEditorScene 76\n";
    out << "environment "
        << m_environment.timeOfDay << ' '
        << m_environment.skyLightIntensity << ' '
        << (m_environment.driveSunLight ? 1 : 0) << ' '
        << m_environment.sunIntensity << ' '
        << (m_environment.showLightGuides ? 1 : 0) << ' '
        << (m_environment.selectedLightGuideOnly ? 1 : 0) << ' '
        << (m_environment.ibl ? 1 : 0) << ' '
        << (m_environment.ssao ? 1 : 0) << ' '
        << m_environment.ssaoRadius << ' '
        << m_environment.ssaoBias << ' '
        << (m_environment.ssr ? 1 : 0) << ' '
        << m_environment.ssrIntensity << ' '
        << (m_environment.directionalShadows ? 1 : 0) << ' '
        << (m_environment.pointShadows ? 1 : 0) << ' '
        << (m_environment.spotShadows ? 1 : 0) << ' '
        << m_environment.shadowSoftness << ' '
        << (m_environment.fog ? 1 : 0) << ' '
        << m_environment.fogColor.r << ' '
        << m_environment.fogColor.g << ' '
        << m_environment.fogColor.b << ' '
        << m_environment.fogDensity << ' '
        << m_environment.fogHeight << ' '
        << m_environment.fogHeightFalloff << ' '
        << m_environment.physicsGravity.x << ' '
        << m_environment.physicsGravity.y << ' '
        << m_environment.physicsGravity.z << ' '
        << m_environment.physicsSolverIterations << ' '
        << (m_environment.physicsBroadPhase ? 1 : 0) << ' '
        << m_environment.physicsCellSize << ' '
        << m_environment.physicsRestitutionThreshold << ' '
        << (m_environment.physicsAllowSleeping ? 1 : 0) << ' '
        << m_environment.physicsSleepLinearVelocity << ' '
        << m_environment.physicsSleepAngularVelocity << ' '
        << m_environment.physicsTimeToSleep << ' '
        << (m_environment.showPhysicsGuides ? 1 : 0) << ' '
        << (m_environment.selectedPhysicsGuideOnly ? 1 : 0) << ' '
        << (m_environment.msaa ? 1 : 0) << ' '
        << (m_environment.fxaa ? 1 : 0) << ' '
        << m_environment.renderScale << ' '
        << (m_environment.hudAsset.empty() ? std::string("~") : m_environment.hudAsset) << '\n';
    for (const Environment::PostProcessEffect& effect :
         m_environment.postProcessEffects) {
        out << "post_effect "
            << std::quoted(effect.shaderPath) << ' '
            << (effect.enabled ? 1 : 0) << ' '
            << effect.parameters.size();
        for (const Environment::PostProcessParameter& parameter :
             effect.parameters) {
            out << ' ' << std::quoted(parameter.name)
                << ' ' << parameter.type
                << ' ' << std::quoted(parameter.value);
        }
        out << '\n';
    }
    for (const CameraPreset& camera : m_cameraPresets) {
        out << "camera "
            << std::quoted(camera.name) << ' '
            << camera.position.x << ' ' << camera.position.y << ' ' << camera.position.z << ' '
            << camera.target.x << ' ' << camera.target.y << ' ' << camera.target.z << ' '
            << camera.fov << ' ' << camera.nearPlane << ' ' << camera.farPlane << ' '
            << camera.blendDuration << ' ' << camera.blendEasing << ' '
            << (camera.primary ? 1 : 0) << ' '
            << (camera.useInPlay ? 1 : 0) << '\n';
    }
    for (const CameraSequence& sequence : m_cameraSequences) {
        out << "camera_sequence "
            << std::quoted(sequence.name) << ' '
            << (sequence.loop ? 1 : 0) << ' '
            << sequence.shots.size();
        for (const CameraSequenceShot& shot : sequence.shots) {
            out << ' ' << std::quoted(shot.cameraName)
                << ' ' << shot.travelDuration
                << ' ' << shot.holdDuration
                << ' ' << shot.easing
                << ' ' << shot.pathMode
                << ' ' << std::quoted(shot.eventName.empty() ? std::string("-") : shot.eventName);
        }
        out << ' ' << sequence.cues.size();
        for (const CinematicCue& cue : sequence.cues) {
            out << ' ' << static_cast<int>(cue.type)
                << ' ' << cue.time
                << ' ' << std::quoted(cue.name.empty() ? std::string("-") : cue.name)
                << ' ' << std::quoted(cue.assetPath.empty() ? std::string("-") : cue.assetPath)
                << ' ' << std::quoted(cue.targetObject.empty() ? std::string("-") : cue.targetObject)
                << ' ' << std::quoted(cue.animationClip.empty() ? std::string("-") : cue.animationClip)
                << ' ' << cue.volume;
        }
        out << '\n';
    }
    for (const Object& object : m_objects) {
        const Transform* transform = m_registry.TryGet<Transform>(object.entity);
        const MeshRenderer* renderer = m_registry.TryGet<MeshRenderer>(object.entity);
        if (!transform || !renderer) {
            continue;
        }

        if (object.light) {
            const Light* light = m_registry.TryGet<Light>(object.entity);
            const Light& data = light ? *light : object.lightData;
            out << "light "
                << object.name << ' '
                << LightTypeName(data.type) << ' '
                << transform->position.x << ' ' << transform->position.y << ' ' << transform->position.z << ' '
                << data.color.r << ' ' << data.color.g << ' ' << data.color.b << ' '
                << data.intensity << ' '
                << data.direction.x << ' ' << data.direction.y << ' ' << data.direction.z << ' '
                << data.innerAngle << ' ' << data.outerAngle << ' ' << data.range << ' ' << data.sourceRadius << ' '
                << (object.visible ? 1 : 0) << ' '
                << (object.locked ? 1 : 0) << '\n';
            continue;
        }

        out << "object "
            << PrimitiveName(object.primitive) << ' '
            << object.name << ' '
            << transform->position.x << ' ' << transform->position.y << ' ' << transform->position.z << ' '
            << transform->scale.x << ' ' << transform->scale.y << ' ' << transform->scale.z << ' '
            << transform->rotation.w << ' ' << transform->rotation.x << ' '
            << transform->rotation.y << ' ' << transform->rotation.z << ' '
            << renderer->color.r << ' ' << renderer->color.g << ' ' << renderer->color.b << ' '
            << (object.visible ? 1 : 0) << ' '
            << (object.locked ? 1 : 0) << ' '
            << StoredPath(object.modelAssetPath) << ' '
            << StoredPath(object.materialAssetPath) << ' '
            << (object.skeletalModel ? 1 : 0) << ' '
            << object.animationClipIndex << ' '
            << StoredPath(object.animationClipName) << ' '
            << (object.animationAutoplay ? 1 : 0) << ' '
            << (object.animationLoop ? 1 : 0) << ' '
            << object.animationSpeed << ' '
            << (object.animationLocomotionEnabled ? 1 : 0) << ' '
            << object.animationIdleClipIndex << ' '
            << StoredPath(object.animationIdleClipName) << ' '
            << object.animationWalkClipIndex << ' '
            << StoredPath(object.animationWalkClipName) << ' '
            << object.animationRunClipIndex << ' '
            << StoredPath(object.animationRunClipName) << ' '
            << object.animationWalkAt << ' '
            << object.animationRunAt << ' '
            << object.animationEvents.size() << ' ';
        for (const AnimationEvent& event : object.animationEvents) {
            out << event.clipIndex << ' '
                << event.time << ' '
                << StoredPath(event.name) << ' ';
        }
        out << object.animationActionProfiles.size() << ' ';
        for (const AnimationActionProfile& profile : object.animationActionProfiles) {
            out << StoredPath(profile.name) << ' '
                << profile.clipIndex << ' '
                << StoredPath(profile.clipName) << ' '
                << StoredPath(profile.maskRootBone) << ' '
                << profile.fadeIn << ' '
                << profile.fadeOut << ' '
                << profile.speed << ' ';
        }
        out << object.animationStates.size() << ' ';
        for (const AnimationStateNode& state : object.animationStates) {
            out << StoredPath(state.name) << ' '
                << state.clipIndex << ' '
                << StoredPath(state.clipName) << ' '
                << (state.loop ? 1 : 0) << ' '
                << state.speed << ' '
                << state.blendClipIndex << ' '
                << StoredPath(state.blendClipName) << ' '
                << StoredPath(state.blendParameter) << ' '
                << state.blendMin << ' '
                << state.blendMax << ' '
                << (state.rootMotion ? 1 : 0) << ' ';
        }
        out << object.animationParameters.size() << ' ';
        for (const AnimationParameter& parameter : object.animationParameters) {
            out << StoredPath(parameter.name) << ' '
                << static_cast<int>(parameter.type) << ' '
                << parameter.defaultValue << ' ';
        }
        out << object.animationTransitions.size() << ' ';
        for (const AnimationStateTransition& transition : object.animationTransitions) {
            out << StoredPath(transition.fromState) << ' '
                << StoredPath(transition.toState) << ' '
                << StoredPath(transition.parameter) << ' '
                << static_cast<int>(transition.compare) << ' '
                << transition.threshold << ' '
                << transition.fade << ' '
                << transition.exitTime << ' '
                << transition.priority << ' '
                << (transition.canInterrupt ? 1 : 0) << ' ';
        }
        out
            << object.linearVelocity.x << ' ' << object.linearVelocity.y << ' ' << object.linearVelocity.z << ' '
            << object.angularVelocityAxis.x << ' ' << object.angularVelocityAxis.y << ' ' << object.angularVelocityAxis.z << ' '
            << object.angularVelocityRadians << ' '
            << (object.linearVelocityEnabled ? 1 : 0) << ' '
            << (object.angularVelocityEnabled ? 1 : 0) << ' '
            << (object.rigidBodyEnabled ? 1 : 0) << ' '
            << object.rigidBody.velocity.x << ' ' << object.rigidBody.velocity.y << ' ' << object.rigidBody.velocity.z << ' '
            << object.rigidBody.invMass << ' '
            << (object.rigidBody.useGravity ? 1 : 0) << ' '
            << (object.rigidBody.allowSleep ? 1 : 0) << ' '
            << (object.rigidBody.ccd ? 1 : 0) << ' '
            << (object.rigidBody.freezeRotation ? 1 : 0) << ' '
            << (object.colliderEnabled ? 1 : 0) << ' '
            << static_cast<int>(object.collider.shape) << ' '
            << object.collider.radius << ' '
            << object.collider.halfHeight << ' '
            << object.collider.majorRadius << ' '
            << object.collider.minorRadius << ' '
            << object.collider.steps << ' '
            << object.collider.halfExtents.x << ' ' << object.collider.halfExtents.y << ' ' << object.collider.halfExtents.z << ' '
            << object.collider.planeNormal.x << ' ' << object.collider.planeNormal.y << ' ' << object.collider.planeNormal.z << ' '
            << object.collider.planeOffset << ' '
            << object.collider.restitution << ' '
            << object.collider.friction << ' '
            << (object.collider.isTrigger ? 1 : 0) << ' '
            << (object.rigidBody.kinematic ? 1 : 0) << ' '
            << object.collider.layer << ' '
            << object.collider.mask << ' '
            << (object.rotatorEnabled ? 1 : 0) << ' '
            << object.rotator.axis.x << ' ' << object.rotator.axis.y << ' ' << object.rotator.axis.z << ' '
            << object.rotator.radiansPerSecond << ' '
            << (object.moverEnabled ? 1 : 0) << ' '
            << object.mover.axis.x << ' ' << object.mover.axis.y << ' ' << object.mover.axis.z << ' '
            << object.mover.distance << ' '
            << object.mover.speed << ' '
            << object.mover.phase << ' '
            << StoredPath(object.triggerTargetName) << ' '
            << static_cast<int>(object.triggerEnterMoverAction) << ' '
            << static_cast<int>(object.triggerEnterRotatorAction) << ' '
            << static_cast<int>(object.triggerExitMoverAction) << ' '
            << static_cast<int>(object.triggerExitRotatorAction) << ' '
            << (object.playerControllerEnabled ? 1 : 0) << ' '
            << (object.playerController.firstPerson ? 1 : 0) << ' '
            << object.playerController.walkSpeed << ' '
            << object.playerController.runSpeed << ' '
            << object.playerController.jumpSpeed << ' '
            << object.playerController.lookSensitivity << ' '
            << object.playerController.capsuleRadius << ' '
            << object.playerController.capsuleHeight << ' '
            << object.playerController.eyeHeight << ' '
            << object.playerController.cameraDistance << ' '
            << object.playerController.cameraTargetHeight << ' '
            << object.playerController.maxSlopeDegrees << ' '
            << object.playerController.stepHeight << ' '
            << (object.playerController.cameraCollision ? 1 : 0) << ' '
            << object.playerController.cameraProbeRadius << ' '
            << object.playerController.cameraCollisionPadding << ' '
            << object.playerController.cameraReturnSpeed << ' '
            << (object.playerController.shoulderCamera ? 1 : 0) << ' '
            << object.playerController.shoulderOffset << ' '
            << object.playerController.shoulderSwitchSpeed << ' '
            << (object.playerController.rightShoulder ? 1 : 0) << ' '
            << (object.playerController.lockOnEnabled ? 1 : 0) << ' '
            << object.playerController.lockOnRange << ' '
            << object.playerController.lockOnViewAngle << ' '
            << object.playerController.lockOnTargetHeight << ' '
            << object.playerController.lockOnTrackingSpeed << ' '
            << StoredPath(object.triggerCameraSequenceName) << ' '
            << static_cast<int>(object.triggerEnterCameraAction) << ' '
            << static_cast<int>(object.triggerExitCameraAction) << ' '
            << (object.triggerCameraLockInput ? 1 : 0) << ' '
            << (object.triggerCameraSkippable ? 1 : 0) << ' '
            << (object.cameraZoneEnabled ? 1 : 0) << ' '
            << StoredPath(object.cameraZonePresetName) << ' '
            << (object.cameraZoneRestoreOnExit ? 1 : 0) << ' '
            << object.cameraZonePriority << ' '
            << object.cameraZoneReturnBlend << ' '
            << (object.healthEnabled ? 1 : 0) << ' '
            << object.health.hp << ' '
            << object.health.maxHp << ' '
            << (object.health.alive ? 1 : 0) << ' '
            << (object.scriptEnabled ? 1 : 0) << ' '
            << StoredPath(object.scriptClassName) << ' '
            << StoredPath(object.scriptPath) << ' '
            << object.scriptFields.size();
        for (const ScriptField& field : object.scriptFields) {
            out << ' '
                << StoredPath(field.name) << ' '
                << static_cast<int>(field.type) << ' '
                << StoredPath(field.value);
        }
        // NavAgent (scene version 37+).
        out << ' ' << (object.navAgentEnabled ? 1 : 0) << ' '
            << object.navAgentSpeed << ' '
            << object.navAgentMaxForce << ' '
            << object.navAgentReachRadius << ' '
            << object.navAgentRepathInterval << ' '
            << object.patrolPoints.size();
        for (const glm::vec3& p : object.patrolPoints) {
            out << ' ' << p.x << ' ' << p.y << ' ' << p.z;
        }
        // NavAgent perception/target (scene version 38+).
        out << ' ' << StoredPath(object.navAgentTargetName) << ' '
            << object.navAgentVisionRange << ' '
            << object.navAgentVisionHalfAngle;
        // NavAgent behaviour-tree asset (scene version 40+).
        out << ' ' << StoredPath(object.navAgentBrainAsset);
        // Editor-authored navigation bake bounds (scene version 41+).
        out << ' ' << (object.navMeshBoundsVolume ? 1 : 0);
        // Audio Source authoring data (scene version 42+).
        out << ' ' << (object.audioSourceEnabled ? 1 : 0)
            << ' ' << StoredPath(object.audioAssetPath)
            << ' ' << object.audioVolume
            << ' ' << object.audioPitch
            << ' ' << (object.audioSpatial ? 1 : 0)
            << ' ' << (object.audioLoop ? 1 : 0)
            << ' ' << (object.audioAutoplay ? 1 : 0)
            << ' ' << object.audioMinDistance
            << ' ' << object.audioMaxDistance
            << ' ' << object.audioRolloff;
        // NavAgent faction targeting (scene version 43+).
        out << ' ' << object.navAgentTeam
            << ' ' << (object.navAgentAutoTarget ? 1 : 0);
        // Trigger-driven Audio Source transport (scene version 44+).
        out << ' ' << static_cast<int>(object.triggerEnterAudioAction)
            << ' ' << static_cast<int>(object.triggerExitAudioAction);
        // Audio mixer routing (scene version 45+).
        out << ' ' << static_cast<int>(object.audioBus);
        // Directional/spatial audio controls (scene version 72+).
        out << ' ' << object.audioDopplerFactor
            << ' ' << object.audioConeInnerAngle
            << ' ' << object.audioConeOuterAngle
            << ' ' << object.audioConeOuterGain
            << ' ' << object.audioOcclusion
            << ' ' << object.audioPriority;
        // Particle System authoring data (scene version 46+).
        const engine::EmitterConfig& particle = object.particleConfig;
        out << ' ' << (object.particleSystemEnabled ? 1 : 0)
            << ' ' << particle.rate << ' ' << particle.maxParticles
            << ' ' << static_cast<int>(particle.shape) << ' ' << particle.shapeRadius
            << ' ' << particle.direction.x << ' ' << particle.direction.y << ' ' << particle.direction.z
            << ' ' << particle.coneAngleDeg
            << ' ' << particle.speedMin << ' ' << particle.speedMax
            << ' ' << particle.lifeMin << ' ' << particle.lifeMax
            << ' ' << particle.gravity.x << ' ' << particle.gravity.y << ' ' << particle.gravity.z
            << ' ' << particle.drag
            << ' ' << particle.startColor.r << ' ' << particle.startColor.g
            << ' ' << particle.startColor.b << ' ' << particle.startColor.a
            << ' ' << particle.endColor.r << ' ' << particle.endColor.g
            << ' ' << particle.endColor.b << ' ' << particle.endColor.a
            << ' ' << particle.startSize << ' ' << particle.endSize
            << ' ' << static_cast<int>(particle.blend)
            << ' ' << (object.particleAutoplay ? 1 : 0)
            << ' ' << (object.particleLoop ? 1 : 0)
            << ' ' << object.particleDuration
            << ' ' << object.particleStartDelay
            << ' ' << object.particleSimulationSpeed
            << ' ' << (object.particleLocalSpace ? 1 : 0)
            << ' ' << object.particleBurstCount
            << ' ' << object.particleBurstInterval
            << ' ' << (object.particlePrewarm ? 1 : 0)
            << ' ' << particle.rotationMinDeg << ' ' << particle.rotationMaxDeg
            << ' ' << particle.angularVelocityMinDeg << ' ' << particle.angularVelocityMaxDeg
            << ' ' << (particle.useSizeCurve ? 1 : 0)
            << ' ' << (particle.useColorCurve ? 1 : 0);
        for (float key : particle.sizeCurve) out << ' ' << key;
        for (float key : particle.colorCurve) out << ' ' << key;
        out << ' ' << std::quoted(particle.texturePath.empty() ? std::string("-") : particle.texturePath)
            << ' ' << particle.textureColumns << ' ' << particle.textureRows
            << ' ' << particle.textureFps << ' ' << (particle.textureLoop ? 1 : 0)
            << ' ' << std::quoted(object.particleAssetPath.empty() ? std::string("-")
                                                                  : object.particleAssetPath)
            << ' ' << (object.particleAssetOverride ? 1 : 0)
            << ' ' << (particle.cullingEnabled ? 1 : 0)
            << ' ' << particle.boundsRadius
            << ' ' << static_cast<int>(object.triggerEnterParticleAction)
            << ' ' << static_cast<int>(object.triggerExitParticleAction)
            << ' ' << (particle.collisionEnabled ? 1 : 0)
            << ' ' << static_cast<int>(particle.collisionResponse)
            << ' ' << particle.collisionRadius << ' ' << particle.collisionBounce
            << ' ' << particle.collisionFriction << ' ' << particle.collisionLifetimeLoss
            << ' ' << (particle.trailsEnabled ? 1 : 0) << ' ' << particle.trailSegments
            << ' ' << particle.trailLength << ' ' << particle.trailWidth << ' ' << particle.trailOpacity;
        out << ' ' << object.particleEffectLayers.size();
        for (const engine::ParticleEffectLayer& layer : object.particleEffectLayers) {
            out << ' ' << std::quoted(layer.name) << ' ' << std::quoted(layer.assetPath)
                << ' ' << (layer.enabled ? 1 : 0)
                << ' ' << layer.offset.x << ' ' << layer.offset.y << ' ' << layer.offset.z;
        }
        out << ' ' << static_cast<int>(particle.renderMode) << ' '
            << static_cast<int>(particle.meshShape) << ' '
            << std::quoted(particle.meshPath.empty() ? std::string("-") : particle.meshPath)
            << ' ' << particle.meshScale << ' ' << (particle.meshAlignToVelocity ? 1 : 0)
            << ' ' << static_cast<int>(particle.simulationBackend)
            << ' ' << particle.modules.size();
        for (const engine::ParticleModule& module : particle.modules) {
            out << ' ' << static_cast<int>(module.type) << ' '
                << (engine::SupportsDuplicateParticleModules(module.type) ? (module.enabled ? 1 : 0)
                    : (engine::IsParticleModuleEnabled(particle, module.type) ? 1 : 0))
                << ' ' << module.instanceId << ' ' << std::quoted(module.name)
                << ' ' << (module.parametersInitialized ? 1 : 0)
                << ' ' << module.vectorValue.x << ' ' << module.vectorValue.y
                << ' ' << module.vectorValue.z << ' ' << module.valueA
                << ' ' << module.valueB << ' ' << module.valueC << ' ' << module.valueD
                << ' ' << module.colorValueA.r << ' ' << module.colorValueA.g
                << ' ' << module.colorValueA.b << ' ' << module.colorValueA.a
                << ' ' << module.colorValueB.r << ' ' << module.colorValueB.g
                << ' ' << module.colorValueB.b << ' ' << module.colorValueB.a;
            for (float key : module.curveValues) out << ' ' << key;
            out << ' ' << (module.curveEnabled ? 1 : 0) << ' ' << static_cast<int>(module.stage);
        }
        out << '\n';
    }

    for (const Object& object : m_objects) {
        if (object.materialParameterOverrides.empty()) continue;
        out << "material_overrides " << std::quoted(object.name) << ' '
            << object.materialParameterOverrides.size();
        for (const auto& overrideValue : object.materialParameterOverrides)
            out << ' ' << std::quoted(overrideValue.first)
                << ' ' << std::quoted(overrideValue.second);
        out << '\n';
    }

    // Terrain records (own line, keyed by object name; applied after the object loads).
    for (const Object& object : m_objects) {
        if (!object.isTerrain) continue;
        out << "terrain " << object.name << ' '
            << object.terrainRes << ' ' << object.terrainSize << ' '
            << object.terrainMaxHeight << ' ' << object.terrainSeed << ' '
            << object.terrainOctaves << ' ' << object.terrainFrequency << ' '
            << object.terrainHeights.size();
        for (float h : object.terrainHeights) out << ' ' << h;
        out << ' ' << object.terrainPaint.size();
        for (unsigned char p : object.terrainPaint) out << ' ' << static_cast<int>(p);
        out << '\n';
    }

    // Water records (own line, keyed by object name; applied after the object loads).
    for (const Object& object : m_objects) {
        if (!object.isWater) continue;
        out << "water " << object.name << ' '
            << object.waterSize << ' ' << object.waterResolution << ' ' << object.waterLevel << ' '
            << object.waterShallow.r << ' ' << object.waterShallow.g << ' ' << object.waterShallow.b << ' '
            << object.waterDeep.r << ' ' << object.waterDeep.g << ' ' << object.waterDeep.b << ' '
            << object.waterReflection.r << ' ' << object.waterReflection.g << ' ' << object.waterReflection.b << ' '
            << object.waterTransparency << ' ' << object.waterFresnel << ' '
            << object.waterSpecular << ' ' << object.waterShininess << '\n';
    }

    for (const PhysicsJoint& joint : m_joints) {
        out << "joint "
            << PhysicsJointTypeName(joint.type) << ' '
            << (joint.enabled ? 1 : 0) << ' '
            << StoredPath(joint.objectA) << ' '
            << StoredPath(joint.objectB) << ' '
            << (joint.worldAnchor ? 1 : 0) << ' '
            << joint.anchor.x << ' ' << joint.anchor.y << ' ' << joint.anchor.z << ' '
            << joint.restLength << ' '
            << (joint.rope ? 1 : 0) << ' '
            << joint.stiffness << ' '
            << joint.damping << '\n';
    }

    if (markClean) {
        m_dirty = false;
        ClearHistory();
    }

    return true;
}

bool EditorScene::Load(const std::string & path, const engine::Mesh & cube, const engine::Mesh & plane, const engine::Mesh & sphere, const engine::Mesh & capsule, const engine::Mesh & cylinder, const engine::Mesh & cone, const engine::Mesh & pyramid, const engine::Mesh & torus, const engine::Mesh & staircase, std::string * error)
{
    std::ifstream in(path);
    if (!in) {
        if (error) *error = "Could not open scene file for reading.";
        return false;
    }

    std::string magic;
    int version = 0;
    in >> magic >> version;
    if (magic != "3DGEditorScene" ||(version < 1 || version > 76)) {
        if (error) *error = "Scene file has an unknown format.";
        return false;
    }

    Clear();

    std::string recordType;
    while (in >> recordType) {
        if (recordType == "post_effect" && version >= 74) {
            Environment::PostProcessEffect effect;
            int enabled = 1;
            std::size_t parameterCount = 0;
            in >> std::quoted(effect.shaderPath) >> enabled >> parameterCount;
            effect.enabled = enabled != 0;
            for (std::size_t i = 0; i < parameterCount; ++i) {
                Environment::PostProcessParameter parameter;
                in >> std::quoted(parameter.name)
                   >> parameter.type
                   >> std::quoted(parameter.value);
                effect.parameters.push_back(std::move(parameter));
            }
            if (!in) {
                if (error) *error =
                    "Scene file contains an invalid post-process effect record.";
                Clear();
                return false;
            }
            m_environment.postProcessEffects.push_back(std::move(effect));
            continue;
        }
        if (recordType == "material_overrides") {
            std::string objectName;
            std::size_t count = 0;
            in >> std::quoted(objectName) >> count;
            auto object = std::find_if(m_objects.begin(), m_objects.end(),
                [&](const Object& candidate) { return candidate.name == objectName; });
            for (std::size_t i = 0; i < count; ++i) {
                std::string name, value;
                in >> std::quoted(name) >> std::quoted(value);
                if (object != m_objects.end())
                    object->materialParameterOverrides[name] = value;
            }
            continue;
        }
        if (recordType == "environment") {
            if (version < 8) {
                std::string rest;
                std::getline(in, rest);
                continue;
            }

            int fog = 1;
            int driveSun = 1;
            int showGuides = 1;
            int selectedGuidesOnly = 1;
            int ibl = 1;
            int ssao = 0;
            int ssr = 0;
            int directionalShadows = 1;
            int pointShadows = 1;
            int spotShadows = 1;
            int physicsBroadPhase = m_environment.physicsBroadPhase ? 1 : 0;
            int physicsAllowSleeping = m_environment.physicsAllowSleeping ? 1 : 0;
            int showPhysicsGuides = m_environment.showPhysicsGuides ? 1 : 0;
            int selectedPhysicsGuideOnly = m_environment.selectedPhysicsGuideOnly ? 1 : 0;
            in >> m_environment.timeOfDay
               >> m_environment.skyLightIntensity;
            if (version >= 9) {
                in >> driveSun
                   >> m_environment.sunIntensity;
            }
            if (version >= 10) {
                in >> showGuides
                   >> selectedGuidesOnly;
            }
            if (version >= 11) {
                in >> ibl
                   >> ssao
                   >> m_environment.ssaoRadius
                   >> m_environment.ssaoBias
                   >> ssr
                   >> m_environment.ssrIntensity
                   >> directionalShadows
                   >> pointShadows
                   >> spotShadows
                   >> m_environment.shadowSoftness;
            }
            in >> fog;
            if (version >= 12) {
                in >> m_environment.fogColor.r
                >> m_environment.fogColor.g
                >> m_environment.fogColor.b;
            }
            in >> m_environment.fogDensity
               >> m_environment.fogHeight
               >> m_environment.fogHeightFalloff;
            if (version >= 15) {
                in >> m_environment.physicsGravity.x
                   >> m_environment.physicsGravity.y
                   >> m_environment.physicsGravity.z
                   >> m_environment.physicsSolverIterations
                   >> physicsBroadPhase
                   >> m_environment.physicsCellSize
                   >> m_environment.physicsRestitutionThreshold
                   >> physicsAllowSleeping
                   >> m_environment.physicsSleepLinearVelocity;
                if (version >= 17) {
                    in >> m_environment.physicsSleepAngularVelocity;
                }
                in >> m_environment.physicsTimeToSleep;
            }
            if (version >= 16) {
                in >> showPhysicsGuides
                   >> selectedPhysicsGuideOnly;
            }
            if (version >= 61) {
                int msaa = 1, fxaa = 1;
                in >> msaa >> fxaa;
                m_environment.msaa = msaa != 0;
                m_environment.fxaa = fxaa != 0;
            }
            if (version >= 63) {
                in >> m_environment.renderScale;
            }
            if (version >= 73) {
                std::string hudTok;
                in >> hudTok;
                m_environment.hudAsset = (hudTok == "~") ? std::string() : hudTok;
            }
            if (!in) {
                if (error) *error = "Scene file contains an invalid environment record.";
                Clear();
                return false;
            }
            m_environment.driveSunLight = driveSun != 0;
            m_environment.showLightGuides = showGuides != 0;
            m_environment.selectedLightGuideOnly = selectedGuidesOnly != 0;
            m_environment.ibl = ibl != 0;
            m_environment.ssao = ssao != 0;
            m_environment.ssr = ssr != 0;
            m_environment.directionalShadows = directionalShadows != 0;
            m_environment.pointShadows = pointShadows != 0;
            m_environment.spotShadows = spotShadows != 0;
            m_environment.fog = fog != 0;
            m_environment.physicsBroadPhase = physicsBroadPhase != 0;
            m_environment.physicsAllowSleeping = physicsAllowSleeping != 0;
            m_environment.showPhysicsGuides = showPhysicsGuides != 0;
            m_environment.selectedPhysicsGuideOnly = selectedPhysicsGuideOnly != 0;
            continue;
        }

        if (recordType == "camera") {
            CameraPreset camera;
            int primary = 0;
            int useInPlay = 0;
            in >> std::quoted(camera.name)
               >> camera.position.x >> camera.position.y >> camera.position.z
               >> camera.target.x >> camera.target.y >> camera.target.z
               >> camera.fov >> camera.nearPlane >> camera.farPlane;
            if (version >= 65) {
                in >> camera.blendDuration >> camera.blendEasing;
            }
            in >> primary >> useInPlay;
            if (!in) {
                if (error) *error = "Scene file contains an invalid camera record.";
                Clear();
                return false;
            }
            camera.name = camera.name.empty() ? "Camera" : camera.name;
            camera.fov = std::clamp(camera.fov, 10.0f, 120.0f);
            camera.nearPlane = std::max(camera.nearPlane, 0.001f);
            camera.farPlane = std::max(camera.farPlane, camera.nearPlane + 0.01f);
            camera.blendDuration = std::max(camera.blendDuration, 0.0f);
            camera.blendEasing = std::clamp(camera.blendEasing, 0, 3);
            camera.primary = primary != 0;
            camera.useInPlay = useInPlay != 0;
            if (camera.primary) {
                for (CameraPreset& existing : m_cameraPresets) existing.primary = false;
            }
            m_cameraPresets.push_back(std::move(camera));
            continue;
        }

        if (recordType == "camera_sequence" && version >= 68) {
            CameraSequence sequence;
            int loop = 0;
            std::size_t shotCount = 0;
            in >> std::quoted(sequence.name) >> loop >> shotCount;
            if (!in || shotCount > 1024) {
                if (error) *error = "Scene file contains an invalid camera sequence.";
                Clear();
                return false;
            }
            sequence.name = sequence.name.empty() ? "Camera Sequence" : sequence.name;
            sequence.loop = loop != 0;
            sequence.shots.reserve(shotCount);
            for (std::size_t i = 0; i < shotCount; ++i) {
                CameraSequenceShot shot;
                in >> std::quoted(shot.cameraName)
                   >> shot.travelDuration >> shot.holdDuration >> shot.easing;
                if (version >= 70) {
                    in >> shot.pathMode >> std::quoted(shot.eventName);
                    if (shot.eventName == "-") shot.eventName.clear();
                }
                if (!in) {
                    if (error) *error = "Scene file contains an invalid camera sequence shot.";
                    Clear();
                    return false;
                }
                shot.travelDuration = std::max(shot.travelDuration, 0.0f);
                shot.holdDuration = std::max(shot.holdDuration, 0.0f);
                shot.easing = std::clamp(shot.easing, 0, 3);
                shot.pathMode = std::clamp(shot.pathMode, 0, 1);
                sequence.shots.push_back(std::move(shot));
            }
            if (version >= 71) {
                std::size_t cueCount = 0;
                in >> cueCount;
                if (!in || cueCount > 4096) {
                    if (error) *error = "Scene file contains invalid cinematic cues.";
                    Clear();
                    return false;
                }
                sequence.cues.reserve(cueCount);
                for (std::size_t i = 0; i < cueCount; ++i) {
                    CinematicCue cue;
                    int type = 0;
                    in >> type >> cue.time
                       >> std::quoted(cue.name)
                       >> std::quoted(cue.assetPath)
                       >> std::quoted(cue.targetObject)
                       >> std::quoted(cue.animationClip)
                       >> cue.volume;
                    if (!in) {
                        if (error) *error = "Scene file contains an invalid cinematic cue.";
                        Clear();
                        return false;
                    }
                    cue.type = static_cast<CinematicCueType>(std::clamp(type, 0, 2));
                    cue.time = std::max(cue.time, 0.0f);
                    cue.volume = std::max(cue.volume, 0.0f);
                    if (cue.name == "-") cue.name.clear();
                    if (cue.assetPath == "-") cue.assetPath.clear();
                    if (cue.targetObject == "-") cue.targetObject.clear();
                    if (cue.animationClip == "-") cue.animationClip.clear();
                    sequence.cues.push_back(std::move(cue));
                }
            }
            m_cameraSequences.push_back(std::move(sequence));
            continue;
        }

        if (recordType == "light") {
            if (version < 7) {
                std::string rest;
                std::getline(in, rest);
                continue;
            }

            std::string name;
            std::string lightTypeName;
            Transform transform;
            Light light;
            int visible = 1;
            int locked = 0;
            in >> name >> lightTypeName
               >> transform.position.x >> transform.position.y >> transform.position.z
               >> light.color.r >> light.color.g >> light.color.b
               >> light.intensity
               >> light.direction.x >> light.direction.y >> light.direction.z
               >> light.innerAngle >> light.outerAngle >> light.range >> light.sourceRadius
               >> visible >> locked;

            if (!in || !ParseLightType(lightTypeName, &light.type)) {
                if (error) *error = "Scene file contains an invalid light record.";
                Clear();
                return false;
            }

            transform.scale = glm::vec3(0.22f);
            const glm::vec3 color = light.color * light.intensity;
            CreateObject(name, Primitive::Cube, cube, transform, color);
            Object& object = m_objects.back();
            object.light = true;
            object.lightData = light;
            object.visible = visible != 0;
            object.locked = locked != 0;
            m_registry.Add<Light>(object.entity, light);
            continue;
        }

        if (recordType == "joint") {
            if (version < 18) {
                std::string rest;
                std::getline(in, rest);
                continue;
            }

            std::string typeName;
            std::string objectA;
            std::string objectB;
            int enabled = 1;
            int worldAnchor = 0;
            int rope = 0;
            PhysicsJoint joint;
            in >> typeName
               >> enabled
               >> objectA
               >> objectB
               >> worldAnchor
               >> joint.anchor.x >> joint.anchor.y >> joint.anchor.z
               >> joint.restLength
               >> rope
               >> joint.stiffness
               >> joint.damping;

            if (!in || !ParsePhysicsJointType(typeName, &joint.type)) {
                if (error) *error = "Scene file contains an invalid joint record.";
                Clear();
                return false;
            }

            joint.enabled = enabled != 0;
            joint.objectA = objectA == "-" ? std::string() : objectA;
            joint.objectB = objectB == "-" ? std::string() : objectB;
            joint.worldAnchor = worldAnchor != 0;
            joint.rope = rope != 0;
            m_joints.push_back(joint);
            continue;
        }

        if (recordType == "terrain") {
            std::string name;
            int res = 128, seed = 1337, octaves = 5;
            float size = 64.0f, maxHeight = 8.0f, frequency = 2.0f;
            std::size_t hcount = 0;
            in >> name >> res >> size >> maxHeight >> seed >> octaves >> frequency >> hcount;
            std::vector<float> heights(hcount);
            for (std::size_t k = 0; k < hcount; ++k) in >> heights[k];
            std::size_t pcount = 0;
            in >> pcount;
            std::vector<unsigned char> paint(pcount);
            for (std::size_t k = 0; k < pcount; ++k) { int v = 0; in >> v; paint[k] = static_cast<unsigned char>(v); }
            for (Object& obj : m_objects) {
                if (obj.name == name) {
                    obj.isTerrain = true;
                    obj.terrainRes = res;
                    obj.terrainSize = size;
                    obj.terrainMaxHeight = maxHeight;
                    obj.terrainSeed = seed;
                    obj.terrainOctaves = octaves;
                    obj.terrainFrequency = frequency;
                    obj.terrainHeights = std::move(heights);
                    obj.terrainPaint = std::move(paint);
                    break;
                }
            }
            continue;
        }

        if (recordType == "water") {
            std::string name;
            float size = 80.0f, level = 0.0f, transparency = 0.72f, fresnel = 4.0f, spec = 1.2f, shininess = 220.0f;
            int res = 160;
            glm::vec3 shallow(0.0f), deep(0.0f), refl(0.0f);
            in >> name >> size >> res >> level
               >> shallow.r >> shallow.g >> shallow.b
               >> deep.r >> deep.g >> deep.b
               >> refl.r >> refl.g >> refl.b
               >> transparency >> fresnel >> spec >> shininess;
            for (Object& obj : m_objects) {
                if (obj.name == name) {
                    obj.isWater = true;
                    obj.waterSize = size;
                    obj.waterResolution = res;
                    obj.waterLevel = level;
                    obj.waterShallow = shallow;
                    obj.waterDeep = deep;
                    obj.waterReflection = refl;
                    obj.waterTransparency = transparency;
                    obj.waterFresnel = fresnel;
                    obj.waterSpecular = spec;
                    obj.waterShininess = shininess;
                    break;
                }
            }
            continue;
        }

        if (recordType != "object") {
            std::string rest;
            std::getline(in, rest);
            continue;
        }

        std::string primitiveName;
        std::string name;
        Primitive primitive = Primitive::Cube;
        Transform transform;
        glm::vec3 color;
        int visible = 1;
        int locked = 0;
        std::string modelAssetPath;
        std::string materialAssetPath;
        int skeletalModel = 0;
        int animationClipIndex = 0;
        std::string animationClipName;
        int animationAutoplay = 1;
        int animationLoop = 1;
        float animationSpeed = 1.0f;
        int animationLocomotionEnabled = 0;
        int animationIdleClipIndex = 0;
        int animationWalkClipIndex = 0;
        int animationRunClipIndex = 0;
        std::string animationIdleClipName;
        std::string animationWalkClipName;
        std::string animationRunClipName;
        float animationWalkAt = 0.15f;
        float animationRunAt = 3.0f;
        std::vector<AnimationEvent> animationEvents;
        std::vector<AnimationActionProfile> animationActionProfiles;
        std::vector<AnimationStateNode> animationStates;
        std::vector<AnimationParameter> animationParameters;
        std::vector<AnimationStateTransition> animationTransitions;
        glm::vec3 linearVelocity{0.0f};
        glm::vec3 angularVelocityAxis{0.0f, 1.0f, 0.0f};
        float angularVelocityRadians = 0.0f;
        int linearVelocityEnabled = 0;
        int angularVelocityEnabled = 0;
        int rigidBodyEnabled = 0;
        RigidBody rigidBody;
        int colliderEnabled = 0;
        Collider collider;
        int colliderShape = static_cast<int>(engine::ecs::ColliderShape::Sphere);
        int rigidBodyUseGravity = rigidBody.useGravity ? 1 : 0;
        int rigidBodyAllowSleep = rigidBody.allowSleep ? 1 : 0;
        int rigidBodyCcd = rigidBody.ccd ? 1 : 0;
        int rigidBodyFreezeRotation = rigidBody.freezeRotation ? 1 : 0;
        int colliderTrigger = collider.isTrigger ? 1 : 0;
        int rotatorEnabled = 0;
        engine::ecs::Rotator rotator;
        int moverEnabled = 0;
        engine::ecs::Mover mover;
        std::string triggerTargetName;
        int triggerEnterMoverAction = 0;
        int triggerEnterRotatorAction = 0;
        int triggerExitMoverAction = 0;
        int triggerExitRotatorAction = 0;
        int triggerEnterAudioAction = 0;
        int triggerExitAudioAction = 0;
        int triggerEnterParticleAction = 0;
        int triggerExitParticleAction = 0;
        std::string triggerCameraSequenceName;
        int triggerEnterCameraAction = 0;
        int triggerExitCameraAction = 0;
        int triggerCameraLockInput = 1;
        int triggerCameraSkippable = 1;
        int playerControllerEnabled = 0;
        int playerFirstPerson = 0;
        PlayerControllerSettings playerController;
        int cameraZoneEnabled = 0;
        std::string cameraZonePresetName;
        int cameraZoneRestoreOnExit = 1;
        int cameraZonePriority = 0;
        float cameraZoneReturnBlend = 0.35f;
        int healthEnabled = 0;
        engine::Health health;
        int healthAlive = health.alive ? 1 : 0;
        int scriptEnabled = 0;
        std::string scriptClassName;
        std::string scriptPath;
        std::vector<ScriptField> scriptFields;
        int navAgentEnabled = 0;
        float navAgentSpeed = 3.0f;
        float navAgentMaxForce = 20.0f;
        float navAgentReachRadius = 0.6f;
        float navAgentRepathInterval = 0.3f;
        std::vector<glm::vec3> patrolPoints;
        std::string navAgentTargetName;
        float navAgentVisionRange = 12.0f;
        float navAgentVisionHalfAngle = 45.0f;
        std::string navAgentBrainAsset;
        int navAgentTeam = 0;
        int navAgentAutoTarget = 0;
        int navMeshBoundsVolume = 0;
        int audioSourceEnabled = 0;
        std::string audioAssetPath;
        int audioBus = static_cast<int>(engine::AudioBus::SFX);
        int particleSystemEnabled = 0;
        engine::EmitterConfig particleConfig;
        std::vector<engine::ParticleEffectLayer> particleEffectLayers;
        int particleShape = static_cast<int>(particleConfig.shape);
        int particleBlend = static_cast<int>(particleConfig.blend);
        int particleAutoplay = 1;
        int particleLoop = 1;
        int particlePrewarm = 0;
        float particleDuration = 5.0f;
        float particleStartDelay = 0.0f;
        float particleSimulationSpeed = 1.0f;
        int particleLocalSpace = 1;
        int particleBurstCount = 0;
        float particleBurstInterval = 0.0f;
        std::string particleAssetPath;
        int particleAssetOverride = 0;
        float audioVolume = 1.0f;
        float audioPitch = 1.0f;
        int audioSpatial = 1;
        int audioLoop = 0;
        int audioAutoplay = 0;
        float audioMinDistance = 1.0f;
        float audioMaxDistance = 40.0f;
        float audioRolloff = 1.0f;
        float audioDopplerFactor = 1.0f;
        float audioConeInnerAngle = 360.0f;
        float audioConeOuterAngle = 360.0f;
        float audioConeOuterGain = 1.0f;
        float audioOcclusion = 0.0f;
        int audioPriority = 50;

        in >> primitiveName >> name
           >> transform.position.x >> transform.position.y >> transform.position.z
           >> transform.scale.x >> transform.scale.y >> transform.scale.z;

        if (version >= 2) {
            in >> transform.rotation.w >> transform.rotation.x
               >> transform.rotation.y >> transform.rotation.z;
        }

        in >> color.r >> color.g >> color.b;

        if (version >= 3) {
            in >> visible;
        }
        if (version >= 4) {
            in >> locked;
        }
        if (version >= 5) {
            in >> modelAssetPath >> materialAssetPath;
            if (modelAssetPath == "-") {
                modelAssetPath.clear();
            }
            if (materialAssetPath == "-") {
                materialAssetPath.clear();
            }
        }
        if (version >= 29) {
            in >> skeletalModel
               >> animationClipIndex
               >> animationClipName
               >> animationAutoplay
               >> animationLoop
               >> animationSpeed;
            if (animationClipName == "-") {
                animationClipName.clear();
            }
        }
        if (version >= 30) {
            in >> animationLocomotionEnabled
               >> animationIdleClipIndex
               >> animationIdleClipName
               >> animationWalkClipIndex
               >> animationWalkClipName
               >> animationRunClipIndex
               >> animationRunClipName
               >> animationWalkAt
               >> animationRunAt;
            if (animationIdleClipName == "-") {
                animationIdleClipName.clear();
            }
            if (animationWalkClipName == "-") {
                animationWalkClipName.clear();
            }
            if (animationRunClipName == "-") {
                animationRunClipName.clear();
            }
        }
        if (version >= 31) {
            std::size_t eventCount = 0;
            in >> eventCount;
            for (std::size_t i = 0; i < eventCount; ++i) {
                AnimationEvent event;
                in >> event.clipIndex
                   >> event.time
                   >> event.name;
                event.clipIndex = std::max(event.clipIndex, 0);
                event.time = std::max(event.time, 0.0f);
                if (event.name == "-") {
                    event.name.clear();
                }
                animationEvents.push_back(event);
            }
        }
        if (version >= 32) {
            std::size_t profileCount = 0;
            in >> profileCount;
            for (std::size_t i = 0; i < profileCount; ++i) {
                AnimationActionProfile profile;
                in >> profile.name
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
                animationActionProfiles.push_back(profile);
            }
        }
        if (version >= 33) {
            std::size_t stateCount = 0;
            in >> stateCount;
            for (std::size_t i = 0; i < stateCount; ++i) {
                AnimationStateNode state;
                int loop = 1;
                in >> state.name
                   >> state.clipIndex
                   >> state.clipName
                   >> loop
                   >> state.speed;
                if (version >= 36) {
                    int rootMotion = 0;
                    in >> state.blendClipIndex
                       >> state.blendClipName
                       >> state.blendParameter
                       >> state.blendMin
                       >> state.blendMax
                       >> rootMotion;
                    if (state.blendClipName == "-") state.blendClipName.clear();
                    if (state.blendParameter == "-") state.blendParameter.clear();
                    state.rootMotion = rootMotion != 0;
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
                animationStates.push_back(state);
            }

            if (version >= 36) {
                std::size_t parameterCount = 0;
                in >> parameterCount;
                for (std::size_t i = 0; i < parameterCount; ++i) {
                    AnimationParameter parameter;
                    int type = 0;
                    in >> parameter.name >> type >> parameter.defaultValue;
                    if (parameter.name == "-") parameter.name.clear();
                    parameter.type = static_cast<AnimationParameter::Type>(std::clamp(type, 0, 2));
                    animationParameters.push_back(parameter);
                }
            }

            std::size_t transitionCount = 0;
            in >> transitionCount;
            for (std::size_t i = 0; i < transitionCount; ++i) {
                AnimationStateTransition transition;
                int compare = 0;
                in >> transition.fromState
                   >> transition.toState
                   >> transition.parameter
                   >> compare
                   >> transition.threshold
                   >> transition.fade;
                if (version >= 34) {
                    in >> transition.exitTime;
                }
                if (version >= 35) {
                    in >> transition.priority
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
                if (compare < 0 || compare > static_cast<int>(AnimationStateTransition::Compare::NotEqual)) {
                    compare = 0;
                }
                transition.compare = static_cast<AnimationStateTransition::Compare>(compare);
                transition.fade = std::max(transition.fade, 0.0f);
                animationTransitions.push_back(transition);
            }
        }
        if (version >= 6) {
            in >> linearVelocity.x >> linearVelocity.y >> linearVelocity.z
            >> angularVelocityAxis.x >> angularVelocityAxis.y >> angularVelocityAxis.z
            >> angularVelocityRadians;
        }
        if (version >= 13) {
            in >> linearVelocityEnabled
               >> angularVelocityEnabled;
        } else {
            linearVelocityEnabled = glm::dot(linearVelocity, linearVelocity) > 0.0f ? 1 : 0;
            angularVelocityEnabled = angularVelocityRadians != 0.0f
                && glm::dot(angularVelocityAxis, angularVelocityAxis) > 0.0f ? 1 : 0;
        }
        if (version >= 14) {
            in >> rigidBodyEnabled
               >> rigidBody.velocity.x >> rigidBody.velocity.y >> rigidBody.velocity.z
               >> rigidBody.invMass
               >> rigidBodyUseGravity
               >> rigidBodyAllowSleep
               >> rigidBodyCcd;
            if (version >= 17) {
                in >> rigidBodyFreezeRotation;
            }
            in >> colliderEnabled
               >> colliderShape
               >> collider.radius;
            if (version >= 39) {
                in >> collider.halfHeight >> collider.majorRadius >> collider.minorRadius >> collider.steps;
            }
            in
               >> collider.halfExtents.x >> collider.halfExtents.y >> collider.halfExtents.z
               >> collider.planeNormal.x >> collider.planeNormal.y >> collider.planeNormal.z
               >> collider.planeOffset
               >> collider.restitution
               >> collider.friction
               >> colliderTrigger;
            rigidBody.useGravity = rigidBodyUseGravity != 0;
            rigidBody.allowSleep = rigidBodyAllowSleep != 0;
            rigidBody.ccd = rigidBodyCcd != 0;
            rigidBody.freezeRotation = version >= 17 && rigidBodyFreezeRotation != 0;
            collider.isTrigger = colliderTrigger != 0;
            if (version >= 75) {
                int kinematic = 0;
                in >> kinematic >> collider.layer >> collider.mask;
                rigidBody.kinematic = kinematic != 0;
            }
            if (colliderShape >= static_cast<int>(engine::ecs::ColliderShape::Sphere)
                && colliderShape <= static_cast<int>(engine::ecs::ColliderShape::Staircase))
                collider.shape = static_cast<engine::ecs::ColliderShape>(colliderShape);
            else
                collider.shape = engine::ecs::ColliderShape::Sphere;
        }
        if (version >= 19) {
            in >> rotatorEnabled
               >> rotator.axis.x >> rotator.axis.y >> rotator.axis.z
               >> rotator.radiansPerSecond;
        }
        if (version >= 20) {
            in >> moverEnabled
               >> mover.axis.x >> mover.axis.y >> mover.axis.z
               >> mover.distance
               >> mover.speed
               >> mover.phase;
        }
        if (version >= 21) {
            in >> triggerTargetName
               >> triggerEnterMoverAction
               >> triggerEnterRotatorAction;
            if (triggerTargetName == "-") {
                triggerTargetName.clear();
            }
            if (version == 21) {
                triggerEnterMoverAction = triggerEnterMoverAction != 0
                    ? static_cast<int>(TriggerActionMode::Toggle)
                    : static_cast<int>(TriggerActionMode::None);
                triggerEnterRotatorAction = triggerEnterRotatorAction != 0
                    ? static_cast<int>(TriggerActionMode::Toggle)
                    : static_cast<int>(TriggerActionMode::None);
            }
            if (version >= 23) {
                in >> triggerExitMoverAction
                   >> triggerExitRotatorAction;
            }
        }
        if (version >= 25) {
            in >> playerControllerEnabled
               >> playerFirstPerson
               >> playerController.walkSpeed
               >> playerController.runSpeed
               >> playerController.jumpSpeed
               >> playerController.lookSensitivity
               >> playerController.capsuleRadius
               >> playerController.capsuleHeight
               >> playerController.eyeHeight
               >> playerController.cameraDistance
               >> playerController.cameraTargetHeight
               >> playerController.maxSlopeDegrees
               >> playerController.stepHeight;
            if (version >= 64) {
                in >> playerController.cameraCollision
                   >> playerController.cameraProbeRadius
                   >> playerController.cameraCollisionPadding
                   >> playerController.cameraReturnSpeed;
            }
            if (version >= 67) {
                in >> playerController.shoulderCamera
                   >> playerController.shoulderOffset
                   >> playerController.shoulderSwitchSpeed
                   >> playerController.rightShoulder
                   >> playerController.lockOnEnabled
                   >> playerController.lockOnRange
                   >> playerController.lockOnViewAngle
                   >> playerController.lockOnTargetHeight
                   >> playerController.lockOnTrackingSpeed;
            }
            if (version >= 69) {
                in >> triggerCameraSequenceName
                   >> triggerEnterCameraAction
                   >> triggerExitCameraAction
                   >> triggerCameraLockInput
                   >> triggerCameraSkippable;
                if (triggerCameraSequenceName == "-") triggerCameraSequenceName.clear();
            }
            if (version >= 66) {
                in >> cameraZoneEnabled
                   >> cameraZonePresetName
                   >> cameraZoneRestoreOnExit
                   >> cameraZonePriority
                   >> cameraZoneReturnBlend;
                if (cameraZonePresetName == "-") cameraZonePresetName.clear();
            }
            playerController.firstPerson = playerFirstPerson != 0;
        }
        if (version >= 28) {
            in >> healthEnabled
               >> health.hp
               >> health.maxHp
               >> healthAlive;
            health.alive = healthAlive != 0;
            health.justDied = false;
        }
        if (version >= 26) {
            in >> scriptEnabled
               >> scriptClassName
               >> scriptPath;
            if (scriptClassName == "-") {
                scriptClassName.clear();
            }
            if (scriptPath == "-") {
                scriptPath.clear();
            }
            if (version >= 27) {
                std::size_t scriptFieldCount = 0;
                in >> scriptFieldCount;
                for (std::size_t i = 0; i < scriptFieldCount; ++i) {
                    ScriptField field;
                    int fieldType = 0;
                    in >> field.name >> fieldType >> field.value;
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
                    scriptFields.push_back(field);
                }
            }
        }

        if (version >= 37) {
            in >> navAgentEnabled >> navAgentSpeed >> navAgentMaxForce
               >> navAgentReachRadius >> navAgentRepathInterval;
            std::size_t patrolCount = 0;
            in >> patrolCount;
            for (std::size_t i = 0; i < patrolCount; ++i) {
                glm::vec3 p(0.0f);
                in >> p.x >> p.y >> p.z;
                patrolPoints.push_back(p);
            }
        }
        if (version >= 38) {
            in >> navAgentTargetName >> navAgentVisionRange >> navAgentVisionHalfAngle;
            if (navAgentTargetName == "-") {
                navAgentTargetName.clear();
            }
        }
        if (version >= 40) {
            in >> navAgentBrainAsset;
            if (navAgentBrainAsset == "-") {
                navAgentBrainAsset.clear();
            }
        }
        if (version >= 41) {
            in >> navMeshBoundsVolume;
        }
        if (version >= 42) {
            in >> audioSourceEnabled >> audioAssetPath >> audioVolume >> audioPitch
               >> audioSpatial >> audioLoop >> audioAutoplay
               >> audioMinDistance >> audioMaxDistance >> audioRolloff;
            if (audioAssetPath == "-") audioAssetPath.clear();
        }
        if (version >= 43) {
            in >> navAgentTeam >> navAgentAutoTarget;
        }
        if (version >= 44) {
            in >> triggerEnterAudioAction >> triggerExitAudioAction;
        }
        if (version >= 45) {
            in >> audioBus;
        }
        if (version >= 72) {
            in >> audioDopplerFactor >> audioConeInnerAngle >> audioConeOuterAngle
               >> audioConeOuterGain >> audioOcclusion >> audioPriority;
        }
        if (version >= 46) {
            in >> particleSystemEnabled
               >> particleConfig.rate >> particleConfig.maxParticles
               >> particleShape >> particleConfig.shapeRadius
               >> particleConfig.direction.x >> particleConfig.direction.y >> particleConfig.direction.z
               >> particleConfig.coneAngleDeg
               >> particleConfig.speedMin >> particleConfig.speedMax
               >> particleConfig.lifeMin >> particleConfig.lifeMax
               >> particleConfig.gravity.x >> particleConfig.gravity.y >> particleConfig.gravity.z
               >> particleConfig.drag
               >> particleConfig.startColor.r >> particleConfig.startColor.g
               >> particleConfig.startColor.b >> particleConfig.startColor.a
               >> particleConfig.endColor.r >> particleConfig.endColor.g
               >> particleConfig.endColor.b >> particleConfig.endColor.a
               >> particleConfig.startSize >> particleConfig.endSize
               >> particleBlend
               >> particleAutoplay >> particleLoop
               >> particleDuration >> particleStartDelay >> particleSimulationSpeed
               >> particleLocalSpace >> particleBurstCount >> particleBurstInterval;
            if (version >= 47) in >> particlePrewarm;
            if (version >= 48) {
                int useSizeCurve = 0, useColorCurve = 0, textureLoop = 1;
                in >> particleConfig.rotationMinDeg >> particleConfig.rotationMaxDeg
                   >> particleConfig.angularVelocityMinDeg >> particleConfig.angularVelocityMaxDeg
                   >> useSizeCurve >> useColorCurve;
                for (float& key : particleConfig.sizeCurve) in >> key;
                for (float& key : particleConfig.colorCurve) in >> key;
                in >> std::quoted(particleConfig.texturePath) >> particleConfig.textureColumns
                   >> particleConfig.textureRows >> particleConfig.textureFps >> textureLoop;
                if (particleConfig.texturePath == "-") particleConfig.texturePath.clear();
                particleConfig.useSizeCurve = useSizeCurve != 0;
                particleConfig.useColorCurve = useColorCurve != 0;
                particleConfig.textureLoop = textureLoop != 0;
            }
            if (version >= 49) {
                in >> std::quoted(particleAssetPath) >> particleAssetOverride;
                if (particleAssetPath == "-") particleAssetPath.clear();
            }
            if (version >= 50) {
                int cullingEnabled = 1;
                in >> cullingEnabled >> particleConfig.boundsRadius;
                particleConfig.cullingEnabled = cullingEnabled != 0;
                particleConfig.boundsRadius = std::max(particleConfig.boundsRadius, 0.01f);
            }
            if (version >= 51) {
                in >> triggerEnterParticleAction >> triggerExitParticleAction;
            }
            if (version >= 52) {
                int collisionEnabled = 0;
                int collisionResponse = 0;
                in >> collisionEnabled >> collisionResponse >> particleConfig.collisionRadius
                   >> particleConfig.collisionBounce >> particleConfig.collisionFriction
                   >> particleConfig.collisionLifetimeLoss;
                particleConfig.collisionEnabled = collisionEnabled != 0;
                particleConfig.collisionResponse = static_cast<engine::ParticleCollisionResponse>(
                    std::clamp(collisionResponse, 0, 1));
                particleConfig.collisionRadius = std::max(particleConfig.collisionRadius, 0.0f);
                particleConfig.collisionBounce = std::max(particleConfig.collisionBounce, 0.0f);
                particleConfig.collisionFriction = std::clamp(particleConfig.collisionFriction, 0.0f, 1.0f);
                particleConfig.collisionLifetimeLoss = std::clamp(
                    particleConfig.collisionLifetimeLoss, 0.0f, 1.0f);
            }
            if (version >= 53) {
                int trailsEnabled = 0;
                in >> trailsEnabled >> particleConfig.trailSegments >> particleConfig.trailLength
                   >> particleConfig.trailWidth >> particleConfig.trailOpacity;
                particleConfig.trailsEnabled = trailsEnabled != 0;
                particleConfig.trailSegments = std::clamp(particleConfig.trailSegments, 2, 16);
                particleConfig.trailLength = std::max(particleConfig.trailLength, 0.001f);
                particleConfig.trailWidth = std::max(particleConfig.trailWidth, 0.0f);
                particleConfig.trailOpacity = std::clamp(particleConfig.trailOpacity, 0.0f, 1.0f);
            }
            if (version >= 54) {
                std::size_t layerCount = 0;
                in >> layerCount;
                layerCount = std::min<std::size_t>(layerCount, 64);
                particleEffectLayers.reserve(layerCount);
                for (std::size_t layerIndex = 0; layerIndex < layerCount; ++layerIndex) {
                    engine::ParticleEffectLayer layer;
                    int layerEnabled = 1;
                    in >> std::quoted(layer.name) >> std::quoted(layer.assetPath) >> layerEnabled
                       >> layer.offset.x >> layer.offset.y >> layer.offset.z;
                    layer.enabled = layerEnabled != 0;
                    particleEffectLayers.push_back(std::move(layer));
                }
            }
            if (version >= 55) {
                int renderMode = 0, meshShape = 0, align = 1;
                in >> renderMode >> meshShape >> std::quoted(particleConfig.meshPath)
                   >> particleConfig.meshScale >> align;
                particleConfig.renderMode = static_cast<engine::ParticleRenderMode>(
                    std::clamp(renderMode, 0, 1));
                particleConfig.meshShape = static_cast<engine::ParticleMeshShape>(
                    std::clamp(meshShape, 0, 4));
                particleConfig.meshAlignToVelocity = align != 0;
                if (particleConfig.meshPath == "-") particleConfig.meshPath.clear();
                particleConfig.meshScale = std::max(particleConfig.meshScale, 0.001f);
            }
            if (version >= 56) {
                int backend = 0;
                in >> backend;
                particleConfig.simulationBackend = static_cast<engine::ParticleSimulationBackend>(
                    std::clamp(backend, 0, 2));
            }
            if (version >= 57) {
                std::size_t moduleCount = 0;
                in >> moduleCount;
                if (moduleCount > 32) { if (error) *error = "Particle module stack is too large."; Clear(); return false; }
                particleConfig.modules.clear();
                for (std::size_t moduleIndex = 0; moduleIndex < moduleCount; ++moduleIndex) {
                    int type = 0, enabledValue = 1;
                    in >> type >> enabledValue;
                    engine::ParticleModule module;
                    module.type = static_cast<engine::ParticleModuleType>(std::clamp(type, 0,
                        static_cast<int>(engine::ParticleModuleType::Renderer)));
                    module.enabled = enabledValue != 0;
                    if (version >= 58) {
                        int initialized = 0;
                        in >> module.instanceId >> std::quoted(module.name) >> initialized
                           >> module.vectorValue.x >> module.vectorValue.y
                           >> module.vectorValue.z >> module.valueA;
                        if (version >= 59) in >> module.valueB >> module.valueC >> module.valueD;
                        if (version >= 60) {
                            int curveEnabled = 0;
                            in >> module.colorValueA.r >> module.colorValueA.g
                               >> module.colorValueA.b >> module.colorValueA.a
                               >> module.colorValueB.r >> module.colorValueB.g
                               >> module.colorValueB.b >> module.colorValueB.a;
                            for (float& key : module.curveValues) in >> key;
                            in >> curveEnabled;
                            module.curveEnabled = curveEnabled != 0;
                        }
                        if (version >= 62) {
                            int stage = 0;
                            in >> stage;
                            module.stage = static_cast<engine::ParticleModuleStage>(std::clamp(stage, 0, 2));
                        }
                        module.parametersInitialized = initialized != 0;
                    }
                    if (type >= 0 && type <= static_cast<int>(engine::ParticleModuleType::Renderer))
                        particleConfig.modules.push_back(std::move(module));
                }
                engine::NormalizeParticleModuleStack(particleConfig, version >= 60);
            } else {
                engine::NormalizeParticleModuleStack(particleConfig, false);
            }
            particleShape = std::clamp(particleShape,
                static_cast<int>(engine::EmitShape::Point), static_cast<int>(engine::EmitShape::Cone));
            particleBlend = std::clamp(particleBlend,
                static_cast<int>(engine::ParticleBlend::Additive), static_cast<int>(engine::ParticleBlend::Alpha));
            particleConfig.shape = static_cast<engine::EmitShape>(particleShape);
            particleConfig.blend = static_cast<engine::ParticleBlend>(particleBlend);
        }

        if (!in || !ParsePrimitive(primitiveName, &primitive)) {
            if (error) *error = "Scene file contains an invalid object record.";
            Clear();
            return false;
        }

        CreateObject(name, primitive, MeshFor(primitive, cube, plane, sphere, capsule, cylinder, cone, pyramid, torus, staircase), transform, color);
        m_objects.back().visible = visible != 0;
        m_objects.back().locked = locked != 0;
        m_objects.back().modelAssetPath = modelAssetPath;
        m_objects.back().materialAssetPath = materialAssetPath;
        m_objects.back().skeletalModel = skeletalModel != 0;
        m_objects.back().animationClipIndex = std::max(animationClipIndex, 0);
        m_objects.back().animationClipName = animationClipName;
        m_objects.back().animationAutoplay = animationAutoplay != 0;
        m_objects.back().animationLoop = animationLoop != 0;
        m_objects.back().animationSpeed = std::max(animationSpeed, 0.0f);
        m_objects.back().animationLocomotionEnabled = animationLocomotionEnabled != 0;
        m_objects.back().animationIdleClipIndex = std::max(animationIdleClipIndex, 0);
        m_objects.back().animationWalkClipIndex = std::max(animationWalkClipIndex, 0);
        m_objects.back().animationRunClipIndex = std::max(animationRunClipIndex, 0);
        m_objects.back().animationIdleClipName = animationIdleClipName;
        m_objects.back().animationWalkClipName = animationWalkClipName;
        m_objects.back().animationRunClipName = animationRunClipName;
        m_objects.back().animationWalkAt = std::max(animationWalkAt, 0.0f);
        m_objects.back().animationRunAt = std::max(animationRunAt, m_objects.back().animationWalkAt);
        m_objects.back().animationEvents = animationEvents;
        m_objects.back().animationActionProfiles = animationActionProfiles;
        m_objects.back().animationStates = animationStates;
        m_objects.back().animationParameters = animationParameters;
        m_objects.back().animationTransitions = animationTransitions;
        m_objects.back().linearVelocityEnabled = linearVelocityEnabled != 0;
        m_objects.back().angularVelocityEnabled = angularVelocityEnabled != 0;
        m_objects.back().linearVelocity = linearVelocity;
        m_objects.back().angularVelocityAxis = angularVelocityAxis;
        m_objects.back().angularVelocityRadians = angularVelocityRadians;
        m_objects.back().rigidBodyEnabled = rigidBodyEnabled != 0;
        m_objects.back().rigidBody = rigidBody;
        m_objects.back().colliderEnabled = colliderEnabled != 0;
        m_objects.back().collider = collider;
        m_objects.back().rotatorEnabled = rotatorEnabled != 0;
        m_objects.back().rotator = rotator;
        m_objects.back().moverEnabled = moverEnabled != 0;
        m_objects.back().mover = mover;
        m_objects.back().triggerTargetName = triggerTargetName;
        m_objects.back().triggerEnterMoverAction = TriggerActionModeFromInt(triggerEnterMoverAction);
        m_objects.back().triggerEnterRotatorAction = TriggerActionModeFromInt(triggerEnterRotatorAction);
        m_objects.back().triggerExitMoverAction = TriggerActionModeFromInt(triggerExitMoverAction);
        m_objects.back().triggerExitRotatorAction = TriggerActionModeFromInt(triggerExitRotatorAction);
        m_objects.back().triggerEnterAudioAction = AudioActionFromInt(triggerEnterAudioAction);
        m_objects.back().triggerExitAudioAction = AudioActionFromInt(triggerExitAudioAction);
        m_objects.back().triggerEnterParticleAction = static_cast<engine::ParticleAction>(
            std::clamp(triggerEnterParticleAction, 0, static_cast<int>(engine::ParticleAction::Clear)));
        m_objects.back().triggerExitParticleAction = static_cast<engine::ParticleAction>(
            std::clamp(triggerExitParticleAction, 0, static_cast<int>(engine::ParticleAction::Clear)));
        m_objects.back().triggerCameraSequenceName = triggerCameraSequenceName;
        m_objects.back().triggerEnterCameraAction =
            static_cast<CameraSequenceTriggerAction>(std::clamp(triggerEnterCameraAction, 0, 3));
        m_objects.back().triggerExitCameraAction =
            static_cast<CameraSequenceTriggerAction>(std::clamp(triggerExitCameraAction, 0, 3));
        m_objects.back().triggerCameraLockInput = triggerCameraLockInput != 0;
        m_objects.back().triggerCameraSkippable = triggerCameraSkippable != 0;
        m_objects.back().playerControllerEnabled = playerControllerEnabled != 0;
        playerController.cameraDistance = std::max(playerController.cameraDistance, 0.0f);
        playerController.cameraProbeRadius = std::max(playerController.cameraProbeRadius, 0.0f);
        playerController.cameraCollisionPadding = std::max(playerController.cameraCollisionPadding, 0.0f);
        playerController.cameraReturnSpeed = std::max(playerController.cameraReturnSpeed, 0.0f);
        playerController.shoulderOffset = std::max(playerController.shoulderOffset, 0.0f);
        playerController.shoulderSwitchSpeed = std::max(playerController.shoulderSwitchSpeed, 0.0f);
        playerController.lockOnRange = std::max(playerController.lockOnRange, 0.0f);
        playerController.lockOnViewAngle = std::clamp(playerController.lockOnViewAngle, 0.0f, 180.0f);
        playerController.lockOnTrackingSpeed = std::max(playerController.lockOnTrackingSpeed, 0.0f);
        m_objects.back().playerController = playerController;
        m_objects.back().cameraZoneEnabled = cameraZoneEnabled != 0;
        m_objects.back().cameraZonePresetName = cameraZonePresetName;
        m_objects.back().cameraZoneRestoreOnExit = cameraZoneRestoreOnExit != 0;
        m_objects.back().cameraZonePriority = cameraZonePriority;
        m_objects.back().cameraZoneReturnBlend = std::max(cameraZoneReturnBlend, 0.0f);
        m_objects.back().healthEnabled = healthEnabled != 0;
        m_objects.back().health = health;
        m_objects.back().scriptEnabled = scriptEnabled != 0;
        m_objects.back().scriptClassName = scriptClassName;
        m_objects.back().scriptPath = scriptPath;
        m_objects.back().scriptFields = scriptFields;
        m_objects.back().navAgentEnabled = navAgentEnabled != 0;
        m_objects.back().navAgentSpeed = navAgentSpeed;
        m_objects.back().navAgentMaxForce = navAgentMaxForce;
        m_objects.back().navAgentReachRadius = navAgentReachRadius;
        m_objects.back().navAgentRepathInterval = navAgentRepathInterval;
        m_objects.back().patrolPoints = patrolPoints;
        m_objects.back().navAgentTargetName = navAgentTargetName;
        m_objects.back().navAgentVisionRange = navAgentVisionRange;
        m_objects.back().navAgentVisionHalfAngle = navAgentVisionHalfAngle;
        m_objects.back().navAgentBrainAsset = navAgentBrainAsset;
        m_objects.back().navAgentTeam = navAgentTeam;
        m_objects.back().navAgentAutoTarget = navAgentAutoTarget != 0;
        m_objects.back().navMeshBoundsVolume = navMeshBoundsVolume != 0;
        m_objects.back().audioSourceEnabled = audioSourceEnabled != 0;
        m_objects.back().audioAssetPath = audioAssetPath;
        audioBus = std::clamp(audioBus, static_cast<int>(engine::AudioBus::Master),
                              static_cast<int>(engine::AudioBus::Ambient));
        m_objects.back().audioBus = static_cast<engine::AudioBus>(audioBus);
        m_objects.back().particleSystemEnabled = particleSystemEnabled != 0;
        m_objects.back().particleConfig = particleConfig;
        m_objects.back().particleAutoplay = particleAutoplay != 0;
        m_objects.back().particleLoop = particleLoop != 0;
        m_objects.back().particlePrewarm = particlePrewarm != 0;
        m_objects.back().particleDuration = std::max(particleDuration, 0.0f);
        m_objects.back().particleStartDelay = std::max(particleStartDelay, 0.0f);
        m_objects.back().particleSimulationSpeed = std::max(particleSimulationSpeed, 0.0f);
        m_objects.back().particleLocalSpace = particleLocalSpace != 0;
        m_objects.back().particleBurstCount = std::max(particleBurstCount, 0);
        m_objects.back().particleBurstInterval = std::max(particleBurstInterval, 0.0f);
        m_objects.back().particleAssetPath = particleAssetPath;
        m_objects.back().particleAssetOverride = particleAssetOverride != 0;
        m_objects.back().particleEffectLayers = std::move(particleEffectLayers);
        m_objects.back().audioVolume = std::clamp(audioVolume, 0.0f, 4.0f);
        m_objects.back().audioPitch = std::clamp(audioPitch, 0.01f, 4.0f);
        m_objects.back().audioSpatial = audioSpatial != 0;
        m_objects.back().audioLoop = audioLoop != 0;
        m_objects.back().audioAutoplay = audioAutoplay != 0;
        m_objects.back().audioMinDistance = std::max(audioMinDistance, 0.01f);
        m_objects.back().audioMaxDistance = std::max(audioMaxDistance, m_objects.back().audioMinDistance);
        m_objects.back().audioRolloff = std::max(audioRolloff, 0.0f);
        m_objects.back().audioDopplerFactor = std::max(audioDopplerFactor, 0.0f);
        m_objects.back().audioConeInnerAngle = std::clamp(audioConeInnerAngle, 0.0f, 360.0f);
        m_objects.back().audioConeOuterAngle = std::clamp(audioConeOuterAngle,
            m_objects.back().audioConeInnerAngle, 360.0f);
        m_objects.back().audioConeOuterGain = std::clamp(audioConeOuterGain, 0.0f, 1.0f);
        m_objects.back().audioOcclusion = std::clamp(audioOcclusion, 0.0f, 1.0f);
        m_objects.back().audioPriority = std::clamp(audioPriority, 0, 100);
    }

    m_selectedIndex = m_objects.empty() ? -1 : 0;
    m_dirty = false;
    ClearHistory();
    return true;
}

const EditorScene::Object * EditorScene::SelectedObject() const
{
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return nullptr;
    }
    return &m_objects[static_cast<std::size_t>(m_selectedIndex)];
}

engine::ecs::Transform * EditorScene::SelectedTransform()
{
    const Object* selected = SelectedObject();
    return selected ? m_registry.TryGet<Transform>(selected->entity) : nullptr;
}

const engine::ecs::Transform *EditorScene::TryGetTransform(engine::ecs::Entity entity) const
{
    return const_cast<engine::ecs::Registry&>(m_registry).TryGet<Transform>(entity);
}

const engine::ecs::MeshRenderer *EditorScene::TryGetMeshRenderer(engine::ecs::Entity entity) const
{
    return const_cast<engine::ecs::Registry&>(m_registry).TryGet<MeshRenderer>(entity);
}

const engine::ecs::Light* EditorScene::TryGetLight(engine::ecs::Entity entity) const
{
    return const_cast<engine::ecs::Registry&>(m_registry).TryGet<Light>(entity);
}

bool EditorScene::IsVisible(engine::ecs::Entity entity) const
{
    for (const Object& object : m_objects) {
        if (object.entity == entity) {
            return object.visible;
        }
    }
    return true;
}

bool EditorScene::SelectedLocked() const
{
    const Object* selected = SelectedObject();
    return selected ? selected->locked : false;
}

void EditorScene::SelectNext()
{
    if (m_objects.empty()) {
        m_selectedIndex = -1;
        return;
    }
    m_selectedIndex = (m_selectedIndex + 1) % static_cast<int>(m_objects.size());
}

void EditorScene::SelectPrevious()
{
    if (m_objects.empty()) {
        m_selectedIndex = -1;
        return;
    }
    m_selectedIndex = (m_selectedIndex <= 0)
        ? static_cast<int>(m_objects.size()) - 1
        : m_selectedIndex - 1;
}

void EditorScene::SelectIndex(int index)
{
    if (index < 0 || index >= static_cast<int>(m_objects.size())) {
        return;
    }
    m_selectedIndex = index;
}

void EditorScene::Deselect()
{
    m_selectedIndex = -1;
}

void EditorScene::MoveSelected(const glm::vec3 & delta)
{
    if (SelectedLocked()) {
        return;
    }

    if (Transform* transform = SelectedTransform()) {
        transform->position += delta;
        m_dirty = true;
    }
}

void EditorScene::RotateSelected(const glm::vec3 &axis, float degrees)
{
    if (SelectedLocked()) {
        return;
    }
    if (Transform* transform = SelectedTransform()) {
        if (glm::dot(axis, axis) <= 0.0001f) {
            return;
        }
        const glm::quat rotation = glm::angleAxis(glm::radians(degrees), glm::normalize(axis));
        transform->rotation = rotation * transform->rotation;
        m_dirty = true;
    }
}

void EditorScene::RotateSelectedYaw(float degrees)
{
    RotateSelected(glm::vec3(0.0f, 1.0f, 0.0f), degrees);
}

void EditorScene::ScaleSelectedAxis(const glm::vec3 &axis, float factor)
{
    if (SelectedLocked()) {
        return;
    }
    if (Transform* transform = SelectedTransform()) {
        if (axis.x != 0.0f) {
            transform->scale.x *= factor;
        }
        if (axis.y != 0.0f) {
            transform->scale.y *= factor;
        }
        if (axis.z != 0.0f) {
            transform->scale.z *= factor;
        }
        m_dirty = true;
    }
}

void EditorScene::ScaleSelected(float factor)
{
    if (SelectedLocked()) {
        return;
    }

    if (Transform* transform = SelectedTransform()) {
        transform->scale *= factor;
        m_dirty = true;
    }
}

bool EditorScene::SetSelectedTransform(const Transform& value) {
    if (SelectedLocked()) {
        return false;
    }

    Transform* transform = SelectedTransform();
    if (!transform) {
        return false;
    }

    if (transform->position == value.position
        && transform->scale == value.scale
        && transform->rotation == value.rotation) {
        return false;
    }

    if (!m_transformEditOpen) {
        PushUndoSnapshot();
    }
    *transform = value;
    m_dirty = true;
    return true;
}

void EditorScene::ResetSelectedTransform()
{
    if (SelectedLocked()) {
        return;
    }

    if (Transform* transform = SelectedTransform()) {
        PushUndoSnapshot();
        transform->position = glm::vec3(0.0f);
        transform->scale = glm::vec3(1.0f);
        transform->rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        m_dirty = true;
    }
}

void EditorScene::BeginTransformEdit()
{
    if (!m_transformEditOpen && SelectedTransform()) {
        PushUndoSnapshot();
        m_transformEditOpen = true;
    }
}

void EditorScene::EndTransformEdit()
{
    m_transformEditOpen = false;
}

void EditorScene::BeginParticleEdit()
{
    if (!m_particleEditOpen && SelectedObject()) {
        PushUndoSnapshot();
        m_particleEditOpen = true;
    }
}

void EditorScene::EndParticleEdit()
{
    m_particleEditOpen = false;
}

bool EditorScene::Undo(const engine::Mesh & cube, const engine::Mesh & plane, const engine::Mesh & sphere, const engine::Mesh & capsule, const engine::Mesh & cylinder, const engine::Mesh & cone, const engine::Mesh & pyramid, const engine::Mesh & torus, const engine::Mesh & staircase)
{
    if (m_undoStack.empty()) {
        return false;
    }

    m_redoStack.push_back(CaptureSnapshot());
    RestoreSnapshot(m_undoStack.back(), cube, plane, sphere, capsule, cylinder, cone, pyramid, torus, staircase);
    m_undoStack.pop_back();
    m_dirty = true;
    m_transformEditOpen = false;
    return true;
}

bool EditorScene::Redo(const engine::Mesh & cube, const engine::Mesh & plane, const engine::Mesh & sphere, const engine::Mesh & capsule, const engine::Mesh & cylinder, const engine::Mesh & cone, const engine::Mesh & pyramid, const engine::Mesh & torus, const engine::Mesh & staircase)
{
    if (m_redoStack.empty()) {
        return false;
    }

    m_undoStack.push_back(CaptureSnapshot());
    RestoreSnapshot(m_redoStack.back(), cube, plane, sphere, capsule, cylinder, cone, pyramid, torus, staircase);
    m_redoStack.pop_back();
    m_dirty = true;
    m_transformEditOpen = false;
    return true;
}

EditorScene::Snapshot EditorScene::CreateSnapshot()
{
    return CaptureSnapshot();
}

void EditorScene::RestoreFromSnapshot(const Snapshot &snapshot, const engine::Mesh &cube, const engine::Mesh &plane, const engine::Mesh &sphere, const engine::Mesh &capsule, const engine::Mesh &cylinder, const engine::Mesh &cone, const engine::Mesh &pyramid, const engine::Mesh &torus, const engine::Mesh &staircase)
{
    RestoreSnapshot(snapshot, cube, plane, sphere, capsule, cylinder, cone, pyramid, torus, staircase);
    ClearHistory();
    m_dirty = false;
}

void EditorScene::AddCube(const engine::Mesh & cube)
{
    PushUndoSnapshot();

    Transform transform;
    transform.position = glm::vec3(0.0f, 0.25f, 0.0f);
    transform.scale = glm::vec3(0.9f);

    const glm::vec3 color(0.78f, 0.48f, 0.18f);
    CreateObject("Cube_" + std::to_string(m_nextCubeNumber++), Primitive::Cube, cube, transform, color);
    m_selectedIndex = static_cast<int>(m_objects.size()) - 1;
    m_dirty = true;
}

void EditorScene::AddPlane(const engine::Mesh & plane)
{
    PushUndoSnapshot();

    Transform transform;
    transform.position = glm::vec3(0.0f, 0.0f, 0.0f);
    transform.scale = glm::vec3(3.0f, 1.0f, 3.0f);

    const glm::vec3 color(0.34f, 0.37f, 0.41f);
    CreateObject("Plane_" + std::to_string(m_nextCubeNumber++), Primitive::Plane, plane, transform, color);
    m_selectedIndex = static_cast<int>(m_objects.size()) - 1;
    m_dirty = true;
}

void EditorScene::AddSphere(const engine::Mesh & sphere)
{
    PushUndoSnapshot();

    Transform transform;
    transform.position = glm::vec3(0.0f, 0.0f, 0.0f);
    transform.scale = glm::vec3(0.9f);

    const glm::vec3 color(0.68f, 0.27f, 0.31f);
    CreateObject("Sphere_" + std::to_string(m_nextCubeNumber++), Primitive::Sphere, sphere, transform, color);
    m_selectedIndex = static_cast<int>(m_objects.size()) - 1;
    m_dirty = true;
}

void EditorScene::AddCapsule(const engine::Mesh & capsule)
{
    PushUndoSnapshot();

    Transform transform;
    transform.position = glm::vec3(0.0f, 0.9f, 0.0f);

    const glm::vec3 color(0.27f, 0.48f, 0.78f);
    CreateObject("Capsule_" + std::to_string(m_nextCubeNumber++), Primitive::Capsule, capsule, transform, color);
    m_selectedIndex = static_cast<int>(m_objects.size()) - 1;
    m_dirty = true;
}

void EditorScene::AddCylinder(const engine::Mesh & cylinder)
{
    Transform transform;
    transform.position.y = 0.5f;
    AddConfiguredPrimitive(Primitive::Cylinder, cylinder, transform, nullptr);
}

void EditorScene::AddCone(const engine::Mesh & cone)
{
    Transform transform;
    transform.position.y = 0.5f;
    AddConfiguredPrimitive(Primitive::Cone, cone, transform, nullptr);
}

void EditorScene::AddNavMeshBoundsVolume(const engine::Mesh& cube)
{
    PushUndoSnapshot();
    Transform transform;
    transform.position = glm::vec3(0.0f, 1.0f, 0.0f);
    transform.scale = glm::vec3(20.0f, 2.0f, 20.0f);
    const std::string name = "NavMeshBoundsVolume_" + std::to_string(m_nextCubeNumber++);
    CreateObject(name, Primitive::Cube, cube, transform, glm::vec3(0.12f, 0.55f, 0.95f));
    m_objects.back().navMeshBoundsVolume = true;
    m_selectedIndex = static_cast<int>(m_objects.size()) - 1;
    m_dirty = true;
}

void EditorScene::AddConfiguredPrimitive(Primitive primitive, const engine::Mesh& mesh,
                                         const Transform& transform, const Collider* collider,
                                         const std::string& requestedName)
{
    PushUndoSnapshot();

    const glm::vec3 color = primitive == Primitive::Plane ? glm::vec3(0.34f, 0.37f, 0.41f)
        : primitive == Primitive::Sphere ? glm::vec3(0.68f, 0.27f, 0.31f)
        : primitive == Primitive::Capsule ? glm::vec3(0.27f, 0.48f, 0.78f)
        : primitive == Primitive::Cylinder ? glm::vec3(0.26f, 0.62f, 0.55f)
        : primitive == Primitive::Cone ? glm::vec3(0.72f, 0.42f, 0.18f)
        : primitive == Primitive::Pyramid ? glm::vec3(0.72f, 0.58f, 0.20f)
        : primitive == Primitive::Torus ? glm::vec3(0.48f, 0.32f, 0.72f)
        : primitive == Primitive::Staircase ? glm::vec3(0.48f, 0.50f, 0.54f)
        : glm::vec3(0.78f, 0.48f, 0.18f);
    const std::string generatedName = std::string(PrimitiveName(primitive)) + "_" + std::to_string(m_nextCubeNumber++);
    const std::string& name = requestedName.empty() ? generatedName : requestedName;
    CreateObject(name, primitive, mesh, transform, color);
    if (collider) {
        m_objects.back().colliderEnabled = true;
        m_objects.back().collider = *collider;
    }
    m_selectedIndex = static_cast<int>(m_objects.size()) - 1;
    m_dirty = true;
}

void EditorScene::AddDirectionalLight(const engine::Mesh& placeholderMesh) {
    PushUndoSnapshot();

    Transform transform;
    transform.position = glm::vec3(-2.5f, 3.0f, 1.5f);
    transform.scale = glm::vec3(0.22f);

    Light light;
    light.type = Light::Type::Directional;
    light.color = glm::vec3(1.0f, 0.92f, 0.82f);
    light.intensity = 4.0f;
    light.direction = glm::normalize(glm::vec3(-0.35f, -1.0f, -0.25f));

    CreateObject("DirectionalLight", Primitive::Cube, placeholderMesh, transform, light.color);
    Object& object = m_objects.back();
    object.light = true;
    object.lightData = light;
    m_registry.Add<Light>(object.entity, light);
    m_selectedIndex = static_cast<int>(m_objects.size()) - 1;
    m_dirty = true;
}

void EditorScene::AddPointLight(const engine::Mesh& placeholderMesh) {
    PushUndoSnapshot();

    Transform transform;
    transform.position = glm::vec3(1.8f, 1.6f, 1.4f);
    transform.scale = glm::vec3(0.18f);

    Light light;
    light.type = Light::Type::Point;
    light.color = glm::vec3(0.45f, 0.68f, 1.0f);
    light.intensity = 45.0f;
    light.range = 12.0f;

    CreateObject("PointLight", Primitive::Cube, placeholderMesh, transform, light.color);
    Object& object = m_objects.back();
    object.light = true;
    object.lightData = light;
    m_registry.Add<Light>(object.entity, light);
    m_selectedIndex = static_cast<int>(m_objects.size()) - 1;
    m_dirty = true;
}

void EditorScene::AddSpotLight(const engine::Mesh& placeholderMesh) {
    PushUndoSnapshot();

    Transform transform;
    transform.position = glm::vec3(0.0f, 3.0f, 2.5f);
    transform.scale = glm::vec3(0.2f);

    Light light;
    light.type = Light::Type::Spot;
    light.color = glm::vec3(1.0f, 0.86f, 0.58f);
    light.intensity = 70.0f;
    light.direction = glm::normalize(glm::vec3(0.0f, -1.0f, -0.35f));
    light.innerAngle = 18.0f;
    light.outerAngle = 32.0f;
    light.range = 18.0f;

    CreateObject("SpotLight", Primitive::Cube, placeholderMesh, transform, light.color);
    Object& object = m_objects.back();
    object.light = true;
    object.lightData = light;
    m_registry.Add<Light>(object.entity, light);
    m_selectedIndex = static_cast<int>(m_objects.size()) - 1;
    m_dirty = true;
}

void EditorScene::AddAreaLight(const engine::Mesh& placeholderMesh) {
    PushUndoSnapshot();

    Transform transform;
    transform.position = glm::vec3(-1.6f, 1.8f, 1.2f);
    transform.scale = glm::vec3(0.28f);

    Light light;
    light.type = Light::Type::Area;
    light.color = glm::vec3(1.0f, 0.72f, 0.42f);
    light.intensity = 80.0f;
    light.sourceRadius = 1.2f;

    CreateObject("AreaLight", Primitive::Cube, placeholderMesh, transform, light.color);
    Object& object = m_objects.back();
    object.light = true;
    object.lightData = light;
    m_registry.Add<Light>(object.entity, light);
    m_selectedIndex = static_cast<int>(m_objects.size()) - 1;
    m_dirty = true;
}

bool EditorScene::AddModel(const std::string &path, const engine::Mesh &placeholderMesh, const engine::ecs::Transform &transform)
{
    if (path.empty()) {
        return false;
    }

    PushUndoSnapshot();

    const std::filesystem::path filePath(path);
    std::string name = filePath.stem().string();
    if (name.empty()) {
        name = "Model";
    }

    CreateObject(name, Primitive::Cube, placeholderMesh, transform, glm::vec3(0.78f, 0.78f, 0.82f));
    m_objects.back().modelAssetPath = path;
    m_selectedIndex = static_cast<int>(m_objects.size()) - 1;
    m_dirty = true;
    return true;
}

bool EditorScene::CycleSelectedColor()
{
    const Object* selected = SelectedObject();
    if (!selected)
    {
        return false;
    }
    if (selected->locked) {
        return false;
    }

    MeshRenderer* renderer = m_registry.TryGet<MeshRenderer>(selected->entity);
    if (!renderer) {
        return false;
    }

    PushUndoSnapshot();

    int next = 0;
    const int paletteCount = static_cast<int>(sizeof(kPalette) / sizeof(kPalette[0]));
    for (int i = 0; i < paletteCount; ++i) {
        const glm::vec3 delta = renderer->color - kPalette[i];
        if (glm::dot(delta, delta) < 0.0001f) {
            next = (i + 1) % paletteCount;
            break;
        }
    }

    renderer->color = kPalette[next];
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedName(const std::string& name) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }

    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked || name.empty() || selected.name == name) {
        return false;
    }

    PushUndoSnapshot();
    selected.name = name;
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedColor(const glm::vec3& color) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }

    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    MeshRenderer* renderer = m_registry.TryGet<MeshRenderer>(selected.entity);
    if (!renderer || selected.locked || renderer->color == color) {
        return false;
    }

    PushUndoSnapshot();
    renderer->color = color;
    if (selected.light) {
        selected.lightData.color = color;
        if (Light* light = m_registry.TryGet<Light>(selected.entity)) {
            light->color = color;
        }
    }
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedPrimitive(Primitive primitive, const engine::Mesh & mesh)
{
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }

    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    MeshRenderer* renderer = m_registry.TryGet<MeshRenderer>(selected.entity);
    if (!renderer || selected.primitive == primitive) {
        return false;
    }

    PushUndoSnapshot();

    selected.primitive = primitive;
    renderer->mesh = &mesh;
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedModelAsset(const std::string &path)
{
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }

    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked) {
        return false;
    }

    PushUndoSnapshot();
    selected.modelAssetPath = path;
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedMaterialAsset(const std::string &path)
{
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }

    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked) {
        return false;
    }

    PushUndoSnapshot();
    selected.materialAssetPath = path;
    selected.materialParameterOverrides.clear();
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedMaterialParameterOverride(
    const std::string& name, const std::string& value)
{
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())
        || name.empty()) return false;
    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked) return false;
    PushUndoSnapshot();
    selected.materialParameterOverrides[name] = value;
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedAnimationSettings(bool skeletalModel,
                                               int clipIndex,
                                               const std::string& clipName,
                                               bool autoplay,
                                               bool loop,
                                               float speed) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }

    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked) {
        return false;
    }

    PushUndoSnapshot();
    selected.skeletalModel = skeletalModel;
    selected.animationClipIndex = std::max(clipIndex, 0);
    selected.animationClipName = clipName;
    selected.animationAutoplay = autoplay;
    selected.animationLoop = loop;
    selected.animationSpeed = std::max(speed, 0.0f);
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedAnimationLocomotion(bool enabled,
                                                 int idleClipIndex,
                                                 const std::string& idleClipName,
                                                 int walkClipIndex,
                                                 const std::string& walkClipName,
                                                 int runClipIndex,
                                                 const std::string& runClipName,
                                                 float walkAt,
                                                 float runAt) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }

    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked) {
        return false;
    }

    PushUndoSnapshot();
    selected.animationLocomotionEnabled = enabled;
    selected.animationIdleClipIndex = std::max(idleClipIndex, 0);
    selected.animationIdleClipName = idleClipName;
    selected.animationWalkClipIndex = std::max(walkClipIndex, 0);
    selected.animationWalkClipName = walkClipName;
    selected.animationRunClipIndex = std::max(runClipIndex, 0);
    selected.animationRunClipName = runClipName;
    selected.animationWalkAt = std::max(walkAt, 0.0f);
    selected.animationRunAt = std::max(runAt, selected.animationWalkAt);
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedAnimationEvents(const std::vector<AnimationEvent>& events) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }

    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked) {
        return false;
    }

    PushUndoSnapshot();
    selected.animationEvents = events;
    for (AnimationEvent& event : selected.animationEvents) {
        event.clipIndex = std::max(event.clipIndex, 0);
        event.time = std::max(event.time, 0.0f);
    }
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedAnimationActionProfiles(const std::vector<AnimationActionProfile>& profiles) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }

    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked) {
        return false;
    }

    PushUndoSnapshot();
    selected.animationActionProfiles = profiles;
    for (AnimationActionProfile& profile : selected.animationActionProfiles) {
        profile.clipIndex = std::max(profile.clipIndex, 0);
        profile.fadeIn = std::max(profile.fadeIn, 0.0f);
        profile.fadeOut = std::max(profile.fadeOut, 0.0f);
        profile.speed = std::max(profile.speed, 0.0f);
    }
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedAnimationStateGraph(const std::vector<AnimationStateNode>& states,
                                                 const std::vector<AnimationStateTransition>& transitions,
                                                 const std::vector<AnimationParameter>& parameters) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }

    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked) {
        return false;
    }

    PushUndoSnapshot();
    selected.animationStates = states;
    for (AnimationStateNode& state : selected.animationStates) {
        state.clipIndex = std::max(state.clipIndex, 0);
        state.speed = std::max(state.speed, 0.0f);
        state.blendClipIndex = std::max(state.blendClipIndex, -1);
        if (state.blendMax < state.blendMin) std::swap(state.blendMin, state.blendMax);
    }
    selected.animationParameters = parameters;
    for (AnimationParameter& parameter : selected.animationParameters) {
        if (parameter.type != AnimationParameter::Type::Float) {
            parameter.defaultValue = parameter.defaultValue != 0.0f ? 1.0f : 0.0f;
        }
    }
    selected.animationTransitions = transitions;
    for (AnimationStateTransition& transition : selected.animationTransitions) {
        transition.fade = std::max(transition.fade, 0.0f);
        transition.exitTime = std::clamp(transition.exitTime, 0.0f, 1.0f);
    }
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedLight(const Light& light) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }

    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked || !selected.light) {
        return false;
    }

    PushUndoSnapshot();
    selected.lightData = light;
    if (Light* component = m_registry.TryGet<Light>(selected.entity)) {
        *component = light;
    } else {
        m_registry.Add<Light>(selected.entity, light);
    }
    if (MeshRenderer* renderer = m_registry.TryGet<MeshRenderer>(selected.entity)) {
        renderer->color = light.color;
    }
    m_dirty = true;
    return true;
}

void EditorScene::SetEnvironment(const Environment& environment) {
    PushUndoSnapshot();
    m_environment = environment;
    m_dirty = true;
}

bool EditorScene::SetSelectedLinearVelocityEnabled(bool enabled)
{
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }

    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked || selected.linearVelocityEnabled == enabled) {
        return false;
    }

    PushUndoSnapshot();
    selected.linearVelocityEnabled = enabled;
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedAngularVelocityEnabled(bool enabled)
{ 
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }

    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked || selected.angularVelocityEnabled == enabled) {
        return false;
    }

    PushUndoSnapshot();
    selected.angularVelocityEnabled = enabled;
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedLinearVelocity(const glm::vec3 &velocity)
{
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }

    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked) {
        return false;
    }

    PushUndoSnapshot();
    selected.linearVelocity = velocity;
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedAngularVelocity(const glm::vec3 &axis, float radiansPerSecond)
{
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }

    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked) {
        return false;
    }

    PushUndoSnapshot();
    selected.angularVelocityAxis = axis;
    selected.angularVelocityRadians = radiansPerSecond;
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedRigidBodyEnabled(bool enabled)
{
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }

    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked || selected.rigidBodyEnabled == enabled) {
        return false;
    }

    PushUndoSnapshot();
    selected.rigidBodyEnabled = enabled;
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedRigidBody(const engine::ecs::RigidBody &rigidBody)
{
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }

    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked) {
        return false;
    }

    PushUndoSnapshot();
    selected.rigidBodyEnabled = true;
    selected.rigidBody = rigidBody;
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedColliderEnabled(bool enabled)
{
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }

    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked || selected.colliderEnabled == enabled) {
        return false;
    }

    PushUndoSnapshot();
    selected.colliderEnabled = enabled;
    if (enabled) {
        const Transform* transform = m_registry.TryGet<Transform>(selected.entity);
        if (selected.primitive == Primitive::Plane && selected.modelAssetPath.empty()) {
            selected.collider = Collider::MakePlane(glm::vec3(0.0f, 1.0f, 0.0f),
                transform ? transform->position.y : 0.0f);
        } else if (selected.primitive == Primitive::Sphere && selected.modelAssetPath.empty() && transform) {
            selected.collider = Collider::MakeSphere(
                std::max({transform->scale.x, transform->scale.y, transform->scale.z}) * 0.5f);
        } else if (selected.primitive == Primitive::Capsule && selected.modelAssetPath.empty() && transform) {
            const float radius = 0.4f * std::max(transform->scale.x, transform->scale.z);
            selected.collider = Collider::MakeCapsuleFromHeight(radius,
                std::max(1.8f * transform->scale.y, radius * 2.0f));
        } else if (selected.primitive == Primitive::Cylinder && selected.modelAssetPath.empty() && transform) {
            const float radius = 0.5f * std::max(transform->scale.x, transform->scale.z);
            selected.collider = Collider::MakeCylinder(radius, transform->scale.y);
        } else if (selected.primitive == Primitive::Cone && selected.modelAssetPath.empty() && transform) {
            selected.collider = Collider::MakeCone(
                0.5f * std::max(transform->scale.x, transform->scale.z), transform->scale.y);
        } else if (selected.primitive == Primitive::Pyramid && selected.modelAssetPath.empty() && transform) {
            selected.collider = Collider::MakePyramid(transform->scale * 0.5f);
        } else if (selected.primitive == Primitive::Torus && selected.modelAssetPath.empty() && transform) {
            const float radialScale = std::max(transform->scale.x, transform->scale.z);
            selected.collider = Collider::MakeTorus(0.35f * radialScale,
                0.15f * std::max(radialScale, transform->scale.y));
        } else if (selected.primitive == Primitive::Staircase && selected.modelAssetPath.empty() && transform) {
            selected.collider = Collider::MakeStaircase(transform->scale * 0.5f, 6);
        } else if (transform) {
            selected.collider = Collider::MakeBox(glm::vec3(
                std::max(transform->scale.x * 0.5f, 0.001f),
                std::max(transform->scale.y * 0.5f, 0.001f),
                std::max(transform->scale.z * 0.5f, 0.001f)));
        }
    }
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedCollider(const engine::ecs::Collider &collider)
{
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }

    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked) {
        return false;
    }

    PushUndoSnapshot();
    selected.colliderEnabled = true;
    selected.collider = collider;
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedRotatorEnabled(bool enabled)
{
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }

    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked || selected.rotatorEnabled == enabled) {
        return false;
    }

    PushUndoSnapshot();
    selected.rotatorEnabled = enabled;
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedRotator(const engine::ecs::Rotator &rotator)
{
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }

    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked) {
        return false;
    }

    PushUndoSnapshot();
    selected.rotatorEnabled = true;
    selected.rotator = rotator;
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedMoverEnabled(bool enabled)
{
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }

    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked || selected.moverEnabled == enabled) {
        return false;
    }

    PushUndoSnapshot();
    selected.moverEnabled = enabled;
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedMover(const engine::ecs::Mover &mover)
{
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }

    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked) {
        return false;
    }

    PushUndoSnapshot();
    selected.moverEnabled = true;
    selected.mover = mover;
    selected.mover.initialized = false;
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedTriggerAction(const std::string& targetName,
                                           TriggerActionMode enterMoverAction,
                                           TriggerActionMode enterRotatorAction,
                                           TriggerActionMode exitMoverAction,
                                           TriggerActionMode exitRotatorAction,
                                           engine::ecs::AudioAction enterAudioAction,
                                           engine::ecs::AudioAction exitAudioAction,
                                           engine::ParticleAction enterParticleAction,
                                           engine::ParticleAction exitParticleAction) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }

    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked) {
        return false;
    }
    if (selected.triggerTargetName == targetName
        && selected.triggerEnterMoverAction == enterMoverAction
        && selected.triggerEnterRotatorAction == enterRotatorAction
        && selected.triggerExitMoverAction == exitMoverAction
        && selected.triggerExitRotatorAction == exitRotatorAction
        && selected.triggerEnterAudioAction == enterAudioAction
        && selected.triggerEnterParticleAction == enterParticleAction
        && selected.triggerExitAudioAction == exitAudioAction
        && selected.triggerExitParticleAction == exitParticleAction) {
        return false;
    }

    PushUndoSnapshot();
    selected.triggerTargetName = targetName;
    selected.triggerEnterMoverAction = enterMoverAction;
    selected.triggerEnterRotatorAction = enterRotatorAction;
    selected.triggerExitMoverAction = exitMoverAction;
    selected.triggerExitRotatorAction = exitRotatorAction;
    selected.triggerEnterAudioAction = enterAudioAction;
    selected.triggerExitAudioAction = exitAudioAction;
    selected.triggerEnterParticleAction = enterParticleAction;
    selected.triggerExitParticleAction = exitParticleAction;
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedTriggerCameraSequence(
    const std::string& sequenceName,
    CameraSequenceTriggerAction enterAction,
    CameraSequenceTriggerAction exitAction,
    bool lockInput, bool skippable) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }
    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked) return false;
    if (selected.triggerCameraSequenceName == sequenceName
        && selected.triggerEnterCameraAction == enterAction
        && selected.triggerExitCameraAction == exitAction
        && selected.triggerCameraLockInput == lockInput
        && selected.triggerCameraSkippable == skippable) {
        return false;
    }
    PushUndoSnapshot();
    selected.triggerCameraSequenceName = sequenceName;
    selected.triggerEnterCameraAction = enterAction;
    selected.triggerExitCameraAction = exitAction;
    selected.triggerCameraLockInput = lockInput;
    selected.triggerCameraSkippable = skippable;
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedPlayerControllerEnabled(bool enabled) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }

    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked || selected.playerControllerEnabled == enabled) {
        return false;
    }

    PushUndoSnapshot();
    selected.playerControllerEnabled = enabled;
    if (enabled) {
        selected.colliderEnabled = true;
        selected.collider = engine::ecs::Collider::MakeCapsuleFromHeight(
            selected.playerController.capsuleRadius,
            selected.playerController.capsuleHeight);
    }
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedPlayerController(const PlayerControllerSettings& settings) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }

    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked) {
        return false;
    }

    PushUndoSnapshot();
    PlayerControllerSettings safe = settings;
    safe.cameraDistance = std::max(safe.cameraDistance, 0.0f);
    safe.cameraProbeRadius = std::max(safe.cameraProbeRadius, 0.0f);
    safe.cameraCollisionPadding = std::max(safe.cameraCollisionPadding, 0.0f);
    safe.cameraReturnSpeed = std::max(safe.cameraReturnSpeed, 0.0f);
    safe.shoulderOffset = std::max(safe.shoulderOffset, 0.0f);
    safe.shoulderSwitchSpeed = std::max(safe.shoulderSwitchSpeed, 0.0f);
    safe.lockOnRange = std::max(safe.lockOnRange, 0.0f);
    safe.lockOnViewAngle = std::clamp(safe.lockOnViewAngle, 0.0f, 180.0f);
    safe.lockOnTrackingSpeed = std::max(safe.lockOnTrackingSpeed, 0.0f);
    selected.playerControllerEnabled = true;
    selected.playerController = safe;
    selected.colliderEnabled = true;
    selected.collider = engine::ecs::Collider::MakeCapsuleFromHeight(safe.capsuleRadius, safe.capsuleHeight);
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedCameraZone(bool enabled, const std::string& presetName,
                                        bool restoreOnExit, int priority, float returnBlend) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }
    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked) return false;

    PushUndoSnapshot();
    selected.cameraZoneEnabled = enabled;
    selected.cameraZonePresetName = presetName;
    selected.cameraZoneRestoreOnExit = restoreOnExit;
    selected.cameraZonePriority = priority;
    selected.cameraZoneReturnBlend = std::max(returnBlend, 0.0f);
    if (enabled) {
        selected.colliderEnabled = true;
        selected.collider.isTrigger = true;
    }
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedHealthEnabled(bool enabled) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }

    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked || selected.healthEnabled == enabled) {
        return false;
    }

    PushUndoSnapshot();
    selected.healthEnabled = enabled;
    if (enabled) {
        selected.health.maxHp = std::max(selected.health.maxHp, 1.0f);
        if (selected.health.hp <= 0.0f) {
            selected.health.hp = selected.health.maxHp;
        }
        selected.health.hp = std::clamp(selected.health.hp, 0.0f, selected.health.maxHp);
        selected.health.alive = selected.health.hp > 0.0f;
        selected.health.justDied = false;
    }
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedHealth(const engine::Health& health) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }

    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked) {
        return false;
    }

    engine::Health edited = health;
    edited.maxHp = std::max(edited.maxHp, 1.0f);
    edited.hp = std::clamp(edited.hp, 0.0f, edited.maxHp);
    edited.alive = edited.alive && edited.hp > 0.0f;
    edited.justDied = false;

    PushUndoSnapshot();
    selected.healthEnabled = true;
    selected.health = edited;
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedScript(const std::string& className, const std::string& path, bool enabled) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }

    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked) {
        return false;
    }

    if (selected.scriptClassName == className
        && selected.scriptPath == path
        && selected.scriptEnabled == enabled) {
        return false;
    }

    PushUndoSnapshot();
    selected.scriptClassName = className;
    selected.scriptPath = path;
    selected.scriptEnabled = enabled && !className.empty();
    if (className.empty()) {
        selected.scriptFields.clear();
    }
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedAudioSource(bool enabled, const std::string& path,
                                         float volume, float pitch, bool spatial,
                                         bool loop, bool autoplay, float minDistance,
                                         float maxDistance, float rolloff,
                                         engine::AudioBus bus, float dopplerFactor,
                                         float coneInnerAngle, float coneOuterAngle,
                                         float coneOuterGain, float occlusion, int priority) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) return false;
    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked) return false;
    const float safeVolume = std::clamp(volume, 0.0f, 4.0f);
    const float safePitch = std::clamp(pitch, 0.01f, 4.0f);
    const float safeMin = std::max(minDistance, 0.01f);
    const float safeMax = std::max(maxDistance, safeMin);
    const float safeRolloff = std::max(rolloff, 0.0f);
    const float safeDoppler = std::max(dopplerFactor, 0.0f);
    const float safeConeInner = std::clamp(coneInnerAngle, 0.0f, 360.0f);
    const float safeConeOuter = std::clamp(coneOuterAngle, safeConeInner, 360.0f);
    const float safeConeGain = std::clamp(coneOuterGain, 0.0f, 1.0f);
    const float safeOcclusion = std::clamp(occlusion, 0.0f, 1.0f);
    const int safePriority = std::clamp(priority, 0, 100);
    if (selected.audioSourceEnabled == enabled && selected.audioAssetPath == path
        && selected.audioVolume == safeVolume && selected.audioPitch == safePitch
        && selected.audioSpatial == spatial && selected.audioLoop == loop
        && selected.audioAutoplay == autoplay && selected.audioMinDistance == safeMin
        && selected.audioMaxDistance == safeMax && selected.audioRolloff == safeRolloff
        && selected.audioBus == bus && selected.audioDopplerFactor == safeDoppler
        && selected.audioConeInnerAngle == safeConeInner
        && selected.audioConeOuterAngle == safeConeOuter
        && selected.audioConeOuterGain == safeConeGain
        && selected.audioOcclusion == safeOcclusion
        && selected.audioPriority == safePriority) return false;
    PushUndoSnapshot();
    selected.audioSourceEnabled = enabled;
    selected.audioAssetPath = path;
    selected.audioVolume = safeVolume;
    selected.audioPitch = safePitch;
    selected.audioSpatial = spatial;
    selected.audioLoop = loop;
    selected.audioAutoplay = autoplay;
    selected.audioMinDistance = safeMin;
    selected.audioMaxDistance = safeMax;
    selected.audioRolloff = safeRolloff;
    selected.audioBus = bus;
    selected.audioDopplerFactor = safeDoppler;
    selected.audioConeInnerAngle = safeConeInner;
    selected.audioConeOuterAngle = safeConeOuter;
    selected.audioConeOuterGain = safeConeGain;
    selected.audioOcclusion = safeOcclusion;
    selected.audioPriority = safePriority;
    m_dirty = true;
    return true;
}

void EditorScene::AddParticleSystem(const engine::Mesh& placeholderMesh,
                                    const Transform& transform,
                                    const std::string& assetPath,
                                    const engine::ParticleSystemComponent& settings) {
    PushUndoSnapshot();
    CreateObject("ParticleSystem_" + std::to_string(m_nextCubeNumber++), Primitive::Cube,
                 placeholderMesh, transform, glm::vec3(0.35f, 0.55f, 1.0f));
    m_selectedIndex = static_cast<int>(m_objects.size()) - 1;
    m_objects.back().visible = false;
    m_particleEditOpen = true;
    SetSelectedParticleSystem(true, settings);
    m_particleEditOpen = false;
    m_objects.back().particleAssetPath = assetPath;
    m_objects.back().particleAssetOverride = false;
    m_dirty = true;
}

bool EditorScene::SetSelectedParticleSystem(bool enabled,
                                             const engine::ParticleSystemComponent& settings) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) return false;
    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked) return false;
    if (!m_particleEditOpen) PushUndoSnapshot();
    selected.particleSystemEnabled = enabled;
    selected.particleConfig = settings.config;
    selected.particleConfig.rate = std::max(selected.particleConfig.rate, 0.0f);
    selected.particleConfig.maxParticles = std::max(selected.particleConfig.maxParticles, 1);
    selected.particleConfig.shapeRadius = std::max(selected.particleConfig.shapeRadius, 0.0f);
    if (selected.particleConfig.speedMin > selected.particleConfig.speedMax)
        std::swap(selected.particleConfig.speedMin, selected.particleConfig.speedMax);
    selected.particleConfig.lifeMin = std::max(selected.particleConfig.lifeMin, 0.001f);
    selected.particleConfig.lifeMax = std::max(selected.particleConfig.lifeMax,
                                                selected.particleConfig.lifeMin);
    selected.particleConfig.drag = std::max(selected.particleConfig.drag, 0.0f);
    selected.particleConfig.startSize = std::max(selected.particleConfig.startSize, 0.0f);
    selected.particleConfig.endSize = std::max(selected.particleConfig.endSize, 0.0f);
    if (selected.particleConfig.rotationMinDeg > selected.particleConfig.rotationMaxDeg)
        std::swap(selected.particleConfig.rotationMinDeg, selected.particleConfig.rotationMaxDeg);
    if (selected.particleConfig.angularVelocityMinDeg > selected.particleConfig.angularVelocityMaxDeg)
        std::swap(selected.particleConfig.angularVelocityMinDeg,
                  selected.particleConfig.angularVelocityMaxDeg);
    for (float& key : selected.particleConfig.sizeCurve) key = std::clamp(key, 0.0f, 1.0f);
    for (float& key : selected.particleConfig.colorCurve) key = std::clamp(key, 0.0f, 1.0f);
    selected.particleConfig.textureColumns = std::max(selected.particleConfig.textureColumns, 1);
    selected.particleConfig.textureRows = std::max(selected.particleConfig.textureRows, 1);
    selected.particleConfig.textureFps = std::max(selected.particleConfig.textureFps, 0.0f);
    selected.particleConfig.boundsRadius = std::max(selected.particleConfig.boundsRadius, 0.01f);
    selected.particleConfig.collisionRadius = std::max(selected.particleConfig.collisionRadius, 0.0f);
    selected.particleConfig.collisionBounce = std::max(selected.particleConfig.collisionBounce, 0.0f);
    selected.particleConfig.collisionFriction = std::clamp(selected.particleConfig.collisionFriction, 0.0f, 1.0f);
    selected.particleConfig.collisionLifetimeLoss = std::clamp(
        selected.particleConfig.collisionLifetimeLoss, 0.0f, 1.0f);
    selected.particleConfig.trailSegments = std::clamp(selected.particleConfig.trailSegments, 2, 16);
    selected.particleConfig.trailLength = std::max(selected.particleConfig.trailLength, 0.001f);
    selected.particleConfig.trailWidth = std::max(selected.particleConfig.trailWidth, 0.0f);
    selected.particleConfig.trailOpacity = std::clamp(selected.particleConfig.trailOpacity, 0.0f, 1.0f);
    selected.particleConfig.meshScale = std::max(selected.particleConfig.meshScale, 0.001f);
    selected.particleAutoplay = settings.autoplay;
    selected.particleLoop = settings.loop;
    selected.particlePrewarm = settings.prewarm;
    selected.particleDuration = std::max(settings.duration, 0.0f);
    selected.particleStartDelay = std::max(settings.startDelay, 0.0f);
    selected.particleSimulationSpeed = std::max(settings.simulationSpeed, 0.0f);
    selected.particleLocalSpace = settings.localSpace;
    selected.particleBurstCount = std::max(settings.burstCount, 0);
    selected.particleBurstInterval = std::max(settings.burstInterval, 0.0f);
    if (!selected.particleAssetPath.empty()) selected.particleAssetOverride = true;
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedParticleAsset(const std::string& path,
                                            const engine::ParticleSystemComponent& settings,
                                            bool instanceOverride) {
    if (!SetSelectedParticleSystem(true, settings)) return false;
    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    selected.particleAssetPath = path;
    selected.particleAssetOverride = instanceOverride;
    return true;
}

int EditorScene::RefreshParticleAssetInstances(const std::string& path,
                                                const engine::ParticleSystemComponent& settings) {
    if (path.empty()) return 0;
    bool hasTarget = false;
    for (const Object& object : m_objects)
        if (object.particleAssetPath == path && !object.particleAssetOverride) { hasTarget = true; break; }
    if (!hasTarget) return 0;
    PushUndoSnapshot();
    int refreshed = 0;
    const int oldSelection = m_selectedIndex;
    const bool oldParticleEdit = m_particleEditOpen;
    for (std::size_t i = 0; i < m_objects.size(); ++i) {
        Object& object = m_objects[i];
        if (object.particleAssetPath != path || object.particleAssetOverride) continue;
        m_selectedIndex = static_cast<int>(i);
        m_particleEditOpen = true;
        SetSelectedParticleSystem(true, settings);
        object.particleAssetOverride = false;
        ++refreshed;
    }
    m_particleEditOpen = oldParticleEdit;
    m_selectedIndex = oldSelection;
    return refreshed;
}

bool EditorScene::SetSelectedParticleEffectLayers(
    const std::vector<engine::ParticleEffectLayer>& layers) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) return false;
    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked) return false;
    PushUndoSnapshot();
    selected.particleEffectLayers = layers;
    if (selected.particleEffectLayers.size() > 64) selected.particleEffectLayers.resize(64);
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedScriptEnabled(bool enabled) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }

    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked || selected.scriptEnabled == enabled) {
        return false;
    }

    PushUndoSnapshot();
    selected.scriptEnabled = enabled && !selected.scriptClassName.empty();
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedNavAgent(bool enabled, float speed, float maxForce,
                                      float reachRadius, float repathInterval,
                                      const std::string& targetName, float visionRange,
                                      float visionHalfAngle) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }
    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked) {
        return false;
    }
    PushUndoSnapshot();
    selected.navAgentEnabled = enabled;
    selected.navAgentSpeed = std::max(speed, 0.0f);
    selected.navAgentMaxForce = std::max(maxForce, 0.0f);
    selected.navAgentReachRadius = std::max(reachRadius, 0.05f);
    selected.navAgentRepathInterval = std::max(repathInterval, 0.05f);
    selected.navAgentTargetName = targetName;
    selected.navAgentVisionRange = std::max(visionRange, 0.0f);
    selected.navAgentVisionHalfAngle = std::clamp(visionHalfAngle, 1.0f, 180.0f);
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedNavAgentBrain(const std::string& brainAsset) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }
    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked) {
        return false;
    }
    PushUndoSnapshot();
    selected.navAgentBrainAsset = brainAsset;
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedNavAgentTeam(int team, bool autoTarget) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }
    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked) {
        return false;
    }
    PushUndoSnapshot();
    selected.navAgentTeam = team;
    selected.navAgentAutoTarget = autoTarget;
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedTerrain(bool enabled, int res, float size, float maxHeight,
                                     int seed, int octaves, float frequency) {
    TerrainTrace("SetSelectedTerrain: begin enabled=" + std::to_string(enabled ? 1 : 0) +
                 " selIndex=" + std::to_string(m_selectedIndex) +
                 " objs=" + std::to_string(m_objects.size()));
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }
    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked) {
        return false;
    }
    PushUndoSnapshot();
    TerrainTrace("SetSelectedTerrain: snapshot pushed entity=" + std::to_string(static_cast<unsigned>(selected.entity)));
    selected.isTerrain = enabled;
    selected.terrainRes = std::clamp(res, 8, 512);
    selected.terrainSize = std::max(size, 1.0f);
    selected.terrainMaxHeight = std::max(maxHeight, 0.0f);
    selected.terrainSeed = seed;
    selected.terrainOctaves = std::clamp(octaves, 1, 10);
    selected.terrainFrequency = std::max(frequency, 0.1f);
    selected.terrainHeights.clear();   // params changed -> regenerate from noise (discard sculpt)
    selected.terrainPaint.clear();

    // The terrain mesh bakes `size` into its own vertices (local span [0, size]), and the
    // height query / sculpt brush map world->terrain-local coordinates assuming that same
    // unscaled span. If the object keeps the source plane's non-unit scale (e.g. 3,1,3) the
    // rendered terrain ends up 3x oversized while the brush still expects [0, size], so sculpt
    // clicks land outside the heightmap and the selection outline no longer covers the mesh.
    // Normalising the transform scale to 1 keeps render, outline, height query and sculpt in
    // the same coordinate space.
    if (enabled) {
        engine::ecs::Transform* t = m_registry.TryGet<engine::ecs::Transform>(selected.entity);
        TerrainTrace(std::string("SetSelectedTerrain: transform ") + (t ? "found, normalising scale" : "MISSING"));
        if (t) {
            t->scale = glm::vec3(1.0f);
        }
    }
    m_dirty = true;
    TerrainTrace("SetSelectedTerrain: end ok");
    return true;
}

bool EditorScene::SetSelectedWater(float size, int resolution, float level,
                                   const glm::vec3& shallow, const glm::vec3& deep,
                                   const glm::vec3& reflection, float transparency,
                                   float fresnel, float specular, float shininess) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }
    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked) {
        return false;
    }
    PushUndoSnapshot();
    selected.isWater = true;
    selected.waterSize = std::max(size, 1.0f);
    selected.waterResolution = std::clamp(resolution, 8, 512);
    selected.waterLevel = level;
    selected.waterShallow = shallow;
    selected.waterDeep = deep;
    selected.waterReflection = reflection;
    selected.waterTransparency = std::clamp(transparency, 0.0f, 1.0f);
    selected.waterFresnel = std::max(fresnel, 0.1f);
    selected.waterSpecular = std::max(specular, 0.0f);
    selected.waterShininess = std::max(shininess, 1.0f);

    // Keep the object's plane (the opaque "bed") matched to the water patch: scale it
    // to the water size and sit it at the surface level, so the animated surface has a
    // bed exactly its own footprint.
    if (engine::ecs::Transform* t = m_registry.TryGet<engine::ecs::Transform>(selected.entity)) {
        t->scale = glm::vec3(selected.waterSize, 1.0f, selected.waterSize);
        t->position.y = selected.waterLevel;
    }
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedTerrainPaint(std::vector<unsigned char> paint) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }
    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked) {
        return false;
    }
    selected.terrainPaint = std::move(paint);
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedTerrainHeights(std::vector<float> heights) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }
    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked) {
        return false;
    }
    selected.terrainHeights = std::move(heights);   // per-stroke; no undo snapshot (avoids spam)
    m_dirty = true;
    return true;
}

bool EditorScene::AddSelectedPatrolPoint(const glm::vec3& point) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }
    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked) {
        return false;
    }
    PushUndoSnapshot();
    selected.patrolPoints.push_back(point);
    m_dirty = true;
    return true;
}

bool EditorScene::ClearSelectedPatrolPoints() {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }
    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked || selected.patrolPoints.empty()) {
        return false;
    }
    PushUndoSnapshot();
    selected.patrolPoints.clear();
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedScriptFields(const std::vector<ScriptField>& fields) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }

    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked) {
        return false;
    }

    PushUndoSnapshot();
    selected.scriptFields = fields;
    m_dirty = true;
    return true;
}

bool EditorScene::AddSelectedScriptField() {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }

    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked) {
        return false;
    }

    PushUndoSnapshot();
    ScriptField field;
    field.name = "speed";
    field.type = ScriptField::Type::Float;
    field.value = "1.0";
    selected.scriptFields.push_back(field);
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedScriptField(std::size_t index, const ScriptField& field) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }

    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked || index >= selected.scriptFields.size()) {
        return false;
    }

    const ScriptField& current = selected.scriptFields[index];
    if (current.name == field.name && current.type == field.type && current.value == field.value) {
        return false;
    }

    PushUndoSnapshot();
    selected.scriptFields[index] = field;
    m_dirty = true;
    return true;
}

bool EditorScene::RemoveSelectedScriptField(std::size_t index) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }

    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked || index >= selected.scriptFields.size()) {
        return false;
    }

    PushUndoSnapshot();
    selected.scriptFields.erase(selected.scriptFields.begin() + static_cast<std::ptrdiff_t>(index));
    m_dirty = true;
    return true;
}

bool EditorScene::AddPhysicsJoint(const PhysicsJoint &joint)
{
    if (joint.objectA.empty() || (!joint.worldAnchor && joint.objectB.empty())) {
        return false;
    }

    PushUndoSnapshot();
    m_joints.push_back(joint);
    m_dirty = true;
    return true;
}

bool EditorScene::SetPhysicsJoint(std::size_t index, const PhysicsJoint &joint)
{
    if (index >= m_joints.size()) {
        return false;
    }
    if (joint.objectA.empty() || (!joint.worldAnchor && joint.objectB.empty())) {
        return false;
    }

    PushUndoSnapshot();
    m_joints[index] = joint;
    m_dirty = true;
    return true;
}

bool EditorScene::RemovePhysicsJoint(std::size_t index)
{
    if (index >= m_joints.size()) {
        return false;
    }

    PushUndoSnapshot();
    m_joints.erase(m_joints.begin() + static_cast<std::ptrdiff_t>(index));
    m_dirty = true;
    return true;
}

const EditorScene::CameraPreset* EditorScene::PrimaryCameraPreset() const
{
    const auto it = std::find_if(m_cameraPresets.begin(), m_cameraPresets.end(),
        [](const CameraPreset& camera) { return camera.primary; });
    return it == m_cameraPresets.end() ? nullptr : &*it;
}

std::size_t EditorScene::AddCameraPreset(const CameraPreset& source)
{
    PushUndoSnapshot();
    CameraPreset preset = source;
    preset.name = preset.name.empty()
        ? "Camera_" + std::to_string(m_cameraPresets.size() + 1)
        : preset.name;
    preset.fov = std::clamp(preset.fov, 10.0f, 120.0f);
    preset.nearPlane = std::max(preset.nearPlane, 0.001f);
    preset.farPlane = std::max(preset.farPlane, preset.nearPlane + 0.01f);
    preset.blendDuration = std::max(preset.blendDuration, 0.0f);
    preset.blendEasing = std::clamp(preset.blendEasing, 0, 3);
    if (preset.primary) {
        for (CameraPreset& camera : m_cameraPresets) camera.primary = false;
    }
    m_cameraPresets.push_back(std::move(preset));
    m_dirty = true;
    return m_cameraPresets.size() - 1;
}

bool EditorScene::SetCameraPreset(std::size_t index, const CameraPreset& source)
{
    if (index >= m_cameraPresets.size()) return false;
    PushUndoSnapshot();
    const std::string previousName = m_cameraPresets[index].name;
    CameraPreset preset = source;
    preset.name = preset.name.empty() ? "Camera" : preset.name;
    preset.fov = std::clamp(preset.fov, 10.0f, 120.0f);
    preset.nearPlane = std::max(preset.nearPlane, 0.001f);
    preset.farPlane = std::max(preset.farPlane, preset.nearPlane + 0.01f);
    preset.blendDuration = std::max(preset.blendDuration, 0.0f);
    preset.blendEasing = std::clamp(preset.blendEasing, 0, 3);
    if (preset.primary) {
        for (std::size_t i = 0; i < m_cameraPresets.size(); ++i) {
            if (i != index) m_cameraPresets[i].primary = false;
        }
    }
    if (preset.name != previousName) {
        for (CameraSequence& sequence : m_cameraSequences) {
            for (CameraSequenceShot& shot : sequence.shots) {
                if (shot.cameraName == previousName) shot.cameraName = preset.name;
            }
        }
        for (Object& object : m_objects) {
            if (object.cameraZonePresetName == previousName) {
                object.cameraZonePresetName = preset.name;
            }
        }
    }
    m_cameraPresets[index] = std::move(preset);
    m_dirty = true;
    return true;
}

bool EditorScene::RemoveCameraPreset(std::size_t index)
{
    if (index >= m_cameraPresets.size()) return false;
    PushUndoSnapshot();
    m_cameraPresets.erase(m_cameraPresets.begin() + static_cast<std::ptrdiff_t>(index));
    m_dirty = true;
    return true;
}

std::size_t EditorScene::DuplicateCameraPreset(std::size_t index)
{
    if (index >= m_cameraPresets.size()) return static_cast<std::size_t>(-1);
    PushUndoSnapshot();
    CameraPreset copy = m_cameraPresets[index];
    copy.name += " Copy";
    copy.primary = false;
    copy.useInPlay = false;
    m_cameraPresets.push_back(std::move(copy));
    m_dirty = true;
    return m_cameraPresets.size() - 1;
}

bool EditorScene::SetPrimaryCameraPreset(std::size_t index)
{
    if (index >= m_cameraPresets.size()) return false;
    PushUndoSnapshot();
    for (std::size_t i = 0; i < m_cameraPresets.size(); ++i) {
        m_cameraPresets[i].primary = i == index;
    }
    m_dirty = true;
    return true;
}

std::size_t EditorScene::AddCameraSequence(const CameraSequence& source)
{
    PushUndoSnapshot();
    CameraSequence sequence = source;
    sequence.name = sequence.name.empty()
        ? "Sequence_" + std::to_string(m_cameraSequences.size() + 1)
        : sequence.name;
    for (CameraSequenceShot& shot : sequence.shots) {
        shot.travelDuration = std::max(shot.travelDuration, 0.0f);
        shot.holdDuration = std::max(shot.holdDuration, 0.0f);
        shot.easing = std::clamp(shot.easing, 0, 3);
        shot.pathMode = std::clamp(shot.pathMode, 0, 1);
    }
    for (CinematicCue& cue : sequence.cues) {
        cue.time = std::max(cue.time, 0.0f);
        cue.volume = std::max(cue.volume, 0.0f);
    }
    m_cameraSequences.push_back(std::move(sequence));
    m_dirty = true;
    return m_cameraSequences.size() - 1;
}

bool EditorScene::SetCameraSequence(std::size_t index, const CameraSequence& source)
{
    if (index >= m_cameraSequences.size()) return false;
    PushUndoSnapshot();
    const std::string previousName = m_cameraSequences[index].name;
    CameraSequence sequence = source;
    sequence.name = sequence.name.empty() ? "Camera Sequence" : sequence.name;
    for (CameraSequenceShot& shot : sequence.shots) {
        shot.travelDuration = std::max(shot.travelDuration, 0.0f);
        shot.holdDuration = std::max(shot.holdDuration, 0.0f);
        shot.easing = std::clamp(shot.easing, 0, 3);
        shot.pathMode = std::clamp(shot.pathMode, 0, 1);
    }
    for (CinematicCue& cue : sequence.cues) {
        cue.time = std::max(cue.time, 0.0f);
        cue.volume = std::max(cue.volume, 0.0f);
    }
    if (sequence.name != previousName) {
        for (Object& object : m_objects) {
            if (object.triggerCameraSequenceName == previousName) {
                object.triggerCameraSequenceName = sequence.name;
            }
        }
    }
    m_cameraSequences[index] = std::move(sequence);
    m_dirty = true;
    return true;
}

bool EditorScene::RemoveCameraSequence(std::size_t index)
{
    if (index >= m_cameraSequences.size()) return false;
    PushUndoSnapshot();
    m_cameraSequences.erase(
        m_cameraSequences.begin() + static_cast<std::ptrdiff_t>(index));
    m_dirty = true;
    return true;
}

bool EditorScene::ToggleSelectVisible()
{
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }

    PushUndoSnapshot();
    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    selected.visible = !selected.visible;
    m_dirty = true;
    return true;
}

bool EditorScene::ToggleSelectedLocked()
{
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }

    PushUndoSnapshot();
    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    selected.locked = !selected.locked;
    m_dirty = true;
    return true;
}

bool EditorScene::DuplicateSelected(const engine::Mesh & cube, const engine::Mesh & plane, const engine::Mesh& sphere, const engine::Mesh& capsule, const engine::Mesh& cylinder, const engine::Mesh& cone, const engine::Mesh& pyramid, const engine::Mesh& torus, const engine::Mesh& staircase)
{
    const Object* selected = SelectedObject();
    if (!selected) {
        return false;
    }

    const Object selectedCopy = *selected;
    const Transform* transform = m_registry.TryGet<Transform>(selected->entity);
    const MeshRenderer* renderer = m_registry.TryGet<MeshRenderer>(selected->entity);
    if (!transform || !renderer || selectedCopy.locked) {
        return false;
    }

    Transform duplicateTransform = *transform;
    const glm::vec3 duplicateColor = renderer->color;

    PushUndoSnapshot();

    duplicateTransform.position += glm::vec3(0.8f, 0.0f, 0.8f);

    const engine::Mesh& mesh = MeshFor(selectedCopy.primitive, cube, plane, sphere, capsule, cylinder, cone, pyramid, torus, staircase);
    CreateObject(selectedCopy.name + "_Copy", selectedCopy.primitive, mesh, duplicateTransform, duplicateColor);
    m_objects.back().modelAssetPath = selectedCopy.modelAssetPath;
    m_objects.back().materialAssetPath = selectedCopy.materialAssetPath;
    m_objects.back().materialParameterOverrides =
        selectedCopy.materialParameterOverrides;
    m_objects.back().skeletalModel = selectedCopy.skeletalModel;
    m_objects.back().animationClipIndex = selectedCopy.animationClipIndex;
    m_objects.back().animationClipName = selectedCopy.animationClipName;
    m_objects.back().animationAutoplay = selectedCopy.animationAutoplay;
    m_objects.back().animationLoop = selectedCopy.animationLoop;
    m_objects.back().animationSpeed = selectedCopy.animationSpeed;
    m_objects.back().animationLocomotionEnabled = selectedCopy.animationLocomotionEnabled;
    m_objects.back().animationIdleClipIndex = selectedCopy.animationIdleClipIndex;
    m_objects.back().animationWalkClipIndex = selectedCopy.animationWalkClipIndex;
    m_objects.back().animationRunClipIndex = selectedCopy.animationRunClipIndex;
    m_objects.back().animationIdleClipName = selectedCopy.animationIdleClipName;
    m_objects.back().animationWalkClipName = selectedCopy.animationWalkClipName;
    m_objects.back().animationRunClipName = selectedCopy.animationRunClipName;
    m_objects.back().animationWalkAt = selectedCopy.animationWalkAt;
    m_objects.back().animationRunAt = selectedCopy.animationRunAt;
    m_objects.back().animationEvents = selectedCopy.animationEvents;
    m_objects.back().animationActionProfiles = selectedCopy.animationActionProfiles;
    m_objects.back().animationStates = selectedCopy.animationStates;
    m_objects.back().animationParameters = selectedCopy.animationParameters;
    m_objects.back().animationTransitions = selectedCopy.animationTransitions;
    m_objects.back().light = selectedCopy.light;
    m_objects.back().navMeshBoundsVolume = selectedCopy.navMeshBoundsVolume;
    m_objects.back().lightData = selectedCopy.lightData;
    if (selectedCopy.light) {
        m_registry.Add<Light>(m_objects.back().entity, selectedCopy.lightData);
    }
    m_objects.back().linearVelocityEnabled = selectedCopy.linearVelocityEnabled;
    m_objects.back().angularVelocityEnabled = selectedCopy.angularVelocityEnabled;
    m_objects.back().linearVelocity = selectedCopy.linearVelocity;
    m_objects.back().angularVelocityAxis = selectedCopy.angularVelocityAxis;
    m_objects.back().angularVelocityRadians = selectedCopy.angularVelocityRadians;
    m_objects.back().rigidBodyEnabled = selectedCopy.rigidBodyEnabled;
    m_objects.back().rigidBody = selectedCopy.rigidBody;
    m_objects.back().colliderEnabled = selectedCopy.colliderEnabled;
    m_objects.back().collider = selectedCopy.collider;
    m_objects.back().rotatorEnabled = selectedCopy.rotatorEnabled;
    m_objects.back().rotator = selectedCopy.rotator;
    m_objects.back().moverEnabled = selectedCopy.moverEnabled;
    m_objects.back().mover = selectedCopy.mover;
    m_objects.back().triggerTargetName = selectedCopy.triggerTargetName;
    m_objects.back().triggerEnterMoverAction = selectedCopy.triggerEnterMoverAction;
    m_objects.back().triggerEnterRotatorAction = selectedCopy.triggerEnterRotatorAction;
    m_objects.back().triggerExitMoverAction = selectedCopy.triggerExitMoverAction;
    m_objects.back().triggerExitRotatorAction = selectedCopy.triggerExitRotatorAction;
    m_objects.back().triggerEnterAudioAction = selectedCopy.triggerEnterAudioAction;
    m_objects.back().triggerExitAudioAction = selectedCopy.triggerExitAudioAction;
    m_objects.back().triggerEnterParticleAction = selectedCopy.triggerEnterParticleAction;
    m_objects.back().triggerExitParticleAction = selectedCopy.triggerExitParticleAction;
    m_objects.back().triggerCameraSequenceName = selectedCopy.triggerCameraSequenceName;
    m_objects.back().triggerEnterCameraAction = selectedCopy.triggerEnterCameraAction;
    m_objects.back().triggerExitCameraAction = selectedCopy.triggerExitCameraAction;
    m_objects.back().triggerCameraLockInput = selectedCopy.triggerCameraLockInput;
    m_objects.back().triggerCameraSkippable = selectedCopy.triggerCameraSkippable;
    m_objects.back().playerControllerEnabled = selectedCopy.playerControllerEnabled;
    m_objects.back().playerController = selectedCopy.playerController;
    m_objects.back().cameraZoneEnabled = selectedCopy.cameraZoneEnabled;
    m_objects.back().cameraZonePresetName = selectedCopy.cameraZonePresetName;
    m_objects.back().cameraZoneRestoreOnExit = selectedCopy.cameraZoneRestoreOnExit;
    m_objects.back().cameraZonePriority = selectedCopy.cameraZonePriority;
    m_objects.back().cameraZoneReturnBlend = selectedCopy.cameraZoneReturnBlend;
    m_objects.back().healthEnabled = selectedCopy.healthEnabled;
    m_objects.back().health = selectedCopy.health;
    m_objects.back().scriptEnabled = selectedCopy.scriptEnabled;
    m_objects.back().scriptClassName = selectedCopy.scriptClassName;
    m_objects.back().scriptPath = selectedCopy.scriptPath;
    m_objects.back().scriptFields = selectedCopy.scriptFields;
    m_objects.back().audioSourceEnabled = selectedCopy.audioSourceEnabled;
    m_objects.back().audioAssetPath = selectedCopy.audioAssetPath;
    m_objects.back().audioBus = selectedCopy.audioBus;
    m_objects.back().audioVolume = selectedCopy.audioVolume;
    m_objects.back().audioPitch = selectedCopy.audioPitch;
    m_objects.back().audioSpatial = selectedCopy.audioSpatial;
    m_objects.back().audioLoop = selectedCopy.audioLoop;
    m_objects.back().audioAutoplay = selectedCopy.audioAutoplay;
    m_objects.back().audioMinDistance = selectedCopy.audioMinDistance;
    m_objects.back().audioMaxDistance = selectedCopy.audioMaxDistance;
    m_objects.back().audioRolloff = selectedCopy.audioRolloff;
    m_objects.back().audioDopplerFactor = selectedCopy.audioDopplerFactor;
    m_objects.back().audioConeInnerAngle = selectedCopy.audioConeInnerAngle;
    m_objects.back().audioConeOuterAngle = selectedCopy.audioConeOuterAngle;
    m_objects.back().audioConeOuterGain = selectedCopy.audioConeOuterGain;
    m_objects.back().audioOcclusion = selectedCopy.audioOcclusion;
    m_objects.back().audioPriority = selectedCopy.audioPriority;
    m_objects.back().particleSystemEnabled = selectedCopy.particleSystemEnabled;
    m_objects.back().particleConfig = selectedCopy.particleConfig;
    m_objects.back().particleAutoplay = selectedCopy.particleAutoplay;
    m_objects.back().particleLoop = selectedCopy.particleLoop;
    m_objects.back().particlePrewarm = selectedCopy.particlePrewarm;
    m_objects.back().particleDuration = selectedCopy.particleDuration;
    m_objects.back().particleStartDelay = selectedCopy.particleStartDelay;
    m_objects.back().particleSimulationSpeed = selectedCopy.particleSimulationSpeed;
    m_objects.back().particleLocalSpace = selectedCopy.particleLocalSpace;
    m_objects.back().particleBurstCount = selectedCopy.particleBurstCount;
    m_objects.back().particleBurstInterval = selectedCopy.particleBurstInterval;
    m_objects.back().particleAssetPath = selectedCopy.particleAssetPath;
    m_objects.back().particleAssetOverride = selectedCopy.particleAssetOverride;
    m_objects.back().particleEffectLayers = selectedCopy.particleEffectLayers;
    m_objects.back().navAgentEnabled = selectedCopy.navAgentEnabled;
    m_objects.back().patrolPoints = selectedCopy.patrolPoints;
    m_objects.back().navAgentSpeed = selectedCopy.navAgentSpeed;
    m_objects.back().navAgentMaxForce = selectedCopy.navAgentMaxForce;
    m_objects.back().navAgentReachRadius = selectedCopy.navAgentReachRadius;
    m_objects.back().navAgentRepathInterval = selectedCopy.navAgentRepathInterval;
    m_objects.back().navAgentTargetName = selectedCopy.navAgentTargetName;
    m_objects.back().navAgentVisionRange = selectedCopy.navAgentVisionRange;
    m_objects.back().navAgentVisionHalfAngle = selectedCopy.navAgentVisionHalfAngle;
    m_objects.back().navAgentBrainAsset = selectedCopy.navAgentBrainAsset;
    m_objects.back().navAgentTeam = selectedCopy.navAgentTeam;
    m_objects.back().navAgentAutoTarget = selectedCopy.navAgentAutoTarget;
    m_objects.back().isTerrain = selectedCopy.isTerrain;
    m_objects.back().terrainRes = selectedCopy.terrainRes;
    m_objects.back().terrainSize = selectedCopy.terrainSize;
    m_objects.back().terrainMaxHeight = selectedCopy.terrainMaxHeight;
    m_objects.back().terrainSeed = selectedCopy.terrainSeed;
    m_objects.back().terrainOctaves = selectedCopy.terrainOctaves;
    m_objects.back().terrainFrequency = selectedCopy.terrainFrequency;
    m_objects.back().terrainHeights = selectedCopy.terrainHeights;
    m_objects.back().terrainPaint = selectedCopy.terrainPaint;
    m_selectedIndex = static_cast<int>(m_objects.size()) - 1;
    m_dirty = true;
    return true;
}

bool EditorScene::DeleteSelected()
{
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }
    if (m_objects[static_cast<std::size_t>(m_selectedIndex)].locked) {
        return false;
    }

    PushUndoSnapshot();

    const std::size_t index = static_cast<std::size_t>(m_selectedIndex);
    m_registry.Destroy(m_objects[index].entity);
    m_objects.erase(m_objects.begin() + static_cast<std::ptrdiff_t>(index));

    if (m_objects.empty()) {
        m_selectedIndex = -1;
    } else if (m_selectedIndex >= static_cast<int>(m_objects.size())) {
        m_selectedIndex = static_cast<int>(m_objects.size()) - 1;
    }

    m_dirty = true;
    return true;
}

engine::ecs::Entity EditorScene::CreateObject(const std::string & name, Primitive primitive, const engine::Mesh & mesh, const engine::ecs::Transform & transform, const glm::vec3 & color)
{
    Entity entity = m_registry.Create();
    m_registry.Add<Transform>(entity, transform);
    m_registry.Add<MeshRenderer>(entity, MeshRenderer{&mesh, color});
    m_objects.push_back({entity, name, primitive});
    return entity;
}

EditorScene::Snapshot EditorScene::CaptureSnapshot()
{
    Snapshot snapshot;
    snapshot.selectedIndex = m_selectedIndex;
    snapshot.nextCubeNumber = m_nextCubeNumber;
    snapshot.joints = m_joints;
    snapshot.cameraPresets = m_cameraPresets;
    snapshot.cameraSequences = m_cameraSequences;
    snapshot.environment = m_environment;

    for (const Object& object : m_objects) {
        const Transform* transform = m_registry.TryGet<Transform>(object.entity);
        const MeshRenderer* renderer = m_registry.TryGet<MeshRenderer>(object.entity);
        if (!transform || !renderer) {
            continue;
        }

        ObjectSnapshot objectSnapshot;
        objectSnapshot.object = object;
        objectSnapshot.transform = *transform;
        objectSnapshot.color = renderer->color;
        snapshot.objects.push_back(objectSnapshot);
    }

    return snapshot;
}

void EditorScene::RestoreSnapshot(const Snapshot & snapshot, const engine::Mesh & cube, const engine::Mesh & plane, const engine::Mesh& sphere, const engine::Mesh& capsule, const engine::Mesh& cylinder, const engine::Mesh& cone, const engine::Mesh& pyramid, const engine::Mesh& torus, const engine::Mesh& staircase)
{
    m_registry = engine::ecs::Registry{};
    m_objects.clear();

    for (const ObjectSnapshot& objectSnapshot : snapshot.objects) {
        Entity entity = m_registry.Create();
        const Object& snapObject = objectSnapshot.object;
        // Lights render with a gizmo mesh (sphere for directional/point/spot, cube for area),
        // not their stored primitive (always Cube). Match what AddXLight uses so restoring a
        // snapshot doesn't turn the light gizmo into a plain cube.
        const engine::Mesh& mesh = snapObject.light
            ? (snapObject.lightData.type == Light::Type::Area ? cube : sphere)
            : MeshFor(snapObject.primitive, cube, plane, sphere, capsule, cylinder, cone, pyramid, torus, staircase);
        m_registry.Add<Transform>(entity, objectSnapshot.transform);
        m_registry.Add<MeshRenderer>(entity, MeshRenderer{&mesh, objectSnapshot.color});

        Object object = snapObject;
        object.entity = entity;
        // Re-add the Light component so the light still casts in edit mode after
        // exiting Play (and after undo/redo). Without this only object.light/lightData
        // survive, but TryGetLight() returns null and edit-mode lighting goes dark.
        if (object.light) {
            m_registry.Add<Light>(entity, object.lightData);
        }
        m_objects.push_back(object);
    }

    m_selectedIndex = snapshot.selectedIndex;
    if (m_selectedIndex >= static_cast<int>(m_objects.size())) {
        m_selectedIndex = m_objects.empty() ? -1 : static_cast<int>(m_objects.size()) -1;
    }
    m_nextCubeNumber = snapshot.nextCubeNumber;
    m_joints = snapshot.joints;
    m_cameraPresets = snapshot.cameraPresets;
    m_cameraSequences = snapshot.cameraSequences;
    m_environment = snapshot.environment;
}

void EditorScene::PushUndoSnapshot()
{
    m_undoStack.push_back(CaptureSnapshot());
    m_redoStack.clear();
}

void EditorScene::ClearHistory()
{
    m_undoStack.clear();
    m_redoStack.clear();
    m_transformEditOpen = false;
    m_particleEditOpen = false;
}

void EditorScene::Clear()
{
    m_registry = engine::ecs::Registry{};
    m_objects.clear();
    m_joints.clear();
    m_cameraPresets.clear();
    m_cameraSequences.clear();
    m_undoStack.clear();
    m_redoStack.clear();
    m_selectedIndex = -1;
    m_nextCubeNumber = 1;
    m_dirty = false;
    m_transformEditOpen = false;
    m_particleEditOpen = false;
}
