#include "GameplayUtilities/GameplayUtilities.h"

#include <engine/ecs/Components.h>
#include <engine/gameplay/GameplayComponents.h>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace gameplay_utilities {

float Saturate(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

float HealthRatio(const engine::Health& health) {
    return health.maxHp > 0.0f ? Saturate(health.hp / health.maxHp) : 0.0f;
}

float Heal(engine::Health& health, float amount, bool revive) {
    if (amount <= 0.0f || health.maxHp <= 0.0f) return 0.0f;
    if (!health.alive && !revive) return 0.0f;

    const float before = health.hp;
    health.hp = std::clamp(health.hp + amount, 0.0f, health.maxHp);
    if (revive && health.hp > 0.0f) {
        health.alive = true;
        health.justDied = false;
    }
    return health.hp - before;
}

glm::vec3 MoveTowards(const glm::vec3& current, const glm::vec3& target,
                      float maximumDistance) {
    const glm::vec3 delta = target - current;
    const float distance = glm::length(delta);
    if (distance <= std::max(maximumDistance, 0.0f) || distance <= 1e-6f) {
        return target;
    }
    return current + delta * (std::max(maximumDistance, 0.0f) / distance);
}

void RotateWorldY(engine::ecs::Transform& transform, float degrees) {
    if (!std::isfinite(degrees)) return;
    const glm::quat rotation = glm::angleAxis(
        glm::radians(degrees), glm::vec3(0.0f, 1.0f, 0.0f));
    transform.rotation = glm::normalize(rotation * transform.rotation);
}

} // namespace gameplay_utilities
