#include "engine/scene/RuntimeSceneLoader.h"

#include "engine/ecs/Components.h"
#include "engine/ecs/Registry.h"
#include "engine/graphics/Mesh.h"

#include <fstream>
#include <sstream>

namespace engine {
namespace {

const Mesh* MeshForPrimitive(const std::string& primitive, const RuntimeSceneLoader::PrimitiveMeshes& meshes) {
    if (primitive == "Cube") {
        return meshes.cube;
    }
    if (primitive == "Plane") {
        return meshes.plane;
    }
    return nullptr;
}

} // namespace

bool RuntimeSceneLoader::Load(const std::string &path, Scene *scene, std::string *error)
{
    if (!scene) {
        if (error) {
            *error = "Runtime scene output pointer was null.";
        }
        return false;
    }

    std::ifstream in(path);
    if (!in) {
        if (error) {
            *error = "Could not open runtime scene file for reading.";
        }
        return false;
    }

    std::string magic;
    int version = 0;
    in >> magic >> version;
    if (magic != "3DGRuntimeScene" || version != 1) {
        if (error) {
            *error = "Runtime scene file has an unknown format.";
        }
        return false;
    }

    Scene loaded;
    std::string line;
    std::getline(in, line);
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }

        std::istringstream record(line);
        std::string recordType;
        record >> recordType;
        if (recordType != "entity") {
            continue;
        }

        EntityDesc entity;
        record >> entity.primitive >> entity.name
               >> entity.position.x >> entity.position.y >> entity.position.z
               >> entity.scale.x >> entity.scale.y >> entity.scale.z
               >> entity.rotation.w >> entity.rotation.x >> entity.rotation.y >> entity.rotation.z
               >> entity.color.r >> entity.color.g >> entity.color.b;

        if (version >= 2) {
            record >> entity.modelPath >> entity.materialPath;
            if (entity.modelPath == "-") {
                entity.modelPath.clear();
            }
            if (entity.materialPath == "-") {
                entity.materialPath.clear();
            }
        }


        if (!record) {
            if (error) {
                *error = "Runtime scene contains an invalid entity record.";
            }
            return false;
        }

        loaded.entities.push_back(entity);
    }

    *scene = loaded;
    return true;
}

bool RuntimeSceneLoader::Instantiate(const Scene &scene, ecs::Registry &registry, const PrimitiveMeshes &meshes, std::vector<ecs::Entity> *created, std::string *error)
{
    if (created) {
        created->clear();
    }

    for (const EntityDesc& desc : scene.entities) {
        const Mesh* mesh = MeshForPrimitive(desc.primitive, meshes);
        if (!mesh) {
            if (error) {
                *error = "Runtime scene references unsupported primitive: " + desc.primitive;
            }
            return false;
        }

        const ecs::Entity entity = registry.Create();
        registry.Add<ecs::Transform>(entity, ecs::Transform{
            desc.position,
            desc.scale,
            desc.rotation
        });
        registry.Add<ecs::MeshRenderer>(entity, ecs::MeshRenderer{
            mesh,
            desc.color
        });
        if (!desc.modelPath.empty()) {
            registry.Add<ecs::ModelAsset>(entity, ecs::ModelAsset{desc.modelPath});
        }
        if (!desc.materialPath.empty()) {
            registry.Add<ecs::MaterialAsset>(entity, ecs::MaterialAsset{desc.materialPath});
        }

        if (created) {
            created->push_back(entity);
        }
    }

    return true;
}

} // namespace engine
