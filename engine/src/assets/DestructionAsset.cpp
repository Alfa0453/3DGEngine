#include "engine/assets/DestructionAsset.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <random>

namespace engine {
namespace {
void Error(std::string* error, const std::string& value) { if (error) *error=value; }
void AddDependency(std::vector<AssetHandle>& dependencies, AssetHandle id) {
    if (!id.Valid()) return;
    if (std::find(dependencies.begin(),dependencies.end(),id)==dependencies.end())
        dependencies.push_back(id);
}
}

void NormalizeDestructionAsset(DestructionAssetData& asset) {
    asset.name = asset.name.empty() ? "Destructible" : asset.name;
    asset.bounds = glm::clamp(glm::abs(asset.bounds),glm::vec3(.02f),glm::vec3(10000.f));
    asset.chunksX=std::clamp(asset.chunksX,1,32);
    asset.chunksY=std::clamp(asset.chunksY,1,32);
    asset.chunksZ=std::clamp(asset.chunksZ,1,32);
    while (asset.chunksX*asset.chunksY*asset.chunksZ>2048) {
        if(asset.chunksX>=asset.chunksY&&asset.chunksX>=asset.chunksZ)--asset.chunksX;
        else if(asset.chunksY>=asset.chunksZ)--asset.chunksY; else --asset.chunksZ;
    }
    asset.maxHealth=std::clamp(asset.maxHealth,.001f,1000000.f);
    asset.minimumDamage=std::clamp(asset.minimumDamage,0.f,asset.maxHealth);
    asset.impactThreshold=std::clamp(asset.impactThreshold,0.f,1000000.f);
    asset.debrisMass=std::clamp(asset.debrisMass,.001f,100000.f);
    asset.impulseScale=std::clamp(asset.impulseScale,0.f,1000.f);
    asset.scatterImpulse=std::clamp(asset.scatterImpulse,0.f,1000.f);
    asset.angularImpulse=std::clamp(asset.angularImpulse,0.f,1000.f);
    asset.debrisLifetime=std::clamp(asset.debrisLifetime,0.f,3600.f);
    asset.gap=std::clamp(asset.gap,0.f,.45f);
    for(auto& state:asset.states) {
        if(state.name.empty())state.name="Damaged";
        state.healthFraction=std::clamp(state.healthFraction,.001f,.999f);
    }
    std::stable_sort(asset.states.begin(),asset.states.end(),
        [](const auto&a,const auto&b){return a.healthFraction>b.healthFraction;});
}

bool ValidateDestructionAsset(const DestructionAssetData& asset,std::string* error) {
    if(asset.name.empty()){Error(error,"Destruction asset needs a name.");return false;}
    if(asset.chunksX*asset.chunksY*asset.chunksZ>2048){Error(error,"Destruction asset exceeds 2048 debris chunks.");return false;}
    if(asset.maxHealth<=0.f){Error(error,"Maximum health must be greater than zero.");return false;}
    return true;
}

std::vector<DestructionChunk> GenerateDestructionChunks(
    const DestructionAssetData& source,const glm::vec3& impactPoint) {
    DestructionAssetData asset=source;NormalizeDestructionAsset(asset);
    std::vector<DestructionChunk> result;
    result.reserve(static_cast<std::size_t>(asset.chunksX*asset.chunksY*asset.chunksZ));
    std::mt19937 random(asset.seed);std::uniform_real_distribution<float> unit(-1.f,1.f);
    const glm::vec3 cell=asset.bounds/glm::vec3(asset.chunksX,asset.chunksY,asset.chunksZ);
    const glm::vec3 start=-asset.bounds*.5f+cell*.5f;
    std::uint32_t index=0;
    for(int z=0;z<asset.chunksZ;++z)for(int y=0;y<asset.chunksY;++y)for(int x=0;x<asset.chunksX;++x){
        DestructionChunk chunk;chunk.index=index++;
        chunk.localCenter=start+cell*glm::vec3(x,y,z);
        chunk.size=cell*(1.f-asset.gap);
        glm::vec3 radial=chunk.localCenter-impactPoint;
        if(glm::dot(radial,radial)<.000001f)radial={unit(random),std::abs(unit(random))+.25f,unit(random)};
        radial=glm::normalize(radial);
        chunk.impulseDirection=glm::normalize(radial+glm::vec3(unit(random)*.2f,std::abs(unit(random))*.25f,unit(random)*.2f));
        chunk.angularVelocity=glm::vec3(unit(random),unit(random),unit(random))*asset.angularImpulse;
        result.push_back(chunk);
    }
    return result;
}

int DestructionStateForHealth(const DestructionAssetData& source,float health) {
    DestructionAssetData asset=source;NormalizeDestructionAsset(asset);
    if(health<=0.f)return static_cast<int>(asset.states.size());
    const float fraction=std::clamp(health/asset.maxHealth,0.f,1.f);
    int result=-1;
    for(std::size_t i=0;i<asset.states.size();++i)
        if(fraction<=asset.states[i].healthFraction)result=static_cast<int>(i);
    return result;
}

bool SaveDestructionAsset(const std::string& path,DestructionAssetData asset,std::string* error) {
    NormalizeDestructionAsset(asset);if(!ValidateDestructionAsset(asset,error))return false;
    asset.header.type=AssetType::Destruction;asset.header.assetVersion=kDestructionAssetVersion;
    if(!asset.header.id.Valid())asset.header.id=AssetHandle::Generate();
    asset.header.dependencies.clear();
    AddDependency(asset.header.dependencies,asset.sourceMeshId);AddDependency(asset.header.dependencies,asset.sourceMaterialId);
    AddDependency(asset.header.dependencies,asset.debrisMeshId);AddDependency(asset.header.dependencies,asset.debrisMaterialId);
    AddDependency(asset.header.dependencies,asset.breakParticleId);AddDependency(asset.header.dependencies,asset.breakAudioId);
    for(const auto&s:asset.states){AddDependency(asset.header.dependencies,s.meshId);AddDependency(asset.header.dependencies,s.materialId);AddDependency(asset.header.dependencies,s.particleId);AddDependency(asset.header.dependencies,s.audioId);}
    std::error_code ec;std::filesystem::create_directories(std::filesystem::path(path).parent_path(),ec);
    std::ofstream out(path);if(!out){Error(error,"Could not create destruction asset: "+path);return false;}
    out<<"3DG_DESTRUCTION "<<kDestructionAssetVersion<<' '<<asset.header.id.ToString()<<'\n';
    out<<"ASSET_DEPS "<<asset.header.dependencies.size();for(auto id:asset.header.dependencies)out<<' '<<id.ToString();out<<'\n';
    out<<std::quoted(asset.name)<<'\n'<<std::quoted(asset.sourceMeshPath)<<' '<<std::quoted(asset.sourceMaterialPath)<<'\n'
       <<std::quoted(asset.debrisMeshPath)<<' '<<std::quoted(asset.debrisMaterialPath)<<'\n'
       <<std::quoted(asset.breakParticlePath)<<' '<<std::quoted(asset.breakAudioPath)<<'\n';
    auto id=[&](AssetHandle v){out<<(v.Valid()?v.ToString():std::string("-"))<<' ';};
    id(asset.sourceMeshId);id(asset.sourceMaterialId);id(asset.debrisMeshId);id(asset.debrisMaterialId);id(asset.breakParticleId);id(asset.breakAudioId);out<<'\n';
    out<<asset.bounds.x<<' '<<asset.bounds.y<<' '<<asset.bounds.z<<' '<<asset.chunksX<<' '<<asset.chunksY<<' '<<asset.chunksZ<<' '<<asset.seed<<'\n';
    out<<asset.maxHealth<<' '<<asset.minimumDamage<<' '<<asset.impactThreshold<<' '<<asset.debrisMass<<' '<<asset.impulseScale<<' '<<asset.scatterImpulse<<' '<<asset.angularImpulse<<' '<<asset.debrisLifetime<<' '<<asset.gap<<' '<<asset.debrisCollision<<' '<<asset.removeSourceOnBreak<<'\n';
    out<<asset.states.size()<<'\n';for(const auto&s:asset.states){out<<std::quoted(s.name)<<' '<<s.healthFraction<<' '<<std::quoted(s.meshPath)<<' '<<std::quoted(s.materialPath)<<' '<<std::quoted(s.particlePath)<<' '<<std::quoted(s.audioPath)<<' ';id(s.meshId);id(s.materialId);id(s.particleId);id(s.audioId);out<<'\n';}
    if(!out){Error(error,"Failed while writing destruction asset.");return false;}return true;
}

bool LoadDestructionAsset(const std::string& path,DestructionAssetData* output,std::string* error) {
    if(!output){Error(error,"Destruction output is null.");return false;}std::ifstream in(path);if(!in){Error(error,"Could not open destruction asset: "+path);return false;}
    DestructionAssetData asset;std::string magic,idText;std::uint32_t version=0;
    if(!(in>>magic>>version>>idText)||magic!="3DG_DESTRUCTION"||version!=kDestructionAssetVersion||!AssetHandle::Parse(idText,&asset.header.id)){Error(error,"Invalid destruction asset header.");return false;}
    std::string deps;if(!(in>>deps)||deps!="ASSET_DEPS"){Error(error,"Destruction dependency metadata is missing.");return false;}std::size_t count=0;in>>count;if(count>4096){Error(error,"Destruction dependency count is invalid.");return false;}asset.header.dependencies.resize(count);for(auto&d:asset.header.dependencies){in>>idText;if(!AssetHandle::Parse(idText,&d)){Error(error,"Invalid destruction dependency ID.");return false;}}
    in>>std::quoted(asset.name)>>std::quoted(asset.sourceMeshPath)>>std::quoted(asset.sourceMaterialPath)>>std::quoted(asset.debrisMeshPath)>>std::quoted(asset.debrisMaterialPath)>>std::quoted(asset.breakParticlePath)>>std::quoted(asset.breakAudioPath);
    auto readId=[&](AssetHandle& id){in>>idText;if(idText!="-"&&!AssetHandle::Parse(idText,&id))in.setstate(std::ios::failbit);};
    readId(asset.sourceMeshId);readId(asset.sourceMaterialId);readId(asset.debrisMeshId);readId(asset.debrisMaterialId);readId(asset.breakParticleId);readId(asset.breakAudioId);
    in>>asset.bounds.x>>asset.bounds.y>>asset.bounds.z>>asset.chunksX>>asset.chunksY>>asset.chunksZ>>asset.seed;
    in>>asset.maxHealth>>asset.minimumDamage>>asset.impactThreshold>>asset.debrisMass>>asset.impulseScale>>asset.scatterImpulse>>asset.angularImpulse>>asset.debrisLifetime>>asset.gap>>asset.debrisCollision>>asset.removeSourceOnBreak;
    in>>count;if(count>128){Error(error,"Destruction state count is invalid.");return false;}asset.states.resize(count);for(auto&s:asset.states){in>>std::quoted(s.name)>>s.healthFraction>>std::quoted(s.meshPath)>>std::quoted(s.materialPath)>>std::quoted(s.particlePath)>>std::quoted(s.audioPath);readId(s.meshId);readId(s.materialId);readId(s.particleId);readId(s.audioId);}
    if(!in){Error(error,"Destruction asset is truncated or corrupt.");return false;}asset.header.type=AssetType::Destruction;asset.header.assetVersion=version;NormalizeDestructionAsset(asset);if(!ValidateDestructionAsset(asset,error))return false;*output=std::move(asset);return true;
}
} // namespace engine
