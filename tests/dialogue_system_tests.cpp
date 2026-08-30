#include <engine/assets/AssetRegistry.h>
#include <engine/assets/DialogueAsset.h>
#include <engine/ecs/Registry.h>
#include <engine/gameplay/DialogueSystem.h>

#include <filesystem>
#include <iostream>

namespace { int failures=0;void Check(bool value,const char* message){if(!value){std::cerr<<"FAILED: "<<message<<'\n';++failures;}} }

int main(){
    engine::DialogueAssetData asset;asset.header.id=engine::AssetHandle::Generate();asset.name="WizardGreeting";asset.entryNode="Greeting";asset.speakers={{"Wizard","Elder Wizard","speaker.wizard",{},{}},{"Player","Player","speaker.player",{},{}}};
    engine::DialogueNode greeting;greeting.id="Greeting";greeting.speaker="Wizard";greeting.text="Are you ready?";greeting.localizationKey="dialogue.wizard.ready";greeting.enterEvent="GreetingStarted";greeting.cameraHook="CloseUpWizard";greeting.choices={{"Yes","dialogue.yes","Accepted","AcceptedQuest",{}},{"Not yet","dialogue.not_yet","Declined",{},{{"AllowDecline",true}}}};
    engine::DialogueNode accepted;accepted.id="Accepted";accepted.speaker="Player";accepted.text="I am ready.";accepted.choices={{"Continue",{},"",{}, {}}};
    engine::DialogueNode declined;declined.id="Declined";declined.speaker="Wizard";declined.text="Return when ready.";
    asset.nodes={greeting,accepted,declined};
    std::string error;Check(engine::ValidateDialogueAsset(asset,&error),"dialogue validates");const auto root=std::filesystem::temp_directory_path()/"3dg_dialogue_test";const auto path=root/"WizardGreeting.3dgdialogue";Check(engine::SaveDialogueAsset(path.string(),asset,&error),"dialogue saves");engine::DialogueAssetData loaded;Check(engine::LoadDialogueAsset(path.string(),&loaded,&error),"dialogue loads");Check(loaded.nodes.size()==3&&loaded.speakers.size()==2,"dialogue graph round trips");
    engine::AssetRegistry registry;Check(registry.RebuildFromContent(root.string(),&error),"registry scans dialogue");const auto* record=registry.Find(asset.header.id);Check(record&&record->type==engine::AssetType::Dialogue,"registry identifies dialogue");
    engine::ecs::Registry world;const auto source=world.Create();const auto listener=world.Create();Check(engine::ConfigureDialogueSource(world,source,path.string(),&error),"source configures");Check(engine::StartDialogue(world,listener,source,&error),"conversation starts from source");const auto* current=engine::CurrentDialogueNode(world,listener);Check(current&&current->id=="Greeting","entry node active");auto choices=engine::AvailableDialogueChoices(world,listener);Check(choices.size()==1&&choices[0]==0,"conditional choice hidden");Check(engine::SetDialogueFlag(world,listener,"AllowDecline",true),"dialogue flag sets");Check(engine::AvailableDialogueChoices(world,listener).size()==2,"conditional choice becomes available");
    Check(engine::ChooseDialogueOption(world,listener,0),"choice advances branch");Check(engine::CurrentDialogueNode(world,listener)&&engine::CurrentDialogueNode(world,listener)->id=="Accepted","branch target entered");const std::string state=engine::SerializeDialogueState(world,listener);Check(!state.empty(),"dialogue state serializes");Check(engine::ContinueDialogue(world,listener),"single choice continues");Check(!engine::IsDialogueActive(world,listener),"conversation ends");Check(engine::RestoreDialogueState(world,listener,state),"dialogue state restores");Check(engine::IsDialogueActive(world,listener)&&engine::CurrentDialogueNode(world,listener)->id=="Accepted","active node restored");
    const auto events=engine::ConsumeDialogueEvents(world,listener);Check(events.empty(),"restore clears transient events");Check(engine::CancelDialogue(world,listener),"conversation cancels");
    std::filesystem::remove_all(root);if(failures){std::cerr<<failures<<" dialogue test(s) failed\n";return 1;}std::cout<<"Dialogue system regression tests passed\n";return 0;
}
