#include <engine/assets/AbilityAsset.h>
#include <engine/assets/AssetRegistry.h>
#include <engine/ecs/Components.h>
#include <engine/ecs/Registry.h>
#include <engine/gameplay/AbilitySystem.h>
#include <engine/gameplay/GameplayComponents.h>

#include <filesystem>
#include <iostream>

namespace { int failures=0;void Check(bool v,const char* m){if(!v){++failures;std::cerr<<"FAIL: "<<m<<'\n';}} }

int main(){
    engine::AbilityAssetData fireball;fireball.name="Fireball";fireball.description="Test spell";
    fireball.cooldown=1.f;fireball.maxCharges=2;fireball.chargeRecovery=.5f;fireball.manaCost=10;fireball.requireTarget=true;fireball.activationRange=10;
    engine::AbilityPhase cast;cast.name="Cast";cast.duration=.2f;
    engine::AbilityEffect damage;damage.type=engine::AbilityEffectType::Damage;damage.target=engine::AbilityTargetMode::ExplicitTarget;damage.time=.1f;damage.magnitude=25;damage.name="Hit";cast.effects.push_back(damage);fireball.phases.push_back(cast);
    const auto content=std::filesystem::temp_directory_path()/"3dg_ability_test_content";
    const auto path=content/"Abilities"/"Fireball.3dgability";std::string error;
    Check(engine::SaveAbilityAsset(path.string(),fireball,&error),"ability saves");
    engine::AbilityAssetData loaded;Check(engine::LoadAbilityAsset(path.string(),&loaded,&error),"ability loads");
    Check(loaded.phases.size()==1&&loaded.phases[0].effects.size()==1,"phases and effects round trip");
    engine::AssetRegistry assets;Check(assets.RebuildFromContent(content.string(),&error),"registry scans ability");
    const auto* entry=assets.Find(fireball.assetId);Check(entry&&entry->type==engine::AssetType::Ability,"ability has registry type");

    engine::ecs::Registry registry;const auto owner=registry.Create(),target=registry.Create();
    registry.Add<engine::ecs::Transform>(owner,{});engine::ecs::Transform targetTransform;targetTransform.position={0,0,5};registry.Add<engine::ecs::Transform>(target,targetTransform);
    registry.Add<engine::Health>(owner,{});registry.Add<engine::Health>(target,{});registry.Add<engine::AbilityResource>(owner,{});
    Check(engine::GrantAbility(registry,owner,loaded,path.string()),"ability grants");
    Check(engine::ActivateAbility(registry,owner,"Fireball",target),"ability activates with valid target");
    Check(registry.Get<engine::AbilityResource>(owner).mana==90.f,"activation consumes mana");
    engine::UpdateAbilities(registry,.05f);Check(registry.Get<engine::Health>(target).hp==100.f,"effect waits for authored time");
    engine::UpdateAbilities(registry,.06f);Check(registry.Get<engine::Health>(target).hp==75.f,"timed damage executes once");
    Check(!engine::ActivateAbility(registry,owner,"Fireball",target),"active ability cannot be spammed");
    engine::UpdateAbilities(registry,.2f);Check(!engine::IsAbilityActive(registry,owner),"ability completes phases");
    Check(engine::AbilityCooldownRemaining(registry,owner,"Fireball")>0,"cooldown remains after completion");
    const auto events=engine::ConsumeAbilityEvents(registry,owner);Check(events.size()==1&&events[0].name=="Hit","effect emits gameplay event");
    engine::UpdateAbilities(registry,1.f);Check(engine::ActivateAbility(registry,owner,"Fireball",target),"ability reactivates after cooldown");
    std::error_code ec;std::filesystem::remove_all(content,ec);
    if(failures)return 1;std::cout<<"ability system tests passed\n";return 0;
}
