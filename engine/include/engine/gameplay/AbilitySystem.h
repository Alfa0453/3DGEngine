#pragma once

#include "engine/assets/AbilityAsset.h"
#include "engine/ecs/Entity.h"

#include <string>
#include <vector>

namespace engine {
namespace ecs { class Registry; }

struct AbilityResource {
    float mana=100.f,maxMana=100.f;
    float stamina=100.f,maxStamina=100.f;
};

struct AbilityRuntimeEvent {
    std::string name;
    AbilityEffectType type=AbilityEffectType::ScriptEvent;
    ecs::Entity source=ecs::kNull,target=ecs::kNull;
    glm::vec3 position{0};
    float magnitude=0.f;
    std::string assetPath;
};

struct AbilitySlot {
    std::string assetPath;
    AbilityAssetData asset;
    float cooldownRemaining=0.f;
    float rechargeRemaining=0.f;
    int charges=1;
};

struct AbilityComponent {
    std::vector<AbilitySlot> abilities;
    int active=-1;
    int phase=-1;
    float phaseTime=0.f;
    ecs::Entity target=ecs::kNull;
    std::vector<bool> fired;
    std::vector<AbilityRuntimeEvent> events;
    float activationHealth=0.f;
};

bool GrantAbility(ecs::Registry& registry, ecs::Entity owner,
                  const AbilityAssetData& ability, const std::string& path={});
bool GrantAbility(ecs::Registry& registry, ecs::Entity owner,
                  const std::string& path, std::string* error=nullptr);
bool ActivateAbility(ecs::Registry& registry, ecs::Entity owner,
                     const std::string& name, ecs::Entity target=ecs::kNull);
bool CancelAbility(ecs::Registry& registry, ecs::Entity owner);
bool IsAbilityActive(const ecs::Registry& registry, ecs::Entity owner,
                     const std::string& name={});
float AbilityCooldownRemaining(const ecs::Registry& registry, ecs::Entity owner,
                               const std::string& name);
void UpdateAbilities(ecs::Registry& registry,float dt);
std::vector<AbilityRuntimeEvent> ConsumeAbilityEvents(ecs::Registry& registry,
                                                      ecs::Entity owner);
} // namespace engine
