#include "engine/assets/WeatherAsset.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <vector>
#include <glm/common.hpp>

namespace engine {
namespace {
void Error(std::string* error,const std::string& message){if(error)*error=message;}
float Lerp(float a,float b,float t){return a+(b-a)*t;}
glm::vec3 Lerp(glm::vec3 a,glm::vec3 b,float t){return a+(b-a)*t;}
}

void NormalizeWeather(WeatherAssetData& w){
    w.timeOfDay=std::clamp(w.timeOfDay,0.f,1.f);w.skyLightIntensity=std::clamp(w.skyLightIntensity,0.f,8.f);w.sunIntensity=std::clamp(w.sunIntensity,0.f,8.f);
    w.cloudCoverage=std::clamp(w.cloudCoverage,.05f,.95f);w.cloudDensity=std::clamp(w.cloudDensity,0.f,2.f);w.cloudScale=std::clamp(w.cloudScale,.1f,6.f);w.cloudSoftness=std::clamp(w.cloudSoftness,.01f,.45f);w.cloudWindSpeed=std::clamp(w.cloudWindSpeed,-.25f,.25f);w.cloudWindDirection=std::clamp(w.cloudWindDirection,-180.f,180.f);w.cloudColor=glm::clamp(w.cloudColor,glm::vec3(0),glm::vec3(1));w.cloudShadowStrength=std::clamp(w.cloudShadowStrength,0.f,1.f);
    w.fogColor=glm::clamp(w.fogColor,glm::vec3(0),glm::vec3(1));w.fogDensity=std::clamp(w.fogDensity,0.f,.2f);w.fogHeight=std::clamp(w.fogHeight,-20.f,20.f);w.fogHeightFalloff=std::clamp(w.fogHeightFalloff,.001f,2.f);
    w.precipitationIntensity=std::clamp(w.precipitationIntensity,0.f,1.f);w.precipitationSize=std::clamp(w.precipitationSize,.02f,3.f);w.precipitationSpeed=std::clamp(w.precipitationSpeed,.1f,100.f);w.windStrength=std::clamp(w.windStrength,0.f,1.f);w.windDirection=std::clamp(w.windDirection,-180.f,180.f);w.lightningFrequency=std::clamp(w.lightningFrequency,0.f,2.f);w.lightningIntensity=std::clamp(w.lightningIntensity,0.f,10.f);w.surfaceWetness=std::clamp(w.surfaceWetness,0.f,1.f);w.puddleAmount=std::clamp(w.puddleAmount,0.f,1.f);w.defaultTransitionSeconds=std::clamp(w.defaultTransitionSeconds,0.f,120.f);
}

WeatherAssetData BlendWeather(const WeatherAssetData& a,const WeatherAssetData& b,float alpha){
    const float t=std::clamp(alpha,0.f,1.f),s=t*t*(3.f-2.f*t);WeatherAssetData r=b;
#define L(field) r.field=Lerp(a.field,b.field,s)
    L(timeOfDay);L(skyLightIntensity);L(sunIntensity);L(cloudCoverage);L(cloudDensity);L(cloudScale);L(cloudSoftness);L(cloudWindSpeed);L(cloudWindDirection);L(cloudColor);L(cloudShadowStrength);L(fogColor);L(fogDensity);L(fogHeight);L(fogHeightFalloff);L(precipitationIntensity);L(precipitationSize);L(precipitationSpeed);L(windStrength);L(windDirection);L(lightningFrequency);L(lightningIntensity);L(surfaceWetness);L(puddleAmount);
#undef L
    r.clouds=t<.5f?a.clouds:b.clouds;r.cloudShadows=t<.5f?a.cloudShadows:b.cloudShadows;r.fog=t<.5f?a.fog:b.fog;r.precipitation=t<.5f?a.precipitation:b.precipitation;r.lightning=t<.5f?a.lightning:b.lightning;NormalizeWeather(r);return r;
}

bool SaveWeatherAsset(const std::string& path,WeatherAssetData& w,std::string* error){
    NormalizeWeather(w);if(!w.assetId.Valid())w.assetId=AssetHandle::Generate();std::error_code ec;const std::filesystem::path p(path);if(p.has_parent_path())std::filesystem::create_directories(p.parent_path(),ec);std::ofstream out(path,std::ios::trunc);if(!out){Error(error,"Could not write weather asset: "+path);return false;}
    out<<"3DG_WEATHER 1 "<<w.assetId.ToString()<<'\n'<<std::quoted(w.name)<<'\n'
       <<w.timeOfDay<<' '<<w.skyLightIntensity<<' '<<w.sunIntensity<<'\n'
       <<w.clouds<<' '<<w.cloudCoverage<<' '<<w.cloudDensity<<' '<<w.cloudScale<<' '<<w.cloudSoftness<<' '<<w.cloudWindSpeed<<' '<<w.cloudWindDirection<<' '<<w.cloudColor.r<<' '<<w.cloudColor.g<<' '<<w.cloudColor.b<<' '<<w.cloudShadows<<' '<<w.cloudShadowStrength<<'\n'
       <<w.fog<<' '<<w.fogColor.r<<' '<<w.fogColor.g<<' '<<w.fogColor.b<<' '<<w.fogDensity<<' '<<w.fogHeight<<' '<<w.fogHeightFalloff<<'\n'
       <<static_cast<int>(w.precipitation)<<' '<<w.precipitationIntensity<<' '<<w.precipitationSize<<' '<<w.precipitationSpeed<<' '<<w.windStrength<<' '<<w.windDirection<<' '<<w.lightning<<' '<<w.lightningFrequency<<' '<<w.lightningIntensity<<' '<<w.surfaceWetness<<' '<<w.puddleAmount<<' '<<w.defaultTransitionSeconds<<' '<<w.previewSeed<<'\n'
       <<std::quoted(w.particleAssetPath)<<' '<<(w.particleAssetId.Valid()?w.particleAssetId.ToString():"-")<<' '<<std::quoted(w.ambientAudioPath)<<' '<<(w.ambientAudioId.Valid()?w.ambientAudioId.ToString():"-")<<'\n';
    std::vector<AssetHandle> deps;if(w.particleAssetId.Valid())deps.push_back(w.particleAssetId);if(w.ambientAudioId.Valid()&&w.ambientAudioId!=w.particleAssetId)deps.push_back(w.ambientAudioId);out<<"ASSET_DEPS "<<deps.size();for(auto id:deps)out<<' '<<id.ToString();out<<'\n';Error(error,{});return static_cast<bool>(out);
}

bool LoadWeatherAsset(const std::string& path,WeatherAssetData* output,std::string* error){
    if(!output){Error(error,"Weather output is null.");return false;}std::ifstream in(path);WeatherAssetData w;std::string magic,id,particleId,audioId;int version=0,type=0;
    if(!(in>>magic>>version>>id)||magic!="3DG_WEATHER"||version!=1||!AssetHandle::Parse(id,&w.assetId)){Error(error,"Invalid weather asset: "+path);return false;}
    in>>std::quoted(w.name)>>w.timeOfDay>>w.skyLightIntensity>>w.sunIntensity
      >>w.clouds>>w.cloudCoverage>>w.cloudDensity>>w.cloudScale>>w.cloudSoftness>>w.cloudWindSpeed>>w.cloudWindDirection>>w.cloudColor.r>>w.cloudColor.g>>w.cloudColor.b>>w.cloudShadows>>w.cloudShadowStrength
      >>w.fog>>w.fogColor.r>>w.fogColor.g>>w.fogColor.b>>w.fogDensity>>w.fogHeight>>w.fogHeightFalloff
      >>type>>w.precipitationIntensity>>w.precipitationSize>>w.precipitationSpeed>>w.windStrength>>w.windDirection>>w.lightning>>w.lightningFrequency>>w.lightningIntensity>>w.surfaceWetness>>w.puddleAmount>>w.defaultTransitionSeconds>>w.previewSeed
      >>std::quoted(w.particleAssetPath)>>particleId>>std::quoted(w.ambientAudioPath)>>audioId;
    if(!in||type<0||type>static_cast<int>(PrecipitationType::Snow)||(particleId!="-"&&!AssetHandle::Parse(particleId,&w.particleAssetId))||(audioId!="-"&&!AssetHandle::Parse(audioId,&w.ambientAudioId))){Error(error,"Weather asset is malformed: "+path);return false;}
    w.precipitation=static_cast<PrecipitationType>(type);NormalizeWeather(w);*output=std::move(w);Error(error,{});return true;
}
} // namespace engine
