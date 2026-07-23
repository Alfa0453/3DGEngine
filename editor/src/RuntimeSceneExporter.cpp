#include "RuntimeSceneExporter.h"

#include <engine/ecs/Components.h>
#include <engine/physics/PhysicsComponents.h>

#include <fstream>
#include <iomanip>
#include <sstream>

using engine::ecs::MeshRenderer;
using engine::ecs::Light;
using engine::ecs::Transform;

namespace {

const char* PrimitiveName(EditorScene::Primitive primitive) {
    switch (primitive) {
    case EditorScene::Primitive::Empty: return "Empty";
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

// Space-safe string field: quoted so values containing spaces (asset paths,
// animation clip names, etc.) round-trip through the whitespace-tokenised reader.
// Empty is stored as the quoted sentinel "-" (the loader clears it back to empty).
std::string StoredPath(const std::string& path) {
    std::ostringstream out;
    out << std::quoted(path.empty() ? std::string("-") : path);
    return out.str();
}

const char* LightTypeName(Light::Type type) {
    switch (type) {
    case Light::Type::Directional: return "Directional";
    case Light::Type::Point: return "Point";
    case Light::Type::Spot: return "Spot";
    case Light::Type::Area: return "Area";
    }
    return "Point";
}

} // namespace

bool RuntimeSceneExporter::Export(const EditorScene &scene, const std::string &path, std::string *error)
{
    std::ofstream out(path);
    if (!out) {
        if (error) *error = "Could not open runtime scene file for writing.";
        return false;
    }

    out << "3DGRuntimeScene 61\n";
    out << "# Runtime export from 3DGEditor. Editor-only flags are omitted.\n";
    const EditorScene::Environment& environment = scene.GetEnvironment();
    out << "environment "
        << environment.timeOfDay << ' '
        << environment.skyLightIntensity << ' '
        << (environment.driveSunLight ? 1 : 0) << ' '
        << environment.sunIntensity << ' '
        << (environment.fog ? 1 : 0) << ' '
        << environment.fogDensity << ' '
        << environment.fogHeight << ' '
        << environment.fogHeightFalloff << ' '
        << environment.physicsGravity.x << ' '
        << environment.physicsGravity.y << ' '
        << environment.physicsGravity.z << ' '
        << environment.physicsSolverIterations << ' '
        << (environment.physicsBroadPhase ? 1 : 0) << ' '
        << environment.physicsCellSize << ' '
        << environment.physicsRestitutionThreshold << ' '
        << (environment.physicsAllowSleeping ? 1 : 0) << ' '
        << environment.physicsSleepLinearVelocity << ' '
        << environment.physicsSleepAngularVelocity << ' '
        << environment.physicsTimeToSleep << ' '
        << std::quoted(environment.hudAsset) << ' '
        << environment.shadowDistance << '\n';
    out << "clouds "
        << (environment.clouds ? 1 : 0) << ' '
        << environment.cloudCoverage << ' '
        << environment.cloudDensity << ' '
        << environment.cloudScale << ' '
        << environment.cloudSoftness << ' '
        << environment.cloudWindSpeed << ' '
        << environment.cloudWindDirection << ' '
        << environment.cloudHorizonHeight << ' '
        << environment.cloudColor.r << ' '
        << environment.cloudColor.g << ' '
        << environment.cloudColor.b << ' '
        << (environment.cloudShadows ? 1 : 0) << ' '
        << environment.cloudShadowStrength << ' '
        << environment.cloudShadowScale << '\n';
    out << "skylight_occlusion "
        << (environment.skylightOcclusion ? 1 : 0) << ' '
        << environment.skylightOcclusionStrength << ' '
        << environment.minimumSkylight << '\n';
    for (const EditorScene::Environment::PostProcessEffect& effect :
         environment.postProcessEffects) {
        out << "post_effect "
            << std::quoted(effect.shaderPath) << ' '
            << (effect.enabled ? 1 : 0) << ' '
            << effect.parameters.size();
        for (const auto& parameter : effect.parameters) {
            out << ' ' << std::quoted(parameter.name)
                << ' ' << parameter.type
                << ' ' << std::quoted(parameter.value);
        }
        out << '\n';
    }

    for (const EditorScene::CameraPreset& camera : scene.CameraPresets()) {
        out << "camera "
            << std::quoted(camera.name) << ' '
            << camera.position.x << ' ' << camera.position.y << ' ' << camera.position.z << ' '
            << camera.target.x << ' ' << camera.target.y << ' ' << camera.target.z << ' '
            << camera.fov << ' ' << camera.nearPlane << ' ' << camera.farPlane << ' '
            << camera.blendDuration << ' ' << camera.blendEasing << ' '
            << (camera.primary ? 1 : 0) << ' ' << (camera.useInPlay ? 1 : 0) << '\n';
    }
    for (const EditorScene::CameraSequence& sequence : scene.CameraSequences()) {
        out << "camera_sequence "
            << std::quoted(sequence.name) << ' ' << (sequence.loop ? 1 : 0) << ' '
            << sequence.shots.size();
        for (const EditorScene::CameraSequenceShot& shot : sequence.shots) {
            out << ' ' << std::quoted(shot.cameraName)
                << ' ' << shot.travelDuration
                << ' ' << shot.holdDuration
                << ' ' << shot.easing
                << ' ' << shot.pathMode
                << ' ' << std::quoted(shot.eventName);
        }
        out << ' ' << sequence.cues.size();
        for (const EditorScene::CinematicCue& cue : sequence.cues) {
            out << ' ' << static_cast<int>(cue.type)
                << ' ' << cue.time
                << ' ' << std::quoted(cue.name)
                << ' ' << std::quoted(cue.assetPath)
                << ' ' << std::quoted(cue.targetObject)
                << ' ' << std::quoted(cue.animationClip)
                << ' ' << cue.volume;
        }
        out << '\n';
    }

    for (const EditorScene::Object& object : scene.Objects()) {
        if (!object.visible || !object.navMeshBoundsVolume) continue;
        const Transform* transform = scene.TryGetTransform(object.entity);
        if (!transform) continue;
        out << "nav_bounds "
            << transform->position.x << ' ' << transform->position.y << ' ' << transform->position.z << ' '
            << transform->scale.x << ' ' << transform->scale.y << ' ' << transform->scale.z << ' '
            << transform->rotation.w << ' ' << transform->rotation.x << ' '
            << transform->rotation.y << ' ' << transform->rotation.z << '\n';
    }

    for (const EditorScene::Object& object : scene.Objects()) {
        if (!object.visible || object.navMeshBoundsVolume) {
            continue;
        }
        if (object.isSpline) {
            continue;   // splines are authoring paths, not runtime meshes
        }

        const Transform* transform = scene.TryGetTransform(object.entity);
        const MeshRenderer* renderer = scene.TryGetMeshRenderer(object.entity);
        if (!transform || !renderer) {
            continue;
        }

        if (object.light) {
            const Light* light = scene.TryGetLight(object.entity);
            const Light& data = light ? *light : object.lightData;
            out << "light "
                << StoredPath(object.name) << ' '
                << LightTypeName(data.type) << ' '
                << transform->position.x << ' ' << transform->position.y << ' ' << transform->position.z << ' '
                << data.color.r << ' ' << data.color.g << ' ' << data.color.b << ' '
                << data.intensity << ' '
                << data.direction.x << ' ' << data.direction.y << ' ' << data.direction.z << ' '
                << data.innerAngle << ' ' << data.outerAngle << ' ' << data.range << ' ' << data.sourceRadius << '\n';
            continue;
        }

        out << "entity "
            << PrimitiveName(object.primitive) << ' '
            << StoredPath(object.name) << ' '
            << transform->position.x << ' ' << transform->position.y << ' ' << transform->position.z << ' '
            << transform->scale.x << ' ' << transform->scale.y << ' ' << transform->scale.z << ' '
            << transform->rotation.w << ' ' << transform->rotation.x << ' '
            << transform->rotation.y << ' ' << transform->rotation.z << ' '
            << renderer->color.r << ' ' << renderer->color.g << ' ' << renderer->color.b << ' '
            << StoredPath(object.modelAssetPath) << ' '
            << StoredPath(object.materialAssetPath) << ' '
            << object.modelOrientationEuler.x << ' '
            << object.modelOrientationEuler.y << ' '
            << object.modelOrientationEuler.z << ' '
            << object.modelOffsetPosition.x << ' '
            << object.modelOffsetPosition.y << ' '
            << object.modelOffsetPosition.z << ' '
            << object.modelOffsetScale.x << ' '
            << object.modelOffsetScale.y << ' '
            << object.modelOffsetScale.z << ' '
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
        for (const EditorScene::AnimationEvent& event : object.animationEvents) {
            out << event.clipIndex << ' '
                << event.time << ' '
                << StoredPath(event.name) << ' ';
        }
        out << object.animationActionProfiles.size() << ' ';
        for (const EditorScene::AnimationActionProfile& profile : object.animationActionProfiles) {
            out << StoredPath(profile.name) << ' '
                << profile.clipIndex << ' '
                << StoredPath(profile.clipName) << ' '
                << StoredPath(profile.maskRootBone) << ' '
                << profile.fadeIn << ' '
                << profile.fadeOut << ' '
                << profile.speed << ' ';
        }
        out << object.animationStates.size() << ' ';
        for (const EditorScene::AnimationStateNode& state : object.animationStates) {
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
                << (state.rootMotion ? 1 : 0) << ' '
                << (state.blendSpace2D ? 1 : 0) << ' '
                << StoredPath(state.blendParameterY) << ' '
                << (state.synchronizeBlendSpace ? 1 : 0) << ' '
                << state.blendSamples.size() << ' ';
            for (const auto& sample : state.blendSamples) {
                out << sample.clipIndex << ' ' << StoredPath(sample.clipName) << ' '
                    << sample.value << ' ' << sample.valueY << ' ';
            }
        }
        out << object.animationParameters.size() << ' ';
        for (const EditorScene::AnimationParameter& parameter : object.animationParameters) {
            out << StoredPath(parameter.name) << ' '
                << static_cast<int>(parameter.type) << ' '
                << parameter.defaultValue << ' ';
        }
        out << object.animationTransitions.size() << ' ';
        for (const EditorScene::AnimationStateTransition& transition : object.animationTransitions) {
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
            << (object.healthEnabled ? 1 : 0) << ' '
            << object.health.hp << ' '
            << object.health.maxHp << ' '
            << (object.health.alive ? 1 : 0) << ' '
            << (object.scriptEnabled ? 1 : 0) << ' '
            << StoredPath(object.scriptClassName) << ' '
            << StoredPath(object.scriptPath) << ' '
            << object.scriptFields.size();
        for (const EditorScene::ScriptField& field : object.scriptFields) {
            out << ' '
                << StoredPath(field.name) << ' '
                << static_cast<int>(field.type) << ' '
                << StoredPath(field.value);
        }
        out << ' '
            << (object.audioSourceEnabled ? 1 : 0) << ' '
            << StoredPath(object.audioAssetPath) << ' '
            << object.audioVolume << ' '
            << object.audioPitch << ' '
            << (object.audioSpatial ? 1 : 0) << ' '
            << (object.audioLoop ? 1 : 0) << ' '
            << (object.audioAutoplay ? 1 : 0) << ' '
            << object.audioMinDistance << ' '
            << object.audioMaxDistance << ' '
            << object.audioRolloff << ' '
            << StoredPath(object.triggerTargetName) << ' '
            << static_cast<int>(object.triggerEnterAudioAction) << ' '
            << static_cast<int>(object.triggerExitAudioAction) << ' '
            << static_cast<int>(object.audioBus) << ' '
            << object.audioDopplerFactor << ' '
            << object.audioConeInnerAngle << ' '
            << object.audioConeOuterAngle << ' '
            << object.audioConeOuterGain << ' '
            << object.audioOcclusion << ' '
            << object.audioPriority;
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
            << ' ' << (particle.cullingEnabled ? 1 : 0) << ' ' << particle.boundsRadius
            << ' ' << StoredPath(object.triggerTargetName)
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

    for (const EditorScene::Object& object : scene.Objects()) {
        if (object.materialParameterOverrides.empty()) continue;
        out << "material_overrides " << std::quoted(object.name) << ' '
            << object.materialParameterOverrides.size();
        for (const auto& overrideValue : object.materialParameterOverrides)
            out << ' ' << std::quoted(overrideValue.first)
                << ' ' << std::quoted(overrideValue.second);
        out << '\n';
    }

    // Player-controller settings (name-matched, like material_overrides). The
    // standalone runtime player reads these to spawn a first/third-person player.
    for (const EditorScene::Object& object : scene.Objects()) {
        if (!object.playerControllerEnabled) continue;
        const EditorScene::PlayerControllerSettings& s = object.playerController;
        out << "player_controller " << std::quoted(object.name) << ' '
            << (s.firstPerson ? 1 : 0) << ' '
            << s.walkSpeed << ' ' << s.runSpeed << ' ' << s.jumpSpeed << ' '
            << s.lookSensitivity << ' '
            << s.capsuleRadius << ' ' << s.capsuleHeight << ' ' << s.eyeHeight << ' '
            << s.cameraDistance << ' ' << s.cameraTargetHeight << ' '
            << (s.cameraCollision ? 1 : 0) << ' '
            << s.cameraProbeRadius << ' ' << s.cameraCollisionPadding << ' ' << s.cameraReturnSpeed << ' '
            << (s.shoulderCamera ? 1 : 0) << ' '
            << s.shoulderOffset << ' ' << s.shoulderSwitchSpeed << ' '
            << (s.rightShoulder ? 1 : 0) << ' '
            << (s.lockOnEnabled ? 1 : 0) << ' '
            << s.lockOnRange << ' ' << s.lockOnViewAngle << ' '
            << s.lockOnTargetHeight << ' ' << s.lockOnTrackingSpeed << ' '
            << s.maxSlopeDegrees << ' ' << s.stepHeight << '\n';
    }

    for (const EditorScene::Object& object : scene.Objects()) {
        if (!object.visible || !object.navAgentEnabled) continue;
        out << "nav_agent " << std::quoted(object.name) << ' '
            << object.navAgentSpeed << ' '
            << object.navAgentMaxForce << ' '
            << object.navAgentReachRadius << ' '
            << object.navAgentRepathInterval << ' '
            << std::quoted(object.navAgentTargetName) << ' '
            << object.navAgentVisionRange << ' '
            << object.navAgentVisionHalfAngle << ' '
            << StoredPath(object.navAgentBrainAsset) << ' '
            << object.navAgentTeam << ' '
            << (object.navAgentAutoTarget ? 1 : 0) << ' '
            << object.patrolPoints.size();
        for (const glm::vec3& point : object.patrolPoints)
            out << ' ' << point.x << ' ' << point.y << ' ' << point.z;
        out << ' ' << static_cast<int>(object.navMovementMode)
            << ' ' << object.navMovementGravity
            << ' ' << object.navMovementMaxFallSpeed
            << ' ' << object.navMovementGroundProbe
            << ' ' << object.navMovementStepHeight
            << ' ' << object.navMovementMaxSlope;
        out << '\n';
    }

    for (const EditorScene::Object& object : scene.Objects()) {
        const bool transformAction =
            object.triggerEnterMoverAction != EditorScene::TriggerActionMode::None ||
            object.triggerEnterRotatorAction != EditorScene::TriggerActionMode::None ||
            object.triggerExitMoverAction != EditorScene::TriggerActionMode::None ||
            object.triggerExitRotatorAction != EditorScene::TriggerActionMode::None;
        const bool cameraAction =
            object.triggerEnterCameraAction != EditorScene::CameraSequenceTriggerAction::None ||
            object.triggerExitCameraAction != EditorScene::CameraSequenceTriggerAction::None;
        if (object.visible && object.colliderEnabled && object.collider.isTrigger
            && (transformAction || cameraAction)) {
            out << "trigger_action " << std::quoted(object.name) << ' '
                << std::quoted(object.triggerTargetName) << ' '
                << static_cast<int>(object.triggerEnterMoverAction) << ' '
                << static_cast<int>(object.triggerEnterRotatorAction) << ' '
                << static_cast<int>(object.triggerExitMoverAction) << ' '
                << static_cast<int>(object.triggerExitRotatorAction) << ' '
                << std::quoted(object.triggerCameraSequenceName) << ' '
                << static_cast<int>(object.triggerEnterCameraAction) << ' '
                << static_cast<int>(object.triggerExitCameraAction) << ' '
                << (object.triggerCameraLockInput ? 1 : 0) << ' '
                << (object.triggerCameraSkippable ? 1 : 0) << '\n';
        }
        if (object.visible && object.cameraZoneEnabled && object.colliderEnabled
            && object.collider.isTrigger) {
            out << "camera_zone " << std::quoted(object.name) << ' '
                << std::quoted(object.cameraZonePresetName) << ' '
                << (object.cameraZoneRestoreOnExit ? 1 : 0) << ' '
                << object.cameraZonePriority << ' '
                << object.cameraZoneReturnBlend << '\n';
        }
        if (object.visible && object.isTerrain) {
            out << "terrain " << std::quoted(object.name) << ' '
                << object.terrainRes << ' ' << object.terrainSize << ' '
                << object.terrainMaxHeight << ' ' << object.terrainSeed << ' '
                << object.terrainOctaves << ' ' << object.terrainFrequency << ' '
                << object.terrainHeights.size();
            for (float height : object.terrainHeights) out << ' ' << height;
            out << ' ' << object.terrainPaint.size();
            for (unsigned char paint : object.terrainPaint)
                out << ' ' << static_cast<unsigned>(paint);
            out << '\n';
        }
    }
    for (const EditorScene::PhysicsJoint& joint : scene.PhysicsJoints()) {
        if (!joint.enabled) continue;
        out << "physics_joint " << static_cast<int>(joint.type) << ' '
            << std::quoted(joint.objectA) << ' ' << std::quoted(joint.objectB) << ' '
            << (joint.worldAnchor ? 1 : 0) << ' '
            << joint.anchor.x << ' ' << joint.anchor.y << ' ' << joint.anchor.z << ' '
            << joint.restLength << ' ' << (joint.rope ? 1 : 0) << ' '
            << joint.stiffness << ' ' << joint.damping << '\n';
    }

    return true;
}
