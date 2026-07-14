#include "EditorScene.h"

#include <engine/graphics/Mesh.h>

#include <fstream>
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

const char* PrimitiveName(EditorScene::Primitive primitive) {
    switch (primitive) {
    case EditorScene::Primitive::Plane: return "Plane";
    case EditorScene::Primitive::Cube: return "Cube";
    case EditorScene::Primitive::Sphere: return "Sphere";
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
    }
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
                            const engine::Mesh& plane, const engine::Mesh& sphere){
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

void EditorScene::BuildDefault(const engine::Mesh & cube, const engine::Mesh & plane, const engine::Mesh & sphere)
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

    out << "3DGEditorScene 36\n";
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
        << (m_environment.selectedPhysicsGuideOnly ? 1 : 0) << '\n';
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
            << object.collider.halfExtents.x << ' ' << object.collider.halfExtents.y << ' ' << object.collider.halfExtents.z << ' '
            << object.collider.planeNormal.x << ' ' << object.collider.planeNormal.y << ' ' << object.collider.planeNormal.z << ' '
            << object.collider.planeOffset << ' '
            << object.collider.restitution << ' '
            << object.collider.friction << ' '
            << (object.collider.isTrigger ? 1 : 0) << ' '
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

    if (markClean) {
        m_dirty = false;
        ClearHistory();
    }

    return true;
}

bool EditorScene::Load(const std::string & path, const engine::Mesh & cube, const engine::Mesh & plane, const engine::Mesh & sphere, std::string * error)
{
    std::ifstream in(path);
    if (!in) {
        if (error) *error = "Could not open scene file for reading.";
        return false;
    }

    std::string magic;
    int version = 0;
    in >> magic >> version;
    if (magic != "3DGEditorScene" ||(version < 1 || version > 36)) {
        if (error) *error = "Scene file has an unknown format.";
        return false;
    }

    Clear();

    std::string recordType;
    while (in >> recordType) {
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
        int playerControllerEnabled = 0;
        int playerFirstPerson = 0;
        PlayerControllerSettings playerController;
        int healthEnabled = 0;
        engine::Health health;
        int healthAlive = health.alive ? 1 : 0;
        int scriptEnabled = 0;
        std::string scriptClassName;
        std::string scriptPath;
        std::vector<ScriptField> scriptFields;

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
            if (version >= 24) {
                in >> collider.halfHeight;
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
            if (colliderShape == static_cast<int>(engine::ecs::ColliderShape::Plane)) {
                collider.shape = engine::ecs::ColliderShape::Plane;
            } else if (colliderShape == static_cast<int>(engine::ecs::ColliderShape::Box)) {
                collider.shape = engine::ecs::ColliderShape::Box;
            } else {
                collider.shape = engine::ecs::ColliderShape::Sphere;
            }
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

        if (!in || !ParsePrimitive(primitiveName, &primitive)) {
            if (error) *error = "Scene file contains an invalid object record.";
            Clear();
            return false;
        }

        CreateObject(name, primitive, MeshFor(primitive, cube, plane, sphere), transform, color);
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
        m_objects.back().playerControllerEnabled = playerControllerEnabled != 0;
        m_objects.back().playerController = playerController;
        m_objects.back().healthEnabled = healthEnabled != 0;
        m_objects.back().health = health;
        m_objects.back().scriptEnabled = scriptEnabled != 0;
        m_objects.back().scriptClassName = scriptClassName;
        m_objects.back().scriptPath = scriptPath;
        m_objects.back().scriptFields = scriptFields;
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

bool EditorScene::Undo(const engine::Mesh & cube, const engine::Mesh & plane, const engine::Mesh & sphere)
{
    if (m_undoStack.empty()) {
        return false;
    }

    m_redoStack.push_back(CaptureSnapshot());
    RestoreSnapshot(m_undoStack.back(), cube, plane, sphere);
    m_undoStack.pop_back();
    m_dirty = true;
    m_transformEditOpen = false;
    return true;
}

bool EditorScene::Redo(const engine::Mesh & cube, const engine::Mesh & plane, const engine::Mesh & sphere)
{
    if (m_redoStack.empty()) {
        return false;
    }

    m_undoStack.push_back(CaptureSnapshot());
    RestoreSnapshot(m_redoStack.back(), cube, plane, sphere);
    m_redoStack.pop_back();
    m_dirty = true;
    m_transformEditOpen = false;
    return true;
}

EditorScene::Snapshot EditorScene::CreateSnapshot()
{
    return CaptureSnapshot();
}

void EditorScene::RestoreFromSnapshot(const Snapshot &snapshot, const engine::Mesh &cube, const engine::Mesh &plane, const engine::Mesh &sphere)
{
    RestoreSnapshot(snapshot, cube, plane, sphere);
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
    CreateObject("Plane_" + std::to_string(m_nextCubeNumber++), Primitive::Sphere, sphere, transform, color);
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
                                           TriggerActionMode exitRotatorAction) {
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
        && selected.triggerExitRotatorAction == exitRotatorAction) {
        return false;
    }

    PushUndoSnapshot();
    selected.triggerTargetName = targetName;
    selected.triggerEnterMoverAction = enterMoverAction;
    selected.triggerEnterRotatorAction = enterRotatorAction;
    selected.triggerExitMoverAction = exitMoverAction;
    selected.triggerExitRotatorAction = exitRotatorAction;
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
    selected.playerControllerEnabled = true;
    selected.playerController = settings;
    selected.colliderEnabled = true;
    selected.collider = engine::ecs::Collider::MakeCapsuleFromHeight(settings.capsuleRadius, settings.capsuleHeight);
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

bool EditorScene::DuplicateSelected(const engine::Mesh & cube, const engine::Mesh & plane, const engine::Mesh& sphere)
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

    const engine::Mesh& mesh = MeshFor(selectedCopy.primitive, cube, plane, sphere);
    CreateObject(selectedCopy.name + "_Copy", selectedCopy.primitive, mesh, duplicateTransform, duplicateColor);
    m_objects.back().modelAssetPath = selectedCopy.modelAssetPath;
    m_objects.back().materialAssetPath = selectedCopy.materialAssetPath;
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
    m_objects.back().playerControllerEnabled = selectedCopy.playerControllerEnabled;
    m_objects.back().playerController = selectedCopy.playerController;
    m_objects.back().healthEnabled = selectedCopy.healthEnabled;
    m_objects.back().health = selectedCopy.health;
    m_objects.back().scriptEnabled = selectedCopy.scriptEnabled;
    m_objects.back().scriptClassName = selectedCopy.scriptClassName;
    m_objects.back().scriptPath = selectedCopy.scriptPath;
    m_objects.back().scriptFields = selectedCopy.scriptFields;
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

void EditorScene::RestoreSnapshot(const Snapshot & snapshot, const engine::Mesh & cube, const engine::Mesh & plane, const engine::Mesh& sphere)
{
    m_registry = engine::ecs::Registry{};
    m_objects.clear();

    for (const ObjectSnapshot& objectSnapshot : snapshot.objects) {
        Entity entity = m_registry.Create();
        const engine::Mesh& mesh = MeshFor(objectSnapshot.object.primitive, cube, plane, sphere);
        m_registry.Add<Transform>(entity, objectSnapshot.transform);
        m_registry.Add<MeshRenderer>(entity, MeshRenderer{&mesh, objectSnapshot.color});

        Object object = objectSnapshot.object;
        object.entity = entity;
        m_objects.push_back(object);
    }

    m_selectedIndex = snapshot.selectedIndex;
    if (m_selectedIndex >= static_cast<int>(m_objects.size())) {
        m_selectedIndex = m_objects.empty() ? -1 : static_cast<int>(m_objects.size()) -1;
    }
    m_nextCubeNumber = snapshot.nextCubeNumber;
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
}

void EditorScene::Clear()
{
    m_registry = engine::ecs::Registry{};
    m_objects.clear();
    m_undoStack.clear();
    m_redoStack.clear();
    m_selectedIndex = -1;
    m_nextCubeNumber = 1;
    m_dirty = false;
    m_transformEditOpen = false;
}
