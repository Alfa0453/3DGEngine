#pragma once

#include "engine/assets/DestructionAsset.h"
#include "engine/ecs/Entity.h"

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace engine {
namespace ecs { class Registry; }

struct DestructionRuntimeEvent {
    enum class Type { DamagedState, Broken } type = Type::DamagedState;
    int state = -1;
    std::string particlePath;
    std::string audioPath;
};

struct DestructibleComponent {
    std::string assetPath;
    DestructionAssetData asset;
    float health = 0.0f;
    int state = -1;
    bool broken = false;
    std::vector<DestructionRuntimeEvent> events;
};

struct DestructionDebrisLifetime { float remaining = 0.0f; };

bool ConfigureDestructible(ecs::Registry& registry, ecs::Entity entity,
                           const std::string& assetPath,
                           std::string* error = nullptr);
bool ConfigureDestructible(ecs::Registry& registry, ecs::Entity entity,
                           const DestructionAssetData& asset,
                           const std::string& assetPath = {});
bool DamageDestructible(ecs::Registry& registry, ecs::Entity entity, float damage,
                        const glm::vec3& hitPoint = glm::vec3(0.0f),
                        const glm::vec3& impulse = glm::vec3(0.0f));
bool ImpactDestructible(ecs::Registry& registry, ecs::Entity entity, float impact,
                        const glm::vec3& hitPoint,
                        const glm::vec3& direction);
float DestructibleHealth(const ecs::Registry& registry, ecs::Entity entity);
bool IsDestructibleBroken(const ecs::Registry& registry, ecs::Entity entity);
std::vector<DestructionRuntimeEvent> ConsumeDestructionEvents(
    ecs::Registry& registry, ecs::Entity entity);
void UpdateDestruction(ecs::Registry& registry,float deltaSeconds);

} // namespace engine
