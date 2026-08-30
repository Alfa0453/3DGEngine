#include "EditorScene.h"

#include <engine/assets/AssetReference.h>
#include <engine/assets/RagdollAsset.h>
#include <engine/assets/AssetRegistry.h>
#include <engine/graphics/Mesh.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <utility>
#include <cstddef>
#include <filesystem>
#include <cmath>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

using engine::ecs::Entity;
using engine::ecs::Light;
using engine::ecs::MeshRenderer;
using engine::ecs::RigidBody;
using engine::ecs::Collider;
using engine::ecs::Transform;

namespace {

constexpr float kMaxSceneCoordinate = 1000000.0f;
constexpr float kMaxSceneScale = 10000.0f;

float FiniteClamp(float value, float fallback, float minimum, float maximum) {
    if (!std::isfinite(value)) return fallback;
    return std::clamp(value, minimum, maximum);
}

void NormalizeTransformValues(Transform& transform) {
    transform.position.x = FiniteClamp(transform.position.x, 0.0f, -kMaxSceneCoordinate, kMaxSceneCoordinate);
    transform.position.y = FiniteClamp(transform.position.y, 0.0f, -kMaxSceneCoordinate, kMaxSceneCoordinate);
    transform.position.z = FiniteClamp(transform.position.z, 0.0f, -kMaxSceneCoordinate, kMaxSceneCoordinate);
    float* scales[] = {&transform.scale.x, &transform.scale.y, &transform.scale.z};
    for (float* valuePtr : scales) {
        float& value = *valuePtr;
        if (!std::isfinite(value)) value = 1.0f;
        const float sign = value < 0.0f ? -1.0f : 1.0f;
        value = sign * std::clamp(std::abs(value), 0.0001f, kMaxSceneScale);
    }
    const float qLength = glm::length(transform.rotation);
    if (!std::isfinite(qLength) || qLength < 0.000001f) {
        transform.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    } else {
        transform.rotation = glm::normalize(transform.rotation);
    }
}

void NormalizeColliderValues(Collider& collider) {
    collider.radius = FiniteClamp(collider.radius, 0.5f, 0.001f, kMaxSceneScale);
    collider.halfHeight = FiniteClamp(collider.halfHeight, 0.5f, 0.0f, kMaxSceneScale);
    collider.halfExtents.x = FiniteClamp(collider.halfExtents.x, 0.5f, 0.001f, kMaxSceneScale);
    collider.halfExtents.y = FiniteClamp(collider.halfExtents.y, 0.5f, 0.001f, kMaxSceneScale);
    collider.halfExtents.z = FiniteClamp(collider.halfExtents.z, 0.5f, 0.001f, kMaxSceneScale);
    collider.majorRadius = FiniteClamp(collider.majorRadius, 0.35f, 0.0f, kMaxSceneScale);
    collider.minorRadius = FiniteClamp(collider.minorRadius, 0.15f, 0.0f, kMaxSceneScale);
    collider.planeOffset = FiniteClamp(collider.planeOffset, 0.0f,
        -kMaxSceneCoordinate, kMaxSceneCoordinate);
    const float normalLength = glm::length(collider.planeNormal);
    if (!std::isfinite(normalLength) || normalLength < 0.000001f) {
        collider.planeNormal = glm::vec3(0.0f, 1.0f, 0.0f);
    } else {
        collider.planeNormal /= normalLength;
    }
    collider.steps = std::clamp(collider.steps, 1, 64);
    collider.restitution = FiniteClamp(collider.restitution, 0.4f, 0.0f, 1.0f);
    collider.friction = FiniteClamp(collider.friction, 0.5f, 0.0f, 2.0f);
    collider.localPosition.x = FiniteClamp(collider.localPosition.x, 0.0f, -kMaxSceneCoordinate, kMaxSceneCoordinate);
    collider.localPosition.y = FiniteClamp(collider.localPosition.y, 0.0f, -kMaxSceneCoordinate, kMaxSceneCoordinate);
    collider.localPosition.z = FiniteClamp(collider.localPosition.z, 0.0f, -kMaxSceneCoordinate, kMaxSceneCoordinate);
    const float localRotationLength = glm::length(collider.localRotation);
    collider.localRotation = (!std::isfinite(localRotationLength) || localRotationLength < 0.000001f)
        ? glm::quat(1.0f, 0.0f, 0.0f, 0.0f) : glm::normalize(collider.localRotation);
    collider.localScale = glm::clamp(glm::abs(collider.localScale), glm::vec3(0.0001f),
                                     glm::vec3(kMaxSceneScale));
}

void NormalizeControllerValues(EditorScene::PlayerControllerSettings& settings) {
    settings.walkSpeed = FiniteClamp(settings.walkSpeed, 4.0f, 0.0f, 1000.0f);
    settings.runSpeed = FiniteClamp(settings.runSpeed, 7.0f, 0.0f, 1000.0f);
    settings.runSpeed = std::max(settings.runSpeed, settings.walkSpeed);
    settings.jumpSpeed = FiniteClamp(settings.jumpSpeed, 5.0f, 0.0f, 1000.0f);
    settings.crouchSpeed = FiniteClamp(settings.crouchSpeed, 2.0f, 0.0f, 1000.0f);
    settings.swimSpeed = FiniteClamp(settings.swimSpeed, 3.5f, 0.0f, 1000.0f);
    settings.swimVerticalSpeed = FiniteClamp(settings.swimVerticalSpeed, 2.5f, 0.0f, 1000.0f);
    settings.lookSensitivity = FiniteClamp(settings.lookSensitivity, 0.1f, 0.001f, 10.0f);
    settings.capsuleRadius = FiniteClamp(settings.capsuleRadius, 0.4f, 0.01f, 100.0f);
    settings.capsuleHeight = FiniteClamp(settings.capsuleHeight, 1.8f,
        settings.capsuleRadius * 2.0f, 200.0f);
    settings.crouchedHeight = FiniteClamp(settings.crouchedHeight, 1.1f,
        settings.capsuleRadius * 2.0f, settings.capsuleHeight);
    settings.eyeHeight = FiniteClamp(settings.eyeHeight, 0.6f, 0.0f, settings.capsuleHeight);
    settings.cameraDistance = FiniteClamp(settings.cameraDistance, 5.0f, 0.0f, 10000.0f);
    settings.cameraTargetHeight = FiniteClamp(settings.cameraTargetHeight, 1.0f,
        -1000.0f, 1000.0f);
    settings.isometricPitch = FiniteClamp(settings.isometricPitch, -35.0f, -89.0f, 89.0f);
    settings.isometricDistance = FiniteClamp(settings.isometricDistance, 12.0f, 0.0f, 10000.0f);
    settings.cameraProbeRadius = FiniteClamp(settings.cameraProbeRadius, 0.2f, 0.0f, 100.0f);
    settings.cameraCollisionPadding = FiniteClamp(settings.cameraCollisionPadding, 0.08f, 0.0f, 100.0f);
    settings.cameraReturnSpeed = FiniteClamp(settings.cameraReturnSpeed, 8.0f, 0.0f, 1000.0f);
    settings.shoulderOffset = FiniteClamp(settings.shoulderOffset, 0.65f, 0.0f, 100.0f);
    settings.shoulderSwitchSpeed = FiniteClamp(settings.shoulderSwitchSpeed, 12.0f, 0.0f, 1000.0f);
    settings.lockOnRange = FiniteClamp(settings.lockOnRange, 18.0f, 0.0f, 10000.0f);
    settings.lockOnViewAngle = FiniteClamp(settings.lockOnViewAngle, 55.0f, 0.0f, 180.0f);
    settings.lockOnTargetHeight = FiniteClamp(settings.lockOnTargetHeight, 1.0f, -1000.0f, 1000.0f);
    settings.lockOnTrackingSpeed = FiniteClamp(settings.lockOnTrackingSpeed, 10.0f, 0.0f, 1000.0f);
    settings.maxSlopeDegrees = FiniteClamp(settings.maxSlopeDegrees, 50.0f, 0.0f, 89.0f);
    settings.stepHeight = FiniteClamp(settings.stepHeight, 0.35f, 0.0f, 100.0f);
}

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
    if (value == "Empty") {
        *primitive = EditorScene::Primitive::Empty;
        return true;
    }
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

// Space-safe string field: quoted so values with spaces (asset paths, animation
// clip names, etc.) survive the whitespace-tokenised reader. Empty -> quoted "-"
// (Load clears it back to empty). Reading with std::quoted stays backward
// compatible with older unquoted scenes for space-free single-word values.
std::string StoredPath(const std::string& path) {
    std::ostringstream out;
    out << std::quoted(path.empty() ? std::string("-") : path);
    return out.str();
}

std::string TrimHierarchyName(std::string value) {
    const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

} // namespace

void EditorScene::BuildDefault(const engine::Mesh &, const engine::Mesh & plane, const engine::Mesh &,
                               const engine::Mesh &, const engine::Mesh &, const engine::Mesh &,
                               const engine::Mesh &, const engine::Mesh &, const engine::Mesh &)
{
    Clear();

    Transform ground;
    ground.position = glm::vec3(0.0f);
    ground.scale = glm::vec3(8.0f, 1.0f, 8.0f);
    CreateObject("Ground", Primitive::Plane, plane, ground, glm::vec3(0.34f, 0.37f, 0.41f));

    m_selectedIndex = 0;
    m_dirty = false;
    ClearHistory();
}

bool EditorScene::Save(const std::string & path, std::string * error, bool markClean)
{
    if (!m_assetId.Valid()) m_assetId = engine::AssetHandle::Generate();
    const std::string contentRoot = engine::FindContentRootForAsset(path);
    engine::AssetRegistry assetRegistry;
    std::string registryError;
    const bool haveRegistry = !contentRoot.empty()
        && assetRegistry.Load(
            engine::AssetRegistry::DefaultRegistryPath(contentRoot),
            &registryError);
    if (haveRegistry) {
        if (m_environment.hudAsset.empty())
            m_environment.hudAssetId = {};
        const engine::AssetHandle hudId = engine::MakeAssetReference(
            &assetRegistry, contentRoot, m_environment.hudAsset,
            engine::AssetType::Hud).id;
        if (hudId.Valid()) m_environment.hudAssetId = hudId;
        if (m_environment.hudAssetId.Valid()) {
            const std::string resolved = engine::ResolveAssetReference(
                &assetRegistry, contentRoot,
                {m_environment.hudAssetId, m_environment.hudAsset},
                engine::AssetType::Hud);
            if (!resolved.empty()) m_environment.hudAsset = resolved;
        }
        for (Environment::PostProcessEffect& effect :
             m_environment.postProcessEffects) {
            const engine::AssetHandle current = engine::MakeAssetReference(
                &assetRegistry, contentRoot, effect.shaderPath,
                engine::AssetType::Shader).id;
            if (current.Valid()) effect.shaderAssetId = current;
            if (effect.shaderAssetId.Valid()) {
                const std::string resolved = engine::ResolveAssetReference(
                    &assetRegistry, contentRoot,
                    {effect.shaderAssetId, effect.shaderPath},
                    engine::AssetType::Shader);
                if (!resolved.empty()) effect.shaderPath = resolved;
            }
        }
        for (Object& object : m_objects) {
            const auto captureParticleReference =
                [&](std::string& assetPath,
                    engine::AssetType type,
                    engine::AssetHandle& id) {
                    if (assetPath.empty()) {
                        id = {};
                        return;
                    }
                    const engine::AssetHandle current =
                        engine::MakeAssetReference(
                            &assetRegistry, contentRoot, assetPath, type).id;
                    if (current.Valid()) id = current;
                    if (id.Valid()) {
                        const std::string resolved =
                            engine::ResolveAssetReference(
                                &assetRegistry, contentRoot,
                                {id, assetPath}, type);
                        if (!resolved.empty()) assetPath = resolved;
                    }
                };
            captureParticleReference(
                object.particleAssetPath, engine::AssetType::Particle,
                object.particleAssetId);
            captureParticleReference(
                object.particleConfig.texturePath, engine::AssetType::Texture,
                object.particleConfig.textureAssetId);
            captureParticleReference(
                object.particleConfig.meshPath, engine::AssetType::StaticMesh,
                object.particleConfig.meshAssetId);
            captureParticleReference(
                object.particleConfig.shaderPath, engine::AssetType::Shader,
                object.particleConfig.shaderAssetId);
            for (engine::ParticleEffectLayer& layer :
                 object.particleEffectLayers)
                captureParticleReference(
                    layer.assetPath, engine::AssetType::Particle,
                    layer.assetId);
            captureParticleReference(
                object.audioAssetPath, engine::AssetType::Audio,
                object.audioAssetId);
            captureParticleReference(
                object.navAgentBrainAsset,
                engine::AssetType::BehaviorTree,
                object.navAgentBrainAssetId);
            captureParticleReference(
                object.foliageAssetPath,
                engine::AssetType::Foliage,
                object.foliageAssetId);
            const engine::AssetType modelType = object.skeletalModel
                ? engine::AssetType::SkeletalMesh
                : engine::AssetType::StaticMesh;
            const engine::AssetHandle currentModel =
                engine::MakeAssetReference(
                    &assetRegistry, contentRoot, object.modelAssetPath,
                    modelType).id;
            if (currentModel.Valid()) object.modelAssetId = currentModel;
            if (object.modelAssetId.Valid()) {
                const std::string resolved = engine::ResolveAssetReference(
                    &assetRegistry, contentRoot,
                    {object.modelAssetId, object.modelAssetPath}, modelType);
                if (!resolved.empty()) object.modelAssetPath = resolved;
            }
            engine::AssetReference currentMaterial =
                engine::MakeAssetReference(
                    &assetRegistry, contentRoot, object.materialAssetPath,
                    engine::AssetType::Material);
            if (!currentMaterial.id.Valid())
                currentMaterial = engine::MakeAssetReference(
                    &assetRegistry, contentRoot, object.materialAssetPath,
                    engine::AssetType::Texture);
            if (currentMaterial.id.Valid())
                object.materialAssetId = currentMaterial.id;
            if (object.materialAssetId.Valid()) {
                const engine::AssetRegistryEntry* materialEntry =
                    assetRegistry.Find(object.materialAssetId);
                const std::string resolved = engine::ResolveAssetReference(
                    &assetRegistry, contentRoot,
                    {object.materialAssetId, object.materialAssetPath},
                    materialEntry
                            && materialEntry->type == engine::AssetType::Texture
                        ? engine::AssetType::Texture
                        : engine::AssetType::Material);
                if (!resolved.empty()) object.materialAssetPath = resolved;
            }
            const engine::AssetHandle currentCharacter =
                engine::MakeAssetReference(
                    &assetRegistry, contentRoot, object.characterAssetPath,
                    engine::AssetType::Character).id;
            if (currentCharacter.Valid())
                object.characterAssetId = currentCharacter;
            for (AnimationSource& source : object.animationSources) {
                const engine::AssetHandle current =
                    engine::MakeAssetReference(
                        &assetRegistry, contentRoot, source.file).id;
                if (current.Valid()) source.assetId = current;
                if (source.assetId.Valid()) {
                    const std::string resolved = engine::ResolveAssetReference(
                        &assetRegistry, contentRoot,
                        {source.assetId, source.file});
                    if (!resolved.empty()) source.file = resolved;
                }
            }
            for (ModelAttachment& attachment : object.modelAttachments) {
                const engine::AssetHandle current =
                    engine::MakeAssetReference(
                        &assetRegistry, contentRoot, attachment.modelPath,
                        engine::AssetType::StaticMesh).id;
                if (current.Valid()) attachment.modelAssetId = current;
                if (attachment.modelAssetId.Valid()) {
                    const std::string resolved = engine::ResolveAssetReference(
                        &assetRegistry, contentRoot,
                        {attachment.modelAssetId, attachment.modelPath},
                        engine::AssetType::StaticMesh);
                    if (!resolved.empty()) attachment.modelPath = resolved;
                }
                const engine::AssetHandle attachmentMaterial =
                    engine::MakeAssetReference(
                        &assetRegistry, contentRoot, attachment.materialPath,
                        engine::AssetType::Material).id;
                if (attachmentMaterial.Valid())
                    attachment.materialAssetId = attachmentMaterial;
            }
        }
    }
    std::ofstream out(path);
    if (!out) {
        if (error) *error = "Could not open scene file for writing.";
        return false;
    }

    out << "3DGEditorScene 144 " << m_assetId.ToString() << '\n';
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
        << (m_environment.hudAsset.empty() ? std::string("~") : m_environment.hudAsset) << ' '
        << m_environment.shadowDistance << ' '
        << (m_environment.hudAssetId.Valid()
                ? m_environment.hudAssetId.ToString() : std::string("-"))
        << '\n';
    out << "game_mode "
        << std::quoted(m_gameMode.playerObjectName.empty()
            ? std::string("-") : m_gameMode.playerObjectName) << ' '
        << m_gameMode.playerInputEnabled << ' '
        << m_gameMode.startPaused << ' '
        << m_gameMode.allowPause << ' '
        << m_gameMode.allowRestart << ' '
        << m_gameMode.loseOnPlayerDeath << ' '
        << m_gameMode.initialScore << ' '
        << m_gameMode.cameraOverride << ' '
        << std::clamp(m_gameMode.cameraMode, 0, 3) << '\n';
    out << "clouds "
        << (m_environment.clouds ? 1 : 0) << ' '
        << m_environment.cloudCoverage << ' '
        << m_environment.cloudDensity << ' '
        << m_environment.cloudScale << ' '
        << m_environment.cloudSoftness << ' '
        << m_environment.cloudWindSpeed << ' '
        << m_environment.cloudWindDirection << ' '
        << m_environment.cloudHorizonHeight << ' '
        << m_environment.cloudColor.r << ' '
        << m_environment.cloudColor.g << ' '
        << m_environment.cloudColor.b << ' '
        << (m_environment.cloudShadows ? 1 : 0) << ' '
        << m_environment.cloudShadowStrength << ' '
        << m_environment.cloudShadowScale << '\n';
    out << "atmosphere "
        << m_environment.atmosphereRayleigh << ' ' << m_environment.atmosphereRayleighHeight << ' '
        << m_environment.atmosphereMie << ' ' << m_environment.atmosphereMieHeight << ' '
        << m_environment.atmosphereMieAnisotropy << ' ' << m_environment.atmosphereOzone << ' '
        << m_environment.atmosphereIntensity << ' ' << m_environment.sunAngularDiameter << ' '
        << m_environment.sunDiskIntensity << '\n';
    out << "night_environment " << m_environment.stars << ' ' << m_environment.starIntensity << ' '
        << m_environment.moon << ' ' << m_environment.moonColor.r << ' '
        << m_environment.moonColor.g << ' ' << m_environment.moonColor.b << ' '
        << m_environment.moonIntensity << ' ' << m_environment.moonAngularDiameter << ' '
        << m_environment.moonPhase << '\n';
    out << "night_energy " << m_environment.dayEnvironmentIntensity << ' '
        << m_environment.twilightEnvironmentIntensity << ' '
        << m_environment.nightEnvironmentIntensity << ' '
        << m_environment.moonGiContribution << ' '
        << m_environment.nightReflectionIntensity << ' '
        << m_environment.nightFogScattering << ' '
        << m_environment.nightCloudAmbient << '\n';
    out << "night_exposure " << m_environment.preserveNightDarkness << ' '
        << m_environment.nightExposureLimitEV << '\n';
    out << "volumetrics " << m_environment.volumetricFog << ' '
        << m_environment.volumetricScattering << ' ' << m_environment.volumetricExtinction << ' '
        << m_environment.volumetricAnisotropy << ' ' << m_environment.volumetricStartDistance << ' '
        << m_environment.volumetricMaxDistance << ' ' << m_environment.environmentQuality << '\n';
    out << "presentation " << m_environment.autoExposure << ' '
        << m_environment.exposureMinEV << ' ' << m_environment.exposureMaxEV << ' '
        << m_environment.exposureCompensationEV << ' ' << m_environment.exposureSpeedUp << ' '
        << m_environment.exposureSpeedDown << ' ' << m_environment.bloom << ' '
        << m_environment.bloomThreshold << ' ' << m_environment.bloomKnee << ' '
        << m_environment.bloomStrength << ' ' << m_environment.colorTemperature << ' '
        << m_environment.colorTint << ' ' << m_environment.colorSaturation << ' '
        << m_environment.colorContrast << ' ' << m_environment.colorLift.r << ' '
        << m_environment.colorLift.g << ' ' << m_environment.colorLift.b << ' '
        << m_environment.colorGamma.r << ' ' << m_environment.colorGamma.g << ' '
        << m_environment.colorGamma.b << ' ' << m_environment.colorGain.r << ' '
        << m_environment.colorGain.g << ' ' << m_environment.colorGain.b << ' '
        << m_environment.colorLutIntensity << ' '
        << std::quoted(m_environment.colorLutPath.empty() ? std::string("-") : m_environment.colorLutPath) << '\n';
    out << "skylight_occlusion "
        << (m_environment.skylightOcclusion ? 1 : 0) << ' '
        << m_environment.skylightOcclusionStrength << ' '
        << m_environment.minimumSkylight << '\n';
    out << "lighting_tuning " << m_environment.exposureEV << ' '
        << m_environment.specularOcclusionStrength << ' '
        << m_environment.localProbeInfluence << ' '
        << m_environment.lightingDebugMode << '\n';
    out << "lighting_build " << std::quoted(m_environment.lightingBuildAsset) << ' '
        << m_environment.lightingBuildHash << ' ' << m_environment.lightingBuildQuality << ' '
        << m_environment.lightingProbeSpacing << ' ' << m_environment.lightingRayDistance << ' '
        << m_environment.lightingIndirectBounceStrength << ' '
        << (m_environment.lightingIndirectBounceEnabled?1:0) << ' '
        << m_environment.lightingEmissiveContribution << ' '
        << m_environment.lightingIndirectSaturation << ' '
        << m_environment.lightingDiffuseBounces << ' '
        << m_environment.lightingRaysPerProbe << ' '
        << m_environment.lightingUseMaterialTextures << ' '
        << m_environment.lightingIncludeStaticLocalLights << ' '
        << m_environment.lightingIncludeEmissive << ' '
        << m_environment.lightingEnergyThreshold << '\n';
    out << "dynamic_gi " << m_environment.dynamicGiEnabled << ' '
        << m_environment.dynamicGiQuality << ' ' << m_environment.dynamicGiProbeSpacing << ' '
        << m_environment.dynamicGiRaysPerProbe << ' ' << m_environment.dynamicGiProbesPerFrame << ' '
        << m_environment.dynamicGiMaxRaysPerFrame << ' ' << m_environment.dynamicGiMaxRayDistance << ' '
        << m_environment.dynamicGiHysteresis << ' ' << m_environment.dynamicGiIntensity << ' '
        << m_environment.dynamicGiRelocation << ' ' << m_environment.dynamicGiClassification << ' '
        << m_environment.dynamicGiVisibilityWeighting << ' '
        << m_environment.dynamicGiMultiBounce << ' '
        << m_environment.dynamicGiMultiBounceStrength << '\n';
    out << "ssgi " << m_environment.ssgiEnabled << ' ' << m_environment.ssgiRayLength << ' '
        << m_environment.ssgiSteps << ' ' << m_environment.ssgiThickness << ' '
        << m_environment.ssgiIntensity << '\n';
    out << "sky "
        << m_environment.skyMode << ' '
        << StoredPath(m_environment.skyTexturePath) << ' '
        << m_environment.skyRotation << ' '
        << m_environment.skyIntensity << ' '
        << (m_environment.skyTextureId.Valid()
                ? m_environment.skyTextureId.ToString() : std::string("-")) << '\n';
    out << "day_night_timeline " << std::quoted(m_environment.dayNightTimelinePath) << ' '
        << (m_environment.dayNightTimelineId.Valid()
                ? m_environment.dayNightTimelineId.ToString() : std::string("-")) << ' '
        << m_environment.dayNightTimelineAutoplay << '\n';
    for (const Environment::PostProcessEffect& effect :
         m_environment.postProcessEffects) {
        out << "post_effect "
            << std::quoted(effect.shaderPath) << ' '
            << (effect.shaderAssetId.Valid()
                    ? effect.shaderAssetId.ToString() : std::string("-")) << ' '
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
    for (const ViewportBookmark& bookmark : m_viewportBookmarks) {
        out << "viewport_bookmark " << std::quoted(bookmark.name) << ' '
            << bookmark.position.x << ' ' << bookmark.position.y << ' ' << bookmark.position.z << ' '
            << bookmark.target.x << ' ' << bookmark.target.y << ' ' << bookmark.target.z << ' '
            << bookmark.fov << ' ' << bookmark.blendDuration << '\n';
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
                << StoredPath(object.name) << ' '
                << LightTypeName(data.type) << ' '
                << transform->position.x << ' ' << transform->position.y << ' ' << transform->position.z << ' '
                << data.color.r << ' ' << data.color.g << ' ' << data.color.b << ' '
                << data.intensity << ' '
                << data.direction.x << ' ' << data.direction.y << ' ' << data.direction.z << ' '
                << data.innerAngle << ' ' << data.outerAngle << ' ' << data.range << ' ' << data.sourceRadius << ' '
                << static_cast<int>(data.areaShape) << ' ' << data.areaWidth << ' ' << data.areaHeight << ' ' << (data.areaTwoSided?1:0) << ' '
                << (data.affectDynamicGi?1:0) << ' '
                << (data.affectVolumetricFog?1:0) << ' ' << data.volumetricPriority << ' '
                << (object.visible ? 1 : 0) << ' '
                << (object.locked ? 1 : 0) << '\n';
            continue;
        }

        out << "object "
            << PrimitiveName(object.primitive) << ' '
            << StoredPath(object.name) << ' '
            << transform->position.x << ' ' << transform->position.y << ' ' << transform->position.z << ' '
            << transform->scale.x << ' ' << transform->scale.y << ' ' << transform->scale.z << ' '
            << transform->rotation.w << ' ' << transform->rotation.x << ' '
            << transform->rotation.y << ' ' << transform->rotation.z << ' '
            << renderer->color.r << ' ' << renderer->color.g << ' ' << renderer->color.b << ' '
            << (object.visible ? 1 : 0) << ' '
            << (object.locked ? 1 : 0) << ' '
            << StoredPath(object.modelAssetPath) << ' '
            << StoredPath(object.materialAssetPath) << ' '
            << (object.modelAssetId.Valid()
                ? object.modelAssetId.ToString() : std::string("-")) << ' '
            << (object.materialAssetId.Valid()
                ? object.materialAssetId.ToString() : std::string("-")) << ' '
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
        for (const AnimationEvent& event : object.animationEvents) {
            out << event.clipIndex << ' '
                << event.time << ' '
                << StoredPath(event.name) << ' '
                << StoredPath(event.clipName) << ' ';
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
        for (const AnimationSource& source : object.animationSources) {
            out << StoredPath(source.file) << ' '
                << (source.assetId.Valid()
                    ? source.assetId.ToString() : std::string("-")) << ' '
                << StoredPath(source.clipName) << ' '
                << (source.stripRootMotion ? 1 : 0) << ' '
                << StoredPath(source.sourceClipName) << ' '
                << source.basePlaybackSpeed << ' ';
        }
        out << object.modelAttachments.size() << ' ';
        for (const ModelAttachment& a : object.modelAttachments) {
            out << StoredPath(a.modelPath) << ' '
                << (a.modelAssetId.Valid()
                    ? a.modelAssetId.ToString() : std::string("-")) << ' '
                << (a.materialAssetId.Valid()
                    ? a.materialAssetId.ToString() : std::string("-")) << ' '
                << StoredPath(a.boneName) << ' '
                << a.position.x << ' ' << a.position.y << ' ' << a.position.z << ' '
                << a.eulerDegrees.x << ' ' << a.eulerDegrees.y << ' ' << a.eulerDegrees.z << ' '
                << a.scale.x << ' ' << a.scale.y << ' ' << a.scale.z << ' '
                << StoredPath(a.materialPath) << ' '
                << StoredPath(a.socketName) << ' ';
        }
        // Foot IK (3DGEditorScene >= 120).
        out << (object.footIK.enabled ? 1 : 0) << ' '
            << object.footIK.traceUp << ' ' << object.footIK.traceDown << ' '
            << object.footIK.footHeight << ' ' << object.footIK.pelvisWeight << ' '
            << object.footIK.maxPelvisDrop << ' ' << object.footIK.weight << ' ';
        out << StoredPath(object.characterAssetPath) << ' '
            << (object.characterAssetId.Valid()
                ? object.characterAssetId.ToString() : std::string("-")) << ' ';
        out << StoredPath(object.prefabAssetPath) << ' '
            << (object.prefabAssetId.Valid()
                ? object.prefabAssetId.ToString() : std::string("-")) << ' ';
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
            << object.playerController.facingMode << ' '
            << object.playerController.turnSpeed << ' '
            << object.playerController.cameraMode << ' '
            << object.playerController.isometricYaw << ' '
            << object.playerController.isometricPitch << ' '
            << object.playerController.isometricDistance << ' '
            << object.playerController.crouchSpeed << ' '
            << object.playerController.crouchedHeight << ' '
            << object.playerController.swimSpeed << ' '
            << object.playerController.swimVerticalSpeed << ' '
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
                << StoredPath(field.value) << ' '
                << field.minValue << ' ' << field.maxValue << ' '
                << StoredPath(field.tooltip) << ' '
                << StoredPath(field.group);
        }
        out << ' ' << object.scriptExecutionOrder
            << ' ' << object.scriptDependencies.size();
        for (const std::string& dependency : object.scriptDependencies)
            out << ' ' << StoredPath(dependency);
        out << ' ' << object.additionalScripts.size();
        for (const ScriptBinding& script : object.additionalScripts) {
            out << ' ' << (script.enabled ? 1 : 0)
                << ' ' << StoredPath(script.className)
                << ' ' << StoredPath(script.path)
                << ' ' << script.fields.size();
            for (const ScriptField& field : script.fields) {
                out << ' ' << StoredPath(field.name)
                    << ' ' << static_cast<int>(field.type)
                    << ' ' << StoredPath(field.value)
                    << ' ' << field.minValue << ' ' << field.maxValue
                    << ' ' << StoredPath(field.tooltip)
                    << ' ' << StoredPath(field.group);
            }
            out << ' ' << script.executionOrder
                << ' ' << script.dependencies.size();
            for (const std::string& dependency : script.dependencies)
                out << ' ' << StoredPath(dependency);
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
        out << ' ' << particleIdText(object.particleAssetId)
            << ' ' << particleIdText(particle.textureAssetId)
            << ' ' << particleIdText(particle.meshAssetId)
            << ' ' << particleIdText(particle.shaderAssetId)
            << ' ' << object.particleEffectLayers.size();
        for (const engine::ParticleEffectLayer& layer :
             object.particleEffectLayers)
            out << ' ' << particleIdText(layer.assetId);
        out << ' ' << particleIdText(object.audioAssetId)
            << ' ' << particleIdText(object.navAgentBrainAssetId);
        // NavAgent hearing range (scene version 110+).
        out << ' ' << object.navAgentHearingRange;
        // NavAgent squad coordination tuning (scene version 111+).
        out << ' ' << object.navAgentSquadAlertRadius
            << ' ' << object.navAgentSquadForgetTime;
        // Platformer camera axis (scene version 112+).
        out << ' ' << object.playerController.platformerYaw;
        out << '\n';
    }

    // Editor-only hierarchy organization. Runtime export intentionally ignores these.
    for (const SceneGroup& group : m_groups) {
        out << "scene_group " << group.id << ' ' << group.parentId << ' '
            << (group.expanded ? 1 : 0) << ' ' << StoredPath(group.name) << '\n';
    }
    for (const Object& object : m_objects) {
        if (object.editorGroupId != kRootGroupId)
            out << "object_group " << StoredPath(object.name) << ' '
                << object.editorGroupId << '\n';
    }

    // Local reflection captures are separate, versioned component records so the
    // main object record stays backward compatible as probe authoring evolves.
    for (Object& object : m_objects) {
        if (!object.reflectionProbeEnabled) continue;
        engine::ecs::ReflectionProbe probe = object.reflectionProbe;
        if (const auto* component =
                m_registry.TryGet<engine::ecs::ReflectionProbe>(object.entity))
            probe = *component;
        if (!probe.stableId.Valid()) {
            probe.stableId = engine::AssetHandle::Generate();
            object.reflectionProbe = probe;
            if (auto* component =
                    m_registry.TryGet<engine::ecs::ReflectionProbe>(object.entity))
                *component = probe;
        }
        out << "reflection_probe " << std::quoted(object.name) << ' '
            << probe.stableId.ToString() << ' '
            << static_cast<int>(probe.shape) << ' '
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
    for (const Object& object : m_objects) {
        if (object.postProcessVolumeEnabled) {
            const auto& v=object.postProcessVolume;
            out<<"post_process_volume "<<std::quoted(object.name)<<' '<<v.enabled<<' '<<v.unbound<<' '
               <<v.priority<<' '<<v.blendDistance<<' '<<v.blendWeight<<' '
               <<v.boxExtents.x<<' '<<v.boxExtents.y<<' '<<v.boxExtents.z<<' '
               <<v.overrideExposure<<' '<<v.exposureCompensationEV<<' '
               <<v.overrideBloom<<' '<<v.bloomStrength<<' '
               <<v.overrideColorGrading<<' '<<v.temperature<<' '<<v.tint<<' '<<v.saturation<<' '<<v.contrast<<' '
               <<v.overrideFogDensity<<' '<<v.fogDensity<<' '
               <<(v.stableId.Valid()?v.stableId.ToString():std::string("-"))<<'\n';
        }
        if (object.localFogVolumeEnabled) {
            const auto& v=object.localFogVolume;
            out<<"local_fog_volume "<<std::quoted(object.name)<<' '<<v.enabled<<' '<<static_cast<int>(v.shape)<<' '
               <<v.boxExtents.x<<' '<<v.boxExtents.y<<' '<<v.boxExtents.z<<' '<<v.radius<<' '
               <<v.blendDistance<<' '<<v.density<<' '<<v.albedo.x<<' '<<v.albedo.y<<' '<<v.albedo.z<<' '
               <<v.extinction<<' '<<v.anisotropy<<' '
               <<(v.stableId.Valid()?v.stableId.ToString():std::string("-"))<<'\n';
        }
    }

    // AI movement authoring is stored separately so older object records remain
    // readable and the component can grow without destabilizing the main line.
    for (const Object& object : m_objects) {
        if (!object.navAgentEnabled) continue;
        out << "ai_movement " << std::quoted(object.name) << ' '
            << static_cast<int>(object.navMovementMode) << ' '
            << object.navMovementGravity << ' '
            << object.navMovementMaxFallSpeed << ' '
            << object.navMovementGroundProbe << ' '
            << object.navMovementStepHeight << ' '
            << object.navMovementMaxSlope << '\n';
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

    // Per-layer painted materials (scene version 79+). Own line keyed by object name.
    for (const Object& object : m_objects) {
        if (!object.isTerrain) continue;
        bool anyMaterial = false;
        for (const std::string& m : object.terrainLayerMaterials) if (!m.empty()) anyMaterial = true;
        if (!anyMaterial) continue;
        out << "terrain_layers " << object.name;
        for (const std::string& m : object.terrainLayerMaterials)
            out << ' ' << (m.empty() ? std::string("-") : m);
        out << '\n';
    }

    // Grass settings (scene version 80+). Own line keyed by object name.
    for (const Object& object : m_objects) {
        if (!object.isTerrain || !object.grassEnabled) continue;
        out << "terrain_grass " << object.name << ' '
            << object.grassDensity << ' ' << object.grassHeight << ' '
            << object.grassWindStrength << ' ' << object.grassWindSpeed << ' '
            << object.grassBaseColor.r << ' ' << object.grassBaseColor.g << ' ' << object.grassBaseColor.b << ' '
            << object.grassTipColor.r << ' ' << object.grassTipColor.g << ' ' << object.grassTipColor.b << '\n';
    }

    // Optional grass height-only randomization (scene version 118+).
    for (const Object& object : m_objects) {
        if (!object.isTerrain || !object.grassRandomizeHeight) continue;
        out << "terrain_grass_height_random " << object.name << ' '
            << 1 << ' ' << object.grassMinHeightScale << ' '
            << object.grassMaxHeightScale << '\n';
    }

    // Foliage actors and their lightweight instance transforms (scene version 119+).
    for (const Object& object : m_objects) {
        if (!object.isFoliage) continue;
        out << "foliage " << std::quoted(object.name) << ' '
            << std::quoted(object.foliageAssetPath.empty()
                ? std::string("-") : object.foliageAssetPath) << ' '
            << (object.foliageAssetId.Valid()
                ? object.foliageAssetId.ToString() : std::string("-")) << ' '
            << object.nextFoliageInstanceId << ' '
            << object.foliageInstances.size();
        for (const engine::ecs::FoliageInstance& instance : object.foliageInstances) {
            out << ' ' << instance.id << ' ' << instance.typeIndex
                << ' ' << instance.position.x << ' ' << instance.position.y << ' ' << instance.position.z
                << ' ' << instance.rotationDegrees.x << ' ' << instance.rotationDegrees.y
                << ' ' << instance.rotationDegrees.z
                << ' ' << instance.scale.x << ' ' << instance.scale.y << ' ' << instance.scale.z
                << ' ' << (instance.enabled ? 1 : 0);
        }
        // Owning terrain name (scene version 121+); "-" when free-standing.
        out << ' ' << std::quoted(object.foliageTerrainOwner.empty()
                ? std::string("-") : object.foliageTerrainOwner);
        out << '\n';
    }

    // Per-region grass style palette + per-vertex slot map (scene version 80+).
    for (const Object& object : m_objects) {
        if (!object.isTerrain) continue;
        if (object.terrainGrassPalette.empty() && object.terrainGrassStyle.empty()) continue;
        out << "terrain_grass_paint " << object.name << ' ' << object.terrainGrassPalette.size();
        for (const Object::GrassStyleEntry& e : object.terrainGrassPalette) {
            out << ' ' << e.density << ' ' << e.height << ' '
                << e.base.r << ' ' << e.base.g << ' ' << e.base.b << ' '
                << e.tip.r << ' ' << e.tip.g << ' ' << e.tip.b;
        }
        out << ' ' << object.terrainGrassStyle.size();
        for (unsigned char s : object.terrainGrassStyle) out << ' ' << static_cast<int>(s);
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
            << object.waterSpecular << ' ' << object.waterShininess << ' '
            << object.waterType << ' ' << object.waterSeaHeight << ' ' << object.waterSeaChoppy << ' '
            << object.waterSeaSpeed << ' ' << object.waterSeaFreq << ' ' << object.waterFoam << ' '
            << (object.waterFlowSpline.empty() ? "-" : object.waterFlowSpline) << ' '
            << object.waterDepthFadeDistance << ' '
            << object.waterShoreFoamWidth << ' '
            << object.waterShoreFoamStrength << ' '
            << object.waterRefractionStrength << ' '
            << object.waterReflectionRoughness << ' '
            << object.waterEnvironmentReflectionStrength << ' '
            << object.waterAbsorptionStrength << ' '
            << object.waterCausticsStrength << ' '
            << object.waterCausticsScale << ' '
            << object.waterMaxRenderDistance << ' '
            << object.waterUnderwaterTint.r << ' '
            << object.waterUnderwaterTint.g << ' '
            << object.waterUnderwaterTint.b << ' '
            << object.waterUnderwaterFogDensity << ' '
            << object.waterUnderwaterDistortion << ' '
            << object.waterUnderwaterTransitionSpeed << ' '
            << object.waterRiverWidth << ' '
            << StoredPath(object.waterShaderPath) << '\n';   // scene version 122+
    }

    // Spline paths (Catmull-Rom control points).
    for (const Object& object : m_objects) {
        if (!object.isSpline) continue;
        out << "spline " << object.name << ' '
            << (object.splineClosed ? 1 : 0) << ' ' << object.splineType << ' '
            << object.splinePoints.size();
        for (std::size_t i = 0; i < object.splinePoints.size(); ++i) {
            const glm::vec3& p = object.splinePoints[i];
            const glm::vec3 r = i < object.splinePointRotations.size()
                ? object.splinePointRotations[i] : glm::vec3(0.0f);
            out << ' ' << p.x << ' ' << p.y << ' ' << p.z
                << ' ' << r.x << ' ' << r.y << ' ' << r.z;
        }
        out << '\n';
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

    for (const Object& object : m_objects) {
        if (!object.ragdollEnabled) continue;
        out << "ragdoll " << std::quoted(object.name) << ' '
            << object.ragdoll.enabled << ' '
            << object.ragdoll.activateOnDeath << ' '
            << object.ragdoll.totalMass << ' '
            << object.ragdoll.bodyRadiusScale << ' '
            << object.ragdoll.linearDamping << ' '
            << object.ragdoll.angularDamping << ' '
            << object.ragdoll.deathImpulse << ' '
            << object.ragdoll.maxBodies << ' '
            << std::quoted(object.ragdoll.assetPath.empty()
                ? std::string("-") : object.ragdoll.assetPath) << '\n';
    }

    for (const Object& object : m_objects) {
        if (!object.decal) continue;
        out << "decal " << std::quoted(object.name) << ' '
            << object.decalOpacity << ' ' << object.decalSurfaceOffset << '\n';
    }

    // Editor layer membership is kept on separate keyed records so the runtime object
    // record remains compact and older scenes naturally fall back to Default.
    for (std::size_t objectIndex = 0; objectIndex < m_objects.size(); ++objectIndex) {
        const Object& object = m_objects[objectIndex];
        out << "editor_layer " << objectIndex << ' '
            << std::quoted(object.editorLayer.empty() ? std::string("Default")
                                                       : object.editorLayer)
            << '\n';
    }

    std::vector<engine::AssetHandle> dependencies;
    const auto addDependency = [&](engine::AssetHandle id) {
        if (id.Valid()
            && std::find(dependencies.begin(), dependencies.end(), id)
                   == dependencies.end())
            dependencies.push_back(id);
    };
    addDependency(m_environment.hudAssetId);
    for (const Object& object : m_objects) {
        addDependency(object.modelAssetId);
        addDependency(object.materialAssetId);
        addDependency(object.characterAssetId);
        addDependency(object.prefabAssetId);
        addDependency(object.particleAssetId);
        addDependency(object.particleConfig.textureAssetId);
        addDependency(object.particleConfig.meshAssetId);
        addDependency(object.particleConfig.shaderAssetId);
        addDependency(object.audioAssetId);
        addDependency(object.navAgentBrainAssetId);
        addDependency(object.reflectionProbe.bakedCubemapId);
        for (const engine::ParticleEffectLayer& layer :
             object.particleEffectLayers)
            addDependency(layer.assetId);
        if (haveRegistry) {
            engine::AssetReference materialReference =
                engine::MakeAssetReference(
                    &assetRegistry, contentRoot, object.materialAssetPath,
                    engine::AssetType::Material);
            if (!materialReference.id.Valid())
                materialReference = engine::MakeAssetReference(
                    &assetRegistry, contentRoot, object.materialAssetPath,
                    engine::AssetType::Texture);
            addDependency(materialReference.id);
        }
        for (const AnimationSource& source : object.animationSources)
            addDependency(source.assetId);
        for (const ModelAttachment& attachment : object.modelAttachments) {
            addDependency(attachment.modelAssetId);
            addDependency(attachment.materialAssetId);
            if (haveRegistry)
                addDependency(engine::MakeAssetReference(
                    &assetRegistry, contentRoot, attachment.materialPath,
                    engine::AssetType::Material).id);
        }
        if (haveRegistry) {
            for (const std::string& material : object.terrainLayerMaterials)
                addDependency(engine::MakeAssetReference(
                    &assetRegistry, contentRoot, material,
                    engine::AssetType::Material).id);
        }
    }
    for (const Environment::PostProcessEffect& effect :
         m_environment.postProcessEffects)
        addDependency(effect.shaderAssetId);
    out << "ASSET_DEPS " << dependencies.size();
    for (engine::AssetHandle id : dependencies) out << ' ' << id.ToString();
    out << '\n';

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
    engine::AssetHandle loadedAssetId;
    if (version >= 101) {
        std::string id;
        in >> id;
        if (!engine::AssetHandle::Parse(id, &loadedAssetId)) {
            if (error) *error = "Scene file has an invalid stable ID.";
            return false;
        }
    }
    if (magic != "3DGEditorScene" ||(version < 1 || version > 144)) {
        if (error) *error = "Scene file has an unknown format.";
        return false;
    }

    Clear();
    m_assetId = loadedAssetId;

    std::string recordType;
    bool resolvedDuplicateNames = false;
    while (in >> recordType) {
        if (recordType == "atmosphere" && version >= 140) {
            in >> m_environment.atmosphereRayleigh >> m_environment.atmosphereRayleighHeight
               >> m_environment.atmosphereMie >> m_environment.atmosphereMieHeight
               >> m_environment.atmosphereMieAnisotropy >> m_environment.atmosphereOzone
               >> m_environment.atmosphereIntensity >> m_environment.sunAngularDiameter
               >> m_environment.sunDiskIntensity;
            if (!in) { if (error) *error = "Scene contains invalid atmosphere settings."; Clear(); return false; }
            continue;
        }
        if (recordType == "night_environment" && version >= 140) {
            in >> m_environment.stars >> m_environment.starIntensity >> m_environment.moon
               >> m_environment.moonColor.r >> m_environment.moonColor.g >> m_environment.moonColor.b
               >> m_environment.moonIntensity >> m_environment.moonAngularDiameter
               >> m_environment.moonPhase;
            if (!in) { if (error) *error = "Scene contains invalid night environment settings."; Clear(); return false; }
            continue;
        }
        if (recordType == "night_energy" && version >= 143) {
            in >> m_environment.dayEnvironmentIntensity
               >> m_environment.twilightEnvironmentIntensity
               >> m_environment.nightEnvironmentIntensity
               >> m_environment.moonGiContribution
               >> m_environment.nightReflectionIntensity
               >> m_environment.nightFogScattering
               >> m_environment.nightCloudAmbient;
            if (!in) { if (error) *error = "Scene contains invalid night energy settings."; Clear(); return false; }
            continue;
        }
        if (recordType == "night_exposure" && version >= 143) {
            in >> m_environment.preserveNightDarkness
               >> m_environment.nightExposureLimitEV;
            if (!in) { if (error) *error = "Scene contains invalid night exposure settings."; Clear(); return false; }
            continue;
        }
        if (recordType == "volumetrics" && version >= 140) {
            in >> m_environment.volumetricFog >> m_environment.volumetricScattering
               >> m_environment.volumetricExtinction >> m_environment.volumetricAnisotropy
               >> m_environment.volumetricStartDistance >> m_environment.volumetricMaxDistance
               >> m_environment.environmentQuality;
            if (!in) { if (error) *error = "Scene contains invalid volumetric settings."; Clear(); return false; }
            continue;
        }
        if (recordType == "presentation" && version >= 140) {
            in >> m_environment.autoExposure >> m_environment.exposureMinEV
               >> m_environment.exposureMaxEV >> m_environment.exposureCompensationEV
               >> m_environment.exposureSpeedUp >> m_environment.exposureSpeedDown
               >> m_environment.bloom >> m_environment.bloomThreshold
               >> m_environment.bloomKnee >> m_environment.bloomStrength
               >> m_environment.colorTemperature >> m_environment.colorTint
               >> m_environment.colorSaturation >> m_environment.colorContrast
               >> m_environment.colorLift.r >> m_environment.colorLift.g >> m_environment.colorLift.b
               >> m_environment.colorGamma.r >> m_environment.colorGamma.g >> m_environment.colorGamma.b
               >> m_environment.colorGain.r >> m_environment.colorGain.g >> m_environment.colorGain.b
               >> m_environment.colorLutIntensity
               >> std::quoted(m_environment.colorLutPath);
            if (m_environment.colorLutPath == "-") m_environment.colorLutPath.clear();
            if (!in) { if (error) *error = "Scene contains invalid presentation settings."; Clear(); return false; }
            continue;
        }
        if (recordType == "reflection_probe" && version >= 137) {
            std::string objectName, stableIdText, cubemapPath, cubemapIdText;
            int shape = 0;
            engine::ecs::ReflectionProbe probe;
            in >> std::quoted(objectName) >> stableIdText >> shape
               >> probe.boxExtents.x >> probe.boxExtents.y >> probe.boxExtents.z
               >> probe.radius >> probe.blendDistance >> probe.intensity
               >> probe.priority >> probe.captureResolution
               >> probe.includeSky >> probe.enabled
               >> std::quoted(cubemapPath) >> cubemapIdText
               >> probe.captureSourceHash;
            probe.shape = shape == 1
                ? engine::ecs::ReflectionProbe::Shape::Sphere
                : engine::ecs::ReflectionProbe::Shape::Box;
            probe.bakedCubemapPath = cubemapPath == "-" ? std::string{} : cubemapPath;
            if (!engine::AssetHandle::Parse(stableIdText, &probe.stableId)
                || (cubemapIdText != "-"
                    && !engine::AssetHandle::Parse(cubemapIdText,
                                                   &probe.bakedCubemapId))) {
                if (error) *error = "Scene contains an invalid reflection probe ID.";
                Clear(); return false;
            }
            if (!in) {
                if (error) *error = "Scene contains invalid reflection probe data.";
                Clear(); return false;
            }
            for (Object& object : m_objects) {
                if (object.name != objectName) continue;
                object.reflectionProbeEnabled = true;
                object.reflectionProbe = probe;
                m_registry.Add<engine::ecs::ReflectionProbe>(object.entity, probe);
                break;
            }
            continue;
        }
        if(recordType=="post_process_volume"&&version>=141){
            std::string name,id;engine::ecs::PostProcessVolume v;
            in>>std::quoted(name)>>v.enabled>>v.unbound>>v.priority>>v.blendDistance>>v.blendWeight
              >>v.boxExtents.x>>v.boxExtents.y>>v.boxExtents.z
              >>v.overrideExposure>>v.exposureCompensationEV>>v.overrideBloom>>v.bloomStrength
              >>v.overrideColorGrading>>v.temperature>>v.tint>>v.saturation>>v.contrast
              >>v.overrideFogDensity>>v.fogDensity>>id;
            if(id!="-"&&!engine::AssetHandle::Parse(id,&v.stableId)){if(error)*error="Invalid post-process volume ID.";Clear();return false;}
            for(Object& object:m_objects)if(object.name==name){object.postProcessVolumeEnabled=true;object.postProcessVolume=v;m_registry.Add<engine::ecs::PostProcessVolume>(object.entity,v);break;}
            continue;
        }
        if(recordType=="local_fog_volume"&&version>=141){
            std::string name,id;int shape=0;engine::ecs::LocalFogVolume v;
            in>>std::quoted(name)>>v.enabled>>shape>>v.boxExtents.x>>v.boxExtents.y>>v.boxExtents.z>>v.radius
              >>v.blendDistance>>v.density>>v.albedo.x>>v.albedo.y>>v.albedo.z>>v.extinction>>v.anisotropy>>id;
            v.shape=shape==1?engine::ecs::LocalFogVolume::Shape::Sphere:engine::ecs::LocalFogVolume::Shape::Box;
            if(id!="-"&&!engine::AssetHandle::Parse(id,&v.stableId)){if(error)*error="Invalid local fog volume ID.";Clear();return false;}
            for(Object& object:m_objects)if(object.name==name){object.localFogVolumeEnabled=true;object.localFogVolume=v;m_registry.Add<engine::ecs::LocalFogVolume>(object.entity,v);break;}
            continue;
        }
        if (recordType == "scene_group" && version >= 133) {
            SceneGroup group;
            int expanded = 1;
            in >> group.id >> group.parentId >> expanded >> std::quoted(group.name);
            group.name = TrimHierarchyName(group.name == "-" ? std::string{} : group.name);
            if (!in || group.id == kRootGroupId || group.name.empty()) {
                if (error) *error = "Scene contains an invalid hierarchy group.";
                Clear(); return false;
            }
            const std::string storedName = group.name;
            group.name = MakeUniqueHierarchyName(group.name);
            resolvedDuplicateNames |= group.name != storedName;
            group.expanded = expanded != 0;
            m_nextGroupId = std::max(m_nextGroupId, group.id + 1);
            m_groups.push_back(std::move(group));
            continue;
        }
        if (recordType == "object_group" && version >= 133) {
            std::string objectName;
            GroupId groupId = kRootGroupId;
            in >> std::quoted(objectName) >> groupId;
            if (!in) { if (error) *error = "Scene contains invalid object grouping."; Clear(); return false; }
            for (Object& object : m_objects) {
                if (object.name == objectName) { object.editorGroupId = groupId; break; }
            }
            continue;
        }
        if (recordType == "day_night_timeline") {
            std::string id;
            in >> std::quoted(m_environment.dayNightTimelinePath) >> id
               >> m_environment.dayNightTimelineAutoplay;
            if (m_environment.dayNightTimelinePath == "-")
                m_environment.dayNightTimelinePath.clear();
            if (id != "-" && !engine::AssetHandle::Parse(id, &m_environment.dayNightTimelineId)) {
                if (error) *error = "Scene contains an invalid day/night timeline ID.";
                Clear(); return false;
            }
            continue;
        }
        if (recordType == "game_mode" && version >= 97) {
            in >> std::quoted(m_gameMode.playerObjectName)
               >> m_gameMode.playerInputEnabled
               >> m_gameMode.startPaused
               >> m_gameMode.allowPause
               >> m_gameMode.allowRestart
               >> m_gameMode.loseOnPlayerDeath
               >> m_gameMode.initialScore
               >> m_gameMode.cameraOverride
               >> m_gameMode.cameraMode;
            if (m_gameMode.playerObjectName == "-") m_gameMode.playerObjectName.clear();
            m_gameMode.cameraMode = std::clamp(m_gameMode.cameraMode, 0, 3);
            if (!in) {
                if (error) *error = "Scene file contains invalid Game Mode settings.";
                Clear();
                return false;
            }
            continue;
        }
        if (recordType == "skylight_occlusion" && version >= 83) {
            int enabled = 1;
            in >> enabled
               >> m_environment.skylightOcclusionStrength
               >> m_environment.minimumSkylight;
            if (!in) {
                if (error) *error = "Scene file contains invalid skylight occlusion settings.";
                Clear();
                return false;
            }
            m_environment.skylightOcclusion = enabled != 0;
            continue;
        }
        if (recordType == "lighting_tuning" && version >= 135) {
            in >> m_environment.exposureEV
               >> m_environment.specularOcclusionStrength
               >> m_environment.localProbeInfluence;
            if (version >= 136) in >> m_environment.lightingDebugMode;
            if (!in) { if (error) *error = "Scene file contains invalid lighting tuning settings."; Clear(); return false; }
            continue;
        }
        if (recordType == "lighting_build" && version >= 131) {
            in >> std::quoted(m_environment.lightingBuildAsset)
               >> m_environment.lightingBuildHash
               >> m_environment.lightingBuildQuality
               >> m_environment.lightingProbeSpacing
               >> m_environment.lightingRayDistance;
            if (version >= 136) in >> m_environment.lightingIndirectBounceStrength;
            if (version >= 138) in >> m_environment.lightingIndirectBounceEnabled
                                   >> m_environment.lightingEmissiveContribution
                                   >> m_environment.lightingIndirectSaturation;
            if (version >= 144) in >> m_environment.lightingDiffuseBounces
                                   >> m_environment.lightingRaysPerProbe
                                   >> m_environment.lightingUseMaterialTextures
                                   >> m_environment.lightingIncludeStaticLocalLights
                                   >> m_environment.lightingIncludeEmissive
                                   >> m_environment.lightingEnergyThreshold;
            if (!in) { if (error) *error = "Scene file contains invalid lighting build settings."; Clear(); return false; }
            continue;
        }
        if (recordType == "dynamic_gi" && version >= 139) {
            in >> m_environment.dynamicGiEnabled >> m_environment.dynamicGiQuality
               >> m_environment.dynamicGiProbeSpacing >> m_environment.dynamicGiRaysPerProbe
               >> m_environment.dynamicGiProbesPerFrame >> m_environment.dynamicGiMaxRaysPerFrame
               >> m_environment.dynamicGiMaxRayDistance >> m_environment.dynamicGiHysteresis
               >> m_environment.dynamicGiIntensity >> m_environment.dynamicGiRelocation
               >> m_environment.dynamicGiClassification >> m_environment.dynamicGiVisibilityWeighting;
            if (version >= 144) in >> m_environment.dynamicGiMultiBounce
                                   >> m_environment.dynamicGiMultiBounceStrength;
            if (!in) { if (error) *error = "Scene file contains invalid dynamic GI settings."; Clear(); return false; }
            continue;
        }
        if (recordType == "ssgi" && version >= 139) {
            in >> m_environment.ssgiEnabled >> m_environment.ssgiRayLength
               >> m_environment.ssgiSteps >> m_environment.ssgiThickness
               >> m_environment.ssgiIntensity;
            if (!in) { if (error) *error = "Scene file contains invalid SSGI settings."; Clear(); return false; }
            continue;
        }
        if (recordType == "sky" && version >= 123) {
            std::string texPath, texId;
            in >> m_environment.skyMode >> std::quoted(texPath)
               >> m_environment.skyRotation >> m_environment.skyIntensity >> texId;
            if (!in) {
                if (error) *error = "Scene file contains an invalid sky record.";
                Clear();
                return false;
            }
            m_environment.skyTexturePath = (texPath == "-") ? std::string() : texPath;
            if (texId != "-")
                engine::AssetHandle::Parse(texId, &m_environment.skyTextureId);
            continue;
        }
        if (recordType == "clouds" && version >= 81) {
            int enabled = 1;
            in >> enabled
               >> m_environment.cloudCoverage
               >> m_environment.cloudDensity
               >> m_environment.cloudScale
               >> m_environment.cloudSoftness
               >> m_environment.cloudWindSpeed
               >> m_environment.cloudWindDirection
               >> m_environment.cloudHorizonHeight
               >> m_environment.cloudColor.r
               >> m_environment.cloudColor.g
               >> m_environment.cloudColor.b;
            if (!in) {
                if (error) *error = "Scene file contains an invalid clouds record.";
                Clear();
                return false;
            }
            m_environment.clouds = enabled != 0;
            if (version >= 82) {
                int shadows = 1;
                in >> shadows
                   >> m_environment.cloudShadowStrength
                   >> m_environment.cloudShadowScale;
                m_environment.cloudShadows = shadows != 0;
            }
            if (!in) {
                if (error) *error = "Scene file contains invalid cloud shadow settings.";
                Clear();
                return false;
            }
            continue;
        }
        if (recordType == "post_effect" && version >= 74) {
            Environment::PostProcessEffect effect;
            int enabled = 1;
            std::size_t parameterCount = 0;
            in >> std::quoted(effect.shaderPath);
            if (version >= 103) {
                std::string shaderId;
                in >> shaderId;
                if (shaderId != "-"
                    && !engine::AssetHandle::Parse(
                        shaderId, &effect.shaderAssetId)) {
                    if (error) *error =
                        "Scene has an invalid post-process shader ID.";
                    Clear();
                    return false;
                }
            }
            in >> enabled >> parameterCount;
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
        if (recordType == "editor_layer" && version >= 124) {
            std::size_t objectIndex = 0;
            std::string layerName;
            in >> objectIndex >> std::quoted(layerName);
            if (objectIndex < m_objects.size())
                m_objects[objectIndex].editorLayer = layerName.empty() ? "Default" : layerName;
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
            if (version >= 84) {
                in >> m_environment.shadowDistance;
            }
            if (version >= 105) {
                std::string hudId;
                in >> hudId;
                if (hudId != "-"
                    && !engine::AssetHandle::Parse(
                        hudId, &m_environment.hudAssetId)) {
                    if (error) *error =
                        "Scene contains an invalid HUD asset ID.";
                    Clear();
                    return false;
                }
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
            m_environment.shadowDistance = std::clamp(
                m_environment.shadowDistance, 10.0f, 5000.0f);
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
        if (recordType == "viewport_bookmark" && version >= 125) {
            ViewportBookmark bookmark;
            in >> std::quoted(bookmark.name)
               >> bookmark.position.x >> bookmark.position.y >> bookmark.position.z
               >> bookmark.target.x >> bookmark.target.y >> bookmark.target.z
               >> bookmark.fov >> bookmark.blendDuration;
            if (!in) {
                if (error) *error = "Scene file contains an invalid viewport bookmark.";
                Clear();
                return false;
            }
            bookmark.name = bookmark.name.empty() ? "View" : bookmark.name;
            bookmark.fov = std::clamp(bookmark.fov, 10.0f, 120.0f);
            bookmark.blendDuration = std::clamp(bookmark.blendDuration, 0.0f, 5.0f);
            m_viewportBookmarks.push_back(std::move(bookmark));
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
            in >> std::quoted(name) >> lightTypeName
               >> transform.position.x >> transform.position.y >> transform.position.z
               >> light.color.r >> light.color.g >> light.color.b
               >> light.intensity
               >> light.direction.x >> light.direction.y >> light.direction.z
               >> light.innerAngle >> light.outerAngle >> light.range >> light.sourceRadius;
            if(version>=138){int areaShape=0,areaTwoSided=0;in>>areaShape>>light.areaWidth>>light.areaHeight>>areaTwoSided;
                light.areaShape=areaShape==1?Light::AreaShape::Rectangle:Light::AreaShape::Sphere;light.areaTwoSided=areaTwoSided!=0;}
            if(version>=139){int affectDynamicGi=1;in>>affectDynamicGi;light.affectDynamicGi=affectDynamicGi!=0;}
            if(version>=142){int affectVolumetric=1;in>>affectVolumetric>>light.volumetricPriority;light.affectVolumetricFog=affectVolumetric!=0;}
            in>>visible>>locked;

            if (!in || !ParseLightType(lightTypeName, &light.type)) {
                if (error) *error = "Scene file contains an invalid light record.";
                Clear();
                return false;
            }

            transform.scale = glm::vec3(0.22f);
            const glm::vec3 color = light.color * light.intensity;
            resolvedDuplicateNames |= !IsHierarchyNameAvailable(name);
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
               >> std::quoted(objectA)
               >> std::quoted(objectB)
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

        if (recordType == "terrain_layers" && version >= 79) {
            std::string name;
            in >> name;
            std::array<std::string, 5> mats{};
            for (std::string& m : mats) {
                std::string token;
                in >> token;
                m = (token == "-") ? std::string() : token;
            }
            for (Object& obj : m_objects) {
                if (obj.name == name) { obj.terrainLayerMaterials = mats; break; }
            }
            continue;
        }

        if (recordType == "terrain_grass" && version >= 80) {
            std::string name;
            float density = 2.0f, height = 0.6f, windStr = 0.18f, windSpd = 1.4f;
            glm::vec3 base(0.16f, 0.34f, 0.12f), tip(0.42f, 0.68f, 0.28f);
            in >> name >> density >> height >> windStr >> windSpd
               >> base.r >> base.g >> base.b >> tip.r >> tip.g >> tip.b;
            for (Object& obj : m_objects) {
                if (obj.name == name) {
                    obj.grassEnabled = true;
                    obj.grassDensity = density;
                    obj.grassHeight = height;
                    obj.grassWindStrength = windStr;
                    obj.grassWindSpeed = windSpd;
                    obj.grassBaseColor = base;
                    obj.grassTipColor = tip;
                    break;
                }
            }
            continue;
        }

        if (recordType == "terrain_grass_height_random" && version >= 118) {
            std::string name;
            int enabled = 0;
            float minScale = 0.75f, maxScale = 1.25f;
            in >> name >> enabled >> minScale >> maxScale;
            for (Object& obj : m_objects) {
                if (obj.name != name) continue;
                obj.grassRandomizeHeight = enabled != 0;
                obj.grassMinHeightScale = std::clamp(
                    std::min(minScale, maxScale), 0.05f, 4.0f);
                obj.grassMaxHeightScale = std::clamp(
                    std::max(minScale, maxScale), obj.grassMinHeightScale, 4.0f);
                break;
            }
            continue;
        }

        if (recordType == "terrain_grass_paint" && version >= 80) {
            std::string name;
            std::size_t pcount = 0;
            in >> name >> pcount;
            std::vector<Object::GrassStyleEntry> pal(pcount);
            for (Object::GrassStyleEntry& e : pal) {
                in >> e.density >> e.height >> e.base.r >> e.base.g >> e.base.b
                   >> e.tip.r >> e.tip.g >> e.tip.b;
            }
            std::size_t scount = 0;
            in >> scount;
            std::vector<unsigned char> style(scount);
            for (std::size_t k = 0; k < scount; ++k) { int v = 0; in >> v; style[k] = static_cast<unsigned char>(v); }
            for (Object& obj : m_objects) {
                if (obj.name == name) {
                    obj.terrainGrassPalette = std::move(pal);
                    obj.terrainGrassStyle = std::move(style);
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
            int   waterType = 0;
            float seaHeight = 0.55f, seaChoppy = 3.2f, seaSpeed = 0.8f, seaFreq = 0.10f, foam = 0.55f;
            std::string flowSpline = "-";
            if (version >= 77) {
                in >> waterType >> seaHeight >> seaChoppy >> seaSpeed >> seaFreq >> foam;
            }
            if (version >= 78) {
                in >> flowSpline;
            }
            float depthFade = 6.0f, shoreWidth = 0.8f, shoreStrength = 0.75f;
            if (version >= 113) {
                in >> depthFade >> shoreWidth >> shoreStrength;
            }
            float refractionStrength = 0.018f, reflectionRoughness = 0.12f;
            float environmentReflectionStrength = 0.85f, absorptionStrength = 0.75f;
            if (version >= 114) {
                in >> refractionStrength >> reflectionRoughness
                   >> environmentReflectionStrength >> absorptionStrength;
            }
            float causticsStrength = 0.25f, causticsScale = 1.5f;
            float maxRenderDistance = 2500.0f;
            glm::vec3 underwaterTint(0.04f, 0.30f, 0.38f);
            float underwaterFogDensity = 0.16f, underwaterDistortion = 0.006f;
            float underwaterTransitionSpeed = 3.5f;
            if (version >= 115) {
                in >> causticsStrength >> causticsScale >> maxRenderDistance
                   >> underwaterTint.r >> underwaterTint.g >> underwaterTint.b
                   >> underwaterFogDensity >> underwaterDistortion
                   >> underwaterTransitionSpeed;
            }
            float riverWidth = 8.0f;
            if (version >= 116) in >> riverWidth;
            std::string waterShaderPath = "-";
            if (version >= 122) in >> std::quoted(waterShaderPath);
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
                    obj.waterType = waterType;
                    obj.waterSeaHeight = seaHeight;
                    obj.waterSeaChoppy = seaChoppy;
                    obj.waterSeaSpeed = seaSpeed;
                    obj.waterSeaFreq = seaFreq;
                    obj.waterFoam = foam;
                    obj.waterFlowSpline = (flowSpline == "-") ? std::string() : flowSpline;
                    obj.waterDepthFadeDistance = depthFade;
                    obj.waterShoreFoamWidth = shoreWidth;
                    obj.waterShoreFoamStrength = shoreStrength;
                    obj.waterRefractionStrength = refractionStrength;
                    obj.waterReflectionRoughness = reflectionRoughness;
                    obj.waterEnvironmentReflectionStrength = environmentReflectionStrength;
                    obj.waterAbsorptionStrength = absorptionStrength;
                    obj.waterCausticsStrength = causticsStrength;
                    obj.waterCausticsScale = causticsScale;
                    obj.waterMaxRenderDistance = maxRenderDistance;
                    obj.waterUnderwaterTint = underwaterTint;
                    obj.waterUnderwaterFogDensity = underwaterFogDensity;
                    obj.waterUnderwaterDistortion = underwaterDistortion;
                    obj.waterUnderwaterTransitionSpeed = underwaterTransitionSpeed;
                    obj.waterRiverWidth = std::max(riverWidth, 0.1f);
                    obj.waterShaderPath =
                        (waterShaderPath == "-") ? std::string() : waterShaderPath;
                    break;
                }
            }
            continue;
        }

        if (recordType == "spline" && version >= 78) {
            std::string name;
            int closed = 0, type = 0;
            std::size_t count = 0;
            in >> name >> closed >> type >> count;
            std::vector<glm::vec3> pts;
            std::vector<glm::vec3> rotations;
            pts.reserve(count);
            rotations.reserve(count);
            for (std::size_t i = 0; i < count; ++i) {
                glm::vec3 p(0.0f);
                in >> p.x >> p.y >> p.z;
                pts.push_back(p);
                glm::vec3 rotation(0.0f);
                if (version >= 117) in >> rotation.x >> rotation.y >> rotation.z;
                rotations.push_back(rotation);
            }
            for (Object& obj : m_objects) {
                if (obj.name == name) {
                    obj.isSpline = true;
                    obj.splineClosed = (closed != 0);
                    obj.splineType = type;
                    obj.splinePoints = std::move(pts);
                    obj.splinePointRotations = std::move(rotations);
                    SyncSplineComponent(obj);
                    break;
                }
            }
            continue;
        }

        if (recordType == "ai_movement" && version >= 85) {
            std::string name;
            int mode = 0;
            float gravity = -9.81f, maxFallSpeed = 35.0f;
            float groundProbe = 0.25f, stepHeight = 0.35f, maxSlope = 50.0f;
            in >> std::quoted(name) >> mode >> gravity >> maxFallSpeed
               >> groundProbe >> stepHeight >> maxSlope;
            for (Object& obj : m_objects) {
                if (obj.name != name) continue;
                obj.navMovementMode = mode == static_cast<int>(engine::ai::AiMovementMode::Flying)
                    ? engine::ai::AiMovementMode::Flying
                    : engine::ai::AiMovementMode::Grounded;
                obj.navMovementGravity = std::min(gravity, 0.0f);
                obj.navMovementMaxFallSpeed = std::max(maxFallSpeed, 0.0f);
                obj.navMovementGroundProbe = std::max(groundProbe, 0.02f);
                obj.navMovementStepHeight = std::max(stepHeight, 0.0f);
                obj.navMovementMaxSlope = std::clamp(maxSlope, 0.0f, 89.0f);
                break;
            }
            continue;
        }
        if (recordType == "foliage" && version >= 119) {
            std::string objectName;
            std::string assetPath;
            std::string assetIdText;
            std::uint32_t nextId = 1;
            std::size_t instanceCount = 0;
            in >> std::quoted(objectName) >> std::quoted(assetPath)
               >> assetIdText >> nextId >> instanceCount;
            if (!in || instanceCount > 10'000'000u) {
                if (error) *error = "Scene contains an invalid foliage record.";
                Clear();
                return false;
            }
            engine::AssetHandle assetId;
            if (assetIdText != "-" && !engine::AssetHandle::Parse(assetIdText, &assetId)) {
                if (error) *error = "Scene contains an invalid foliage asset ID.";
                Clear();
                return false;
            }
            std::vector<engine::ecs::FoliageInstance> instances(instanceCount);
            for (engine::ecs::FoliageInstance& instance : instances) {
                int enabled = 1;
                in >> instance.id >> instance.typeIndex
                   >> instance.position.x >> instance.position.y >> instance.position.z
                   >> instance.rotationDegrees.x >> instance.rotationDegrees.y
                   >> instance.rotationDegrees.z
                   >> instance.scale.x >> instance.scale.y >> instance.scale.z
                   >> enabled;
                instance.enabled = enabled != 0;
            }
            std::string foliageOwner;
            if (version >= 121) {
                in >> std::quoted(foliageOwner);
                if (foliageOwner == "-") foliageOwner.clear();
            }
            if (!in) {
                if (error) *error = "Scene contains invalid foliage instances.";
                Clear();
                return false;
            }
            for (Object& object : m_objects) {
                if (object.name != objectName) continue;
                object.isFoliage = true;
                object.foliageAssetPath = assetPath == "-" ? std::string{} : assetPath;
                object.foliageAssetId = assetId;
                object.foliageInstances = std::move(instances);
                object.nextFoliageInstanceId = std::max(nextId, 1u);
                object.foliageTerrainOwner = foliageOwner;
                SyncFoliageComponent(object);
                break;
            }
            continue;
        }
        if (recordType == "ragdoll" && version >= 106) {
            std::string objectName;
            engine::Ragdoll ragdoll;
            in >> std::quoted(objectName)
               >> ragdoll.enabled >> ragdoll.activateOnDeath
               >> ragdoll.totalMass >> ragdoll.bodyRadiusScale
               >> ragdoll.linearDamping >> ragdoll.angularDamping
               >> ragdoll.deathImpulse >> ragdoll.maxBodies;
            if (version >= 129) {
                in >> std::quoted(ragdoll.assetPath);
                if (ragdoll.assetPath == "-") ragdoll.assetPath.clear();
                if (!ragdoll.assetPath.empty()) {
                    const std::string root = engine::FindContentRootForAsset(path);
                    std::filesystem::path assetPath(ragdoll.assetPath);
                    if (!assetPath.is_absolute() && !root.empty())
                        assetPath = std::filesystem::path(root) / assetPath;
                    engine::RagdollAssetData asset;
                    if (engine::LoadRagdollAsset(assetPath.string(), &asset, nullptr))
                        engine::ApplyRagdollAsset(asset, &ragdoll);
                }
            }
            auto found = std::find_if(
                m_objects.begin(), m_objects.end(),
                [&](const Object& object) { return object.name == objectName; });
            if (found != m_objects.end()) {
                found->ragdollEnabled = true;
                found->ragdoll = std::move(ragdoll);
            }
            if (!in) {
                if (error) *error = "Scene contains invalid ragdoll settings.";
                Clear();
                return false;
            }
            continue;
        }
        if (recordType == "decal" && version >= 128) {
            std::string name;
            float opacity = 1.0f;
            float surfaceOffset = 0.012f;
            in >> std::quoted(name) >> opacity >> surfaceOffset;
            if (!in) {
                if (error) *error = "Scene contains an invalid decal record.";
                Clear();
                return false;
            }
            auto found = std::find_if(
                m_objects.begin(), m_objects.end(),
                [&](const Object& object) { return object.name == name; });
            if (found != m_objects.end()) {
                found->decal = true;
                found->decalOpacity = std::clamp(opacity, 0.0f, 1.0f);
                found->decalSurfaceOffset =
                    std::clamp(surfaceOffset, 0.001f, 0.08f);
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
        engine::AssetHandle modelAssetId;
        engine::AssetHandle materialAssetId;
        std::string materialAssetPath;
        glm::vec3 modelOrientationEuler{0.0f};
        glm::vec3 modelOffsetPosition{0.0f};
        glm::vec3 modelOffsetScale{1.0f};
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
        std::vector<AnimationSource> animationSources;
        std::vector<ModelAttachment> modelAttachments;
        engine::ecs::FootIKSettings footIK;
        std::string characterAssetPath;
        engine::AssetHandle characterAssetId;
        std::string prefabAssetPath;
        engine::AssetHandle prefabAssetId;
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
        int scriptExecutionOrder = 0;
        std::vector<std::string> scriptDependencies;
        std::vector<ScriptBinding> additionalScripts;
        int navAgentEnabled = 0;
        float navAgentSpeed = 3.0f;
        float navAgentMaxForce = 20.0f;
        float navAgentReachRadius = 0.6f;
        float navAgentRepathInterval = 0.3f;
        std::vector<glm::vec3> patrolPoints;
        std::string navAgentTargetName;
        float navAgentVisionRange = 12.0f;
        float navAgentVisionHalfAngle = 45.0f;
        float navAgentHearingRange = 12.0f;
        float navAgentSquadAlertRadius = 18.0f;
        float navAgentSquadForgetTime = 6.0f;
        std::string navAgentBrainAsset;
        engine::AssetHandle navAgentBrainAssetId;
        int navAgentTeam = 0;
        int navAgentAutoTarget = 0;
        int navMeshBoundsVolume = 0;
        int audioSourceEnabled = 0;
        std::string audioAssetPath;
        engine::AssetHandle audioAssetId;
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
        engine::AssetHandle particleAssetId;
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

        in >> primitiveName >> std::quoted(name)
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
            in >> std::quoted(modelAssetPath) >> std::quoted(materialAssetPath);
            if (version >= 101) {
                std::string id;
                in >> id;
                if (id != "-" && !engine::AssetHandle::Parse(id, &modelAssetId))
                    in.setstate(std::ios::failbit);
                if (version >= 102) {
                    in >> id;
                    if (id != "-"
                        && !engine::AssetHandle::Parse(id, &materialAssetId))
                        in.setstate(std::ios::failbit);
                }
            }
            if (version >= 88) {
                in >> modelOrientationEuler.x >> modelOrientationEuler.y >> modelOrientationEuler.z;
            }
            if (version >= 89) {
                in >> modelOffsetPosition.x >> modelOffsetPosition.y >> modelOffsetPosition.z
                   >> modelOffsetScale.x >> modelOffsetScale.y >> modelOffsetScale.z;
            }
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
               >> std::quoted(animationClipName)
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
               >> std::quoted(animationIdleClipName)
               >> animationWalkClipIndex
               >> std::quoted(animationWalkClipName)
               >> animationRunClipIndex
               >> std::quoted(animationRunClipName)
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
                   >> std::quoted(event.name);
                if (version >= 99) in >> std::quoted(event.clipName);
                event.clipIndex = std::max(event.clipIndex, 0);
                event.time = std::max(event.time, 0.0f);
                if (event.name == "-") {
                    event.name.clear();
                }
                if (event.clipName == "-") event.clipName.clear();
                animationEvents.push_back(event);
            }
        }
        if (version >= 32) {
            std::size_t profileCount = 0;
            in >> profileCount;
            for (std::size_t i = 0; i < profileCount; ++i) {
                AnimationActionProfile profile;
                in >> std::quoted(profile.name)
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
                animationActionProfiles.push_back(profile);
            }
        }
        if (version >= 33) {
            std::size_t stateCount = 0;
            in >> stateCount;
            for (std::size_t i = 0; i < stateCount; ++i) {
                AnimationStateNode state;
                int loop = 1;
                in >> std::quoted(state.name)
                   >> state.clipIndex
                   >> std::quoted(state.clipName)
                   >> loop
                   >> state.speed;
                if (version >= 36) {
                    int rootMotion = 0;
                    in >> state.blendClipIndex
                       >> std::quoted(state.blendClipName)
                       >> std::quoted(state.blendParameter)
                       >> state.blendMin
                       >> state.blendMax
                       >> rootMotion;
                    if (state.blendClipName == "-") state.blendClipName.clear();
                    if (state.blendParameter == "-") state.blendParameter.clear();
                    state.rootMotion = rootMotion != 0;
                    if (version >= 86) {
                        if (version >= 87) {
                            int is2D = 0, synchronize = 1;
                            in >> is2D >> std::quoted(state.blendParameterY) >> synchronize;
                            if (state.blendParameterY == "-") state.blendParameterY.clear();
                            state.blendSpace2D = is2D != 0;
                            state.synchronizeBlendSpace = synchronize != 0;
                        }
                        std::size_t sampleCount = 0;
                        in >> sampleCount;
                        for (std::size_t sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex) {
                            AnimationStateNode::BlendSample sample;
                            in >> sample.clipIndex >> std::quoted(sample.clipName) >> sample.value;
                            if (version >= 87) in >> sample.valueY;
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
                animationStates.push_back(state);
            }

            if (version >= 36) {
                std::size_t parameterCount = 0;
                in >> parameterCount;
                for (std::size_t i = 0; i < parameterCount; ++i) {
                    AnimationParameter parameter;
                    int type = 0;
                    in >> std::quoted(parameter.name) >> type >> parameter.defaultValue;
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
                in >> std::quoted(transition.fromState)
                   >> std::quoted(transition.toState)
                   >> std::quoted(transition.parameter)
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
                if (version >= 95) {
                    std::size_t conditionCount = 0;
                    if (version >= 130) {
                        in >> transition.useConditions;
                    } else {
                        transition.useConditions = true;
                    }
                    in >> transition.requireAllConditions >> conditionCount;
                    for (std::size_t c = 0; c < conditionCount; ++c) {
                        AnimationStateTransition::Condition condition;
                        int conditionCompare = 0;
                        in >> std::quoted(condition.parameter)
                           >> conditionCompare >> condition.threshold;
                        if (condition.parameter == "-") condition.parameter.clear();
                        condition.compare = static_cast<AnimationStateTransition::Compare>(
                            std::clamp(conditionCompare, 0, 5));
                        transition.additionalConditions.push_back(std::move(condition));
                    }
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
        if (version >= 90) {
            std::size_t sourceCount = 0;
            in >> sourceCount;
            for (std::size_t i = 0; i < sourceCount; ++i) {
                AnimationSource source;
                int strip = 0;
                in >> std::quoted(source.file);
                if (version >= 101) {
                    std::string id;
                    in >> id;
                    if (id != "-"
                        && !engine::AssetHandle::Parse(id, &source.assetId))
                        in.setstate(std::ios::failbit);
                }
                in >> std::quoted(source.clipName) >> strip;
                if (version >= 94) in >> std::quoted(source.sourceClipName);
                if (version >= 134) in >> source.basePlaybackSpeed;
                if (source.file == "-") source.file.clear();
                if (source.clipName == "-") source.clipName.clear();
                if (source.sourceClipName == "-") source.sourceClipName.clear();
                source.stripRootMotion = strip != 0;
                source.basePlaybackSpeed = std::max(source.basePlaybackSpeed, 0.0f);
                animationSources.push_back(std::move(source));
            }
        }
        if (version >= 92) {
            std::size_t attachmentCount = 0;
            in >> attachmentCount;
            for (std::size_t i = 0; i < attachmentCount; ++i) {
                ModelAttachment a;
                in >> std::quoted(a.modelPath);
                if (version >= 101) {
                    std::string id;
                    in >> id;
                    if (id != "-"
                        && !engine::AssetHandle::Parse(id, &a.modelAssetId))
                        in.setstate(std::ios::failbit);
                    if (version >= 102) {
                        in >> id;
                        if (id != "-"
                            && !engine::AssetHandle::Parse(
                                id, &a.materialAssetId))
                            in.setstate(std::ios::failbit);
                    }
                }
                in >> std::quoted(a.boneName)
                   >> a.position.x >> a.position.y >> a.position.z
                   >> a.eulerDegrees.x >> a.eulerDegrees.y >> a.eulerDegrees.z
                   >> a.scale.x >> a.scale.y >> a.scale.z;
                if (version >= 93) {
                    in >> std::quoted(a.materialPath);
                    if (a.materialPath == "-") a.materialPath.clear();
                }
                if (version >= 98) {
                    in >> std::quoted(a.socketName);
                    if (a.socketName == "-") a.socketName.clear();
                }
                if (a.modelPath == "-") a.modelPath.clear();
                if (a.boneName == "-") a.boneName.clear();
                modelAttachments.push_back(std::move(a));
            }
        }
        if (version >= 120) {
            int footIkEnabled = 0;
            in >> footIkEnabled
               >> footIK.traceUp >> footIK.traceDown >> footIK.footHeight
               >> footIK.pelvisWeight >> footIK.maxPelvisDrop >> footIK.weight;
            footIK.enabled = footIkEnabled != 0;
        }
        if (version >= 93) {
            in >> std::quoted(characterAssetPath);
            if (version >= 101) {
                std::string id;
                in >> id;
                if (id != "-"
                    && !engine::AssetHandle::Parse(id, &characterAssetId))
                    in.setstate(std::ios::failbit);
            }
            if (characterAssetPath == "-") characterAssetPath.clear();
        }
        if (version >= 107) {
            in >> std::quoted(prefabAssetPath);
            std::string prefabId;
            in >> prefabId;
            if (prefabId != "-"
                && !engine::AssetHandle::Parse(prefabId, &prefabAssetId))
                in.setstate(std::ios::failbit);
            if (prefabAssetPath == "-") prefabAssetPath.clear();
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
            if (version >= 132) {
                int inheritScale = 1, collisionDirty = 0;
                in >> collider.localPosition.x >> collider.localPosition.y >> collider.localPosition.z
                   >> collider.localRotation.w >> collider.localRotation.x
                   >> collider.localRotation.y >> collider.localRotation.z
                   >> collider.localScale.x >> collider.localScale.y >> collider.localScale.z
                   >> inheritScale >> std::quoted(collider.collisionAssetPath) >> collisionDirty;
                collider.inheritTransformScale = inheritScale != 0;
                collider.collisionDirty = collisionDirty != 0;
            } else {
                // Older scenes stored collider dimensions in world/object-authored
                // units. Preserve their behavior instead of double-scaling them.
                collider.inheritTransformScale = false;
            }
            if (colliderShape >= static_cast<int>(engine::ecs::ColliderShape::Sphere)
                && colliderShape <= static_cast<int>(engine::ecs::ColliderShape::TriangleMesh))
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
            in >> std::quoted(triggerTargetName)
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
            if (version >= 91) {
                in >> playerController.facingMode
                   >> playerController.turnSpeed;
            }
            if (version >= 96) {
                in >> playerController.cameraMode
                   >> playerController.isometricYaw
                   >> playerController.isometricPitch
                   >> playerController.isometricDistance;
            } else {
                playerController.cameraMode = playerFirstPerson != 0 ? 1 : 0;
            }
            if (version >= 127) {
                in >> playerController.crouchSpeed
                   >> playerController.crouchedHeight
                   >> playerController.swimSpeed
                   >> playerController.swimVerticalSpeed;
            }
            if (version >= 69) {
                in >> std::quoted(triggerCameraSequenceName)
                   >> triggerEnterCameraAction
                   >> triggerExitCameraAction
                   >> triggerCameraLockInput
                   >> triggerCameraSkippable;
                if (triggerCameraSequenceName == "-") triggerCameraSequenceName.clear();
            }
            if (version >= 66) {
                in >> cameraZoneEnabled
                   >> std::quoted(cameraZonePresetName)
                   >> cameraZoneRestoreOnExit
                   >> cameraZonePriority
                   >> cameraZoneReturnBlend;
                if (cameraZonePresetName == "-") cameraZonePresetName.clear();
            }
            playerController.firstPerson = playerFirstPerson != 0;
            playerController.firstPerson = playerController.cameraMode == 1;
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
               >> std::quoted(scriptClassName)
               >> std::quoted(scriptPath);
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
                    in >> std::quoted(field.name) >> fieldType >> std::quoted(field.value);
                    if (field.name == "-") {
                        field.name.clear();
                    }
                    if (field.value == "-") {
                        field.value.clear();
                    }
                    field.type = static_cast<ScriptField::Type>(std::clamp(fieldType, 0, 7));
                    if (version >= 108) {
                        in >> field.minValue >> field.maxValue >> std::quoted(field.tooltip);
                        if (field.tooltip == "-") field.tooltip.clear();
                    }
                    if (version >= 109) {
                        in >> std::quoted(field.group);
                        if (field.group == "-") field.group.clear();
                    }
                    scriptFields.push_back(field);
                }
            }
            if (version >= 126) {
                std::size_t dependencyCount = 0;
                in >> scriptExecutionOrder >> dependencyCount;
                for (std::size_t i = 0; i < dependencyCount; ++i) {
                    std::string dependency;
                    in >> std::quoted(dependency);
                    if (dependency != "-") scriptDependencies.push_back(std::move(dependency));
                }
            }
            if (version >= 100) {
                std::size_t additionalCount = 0;
                in >> additionalCount;
                additionalScripts.reserve(additionalCount);
                for (std::size_t scriptIndex = 0; scriptIndex < additionalCount; ++scriptIndex) {
                    ScriptBinding script;
                    int enabled = 1;
                    std::size_t fieldCount = 0;
                    in >> enabled >> std::quoted(script.className)
                       >> std::quoted(script.path) >> fieldCount;
                    script.enabled = enabled != 0;
                    if (script.className == "-") script.className.clear();
                    if (script.path == "-") script.path.clear();
                    for (std::size_t fieldIndex = 0; fieldIndex < fieldCount; ++fieldIndex) {
                        ScriptField field;
                        int fieldType = 0;
                        in >> std::quoted(field.name) >> fieldType >> std::quoted(field.value);
                        if (field.name == "-") field.name.clear();
                        if (field.value == "-") field.value.clear();
                        field.type = static_cast<ScriptField::Type>(
                            std::clamp(fieldType, 0, 7));
                        if (version >= 108) {
                            in >> field.minValue >> field.maxValue >> std::quoted(field.tooltip);
                            if (field.tooltip == "-") field.tooltip.clear();
                        }
                        if (version >= 109) {
                            in >> std::quoted(field.group);
                            if (field.group == "-") field.group.clear();
                        }
                        script.fields.push_back(std::move(field));
                    }
                    if (version >= 126) {
                        std::size_t dependencyCount = 0;
                        in >> script.executionOrder >> dependencyCount;
                        for (std::size_t i = 0; i < dependencyCount; ++i) {
                            std::string dependency;
                            in >> std::quoted(dependency);
                            if (dependency != "-")
                                script.dependencies.push_back(std::move(dependency));
                        }
                    }
                    additionalScripts.push_back(std::move(script));
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
            in >> std::quoted(navAgentTargetName) >> navAgentVisionRange >> navAgentVisionHalfAngle;
            if (navAgentTargetName == "-") {
                navAgentTargetName.clear();
            }
        }
        if (version >= 40) {
            in >> std::quoted(navAgentBrainAsset);
            if (navAgentBrainAsset == "-") {
                navAgentBrainAsset.clear();
            }
        }
        if (version >= 41) {
            in >> navMeshBoundsVolume;
        }
        if (version >= 42) {
            in >> audioSourceEnabled >> std::quoted(audioAssetPath) >> audioVolume >> audioPitch
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
            if (version >= 104) {
                std::size_t shaderParameterCount = 0;
                in >> std::quoted(particleConfig.shaderPath)
                   >> shaderParameterCount;
                if (particleConfig.shaderPath == "-")
                    particleConfig.shaderPath.clear();
                if (!in || shaderParameterCount > 64) {
                    if (error) *error =
                        "Scene particle shader parameter count is invalid.";
                    Clear();
                    return false;
                }
                particleConfig.shaderParameters.clear();
                for (std::size_t parameterIndex = 0;
                     parameterIndex < shaderParameterCount;
                     ++parameterIndex) {
                    engine::ParticleShaderParameter parameter;
                    in >> std::quoted(parameter.name)
                       >> parameter.type
                       >> std::quoted(parameter.value);
                    particleConfig.shaderParameters.push_back(
                        std::move(parameter));
                }
                const auto readParticleId =
                    [&](engine::AssetHandle& id) {
                        std::string text;
                        in >> text;
                        return text == "-"
                            || engine::AssetHandle::Parse(text, &id);
                    };
                if (!readParticleId(particleAssetId)
                    || !readParticleId(particleConfig.textureAssetId)
                    || !readParticleId(particleConfig.meshAssetId)
                    || !readParticleId(particleConfig.shaderAssetId)) {
                    if (error) *error =
                        "Scene contains invalid particle asset IDs.";
                    Clear();
                    return false;
                }
                std::size_t layerIdCount = 0;
                in >> layerIdCount;
                if (!in || layerIdCount != particleEffectLayers.size()) {
                    if (error) *error =
                        "Scene particle layer identity count is invalid.";
                    Clear();
                    return false;
                }
                for (engine::ParticleEffectLayer& layer :
                     particleEffectLayers) {
                    if (!readParticleId(layer.assetId)) {
                        if (error) *error =
                            "Scene contains an invalid particle layer ID.";
                        Clear();
                        return false;
                    }
                }
            }
            if (version >= 105) {
                const auto readAssetId =
                    [&](engine::AssetHandle& id) {
                        std::string text;
                        in >> text;
                        return text == "-"
                            || engine::AssetHandle::Parse(text, &id);
                    };
                if (!readAssetId(audioAssetId)
                    || !readAssetId(navAgentBrainAssetId)) {
                    if (error) *error =
                        "Scene contains invalid audio or behavior asset IDs.";
                    Clear();
                    return false;
                }
            }
            // NavAgent hearing range (scene version 110+).
            if (version >= 110) {
                in >> navAgentHearingRange;
            }
            // NavAgent squad coordination tuning (scene version 111+).
            if (version >= 111) {
                in >> navAgentSquadAlertRadius >> navAgentSquadForgetTime;
            }
            // Platformer camera axis (scene version 112+).
            if (version >= 112) {
                in >> playerController.platformerYaw;
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

        resolvedDuplicateNames |= !IsHierarchyNameAvailable(name);
        CreateObject(name, primitive, MeshFor(primitive, cube, plane, sphere, capsule, cylinder, cone, pyramid, torus, staircase), transform, color);
        m_objects.back().visible = visible != 0;
        m_objects.back().locked = locked != 0;
        m_objects.back().modelAssetPath = modelAssetPath;
        m_objects.back().modelAssetId = modelAssetId;
        m_objects.back().materialAssetPath = materialAssetPath;
        m_objects.back().materialAssetId = materialAssetId;
        m_objects.back().modelOrientationEuler = modelOrientationEuler;
        m_objects.back().modelOffsetPosition = modelOffsetPosition;
        m_objects.back().modelOffsetScale = modelOffsetScale;
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
        m_objects.back().animationSources = animationSources;
        m_objects.back().modelAttachments = modelAttachments;
        m_objects.back().footIK = footIK;
        m_objects.back().characterAssetPath = characterAssetPath;
        m_objects.back().characterAssetId = characterAssetId;
        m_objects.back().prefabAssetPath = prefabAssetPath;
        m_objects.back().prefabAssetId = prefabAssetId;
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
        playerController.cameraMode = std::clamp(playerController.cameraMode, 0, 3);
        playerController.isometricPitch =
            std::clamp(playerController.isometricPitch, -89.0f, 89.0f);
        playerController.isometricDistance =
            std::max(playerController.isometricDistance, 0.0f);
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
        m_objects.back().scriptExecutionOrder = scriptExecutionOrder;
        m_objects.back().scriptDependencies = std::move(scriptDependencies);
        m_objects.back().additionalScripts = std::move(additionalScripts);
        m_objects.back().navAgentEnabled = navAgentEnabled != 0;
        m_objects.back().navAgentSpeed = navAgentSpeed;
        m_objects.back().navAgentMaxForce = navAgentMaxForce;
        m_objects.back().navAgentReachRadius = navAgentReachRadius;
        m_objects.back().navAgentRepathInterval = navAgentRepathInterval;
        m_objects.back().patrolPoints = patrolPoints;
        m_objects.back().navAgentTargetName = navAgentTargetName;
        m_objects.back().navAgentVisionRange = navAgentVisionRange;
        m_objects.back().navAgentVisionHalfAngle = navAgentVisionHalfAngle;
        m_objects.back().navAgentHearingRange = navAgentHearingRange;
        m_objects.back().navAgentSquadAlertRadius = navAgentSquadAlertRadius;
        m_objects.back().navAgentSquadForgetTime = navAgentSquadForgetTime;
        m_objects.back().navAgentBrainAsset = navAgentBrainAsset;
        m_objects.back().navAgentBrainAssetId = navAgentBrainAssetId;
        m_objects.back().navAgentTeam = navAgentTeam;
        m_objects.back().navAgentAutoTarget = navAgentAutoTarget != 0;
        m_objects.back().navMeshBoundsVolume = navMeshBoundsVolume != 0;
        m_objects.back().audioSourceEnabled = audioSourceEnabled != 0;
        m_objects.back().audioAssetPath = audioAssetPath;
        m_objects.back().audioAssetId = audioAssetId;
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
        m_objects.back().particleAssetId = particleAssetId;
        m_objects.back().particleAssetOverride = particleAssetOverride != 0;
        m_objects.back().particleEffectLayers = std::move(particleEffectLayers);
        // Older generated particle actors were invisible placeholder cubes,
        // which caused runtime export to omit the entire emitter. Upgrade only
        // that generated pattern; intentionally hidden user objects stay
        // hidden.
        if (m_objects.back().particleSystemEnabled
            && !m_objects.back().visible
            && m_objects.back().primitive == Primitive::Cube
            && m_objects.back().modelAssetPath.empty()
            && m_objects.back().name.rfind("ParticleSystem_", 0) == 0) {
            m_objects.back().primitive = Primitive::Empty;
            m_objects.back().visible = true;
        }
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

    const std::string contentRoot = engine::FindContentRootForAsset(path);
    engine::AssetRegistry assetRegistry;
    std::string ignoredRegistryError;
    if (!contentRoot.empty()
        && assetRegistry.Load(
            engine::AssetRegistry::DefaultRegistryPath(contentRoot),
            &ignoredRegistryError)) {
        if (m_environment.hudAssetId.Valid()) {
            const std::string resolved = engine::ResolveAssetReference(
                &assetRegistry, contentRoot,
                {m_environment.hudAssetId, m_environment.hudAsset},
                engine::AssetType::Hud);
            if (!resolved.empty()) m_environment.hudAsset = resolved;
        }
        for (Environment::PostProcessEffect& effect :
             m_environment.postProcessEffects) {
            if (!effect.shaderAssetId.Valid()) continue;
            const std::string resolved = engine::ResolveAssetReference(
                &assetRegistry, contentRoot,
                {effect.shaderAssetId, effect.shaderPath},
                engine::AssetType::Shader);
            if (!resolved.empty()) effect.shaderPath = resolved;
        }
        for (Object& object : m_objects) {
            const auto resolveParticleReference =
                [&](engine::AssetHandle id, std::string& fallback,
                    engine::AssetType type) {
                    if (!id.Valid()) return;
                    const std::string resolved =
                        engine::ResolveAssetReference(
                            &assetRegistry, contentRoot,
                            {id, fallback}, type);
                    if (!resolved.empty()) fallback = resolved;
                };
            resolveParticleReference(
                object.particleAssetId, object.particleAssetPath,
                engine::AssetType::Particle);
            resolveParticleReference(
                object.particleConfig.textureAssetId,
                object.particleConfig.texturePath,
                engine::AssetType::Texture);
            resolveParticleReference(
                object.particleConfig.meshAssetId,
                object.particleConfig.meshPath,
                engine::AssetType::StaticMesh);
            resolveParticleReference(
                object.particleConfig.shaderAssetId,
                object.particleConfig.shaderPath,
                engine::AssetType::Shader);
            for (engine::ParticleEffectLayer& layer :
                 object.particleEffectLayers)
                resolveParticleReference(
                    layer.assetId, layer.assetPath,
                    engine::AssetType::Particle);
            resolveParticleReference(
                object.audioAssetId, object.audioAssetPath,
                engine::AssetType::Audio);
            resolveParticleReference(
                object.navAgentBrainAssetId,
                object.navAgentBrainAsset,
                engine::AssetType::BehaviorTree);
            if (object.modelAssetId.Valid()) {
                const std::string resolved = engine::ResolveAssetReference(
                    &assetRegistry, contentRoot,
                    {object.modelAssetId, object.modelAssetPath},
                    object.skeletalModel ? engine::AssetType::SkeletalMesh
                                         : engine::AssetType::StaticMesh);
                if (!resolved.empty()) object.modelAssetPath = resolved;
            }
            if (object.materialAssetId.Valid()) {
                const engine::AssetRegistryEntry* materialEntry =
                    assetRegistry.Find(object.materialAssetId);
                const std::string resolved = engine::ResolveAssetReference(
                    &assetRegistry, contentRoot,
                    {object.materialAssetId, object.materialAssetPath},
                    materialEntry
                            && materialEntry->type == engine::AssetType::Texture
                        ? engine::AssetType::Texture
                        : engine::AssetType::Material);
                if (!resolved.empty()) object.materialAssetPath = resolved;
            }
            if (object.characterAssetId.Valid()) {
                const std::string resolved = engine::ResolveAssetReference(
                    &assetRegistry, contentRoot,
                    {object.characterAssetId, object.characterAssetPath},
                    engine::AssetType::Character);
                if (!resolved.empty()) object.characterAssetPath = resolved;
            }
            for (AnimationSource& source : object.animationSources) {
                if (!source.assetId.Valid()) continue;
                const std::string resolved = engine::ResolveAssetReference(
                    &assetRegistry, contentRoot, {source.assetId, source.file});
                if (!resolved.empty()) source.file = resolved;
            }
            for (ModelAttachment& attachment : object.modelAttachments) {
                if (attachment.modelAssetId.Valid()) {
                    const std::string resolved = engine::ResolveAssetReference(
                        &assetRegistry, contentRoot,
                        {attachment.modelAssetId, attachment.modelPath},
                        engine::AssetType::StaticMesh);
                    if (!resolved.empty()) attachment.modelPath = resolved;
                }
                if (attachment.materialAssetId.Valid()) {
                    const std::string resolved = engine::ResolveAssetReference(
                        &assetRegistry, contentRoot,
                        {attachment.materialAssetId, attachment.materialPath},
                        engine::AssetType::Material);
                    if (!resolved.empty()) attachment.materialPath = resolved;
                }
            }
        }
    }

    // Migrate existing rivers whose legacy transform still points at the original
    // square water patch rather than the generated spline ribbon.
    for (Object& water : m_objects) {
        if (!water.isWater || water.waterFlowSpline.empty()) continue;
        for (const Object& spline : m_objects) {
            if (!spline.isSpline || spline.name != water.waterFlowSpline
                || spline.splinePoints.empty()) continue;
            glm::vec3 boundsMin = spline.splinePoints.front();
            glm::vec3 boundsMax = boundsMin;
            for (const glm::vec3& point : spline.splinePoints) {
                boundsMin = glm::min(boundsMin, point);
                boundsMax = glm::max(boundsMax, point);
            }
            if (Transform* transform = m_registry.TryGet<Transform>(water.entity))
                transform->position = (boundsMin + boundsMax) * 0.5f;
            break;
        }
    }
    // Repair invalid memberships and parent links without rejecting older/corrupt
    // editor organization data. Runtime object data remains untouched.
    for (Object& object : m_objects)
        if (!GroupExists(object.editorGroupId)) object.editorGroupId = kRootGroupId;
    for (SceneGroup& group : m_groups) {
        if (group.parentId == group.id || !GroupExists(group.parentId))
            group.parentId = kRootGroupId;
        GroupId cursor = group.parentId;
        std::size_t guard = 0;
        while (cursor != kRootGroupId && guard++ <= m_groups.size()) {
            if (cursor == group.id) { group.parentId = kRootGroupId; break; }
            const auto parent = std::find_if(m_groups.begin(), m_groups.end(),
                [cursor](const SceneGroup& candidate) { return candidate.id == cursor; });
            if (parent == m_groups.end()) break;
            cursor = parent->parentId;
        }
    }
    m_selectedIndex = m_objects.empty() ? -1 : 0;
    m_hierarchySelection = m_objects.empty()
        ? HierarchySelectionType::None : HierarchySelectionType::Object;
    m_dirty = false;
    ClearHistory();
    if (error) *error = resolvedDuplicateNames
        ? "Resolved duplicate hierarchy names while loading scene." : std::string{};
    return true;
}

const EditorScene::Object * EditorScene::SelectedObject() const
{
    if (m_hierarchySelection == HierarchySelectionType::Group) return nullptr;
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return nullptr;
    }
    return &m_objects[static_cast<std::size_t>(m_selectedIndex)];
}

const EditorScene::SceneGroup* EditorScene::SelectedGroup() const {
    if (m_hierarchySelection != HierarchySelectionType::Group) return nullptr;
    for (const SceneGroup& group : m_groups)
        if (group.id == m_selectedGroupId) return &group;
    return nullptr;
}

const EditorScene::Object* EditorScene::FindObject(Entity entity) const {
    for (const Object& object : m_objects) if (object.entity == entity) return &object;
    return nullptr;
}

EditorScene::Object* EditorScene::FindObject(Entity entity) {
    return const_cast<Object*>(std::as_const(*this).FindObject(entity));
}

int EditorScene::FindObjectIndex(Entity entity) const {
    for (int i = 0; i < static_cast<int>(m_objects.size()); ++i)
        if (m_objects[static_cast<std::size_t>(i)].entity == entity) return i;
    return -1;
}

bool EditorScene::SelectEntity(Entity entity) {
    const int index = FindObjectIndex(entity);
    if (index < 0) return false;
    SelectIndex(index);
    return true;
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

const engine::ecs::ReflectionProbe* EditorScene::TryGetReflectionProbe(
    engine::ecs::Entity entity) const
{
    return const_cast<engine::ecs::Registry&>(m_registry)
        .TryGet<engine::ecs::ReflectionProbe>(entity);
}

bool EditorScene::SetSelectedReflectionProbe(bool enabled,const engine::ecs::ReflectionProbe& input){
    if(m_selectedIndex<0||m_selectedIndex>=static_cast<int>(m_objects.size()))return false;
    Object& selected=m_objects[static_cast<std::size_t>(m_selectedIndex)];if(selected.locked)return false;
    PushUndoSnapshot();engine::ecs::ReflectionProbe probe=input;
    if(!probe.stableId.Valid())probe.stableId=engine::AssetHandle::Generate();
    probe.boxExtents=glm::max(probe.boxExtents,glm::vec3(0.01f));probe.radius=std::max(probe.radius,0.01f);
    probe.blendDistance=std::max(probe.blendDistance,0.001f);probe.intensity=std::max(probe.intensity,0.0f);
    probe.captureResolution=static_cast<std::uint32_t>(std::clamp<int>(static_cast<int>(probe.captureResolution),32,512));
    probe.enabled=enabled&&probe.enabled;selected.reflectionProbeEnabled=enabled;selected.reflectionProbe=probe;
    if(enabled){if(auto* component=m_registry.TryGet<engine::ecs::ReflectionProbe>(selected.entity))*component=probe;
        else m_registry.Add<engine::ecs::ReflectionProbe>(selected.entity,probe);}
    else m_registry.Remove<engine::ecs::ReflectionProbe>(selected.entity);
    m_dirty=true;return true;
}

bool EditorScene::SetSelectedPostProcessVolume(bool enabled,const engine::ecs::PostProcessVolume& input){
    if(m_selectedIndex<0||m_selectedIndex>=static_cast<int>(m_objects.size()))return false;
    Object& selected=m_objects[static_cast<std::size_t>(m_selectedIndex)];if(selected.locked)return false;
    PushUndoSnapshot();engine::ecs::PostProcessVolume volume=input;
    if(!volume.stableId.Valid())volume.stableId=engine::AssetHandle::Generate();
    volume.boxExtents=glm::max(volume.boxExtents,glm::vec3(0.01f));
    volume.blendDistance=std::max(volume.blendDistance,0.0f);
    volume.blendWeight=std::clamp(volume.blendWeight,0.0f,1.0f);
    selected.postProcessVolumeEnabled=enabled;selected.postProcessVolume=volume;
    if(enabled)m_registry.Add<engine::ecs::PostProcessVolume>(selected.entity,volume);
    else m_registry.Remove<engine::ecs::PostProcessVolume>(selected.entity);
    m_dirty=true;return true;
}

bool EditorScene::SetSelectedLocalFogVolume(bool enabled,const engine::ecs::LocalFogVolume& input){
    if(m_selectedIndex<0||m_selectedIndex>=static_cast<int>(m_objects.size()))return false;
    Object& selected=m_objects[static_cast<std::size_t>(m_selectedIndex)];if(selected.locked)return false;
    PushUndoSnapshot();engine::ecs::LocalFogVolume volume=input;
    if(!volume.stableId.Valid())volume.stableId=engine::AssetHandle::Generate();
    volume.boxExtents=glm::max(volume.boxExtents,glm::vec3(0.01f));
    volume.radius=std::max(volume.radius,0.01f);volume.blendDistance=std::max(volume.blendDistance,0.001f);
    volume.density=std::max(volume.density,0.0f);volume.extinction=std::max(volume.extinction,0.0f);
    volume.anisotropy=std::clamp(volume.anisotropy,-0.94f,0.94f);
    selected.localFogVolumeEnabled=enabled;selected.localFogVolume=volume;
    if(enabled)m_registry.Add<engine::ecs::LocalFogVolume>(selected.entity,volume);
    else m_registry.Remove<engine::ecs::LocalFogVolume>(selected.entity);
    m_dirty=true;return true;
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
        m_selectedIndices.clear();
        return;
    }
    m_selectedIndex = (m_selectedIndex + 1) % static_cast<int>(m_objects.size());
    m_selectedIndices.assign(1, m_selectedIndex);
    m_hierarchySelection = HierarchySelectionType::Object;
    m_selectedGroupId = kRootGroupId;
}

void EditorScene::SelectPrevious()
{
    if (m_objects.empty()) {
        m_selectedIndex = -1;
        m_selectedIndices.clear();
        return;
    }
    m_selectedIndex = (m_selectedIndex <= 0)
        ? static_cast<int>(m_objects.size()) - 1
        : m_selectedIndex - 1;
    m_selectedIndices.assign(1, m_selectedIndex);
    m_hierarchySelection = HierarchySelectionType::Object;
    m_selectedGroupId = kRootGroupId;
}

void EditorScene::SelectIndex(int index)
{
    if (index < 0 || index >= static_cast<int>(m_objects.size())) {
        return;
    }
    m_selectedIndex = index;
    m_selectedIndices.assign(1, index);   // single selection replaces the whole set
    m_hierarchySelection = HierarchySelectionType::Object;
    m_selectedGroupId = kRootGroupId;
    // River geometry is authored by its linked world-space spline. Keep the water
    // object's transform at the ribbon centre so its gizmo sits on the visible plane.
    Object& selected = m_objects[static_cast<std::size_t>(index)];
    if (selected.isWater && !selected.waterFlowSpline.empty()) {
        for (const Object& spline : m_objects) {
            if (!spline.isSpline || spline.name != selected.waterFlowSpline
                || spline.splinePoints.empty()) continue;
            glm::vec3 boundsMin = spline.splinePoints.front();
            glm::vec3 boundsMax = boundsMin;
            for (const glm::vec3& point : spline.splinePoints) {
                boundsMin = glm::min(boundsMin, point);
                boundsMax = glm::max(boundsMax, point);
            }
            if (Transform* transform = m_registry.TryGet<Transform>(selected.entity))
                transform->position = (boundsMin + boundsMax) * 0.5f;
            break;
        }
    }
}

void EditorScene::SelectGroup(GroupId id) {
    if (!GroupExists(id)) return;
    m_selectedIndex = -1;
    m_selectedIndices.clear();
    m_hierarchySelection = HierarchySelectionType::Group;
    m_selectedGroupId = id;
}

void EditorScene::ToggleSelection(int index)
{
    if (index < 0 || index >= static_cast<int>(m_objects.size())) {
        return;
    }
    EnsureSelectionValid();
    m_hierarchySelection = HierarchySelectionType::Object;
    m_selectedGroupId = kRootGroupId;
    const auto it = std::find(m_selectedIndices.begin(), m_selectedIndices.end(), index);
    if (it != m_selectedIndices.end()) {
        // Already selected -> remove it. Move the primary to another member (or none).
        m_selectedIndices.erase(it);
        if (m_selectedIndex == index)
            m_selectedIndex = m_selectedIndices.empty() ? -1 : m_selectedIndices.back();
    } else {
        m_selectedIndices.push_back(index);
        m_selectedIndex = index;            // primary follows the most-recently added
    }
}

void EditorScene::Deselect()
{
    m_selectedIndex = -1;
    m_selectedIndices.clear();
    m_hierarchySelection = HierarchySelectionType::None;
    m_selectedGroupId = kRootGroupId;
}

bool EditorScene::IsHierarchyNameAvailable(const std::string& requested,
                                            Entity ignoreObject,
                                            GroupId ignoreGroup) const {
    const std::string name = TrimHierarchyName(requested);
    if (name.empty()) return false;
    for (const Object& object : m_objects)
        if (object.entity != ignoreObject && object.name == name) return false;
    for (const SceneGroup& group : m_groups)
        if (group.id != ignoreGroup && group.name == name) return false;
    return true;
}

std::string EditorScene::MakeUniqueHierarchyName(const std::string& requested,
                                                  Entity ignoreObject,
                                                  GroupId ignoreGroup) const {
    std::string base = TrimHierarchyName(requested);
    if (base.empty()) base = "Object";
    if (IsHierarchyNameAvailable(base, ignoreObject, ignoreGroup)) return base;
    for (std::uint64_t suffix = 1;; ++suffix) {
        const std::string candidate = base + "_" + std::to_string(suffix);
        if (IsHierarchyNameAvailable(candidate, ignoreObject, ignoreGroup)) return candidate;
    }
}

bool EditorScene::GroupExists(GroupId id) const {
    if (id == kRootGroupId) return true;
    return std::any_of(m_groups.begin(), m_groups.end(),
        [id](const SceneGroup& group) { return group.id == id; });
}

EditorScene::GroupId EditorScene::CreateGroup(const std::string& requested, GroupId parentId) {
    if (!GroupExists(parentId)) parentId = kRootGroupId;
    PushUndoSnapshot();
    SceneGroup group;
    group.id = m_nextGroupId++;
    group.name = MakeUniqueHierarchyName(requested.empty() ? "Group" : requested);
    group.parentId = parentId;
    m_groups.push_back(group);
    m_dirty = true;
    SelectGroup(group.id);
    return group.id;
}

bool EditorScene::RenameGroup(GroupId id, const std::string& requested) {
    const std::string name = TrimHierarchyName(requested);
    if (name.empty() || !IsHierarchyNameAvailable(name, engine::ecs::kNull, id)) return false;
    for (SceneGroup& group : m_groups) {
        if (group.id != id || group.name == name) continue;
        PushUndoSnapshot(); group.name = name; m_dirty = true; return true;
    }
    return false;
}

bool EditorScene::MoveObjectToGroup(int objectIndex, GroupId groupId) {
    if (objectIndex < 0 || objectIndex >= static_cast<int>(m_objects.size())
        || !GroupExists(groupId)) return false;
    Object& object = m_objects[static_cast<std::size_t>(objectIndex)];
    if (object.editorGroupId == groupId) return false;
    PushUndoSnapshot(); object.editorGroupId = groupId; m_dirty = true; return true;
}

bool EditorScene::MoveSelectedObjectsToGroup(GroupId groupId) {
    if (!GroupExists(groupId)) return false;
    EnsureSelectionValid();
    bool changed = false;
    for (int index : m_selectedIndices)
        if (index >= 0 && index < static_cast<int>(m_objects.size())
            && m_objects[static_cast<std::size_t>(index)].editorGroupId != groupId) changed = true;
    if (!changed) return false;
    PushUndoSnapshot();
    for (int index : m_selectedIndices)
        if (index >= 0 && index < static_cast<int>(m_objects.size()))
            m_objects[static_cast<std::size_t>(index)].editorGroupId = groupId;
    m_dirty = true; return true;
}

bool EditorScene::MoveGroupToGroup(GroupId id, GroupId parentId) {
    if (id == kRootGroupId || id == parentId || !GroupExists(id) || !GroupExists(parentId)) return false;
    for (GroupId cursor = parentId; cursor != kRootGroupId;) {
        if (cursor == id) return false;
        const auto it = std::find_if(m_groups.begin(), m_groups.end(),
            [cursor](const SceneGroup& group) { return group.id == cursor; });
        if (it == m_groups.end()) break;
        cursor = it->parentId;
    }
    for (SceneGroup& group : m_groups) if (group.id == id) {
        if (group.parentId == parentId) return false;
        PushUndoSnapshot(); group.parentId = parentId; m_dirty = true; return true;
    }
    return false;
}

bool EditorScene::DeleteGroup(GroupId id) {
    if (id == kRootGroupId) return false;
    const auto it = std::find_if(m_groups.begin(), m_groups.end(),
        [id](const SceneGroup& group) { return group.id == id; });
    if (it == m_groups.end()) return false;
    const GroupId parent = GroupExists(it->parentId) ? it->parentId : kRootGroupId;
    PushUndoSnapshot();
    for (Object& object : m_objects) if (object.editorGroupId == id) object.editorGroupId = parent;
    for (SceneGroup& group : m_groups) if (group.parentId == id) group.parentId = parent;
    m_groups.erase(it);
    if (m_selectedGroupId == id) Deselect();
    m_dirty = true; return true;
}

std::size_t EditorScene::GroupObjectCount(GroupId id, bool recursive) const {
    std::size_t count = 0;
    for (const Object& object : m_objects) if (object.editorGroupId == id) ++count;
    if (recursive) for (const SceneGroup& group : m_groups)
        if (group.parentId == id) count += GroupObjectCount(group.id, true);
    return count;
}

std::size_t EditorScene::ChildGroupCount(GroupId id) const {
    return static_cast<std::size_t>(std::count_if(m_groups.begin(), m_groups.end(),
        [id](const SceneGroup& group) { return group.parentId == id; }));
}

const std::vector<int>& EditorScene::SelectedIndices() const
{
    EnsureSelectionValid();
    return m_selectedIndices;
}

void EditorScene::EnsureSelectionValid() const
{
    const int count = static_cast<int>(m_objects.size());
    m_selectedIndices.erase(
        std::remove_if(m_selectedIndices.begin(), m_selectedIndices.end(),
                       [count](int i) { return i < 0 || i >= count; }),
        m_selectedIndices.end());
    if (m_selectedIndex >= 0 && m_selectedIndex < count) {
        // A valid primary that isn't in the set means it was set directly (add/duplicate/
        // undo) and the set is stale -> collapse to just the primary.
        if (std::find(m_selectedIndices.begin(), m_selectedIndices.end(), m_selectedIndex)
            == m_selectedIndices.end()) {
            m_selectedIndices.assign(1, m_selectedIndex);
        }
    } else if (m_selectedIndex < 0) {
        m_selectedIndices.clear();
    }
}

void EditorScene::MoveSelected(const glm::vec3 & delta)
{
    // Translate every selected (unlocked) object by the same delta so a gizmo drag
    // moves the whole multi-selection as a rigid group.
    EnsureSelectionValid();
    bool moved = false;
    for (int index : m_selectedIndices) {
        if (index < 0 || index >= static_cast<int>(m_objects.size())) continue;
        Object& object = m_objects[static_cast<std::size_t>(index)];
        if (object.locked) continue;
        if (object.isWater && !object.waterFlowSpline.empty()) {
            for (Object& spline : m_objects) {
                if (!spline.isSpline || spline.name != object.waterFlowSpline) continue;
                for (glm::vec3& point : spline.splinePoints) point += delta;
                break;
            }
        }
        if (Transform* transform = m_registry.TryGet<Transform>(object.entity)) {
            transform->position += delta;
            moved = true;
        }
    }
    if (moved) m_dirty = true;
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

        NormalizeTransformValues(*transform);
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
        NormalizeTransformValues(*transform);
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

    Transform normalized = value;
    NormalizeTransformValues(normalized);
    if (transform->position == normalized.position
        && transform->scale == normalized.scale
        && transform->rotation == normalized.rotation) {
        return false;
    }

    if (!m_transformEditOpen) {
        PushUndoSnapshot();
    }
    const glm::vec3 positionDelta = normalized.position - transform->position;
    Object* selected = m_selectedIndex >= 0
        ? &m_objects[static_cast<std::size_t>(m_selectedIndex)] : nullptr;
    if (selected && selected->isWater && !selected->waterFlowSpline.empty()
        && glm::dot(positionDelta, positionDelta) > 0.0f) {
        for (Object& spline : m_objects) {
            if (!spline.isSpline || spline.name != selected->waterFlowSpline) continue;
            for (glm::vec3& point : spline.splinePoints) point += positionDelta;
            break;
        }
    }
    *transform = normalized;
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

bool EditorScene::SetObjectTransformsUndoable(
    const std::vector<int>& indices,
    const std::vector<Transform>& transforms)
{
    if (indices.size() != transforms.size() || indices.empty()) return false;
    bool canApply = false;
    for (std::size_t i = 0; i < indices.size(); ++i) {
        const int index = indices[i];
        if (index < 0 || index >= static_cast<int>(m_objects.size())) continue;
        const Object& object = m_objects[static_cast<std::size_t>(index)];
        if (!object.locked && m_registry.TryGet<Transform>(object.entity)) {
            canApply = true;
            break;
        }
    }
    if (!canApply) return false;
    PushUndoSnapshot();
    for (std::size_t i = 0; i < indices.size(); ++i) {
        const int index = indices[i];
        if (index < 0 || index >= static_cast<int>(m_objects.size())) continue;
        Object& object = m_objects[static_cast<std::size_t>(index)];
        if (object.locked) continue;
        if (Transform* transform = m_registry.TryGet<Transform>(object.entity))
            *transform = transforms[i];
    }
    m_dirty = true;
    return true;
}

void EditorScene::SelectIndices(const std::vector<int>& indices)
{
    m_selectedIndices.clear();
    for (int index : indices) {
        if (index < 0 || index >= static_cast<int>(m_objects.size())) continue;
        if (std::find(m_selectedIndices.begin(), m_selectedIndices.end(), index)
            == m_selectedIndices.end())
            m_selectedIndices.push_back(index);
    }
    m_selectedIndex = m_selectedIndices.empty() ? -1 : m_selectedIndices.back();
    m_hierarchySelection = m_selectedIndices.empty()
        ? HierarchySelectionType::None : HierarchySelectionType::Object;
    m_selectedGroupId = kRootGroupId;
}

bool EditorScene::AssignObjectsToLayer(const std::vector<int>& indices,
                                       const std::string& requestedLayer)
{
    const std::string layer = requestedLayer.empty() ? "Default" : requestedLayer;
    bool changed = false;
    for (int index : indices) {
        if (index >= 0 && index < static_cast<int>(m_objects.size())
            && m_objects[static_cast<std::size_t>(index)].editorLayer != layer) {
            changed = true;
            break;
        }
    }
    if (!changed) return false;
    PushUndoSnapshot();
    for (int index : indices) {
        if (index >= 0 && index < static_cast<int>(m_objects.size()))
            m_objects[static_cast<std::size_t>(index)].editorLayer = layer;
    }
    m_dirty = true;
    return true;
}

bool EditorScene::SetLayerVisible(const std::string& layer, bool visible)
{
    bool changed = false;
    for (const Object& object : m_objects)
        if (object.editorLayer == layer && object.visible != visible) changed = true;
    if (!changed) return false;
    PushUndoSnapshot();
    for (Object& object : m_objects)
        if (object.editorLayer == layer) object.visible = visible;
    m_dirty = true;
    return true;
}

bool EditorScene::SetLayerLocked(const std::string& layer, bool locked)
{
    bool changed = false;
    for (const Object& object : m_objects)
        if (object.editorLayer == layer && object.locked != locked) changed = true;
    if (!changed) return false;
    PushUndoSnapshot();
    for (Object& object : m_objects)
        if (object.editorLayer == layer) object.locked = locked;
    m_dirty = true;
    return true;
}

bool EditorScene::RenameLayer(const std::string& oldName,
                              const std::string& requestedName)
{
    const std::string newName = requestedName.empty() ? "Default" : requestedName;
    if (oldName.empty() || oldName == newName) return false;
    bool found = false;
    for (const Object& object : m_objects)
        if (object.editorLayer == oldName) { found = true; break; }
    if (!found) return false;
    PushUndoSnapshot();
    for (Object& object : m_objects)
        if (object.editorLayer == oldName) object.editorLayer = newName;
    m_dirty = true;
    return true;
}

bool EditorScene::ShowAllLayers()
{
    bool changed = false;
    for (const Object& object : m_objects)
        if (!object.visible) { changed = true; break; }
    if (!changed) return false;
    PushUndoSnapshot();
    for (Object& object : m_objects) object.visible = true;
    m_dirty = true;
    return true;
}

void EditorScene::ApplySnapshotUndoable(const Snapshot& snapshot,
                                        const engine::Mesh& cube,
                                        const engine::Mesh& plane,
                                        const engine::Mesh& sphere,
                                        const engine::Mesh& capsule,
                                        const engine::Mesh& cylinder,
                                        const engine::Mesh& cone,
                                        const engine::Mesh& pyramid,
                                        const engine::Mesh& torus,
                                        const engine::Mesh& staircase) {
    PushUndoSnapshot();
    RestoreSnapshot(snapshot, cube, plane, sphere, capsule, cylinder, cone,
                    pyramid, torus, staircase);
    m_dirty = true;
    m_transformEditOpen = false;
}

void EditorScene::AddEmpty(const engine::Mesh& placeholderMesh)
{
    PushUndoSnapshot();

    Transform transform;
    const std::string name = "EmptyObject_" + std::to_string(m_nextCubeNumber++);
    // Keep a placeholder only for editor snapshots and serialization. Empty
    // objects are excluded from rendering and receive no runtime MeshRenderer.
    CreateObject(name, Primitive::Empty, placeholderMesh, transform, glm::vec3(0.45f));
    m_selectedIndex = static_cast<int>(m_objects.size()) - 1;
    m_dirty = true;
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
    const std::string trimmed = TrimHierarchyName(name);
    if (selected.locked || trimmed.empty() || selected.name == trimmed
        || !IsHierarchyNameAvailable(trimmed, selected.entity)) {
        return false;
    }

    PushUndoSnapshot();
    selected.name = trimmed;
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

bool EditorScene::SetSelectedModelAsset(
    const std::string &path, engine::AssetHandle id)
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
    selected.modelAssetId = id;
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedModelOrientation(const glm::vec3 &eulerDegrees)
{
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }
    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked) {
        return false;
    }
    PushUndoSnapshot();
    selected.modelOrientationEuler = eulerDegrees;
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedModelOffset(const glm::vec3& position,
                                         const glm::vec3& eulerDegrees,
                                         const glm::vec3& scale)
{
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }
    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked) {
        return false;
    }
    PushUndoSnapshot();
    selected.modelOffsetPosition = position;
    selected.modelOffsetPosition.x = FiniteClamp(selected.modelOffsetPosition.x, 0.0f,
        -kMaxSceneCoordinate, kMaxSceneCoordinate);
    selected.modelOffsetPosition.y = FiniteClamp(selected.modelOffsetPosition.y, 0.0f,
        -kMaxSceneCoordinate, kMaxSceneCoordinate);
    selected.modelOffsetPosition.z = FiniteClamp(selected.modelOffsetPosition.z, 0.0f,
        -kMaxSceneCoordinate, kMaxSceneCoordinate);
    selected.modelOrientationEuler = eulerDegrees;
    selected.modelOffsetScale = scale;
    float* offsetScales[] = {&selected.modelOffsetScale.x, &selected.modelOffsetScale.y,
        &selected.modelOffsetScale.z};
    for (float* valuePtr : offsetScales) {
        float& value = *valuePtr;
        if (!std::isfinite(value)) value = 1.0f;
        const float sign = value < 0.0f ? -1.0f : 1.0f;
        value = sign * std::clamp(std::abs(value), 0.0001f, kMaxSceneScale);
    }
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedMaterialAsset(
    const std::string &path, engine::AssetHandle id)
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
    selected.materialAssetId = id;
    selected.materialParameterOverrides.clear();
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedDecalSettings(float opacity, float surfaceOffset) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size()))
        return false;
    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (!selected.decal || selected.locked) return false;
    Transform* transform = SelectedTransform();
    if (!transform) return false;
    PushUndoSnapshot();
    selected.decalOpacity = std::clamp(opacity, 0.0f, 1.0f);
    const float clampedOffset = std::clamp(surfaceOffset, 0.001f, 0.08f);
    const float offsetDelta = clampedOffset - selected.decalSurfaceOffset;
    transform->position +=
        (transform->rotation * glm::vec3(0.0f, 1.0f, 0.0f)) * offsetDelta;
    selected.decalSurfaceOffset = clampedOffset;
    m_dirty = true;
    return true;
}

void EditorScene::AddDecal(const engine::Mesh& plane, const glm::vec3& position,
                           const glm::vec3& surfaceNormal, const glm::vec2& size,
                           float rotationDegrees, float surfaceOffset, float opacity,
                           const std::string& materialPath) {
    PushUndoSnapshot();
    const glm::vec3 normal = glm::length(surfaceNormal) > 0.0001f
        ? glm::normalize(surfaceNormal) : glm::vec3(0, 1, 0);
    Transform transform;
    transform.position = position + normal * std::clamp(surfaceOffset, 0.001f, 0.08f);
    transform.scale = glm::vec3(std::clamp(size.x, 0.05f, 100.0f), 1.0f,
                                std::clamp(size.y, 0.05f, 100.0f));
    const glm::quat align = glm::rotation(glm::vec3(0, 1, 0), normal);
    const glm::quat spin = glm::angleAxis(glm::radians(rotationDegrees), normal);
    transform.rotation = glm::normalize(spin * align);
    const std::string name = "Decal_" + std::to_string(m_nextCubeNumber++);
    CreateObject(name, Primitive::Plane, plane, transform, glm::vec3(1.0f));
    Object& object = m_objects.back();
    object.decal = true;
    object.decalOpacity = std::clamp(opacity, 0.0f, 1.0f);
    object.decalSurfaceOffset = std::clamp(surfaceOffset, 0.001f, 0.08f);
    object.materialAssetPath = materialPath;
    object.editorLayer = "Decals";
    m_selectedIndex = static_cast<int>(m_objects.size()) - 1;
    m_dirty = true;
}

int EditorScene::SetSelectedMaterialAssetToSelection(
    const std::string& path, engine::AssetHandle id)
{
    EnsureSelectionValid();
    int assignable = 0;
    for (int index : m_selectedIndices) {
        if (index < 0 || index >= static_cast<int>(m_objects.size())) continue;
        if (!m_objects[static_cast<std::size_t>(index)].locked) ++assignable;
    }
    if (assignable == 0) return 0;

    PushUndoSnapshot();
    for (int index : m_selectedIndices) {
        if (index < 0 || index >= static_cast<int>(m_objects.size())) continue;
        Object& object = m_objects[static_cast<std::size_t>(index)];
        if (object.locked) continue;
        object.materialAssetPath = path;
        object.materialAssetId = id;
        object.materialParameterOverrides.clear();
    }
    m_dirty = true;
    return assignable;
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

bool EditorScene::SetSelectedAnimationSources(const std::vector<AnimationSource>& sources) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }
    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked) {
        return false;
    }
    PushUndoSnapshot();
    selected.animationSources = sources;
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedModelAttachments(const std::vector<ModelAttachment>& attachments) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }
    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked) {
        return false;
    }
    PushUndoSnapshot();
    selected.modelAttachments = attachments;
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedFootIK(const engine::ecs::FootIKSettings& footIK) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }
    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked) {
        return false;
    }
    PushUndoSnapshot();
    selected.footIK = footIK;
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedCharacterAssetPath(
    const std::string& path, engine::AssetHandle id) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }
    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    selected.characterAssetPath = path;   // metadata; no undo snapshot needed
    selected.characterAssetId = id;
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedPrefabAssetPath(
    const std::string& path, engine::AssetHandle id) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }
    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    selected.prefabAssetPath = path;   // metadata; no undo snapshot needed
    selected.prefabAssetId = id;
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
        for (AnimationStateTransition::Condition& condition
             : transition.additionalConditions) {
            const int compare = std::clamp(static_cast<int>(condition.compare), 0, 5);
            condition.compare = static_cast<AnimationStateTransition::Compare>(compare);
        }
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
    m_environment.shadowDistance = std::clamp(m_environment.shadowDistance, 10.0f, 5000.0f);
    m_environment.atmosphereRayleigh = std::clamp(m_environment.atmosphereRayleigh,0.0f,8.0f);
    m_environment.atmosphereRayleighHeight = std::clamp(m_environment.atmosphereRayleighHeight,1.0f,32.0f);
    m_environment.atmosphereMie = std::clamp(m_environment.atmosphereMie,0.0f,8.0f);
    m_environment.atmosphereMieHeight = std::clamp(m_environment.atmosphereMieHeight,0.1f,8.0f);
    m_environment.atmosphereMieAnisotropy = std::clamp(m_environment.atmosphereMieAnisotropy,-0.94f,0.94f);
    m_environment.atmosphereOzone = std::clamp(m_environment.atmosphereOzone,0.0f,4.0f);
    m_environment.volumetricAnisotropy = std::clamp(m_environment.volumetricAnisotropy,-0.94f,0.94f);
    m_environment.volumetricMaxDistance = std::max(m_environment.volumetricMaxDistance,1.0f);
    m_environment.environmentQuality = std::clamp(m_environment.environmentQuality,0,3);
    if(m_environment.exposureMinEV>m_environment.exposureMaxEV)
        std::swap(m_environment.exposureMinEV,m_environment.exposureMaxEV);
    m_environment.colorGamma=glm::max(m_environment.colorGamma,glm::vec3(0.01f));
    m_environment.colorGain=glm::max(m_environment.colorGain,glm::vec3(0.0f));
    m_dirty = true;
}

void EditorScene::SetGameModeSettings(const GameModeSettings& settings) {
    PushUndoSnapshot();
    m_gameMode = settings;
    m_gameMode.cameraMode = std::clamp(m_gameMode.cameraMode, 0, 3);
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
    const float axisLength = glm::length(selected.angularVelocityAxis);
    if (!std::isfinite(axisLength) || axisLength < 0.000001f)
        selected.angularVelocityAxis = glm::vec3(0.0f, 1.0f, 0.0f);
    else
        selected.angularVelocityAxis /= axisLength;
    selected.angularVelocityRadians = FiniteClamp(radiansPerSecond, 0.0f, -1000.0f, 1000.0f);
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
            selected.collider = Collider::MakePlane(glm::vec3(0.0f, 1.0f, 0.0f), 0.0f);
        } else if (selected.primitive == Primitive::Sphere && selected.modelAssetPath.empty() && transform) {
            selected.collider = Collider::MakeSphere(0.5f);
        } else if (selected.primitive == Primitive::Capsule && selected.modelAssetPath.empty() && transform) {
            selected.collider = Collider::MakeCapsuleFromHeight(0.4f, 1.8f);
        } else if (selected.primitive == Primitive::Cylinder && selected.modelAssetPath.empty() && transform) {
            selected.collider = Collider::MakeCylinder(0.5f, 1.0f);
        } else if (selected.primitive == Primitive::Cone && selected.modelAssetPath.empty() && transform) {
            selected.collider = Collider::MakeCone(0.5f, 1.0f);
        } else if (selected.primitive == Primitive::Pyramid && selected.modelAssetPath.empty() && transform) {
            selected.collider = Collider::MakePyramid(glm::vec3(0.5f));
        } else if (selected.primitive == Primitive::Torus && selected.modelAssetPath.empty() && transform) {
            selected.collider = Collider::MakeTorus(0.35f, 0.15f);
        } else if (selected.primitive == Primitive::Staircase && selected.modelAssetPath.empty() && transform) {
            selected.collider = Collider::MakeStaircase(glm::vec3(0.5f), 6);
        } else if (transform) {
            selected.collider = Collider::MakeBox(glm::vec3(0.5f));
        }
        NormalizeColliderValues(selected.collider);
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
    NormalizeColliderValues(selected.collider);
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
        NormalizeControllerValues(selected.playerController);
        selected.playerController.capsuleRadius =
            std::max(selected.playerController.capsuleRadius, 0.01f);
        selected.playerController.capsuleHeight = std::max(
            selected.playerController.capsuleHeight,
            selected.playerController.capsuleRadius * 2.0f);
        selected.colliderEnabled = true;
        selected.collider = engine::ecs::Collider::MakeCapsuleFromHeight(
            selected.playerController.capsuleRadius,
            selected.playerController.capsuleHeight);
        selected.collider.inheritTransformScale = false;
        selected.collider.isTrigger = true;
        selected.collider.layer = engine::ecs::CollisionLayer::Player;
        selected.collider.mask = engine::ecs::CollisionLayer::All;
        NormalizeColliderValues(selected.collider);
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
    NormalizeControllerValues(safe);
    if (safe.firstPerson && safe.cameraMode == 0) safe.cameraMode = 1;
    safe.cameraMode = std::clamp(safe.cameraMode, 0, 3);
    safe.firstPerson = safe.cameraMode == 1;
    safe.cameraDistance = std::max(safe.cameraDistance, 0.0f);
    safe.isometricPitch = std::clamp(safe.isometricPitch, -89.0f, 89.0f);
    safe.isometricDistance = std::max(safe.isometricDistance, 0.0f);
    safe.cameraProbeRadius = std::max(safe.cameraProbeRadius, 0.0f);
    safe.cameraCollisionPadding = std::max(safe.cameraCollisionPadding, 0.0f);
    safe.cameraReturnSpeed = std::max(safe.cameraReturnSpeed, 0.0f);
    safe.capsuleRadius = std::max(safe.capsuleRadius, 0.01f);
    safe.capsuleHeight = std::max(safe.capsuleHeight, safe.capsuleRadius * 2.0f);
    safe.shoulderOffset = std::max(safe.shoulderOffset, 0.0f);
    safe.shoulderSwitchSpeed = std::max(safe.shoulderSwitchSpeed, 0.0f);
    safe.lockOnRange = std::max(safe.lockOnRange, 0.0f);
    safe.lockOnViewAngle = std::clamp(safe.lockOnViewAngle, 0.0f, 180.0f);
    safe.lockOnTrackingSpeed = std::max(safe.lockOnTrackingSpeed, 0.0f);
    selected.playerControllerEnabled = true;
    selected.playerController = safe;
    selected.colliderEnabled = true;
    selected.collider = engine::ecs::Collider::MakeCapsuleFromHeight(safe.capsuleRadius, safe.capsuleHeight);
    selected.collider.inheritTransformScale = false;
    selected.collider.isTrigger = true;
    selected.collider.layer = engine::ecs::CollisionLayer::Player;
    selected.collider.mask = engine::ecs::CollisionLayer::All;
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

bool EditorScene::SetSelectedRagdollEnabled(bool enabled) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }
    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked || selected.ragdollEnabled == enabled) return false;
    PushUndoSnapshot();
    selected.ragdollEnabled = enabled;
    selected.ragdoll.active = false;
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedRagdoll(const engine::Ragdoll& ragdoll) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }
    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked) return false;
    PushUndoSnapshot();
    selected.ragdollEnabled = true;
    selected.ragdoll = ragdoll;
    selected.ragdoll.active = false;
    selected.ragdoll.parts.clear();
    selected.ragdoll.boneDrivers.clear();
    selected.ragdoll.boneFromBody.clear();
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedScript(const std::string& className, const std::string& path, bool enabled) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }

    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    // Object locking protects placement/scene-authoring edits. Script metadata is
    // deliberately still editable so a locked character can receive gameplay logic.

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
        selected.scriptExecutionOrder = 0;
        selected.scriptDependencies.clear();
    }
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedScriptScheduling(
    int executionOrder, const std::vector<std::string>& dependencies) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size()))
        return false;
    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    executionOrder = std::clamp(executionOrder, -10000, 10000);
    std::vector<std::string> normalized;
    for (const std::string& dependency : dependencies) {
        if (dependency.empty()
            || std::find(normalized.begin(), normalized.end(), dependency) != normalized.end())
            continue;
        normalized.push_back(dependency);
    }
    if (selected.scriptExecutionOrder == executionOrder
        && selected.scriptDependencies == normalized) return false;
    PushUndoSnapshot();
    selected.scriptExecutionOrder = executionOrder;
    selected.scriptDependencies = std::move(normalized);
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedAdditionalScripts(const std::vector<ScriptBinding>& scripts) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }
    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    PushUndoSnapshot();
    selected.additionalScripts = scripts;
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
    CreateObject("ParticleSystem_" + std::to_string(m_nextCubeNumber++), Primitive::Empty,
                 placeholderMesh, transform, glm::vec3(0.35f, 0.55f, 1.0f));
    m_selectedIndex = static_cast<int>(m_objects.size()) - 1;
    // Empty is a selectable editor actor with no runtime mesh. Keep it visible
    // so scene export does not discard the particle component.
    m_objects.back().visible = true;
    m_particleEditOpen = true;
    SetSelectedParticleSystem(true, settings);
    m_particleEditOpen = false;
    m_objects.back().particleAssetPath = assetPath;
    m_objects.back().particleAssetId = settings.assetId;
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
    selected.particleAssetId = settings.assetId;
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
        object.particleAssetId = settings.assetId;
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
    if (selected.scriptEnabled == enabled) {
        return false;
    }
    // An enabled native-script component without a class cannot be constructed
    // by the runtime loader. Reject it without adding a misleading undo entry or
    // dirtying the scene; the Inspector can commit its buffered class first.
    if (enabled && selected.scriptClassName.empty()) {
        return false;
    }

    PushUndoSnapshot();
    selected.scriptEnabled = enabled;
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedNavAgent(bool enabled, float speed, float maxForce,
                                      float reachRadius, float repathInterval,
                                      const std::string& targetName, float visionRange,
                                      float visionHalfAngle, float hearingRange,
                                      float squadAlertRadius, float squadForgetTime) {
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
    selected.navAgentHearingRange = std::max(hearingRange, 0.0f);
    selected.navAgentSquadAlertRadius = std::max(squadAlertRadius, 0.0f);
    selected.navAgentSquadForgetTime = std::max(squadForgetTime, 0.1f);
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

bool EditorScene::SetSelectedNavAgentMovement(engine::ai::AiMovementMode mode,
                                              float gravity, float maxFallSpeed,
                                              float groundProbe, float stepHeight,
                                              float maxSlopeDegrees) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }
    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked) return false;
    PushUndoSnapshot();
    selected.navMovementMode = mode == engine::ai::AiMovementMode::Flying
        ? engine::ai::AiMovementMode::Flying : engine::ai::AiMovementMode::Grounded;
    selected.navMovementGravity = std::min(gravity, 0.0f);
    selected.navMovementMaxFallSpeed = std::max(maxFallSpeed, 0.0f);
    selected.navMovementGroundProbe = std::max(groundProbe, 0.02f);
    selected.navMovementStepHeight = std::max(stepHeight, 0.0f);
    selected.navMovementMaxSlope = std::clamp(maxSlopeDegrees, 0.0f, 89.0f);
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedTerrain(bool enabled, int res, float size, float maxHeight,
                                     int seed, int octaves, float frequency) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }
    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked) {
        return false;
    }
    PushUndoSnapshot();
    selected.isTerrain = enabled;
    selected.terrainRes = std::clamp(res, 8, 1024);
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
        if (engine::ecs::Transform* t = m_registry.TryGet<engine::ecs::Transform>(selected.entity)) {
            t->scale = glm::vec3(1.0f);
        }
    }
    m_dirty = true;
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

bool EditorScene::SetSelectedWaterWaves(float seaHeight, float seaChoppy, float seaSpeed,
                                        float seaFreq, float foam, int waterType) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }
    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked) {
        return false;
    }
    PushUndoSnapshot();
    selected.isWater = true;
    selected.waterSeaHeight = std::max(seaHeight, 0.0f);
    selected.waterSeaChoppy = std::clamp(seaChoppy, 0.0f, 10.0f);
    selected.waterSeaSpeed  = std::max(seaSpeed, 0.0f);
    selected.waterSeaFreq   = std::clamp(seaFreq, 0.01f, 1.0f);
    selected.waterFoam      = std::clamp(foam, 0.0f, 4.0f);
    selected.waterType      = waterType;
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedWaterFlowSpline(const std::string& splineName) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) return false;
    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked) return false;
    PushUndoSnapshot();
    selected.waterFlowSpline = splineName;
    for (const Object& spline : m_objects) {
        if (!spline.isSpline || spline.name != splineName || spline.splinePoints.empty()) continue;
        glm::vec3 boundsMin = spline.splinePoints.front();
        glm::vec3 boundsMax = boundsMin;
        for (const glm::vec3& point : spline.splinePoints) {
            boundsMin = glm::min(boundsMin, point);
            boundsMax = glm::max(boundsMax, point);
        }
        if (Transform* transform = m_registry.TryGet<Transform>(selected.entity))
            transform->position = (boundsMin + boundsMax) * 0.5f;
        break;
    }
    m_dirty = true;
    return true;
}

void EditorScene::SyncSplineComponent(Object& object) {
    if (!object.isSpline) {
        m_registry.Remove<engine::ecs::SplineComponent>(object.entity);
        return;
    }
    engine::ecs::SplineComponent* component =
        m_registry.TryGet<engine::ecs::SplineComponent>(object.entity);
    if (!component) {
        component = &m_registry.Add<engine::ecs::SplineComponent>(
            object.entity, engine::ecs::SplineComponent{});
    }
    component->points = object.splinePoints;
    component->rotations = object.splinePointRotations;
    component->rotations.resize(component->points.size(), glm::vec3(0.0f));
    component->closed = object.splineClosed;
    ++component->revision;
}

void EditorScene::SyncFoliageComponent(Object& object) {
    if (!object.isFoliage) {
        m_registry.Remove<engine::ecs::FoliageComponent>(object.entity);
        return;
    }
    engine::ecs::FoliageComponent* component =
        m_registry.TryGet<engine::ecs::FoliageComponent>(object.entity);
    if (!component) {
        component = &m_registry.Add<engine::ecs::FoliageComponent>(
            object.entity, engine::ecs::FoliageComponent{});
    }
    component->assetPath = object.foliageAssetPath;
    component->assetId = object.foliageAssetId;
    component->instances = object.foliageInstances;
    component->visible = object.visible;
    ++component->revision;
}

bool EditorScene::SetSelectedFoliageAsset(const std::string& path,
                                          engine::AssetHandle id) {
    if (m_selectedIndex < 0) return false;
    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (!selected.isFoliage) return false;
    PushUndoSnapshot();
    selected.foliageAssetPath = path;
    selected.foliageAssetId = id;
    // Types are resolved afresh by RuntimeAssetManager, but painted transforms remain.
    if (auto* component = m_registry.TryGet<engine::ecs::FoliageComponent>(selected.entity))
        component->types.clear();
    SyncFoliageComponent(selected);
    m_dirty = true;
    return true;
}

bool EditorScene::AddSelectedFoliageInstance(
    const glm::vec3& worldPosition, const glm::vec3& rotationDegrees,
    const glm::vec3& scale, std::uint32_t typeIndex) {
    if (m_selectedIndex < 0) return false;
    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (!selected.isFoliage || selected.foliageAssetPath.empty()) return false;
    const Transform* owner = m_registry.TryGet<Transform>(selected.entity);
    const glm::mat4 inverseOwner = owner ? glm::inverse(owner->Model()) : glm::mat4(1.0f);
    engine::ecs::FoliageInstance instance;
    instance.id = selected.nextFoliageInstanceId++;
    instance.typeIndex = typeIndex;
    instance.position = glm::vec3(inverseOwner * glm::vec4(worldPosition, 1.0f));
    instance.rotationDegrees = rotationDegrees;
    instance.scale = glm::max(scale, glm::vec3(0.001f));
    selected.foliageInstances.push_back(instance);
    SyncFoliageComponent(selected);
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedFoliageTerrainOwner(const std::string& terrainName) {
    if (m_selectedIndex < 0) return false;
    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (!selected.isFoliage) return false;
    if (selected.foliageTerrainOwner == terrainName) return false;
    selected.foliageTerrainOwner = terrainName;
    m_dirty = true;
    return true;
}

std::size_t EditorScene::EraseSelectedFoliageInstances(
    const glm::vec3& worldPosition, float radius) {
    if (m_selectedIndex < 0) return 0;
    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (!selected.isFoliage) return 0;
    const Transform* owner = m_registry.TryGet<Transform>(selected.entity);
    const glm::mat4 model = owner ? owner->Model() : glm::mat4(1.0f);
    const float radiusSquared = std::max(radius, 0.01f) * std::max(radius, 0.01f);
    const std::size_t before = selected.foliageInstances.size();
    selected.foliageInstances.erase(
        std::remove_if(selected.foliageInstances.begin(), selected.foliageInstances.end(),
        [&](const engine::ecs::FoliageInstance& instance) {
            const glm::vec3 world = glm::vec3(model * glm::vec4(instance.position, 1.0f));
            const glm::vec2 delta(world.x - worldPosition.x, world.z - worldPosition.z);
            return glm::dot(delta, delta) <= radiusSquared;
        }), selected.foliageInstances.end());
    const std::size_t removed = before - selected.foliageInstances.size();
    if (removed != 0) {
        SyncFoliageComponent(selected);
        m_dirty = true;
    }
    return removed;
}

bool EditorScene::SetSelectedFoliageInstance(
    std::uint32_t id, const engine::ecs::FoliageInstance& instance) {
    if (m_selectedIndex < 0) return false;
    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (!selected.isFoliage) return false;
    const auto found = std::find_if(selected.foliageInstances.begin(),
        selected.foliageInstances.end(), [id](const engine::ecs::FoliageInstance& current) {
            return current.id == id;
        });
    if (found == selected.foliageInstances.end()) return false;
    engine::ecs::FoliageInstance sanitized = instance;
    sanitized.id = id;
    sanitized.scale = glm::max(sanitized.scale, glm::vec3(0.001f));
    *found = sanitized;
    SyncFoliageComponent(selected);
    m_dirty = true;
    return true;
}

bool EditorScene::RemoveSelectedFoliageInstance(std::uint32_t id) {
    if (m_selectedIndex < 0) return false;
    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (!selected.isFoliage) return false;
    const auto found = std::find_if(selected.foliageInstances.begin(),
        selected.foliageInstances.end(), [id](const engine::ecs::FoliageInstance& instance) {
            return instance.id == id;
        });
    if (found == selected.foliageInstances.end()) return false;
    PushUndoSnapshot();
    selected.foliageInstances.erase(found);
    SyncFoliageComponent(selected);
    m_dirty = true;
    return true;
}

bool EditorScene::DuplicateSelectedFoliageInstance(std::uint32_t id,
                                                    std::uint32_t* newId) {
    if (m_selectedIndex < 0) return false;
    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (!selected.isFoliage) return false;
    const auto found = std::find_if(selected.foliageInstances.begin(),
        selected.foliageInstances.end(), [id](const engine::ecs::FoliageInstance& instance) {
            return instance.id == id;
        });
    if (found == selected.foliageInstances.end()) return false;
    PushUndoSnapshot();
    engine::ecs::FoliageInstance duplicate = *found;
    duplicate.id = selected.nextFoliageInstanceId++;
    duplicate.position += glm::vec3(0.35f, 0.0f, 0.35f);
    selected.foliageInstances.push_back(duplicate);
    SyncFoliageComponent(selected);
    m_dirty = true;
    if (newId) *newId = duplicate.id;
    return true;
}

bool EditorScene::ClearSelectedFoliageInstances() {
    if (m_selectedIndex < 0) return false;
    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (!selected.isFoliage || selected.foliageInstances.empty()) return false;
    PushUndoSnapshot();
    selected.foliageInstances.clear();
    SyncFoliageComponent(selected);
    m_dirty = true;
    return true;
}

void EditorScene::AddFoliage(const engine::Mesh& placeholderMesh)
{
    PushUndoSnapshot();
    Transform transform;
    const std::string name = "Foliage_" + std::to_string(m_nextCubeNumber++);
    CreateObject(name, Primitive::Empty, placeholderMesh, transform, glm::vec3(0.18f, 0.65f, 0.24f));
    Object& object = m_objects.back();
    object.isFoliage = true;
    SyncFoliageComponent(object);
    m_selectedIndex = static_cast<int>(m_objects.size()) - 1;
    m_dirty = true;
}

bool EditorScene::SetSelectedSpline(bool enabled, bool closed, int type) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) return false;
    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked) return false;
    PushUndoSnapshot();
    selected.isSpline = enabled;
    selected.splineClosed = closed;
    selected.splineType = type;
    if (enabled && selected.splinePoints.empty()) {
        // Seed a short 3-point path centred on the object so there's something to edit.
        const glm::vec3 c = m_registry.TryGet<engine::ecs::Transform>(selected.entity)
            ? m_registry.TryGet<engine::ecs::Transform>(selected.entity)->position : glm::vec3(0.0f);
        selected.splinePoints = {c + glm::vec3(-6.0f, 0.0f, 0.0f),
                                 c + glm::vec3(0.0f, 0.0f, 4.0f),
                                 c + glm::vec3(6.0f, 0.0f, 0.0f)};
    }
    selected.splinePointRotations.resize(selected.splinePoints.size(), glm::vec3(0.0f));
    SyncSplineComponent(selected);
    m_dirty = true;
    return true;
}

bool EditorScene::AddSelectedSplinePoint(const glm::vec3& point) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) return false;
    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked || !selected.isSpline) return false;
    PushUndoSnapshot();
    selected.splinePoints.push_back(point);
    selected.splinePointRotations.resize(selected.splinePoints.size(), glm::vec3(0.0f));
    SyncSplineComponent(selected);
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedWaterRiverWidth(float width) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) return false;
    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked || !selected.isWater) return false;
    PushUndoSnapshot();
    selected.waterRiverWidth = std::clamp(width, 0.1f, 500.0f);
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedWaterShaderPath(const std::string& path) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) return false;
    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked || !selected.isWater) return false;
    PushUndoSnapshot();
    selected.waterShaderPath = path;
    m_dirty = true;
    return true;
}

bool EditorScene::InsertSelectedSplinePoint(std::size_t index, const glm::vec3& point) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) return false;
    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked || !selected.isSpline) return false;
    index = std::min(index, selected.splinePoints.size());
    PushUndoSnapshot();
    selected.splinePointRotations.resize(selected.splinePoints.size(), glm::vec3(0.0f));
    selected.splinePoints.insert(
        selected.splinePoints.begin() + static_cast<std::ptrdiff_t>(index), point);
    selected.splinePointRotations.insert(
        selected.splinePointRotations.begin() + static_cast<std::ptrdiff_t>(index), glm::vec3(0.0f));
    SyncSplineComponent(selected);
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedSplinePoint(std::size_t index, const glm::vec3& point) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) return false;
    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked || index >= selected.splinePoints.size()) return false;
    selected.splinePoints[index] = point;   // no snapshot: called during drags
    SyncSplineComponent(selected);
    m_dirty = true;
    return true;
}

bool EditorScene::RemoveSelectedSplinePoint(std::size_t index) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) return false;
    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked || index >= selected.splinePoints.size()) return false;
    PushUndoSnapshot();
    selected.splinePoints.erase(selected.splinePoints.begin() + static_cast<std::ptrdiff_t>(index));
    if (index < selected.splinePointRotations.size()) {
        selected.splinePointRotations.erase(
            selected.splinePointRotations.begin() + static_cast<std::ptrdiff_t>(index));
    }
    SyncSplineComponent(selected);
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedSplinePointRotation(std::size_t index, const glm::vec3& degrees) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) return false;
    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked || index >= selected.splinePoints.size()) return false;
    selected.splinePointRotations.resize(selected.splinePoints.size(), glm::vec3(0.0f));
    selected.splinePointRotations[index] = degrees;
    SyncSplineComponent(selected);
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedSplinePoints(const std::vector<glm::vec3>& points) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) return false;
    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked || !selected.isSpline) return false;
    PushUndoSnapshot();
    selected.splinePoints = points;
    selected.splinePointRotations.resize(points.size(), glm::vec3(0.0f));
    SyncSplineComponent(selected);
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedSplinePointRotations(const std::vector<glm::vec3>& rotations) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) return false;
    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked || !selected.isSpline) return false;
    selected.splinePointRotations = rotations;
    selected.splinePointRotations.resize(selected.splinePoints.size(), glm::vec3(0.0f));
    SyncSplineComponent(selected);
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

bool EditorScene::SetSelectedTerrainLayerMaterial(int layer, const std::string& materialPath) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) return false;
    if (layer < 1 || layer > 5) return false;
    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked) return false;
    PushUndoSnapshot();
    selected.terrainLayerMaterials[static_cast<std::size_t>(layer - 1)] = materialPath;
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedTerrainGrass(bool enabled, float density, float height,
                                          float windStrength, float windSpeed,
                                          const glm::vec3& baseColor, const glm::vec3& tipColor,
                                          bool randomizeHeight, float minHeightScale,
                                          float maxHeightScale) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) return false;
    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked) return false;
    PushUndoSnapshot();
    selected.grassEnabled = enabled;
    selected.grassDensity = std::clamp(density, 0.05f, 40.0f);
    selected.grassHeight = std::max(height, 0.05f);
    selected.grassRandomizeHeight = randomizeHeight;
    selected.grassMinHeightScale = std::clamp(
        std::min(minHeightScale, maxHeightScale), 0.05f, 4.0f);
    selected.grassMaxHeightScale = std::clamp(
        std::max(minHeightScale, maxHeightScale),
        selected.grassMinHeightScale, 4.0f);
    selected.grassWindStrength = std::max(windStrength, 0.0f);
    selected.grassWindSpeed = std::max(windSpeed, 0.0f);
    selected.grassBaseColor = baseColor;
    selected.grassTipColor = tipColor;
    m_dirty = true;
    return true;
}

int EditorScene::EnsureActiveGrassStyleSlot() {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) return 0;
    Object& s = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    Object::GrassStyleEntry active;
    active.density = s.grassDensity;
    active.height = s.grassHeight;
    active.base = s.grassBaseColor;
    active.tip = s.grassTipColor;
    auto close = [](float a, float b) { const float d = a - b; return d < 1.0e-4f && d > -1.0e-4f; };
    for (std::size_t i = 0; i < s.terrainGrassPalette.size(); ++i) {
        const Object::GrassStyleEntry& e = s.terrainGrassPalette[i];
        if (close(e.density, active.density) && close(e.height, active.height) &&
            close(e.base.r, active.base.r) && close(e.base.g, active.base.g) && close(e.base.b, active.base.b) &&
            close(e.tip.r, active.tip.r) && close(e.tip.g, active.tip.g) && close(e.tip.b, active.tip.b)) {
            return static_cast<int>(i) + 1;   // reuse an identical style
        }
    }
    if (s.terrainGrassPalette.size() >= 250) return 1;   // palette full: reuse first
    s.terrainGrassPalette.push_back(active);
    m_dirty = true;
    return static_cast<int>(s.terrainGrassPalette.size());   // 1-based
}

std::vector<unsigned char> EditorScene::SelectedTerrainGrassStyle() {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) return {};
    return m_objects[static_cast<std::size_t>(m_selectedIndex)].terrainGrassStyle;
}

bool EditorScene::SetSelectedTerrainGrassStyle(std::vector<unsigned char> style) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) return false;
    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked) return false;
    selected.terrainGrassStyle = std::move(style);   // no undo: called during paint drags
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

bool EditorScene::SetSelectedWaterDepth(float fadeDistance, float shoreFoamWidth,
                                        float shoreFoamStrength) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size()))
        return false;
    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked) return false;
    PushUndoSnapshot();
    selected.waterDepthFadeDistance = std::max(fadeDistance, 0.05f);
    selected.waterShoreFoamWidth = std::max(shoreFoamWidth, 0.01f);
    selected.waterShoreFoamStrength = std::clamp(shoreFoamStrength, 0.0f, 4.0f);
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedWaterOptics(float refractionStrength,
                                         float reflectionRoughness,
                                         float environmentReflectionStrength,
                                         float absorptionStrength) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size()))
        return false;
    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked) return false;
    PushUndoSnapshot();
    selected.waterRefractionStrength = std::clamp(refractionStrength, 0.0f, 0.25f);
    selected.waterReflectionRoughness = std::clamp(reflectionRoughness, 0.0f, 1.0f);
    selected.waterEnvironmentReflectionStrength =
        std::clamp(environmentReflectionStrength, 0.0f, 1.0f);
    selected.waterAbsorptionStrength = std::clamp(absorptionStrength, 0.0f, 2.0f);
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedWaterEffects(float causticsStrength,
                                          float causticsScale,
                                          float maxRenderDistance,
                                          const glm::vec3& underwaterTint,
                                          float underwaterFogDensity,
                                          float underwaterDistortion,
                                          float underwaterTransitionSpeed) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size()))
        return false;
    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked) return false;
    PushUndoSnapshot();
    selected.waterCausticsStrength = std::clamp(causticsStrength, 0.0f, 4.0f);
    selected.waterCausticsScale = std::clamp(causticsScale, 0.05f, 20.0f);
    selected.waterMaxRenderDistance = std::max(maxRenderDistance, 0.0f);
    selected.waterUnderwaterTint = glm::clamp(underwaterTint, glm::vec3(0.0f), glm::vec3(4.0f));
    selected.waterUnderwaterFogDensity = std::clamp(underwaterFogDensity, 0.0f, 2.0f);
    selected.waterUnderwaterDistortion = std::clamp(underwaterDistortion, 0.0f, 0.1f);
    selected.waterUnderwaterTransitionSpeed =
        std::clamp(underwaterTransitionSpeed, 0.1f, 20.0f);
    m_dirty = true;
    return true;
}

bool EditorScene::UpdateSelectedTerrainHeightRegion(
    const std::vector<float>& heights, int resolution,
    int minI, int minJ, int maxI, int maxJ) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())
        || resolution < 2
        || heights.size() != static_cast<std::size_t>(resolution) * resolution)
        return false;
    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked) return false;
    if (selected.terrainHeights.size() != heights.size()) {
        selected.terrainHeights = heights; // one full copy on the first sculpt stroke
    } else {
        minI = std::clamp(minI, 0, resolution - 1);
        maxI = std::clamp(maxI, 0, resolution - 1);
        minJ = std::clamp(minJ, 0, resolution - 1);
        maxJ = std::clamp(maxJ, 0, resolution - 1);
        if (minI > maxI || minJ > maxJ) return false;
        const std::size_t count = static_cast<std::size_t>(maxI - minI + 1);
        for (int j = minJ; j <= maxJ; ++j) {
            const std::size_t offset = static_cast<std::size_t>(j) * resolution + minI;
            std::copy_n(heights.begin() + static_cast<std::ptrdiff_t>(offset),
                        count,
                        selected.terrainHeights.begin() + static_cast<std::ptrdiff_t>(offset));
        }
    }
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
    if (index >= selected.scriptFields.size()) {
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
    if (index >= selected.scriptFields.size()) {
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

std::size_t EditorScene::AddViewportBookmark(const ViewportBookmark& source)
{
    PushUndoSnapshot();
    ViewportBookmark bookmark = source;
    bookmark.name = bookmark.name.empty()
        ? "View_" + std::to_string(m_viewportBookmarks.size() + 1)
        : bookmark.name;
    bookmark.fov = std::clamp(bookmark.fov, 10.0f, 120.0f);
    bookmark.blendDuration = std::clamp(bookmark.blendDuration, 0.0f, 5.0f);
    m_viewportBookmarks.push_back(std::move(bookmark));
    m_dirty = true;
    return m_viewportBookmarks.size() - 1;
}

bool EditorScene::SetViewportBookmark(std::size_t index,
                                      const ViewportBookmark& source)
{
    if (index >= m_viewportBookmarks.size()) return false;
    PushUndoSnapshot();
    ViewportBookmark bookmark = source;
    bookmark.name = bookmark.name.empty() ? "View" : bookmark.name;
    bookmark.fov = std::clamp(bookmark.fov, 10.0f, 120.0f);
    bookmark.blendDuration = std::clamp(bookmark.blendDuration, 0.0f, 5.0f);
    m_viewportBookmarks[index] = std::move(bookmark);
    m_dirty = true;
    return true;
}

bool EditorScene::RemoveViewportBookmark(std::size_t index)
{
    if (index >= m_viewportBookmarks.size()) return false;
    PushUndoSnapshot();
    m_viewportBookmarks.erase(
        m_viewportBookmarks.begin() + static_cast<std::ptrdiff_t>(index));
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
    CreateObject(selectedCopy.name, selectedCopy.primitive, mesh, duplicateTransform, duplicateColor);
    m_objects.back().editorGroupId = selectedCopy.editorGroupId;
    m_objects.back().modelAssetPath = selectedCopy.modelAssetPath;
    m_objects.back().materialAssetPath = selectedCopy.materialAssetPath;
    m_objects.back().decal = selectedCopy.decal;
    m_objects.back().decalOpacity = selectedCopy.decalOpacity;
    m_objects.back().decalSurfaceOffset = selectedCopy.decalSurfaceOffset;
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
    m_objects.back().animationSources = selectedCopy.animationSources;
    m_objects.back().modelAttachments = selectedCopy.modelAttachments;
    m_objects.back().footIK = selectedCopy.footIK;
    m_objects.back().characterAssetPath = selectedCopy.characterAssetPath;
    m_objects.back().prefabAssetPath = selectedCopy.prefabAssetPath;
    m_objects.back().prefabAssetId = selectedCopy.prefabAssetId;
    m_objects.back().animationActionProfiles = selectedCopy.animationActionProfiles;
    m_objects.back().animationStates = selectedCopy.animationStates;
    m_objects.back().animationParameters = selectedCopy.animationParameters;
    m_objects.back().animationTransitions = selectedCopy.animationTransitions;
    m_objects.back().light = selectedCopy.light;
    m_objects.back().navMeshBoundsVolume = selectedCopy.navMeshBoundsVolume;
    m_objects.back().reflectionProbeEnabled = selectedCopy.reflectionProbeEnabled;
    m_objects.back().reflectionProbe = selectedCopy.reflectionProbe;
    if (selectedCopy.reflectionProbeEnabled) {
        // A duplicate is a different authored capture even if it initially reuses
        // the same baked texture. Its dirty state is evaluated independently.
        m_objects.back().reflectionProbe.stableId = engine::AssetHandle::Generate();
        m_objects.back().reflectionProbe.captureSourceHash = 0;
        m_registry.Add<engine::ecs::ReflectionProbe>(
            m_objects.back().entity, m_objects.back().reflectionProbe);
    }
    m_objects.back().postProcessVolumeEnabled=selectedCopy.postProcessVolumeEnabled;
    m_objects.back().postProcessVolume=selectedCopy.postProcessVolume;
    if(selectedCopy.postProcessVolumeEnabled){m_objects.back().postProcessVolume.stableId=engine::AssetHandle::Generate();m_registry.Add<engine::ecs::PostProcessVolume>(m_objects.back().entity,m_objects.back().postProcessVolume);}
    m_objects.back().localFogVolumeEnabled=selectedCopy.localFogVolumeEnabled;
    m_objects.back().localFogVolume=selectedCopy.localFogVolume;
    if(selectedCopy.localFogVolumeEnabled){m_objects.back().localFogVolume.stableId=engine::AssetHandle::Generate();m_registry.Add<engine::ecs::LocalFogVolume>(m_objects.back().entity,m_objects.back().localFogVolume);}
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
    m_objects.back().ragdollEnabled = selectedCopy.ragdollEnabled;
    m_objects.back().ragdoll = selectedCopy.ragdoll;
    m_objects.back().ragdoll.active = false;
    m_objects.back().ragdoll.parts.clear();
    m_objects.back().ragdoll.boneDrivers.clear();
    m_objects.back().ragdoll.boneFromBody.clear();
    m_objects.back().scriptEnabled = selectedCopy.scriptEnabled;
    m_objects.back().scriptClassName = selectedCopy.scriptClassName;
    m_objects.back().scriptPath = selectedCopy.scriptPath;
    m_objects.back().scriptFields = selectedCopy.scriptFields;
    m_objects.back().scriptExecutionOrder = selectedCopy.scriptExecutionOrder;
    m_objects.back().scriptDependencies = selectedCopy.scriptDependencies;
    m_objects.back().additionalScripts = selectedCopy.additionalScripts;
    m_objects.back().audioSourceEnabled = selectedCopy.audioSourceEnabled;
    m_objects.back().audioAssetPath = selectedCopy.audioAssetPath;
    m_objects.back().audioAssetId = selectedCopy.audioAssetId;
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
    m_objects.back().particleAssetId = selectedCopy.particleAssetId;
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
    m_objects.back().navAgentHearingRange = selectedCopy.navAgentHearingRange;
    m_objects.back().navAgentSquadAlertRadius = selectedCopy.navAgentSquadAlertRadius;
    m_objects.back().navAgentSquadForgetTime = selectedCopy.navAgentSquadForgetTime;
    m_objects.back().navAgentBrainAsset = selectedCopy.navAgentBrainAsset;
    m_objects.back().navAgentBrainAssetId =
        selectedCopy.navAgentBrainAssetId;
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
    // Delete every selected (unlocked) object. Erase highest index first so the earlier
    // indices stay valid as the vector shrinks.
    EnsureSelectionValid();
    std::vector<int> indices;
    for (int index : m_selectedIndices) {
        if (index >= 0 && index < static_cast<int>(m_objects.size())
            && !m_objects[static_cast<std::size_t>(index)].locked) {
            indices.push_back(index);
        }
    }
    if (indices.empty()) {
        return false;
    }
    // Cascade: deleting a terrain also deletes the grass/foliage painted onto it, so
    // grass "goes with the ground" it belongs to. Collect the names AND world XZ
    // footprints of the terrains being removed. A foliage object is removed if either
    // (1) it is bound to a deleted terrain by foliageTerrainOwner (set at paint time),
    // or (2) the majority of its instances sit within a deleted terrain's footprint
    // (a fallback so grass painted before the owner-link existed is still cleaned up).
    struct DeletedTerrainFootprint { float minX, minZ, maxX, maxZ; };
    std::vector<std::string> deletedTerrainNames;
    std::vector<DeletedTerrainFootprint> deletedTerrainFootprints;
    for (int index : indices) {
        const Object& obj = m_objects[static_cast<std::size_t>(index)];
        if (!obj.isTerrain) continue;
        deletedTerrainNames.push_back(obj.name);
        const Transform* tt = m_registry.TryGet<Transform>(obj.entity);
        const glm::vec3 base = tt ? tt->position : glm::vec3(0.0f);
        const glm::vec3 scl = tt ? glm::abs(tt->scale) : glm::vec3(1.0f);
        // Matches TerrainSurfaceY: terrain spans [base, base + terrainSize * scale].
        DeletedTerrainFootprint f;
        f.minX = base.x;
        f.maxX = base.x + obj.terrainSize * std::max(scl.x, 1e-4f);
        f.minZ = base.z;
        f.maxZ = base.z + obj.terrainSize * std::max(scl.z, 1e-4f);
        deletedTerrainFootprints.push_back(f);
    }
    if (!deletedTerrainNames.empty()) {
        for (int i = 0; i < static_cast<int>(m_objects.size()); ++i) {
            const Object& obj = m_objects[static_cast<std::size_t>(i)];
            if (!obj.isFoliage || obj.locked) continue;
            if (std::find(indices.begin(), indices.end(), i) != indices.end()) continue;

            bool remove = false;
            if (!obj.foliageTerrainOwner.empty()
                && std::find(deletedTerrainNames.begin(), deletedTerrainNames.end(),
                             obj.foliageTerrainOwner) != deletedTerrainNames.end()) {
                remove = true;
            }
            if (!remove && !obj.foliageInstances.empty()) {
                const Transform* ft = m_registry.TryGet<Transform>(obj.entity);
                const glm::mat4 fm = ft ? ft->Model() : glm::mat4(1.0f);
                std::size_t inside = 0, total = 0;
                for (const engine::ecs::FoliageInstance& inst : obj.foliageInstances) {
                    if (!inst.enabled) continue;
                    ++total;
                    const glm::vec3 wp = glm::vec3(fm * glm::vec4(inst.position, 1.0f));
                    for (const DeletedTerrainFootprint& f : deletedTerrainFootprints) {
                        if (wp.x >= f.minX && wp.x <= f.maxX
                            && wp.z >= f.minZ && wp.z <= f.maxZ) { ++inside; break; }
                    }
                }
                if (total > 0 && inside * 2 >= total) remove = true;
            }
            if (remove) indices.push_back(i);
        }
    }
    std::sort(indices.begin(), indices.end(), [](int a, int b) { return a > b; });
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());

    PushUndoSnapshot();
    for (int index : indices) {
        m_registry.Destroy(m_objects[static_cast<std::size_t>(index)].entity);
        m_objects.erase(m_objects.begin() + static_cast<std::ptrdiff_t>(index));
    }

    m_selectedIndices.clear();
    m_selectedIndex = -1;
    m_dirty = true;
    return true;
}

engine::ecs::Entity EditorScene::CreateObject(const std::string & name, Primitive primitive, const engine::Mesh & mesh, const engine::ecs::Transform & transform, const glm::vec3 & color)
{
    Entity entity = m_registry.Create();
    m_registry.Add<Transform>(entity, transform);
    m_registry.Add<MeshRenderer>(entity, MeshRenderer{&mesh, color});
    m_objects.push_back({entity, MakeUniqueHierarchyName(name), primitive});
    return entity;
}

EditorScene::Snapshot EditorScene::CaptureSnapshot()
{
    Snapshot snapshot;
    snapshot.selectedIndex = m_selectedIndex;
    snapshot.hierarchySelection = m_hierarchySelection;
    snapshot.selectedGroupId = m_selectedGroupId;
    snapshot.groups = m_groups;
    snapshot.nextGroupId = m_nextGroupId;
    snapshot.nextCubeNumber = m_nextCubeNumber;
    snapshot.joints = m_joints;
    snapshot.cameraPresets = m_cameraPresets;
    snapshot.cameraSequences = m_cameraSequences;
    snapshot.viewportBookmarks = m_viewportBookmarks;
    snapshot.environment = m_environment;
    snapshot.gameMode = m_gameMode;

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
        if (object.reflectionProbeEnabled) {
            m_registry.Add<engine::ecs::ReflectionProbe>(
                entity, object.reflectionProbe);
        }
        if (object.postProcessVolumeEnabled)
            m_registry.Add<engine::ecs::PostProcessVolume>(entity, object.postProcessVolume);
        if (object.localFogVolumeEnabled)
            m_registry.Add<engine::ecs::LocalFogVolume>(entity, object.localFogVolume);
        m_objects.push_back(object);
        if (m_objects.back().isSpline) SyncSplineComponent(m_objects.back());
        if (m_objects.back().isFoliage) SyncFoliageComponent(m_objects.back());
    }

    m_selectedIndex = snapshot.selectedIndex;
    if (m_selectedIndex >= static_cast<int>(m_objects.size())) {
        m_selectedIndex = m_objects.empty() ? -1 : static_cast<int>(m_objects.size()) -1;
    }
    m_nextCubeNumber = snapshot.nextCubeNumber;
    m_groups = snapshot.groups;
    m_nextGroupId = snapshot.nextGroupId;
    m_hierarchySelection = snapshot.hierarchySelection;
    m_selectedGroupId = snapshot.selectedGroupId;
    if (m_hierarchySelection == HierarchySelectionType::Group
        && !GroupExists(m_selectedGroupId)) Deselect();
    m_joints = snapshot.joints;
    m_cameraPresets = snapshot.cameraPresets;
    m_cameraSequences = snapshot.cameraSequences;
    m_viewportBookmarks = snapshot.viewportBookmarks;
    m_environment = snapshot.environment;
    m_gameMode = snapshot.gameMode;
}

void EditorScene::PushUndoSnapshot()
{
    if (m_undoSuppressed) {
        return;
    }
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
    m_assetId = {};
    m_objects.clear();
    m_groups.clear();
    m_joints.clear();
    m_cameraPresets.clear();
    m_cameraSequences.clear();
    m_viewportBookmarks.clear();
    m_environment = Environment{};
    m_gameMode = GameModeSettings{};
    m_undoStack.clear();
    m_redoStack.clear();
    m_selectedIndex = -1;
    m_hierarchySelection = HierarchySelectionType::None;
    m_selectedGroupId = kRootGroupId;
    m_nextGroupId = 1;
    m_nextCubeNumber = 1;
    m_dirty = false;
    m_transformEditOpen = false;
    m_particleEditOpen = false;
}
