#pragma once

#include <glm/glm.hpp>

namespace engine {
struct Health;
namespace ecs { struct Transform; }
}

namespace gameplay_utilities {

float Saturate(float value);
float HealthRatio(const engine::Health& health);
float Heal(engine::Health& health, float amount, bool revive = false);
glm::vec3 MoveTowards(const glm::vec3& current, const glm::vec3& target,
                      float maximumDistance);
void RotateWorldY(engine::ecs::Transform& transform, float degrees);

} // namespace gameplay_utilities
