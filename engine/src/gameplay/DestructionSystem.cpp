#include "engine/gameplay/DestructionSystem.h"

#include "engine/assets/ParticleAsset.h"
#include "engine/core/Paths.h"
#include "engine/ecs/Components.h"
#include "engine/ecs/Registry.h"
#include "engine/physics/PhysicsComponents.h"

#include <algorithm>
#include <filesystem>

namespace engine {
namespace {
std::string Resolve(const std::string& path){std::filesystem::path p(path);if(std::filesystem::exists(p))return p.string();const std::filesystem::path exe(ExecutableDir());const std::filesystem::path candidates[]={exe/p,exe/"Content"/p,exe.parent_path()/p};for(const auto& candidate:candidates)if(std::filesystem::exists(candidate))return candidate.string();return path;}

void SpawnEffects(ecs::Registry& registry,const glm::vec3& position,
                  const std::string& particlePath,const std::string& audioPath) {
    if(particlePath.empty()&&audioPath.empty())return;const ecs::Entity effect=registry.Create();
    registry.Add<ecs::Transform>(effect,ecs::Transform{position});
    registry.Add<ecs::RuntimeName>(effect,{"DestructionEffect"});float lifetime=0.f;
    if(!particlePath.empty()){ParticleSystemComponent particles;std::string ignored;if(LoadParticleAsset(Resolve(particlePath),&particles,&ignored)){particles.enabled=true;particles.autoplay=true;particles.loop=false;particles.initialized=false;particles.playing=false;particles.elapsed=0;lifetime=std::max(lifetime,particles.duration+particles.startDelay+1.f);registry.Add<ParticleSystemComponent>(effect,std::move(particles));}}
    if(!audioPath.empty()){ecs::AudioSource audio;audio.path=audioPath;audio.autoplay=true;audio.loop=false;audio.spatial=true;registry.Add<ecs::AudioSource>(effect,std::move(audio));lifetime=std::max(lifetime,10.f);}
    if(lifetime>0.f)registry.Add<DestructionDebrisLifetime>(effect,{lifetime});
}

void Break(ecs::Registry& registry,ecs::Entity entity,DestructibleComponent& component,
           const glm::vec3& hitPoint,const glm::vec3& impulse) {
    const ecs::Transform* source=registry.TryGet<ecs::Transform>(entity);
    if(!source)return;const ecs::Transform sourceTransform=*source;
    const glm::vec3 localImpact=glm::inverse(sourceTransform.rotation)*(hitPoint-sourceTransform.position);
    const auto chunks=GenerateDestructionChunks(component.asset,localImpact);
    const std::string mesh=component.asset.debrisMeshPath.empty()?component.asset.sourceMeshPath:component.asset.debrisMeshPath;
    const float mass=component.asset.debrisMass/std::max<std::size_t>(chunks.size(),1);
    for(const auto& chunk:chunks){
        const ecs::Entity debris=registry.Create();ecs::Transform transform;
        transform.position=sourceTransform.position+sourceTransform.rotation*chunk.localCenter;
        transform.rotation=sourceTransform.rotation;transform.scale=chunk.size;
        registry.Add<ecs::Transform>(debris,transform);if(!mesh.empty())registry.Add<ecs::ModelAsset>(debris,{mesh});
        const std::string material=component.asset.debrisMaterialPath.empty()?component.asset.sourceMaterialPath:component.asset.debrisMaterialPath;
        if(!material.empty())registry.Add<ecs::MaterialAsset>(debris,{material});
        registry.Add<ecs::RuntimeName>(debris,{"Debris_"+component.asset.name+"_"+std::to_string(chunk.index)});
        ecs::RigidBody body=ecs::RigidBody::Dynamic(std::max(mass,.001f));
        body.velocity=impulse*component.asset.impulseScale+chunk.impulseDirection*component.asset.scatterImpulse;
        body.angularVelocity=chunk.angularVelocity;body.ccd=glm::length(body.velocity)>20.f;
        registry.Add<ecs::RigidBody>(debris,body);
        if(component.asset.debrisLifetime>0.f)
            registry.Add<DestructionDebrisLifetime>(debris,{component.asset.debrisLifetime});
        if(component.asset.debrisCollision){auto collider=ecs::Collider::MakeBox(glm::vec3(.5f));collider.layer=ecs::CollisionLayer::WorldDynamic;collider.mask=ecs::CollisionLayer::All;registry.Add<ecs::Collider>(debris,collider);}
    }
    if(component.asset.removeSourceOnBreak){registry.Remove<ecs::ModelAsset>(entity);registry.Remove<ecs::LoadedModelAsset>(entity);registry.Remove<ecs::Collider>(entity);registry.Remove<ecs::RigidBody>(entity);}
    component.broken=true;component.health=0.f;component.state=static_cast<int>(component.asset.states.size());
    component.events.push_back({DestructionRuntimeEvent::Type::Broken,component.state,component.asset.breakParticlePath,component.asset.breakAudioPath});
    SpawnEffects(registry,hitPoint,component.asset.breakParticlePath,component.asset.breakAudioPath);
}
}

bool ConfigureDestructible(ecs::Registry& registry,ecs::Entity entity,const std::string& path,std::string* error){
    DestructionAssetData asset;if(!LoadDestructionAsset(Resolve(path),&asset,error))return false;return ConfigureDestructible(registry,entity,asset,path);
}
bool ConfigureDestructible(ecs::Registry& registry,ecs::Entity entity,const DestructionAssetData& source,const std::string& path){
    if(!registry.Valid(entity))return false;DestructionAssetData asset=source;NormalizeDestructionAsset(asset);if(!ValidateDestructionAsset(asset))return false;
    DestructibleComponent component;component.assetPath=path;component.asset=std::move(asset);component.health=component.asset.maxHealth;registry.Add<DestructibleComponent>(entity,std::move(component));return true;
}
bool DamageDestructible(ecs::Registry& registry,ecs::Entity entity,float damage,const glm::vec3& hitPoint,const glm::vec3& impulse){
    auto* component=registry.TryGet<DestructibleComponent>(entity);if(!component||component->broken||damage<component->asset.minimumDamage)return false;
    component->health=std::max(0.f,component->health-std::max(damage,0.f));const int next=DestructionStateForHealth(component->asset,component->health);
    if(component->health<=0.f){Break(registry,entity,*component,hitPoint,impulse);return true;}
    if(next!=component->state&&next>=0){component->state=next;const auto&s=component->asset.states[static_cast<std::size_t>(next)];if(!s.meshPath.empty()){registry.Add<ecs::ModelAsset>(entity,{s.meshPath});registry.Remove<ecs::LoadedModelAsset>(entity);}if(!s.materialPath.empty())registry.Add<ecs::MaterialAsset>(entity,{s.materialPath});component->events.push_back({DestructionRuntimeEvent::Type::DamagedState,next,s.particlePath,s.audioPath});SpawnEffects(registry,hitPoint,s.particlePath,s.audioPath);}
    return false;
}
bool ImpactDestructible(ecs::Registry& registry,ecs::Entity entity,float impact,const glm::vec3& point,const glm::vec3& direction){
    auto* c=registry.TryGet<DestructibleComponent>(entity);if(!c||impact<c->asset.impactThreshold)return false;return DamageDestructible(registry,entity,impact,point,direction*impact);
}
float DestructibleHealth(const ecs::Registry& registry,ecs::Entity entity){const auto*c=registry.TryGet<DestructibleComponent>(entity);return c?c->health:0.f;}
bool IsDestructibleBroken(const ecs::Registry& registry,ecs::Entity entity){const auto*c=registry.TryGet<DestructibleComponent>(entity);return c&&c->broken;}
std::vector<DestructionRuntimeEvent> ConsumeDestructionEvents(ecs::Registry& registry,ecs::Entity entity){auto*c=registry.TryGet<DestructibleComponent>(entity);if(!c)return{};auto events=std::move(c->events);c->events.clear();return events;}
void UpdateDestruction(ecs::Registry& registry,float deltaSeconds){const float dt=std::max(deltaSeconds,0.f);std::vector<ecs::Entity> expired;registry.view<DestructionDebrisLifetime>().each([&](ecs::Entity entity,DestructionDebrisLifetime& lifetime){lifetime.remaining-=dt;if(lifetime.remaining<=0.f)expired.push_back(entity);});for(ecs::Entity entity:expired)registry.Destroy(entity);}
} // namespace engine
