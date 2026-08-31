#pragma once

#include "engine/assets/AssetIdentity.h"

#include <cstdint>
#include <string>
#include <vector>

namespace engine {
inline constexpr std::uint32_t kCombatAssetVersion=1;
struct CombatDamageType {std::string name="Physical";float damageMultiplier=1.0f;float staggerMultiplier=1.0f;float armorPenetration=0.0f;};
struct CombatResistance {std::string damageType="Physical";float multiplier=1.0f;};
struct CombatComboStep {std::string name="Attack";std::string actionClipPath;AssetHandle actionClipId;std::string damageType="Physical";float damage=10.0f;float staminaCost=5.0f;float duration=.6f;float hitTime=.25f;float inputWindowStart=.3f;float inputWindowEnd=.55f;float range=2.0f;float radius=.35f;std::string hitReaction;std::string particlePath,audioPath;AssetHandle particleId,audioId;};
struct CombatAssetData {NativeAssetHeader header;std::string name="CombatProfile";int team=0;bool friendlyFire=false;float poise=100.0f;float staggerRecovery=1.0f;float blockReduction=.75f;float blockStaminaCost=10.0f;float parryWindow=.15f;float immunityAfterHit=.1f;float targetingRange=20.0f;bool autoFaceTarget=true;std::vector<CombatDamageType> damageTypes;std::vector<CombatResistance> resistances;std::vector<CombatComboStep> combo;};
void NormalizeCombatAsset(CombatAssetData& asset);
bool ValidateCombatAsset(const CombatAssetData& asset,std::string* error=nullptr);
bool SaveCombatAsset(const std::string& path,CombatAssetData asset,std::string* error=nullptr);
bool LoadCombatAsset(const std::string& path,CombatAssetData* asset,std::string* error=nullptr);
} // namespace engine
