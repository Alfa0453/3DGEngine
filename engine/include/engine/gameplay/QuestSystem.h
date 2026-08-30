#pragma once

#include "engine/assets/QuestAsset.h"
#include "engine/ecs/Entity.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace engine { namespace ecs { class Registry; }

enum class QuestState : std::uint8_t { Inactive, Active, Completed, Failed };
enum class QuestEventType : std::uint8_t { Started, ObjectiveAdvanced, ObjectiveCompleted, Checkpoint, Dialogue, Reward, Completed, Failed };
struct QuestRuntimeEvent { QuestEventType type=QuestEventType::Started;std::string quest,objective,name,assetPath;int amount=0; };
struct QuestRuntimeEntry { std::string assetPath;QuestAssetData asset;QuestState state=QuestState::Inactive;std::vector<int> progress; };
struct QuestLogComponent { std::vector<QuestRuntimeEntry> quests;std::unordered_map<std::string,bool> flags;std::vector<QuestRuntimeEvent> events; };

bool GrantQuest(ecs::Registry& registry,ecs::Entity owner,const std::string& assetPath,std::string* error=nullptr);
bool GrantQuest(ecs::Registry& registry,ecs::Entity owner,const QuestAssetData& asset,const std::string& assetPath={});
bool StartQuest(ecs::Registry& registry,ecs::Entity owner,const std::string& quest);
bool AdvanceQuest(ecs::Registry& registry,ecs::Entity owner,const std::string& quest,const std::string& objective,int amount=1);
bool FailQuest(ecs::Registry& registry,ecs::Entity owner,const std::string& quest);
bool SetQuestFlag(ecs::Registry& registry,ecs::Entity owner,const std::string& flag,bool value);
QuestState GetQuestState(const ecs::Registry& registry,ecs::Entity owner,const std::string& quest);
int GetQuestProgress(const ecs::Registry& registry,ecs::Entity owner,const std::string& quest,const std::string& objective);
const char* QuestStateName(QuestState state);
std::vector<QuestRuntimeEvent> ConsumeQuestEvents(ecs::Registry& registry,ecs::Entity owner);
std::string SerializeQuestState(const ecs::Registry& registry,ecs::Entity owner);
bool RestoreQuestState(ecs::Registry& registry,ecs::Entity owner,const std::string& data);

} // namespace engine
