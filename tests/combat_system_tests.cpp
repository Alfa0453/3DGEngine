#include <engine/assets/AssetRegistry.h>
#include <engine/assets/CombatAsset.h>
#include <engine/ecs/Registry.h>
#include <engine/gameplay/CombatSystem.h>
#include <engine/gameplay/GameplayComponents.h>

#include <cmath>
#include <filesystem>
#include <iostream>

namespace {
int failures=0;
void Check(bool value,const char* message){if(!value){++failures;std::cerr<<"FAILED: "<<message<<'\n';}}
bool Near(float a,float b){return std::abs(a-b)<0.001f;}
engine::CombatAssetData Profile(const std::string& name,int team){
    engine::CombatAssetData asset;asset.header.id=engine::AssetHandle::Generate();asset.name=name;asset.team=team;
    asset.damageTypes={{"Physical",1.5f,1.0f,0.0f},{"Fire",2.0f,1.25f,0.0f}};
    asset.combo={{"LightAttack",{}, {},"Physical",10.0f,5.0f,0.6f,0.2f,0.25f,0.5f,2.0f,0.35f,"HitLight"},
                 {"HeavyAttack",{}, {},"Fire",20.0f,10.0f,0.8f,0.3f,0.3f,0.65f,2.5f,0.45f,"HitHeavy"}};
    return asset;
}
}

int main(){
    const auto root=std::filesystem::temp_directory_path()/"3dg_combat_test";std::error_code ec;std::filesystem::remove_all(root,ec);std::string error;
    auto saved=Profile("WizardCombat",1);saved.resistances={{"Fire",0.5f}};saved.combo[0].actionClipId=engine::AssetHandle::Generate();saved.combo[0].actionClipPath="Animation/Attack.3dgaction";
    const auto path=root/"WizardCombat.3dgcombat";
    Check(engine::SaveCombatAsset(path.string(),saved,&error),"combat profile saves");
    engine::CombatAssetData loaded;Check(engine::LoadCombatAsset(path.string(),&loaded,&error),"combat profile loads");
    Check(loaded.combo.size()==2&&loaded.damageTypes.size()==2&&loaded.resistances.size()==1,"combat profile fields round trip");
    Check(loaded.header.dependencies.size()==1,"combat asset dependencies are stored");
    engine::AssetRegistry assets;Check(assets.RebuildFromContent(root.string(),&error),"registry scans combat profiles");
    const auto* entry=assets.Find(saved.header.id);Check(entry&&entry->type==engine::AssetType::Combat,"registry identifies combat profile");

    engine::ecs::Registry world;const auto attacker=world.Create(),target=world.Create(),friendlyTarget=world.Create();
    auto attack=Profile("Attacker",1);attack.staggerRecovery=0.75f;
    auto defense=Profile("Defender",2);defense.resistances={{"Physical",0.5f}};defense.immunityAfterHit=0.1f;defense.poise=12.0f;defense.blockReduction=0.75f;defense.parryWindow=0.15f;
    auto friendly=Profile("Friendly",1);
    Check(engine::ConfigureCombat(world,attacker,attack),"attacker configures");
    Check(engine::ConfigureCombat(world,target,defense),"target configures");
    Check(engine::ConfigureCombat(world,friendlyTarget,friendly),"friendly configures");
    world.Add<engine::Health>(target,engine::Health{100,100,true,false});world.Add<engine::Health>(friendlyTarget,engine::Health{100,100,true,false});

    Check(engine::ApplyCombatDamage(world,attacker,friendlyTarget,10,"Physical")==engine::CombatResult::Friendly,"friendly fire is rejected");
    Check(Near(world.Get<engine::Health>(friendlyTarget).hp,100),"friendly target takes no damage");
    Check(engine::ApplyCombatDamage(world,attacker,target,10,"Physical")==engine::CombatResult::Hit,"hostile hit succeeds");
    Check(Near(world.Get<engine::Health>(target).hp,92.5f),"damage type and resistance multipliers apply");
    Check(engine::ApplyCombatDamage(world,attacker,target,10,"Physical")==engine::CombatResult::Immune,"hit immunity prevents repeated damage");
    engine::UpdateCombat(world,0.11f);

    engine::SetCombatBlocking(world,target,true);engine::UpdateCombat(world,0.2f);
    Check(engine::ApplyCombatDamage(world,attacker,target,10,"Physical")==engine::CombatResult::Blocked,"late guard blocks instead of parrying");
    Check(Near(world.Get<engine::Health>(target).hp,90.625f),"block damage reduction applies");
    engine::SetCombatBlocking(world,target,false);engine::UpdateCombat(world,0.11f);engine::SetCombatBlocking(world,target,true);
    Check(engine::ApplyCombatDamage(world,attacker,target,10,"Physical")==engine::CombatResult::Parried,"fresh guard parries");
    Check(engine::IsCombatStaggered(world,attacker),"parry staggers attacker");

    engine::UpdateCombat(world,0.8f);engine::SetCombatBlocking(world,target,false);engine::UpdateCombat(world,0.11f);
    Check(engine::StartCombatCombo(world,attacker,target),"combo starts");
    Check(engine::CombatComboStep(world,attacker)==0,"combo begins at first step");
    engine::UpdateCombat(world,0.26f);Check(engine::AdvanceCombatCombo(world,attacker),"combo advances inside input window");
    Check(engine::CombatComboStep(world,attacker)==1,"combo reaches second step");
    engine::UpdateCombat(world,0.31f);Check(engine::ExecuteCombatHit(world,attacker,target)==engine::CombatResult::Hit,"combo step executes hit");
    auto events=engine::ConsumeCombatEvents(world,attacker);bool started=false,advanced=false,window=false,hit=false,reaction=false;
    for(const auto& event:events){started|=event.type==engine::CombatEventType::ComboStarted;advanced|=event.type==engine::CombatEventType::ComboAdvanced;window|=event.type==engine::CombatEventType::AttackWindow;hit|=event.type==engine::CombatEventType::Hit;reaction|=event.type==engine::CombatEventType::Reaction;}
    Check(started&&advanced&&window&&hit&&reaction,"combo lifecycle events are emitted");

    std::filesystem::remove_all(root,ec);if(failures)return 1;std::cout<<"Combat system regression tests passed\n";return 0;
}
