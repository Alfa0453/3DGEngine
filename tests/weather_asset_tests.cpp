#include <engine/assets/WeatherAsset.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>

namespace { void Check(bool value,const char* message){if(value)return;std::cerr<<"FAILED: "<<message<<'\n';std::exit(1);} bool Near(float a,float b){return std::abs(a-b)<.0001f;} }

int main(){namespace fs=std::filesystem;std::string error;const fs::path path=fs::temp_directory_path()/"weather_asset_tests.3dgweather";
    engine::WeatherAssetData rain;rain.name="Heavy Rain";rain.precipitation=engine::PrecipitationType::Rain;rain.precipitationIntensity=1.4f;rain.cloudCoverage=.2f;rain.surfaceWetness=.9f;rain.particleAssetId=engine::AssetHandle::Generate();rain.particleAssetPath="Assets/Particles/Rain.particle";
    Check(engine::SaveWeatherAsset(path.string(),rain,&error),"weather saves");Check(rain.assetId.Valid(),"save assigns stable id");Check(Near(rain.precipitationIntensity,1.f),"save normalizes values");
    engine::WeatherAssetData loaded;Check(engine::LoadWeatherAsset(path.string(),&loaded,&error),"weather loads");Check(loaded.assetId==rain.assetId&&loaded.precipitation==engine::PrecipitationType::Rain&&loaded.particleAssetId==rain.particleAssetId,"weather identity and effects round trip");
    engine::WeatherAssetData clear;clear.cloudDensity=.2f;clear.precipitationIntensity=0;const auto middle=engine::BlendWeather(clear,loaded,.5f);Check(middle.cloudDensity>clear.cloudDensity&&middle.cloudDensity<loaded.cloudDensity,"transition blends numeric fields");Check(Near(engine::BlendWeather(clear,loaded,-1).precipitationIntensity,0),"transition clamps low alpha");Check(Near(engine::BlendWeather(clear,loaded,2).precipitationIntensity,1),"transition clamps high alpha");
    std::error_code ec;fs::remove(path,ec);std::cout<<"weather asset tests passed\n";return 0;}
