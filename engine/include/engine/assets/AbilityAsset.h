#pragma once

#include "engine/assets/AssetIdentity.h"

#include <glm/vec3.hpp>

#include <string>
#include <vector>

namespace engine {

enum class AbilityTargetMode { Self, ExplicitTarget, Radius };
enum class AbilityEffectType {
    Damage, Heal, Impulse, AnimationAction, Projectile, Particle, Audio, ScriptEvent
};

struct AbilityEffect {
    AbilityEffectType type = AbilityEffectType::Damage;
    AbilityTargetMode target = AbilityTargetMode::ExplicitTarget;
    float time = 0.0f;
    float magnitude = 10.0f;
    float radius = 0.0f;
    float speed = 12.0f;
    float range = 20.0f;
    glm::vec3 direction{0.0f, 0.0f, 1.0f};
    std::string name;
    std::string assetPath;
    AssetHandle assetId;
};

struct AbilityPhase {
    std::string name = "Active";
    float duration = 0.1f;
    bool interruptible = true;
    std::vector<AbilityEffect> effects;
};

struct AbilityAssetData {
    int version = 1;
    AssetHandle assetId;
    std::string name = "Ability";
    std::string description;
    float cooldown = 1.0f;
    int maxCharges = 1;
    float chargeRecovery = 1.0f;
    float manaCost = 0.0f;
    float staminaCost = 0.0f;
    float healthCost = 0.0f;
    float activationRange = 20.0f;
    bool requireTarget = false;
    bool cancelOnDamage = false;
    std::vector<AbilityPhase> phases;
};

bool SaveAbilityAsset(const std::string& path, AbilityAssetData& asset,
                      std::string* error = nullptr);
bool LoadAbilityAsset(const std::string& path, AbilityAssetData* asset,
                      std::string* error = nullptr);

} // namespace engine
