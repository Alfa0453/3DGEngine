#include "engine/gameplay/AbilitySystem.h"

#include "engine/animation/AnimatedModel.h"
#include "engine/core/Paths.h"
#include "engine/ecs/Components.h"
#include "engine/ecs/Registry.h"
#include "engine/gameplay/GameplayComponents.h"
#include "engine/physics/PhysicsComponents.h"
#include "engine/graphics/SkinnedModel.h"

#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <filesystem>

namespace engine {
namespace {
AbilitySlot* FindSlot(AbilityComponent& component,const std::string& name){
    for(auto& slot:component.abilities)if(slot.asset.name==name||slot.assetPath==name)return &slot;
    return nullptr;
}
const AbilitySlot* FindSlot(const AbilityComponent& component,const std::string& name){
    for(const auto& slot:component.abilities)if(slot.asset.name==name||slot.assetPath==name)return &slot;
    return nullptr;
}
std::string ResolvePath(const std::string& path){
    std::filesystem::path p(path);if(std::filesystem::exists(p))return p.string();
    const std::filesystem::path exe=ExecutableDir();
    const std::filesystem::path candidates[]={exe/p,exe/"Content"/p,exe.parent_path()/p};
    for(const auto& c:candidates)if(std::filesystem::exists(c))return c.string();return path;
}
std::vector<ecs::Entity> Targets(ecs::Registry& registry,ecs::Entity owner,
                                 ecs::Entity explicitTarget,const AbilityEffect& effect){
    if(effect.target==AbilityTargetMode::Self)return{owner};
    if(effect.target==AbilityTargetMode::ExplicitTarget)
        return explicitTarget!=ecs::kNull&&registry.Valid(explicitTarget)
            ?std::vector<ecs::Entity>{explicitTarget}:std::vector<ecs::Entity>{};
    std::vector<ecs::Entity> result;const ecs::Transform* origin=registry.TryGet<ecs::Transform>(owner);
    if(!origin)return result;const float radius=std::max(effect.radius,0.f);
    registry.view<ecs::Transform,Health>().each([&](ecs::Entity e,const ecs::Transform& t,Health&){
        if(e!=owner&&glm::distance(t.position,origin->position)<=radius)result.push_back(e);
    });return result;
}
void Emit(AbilityComponent& component,const AbilityEffect& effect,ecs::Entity owner,
          ecs::Entity target,const glm::vec3& position){
    component.events.push_back({effect.name,effect.type,owner,target,position,
                                effect.magnitude,effect.assetPath});
}
void Execute(ecs::Registry& registry,ecs::Entity owner,ecs::Entity explicitTarget,
             const AbilityEffect& effect,AbilityComponent& component){
    const ecs::Transform* ownerTransform=registry.TryGet<ecs::Transform>(owner);
    const glm::vec3 origin=ownerTransform?ownerTransform->position:glm::vec3(0);
    if(effect.type==AbilityEffectType::AnimationAction){
        if(AnimatedModel* animated=registry.TryGet<AnimatedModel>(owner)) {
            int clip = -1;
            if (animated->model) {
                const auto& animations = animated->model->Animations();
                for (std::size_t i = 0; i < animations.size(); ++i)
                    if (animations[i].name == effect.name) { clip = static_cast<int>(i); break; }
            }
            if (clip >= 0) animated->PlayAction(clip, {}, {}, 0.08f, 0.15f, 1.f);
        }
        Emit(component,effect,owner,owner,origin);return;
    }
    if(effect.type==AbilityEffectType::Projectile){
        glm::vec3 direction=effect.direction;
        if(explicitTarget!=ecs::kNull)if(const ecs::Transform* target=registry.TryGet<ecs::Transform>(explicitTarget))
            direction=target->position-origin;
        else if(ownerTransform)direction=ownerTransform->rotation*direction;
        if(glm::length2(direction)<.000001f)direction={0,0,1};direction=glm::normalize(direction);
        const ecs::Entity projectile=registry.Create();
        registry.Add<ecs::Transform>(projectile,ecs::Transform{origin+direction*.25f});
        Projectile p;p.owner=owner;p.dir=direction;p.speed=effect.speed;p.range=effect.range;
        p.damage=effect.magnitude;p.radius=std::max(effect.radius,.05f);registry.Add<Projectile>(projectile,p);
        ecs::Collider collider=ecs::Collider::MakeSphere(p.radius);collider.layer=ecs::CollisionLayer::Projectile;
        registry.Add<ecs::Collider>(projectile,collider);registry.Add<ecs::RuntimeName>(projectile,{effect.name.empty()?"AbilityProjectile":effect.name});
        Emit(component,effect,owner,projectile,origin);return;
    }
    const auto targets=Targets(registry,owner,explicitTarget,effect);
    if((effect.type==AbilityEffectType::Particle||effect.type==AbilityEffectType::Audio
        ||effect.type==AbilityEffectType::ScriptEvent)&&targets.empty()){
        Emit(component,effect,owner,explicitTarget,origin);return;
    }
    for(ecs::Entity target:targets){
        glm::vec3 position=origin;if(const ecs::Transform* t=registry.TryGet<ecs::Transform>(target))position=t->position;
        if(effect.type==AbilityEffectType::Damage){if(Health* h=registry.TryGet<Health>(target))h->Damage(std::max(effect.magnitude,0.f));}
        else if(effect.type==AbilityEffectType::Heal){if(Health* h=registry.TryGet<Health>(target)){
            h->hp=std::min(h->maxHp,h->hp+std::max(effect.magnitude,0.f));if(h->hp>0)h->alive=true;}}
        else if(effect.type==AbilityEffectType::Impulse){if(ecs::RigidBody* body=registry.TryGet<ecs::RigidBody>(target)){
            glm::vec3 direction=effect.direction;if(ownerTransform)if(const ecs::Transform* t=registry.TryGet<ecs::Transform>(target))direction=t->position-origin;
            if(glm::length2(direction)>.000001f)body->velocity+=glm::normalize(direction)*effect.magnitude;}}
        Emit(component,effect,owner,target,position);
    }
}
}

bool GrantAbility(ecs::Registry& registry,ecs::Entity owner,const AbilityAssetData& ability,
                  const std::string& path){
    if(!registry.Valid(owner)||ability.name.empty()||ability.phases.empty())return false;
    AbilityComponent* component=registry.TryGet<AbilityComponent>(owner);
    if(!component)component=&registry.Add<AbilityComponent>(owner,{});
    for(AbilitySlot& slot:component->abilities)if(slot.asset.name==ability.name){slot.asset=ability;slot.assetPath=path;return true;}
    AbilitySlot slot;slot.asset=ability;slot.assetPath=path;slot.charges=std::max(ability.maxCharges,1);
    component->abilities.push_back(std::move(slot));return true;
}
bool GrantAbility(ecs::Registry& registry,ecs::Entity owner,const std::string& path,std::string* error){
    AbilityAssetData asset;if(!LoadAbilityAsset(ResolvePath(path),&asset,error))return false;
    return GrantAbility(registry,owner,asset,path);
}
bool ActivateAbility(ecs::Registry& registry,ecs::Entity owner,const std::string& name,ecs::Entity target){
    AbilityComponent* component=registry.TryGet<AbilityComponent>(owner);if(!component||component->active>=0)return false;
    AbilitySlot* slot=FindSlot(*component,name);if(!slot||slot->charges<=0||slot->cooldownRemaining>0)return false;
    const AbilityAssetData& a=slot->asset;if(a.requireTarget&&(target==ecs::kNull||!registry.Valid(target)))return false;
    if(a.requireTarget){const auto* o=registry.TryGet<ecs::Transform>(owner);const auto* t=registry.TryGet<ecs::Transform>(target);
        if(!o||!t||glm::distance(o->position,t->position)>a.activationRange)return false;}
    AbilityResource* resource=registry.TryGet<AbilityResource>(owner);
    if((a.manaCost>0||a.staminaCost>0)&&!resource)return false;
    Health* health=registry.TryGet<Health>(owner);
    if(resource&&(resource->mana<a.manaCost||resource->stamina<a.staminaCost))return false;
    if(a.healthCost>0&&(!health||health->hp<=a.healthCost))return false;
    if(resource){resource->mana-=a.manaCost;resource->stamina-=a.staminaCost;}if(health)health->hp-=a.healthCost;
    --slot->charges;slot->cooldownRemaining=a.cooldown;if(slot->charges<std::max(a.maxCharges,1)&&slot->rechargeRemaining<=0)
        slot->rechargeRemaining=std::max(a.chargeRecovery,.0001f);
    component->active=static_cast<int>(slot-component->abilities.data());component->phase=0;component->phaseTime=0;
    component->target=target;component->activationHealth=health?health->hp:0.f;
    component->fired.assign(a.phases[0].effects.size(),false);return true;
}
bool CancelAbility(ecs::Registry& registry,ecs::Entity owner){
    AbilityComponent* c=registry.TryGet<AbilityComponent>(owner);if(!c||c->active<0)return false;
    const AbilityPhase& phase=c->abilities[static_cast<std::size_t>(c->active)].asset.phases[static_cast<std::size_t>(c->phase)];
    if(!phase.interruptible)return false;c->active=c->phase=-1;c->phaseTime=0;c->fired.clear();return true;
}
bool IsAbilityActive(const ecs::Registry& registry,ecs::Entity owner,const std::string& name){
    const AbilityComponent* c=registry.TryGet<AbilityComponent>(owner);if(!c||c->active<0)return false;
    return name.empty()||c->abilities[static_cast<std::size_t>(c->active)].asset.name==name;
}
float AbilityCooldownRemaining(const ecs::Registry& registry,ecs::Entity owner,const std::string& name){
    const AbilityComponent* c=registry.TryGet<AbilityComponent>(owner);if(!c)return 0;const AbilitySlot* s=FindSlot(*c,name);return s?s->cooldownRemaining:0;
}
void UpdateAbilities(ecs::Registry& registry,float delta){
    const float dt=std::max(delta,0.f);registry.view<AbilityComponent>().each([&](ecs::Entity owner,AbilityComponent& c){
        for(AbilitySlot& s:c.abilities){s.cooldownRemaining=std::max(0.f,s.cooldownRemaining-dt);
            if(s.charges<std::max(s.asset.maxCharges,1)){s.rechargeRemaining-=dt;if(s.rechargeRemaining<=0){++s.charges;
                if(s.charges<std::max(s.asset.maxCharges,1))s.rechargeRemaining=std::max(s.asset.chargeRecovery,.0001f);}}}
        if(c.active<0||c.active>=static_cast<int>(c.abilities.size()))return;
        AbilitySlot& slot=c.abilities[static_cast<std::size_t>(c.active)];
        if(c.phase<0||c.phase>=static_cast<int>(slot.asset.phases.size())){c.active=c.phase=-1;return;}
        AbilityPhase& phase=slot.asset.phases[static_cast<std::size_t>(c.phase)];const float before=c.phaseTime;c.phaseTime+=dt;
        if(slot.asset.cancelOnDamage&&phase.interruptible)if(const Health* health=registry.TryGet<Health>(owner))
            if(health->hp<c.activationHealth){c.active=c.phase=-1;c.phaseTime=0;c.fired.clear();return;}
        if(c.fired.size()!=phase.effects.size())c.fired.assign(phase.effects.size(),false);
        for(std::size_t i=0;i<phase.effects.size();++i)if(!c.fired[i]&&phase.effects[i].time<=c.phaseTime){
            c.fired[i]=true;Execute(registry,owner,c.target,phase.effects[i],c);}
        if(c.phaseTime>=phase.duration){c.phaseTime=std::max(0.f,c.phaseTime-phase.duration);++c.phase;
            if(c.phase>=static_cast<int>(slot.asset.phases.size())){c.active=c.phase=-1;c.phaseTime=0;c.fired.clear();}
            else c.fired.assign(slot.asset.phases[static_cast<std::size_t>(c.phase)].effects.size(),false);}
        (void)before;
    });
}
std::vector<AbilityRuntimeEvent> ConsumeAbilityEvents(ecs::Registry& registry,ecs::Entity owner){
    AbilityComponent* c=registry.TryGet<AbilityComponent>(owner);if(!c)return{};auto events=std::move(c->events);c->events.clear();return events;
}
} // namespace engine
