#pragma once

#include "engine/ecs/Entity.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <string>
#include <vector>

namespace engine {

class Mesh;

namespace ecs {
class Registry;
}

class RuntimeSceneLoader {
public:
    struct EntityDesc {
        std::string primitive;
        std::string name;
        glm::vec3 position{0.0f};
        glm::vec3 scale{1.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 color{1.0f};
        std::string modelPath;
        std::string materialPath;
    };

    struct Scene {
        std::vector<EntityDesc> entities;
    };

    struct PrimitiveMeshes {
        const Mesh* cube = nullptr;
        const Mesh* plane = nullptr;
    };

    static bool Load(const std::string& path, Scene* scene, std::string* error);
    static bool Instantiate(const Scene& scene, ecs::Registry& registry,
                            const PrimitiveMeshes& meshes, std::vector<ecs::Entity>* created,
                            std::string* error);
};

} // namespace engine