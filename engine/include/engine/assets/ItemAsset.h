#pragma once

#include "engine/assets/AssetIdentity.h"

#include <cstdint>
#include <string>
#include <vector>

namespace engine {

inline constexpr std::uint32_t kItemAssetVersion = 1;
enum class ItemType : std::uint8_t { Miscellaneous, Weapon, Armor, Consumable, Currency, Quest, Crafting };
enum class EquipmentSlot : std::uint8_t { None, MainHand, OffHand, Head, Chest, Hands, Legs, Feet, Accessory };

struct ItemStatistic { std::string name;float value=0.0f; };
struct ItemEffect { std::string event;float magnitude=0.0f;std::string assetPath;AssetHandle assetId; };

struct ItemAssetData {
    NativeAssetHeader header;
    std::string name="Item";
    std::string displayName="New Item";
    std::string description;
    std::string localizationKey;
    ItemType type=ItemType::Miscellaneous;
    EquipmentSlot equipmentSlot=EquipmentSlot::None;
    int maximumStack=1;
    int value=0;
    float weight=0.0f;
    bool consumable=false;
    bool droppable=true;
    bool unique=false;
    std::string iconPath,meshPath,pickupPrefabPath,abilityPath,useAnimation,useAudio,useParticle;
    AssetHandle iconId,meshId,pickupPrefabId,abilityId,useAnimationId,useAudioId,useParticleId;
    std::vector<ItemStatistic> statistics;
    std::vector<ItemEffect> effects;
    std::vector<std::string> tags;
};

const char* ItemTypeName(ItemType type);
const char* EquipmentSlotName(EquipmentSlot slot);
void NormalizeItemAsset(ItemAssetData& asset);
bool ValidateItemAsset(const ItemAssetData& asset,std::string* error=nullptr);
bool SaveItemAsset(const std::string& path,ItemAssetData asset,std::string* error=nullptr);
bool LoadItemAsset(const std::string& path,ItemAssetData* asset,std::string* error=nullptr);

} // namespace engine
