#pragma once
#include "engine/ecs/Entity.h"
#include <string>
namespace engine { namespace ecs {class Registry;} class RuntimeAssetManager;
void SetEquipmentAssetManager(RuntimeAssetManager* assets);
bool EquipItem(ecs::Registry& registry,ecs::Entity entity,const std::string& assetPath,
               const std::string& itemName,std::string* error=nullptr);
bool UnequipSlot(ecs::Registry& registry,ecs::Entity entity,const std::string& slot);
std::string EquippedItem(const ecs::Registry& registry,ecs::Entity entity,const std::string& slot);
}
