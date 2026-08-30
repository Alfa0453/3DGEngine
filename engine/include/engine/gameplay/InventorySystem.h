#pragma once

#include "engine/assets/ItemAsset.h"
#include "engine/ecs/Entity.h"

#include <string>
#include <vector>

namespace engine { namespace ecs { class Registry; }
enum class InventoryEventType : std::uint8_t { Added,Removed,Used,Equipped,Unequipped,Rejected };
struct InventoryEvent {InventoryEventType type=InventoryEventType::Added;std::string item,event,assetPath;int count=0;float magnitude=0.0f;EquipmentSlot slot=EquipmentSlot::None;};
struct InventoryStack {std::string assetPath;ItemAssetData asset;int count=1;bool equipped=false;};
struct InventoryComponent {int maximumSlots=24;float maximumWeight=100.0f;std::vector<InventoryStack> items;std::vector<InventoryEvent> events;};
bool AddItem(ecs::Registry& registry,ecs::Entity owner,const std::string& assetPath,int count=1,std::string* error=nullptr);
bool AddItem(ecs::Registry& registry,ecs::Entity owner,const ItemAssetData& item,int count=1,const std::string& assetPath={});
int RemoveItem(ecs::Registry& registry,ecs::Entity owner,const std::string& item,int count=1);
bool UseItem(ecs::Registry& registry,ecs::Entity owner,const std::string& item);
bool EquipItem(ecs::Registry& registry,ecs::Entity owner,const std::string& item);
bool UnequipSlot(ecs::Registry& registry,ecs::Entity owner,EquipmentSlot slot);
int ItemCount(const ecs::Registry& registry,ecs::Entity owner,const std::string& item);
bool HasItem(const ecs::Registry& registry,ecs::Entity owner,const std::string& item,int count=1);
float InventoryWeight(const ecs::Registry& registry,ecs::Entity owner);
std::vector<InventoryEvent> ConsumeInventoryEvents(ecs::Registry& registry,ecs::Entity owner);
std::string SerializeInventory(const ecs::Registry& registry,ecs::Entity owner);
bool RestoreInventory(ecs::Registry& registry,ecs::Entity owner,const std::string& data);
} // namespace engine
