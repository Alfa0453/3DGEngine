#include "engine/assets/PortalAsset.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>

namespace engine {
namespace {
void Error(std::string* error,const std::string& value){if(error)*error=value;}
void AddDependency(std::vector<AssetHandle>& values,AssetHandle id){if(id.Valid()&&std::find(values.begin(),values.end(),id)==values.end())values.push_back(id);}
}
const char* PortalModeName(PortalMode mode){switch(mode){case PortalMode::SameLevel:return "Same-Level Teleport";case PortalMode::LevelTransition:return "Level Transition";case PortalMode::SeamlessDoor:return "Seamless Door";}return "Same-Level Teleport";}
void NormalizePortalAsset(PortalAssetData& asset){
    if(asset.name.empty())asset.name="Portal";asset.mode=static_cast<PortalMode>(std::clamp(static_cast<int>(asset.mode),0,2));
    asset.arrivalOffset=glm::clamp(asset.arrivalOffset,glm::vec3(-10000.f),glm::vec3(10000.f));
    asset.arrivalRotationDegrees=glm::clamp(asset.arrivalRotationDegrees,glm::vec3(-360.f),glm::vec3(360.f));
    asset.activationRadius=std::clamp(asset.activationRadius,.05f,10000.f);asset.cooldown=std::clamp(asset.cooldown,0.f,3600.f);
    if(asset.prompt.empty())asset.prompt="Enter";
}
bool ValidatePortalAsset(const PortalAssetData& asset,std::string* error){if(asset.name.empty()){Error(error,"Portal asset needs a name.");return false;}if(asset.mode==PortalMode::SameLevel&&asset.destinationObject.empty()){Error(error,"Same-level portals need a destination object.");return false;}if(asset.mode!=PortalMode::SameLevel&&asset.destinationLevel.empty()){Error(error,"Level-transition portals need a destination level.");return false;}return true;}
bool SavePortalAsset(const std::string& path,PortalAssetData asset,std::string* error){
    NormalizePortalAsset(asset);if(!ValidatePortalAsset(asset,error))return false;asset.header.type=AssetType::Portal;asset.header.assetVersion=kPortalAssetVersion;if(!asset.header.id.Valid())asset.header.id=AssetHandle::Generate();asset.header.dependencies.clear();AddDependency(asset.header.dependencies,asset.destinationLevelId);AddDependency(asset.header.dependencies,asset.enterAudioId);AddDependency(asset.header.dependencies,asset.exitAudioId);AddDependency(asset.header.dependencies,asset.transitionEffectId);
    std::error_code ec;std::filesystem::create_directories(std::filesystem::path(path).parent_path(),ec);std::ofstream out(path);if(!out){Error(error,"Could not create portal asset: "+path);return false;}
    out<<"3DG_PORTAL "<<kPortalAssetVersion<<' '<<asset.header.id.ToString()<<'\n'<<"ASSET_DEPS "<<asset.header.dependencies.size();for(auto id:asset.header.dependencies)out<<' '<<id.ToString();out<<'\n';
    out<<std::quoted(asset.name)<<' '<<static_cast<int>(asset.mode)<<' '<<std::quoted(asset.destinationObject)<<' '<<std::quoted(asset.destinationLevel)<<' '<<(asset.destinationLevelId.Valid()?asset.destinationLevelId.ToString():std::string("-"))<<'\n';
    out<<asset.arrivalOffset.x<<' '<<asset.arrivalOffset.y<<' '<<asset.arrivalOffset.z<<' '<<asset.arrivalRotationDegrees.x<<' '<<asset.arrivalRotationDegrees.y<<' '<<asset.arrivalRotationDegrees.z<<' '<<asset.activationRadius<<' '<<asset.cooldown<<'\n';
    out<<asset.automatic<<' '<<asset.preserveVelocity<<' '<<asset.alignFacing<<' '<<asset.oneWay<<' '<<asset.safeArrival<<' '<<std::quoted(asset.requiredAccessTag)<<' '<<std::quoted(asset.prompt)<<'\n';
    out<<std::quoted(asset.enterAudioPath)<<' '<<std::quoted(asset.exitAudioPath)<<' '<<std::quoted(asset.transitionEffectPath)<<' ';
    auto writeId=[&](AssetHandle id){out<<(id.Valid()?id.ToString():std::string("-"))<<' ';};writeId(asset.enterAudioId);writeId(asset.exitAudioId);writeId(asset.transitionEffectId);out<<'\n';if(!out){Error(error,"Failed while writing portal asset.");return false;}return true;
}
bool LoadPortalAsset(const std::string& path,PortalAssetData* output,std::string* error){
    if(!output){Error(error,"Portal output is null.");return false;}std::ifstream in(path);if(!in){Error(error,"Could not open portal asset: "+path);return false;}PortalAssetData asset;std::string magic,text;std::uint32_t version=0;if(!(in>>magic>>version>>text)||magic!="3DG_PORTAL"||version!=kPortalAssetVersion||!AssetHandle::Parse(text,&asset.header.id)){Error(error,"Invalid portal asset header.");return false;}std::string deps;std::size_t count=0;if(!(in>>deps>>count)||deps!="ASSET_DEPS"||count>64){Error(error,"Portal dependency metadata is invalid.");return false;}asset.header.dependencies.resize(count);for(auto&id:asset.header.dependencies){in>>text;if(!AssetHandle::Parse(text,&id)){Error(error,"Invalid portal dependency ID.");return false;}}
    int mode=0;in>>std::quoted(asset.name)>>mode>>std::quoted(asset.destinationObject)>>std::quoted(asset.destinationLevel)>>text;asset.mode=static_cast<PortalMode>(mode);if(text!="-"&&!AssetHandle::Parse(text,&asset.destinationLevelId))in.setstate(std::ios::failbit);
    in>>asset.arrivalOffset.x>>asset.arrivalOffset.y>>asset.arrivalOffset.z>>asset.arrivalRotationDegrees.x>>asset.arrivalRotationDegrees.y>>asset.arrivalRotationDegrees.z>>asset.activationRadius>>asset.cooldown;
    in>>asset.automatic>>asset.preserveVelocity>>asset.alignFacing>>asset.oneWay>>asset.safeArrival>>std::quoted(asset.requiredAccessTag)>>std::quoted(asset.prompt)>>std::quoted(asset.enterAudioPath)>>std::quoted(asset.exitAudioPath)>>std::quoted(asset.transitionEffectPath);
    auto readId=[&](AssetHandle&id){in>>text;if(text!="-"&&!AssetHandle::Parse(text,&id))in.setstate(std::ios::failbit);};readId(asset.enterAudioId);readId(asset.exitAudioId);readId(asset.transitionEffectId);if(!in){Error(error,"Portal asset is truncated or corrupt.");return false;}asset.header.type=AssetType::Portal;asset.header.assetVersion=version;NormalizePortalAsset(asset);if(!ValidatePortalAsset(asset,error))return false;*output=std::move(asset);return true;
}
} // namespace engine
