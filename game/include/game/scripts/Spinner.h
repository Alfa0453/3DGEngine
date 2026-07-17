#pragma once

#include <engine/gameplay/Script.h>
#include <engine/ecs/Components.h>

#include <glm/gtc/quaternion.hpp>

// Example gameplay script. Attach a Script component with class name "Spinner" to any
// object in the editor, and it will slowly rotate around Y in Play mode (and in the
// standalone player). Copy this as a starting point for your own scripts.
class Spinner : public engine::Script {
public:
    void OnUpdate(float dt) override {
        if (engine::ecs::Transform* t = Transform()) {
            const glm::quat spin = glm::angleAxis(dt * 1.5f, glm::vec3(0.0f, 1.0f, 0.0f));
            t->rotation = glm::normalize(spin * t->rotation);
        }
    }
};
