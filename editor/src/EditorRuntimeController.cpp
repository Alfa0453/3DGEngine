#include "EditorRuntimeController.h"

#include "EditorLog.h"
#include "EditorProject.h"
#include "EditorScene.h"
#include "RuntimeSceneExporter.h"

#include <engine/ecs/Components.h>
#include <engine/scene/RuntimeSceneLoader.h>

#include <cstddef>
#include <filesystem>

namespace {

template <class T>
std::size_t ComponentCount(engine::ecs::Registry& registry) {
    engine::ecs::Pool<T>* pool = registry.TryPool<T>();
    return pool ? pool->dense.size() : 0;
}

std::filesystem::path RuntimePathFor(const EditorProject& project) {
    std::filesystem::path runtimePath(project.ScenePath());
    runtimePath.replace_extension(".runtime.scene");
    return runtimePath;
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

bool EditorRuntimeController::LoadScene(EditorScene& scene,
    const EditorProject& project,
    const engine::Mesh& cube,
    const engine::Mesh& plane,
    EditorLog& log) const {
    std::string error;
    if (scene.Load(project.ScenePath(), cube, plane, &error)) {
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
    EditorLog& log) const {
    engine::RuntimeSceneLoader::Scene runtimeScene;
    std::string error;
    if (!engine::RuntimeSceneLoader::Load(RuntimePathFor(project).string(), &runtimeScene, &error)) {
        log.Error("Runtime scene validation failed: " + error);
        return false;
    }

    engine::ecs::Registry registry;
    engine::RuntimeSceneLoader::PrimitiveMeshes meshes{&cube, &plane};
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

    log.Info("Validated runtime scene: "
        + std::to_string(runtimeScene.entities.size()) + " entities, "
        + std::to_string(report.modelsAssigned) + " models, "
        + std::to_string(report.texturesAssigned) + " textures, "
        + std::to_string(ComponentCount<engine::ecs::LinearVelocity>(registry)) + " linear, "
        + std::to_string(ComponentCount<engine::ecs::AngularVelocity>(registry)) + " angular");
    return true;
}

bool EditorRuntimeController::BuildPlayRuntimePreview(const EditorScene& scene,
    const EditorProject& project,
    const engine::Mesh& cube,
    const engine::Mesh& plane,
    engine::ecs::Registry& playRegistry,
    engine::RuntimeAssetManager& playAssets,
    std::string* error) const {
    const std::filesystem::path runtimePath = RuntimePathFor(project);

    if (!RuntimeSceneExporter::Export(scene, runtimePath.string(), error)) {
        return false;
    }

    engine::RuntimeSceneLoader::Scene runtimeScene;
    if (!engine::RuntimeSceneLoader::Load(runtimePath.string(), &runtimeScene, error)) {
        return false;
    }

    engine::RuntimeSceneLoader::PrimitiveMeshes meshes{&cube, &plane};
    if (!engine::RuntimeSceneLoader::Instantiate(runtimeScene, playRegistry, meshes, nullptr, error)) {
        return false;
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
