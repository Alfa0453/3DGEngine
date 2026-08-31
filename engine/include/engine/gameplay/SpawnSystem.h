#pragma once
#include "engine/assets/SpawnAsset.h"
#include "engine/ecs/Entity.h"
#include <string>
#include <vector>
namespace engine{namespace ecs{class Registry;}
enum class SpawnEventType:std::uint8_t{EncounterStarted,WaveStarted,EntitySpawned,WaveCleared,EncounterCompleted,EncounterStopped,SpawnFailed};
struct SpawnEvent{SpawnEventType type=SpawnEventType::EntitySpawned;ecs::Entity entity=ecs::kNull;int wave=-1;std::string name,message;};
struct SpawnedEntityComponent{ecs::Entity manager=ecs::kNull;int entry=-1;bool pooled=false;};
struct SpawnManagerComponent{std::string assetPath;SpawnAssetData asset;bool running=false,completed=false,playerInside=false;int wave=-1,spawnedInWave=0,totalSpawned=0;float timer=0,cooldown=0,difficulty=1.0f;std::uint32_t randomState=1;std::vector<ecs::Entity> managed;std::vector<SpawnEvent> events;};
bool ConfigureSpawnManager(ecs::Registry& registry,ecs::Entity owner,const std::string& path,std::string* error=nullptr);
bool ConfigureSpawnManager(ecs::Registry& registry,ecs::Entity owner,const SpawnAssetData& asset,const std::string& path={});
bool StartSpawnEncounter(ecs::Registry& registry,ecs::Entity owner,float difficulty=1.0f);
void StopSpawnEncounter(ecs::Registry& registry,ecs::Entity owner);
void ResetSpawnEncounter(ecs::Registry& registry,ecs::Entity owner);
bool TriggerSpawnWave(ecs::Registry& registry,ecs::Entity owner,int wave=-1);
void SetSpawnDifficulty(ecs::Registry& registry,ecs::Entity owner,float difficulty);
void UpdateSpawnManagers(ecs::Registry& registry,float dt,ecs::Entity player=ecs::kNull);
int SpawnAliveCount(const ecs::Registry& registry,ecs::Entity owner);
bool SpawnEncounterRunning(const ecs::Registry& registry,ecs::Entity owner);
std::vector<SpawnEvent> ConsumeSpawnEvents(ecs::Registry& registry,ecs::Entity owner);
}
