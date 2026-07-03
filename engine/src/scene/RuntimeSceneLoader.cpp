#include "engine/scene/RuntimeSceneLoader.h"

#include "engine/ecs/Components.h"
#include "engine/ecs/Registry.h"
#include "engine/graphics/DayNightCycle.h"
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
    if (primitive == "Sphere") {
        return meshes.sphere;
    }
    return nullptr;
}

bool ParseLightType(const std::string& value, ecs::Light::Type* type) {
    if (value == "Directional") {
        *type = ecs::Light::Type::Directional;
        return true;
    }
    if (value == "Point") {
        *type = ecs::Light::Type::Point;
        return true;
    }
    if (value == "Spot") {
        *type = ecs::Light::Type::Spot;
        return true;
    }
    if (value == "Area") {
        *type = ecs::Light::Type::Area;
        return true;
    }
    return false;
}

ecs::Light EnvironmentSunLight(const RuntimeSceneLoader::Scene::Environment& environment) {
    const DayNightCycle::Sample sky = DayNightCycle::At(environment.timeOfDay);
    const glm::vec3 radiance = sky.keyLightColor * std::max(environment.sunIntensity, 0.0f);

    ecs::Light light;
    light.type = ecs::Light::Type::Directional;
    light.direction = sky.keyLightDirection;
    light.intensity = std::max(std::max(radiance.r, radiance.g), radiance.b);
    light.color = light.intensity > 0.0001f ? radiance / light.intensity : glm::vec3(1.0f);
    return light;
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
    if (magic != "3DGRuntimeScene" || version < 1 || version > 5) {
        if (error) {
            *error = "Runtime scene file has an unknown format: "
                + magic + " " + std::to_string(version)
                + " (expected 3DGRuntimeScene 1..5).";
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
        if (recordType == "environment") {
            if (version < 5) {
                continue;
            }

            int fog = 1;
            record >> loaded.environment.timeOfDay
                >> loaded.environment.skyLightIntensity
                >> fog
                >> loaded.environment.fogDensity
                >> loaded.environment.fogHeight
                >> loaded.environment.fogHeightFalloff;
            if (!record) {
                if (error) {
                    *error = "Runtime scene contains an invalid environment record.";
                }
                return false;
            }
            loaded.environment.fog = fog != 0;
            continue;
        }

        if (recordType == "light") {
            if (version < 4) {
                if (error) {
                    *error = "Runtime scene file contains a light record, but the file version is "
                        + std::to_string(version) + " (expected 4 or higher).";
                }
                return false;
            }
            Scene::LightDesc desc;
            std::string typeName;
            record >> desc.name >> typeName
                >> desc.position.x >> desc.position.y >> desc.position.z
                >> desc.light.color.r >> desc.light.color.g >> desc.light.color.b
                >> desc.light.intensity
                >> desc.light.direction.x >> desc.light.direction.y >> desc.light.direction.z
                >> desc.light.innerAngle >> desc.light.outerAngle >> desc.light.range >> desc.light.sourceRadius;

            if (!record || !ParseLightType(typeName, &desc.light.type)) {
                if (error) {
                    *error = "Runtime scene contains an invalid light record.";
                }
                return false;
            }

            loaded.lights.push_back(desc);
            continue;
        }
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
        if (version >= 3) {
            record >> entity.linearVelocity.x >> entity.linearVelocity.y >> entity.linearVelocity.z
                   >> entity.angularVelocityAxis.x >> entity.angularVelocityAxis.y >> entity.angularVelocityAxis.z
                   >> entity.angularVelocityRadians;
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

        if (glm::dot(desc.linearVelocity, desc.linearVelocity) > 0.0f) {
            registry.Add<ecs::LinearVelocity>(entity, ecs::LinearVelocity{desc.linearVelocity});
        }

        if (desc.angularVelocityRadians != 0.0f &&
            glm::dot(desc.angularVelocityAxis, desc.angularVelocityAxis) > 0.0f) {
            registry.Add<ecs::AngularVelocity>(entity, ecs::AngularVelocity{
                desc.angularVelocityAxis,
                desc.angularVelocityRadians
            });
        }

        if (created) {
            created->push_back(entity);
        }
    }

    bool environmentSunApplied = false;
    for (const Scene::LightDesc& desc : scene.lights) {
        ecs::Light light = desc.light;
        if (scene.environment.driveSunLight
            && light.type == ecs::Light::Type::Directional
            && !environmentSunApplied) {
            light = EnvironmentSunLight(scene.environment);
            environmentSunApplied = true;
        }

        const ecs::Entity entity = registry.Create();
        registry.Add<ecs::Transform>(entity, ecs::Transform{
            desc.position,
            glm::vec3(1.0f),
            glm::quat(1.0f, 0.0f, 0.0f, 0.0f)
        });
        registry.Add<ecs::Light>(entity, light);
        if (created) {
            created->push_back(entity);
        }
    }

    if (scene.environment.driveSunLight && !environmentSunApplied) {
        const ecs::Entity entity = registry.Create();
        registry.Add<ecs::Transform>(entity, ecs::Transform{});
        registry.Add<ecs::Light>(entity, EnvironmentSunLight(scene.environment));
        if (created) {
            created->push_back(entity);
        }
    }

    return true;
}

} // namespace engine
