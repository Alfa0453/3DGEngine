#pragma once
#include "engine/assets/CombatAsset.h"
#include "engine/ecs/Entity.h"
#include <string>
#include <vector>
namespace engine {namespace ecs{class Registry;}
enum class CombatResult:std::uint8_t{Miss,Hit,Blocked,Parried,Immune,Friendly};
const char* CombatResultName(CombatResult result);
enum class CombatEventType:std::uint8_t{ComboStarted,ComboAdvanced,AttackWindow,Hit,Blocked,Parried,Staggered,Reaction,ComboEnded};
struct CombatEvent{CombatEventType type=CombatEventType::Hit;ecs::Entity source=ecs::kNull,target=ecs::kNull;std::string name,damageType,assetPath;float value=0;int comboStep=-1;};
struct CombatComponent{std::string assetPath;CombatAssetData asset;bool blocking=false;float blockTime=0,immunityRemaining=0,stagger=0,staggerRemaining=0;int comboStep=-1;float comboTime=0;bool hitWindowEmitted=false;ecs::Entity target=ecs::kNull;std::vector<CombatEvent> events;};
bool ConfigureCombat(ecs::Registry& registry,ecs::Entity owner,const std::string& path,std::string* error=nullptr);
bool ConfigureCombat(ecs::Registry& registry,ecs::Entity owner,const CombatAssetData& asset,const std::string& path={});
void SetCombatBlocking(ecs::Registry& registry,ecs::Entity owner,bool blocking);
bool StartCombatCombo(ecs::Registry& registry,ecs::Entity owner,ecs::Entity target=ecs::kNull);
bool AdvanceCombatCombo(ecs::Registry& registry,ecs::Entity owner);
CombatResult ExecuteCombatHit(ecs::Registry& registry,ecs::Entity owner,ecs::Entity target=ecs::kNull);
CombatResult ApplyCombatDamage(ecs::Registry& registry,ecs::Entity source,ecs::Entity target,float damage,const std::string& damageType="Physical");
void UpdateCombat(ecs::Registry& registry,float dt);
bool IsCombatStaggered(const ecs::Registry& registry,ecs::Entity owner);
int CombatComboStep(const ecs::Registry& registry,ecs::Entity owner);
std::vector<CombatEvent> ConsumeCombatEvents(ecs::Registry& registry,ecs::Entity owner);
} // namespace engine
