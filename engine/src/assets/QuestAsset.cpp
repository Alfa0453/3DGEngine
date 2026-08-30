#include "engine/assets/QuestAsset.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <unordered_set>

namespace engine {
namespace {
void Error(std::string* error, const std::string& message) { if (error) *error = message; }
void Dependency(std::vector<AssetHandle>& values, AssetHandle id) {
    if (id.Valid() && std::find(values.begin(), values.end(), id) == values.end()) values.push_back(id);
}
}

const char* QuestObjectiveTypeName(QuestObjectiveType type) {
    switch (type) { case QuestObjectiveType::Custom:return "Custom"; case QuestObjectiveType::Reach:return "Reach Location";
    case QuestObjectiveType::Collect:return "Collect"; case QuestObjectiveType::Defeat:return "Defeat";
    case QuestObjectiveType::Interact:return "Interact"; } return "Custom";
}
const char* QuestRewardTypeName(QuestRewardType type) {
    switch (type) { case QuestRewardType::Score:return "Score"; case QuestRewardType::Item:return "Item";
    case QuestRewardType::Ability:return "Ability"; case QuestRewardType::ScriptEvent:return "Script Event"; } return "Score";
}

void NormalizeQuestAsset(QuestAssetData& asset) {
    if (asset.name.empty()) asset.name = "Quest";
    if (asset.title.empty()) asset.title = asset.name;
    std::unordered_set<std::string> used;
    for (std::size_t i=0;i<asset.objectives.size();++i) {
        auto& objective=asset.objectives[i];
        if(objective.id.empty()) objective.id="Objective"+std::to_string(i+1);
        std::string base=objective.id; int suffix=2;
        while(used.count(objective.id)) objective.id=base+std::to_string(suffix++);
        used.insert(objective.id); objective.requiredCount=std::clamp(objective.requiredCount,1,100000000);
        if(objective.description.empty()) objective.description=objective.id;
    }
    for(auto& reward:asset.rewards) reward.amount=std::clamp(reward.amount,1,100000000);
}

bool ValidateQuestAsset(const QuestAssetData& asset,std::string* error) {
    if(asset.name.empty()){Error(error,"Quest needs an internal name.");return false;}
    if(asset.objectives.empty()){Error(error,"Quest needs at least one objective.");return false;}
    std::unordered_set<std::string> ids;
    for(const auto& objective:asset.objectives){if(objective.id.empty()||!ids.insert(objective.id).second){Error(error,"Quest objective IDs must be unique.");return false;}}
    for(const auto& objective:asset.objectives) if(!objective.requiredObjective.empty()&&!ids.count(objective.requiredObjective)){Error(error,"Objective prerequisite does not exist: "+objective.requiredObjective);return false;}
    return true;
}

bool SaveQuestAsset(const std::string& path,QuestAssetData asset,std::string* error) {
    NormalizeQuestAsset(asset); if(!ValidateQuestAsset(asset,error))return false;
    asset.header.type=AssetType::Quest;asset.header.assetVersion=kQuestAssetVersion;
    if(!asset.header.id.Valid())asset.header.id=AssetHandle::Generate();asset.header.dependencies.clear();
    for(const auto& reward:asset.rewards)Dependency(asset.header.dependencies,reward.assetId);
    std::error_code ec;std::filesystem::create_directories(std::filesystem::path(path).parent_path(),ec);
    std::ofstream out(path);if(!out){Error(error,"Could not create quest asset: "+path);return false;}
    out<<"3DG_QUEST "<<kQuestAssetVersion<<' '<<asset.header.id.ToString()<<'\n'<<"ASSET_DEPS "<<asset.header.dependencies.size();for(auto id:asset.header.dependencies)out<<' '<<id.ToString();out<<'\n';
    out<<"QUEST "<<std::quoted(asset.name)<<' '<<std::quoted(asset.title)<<' '<<std::quoted(asset.description)<<' '<<asset.autoStart<<' '<<asset.repeatable<<' '<<asset.hiddenUntilStarted<<'\n';
    for(const auto& condition:asset.startConditions)out<<"CONDITION "<<std::quoted(condition.flag)<<' '<<condition.expected<<'\n';
    for(const auto& objective:asset.objectives)out<<"OBJECTIVE "<<std::quoted(objective.id)<<' '<<std::quoted(objective.description)<<' '<<static_cast<int>(objective.type)<<' '<<std::quoted(objective.target)<<' '<<objective.requiredCount<<' '<<objective.optional<<' '<<objective.checkpoint<<' '<<std::quoted(objective.requiredObjective)<<' '<<std::quoted(objective.requiredFlag)<<' '<<std::quoted(objective.dialogueTrigger)<<'\n';
    for(const auto& reward:asset.rewards)out<<"REWARD "<<static_cast<int>(reward.type)<<' '<<std::quoted(reward.name)<<' '<<reward.amount<<' '<<std::quoted(reward.assetPath)<<' '<<(reward.assetId.Valid()?reward.assetId.ToString():std::string("-"))<<'\n';
    out<<"END\n";if(!out){Error(error,"Failed while writing quest asset.");return false;}return true;
}

bool LoadQuestAsset(const std::string& path,QuestAssetData* output,std::string* error) {
    if(!output){Error(error,"Quest output is null.");return false;}std::ifstream in(path);if(!in){Error(error,"Could not open quest asset: "+path);return false;}
    QuestAssetData asset;std::string magic,idText,record;std::uint32_t version=0;
    if(!(in>>magic>>version>>idText)||magic!="3DG_QUEST"||version!=kQuestAssetVersion||!AssetHandle::Parse(idText,&asset.header.id)){Error(error,"Invalid quest asset header.");return false;}
    std::size_t dependencyCount=0;if(!(in>>record>>dependencyCount)||record!="ASSET_DEPS"||dependencyCount>1024){Error(error,"Invalid quest dependencies.");return false;}asset.header.dependencies.resize(dependencyCount);for(auto& id:asset.header.dependencies){in>>idText;if(!AssetHandle::Parse(idText,&id)){Error(error,"Invalid quest dependency ID.");return false;}}
    while(in>>record&&record!="END"){
        if(record=="QUEST")in>>std::quoted(asset.name)>>std::quoted(asset.title)>>std::quoted(asset.description)>>asset.autoStart>>asset.repeatable>>asset.hiddenUntilStarted;
        else if(record=="CONDITION"){QuestCondition value;in>>std::quoted(value.flag)>>value.expected;asset.startConditions.push_back(std::move(value));}
        else if(record=="OBJECTIVE"){QuestObjective value;int type=0;in>>std::quoted(value.id)>>std::quoted(value.description)>>type>>std::quoted(value.target)>>value.requiredCount>>value.optional>>value.checkpoint>>std::quoted(value.requiredObjective)>>std::quoted(value.requiredFlag)>>std::quoted(value.dialogueTrigger);value.type=static_cast<QuestObjectiveType>(std::clamp(type,0,4));asset.objectives.push_back(std::move(value));}
        else if(record=="REWARD"){QuestReward value;int type=0;in>>type>>std::quoted(value.name)>>value.amount>>std::quoted(value.assetPath)>>idText;value.type=static_cast<QuestRewardType>(std::clamp(type,0,3));if(idText!="-"&&!AssetHandle::Parse(idText,&value.assetId))in.setstate(std::ios::failbit);asset.rewards.push_back(std::move(value));}
        else {Error(error,"Unknown quest record: "+record);return false;}
        if(!in){Error(error,"Quest asset is truncated or corrupt.");return false;}
    }
    asset.header.type=AssetType::Quest;asset.header.assetVersion=version;NormalizeQuestAsset(asset);if(!ValidateQuestAsset(asset,error))return false;*output=std::move(asset);return true;
}
} // namespace engine
