#pragma once

#include "engine/assets/AssetIdentity.h"
#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace engine {
inline constexpr std::uint32_t kEquipmentAssetVersion=1;
struct EquipmentItem {
    std::string name="NewItem";
    std::string slot="RightHand";
    std::string socketName="RightHand";
    std::string modelPath,materialPath,equipAudioPath,equipEffectPath;
    AssetHandle modelId,materialId,audioId,effectId;
    glm::vec3 position{0},eulerDegrees{0},scale{1};
    std::vector<std::string> tags;
};
struct EquipmentAssetData {
    NativeAssetHeader header;
    std::string name="NewEquipmentSet";
    std::string characterPath;
    AssetHandle characterId;
    std::vector<EquipmentItem> items;
};
void NormalizeEquipmentAsset(EquipmentAssetData& asset);
bool ValidateEquipmentAsset(const EquipmentAssetData& asset,std::string* error=nullptr);
bool SaveEquipmentAsset(const std::string& path,EquipmentAssetData asset,std::string* error=nullptr);
bool LoadEquipmentAsset(const std::string& path,EquipmentAssetData* asset,std::string* error=nullptr);
const EquipmentItem* FindEquipmentItem(const EquipmentAssetData& asset,const std::string& name);
} // namespace engine
