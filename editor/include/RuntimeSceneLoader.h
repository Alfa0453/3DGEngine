#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <string>
#include <vector>

class RuntimeSceneLoader {
public:
    struct Entity {
        std::string primitive;
        std::string name;
        glm::vec3 position{0.0f};
        glm::vec3 scale{1.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 color{1.0f};
    };

    struct Scene {
        std::vector<Entity> entities;
    };

    static bool Load(const std::string& path, Scene* scene, std::string* error);
};