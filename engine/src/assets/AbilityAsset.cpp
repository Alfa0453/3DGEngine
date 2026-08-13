#include "engine/assets/AbilityAsset.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>

namespace engine {
namespace { void Error(std::string* e,const std::string& m){if(e)*e=m;} }

bool SaveAbilityAsset(const std::string& path, AbilityAssetData& asset,
                      std::string* error) {
    if (!asset.assetId.Valid()) asset.assetId=AssetHandle::Generate();
    if (asset.phases.empty()) { Error(error,"An ability needs at least one phase."); return false; }
    std::error_code ec; const std::filesystem::path p(path);
    if(p.has_parent_path())std::filesystem::create_directories(p.parent_path(),ec);
    std::ofstream out(path,std::ios::trunc);
    if(!out){Error(error,"Could not write ability: "+path);return false;}
    out<<"3DG_ABILITY 1 "<<asset.assetId.ToString()<<'\n'
       <<std::quoted(asset.name)<<' '<<std::quoted(asset.description)<<'\n'
       <<std::max(asset.cooldown,0.f)<<' '<<std::max(asset.maxCharges,1)<<' '
       <<std::max(asset.chargeRecovery,0.f)<<' '<<std::max(asset.manaCost,0.f)<<' '
       <<std::max(asset.staminaCost,0.f)<<' '<<std::max(asset.healthCost,0.f)<<' '
       <<std::max(asset.activationRange,0.f)<<' '<<(asset.requireTarget?1:0)<<' '
       <<(asset.cancelOnDamage?1:0)<<'\n'<<"PHASES "<<asset.phases.size()<<'\n';
    std::vector<AssetHandle> deps;
    for(const AbilityPhase& phase:asset.phases){
        out<<std::quoted(phase.name)<<' '<<std::max(phase.duration,0.f)<<' '
           <<(phase.interruptible?1:0)<<' '<<phase.effects.size()<<'\n';
        for(const AbilityEffect& effect:phase.effects){
            out<<static_cast<int>(effect.type)<<' '<<static_cast<int>(effect.target)<<' '
               <<std::max(effect.time,0.f)<<' '<<effect.magnitude<<' '
               <<std::max(effect.radius,0.f)<<' '<<std::max(effect.speed,0.f)<<' '
               <<std::max(effect.range,0.f)<<' '<<effect.direction.x<<' '
               <<effect.direction.y<<' '<<effect.direction.z<<' '
               <<std::quoted(effect.name)<<' '<<std::quoted(effect.assetPath)<<' '
               <<(effect.assetId.Valid()?effect.assetId.ToString():"-")<<'\n';
            if(effect.assetId.Valid()&&std::find(deps.begin(),deps.end(),effect.assetId)==deps.end())
                deps.push_back(effect.assetId);
        }
    }
    out<<"ASSET_DEPS "<<deps.size(); for(AssetHandle id:deps)out<<' '<<id.ToString(); out<<'\n';
    Error(error,{}); return static_cast<bool>(out);
}

bool LoadAbilityAsset(const std::string& path, AbilityAssetData* asset,
                      std::string* error) {
    if(!asset){Error(error,"Ability output is null.");return false;}
    std::ifstream in(path); std::string magic,id,tag; int version=0;
    AbilityAssetData a;
    if(!(in>>magic>>version>>id)||magic!="3DG_ABILITY"||version!=1
       ||!AssetHandle::Parse(id,&a.assetId)){Error(error,"Invalid ability asset: "+path);return false;}
    int require=0,cancel=0; in>>std::quoted(a.name)>>std::quoted(a.description)
      >>a.cooldown>>a.maxCharges>>a.chargeRecovery>>a.manaCost>>a.staminaCost
      >>a.healthCost>>a.activationRange>>require>>cancel>>tag;
    std::size_t phases=0;in>>phases;
    if(!in||tag!="PHASES"||phases==0||phases>128){Error(error,"Ability phases are invalid.");return false;}
    a.requireTarget=require!=0;a.cancelOnDamage=cancel!=0;a.phases.resize(phases);
    for(AbilityPhase& phase:a.phases){int interrupt=1;std::size_t effects=0;
        in>>std::quoted(phase.name)>>phase.duration>>interrupt>>effects;
        if(effects>1024){Error(error,"Ability has too many effects.");return false;}
        phase.interruptible=interrupt!=0;phase.effects.resize(effects);
        for(AbilityEffect& effect:phase.effects){int type=0,target=0;std::string eid;
            in>>type>>target>>effect.time>>effect.magnitude>>effect.radius>>effect.speed
              >>effect.range>>effect.direction.x>>effect.direction.y>>effect.direction.z
              >>std::quoted(effect.name)>>std::quoted(effect.assetPath)>>eid;
            if(type<0||type>static_cast<int>(AbilityEffectType::ScriptEvent)
               ||target<0||target>static_cast<int>(AbilityTargetMode::Radius)
               ||(eid!="-"&&!AssetHandle::Parse(eid,&effect.assetId))){
                Error(error,"Ability effect data is invalid.");return false;}
            effect.type=static_cast<AbilityEffectType>(type);
            effect.target=static_cast<AbilityTargetMode>(target);
            effect.time=std::max(effect.time,0.f); effect.radius=std::max(effect.radius,0.f);
        }
    }
    if(!in){Error(error,"Ability asset is incomplete.");return false;}
    a.cooldown=std::max(a.cooldown,0.f);a.maxCharges=std::max(a.maxCharges,1);
    a.chargeRecovery=std::max(a.chargeRecovery,0.f);*asset=std::move(a);Error(error,{});return true;
}
} // namespace engine
