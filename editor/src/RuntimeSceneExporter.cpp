#include "RuntimeSceneExporter.h"

#include <engine/ecs/Components.h>

#include <fstream>

using engine::ecs::MeshRenderer;
using engine::ecs::Transform;

namespace {

const char* PrimitiveName(EditorScene::Primitive primitive) {
    switch (primitive) {
    case EditorScene::Primitive::Plane: return "Plane";
    case EditorScene::Primitive::Cube: return "Cube";
    }
    return "Cube";
}

const char* StoredPath(const std::string& path) {
    return path.empty() ? "-" : path.c_str();
}

} // namespace

bool RuntimeSceneExporter::Export(const EditorScene &scene, const std::string &path, std::string *error)
{
    std::ofstream out(path);
    if (!out) {
        if (error) *error = "Could not open runtime scene file for writing.";
        return false;
    }

    out << "3DGRuntimeScene 1\n";
    out << "# Runtime export from 3DGEditor. Editor-only flags are omitted.\n";

    for (const EditorScene::Object& object : scene.Objects()) {
        if (!object.visible) {
            continue;
        }

        const Transform* transform = scene.TryGetTransform(object.entity);
        const MeshRenderer* renderer = scene.TryGetMeshRenderer(object.entity);
        if (!transform || !renderer) {
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
            << StoredPath(object.materialAssetPath) << '\n';
    }

    return true;
}