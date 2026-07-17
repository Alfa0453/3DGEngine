#include "EditorRuntimeController.h"

#include "EditorLog.h"
#include "EditorProject.h"
#include "EditorScene.h"
#include "RuntimeSceneExporter.h"

#include <engine/ecs/Components.h>
#include <engine/physics/PhysicsComponents.h>
#include <engine/scene/RuntimeSceneLoader.h>

#include <cstddef>
#include <filesystem>

namespace {

template <class T>
std::size_t ComponentCount(engine::ecs::Registry& registry) {
    engine::ecs::Pool<T>* pool = registry.TryPool<T>();
    return pool ? pool->dense.size() : 0;
}

struct PhysicsValidationStats {
    std::size_t rigidBodies = 0;
    std::size_t dynamicBodies = 0;
    std::size_t colliders = 0;
    std::size_t staticColliders = 0;
    std::size_t triggerColliders = 0;
    std::size_t dynamicBodiesWithoutCollider = 0;
    std::size_t rigidBodiesWithoutTransform = 0;
    std::size_t collidersWithoutTransform = 0;
    std::size_t invalidColliders = 0;
};

bool ColliderShapeIsInvalid(const engine::ecs::Collider& collider) {
    switch (collider.shape) {
    case engine::ecs::ColliderShape::Sphere:
        return collider.radius <= 0.0f;
    case engine::ecs::ColliderShape::Box:
    case engine::ecs::ColliderShape::Pyramid:
    case engine::ecs::ColliderShape::Staircase:
        return collider.halfExtents.x <= 0.0f
            || collider.halfExtents.y <= 0.0f
            || collider.halfExtents.z <= 0.0f;
    case engine::ecs::ColliderShape::Plane:
        return glm::dot(collider.planeNormal, collider.planeNormal) <= 0.0001f;
    case engine::ecs::ColliderShape::Capsule:
        return collider.radius <= 0.0f || collider.halfHeight < 0.0f;
    case engine::ecs::ColliderShape::Cylinder:
    case engine::ecs::ColliderShape::Cone:
        return collider.radius <= 0.0f || collider.halfHeight <= 0.0f;
    case engine::ecs::ColliderShape::Torus:
        return collider.majorRadius <= 0.0f || collider.minorRadius <= 0.0f;
    }

    return true;
}

PhysicsValidationStats CollectPhysicsValidationStats(engine::ecs::Registry& registry) {
    PhysicsValidationStats stats;

    if (engine::ecs::Pool<engine::ecs::RigidBody>* bodies = registry.TryPool<engine::ecs::RigidBody>()) {
        stats.rigidBodies = bodies->dense.size();
        for (const engine::ecs::Entity entity : bodies->dense) {
            const engine::ecs::RigidBody& body = bodies->Get(entity);
            if (body.invMass > 0.0f) {
                ++stats.dynamicBodies;
                if (!registry.Has<engine::ecs::Collider>(entity)) {
                    ++stats.dynamicBodiesWithoutCollider;
                }
            }
            if (!registry.Has<engine::ecs::Transform>(entity)) {
                ++stats.rigidBodiesWithoutTransform;
            }
        }
    }

    if (engine::ecs::Pool<engine::ecs::Collider>* colliders = registry.TryPool<engine::ecs::Collider>()) {
        stats.colliders = colliders->dense.size();
        for (const engine::ecs::Entity entity : colliders->dense) {
            const engine::ecs::Collider& collider = colliders->Get(entity);
            if (collider.isTrigger) {
                ++stats.triggerColliders;
            }
            if (!registry.Has<engine::ecs::Transform>(entity)) {
                ++stats.collidersWithoutTransform;
            }
            if (ColliderShapeIsInvalid(collider)) {
                ++stats.invalidColliders;
            }

            const engine::ecs::RigidBody* body = registry.TryGet<engine::ecs::RigidBody>(entity);
            if (!body || body->invMass <= 0.0f) {
                ++stats.staticColliders;
            }
        }
    }

    return stats;
}

void LogPhysicsValidationWarnings(const PhysicsValidationStats& stats, EditorLog& log) {
    if (stats.dynamicBodiesWithoutCollider > 0) {
        log.Warning("Physics validation: "
            + std::to_string(stats.dynamicBodiesWithoutCollider)
            + " dynamic body/bodies have no collider");
    }
    if (stats.rigidBodiesWithoutTransform > 0) {
        log.Warning("Physics validation: "
            + std::to_string(stats.rigidBodiesWithoutTransform)
            + " rigid body/bodies have no transform");
    }
    if (stats.collidersWithoutTransform > 0) {
        log.Warning("Physics validation: "
            + std::to_string(stats.collidersWithoutTransform)
            + " collider(s) have no transform");
    }
    if (stats.invalidColliders > 0) {
        log.Warning("Physics validation: "
            + std::to_string(stats.invalidColliders)
            + " collider(s) have invalid radius, extents, or plane normal");
    }
    if (stats.dynamicBodies > 0 && stats.staticColliders == 0) {
        log.Warning("Physics validation: dynamic bodies exist but there are no static colliders to collide with");
    }
}

std::filesystem::path RuntimePathFor(const EditorProject& project) {
    std::filesystem::path runtimePath(project.ScenePath());
    runtimePath.replace_extension(".runtime.scene");
    return runtimePath;
}

std::filesystem::path AutosavePathFor(const EditorProject& project) {
    std::filesystem::path autosavePath(project.ScenePath());
    const std::filesystem::path extension = autosavePath.extension();
    autosavePath.replace_extension(".autosave" + extension.string());
    return autosavePath;
}

} // namespace

bool EditorRuntimeController::SaveScene(EditorScene& scene, const EditorProject& project, EditorLog& log) const {
    std::string error;
    if (scene.Save(project.ScenePath(), &error)) {
        log.Info("Saved " + project.ScenePath());
        return true;
    }

    log.Error("Save failed: " + error);
    return false;
}

bool EditorRuntimeController::AutosaveScene(EditorScene& scene, const EditorProject& project, EditorLog& log) const {
    const std::filesystem::path autosavePath = AutosavePathFor(project);

    std::string error;
    if (scene.Save(autosavePath.string(), &error, false)) {
        log.Info("Autosaved " + autosavePath.string());
        return true;
    }

    log.Warning("Autosave failed: " + error);
    return false;
}

bool EditorRuntimeController::LoadScene(EditorScene& scene,
    const EditorProject& project,
    const engine::Mesh& cube,
    const engine::Mesh& plane,
    const engine::Mesh& sphere,
    const engine::Mesh& capsule,
    const engine::Mesh& cylinder,
    const engine::Mesh& cone,
    const engine::Mesh& pyramid,
    const engine::Mesh& torus,
    const engine::Mesh& staircase,
    EditorLog& log) const {
    std::string error;
    if (scene.Load(project.ScenePath(), cube, plane, sphere, capsule, cylinder, cone, pyramid, torus, staircase, &error)) {
        log.Info("Loaded " + project.ScenePath());
        return true;
    }

    log.Error("Load failed: " + error);
    return false;
}

bool EditorRuntimeController::ExportRuntimeScene(const EditorScene& scene,
    const EditorProject& project,
    EditorLog& log) const {
    const std::filesystem::path exportPath = RuntimePathFor(project);

    std::string error;
    if (RuntimeSceneExporter::Export(scene, exportPath.string(), &error)) {
        log.Info("Exported runtime scene " + exportPath.string());
        return true;
    }

    log.Error("Runtime export failed: " + error);
    return false;
}

bool EditorRuntimeController::ValidateRuntimeScene(const EditorProject& project,
    const engine::Mesh& cube,
    const engine::Mesh& plane,
    const engine::Mesh& sphere,
    const engine::Mesh& capsule,
    const engine::Mesh& cylinder,
    const engine::Mesh& cone,
    const engine::Mesh& pyramid,
    const engine::Mesh& torus,
    const engine::Mesh& staircase,
    EditorLog& log) const {
    engine::RuntimeSceneLoader::Scene runtimeScene;
    std::string error;
    if (!engine::RuntimeSceneLoader::Load(RuntimePathFor(project).string(), &runtimeScene, &error)) {
        log.Error("Runtime scene validation failed: " + error);
        return false;
    }

    engine::ecs::Registry registry;
    engine::RuntimeSceneLoader::PrimitiveMeshes meshes{&cube, &plane, &sphere, &capsule, &cylinder, &cone, &pyramid, &torus, &staircase};
    if (!engine::RuntimeSceneLoader::Instantiate(runtimeScene, registry, meshes, nullptr, &error)) {
        log.Error("Runtime scene validation failed: " + error);
        return false;
    }

    engine::RuntimeAssetManager assets;
    const engine::RuntimeAssetManager::ResolveReport report = assets.ResolveRegistryAssets(registry);
    if (!report.errors.empty()) {
        log.Error("Runtime asset validation failed: " + report.errors.front());
        return false;
    }


    const PhysicsValidationStats physics = CollectPhysicsValidationStats(registry);
    LogPhysicsValidationWarnings(physics, log);

    log.Info("Validated runtime scene: "
        + std::to_string(runtimeScene.entities.size()) + " entities, "
        + std::to_string(report.modelsAssigned) + " models, "
        + std::to_string(report.texturesAssigned) + " textures, "
        + std::to_string(ComponentCount<engine::ecs::LinearVelocity>(registry)) + " linear, "
        + std::to_string(ComponentCount<engine::ecs::AngularVelocity>(registry)) + " angular, "
        + std::to_string(physics.rigidBodies) + " rigid bodies, "
        + std::to_string(physics.dynamicBodies) + " dynamic, "
        + std::to_string(physics.colliders) + " colliders, "
        + std::to_string(physics.triggerColliders) + " triggers, "
        + std::to_string(ComponentCount<engine::ecs::AudioSource>(registry)) + " audio sources, "
        + std::to_string(ComponentCount<engine::ParticleSystemComponent>(registry)) + " particle systems");
    return true;
}

bool EditorRuntimeController::BuildPlayRuntimePreview(const EditorScene& scene,
    const EditorProject& project,
    const engine::Mesh& cube,
    const engine::Mesh& plane,
    const engine::Mesh& sphere,
    const engine::Mesh& capsule,
    const engine::Mesh& cylinder,
    const engine::Mesh& cone,
    const engine::Mesh& pyramid,
    const engine::Mesh& torus,
    const engine::Mesh& staircase,
    engine::ecs::Registry& playRegistry,
    engine::RuntimeAssetManager& playAssets,
    std::vector<engine::ecs::Entity>* createdEntities,
    std::vector<std::string>* createdNames,
    std::string* error) const {
    const std::filesystem::path runtimePath = RuntimePathFor(project);

    if (!RuntimeSceneExporter::Export(scene, runtimePath.string(), error)) {
        return false;
    }

    engine::RuntimeSceneLoader::Scene runtimeScene;
    if (!engine::RuntimeSceneLoader::Load(runtimePath.string(), &runtimeScene, error)) {
        return false;
    }

    engine::RuntimeSceneLoader::PrimitiveMeshes meshes{&cube, &plane, &sphere, &capsule, &cylinder, &cone, &pyramid, &torus, &staircase};
    if (!engine::RuntimeSceneLoader::Instantiate(runtimeScene, playRegistry, meshes, createdEntities, error)) {
        return false;
    }


    if (createdNames) {
        for (const engine::RuntimeSceneLoader::EntityDesc& desc : runtimeScene.entities) {
            createdNames->push_back(desc.name);
        }
        for (const engine::RuntimeSceneLoader::Scene::LightDesc& desc : runtimeScene.lights) {
            createdNames->push_back(desc.name);
        }
        while (createdEntities && createdNames->size() < createdEntities->size()) {
            createdNames->push_back("RuntimeEntity");
        }
    }

    const engine::RuntimeAssetManager::ResolveReport report = playAssets.ResolveRegistryAssets(playRegistry);
    if (!report.errors.empty()) {
        if (error) {
            *error = report.errors.front();
        }
        return false;
    }

    if (error) {
        *error = {};
    }
    return true;
}
