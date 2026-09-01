#pragma once

#include "engine/assets/IKRigAsset.h"
#include "engine/ecs/Entity.h"

#include <glm/glm.hpp>
#include <string>

namespace engine {
namespace ecs { class Registry; }

bool ConfigureIKRig(ecs::Registry& registry, ecs::Entity entity,
                    const std::string& assetPath, std::string* error = nullptr);
bool ConfigureIKRig(ecs::Registry& registry, ecs::Entity entity,
                    const IKRigAssetData& asset, const std::string& assetPath = {},
                    std::string* error = nullptr);
bool SetIKTarget(ecs::Registry& registry, ecs::Entity entity,
                 const std::string& goalName, const glm::vec3& worldTarget,
                 float weight = 1.0f);
bool ClearIKTarget(ecs::Registry& registry, ecs::Entity entity,
                   const std::string& goalName);
bool SetIKGoalWeight(ecs::Registry& registry, ecs::Entity entity,
                     const std::string& goalName, float weight);
bool HasIKGoal(const ecs::Registry& registry, ecs::Entity entity,
               const std::string& goalName);

} // namespace engine
