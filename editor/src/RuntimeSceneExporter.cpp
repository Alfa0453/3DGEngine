#include "RuntimeSceneExporter.h"

#include <engine/ecs/Components.h>
#include <engine/physics/PhysicsComponents.h>

#include <fstream>

using engine::ecs::MeshRenderer;
using engine::ecs::Light;
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

const char* StoredPath(const std::string& path) {
    return path.empty() ? "-" : path.c_str();
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

    out << "3DGRuntimeScene 10\n";
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
        << environment.physicsTimeToSleep << '\n';

    for (const EditorScene::Object& object : scene.Objects()) {
        if (!object.visible) {
            continue;
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
                << object.name << ' '
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
            << object.name << ' '
            << transform->position.x << ' ' << transform->position.y << ' ' << transform->position.z << ' '
            << transform->scale.x << ' ' << transform->scale.y << ' ' << transform->scale.z << ' '
            << transform->rotation.w << ' ' << transform->rotation.x << ' '
            << transform->rotation.y << ' ' << transform->rotation.z << ' '
            << renderer->color.r << ' ' << renderer->color.g << ' ' << renderer->color.b << ' '
            << StoredPath(object.modelAssetPath) << ' '
            << StoredPath(object.materialAssetPath) << ' '
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
            << (object.collider.isTrigger ? 1 : 0) << '\n';
    }

    return true;
}