#include "engine/gameplay/SpawnSystem.h"
#include "engine/core/Paths.h"
#include "engine/ecs/Components.h"
#include "engine/ecs/Registry.h"
#include "engine/gameplay/GameplayComponents.h"
#include "engine/physics/PhysicsComponents.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
namespace engine{namespace{
std::string Resolve(const std::string&p){const std::filesystem::path path(p);if(std::filesystem::exists(path))return path.string();const std::filesystem::path e(ExecutableDir());for(const auto&c:{e/path,e/"Content"/path,e.parent_path()/path})if(std::filesystem::exists(c))return c.string();return p;}
void Push(SpawnManagerComponent&c,SpawnEventType type,ecs::Entity entity=ecs::kNull,const std::string&name={},const std::string&message={}){c.events.push_back({type,entity,c.wave,name,message});}
float Random01(SpawnManagerComponent&c){c.randomState=c.randomState*1664525u+1013904223u;return static_cast<float>((c.randomState>>8)&0x00ffffffu)/16777216.0f;}
ecs::Entity FindNamed(ecs::Registry&r,const std::string&name){ecs::Entity found=ecs::kNull;r.view<ecs::RuntimeName>().each([&](ecs::Entity e,ecs::RuntimeName&n){if(found==ecs::kNull&&n.value==name)found=e;});return found;}
bool Active(const ecs::Registry&r,ecs::Entity e){if(!r.Valid(e))return false;const auto*h=r.TryGet<Health>(e);return!h||h->alive;}
glm::vec3 SpawnPosition(SpawnManagerComponent&c,const ecs::Transform&t){if(c.asset.shape==SpawnShape::Point)return t.position;const float a=Random01(c)*glm::two_pi<float>();if(c.asset.shape==SpawnShape::Sphere){const float radius=std::sqrt(Random01(c))*c.asset.radius;return t.position+glm::vec3(std::cos(a)*radius,(Random01(c)*2-1)*c.asset.height,std::sin(a)*radius);}return t.position+glm::vec3((Random01(c)*2-1)*c.asset.boxExtents.x,(Random01(c)*2-1)*c.asset.boxExtents.y,(Random01(c)*2-1)*c.asset.boxExtents.z);}
bool Inside(const SpawnAssetData&a,const glm::vec3&center,const glm::vec3&p){const glm::vec3 d=p-center;if(a.shape==SpawnShape::Box)return std::abs(d.x)<=a.boxExtents.x&&std::abs(d.y)<=a.boxExtents.y&&std::abs(d.z)<=a.boxExtents.z;return glm::length(glm::vec2(d.x,d.z))<=a.radius&&std::abs(d.y)<=std::max(a.height,.1f);}
int EntryAlive(const ecs::Registry&r,const SpawnManagerComponent&c,int entry){int n=0;for(auto e:c.managed){const auto*s=r.TryGet<SpawnedEntityComponent>(e);if(s&&s->entry==entry&&Active(r,e))++n;}return n;}
void PruneManaged(ecs::Registry&r,SpawnManagerComponent&c){
    std::vector<int> pooled(c.asset.entries.size(),0);
    for(auto&entity:c.managed){
        if(!r.Valid(entity)){entity=ecs::kNull;continue;}
        const auto*spawned=r.TryGet<SpawnedEntityComponent>(entity);
        const auto*health=r.TryGet<Health>(entity);
        if(!spawned||!health||health->alive)continue;
        const bool validEntry=spawned->entry>=0&&spawned->entry<static_cast<int>(c.asset.entries.size());
        const int limit=c.asset.recycleDead&&validEntry?c.asset.entries[static_cast<std::size_t>(spawned->entry)].poolLimit:0;
        if(validEntry&&pooled[static_cast<std::size_t>(spawned->entry)]<limit){
            ++pooled[static_cast<std::size_t>(spawned->entry)];
            continue;
        }
        r.Destroy(entity);
        entity=ecs::kNull;
    }
    c.managed.erase(std::remove_if(c.managed.begin(),c.managed.end(),[&](ecs::Entity entity){return entity==ecs::kNull||!r.Valid(entity);}),c.managed.end());
}
int Choose(SpawnManagerComponent&c,const std::string&group,const ecs::Registry&r){float total=0;for(std::size_t i=0;i<c.asset.entries.size();++i){const auto&e=c.asset.entries[i];if(e.group==group&&c.difficulty>=e.minimumDifficulty&&c.difficulty<=e.maximumDifficulty&&EntryAlive(r,c,static_cast<int>(i))<e.maximumAlive)total+=e.weight;}if(total<=0)return-1;float pick=Random01(c)*total;for(std::size_t i=0;i<c.asset.entries.size();++i){const auto&e=c.asset.entries[i];if(e.group!=group||c.difficulty<e.minimumDifficulty||c.difficulty>e.maximumDifficulty||EntryAlive(r,c,static_cast<int>(i))>=e.maximumAlive)continue;pick-=e.weight;if(pick<=0)return static_cast<int>(i);}return-1;}
ecs::Entity SpawnOne(ecs::Registry&r,ecs::Entity owner,SpawnManagerComponent&c,int index){const auto&entry=c.asset.entries[static_cast<std::size_t>(index)];auto*managerTransform=r.TryGet<ecs::Transform>(owner);if(!managerTransform)return ecs::kNull;ecs::Entity entity=ecs::kNull;if(c.asset.recycleDead&&entry.poolLimit>0)for(auto e:c.managed){const auto*s=r.TryGet<SpawnedEntityComponent>(e);auto*h=r.TryGet<Health>(e);if(s&&s->entry==index&&h&&!h->alive){entity=e;h->Reset(h->maxHp);break;}}if(entity==ecs::kNull){const ecs::Entity prototype=FindNamed(r,entry.prototypeName);if(prototype==ecs::kNull)return ecs::kNull;entity=r.Clone(prototype);r.Remove<SpawnManagerComponent>(entity);r.Add<SpawnedEntityComponent>(entity,{owner,index,c.asset.recycleDead&&entry.poolLimit>0});c.managed.push_back(entity);}auto*transform=r.TryGet<ecs::Transform>(entity);if(!transform)transform=&r.Add<ecs::Transform>(entity,{});transform->position=SpawnPosition(c,*managerTransform);if(c.asset.randomYaw)transform->rotation=glm::angleAxis(Random01(c)*glm::two_pi<float>(),glm::vec3(0,1,0));if(auto*body=r.TryGet<ecs::RigidBody>(entity)){body->velocity={};body->angularVelocity={};body->sleeping=false;body->sleepTimer=0;}if(auto*name=r.TryGet<ecs::RuntimeName>(entity))name->value=entry.name+"_"+std::to_string(c.totalSpawned+1);return entity;}
}
bool ConfigureSpawnManager(ecs::Registry&r,ecs::Entity o,const std::string&p,std::string*e){SpawnAssetData a;if(!LoadSpawnAsset(Resolve(p),&a,e))return false;return ConfigureSpawnManager(r,o,a,p);}
bool ConfigureSpawnManager(ecs::Registry&r,ecs::Entity o,const SpawnAssetData&source,const std::string&p){if(!r.Valid(o))return false;SpawnAssetData a=source;NormalizeSpawnAsset(a);if(!ValidateSpawnAsset(a))return false;SpawnManagerComponent value;value.assetPath=p;value.asset=std::move(a);value.randomState=value.asset.seed?value.asset.seed:1;if(auto*c=r.TryGet<SpawnManagerComponent>(o))*c=std::move(value);else r.Add<SpawnManagerComponent>(o,std::move(value));return true;}
bool StartSpawnEncounter(ecs::Registry&r,ecs::Entity o,float difficulty){auto*c=r.TryGet<SpawnManagerComponent>(o);if(!c||c->asset.waves.empty()||(c->completed&&c->asset.oneShot))return false;c->running=true;c->completed=false;c->difficulty=std::max(difficulty,0.f);c->wave=0;c->spawnedInWave=0;c->timer=c->asset.waves[0].delayBefore;c->randomState=c->asset.seed?c->asset.seed:1;Push(*c,SpawnEventType::EncounterStarted);Push(*c,SpawnEventType::WaveStarted,ecs::kNull,c->asset.waves[0].name);return true;}
void StopSpawnEncounter(ecs::Registry&r,ecs::Entity o){if(auto*c=r.TryGet<SpawnManagerComponent>(o)){c->running=false;Push(*c,SpawnEventType::EncounterStopped);}}
void ResetSpawnEncounter(ecs::Registry&r,ecs::Entity o){auto*c=r.TryGet<SpawnManagerComponent>(o);if(!c)return;c->running=false;c->completed=false;c->wave=-1;c->spawnedInWave=0;c->totalSpawned=0;c->timer=0;c->cooldown=0;c->randomState=c->asset.seed?c->asset.seed:1;c->events.clear();}
bool TriggerSpawnWave(ecs::Registry&r,ecs::Entity o,int wave){auto*c=r.TryGet<SpawnManagerComponent>(o);if(!c||c->asset.waves.empty())return false;if(wave<0)wave=c->wave<0?0:c->wave;wave=std::clamp(wave,0,static_cast<int>(c->asset.waves.size())-1);c->running=true;c->completed=false;c->wave=wave;c->spawnedInWave=0;c->timer=c->asset.waves[static_cast<std::size_t>(wave)].delayBefore;Push(*c,SpawnEventType::WaveStarted,ecs::kNull,c->asset.waves[static_cast<std::size_t>(wave)].name);return true;}
void SetSpawnDifficulty(ecs::Registry&r,ecs::Entity o,float d){if(auto*c=r.TryGet<SpawnManagerComponent>(o))c->difficulty=std::max(d,0.f);}
int SpawnAliveCount(const ecs::Registry&r,ecs::Entity o){const auto*c=r.TryGet<SpawnManagerComponent>(o);if(!c)return 0;int n=0;for(auto e:c->managed)if(Active(r,e))++n;return n;}
bool SpawnEncounterRunning(const ecs::Registry&r,ecs::Entity o){const auto*c=r.TryGet<SpawnManagerComponent>(o);return c&&c->running;}
void UpdateSpawnManagers(ecs::Registry&r,float dt,ecs::Entity player){r.view<SpawnManagerComponent>().each([&](ecs::Entity owner,SpawnManagerComponent&c){const float step=std::max(dt,0.f);c.cooldown=std::max(0.f,c.cooldown-step);PruneManaged(r,c);const auto*origin=r.TryGet<ecs::Transform>(owner);const auto*playerTransform=r.TryGet<ecs::Transform>(player);const bool inside=origin&&playerTransform&&Inside(c.asset,origin->position,playerTransform->position);if(!c.running){if(c.asset.triggerOnPlayerEnter&&inside&&!c.playerInside&&c.cooldown<=0)StartSpawnEncounter(r,owner,c.difficulty);else if(c.asset.autoStart&&!c.completed&&c.wave<0)StartSpawnEncounter(r,owner,c.difficulty);c.playerInside=inside;return;}c.playerInside=inside;if(c.wave<0||c.wave>=static_cast<int>(c.asset.waves.size())){c.running=false;return;}auto&wave=c.asset.waves[static_cast<std::size_t>(c.wave)];c.timer-=step;if(c.spawnedInWave<wave.count&&c.totalSpawned<c.asset.maximumTotal){if(c.timer<=0&&SpawnAliveCount(r,owner)<c.asset.maximumConcurrent){const int index=Choose(c,wave.group,r);const ecs::Entity spawned=index>=0?SpawnOne(r,owner,c,index):ecs::kNull;++c.spawnedInWave;if(spawned!=ecs::kNull){++c.totalSpawned;Push(c,SpawnEventType::EntitySpawned,spawned,c.asset.entries[static_cast<std::size_t>(index)].name);}else Push(c,SpawnEventType::SpawnFailed,ecs::kNull,wave.name,"No eligible entry or named prototype.");c.timer=c.spawnedInWave>=wave.count?wave.delayAfter:wave.spawnInterval;}return;}if(c.spawnedInWave<wave.count&&c.totalSpawned>=c.asset.maximumTotal){c.spawnedInWave=wave.count;c.timer=wave.delayAfter;}if(wave.waitForClear&&SpawnAliveCount(r,owner)>0)return;if(c.timer>0)return;Push(c,SpawnEventType::WaveCleared,ecs::kNull,wave.name);int next=c.wave+1;if(next>=static_cast<int>(c.asset.waves.size())&&c.asset.loopWaves&&c.totalSpawned<c.asset.maximumTotal)next=0;if(next<static_cast<int>(c.asset.waves.size())){c.wave=next;c.spawnedInWave=0;c.timer=c.asset.waves[static_cast<std::size_t>(next)].delayBefore;Push(c,SpawnEventType::WaveStarted,ecs::kNull,c.asset.waves[static_cast<std::size_t>(next)].name);}else{c.running=false;c.completed=true;c.cooldown=c.asset.retriggerCooldown;Push(c,SpawnEventType::EncounterCompleted);}});}
std::vector<SpawnEvent>ConsumeSpawnEvents(ecs::Registry&r,ecs::Entity o){auto*c=r.TryGet<SpawnManagerComponent>(o);if(!c)return{};auto events=std::move(c->events);c->events.clear();return events;}
}
