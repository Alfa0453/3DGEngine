#pragma once

#include <engine/assets/RuntimeAssetManager.h>
#include <engine/ecs/Registry.h>
#include <engine/graphics/Mesh.h>

#include <string>

class EditorLog;
class EditorProject;
class EditorScene;

class EditorRuntimeController {
public:
    bool SaveScene(EditorScene& scene, const EditorProject& project, EditorLog& log) const;
    bool AutosaveScene(EditorScene& scene, const EditorProject& project, EditorLog& log) const;
    bool LoadScene(EditorScene& scene,
        const EditorProject& project,
        const engine::Mesh& cube,
        const engine::Mesh& plane,
        const engine::Mesh& sphere,
        EditorLog& log) const;
    bool ExportRuntimeScene(const EditorScene& scene, const EditorProject& project, EditorLog& log) const;
    bool ValidateRuntimeScene(const EditorProject& project,
        const engine::Mesh& cube,
        const engine::Mesh& plane,
        const engine::Mesh& sphere,
        EditorLog& log) const;
    bool BuildPlayRuntimePreview(const EditorScene& scene,
        const EditorProject& project,
        const engine::Mesh& cube,
        const engine::Mesh& plane,
        const engine::Mesh& sphere,
        engine::ecs::Registry& playRegistry,
        engine::RuntimeAssetManager& playAssets,
        std::string* error) const;
};
