#include <engine/assets/AssetRegistry.h>
#include <engine/assets/QuestAsset.h>
#include <engine/ecs/Registry.h>
#include <engine/gameplay/QuestSystem.h>

#include <filesystem>
#include <iostream>

namespace{int failures=0;void Check(bool v,const char* m){if(!v){++failures;std::cerr<<"FAIL: "<<m<<'\n';}}}
int main(){
    engine::QuestAssetData asset;asset.header.id=engine::AssetHandle::Generate();asset.name="WizardTrial";asset.title="The Wizard's Trial";asset.startConditions.push_back({"MetWizard",true});
    engine::QuestObjective talk;talk.id="Talk";talk.type=engine::QuestObjectiveType::Interact;talk.target="Wizard";talk.dialogueTrigger="WizardIntro";talk.checkpoint=true;
    engine::QuestObjective crystals;crystals.id="Crystals";crystals.type=engine::QuestObjectiveType::Collect;crystals.target="Crystal";crystals.requiredCount=3;crystals.requiredObjective="Talk";
    engine::QuestObjective bonus;bonus.id="Bonus";bonus.optional=true;asset.objectives={talk,crystals,bonus};
    engine::QuestReward reward;reward.type=engine::QuestRewardType::Ability;reward.name="Fireball";reward.assetId=engine::AssetHandle::Generate();reward.assetPath="Abilities/Fireball.3dgability";asset.rewards.push_back(reward);
    std::string error;Check(engine::ValidateQuestAsset(asset,&error),"quest validates");const auto root=std::filesystem::temp_directory_path()/"3dg_quest_test";const auto path=root/"WizardTrial.3dgquest";Check(engine::SaveQuestAsset(path.string(),asset,&error),"quest saves");engine::QuestAssetData loaded;Check(engine::LoadQuestAsset(path.string(),&loaded,&error),"quest loads");Check(loaded.objectives.size()==3&&loaded.rewards.size()==1,"quest content round trips");
    engine::AssetRegistry registryAssets;Check(registryAssets.RebuildFromContent(root.string(),&error),"registry scans quest");const auto* entry=registryAssets.Find(asset.header.id);Check(entry&&entry->type==engine::AssetType::Quest,"quest registry type");
    engine::ecs::Registry registry;const auto owner=registry.Create();Check(engine::GrantQuest(registry,owner,loaded,path.string()),"quest granted");Check(!engine::StartQuest(registry,owner,"WizardTrial"),"start condition enforced");Check(engine::SetQuestFlag(registry,owner,"MetWizard",true),"flag set");Check(engine::StartQuest(registry,owner,"WizardTrial"),"quest starts");Check(!engine::AdvanceQuest(registry,owner,"WizardTrial","Crystals"),"objective prerequisite enforced");Check(engine::AdvanceQuest(registry,owner,"WizardTrial","Talk"),"first objective advances");Check(engine::AdvanceQuest(registry,owner,"WizardTrial","Crystals",2),"collect progress advances");Check(engine::GetQuestProgress(registry,owner,"WizardTrial","Crystals")==2,"progress query");
    const std::string saved=engine::SerializeQuestState(registry,owner);engine::ecs::Registry restored;const auto restoredOwner=restored.Create();Check(engine::GrantQuest(restored,restoredOwner,loaded),"restore owner quest granted");Check(engine::RestoreQuestState(restored,restoredOwner,saved),"quest state restores");Check(engine::GetQuestProgress(restored,restoredOwner,"WizardTrial","Crystals")==2,"restored progress matches");Check(engine::AdvanceQuest(restored,restoredOwner,"WizardTrial","Crystals"),"final count advances");Check(engine::GetQuestState(restored,restoredOwner,"WizardTrial")==engine::QuestState::Completed,"required objectives complete quest");const auto events=engine::ConsumeQuestEvents(restored,restoredOwner);bool rewardFound=false,completeFound=false;for(const auto& e:events){rewardFound|=e.type==engine::QuestEventType::Reward&&e.name=="Fireball";completeFound|=e.type==engine::QuestEventType::Completed;}Check(rewardFound&&completeFound,"completion and reward events emitted");
    std::error_code ec;std::filesystem::remove_all(root,ec);if(failures)return 1;std::cout<<"Quest system tests passed\n";return 0;
}
