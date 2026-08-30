#pragma once

#include "engine/assets/DialogueAsset.h"
#include "engine/ecs/Entity.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace engine { namespace ecs { class Registry; }

struct DialogueSourceComponent { std::string assetPath;DialogueAssetData asset; };
enum class DialogueEventType : std::uint8_t { Started, NodeEntered, NodeExited, ChoiceSelected, ScriptEvent, CameraHook, Ended, Cancelled };
struct DialogueRuntimeEvent { DialogueEventType type=DialogueEventType::Started;std::string dialogue,node,name;int choice=-1; };
struct DialogueSessionComponent { std::string assetPath;DialogueAssetData asset;std::string currentNode;bool active=false;ecs::Entity source=ecs::kNull;std::unordered_map<std::string,bool> flags;std::vector<std::string> history;std::vector<DialogueRuntimeEvent> events; };

bool ConfigureDialogueSource(ecs::Registry& registry,ecs::Entity source,const std::string& assetPath,std::string* error=nullptr);
bool StartDialogue(ecs::Registry& registry,ecs::Entity listener,const std::string& assetPath,ecs::Entity source=ecs::kNull,std::string* error=nullptr);
bool StartDialogue(ecs::Registry& registry,ecs::Entity listener,ecs::Entity source,std::string* error=nullptr);
bool ChooseDialogueOption(ecs::Registry& registry,ecs::Entity listener,int choiceIndex);
bool ContinueDialogue(ecs::Registry& registry,ecs::Entity listener);
bool CancelDialogue(ecs::Registry& registry,ecs::Entity listener);
bool SetDialogueFlag(ecs::Registry& registry,ecs::Entity listener,const std::string& flag,bool value);
bool IsDialogueActive(const ecs::Registry& registry,ecs::Entity listener);
const DialogueNode* CurrentDialogueNode(const ecs::Registry& registry,ecs::Entity listener);
std::vector<int> AvailableDialogueChoices(const ecs::Registry& registry,ecs::Entity listener);
std::vector<DialogueRuntimeEvent> ConsumeDialogueEvents(ecs::Registry& registry,ecs::Entity listener);
std::string SerializeDialogueState(const ecs::Registry& registry,ecs::Entity listener);
bool RestoreDialogueState(ecs::Registry& registry,ecs::Entity listener,const std::string& data);

} // namespace engine
