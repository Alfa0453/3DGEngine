#include "RuntimeSceneExporter.h"

#include <engine/assets/AssetReference.h>
#include <engine/assets/AssetRegistry.h>
#include <engine/ecs/Components.h>
#include <engine/physics/PhysicsComponents.h>
#include <engine/math/Spline.h>
#include <engine/gameplay/InteractionSystem.h>
#include <engine/gameplay/PortalSystem.h>
#include <engine/gameplay/QuestSystem.h>
#include <engine/gameplay/DialogueSystem.h>
#include <engine/gameplay/InventorySystem.h>
#include <engine/gameplay/CombatSystem.h>
#include <engine/gameplay/SpawnSystem.h>

#include <algorithm>
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

void WriteColliderFields(std::ostream& out, const engine::ecs::Collider& c) {
    out << static_cast<int>(c.shape) << ' ' << c.radius << ' ' << c.halfHeight << ' '
        << c.majorRadius << ' ' << c.minorRadius << ' ' << c.steps << ' '
        << c.halfExtents.x << ' ' << c.halfExtents.y << ' ' << c.halfExtents.z << ' '
        << c.planeNormal.x << ' ' << c.planeNormal.y << ' ' << c.planeNormal.z << ' '
        << c.planeOffset << ' ' << c.restitution << ' ' << c.friction << ' '
        << c.isTrigger << ' ' << c.layer << ' ' << c.mask << ' '
        << c.localPosition.x << ' ' << c.localPosition.y << ' ' << c.localPosition.z << ' '
        << c.localRotation.w << ' ' << c.localRotation.x << ' '
        << c.localRotation.y << ' ' << c.localRotation.z << ' '
        << c.localScale.x << ' ' << c.localScale.y << ' ' << c.localScale.z << ' '
        << c.inheritTransformScale << ' ' << std::quoted(c.collisionAssetPath) << ' '
        << c.collisionDirty << ' '
        // Pass-4 material tail (runtime scene 114+).
        << c.staticFriction << ' ' << c.dynamicFriction << ' ' << c.density << ' '
        << static_cast<int>(c.frictionCombine) << ' '
        << static_cast<int>(c.restitutionCombine) << ' '
        << std::quoted(c.physicsMaterialPath.empty() ? std::string("-") : c.physicsMaterialPath) << ' ';
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

    const engine::AssetHandle sceneId = scene.AssetId().Valid()
        ? scene.AssetId() : engine::AssetHandle::Generate();
    const std::string contentRoot = engine::FindContentRootForAsset(path);
    engine::AssetRegistry assetRegistry;
    std::string ignoredRegistryError;
    const bool haveRegistry = !contentRoot.empty()
        && assetRegistry.Load(
            engine::AssetRegistry::DefaultRegistryPath(contentRoot),
            &ignoredRegistryError);
    const auto materialIdFor = [&](const std::string& assetPath,
                                   engine::AssetHandle current) {
        if (current.Valid() || !haveRegistry || assetPath.empty())
            return current;
        engine::AssetReference reference = engine::MakeAssetReference(
            &assetRegistry, contentRoot, assetPath,
            engine::AssetType::Material);
        if (!reference.id.Valid())
            reference = engine::MakeAssetReference(
                &assetRegistry, contentRoot, assetPath,
                engine::AssetType::Texture);
        return reference.id;
    };
    const auto shaderIdFor = [&](const std::string& assetPath,
                                 engine::AssetHandle current) {
        if (current.Valid() || !haveRegistry || assetPath.empty())
            return current;
        return engine::MakeAssetReference(
            &assetRegistry, contentRoot, assetPath,
            engine::AssetType::Shader).id;
    };
    const auto assetIdFor = [&](const std::string& assetPath,
                                engine::AssetHandle current,
                                engine::AssetType type) {
        if (current.Valid() || !haveRegistry || assetPath.empty())
            return current;
        return engine::MakeAssetReference(
            &assetRegistry, contentRoot, assetPath, type).id;
    };
    out << "3DGRuntimeScene 115 " << sceneId.ToString() << '\n';
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
        << environment.shadowDistance << ' '
        << ([&] {
               const engine::AssetHandle id = assetIdFor(
                   environment.hudAsset, environment.hudAssetId,
                   engine::AssetType::Hud);
               return id.Valid() ? id.ToString() : std::string("-");
           }()) << '\n';
    const EditorScene::GameModeSettings& gameMode = scene.GetGameModeSettings();
    out << "game_mode "
        << StoredPath(gameMode.playerObjectName) << ' '
        << gameMode.playerInputEnabled << ' '
        << gameMode.startPaused << ' '
        << gameMode.allowPause << ' '
        << gameMode.allowRestart << ' '
        << gameMode.loseOnPlayerDeath << ' '
        << gameMode.initialScore << ' '
        << gameMode.cameraOverride << ' '
        << std::clamp(gameMode.cameraMode, 0, 3) << '\n';
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
    out << "atmosphere " << environment.atmosphereRayleigh << ' '
        << environment.atmosphereRayleighHeight << ' ' << environment.atmosphereMie << ' '
        << environment.atmosphereMieHeight << ' ' << environment.atmosphereMieAnisotropy << ' '
        << environment.atmosphereOzone << ' ' << environment.atmosphereIntensity << ' '
        << environment.sunAngularDiameter << ' ' << environment.sunDiskIntensity << '\n';
    out << "night_environment " << environment.stars << ' ' << environment.starIntensity << ' '
        << environment.moon << ' ' << environment.moonColor.r << ' ' << environment.moonColor.g << ' '
        << environment.moonColor.b << ' ' << environment.moonIntensity << ' '
        << environment.moonAngularDiameter << ' ' << environment.moonPhase << '\n';
    out << "night_energy " << environment.dayEnvironmentIntensity << ' '
        << environment.twilightEnvironmentIntensity << ' '
        << environment.nightEnvironmentIntensity << ' '
        << environment.moonGiContribution << ' '
        << environment.nightReflectionIntensity << ' '
        << environment.nightFogScattering << ' '
        << environment.nightCloudAmbient << '\n';
    out << "night_exposure " << environment.preserveNightDarkness << ' '
        << environment.nightExposureLimitEV << '\n';
    out << "volumetrics " << environment.volumetricFog << ' '
        << environment.volumetricScattering << ' ' << environment.volumetricExtinction << ' '
        << environment.volumetricAnisotropy << ' ' << environment.volumetricStartDistance << ' '
        << environment.volumetricMaxDistance << ' ' << environment.environmentQuality << '\n';
    out << "presentation " << environment.autoExposure << ' ' << environment.exposureMinEV << ' '
        << environment.exposureMaxEV << ' ' << environment.exposureCompensationEV << ' '
        << environment.exposureSpeedUp << ' ' << environment.exposureSpeedDown << ' '
        << environment.bloom << ' ' << environment.bloomThreshold << ' ' << environment.bloomKnee << ' '
        << environment.bloomStrength << ' ' << environment.colorTemperature << ' '
        << environment.colorTint << ' ' << environment.colorSaturation << ' ' << environment.colorContrast << ' '
        << environment.colorLift.r << ' ' << environment.colorLift.g << ' ' << environment.colorLift.b << ' '
        << environment.colorGamma.r << ' ' << environment.colorGamma.g << ' ' << environment.colorGamma.b << ' '
        << environment.colorGain.r << ' ' << environment.colorGain.g << ' ' << environment.colorGain.b << ' '
        << environment.colorLutIntensity << ' '
        << std::quoted(StoredPath(environment.colorLutPath)) << '\n';
    out << "skylight_occlusion "
        << (environment.skylightOcclusion ? 1 : 0) << ' '
        << environment.skylightOcclusionStrength << ' '
        << environment.minimumSkylight << '\n';
    out << "lighting_tuning " << environment.exposureEV << ' '
        << environment.specularOcclusionStrength << ' '
        << environment.localProbeInfluence << ' '
        << environment.lightingDebugMode << '\n';
    out << "lighting_build " << std::quoted(StoredPath(environment.lightingBuildAsset))
        << ' ' << environment.lightingBuildHash << '\n';
    out << "dynamic_gi " << environment.dynamicGiEnabled << ' '
        << environment.dynamicGiQuality << ' ' << environment.dynamicGiProbeSpacing << ' '
        << environment.dynamicGiRaysPerProbe << ' ' << environment.dynamicGiProbesPerFrame << ' '
        << environment.dynamicGiMaxRaysPerFrame << ' ' << environment.dynamicGiMaxRayDistance << ' '
        << environment.dynamicGiHysteresis << ' ' << environment.dynamicGiIntensity << ' '
        << environment.dynamicGiRelocation << ' ' << environment.dynamicGiClassification << ' '
        << environment.dynamicGiVisibilityWeighting << ' '
        << environment.dynamicGiMultiBounce << ' '
        << environment.dynamicGiMultiBounceStrength << '\n';
    out << "ssgi " << environment.ssgiEnabled << ' ' << environment.ssgiRayLength << ' '
        << environment.ssgiSteps << ' ' << environment.ssgiThickness << ' '
        << environment.ssgiIntensity << '\n';
    out << "sky "
        << environment.skyMode << ' '
        << StoredPath(environment.skyTexturePath) << ' '
        << environment.skyRotation << ' '
        << environment.skyIntensity << '\n';   // runtime scene version 88+
    out << "day_night_timeline " << StoredPath(environment.dayNightTimelinePath)
        << ' ' << environment.dayNightTimelineAutoplay << '\n';
    for (const EditorScene::Environment::PostProcessEffect& effect :
         environment.postProcessEffects) {
        out << "post_effect "
            << std::quoted(effect.shaderPath) << ' '
            << ([&] {
                   const engine::AssetHandle id = shaderIdFor(
                       effect.shaderPath, effect.shaderAssetId);
                   return id.Valid() ? id.ToString() : std::string("-");
               }()) << ' '
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
        if (!object.visible || !object.isWater) continue;
        const Transform* transform = scene.TryGetTransform(object.entity);
        if (!transform) continue;
        glm::vec2 flowDir(0.0f);
        float flowStrength = 0.0f;
        const EditorScene::Object* flowSpline = nullptr;
        glm::vec3 waterCenter = transform->position;
        float waterSize = object.waterSize;
        if (!object.waterFlowSpline.empty()) {
            for (const EditorScene::Object& splineObject : scene.Objects()) {
                if (!splineObject.isSpline
                    || splineObject.name != object.waterFlowSpline
                    || splineObject.splinePoints.size() < 2) continue;
                engine::Spline spline(
                    splineObject.splinePoints, splineObject.splineClosed);
                flowSpline = &splineObject;
                glm::vec3 boundsMin = splineObject.splinePoints.front();
                glm::vec3 boundsMax = boundsMin;
                for (const glm::vec3& point : splineObject.splinePoints) {
                    boundsMin = glm::min(boundsMin, point);
                    boundsMax = glm::max(boundsMax, point);
                }
                waterCenter = (boundsMin + boundsMax) * 0.5f;
                waterSize = std::max(boundsMax.x - boundsMin.x,
                                     boundsMax.z - boundsMin.z) + object.waterRiverWidth;
                glm::vec3 tangent(0.0f, 0.0f, 1.0f);
                spline.ClosestPoint(transform->position, nullptr, &tangent);
                flowDir = glm::vec2(tangent.x, tangent.z);
                const float length = glm::length(flowDir);
                if (length > 0.0001f) {
                    flowDir /= length;
                    flowStrength = std::max(object.waterSeaSpeed, 0.5f);
                }
                break;
            }
        }
        out << "water " << std::quoted(object.name) << ' '
            << waterCenter.x << ' ' << waterCenter.y << ' ' << waterCenter.z << ' '
            << waterSize << ' ' << object.waterResolution << ' '
            << object.waterShallow.r << ' ' << object.waterShallow.g << ' ' << object.waterShallow.b << ' '
            << object.waterDeep.r << ' ' << object.waterDeep.g << ' ' << object.waterDeep.b << ' '
            << object.waterReflection.r << ' ' << object.waterReflection.g << ' ' << object.waterReflection.b << ' '
            << object.waterTransparency << ' ' << object.waterFresnel << ' '
            << object.waterSpecular << ' ' << object.waterShininess << ' '
            << object.waterSeaHeight << ' ' << object.waterSeaChoppy << ' '
            << object.waterSeaSpeed << ' ' << object.waterSeaFreq << ' ' << object.waterFoam << ' '
            << flowDir.x << ' ' << flowDir.y << ' ' << flowStrength << ' '
            << object.waterDepthFadeDistance << ' ' << object.waterShoreFoamWidth << ' '
            << object.waterShoreFoamStrength << ' ' << object.waterRefractionStrength << ' '
            << object.waterReflectionRoughness << ' '
            << object.waterEnvironmentReflectionStrength << ' ' << object.waterAbsorptionStrength << ' '
            << object.waterCausticsStrength << ' ' << object.waterCausticsScale << ' '
            << object.waterMaxRenderDistance << ' '
            << object.waterUnderwaterTint.r << ' ' << object.waterUnderwaterTint.g << ' '
            << object.waterUnderwaterTint.b << ' ' << object.waterUnderwaterFogDensity << ' '
            << object.waterUnderwaterDistortion << ' '
            << object.waterUnderwaterTransitionSpeed << ' '
            << object.waterRiverWidth << ' '
            << (flowSpline && flowSpline->splineClosed ? 1 : 0) << ' '
            << (flowSpline ? flowSpline->splinePoints.size() : 0);
        if (flowSpline) {
            for (std::size_t i = 0; i < flowSpline->splinePoints.size(); ++i) {
                const glm::vec3& point = flowSpline->splinePoints[i];
                const glm::vec3 rotation = i < flowSpline->splinePointRotations.size()
                    ? flowSpline->splinePointRotations[i] : glm::vec3(0.0f);
                out << ' ' << point.x << ' ' << point.y << ' ' << point.z
                    << ' ' << rotation.x << ' ' << rotation.y << ' ' << rotation.z;
            }
        }
        out << ' ' << StoredPath(flowSpline ? flowSpline->name : std::string{});
        out << ' ' << StoredPath(object.waterShaderPath);   // runtime scene version 88+
        out << '\n';
    }

    // General spline objects remain addressable by name at runtime and can be
    // manipulated by native or Lua gameplay scripts.
    for (const EditorScene::Object& spline : scene.Objects()) {
        if (!spline.isSpline) continue;
        out << "spline " << StoredPath(spline.name) << ' '
            << (spline.splineClosed ? 1 : 0) << ' ' << spline.splinePoints.size();
        for (std::size_t i = 0; i < spline.splinePoints.size(); ++i) {
            const glm::vec3& point = spline.splinePoints[i];
            const glm::vec3 rotation = i < spline.splinePointRotations.size()
                ? spline.splinePointRotations[i] : glm::vec3(0.0f);
            out << ' ' << point.x << ' ' << point.y << ' ' << point.z
                << ' ' << rotation.x << ' ' << rotation.y << ' ' << rotation.z;
        }
        out << '\n';
    }

    for (const EditorScene::Object& object : scene.Objects()) {
        if (!object.isFoliage || !object.visible || object.foliageAssetPath.empty()) continue;
        const Transform* transform = scene.TryGetTransform(object.entity);
        if (!transform) continue;
        const engine::AssetHandle foliageId = assetIdFor(
            object.foliageAssetPath, object.foliageAssetId,
            engine::AssetType::Foliage);
        out << "foliage " << StoredPath(object.name) << ' '
            << StoredPath(object.foliageAssetPath) << ' '
            << (foliageId.Valid() ? foliageId.ToString() : std::string("-")) << ' '
            << transform->position.x << ' ' << transform->position.y << ' ' << transform->position.z << ' '
            << transform->scale.x << ' ' << transform->scale.y << ' ' << transform->scale.z << ' '
            << transform->rotation.w << ' ' << transform->rotation.x << ' '
            << transform->rotation.y << ' ' << transform->rotation.z << ' '
            << object.foliageInstances.size();
        for (const engine::ecs::FoliageInstance& instance : object.foliageInstances) {
            out << ' ' << instance.id << ' ' << instance.typeIndex
                << ' ' << instance.position.x << ' ' << instance.position.y << ' ' << instance.position.z
                << ' ' << instance.rotationDegrees.x << ' ' << instance.rotationDegrees.y
                << ' ' << instance.rotationDegrees.z
                << ' ' << instance.scale.x << ' ' << instance.scale.y << ' ' << instance.scale.z
                << ' ' << (instance.enabled ? 1 : 0);
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
        if (object.isSpline || object.isWater || object.isFoliage) {
            continue;   // authoring helpers are rendered by their dedicated systems
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
                << data.innerAngle << ' ' << data.outerAngle << ' ' << data.range << ' ' << data.sourceRadius << ' '
                << static_cast<int>(data.areaShape) << ' ' << data.areaWidth << ' ' << data.areaHeight << ' ' << (data.areaTwoSided?1:0) << ' '
                << (data.affectDynamicGi?1:0) << ' '
                << (data.affectVolumetricFog?1:0) << ' ' << data.volumetricPriority << '\n';
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
            << (object.modelAssetId.Valid()
                ? object.modelAssetId.ToString() : std::string("-")) << ' '
            << (materialIdFor(
                    object.materialAssetPath, object.materialAssetId).Valid()
                ? materialIdFor(
                    object.materialAssetPath, object.materialAssetId).ToString()
                : std::string("-")) << ' '
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
                << StoredPath(event.name) << ' '
                << StoredPath(event.clipName) << ' ';
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
                << (transition.canInterrupt ? 1 : 0) << ' '
                << (transition.useConditions ? 1 : 0) << ' '
                << (transition.requireAllConditions ? 1 : 0) << ' '
                << transition.additionalConditions.size() << ' ';
            for (const auto& condition : transition.additionalConditions) {
                out << StoredPath(condition.parameter) << ' '
                    << static_cast<int>(condition.compare) << ' '
                    << condition.threshold << ' ';
            }
        }
        out << object.animationSources.size() << ' ';
        for (const EditorScene::AnimationSource& source : object.animationSources) {
            out << StoredPath(source.file) << ' '
                << (source.assetId.Valid()
                    ? source.assetId.ToString() : std::string("-")) << ' '
                << StoredPath(source.clipName) << ' '
                << (source.stripRootMotion ? 1 : 0) << ' '
                << StoredPath(source.sourceClipName) << ' '
                << source.basePlaybackSpeed << ' ';
        }
        out << object.modelAttachments.size() << ' ';
        for (const EditorScene::ModelAttachment& a : object.modelAttachments) {
            out << StoredPath(a.modelPath) << ' '
                << (a.modelAssetId.Valid()
                    ? a.modelAssetId.ToString() : std::string("-")) << ' '
                << (materialIdFor(a.materialPath, a.materialAssetId).Valid()
                    ? materialIdFor(
                        a.materialPath, a.materialAssetId).ToString()
                    : std::string("-")) << ' '
                << StoredPath(a.boneName) << ' '
                << a.position.x << ' ' << a.position.y << ' ' << a.position.z << ' '
                << a.eulerDegrees.x << ' ' << a.eulerDegrees.y << ' ' << a.eulerDegrees.z << ' '
                << a.scale.x << ' ' << a.scale.y << ' ' << a.scale.z << ' '
                << StoredPath(a.materialPath) << ' '
                << StoredPath(a.socketName) << ' ';
        }
        // Foot IK (3DGRuntimeScene >= 87).
        out << (object.footIK.enabled ? 1 : 0) << ' '
            << object.footIK.traceUp << ' '
            << object.footIK.traceDown << ' '
            << object.footIK.footHeight << ' '
            << object.footIK.pelvisWeight << ' '
            << object.footIK.maxPelvisDrop << ' '
            << object.footIK.weight << ' ';
        out << StoredPath(object.ikRigPath) << ' '
            << (object.ikRigAssetId.Valid() ? object.ikRigAssetId.ToString() : std::string("-")) << ' ';
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
            << object.collider.localPosition.x << ' ' << object.collider.localPosition.y << ' ' << object.collider.localPosition.z << ' '
            << object.collider.localRotation.w << ' ' << object.collider.localRotation.x << ' '
            << object.collider.localRotation.y << ' ' << object.collider.localRotation.z << ' '
            << object.collider.localScale.x << ' ' << object.collider.localScale.y << ' ' << object.collider.localScale.z << ' '
            << (object.collider.inheritTransformScale ? 1 : 0) << ' '
            << StoredPath(object.collider.collisionAssetPath) << ' '
            << (object.collider.collisionDirty ? 1 : 0) << ' '
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
        out << ' ' << object.scriptExecutionOrder
            << ' ' << object.scriptDependencies.size();
        for (const std::string& dependency : object.scriptDependencies)
            out << ' ' << StoredPath(dependency);
        out << ' ' << object.additionalScripts.size();
        for (const EditorScene::ScriptBinding& script : object.additionalScripts) {
            out << ' ' << (script.enabled ? 1 : 0)
                << ' ' << StoredPath(script.className)
                << ' ' << StoredPath(script.path)
                << ' ' << script.fields.size();
            for (const EditorScene::ScriptField& field : script.fields) {
                out << ' ' << StoredPath(field.name)
                    << ' ' << static_cast<int>(field.type)
                    << ' ' << StoredPath(field.value);
            }
            out << ' ' << script.executionOrder
                << ' ' << script.dependencies.size();
            for (const std::string& dependency : script.dependencies)
                out << ' ' << StoredPath(dependency);
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
        out << ' ' << std::quoted(
                particle.shaderPath.empty() ? std::string("-")
                                            : particle.shaderPath)
            << ' ' << particle.shaderParameters.size();
        for (const engine::ParticleShaderParameter& parameter :
             particle.shaderParameters)
            out << ' ' << std::quoted(parameter.name)
                << ' ' << parameter.type
                << ' ' << std::quoted(parameter.value);
        const auto particleIdText = [](engine::AssetHandle id) {
            return id.Valid() ? id.ToString() : std::string("-");
        };
        out << ' ' << particleIdText(assetIdFor(
                object.particleAssetPath, object.particleAssetId,
                engine::AssetType::Particle))
            << ' ' << particleIdText(assetIdFor(
                particle.texturePath, particle.textureAssetId,
                engine::AssetType::Texture))
            << ' ' << particleIdText(assetIdFor(
                particle.meshPath, particle.meshAssetId,
                engine::AssetType::StaticMesh))
            << ' ' << particleIdText(assetIdFor(
                particle.shaderPath, particle.shaderAssetId,
                engine::AssetType::Shader))
            << ' ' << object.particleEffectLayers.size();
        for (const engine::ParticleEffectLayer& layer :
             object.particleEffectLayers)
            out << ' ' << particleIdText(assetIdFor(
                layer.assetPath, layer.assetId,
                engine::AssetType::Particle));
        out << ' ' << particleIdText(assetIdFor(
                object.audioAssetPath, object.audioAssetId,
                engine::AssetType::Audio));
        // Physics material + mass mode (runtime scene 114+). Tail of the object record.
        out << ' ' << object.collider.staticFriction
            << ' ' << object.collider.dynamicFriction
            << ' ' << object.collider.density
            << ' ' << static_cast<int>(object.collider.frictionCombine)
            << ' ' << static_cast<int>(object.collider.restitutionCombine)
            << ' ' << static_cast<int>(object.rigidBody.massMode)
            // Center of mass (runtime scene 114+); the runtime solver rotates bodies about it.
            << ' ' << (object.rigidBody.autoCenterOfMass ? 1 : 0)
            << ' ' << object.rigidBody.centerOfMassLocal.x
            << ' ' << object.rigidBody.centerOfMassLocal.y
            << ' ' << object.rigidBody.centerOfMassLocal.z;
        out << '\n';
    }

    for (const EditorScene::Object& object : scene.Objects()) {
        if (object.additionalColliders.empty()) continue;
        out << "compound_colliders " << std::quoted(object.name) << ' '
            << object.additionalColliders.size() << ' ';
        for (const engine::ecs::Collider& collider : object.additionalColliders)
            WriteColliderFields(out, collider);
        out << '\n';
    }

    // Pass-5 collision matrix (runtime scene 114+): enabled flag + the 9 named-layer masks.
    {
        const EditorScene::Environment& env = scene.GetEnvironment();
        out << "physics_matrix " << (env.physicsLayerMatrixEnabled ? 1 : 0);
        for (int i = 0; i < 9; ++i) out << ' ' << env.physicsLayerMasks[i];
        out << '\n';
    }

    for (const EditorScene::Object& object : scene.Objects()) {
        if (object.materialParameterOverrides.empty() && !object.decal) continue;
        const bool hasAuthoredOpacity =
            object.materialParameterOverrides.find("Opacity") !=
            object.materialParameterOverrides.end();
        const std::size_t overrideCount = object.materialParameterOverrides.size()
            + ((object.decal && !hasAuthoredOpacity) ? 1u : 0u);
        out << "material_overrides " << std::quoted(object.name) << ' '
            << overrideCount;
        for (const auto& overrideValue : object.materialParameterOverrides) {
            if (object.decal && overrideValue.first == "Opacity") {
                out << ' ' << std::quoted(overrideValue.first)
                    << ' ' << std::quoted(std::to_string(object.decalOpacity));
                continue;
            }
            out << ' ' << std::quoted(overrideValue.first)
                << ' ' << std::quoted(overrideValue.second);
        }
        if (object.decal && !hasAuthoredOpacity)
            out << ' ' << std::quoted(std::string("Opacity"))
                << ' ' << std::quoted(std::to_string(object.decalOpacity));
        out << '\n';
    }

    // Reflection probes carry their own transform so an invisible/empty probe
    // authoring object is still present in packaged scenes.
    for (const EditorScene::Object& object : scene.Objects()) {
        if (!object.visible || !object.reflectionProbeEnabled) continue;
        const Transform* transform = scene.TryGetTransform(object.entity);
        if (!transform) continue;
        const engine::ecs::ReflectionProbe* component =
            scene.TryGetReflectionProbe(object.entity);
        const engine::ecs::ReflectionProbe& probe = component
            ? *component : object.reflectionProbe;
        out << "reflection_probe " << std::quoted(object.name) << ' '
            << (probe.stableId.Valid() ? probe.stableId.ToString() : std::string("-")) << ' '
            << static_cast<int>(probe.shape) << ' '
            << transform->position.x << ' ' << transform->position.y << ' '
            << transform->position.z << ' '
            << transform->rotation.w << ' ' << transform->rotation.x << ' '
            << transform->rotation.y << ' ' << transform->rotation.z << ' '
            << probe.boxExtents.x << ' ' << probe.boxExtents.y << ' '
            << probe.boxExtents.z << ' ' << probe.radius << ' '
            << probe.blendDistance << ' ' << probe.intensity << ' '
            << probe.priority << ' ' << probe.captureResolution << ' '
            << probe.includeSky << ' ' << probe.enabled << ' '
            << std::quoted(probe.bakedCubemapPath.empty()
                    ? std::string("-") : probe.bakedCubemapPath) << ' '
            << (probe.bakedCubemapId.Valid()
                    ? probe.bakedCubemapId.ToString() : std::string("-")) << ' '
            << probe.captureSourceHash << '\n';
    }
    for(const EditorScene::Object& object:scene.Objects()){
        const Transform* transform=scene.TryGetTransform(object.entity);if(!transform)continue;
        if(object.postProcessVolumeEnabled){const auto& v=object.postProcessVolume;
            out<<"post_process_volume "<<std::quoted(object.name)<<' '
               <<transform->position.x<<' '<<transform->position.y<<' '<<transform->position.z<<' '
               <<transform->scale.x<<' '<<transform->scale.y<<' '<<transform->scale.z<<' '
               <<v.enabled<<' '<<v.unbound<<' '<<v.priority<<' '<<v.blendDistance<<' '<<v.blendWeight<<' '
               <<v.boxExtents.x<<' '<<v.boxExtents.y<<' '<<v.boxExtents.z<<' '
               <<v.overrideExposure<<' '<<v.exposureCompensationEV<<' '<<v.overrideBloom<<' '<<v.bloomStrength<<' '
               <<v.overrideColorGrading<<' '<<v.temperature<<' '<<v.tint<<' '<<v.saturation<<' '<<v.contrast<<' '
               <<v.overrideFogDensity<<' '<<v.fogDensity<<' '
               <<(v.stableId.Valid()?v.stableId.ToString():std::string("-"))<<'\n';}
        if(object.localFogVolumeEnabled){const auto& v=object.localFogVolume;
            out<<"local_fog_volume "<<std::quoted(object.name)<<' '
               <<transform->position.x<<' '<<transform->position.y<<' '<<transform->position.z<<' '
               <<transform->scale.x<<' '<<transform->scale.y<<' '<<transform->scale.z<<' '
               <<v.enabled<<' '<<static_cast<int>(v.shape)<<' '<<v.boxExtents.x<<' '<<v.boxExtents.y<<' '<<v.boxExtents.z<<' '
               <<v.radius<<' '<<v.blendDistance<<' '<<v.density<<' '<<v.albedo.x<<' '<<v.albedo.y<<' '<<v.albedo.z<<' '
               <<v.extinction<<' '<<v.anisotropy<<' '
               <<(v.stableId.Valid()?v.stableId.ToString():std::string("-"))<<'\n';}
    }

    // Player-controller settings (name-matched, like material_overrides). The
    // standalone runtime player reads these to spawn a first/third-person player.
    for (const EditorScene::Object& object : scene.Objects()) {
        if (!object.playerControllerEnabled) continue;
        const EditorScene::PlayerControllerSettings& s = object.playerController;
        const int cameraMode = s.firstPerson ? 1 : std::clamp(s.cameraMode, 0, 3);
        out << "player_controller " << std::quoted(object.name) << ' '
            << (cameraMode == 1 ? 1 : 0) << ' '
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
            << s.maxSlopeDegrees << ' ' << s.stepHeight << ' '
            << s.facingMode << ' ' << s.turnSpeed << ' '
            << cameraMode << ' '
            << s.isometricYaw << ' ' << s.isometricPitch << ' '
            << s.isometricDistance << ' '
            << s.platformerYaw << ' '
            << s.crouchSpeed << ' ' << s.crouchedHeight << ' '
            << s.swimSpeed << ' ' << s.swimVerticalSpeed << '\n';
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
            << ' ' << object.navMovementMaxSlope << ' '
            << ([&] {
                   const engine::AssetHandle id = assetIdFor(
                       object.navAgentBrainAsset,
                       object.navAgentBrainAssetId,
                       engine::AssetType::BehaviorTree);
                   return id.Valid() ? id.ToString()
                                     : std::string("-");
               }());
        // NavAgent hearing range (runtime scene version 79+).
        out << ' ' << object.navAgentHearingRange;
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
        if (object.visible) {
            const auto* interaction = scene.Registry().TryGet<engine::InteractiveMotionComponent>(object.entity);
            if (interaction && !interaction->assetPath.empty()) {
                out << "interaction " << std::quoted(object.name) << ' '
                    << std::quoted(interaction->assetPath) << ' '
                    << (interaction->asset.header.id.Valid()
                        ? interaction->asset.header.id.ToString() : std::string("-")) << '\n';
            }
            const auto* portal = scene.Registry().TryGet<engine::PortalComponent>(object.entity);
            if (portal && !portal->assetPath.empty()) {
                out << "portal " << std::quoted(object.name) << ' '
                    << std::quoted(portal->assetPath) << ' '
                    << (portal->asset.header.id.Valid() ? portal->asset.header.id.ToString() : std::string("-")) << '\n';
            }
            if (const auto* log = scene.Registry().TryGet<engine::QuestLogComponent>(object.entity))
                for (const auto& quest : log->quests) if (!quest.assetPath.empty())
                    out << "quest " << std::quoted(object.name) << ' '
                        << std::quoted(quest.assetPath) << ' '
                        << (quest.asset.header.id.Valid() ? quest.asset.header.id.ToString() : std::string("-")) << '\n';
            if (const auto* dialogue = scene.Registry().TryGet<engine::DialogueSourceComponent>(object.entity))
                if (!dialogue->assetPath.empty())
                    out << "dialogue " << std::quoted(object.name) << ' '
                        << std::quoted(dialogue->assetPath) << ' '
                        << (dialogue->asset.header.id.Valid() ? dialogue->asset.header.id.ToString() : std::string("-")) << '\n';
            if (const auto* inventory = scene.Registry().TryGet<engine::InventoryComponent>(object.entity)) {
                out << "inventory " << std::quoted(object.name) << ' ' << inventory->maximumSlots << ' ' << inventory->maximumWeight << '\n';
                for (const auto& stack : inventory->items) if (!stack.assetPath.empty())
                    out << "inventory_item " << std::quoted(object.name) << ' ' << std::quoted(stack.assetPath) << ' '
                        << (stack.asset.header.id.Valid()?stack.asset.header.id.ToString():std::string("-")) << ' ' << stack.count << ' ' << stack.equipped << '\n';
            }
            if (const auto* combat = scene.Registry().TryGet<engine::CombatComponent>(object.entity))
                if (!combat->assetPath.empty())
                    out << "combat " << std::quoted(object.name) << ' ' << std::quoted(combat->assetPath) << ' '
                        << (combat->asset.header.id.Valid()?combat->asset.header.id.ToString():std::string("-")) << '\n';
            if (const auto* spawn = scene.Registry().TryGet<engine::SpawnManagerComponent>(object.entity))
                if (!spawn->assetPath.empty())
                    out << "spawn_manager " << std::quoted(object.name) << ' ' << std::quoted(spawn->assetPath) << ' '
                        << (spawn->asset.header.id.Valid()?spawn->asset.header.id.ToString():std::string("-")) << '\n';
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
            for (const std::string& material : object.terrainLayerMaterials)
                out << ' ' << StoredPath(material);
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
            << joint.stiffness << ' ' << joint.damping << ' '
            // Ball/Hinge authoring (runtime scene 114+).
            << joint.axis.x << ' ' << joint.axis.y << ' ' << joint.axis.z << ' '
            << (joint.collideConnected ? 1 : 0) << ' '
            << (joint.angularLimit ? 1 : 0) << ' '
            << joint.minAngle << ' ' << joint.maxAngle << ' '
            << (joint.motorEnabled ? 1 : 0) << ' '
            << joint.motorTargetVelocity << ' ' << joint.motorMaxTorque << ' '
            << joint.breakImpulse << '\n';
    }

    std::vector<engine::AssetHandle> dependencies;
    const auto addDependency = [&](engine::AssetHandle id) {
        if (id.Valid()
            && std::find(dependencies.begin(), dependencies.end(), id)
                   == dependencies.end())
            dependencies.push_back(id);
    };
    addDependency(assetIdFor(
        environment.hudAsset, environment.hudAssetId,
        engine::AssetType::Hud));
    const auto addAssetPath = [&](const std::string& assetPath) {
        if (!haveRegistry || assetPath.empty()) return;
        engine::AssetReference reference = engine::MakeAssetReference(
            &assetRegistry, contentRoot, assetPath,
            engine::AssetType::Material);
        if (!reference.id.Valid())
            reference = engine::MakeAssetReference(
                &assetRegistry, contentRoot, assetPath,
                engine::AssetType::Texture);
        addDependency(reference.id);
    };
    for (const EditorScene::Object& object : scene.Objects()) {
        addDependency(object.modelAssetId);
        addDependency(object.ikRigAssetId);
        if (const auto* interaction =
                scene.Registry().TryGet<engine::InteractiveMotionComponent>(object.entity))
            addDependency(interaction->asset.header.id);
        if (const auto* portal = scene.Registry().TryGet<engine::PortalComponent>(object.entity))
            addDependency(portal->asset.header.id);
        if (const auto* log = scene.Registry().TryGet<engine::QuestLogComponent>(object.entity))
            for (const auto& quest : log->quests) addDependency(quest.asset.header.id);
        if (const auto* dialogue = scene.Registry().TryGet<engine::DialogueSourceComponent>(object.entity))
            addDependency(dialogue->asset.header.id);
        if (const auto* inventory = scene.Registry().TryGet<engine::InventoryComponent>(object.entity))
            for (const auto& stack : inventory->items) addDependency(stack.asset.header.id);
        if (const auto* combat = scene.Registry().TryGet<engine::CombatComponent>(object.entity))
            addDependency(combat->asset.header.id);
        if (const auto* spawn = scene.Registry().TryGet<engine::SpawnManagerComponent>(object.entity))
            addDependency(spawn->asset.header.id);
        addDependency(assetIdFor(
            object.collider.collisionAssetPath, {},
            engine::AssetType::StaticMesh));
        addDependency(object.materialAssetId);
        addDependency(object.reflectionProbe.bakedCubemapId);
        addDependency(assetIdFor(
            object.audioAssetPath, object.audioAssetId,
            engine::AssetType::Audio));
        addDependency(assetIdFor(
            object.navAgentBrainAsset, object.navAgentBrainAssetId,
            engine::AssetType::BehaviorTree));
        addDependency(assetIdFor(
            object.ragdoll.assetPath, {}, engine::AssetType::Ragdoll));
        addDependency(assetIdFor(
            object.particleAssetPath, object.particleAssetId,
            engine::AssetType::Particle));
        addDependency(assetIdFor(
            object.particleConfig.texturePath,
            object.particleConfig.textureAssetId,
            engine::AssetType::Texture));
        addDependency(assetIdFor(
            object.particleConfig.meshPath,
            object.particleConfig.meshAssetId,
            engine::AssetType::StaticMesh));
        addDependency(assetIdFor(
            object.particleConfig.shaderPath,
            object.particleConfig.shaderAssetId,
            engine::AssetType::Shader));
        for (const engine::ParticleEffectLayer& layer :
             object.particleEffectLayers)
            addDependency(assetIdFor(
                layer.assetPath, layer.assetId,
                engine::AssetType::Particle));
        addAssetPath(object.materialAssetPath);
        for (const EditorScene::AnimationSource& source : object.animationSources)
            addDependency(source.assetId);
        for (const EditorScene::ModelAttachment& attachment :
             object.modelAttachments) {
            addDependency(attachment.modelAssetId);
            addDependency(attachment.materialAssetId);
            addAssetPath(attachment.materialPath);
        }
        for (const std::string& material : object.terrainLayerMaterials)
            addAssetPath(material);
    }
    for (const EditorScene::Object& object : scene.Objects()) {
        if (!object.visible || !object.ragdollEnabled) continue;
        out << "ragdoll " << std::quoted(object.name) << ' '
            << object.ragdoll.enabled << ' '
            << object.ragdoll.activateOnDeath << ' '
            << object.ragdoll.totalMass << ' '
            << object.ragdoll.bodyRadiusScale << ' '
            << object.ragdoll.linearDamping << ' '
            << object.ragdoll.angularDamping << ' '
            << object.ragdoll.deathImpulse << ' '
            << object.ragdoll.maxBodies << ' '
            << StoredPath(object.ragdoll.assetPath) << '\n';
    }
    for (const EditorScene::Environment::PostProcessEffect& effect :
         environment.postProcessEffects)
        addDependency(shaderIdFor(
            effect.shaderPath, effect.shaderAssetId));
    out << "ASSET_DEPS " << dependencies.size();
    for (engine::AssetHandle id : dependencies) out << ' ' << id.ToString();
    out << '\n';

    return true;
}
