#include "engine/assets/DialogueAsset.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <unordered_set>

namespace engine { namespace {
void Error(std::string* error,const std::string& message){if(error)*error=message;}
void Dependency(std::vector<AssetHandle>& values,AssetHandle id){if(id.Valid()&&std::find(values.begin(),values.end(),id)==values.end())values.push_back(id);}
}

void NormalizeDialogueAsset(DialogueAssetData& asset){
    if(asset.name.empty())asset.name="Dialogue";
    if(asset.speakers.empty())asset.speakers.push_back({});
    if(asset.nodes.empty())asset.nodes.push_back({});
    std::unordered_set<std::string> speakerIds,nodeIds;
    for(std::size_t i=0;i<asset.speakers.size();++i){auto& s=asset.speakers[i];if(s.id.empty())s.id="Speaker"+std::to_string(i+1);const std::string base=s.id;int n=2;while(speakerIds.count(s.id))s.id=base+std::to_string(n++);speakerIds.insert(s.id);if(s.displayName.empty())s.displayName=s.id;}
    for(std::size_t i=0;i<asset.nodes.size();++i){auto& node=asset.nodes[i];if(node.id.empty())node.id="Node"+std::to_string(i+1);const std::string base=node.id;int n=2;while(nodeIds.count(node.id))node.id=base+std::to_string(n++);nodeIds.insert(node.id);if(node.speaker.empty())node.speaker=asset.speakers.front().id;}
    if(asset.entryNode.empty()||!nodeIds.count(asset.entryNode))asset.entryNode=asset.nodes.front().id;
}

bool ValidateDialogueAsset(const DialogueAssetData& asset,std::string* error){
    if(asset.name.empty()){Error(error,"Dialogue needs an internal name.");return false;}
    if(asset.speakers.empty()||asset.nodes.empty()){Error(error,"Dialogue needs at least one speaker and one node.");return false;}
    std::unordered_set<std::string> speakers,nodes;
    for(const auto& speaker:asset.speakers)if(speaker.id.empty()||!speakers.insert(speaker.id).second){Error(error,"Dialogue speaker IDs must be unique.");return false;}
    for(const auto& node:asset.nodes)if(node.id.empty()||!nodes.insert(node.id).second){Error(error,"Dialogue node IDs must be unique.");return false;}
    if(!nodes.count(asset.entryNode)){Error(error,"Dialogue entry node does not exist: "+asset.entryNode);return false;}
    for(const auto& node:asset.nodes){if(!speakers.count(node.speaker)){Error(error,"Dialogue node references a missing speaker: "+node.speaker);return false;}for(const auto& choice:node.choices)if(!choice.nextNode.empty()&&!nodes.count(choice.nextNode)){Error(error,"Dialogue choice references a missing node: "+choice.nextNode);return false;}}
    return true;
}

bool SaveDialogueAsset(const std::string& path,DialogueAssetData asset,std::string* error){
    NormalizeDialogueAsset(asset);if(!ValidateDialogueAsset(asset,error))return false;asset.header.type=AssetType::Dialogue;asset.header.assetVersion=kDialogueAssetVersion;if(!asset.header.id.Valid())asset.header.id=AssetHandle::Generate();asset.header.dependencies.clear();for(const auto& s:asset.speakers)Dependency(asset.header.dependencies,s.portraitId);for(const auto& n:asset.nodes)Dependency(asset.header.dependencies,n.voiceId);
    std::error_code ec;std::filesystem::create_directories(std::filesystem::path(path).parent_path(),ec);std::ofstream out(path);if(!out){Error(error,"Could not create dialogue asset: "+path);return false;}
    out<<"3DG_DIALOGUE "<<kDialogueAssetVersion<<' '<<asset.header.id.ToString()<<'\n'<<"ASSET_DEPS "<<asset.header.dependencies.size();for(auto id:asset.header.dependencies)out<<' '<<id.ToString();out<<'\n';
    out<<"DIALOGUE "<<std::quoted(asset.name)<<' '<<std::quoted(asset.entryNode)<<' '<<asset.skippable<<' '<<asset.pauseGameplay<<' '<<asset.duckMusic<<'\n';
    for(const auto& s:asset.speakers)out<<"SPEAKER "<<std::quoted(s.id)<<' '<<std::quoted(s.displayName)<<' '<<std::quoted(s.localizationKey)<<' '<<std::quoted(s.portraitPath)<<' '<<(s.portraitId.Valid()?s.portraitId.ToString():"-")<<'\n';
    for(const auto& n:asset.nodes){out<<"NODE "<<std::quoted(n.id)<<' '<<std::quoted(n.speaker)<<' '<<std::quoted(n.text)<<' '<<std::quoted(n.localizationKey)<<' '<<std::quoted(n.voicePath)<<' '<<(n.voiceId.Valid()?n.voiceId.ToString():"-")<<' '<<std::quoted(n.cameraHook)<<' '<<std::quoted(n.enterEvent)<<' '<<std::quoted(n.exitEvent)<<'\n';for(const auto& c:n.conditions)out<<"NODE_CONDITION "<<std::quoted(n.id)<<' '<<std::quoted(c.flag)<<' '<<c.expected<<'\n';for(const auto& choice:n.choices){out<<"CHOICE "<<std::quoted(n.id)<<' '<<std::quoted(choice.text)<<' '<<std::quoted(choice.localizationKey)<<' '<<std::quoted(choice.nextNode)<<' '<<std::quoted(choice.event)<<' '<<choice.conditions.size();for(const auto& c:choice.conditions)out<<' '<<std::quoted(c.flag)<<' '<<c.expected;out<<'\n';}}
    out<<"END\n";if(!out){Error(error,"Failed while writing dialogue asset.");return false;}return true;
}

bool LoadDialogueAsset(const std::string& path,DialogueAssetData* output,std::string* error){
    if(!output){Error(error,"Dialogue output is null.");return false;}std::ifstream in(path);if(!in){Error(error,"Could not open dialogue asset: "+path);return false;}DialogueAssetData asset;std::string magic,idText,record;std::uint32_t version=0;if(!(in>>magic>>version>>idText)||magic!="3DG_DIALOGUE"||version!=kDialogueAssetVersion||!AssetHandle::Parse(idText,&asset.header.id)){Error(error,"Invalid dialogue asset header.");return false;}
    std::size_t count=0;if(!(in>>record>>count)||record!="ASSET_DEPS"||count>4096){Error(error,"Invalid dialogue dependencies.");return false;}asset.header.dependencies.resize(count);for(auto& id:asset.header.dependencies){in>>idText;if(!AssetHandle::Parse(idText,&id)){Error(error,"Invalid dialogue dependency ID.");return false;}}
    auto node=[&](const std::string& id)->DialogueNode*{for(auto& n:asset.nodes)if(n.id==id)return &n;return nullptr;};
    while(in>>record&&record!="END"){
        if(record=="DIALOGUE")in>>std::quoted(asset.name)>>std::quoted(asset.entryNode)>>asset.skippable>>asset.pauseGameplay>>asset.duckMusic;
        else if(record=="SPEAKER"){DialogueSpeaker s;in>>std::quoted(s.id)>>std::quoted(s.displayName)>>std::quoted(s.localizationKey)>>std::quoted(s.portraitPath)>>idText;if(idText!="-"&&!AssetHandle::Parse(idText,&s.portraitId))in.setstate(std::ios::failbit);asset.speakers.push_back(std::move(s));}
        else if(record=="NODE"){DialogueNode n;in>>std::quoted(n.id)>>std::quoted(n.speaker)>>std::quoted(n.text)>>std::quoted(n.localizationKey)>>std::quoted(n.voicePath)>>idText>>std::quoted(n.cameraHook)>>std::quoted(n.enterEvent)>>std::quoted(n.exitEvent);if(idText!="-"&&!AssetHandle::Parse(idText,&n.voiceId))in.setstate(std::ios::failbit);asset.nodes.push_back(std::move(n));}
        else if(record=="NODE_CONDITION"){std::string owner;DialogueCondition c;in>>std::quoted(owner)>>std::quoted(c.flag)>>c.expected;auto* n=node(owner);if(!n){Error(error,"Dialogue condition owner is missing: "+owner);return false;}n->conditions.push_back(std::move(c));}
        else if(record=="CHOICE"){std::string owner;DialogueChoice c;in>>std::quoted(owner)>>std::quoted(c.text)>>std::quoted(c.localizationKey)>>std::quoted(c.nextNode)>>std::quoted(c.event)>>count;if(count>128){Error(error,"Dialogue choice has too many conditions.");return false;}for(std::size_t i=0;i<count;++i){DialogueCondition condition;in>>std::quoted(condition.flag)>>condition.expected;c.conditions.push_back(std::move(condition));}auto* n=node(owner);if(!n){Error(error,"Dialogue choice owner is missing: "+owner);return false;}n->choices.push_back(std::move(c));}
        else{Error(error,"Unknown dialogue record: "+record);return false;}if(!in){Error(error,"Dialogue asset is truncated or corrupt.");return false;}
    }
    asset.header.type=AssetType::Dialogue;asset.header.assetVersion=version;NormalizeDialogueAsset(asset);if(!ValidateDialogueAsset(asset,error))return false;*output=std::move(asset);return true;
}
} // namespace engine
