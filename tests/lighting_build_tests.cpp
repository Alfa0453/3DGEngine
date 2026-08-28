#include <engine/graphics/LightingBuildData.h>
#include <engine/graphics/DynamicIrradiance.h>
#include <engine/graphics/PbrLightingCommon.h>
#include <engine/graphics/EnvironmentLighting.h>
#include <engine/graphics/LightingScalability.h>
#include <engine/ecs/Components.h>
#include <glm/gtc/epsilon.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
int failures=0;
void Check(bool condition,const char* message){if(!condition){std::cerr<<"FAIL: "<<message<<'\n';++failures;}}
void Quad(std::vector<engine::LightingTriangle>& out,float y,float half){
    out.push_back({{-half,y,-half},{ half,y,-half},{ half,y, half}});
    out.push_back({{-half,y,-half},{ half,y, half},{-half,y, half}});
}
float ClosestVisibility(const engine::LightingBuildData& data,const glm::vec3& target){
    float best=1e30f,value=1.0f;
    for(int z=0;z<data.dimensions.z;++z)for(int y=0;y<data.dimensions.y;++y)for(int x=0;x<data.dimensions.x;++x){
        glm::vec3 p=data.boundsMin+glm::vec3(x,y,z)*data.spacing;const glm::vec3 delta=p-target;float d=glm::dot(delta,delta);
        if(d<best){best=d;value=data.probes[data.Index(x,y,z)].skyVisibility;}}
    return value;
}
glm::vec3 EvaluateProbe(const engine::LightingProbe& probe,const glm::vec3& n){
    return glm::max(probe.irradianceSH[0]*0.2820947918f
        +probe.irradianceSH[1]*(0.4886025119f*n.y)
        +probe.irradianceSH[2]*(0.4886025119f*n.z)
        +probe.irradianceSH[3]*(0.4886025119f*n.x),glm::vec3(0.0f));
}
float SumContribution(const engine::LightingBuildData& data,bool emissive){
    float total=0.0f;
    for(const auto& probe:data.probes)for(const auto& coefficient:(emissive?probe.emissiveSH:probe.bounceSH))
        total+=coefficient.r+coefficient.g+coefficient.b;
    return total;
}
}

int main(){
    const auto& lightingLow = engine::GetLightingQualityProfile(engine::LightingQuality::Low);
    const auto& lightingHigh = engine::GetLightingQualityProfile(engine::LightingQuality::High);
    const auto& lightingUltra = engine::GetLightingQualityProfile(engine::LightingQuality::Ultra);
    Check(lightingLow.shadowFilterSamples < lightingHigh.shadowFilterSamples
          && lightingHigh.shadowFilterSamples <= lightingUltra.shadowFilterSamples,
          "lighting quality increases shadow filtering monotonically");
    Check(lightingLow.reflectionBudgetBytes < lightingHigh.reflectionBudgetBytes,
          "lighting quality exposes an explicit reflection-memory budget");
    Check(lightingLow.maxVolumetricLights < lightingUltra.maxVolumetricLights
          && lightingLow.volumetricSlices < lightingUltra.volumetricSlices,
          "lighting quality scales volumetric work as one profile");
    const auto noonSample = engine::DayNightCycle::At(0.5f);
    const engine::EnvironmentLightingState noon =
        engine::ResolveEnvironmentLighting(0.5f, noonSample);
    const glm::vec3 noonZenith = noon.SampleEnvironmentRadiance({0,1,0});
    Check(std::isfinite(noonZenith.x) && std::isfinite(noonZenith.y)
          && std::isfinite(noonZenith.z) && glm::dot(noonZenith,noonZenith)>0.0f,
          "shared atmosphere sampler returns finite daylight radiance");
    const auto nightSample = engine::DayNightCycle::At(0.0f);
    const engine::EnvironmentLightingState night =
        engine::ResolveEnvironmentLighting(0.0f, nightSample);
    Check(glm::length(night.ToDirectionalSkyRadiance().zenith)
          < glm::length(noon.ToDirectionalSkyRadiance().zenith),
          "night environment is dimmer than the daylight GI source");
    engine::DynamicIrradianceSettings dynamicSettings;
    dynamicSettings.enabled = true;
    dynamicSettings.boundsMin = {-2.0f, 0.0f, -2.0f};
    dynamicSettings.boundsMax = { 2.0f, 2.0f,  2.0f};
    dynamicSettings.probeSpacing = 1.0f;
    dynamicSettings.raysPerProbe = 16;
    dynamicSettings.probesPerFrame = 3;
    dynamicSettings.maxGiRaysPerFrame = 32;
    dynamicSettings.Normalize();
    Check(dynamicSettings.maxGiRaysPerFrame == 32,
          "dynamic GI normalization preserves a valid ray budget");
    engine::DirectionalSkyRadiance dynamicSky;
    dynamicSky.zenith = {0.2f, 0.3f, 0.5f};
    dynamicSky.horizon = {0.1f, 0.12f, 0.16f};
    dynamicSky.ground = {0.02f, 0.02f, 0.02f};
    dynamicSky.sunDirection = glm::normalize(glm::vec3(0.4f, 1.0f, 0.2f));
    dynamicSky.sunRadiance = {2.0f, 1.8f, 1.5f};
    engine::DynamicIrradianceSystem dynamicGi;
    std::string dynamicError;
    Check(dynamicGi.Configure(dynamicSettings, dynamicSky, nullptr, &dynamicError),
          "dynamic GI configures without baked data");
    std::vector<engine::LightingTriangle> dynamicFloor;
    Quad(dynamicFloor, 0.0f, 5.0f);
    dynamicGi.SetSceneGeometry(dynamicFloor);
    dynamicGi.Update({0.0f, 1.0f, 0.0f}, dynamicSky, {}, 1u);
    Check(dynamicGi.Stats().probesUpdated <= 2u,
          "dynamic GI obeys the per-frame ray budget");
    Check(dynamicGi.Stats().raysCast <= dynamicSettings.maxGiRaysPerFrame,
          "dynamic GI never exceeds max rays per frame");
    Check(dynamicGi.ComposedData().IsValid(),
          "dynamic GI produces a shader-compatible SH4 grid");

    engine::LightingBuildSettings settings;settings.quality=engine::LightingBuildQuality::Preview;
    settings.probeSpacing=1.0f;settings.boundsPadding=0.25f;settings.maxRayDistance=30.0f;settings.raysPerProbe=24;
    std::vector<engine::LightingTriangle> open;Quad(open,0.0f,10.0f);
    engine::LightingBuildData outside;std::string error;
    Check(engine::BuildLightingProbes(open,{1,1,1},1,"Open",settings,&outside,nullptr,&error),"open lighting build succeeds");
    std::vector<engine::LightingTriangle> closed=open;Quad(closed,2.0f,10.0f);
    engine::LightingBuildData inside;
    Check(engine::BuildLightingProbes(closed,{1,1,1},2,"Closed",settings,&inside,nullptr,&error),"closed lighting build succeeds");
    Check(ClosestVisibility(outside,{0,1,0})>0.9f,"open sky remains visible");
    Check(ClosestVisibility(inside,{0,1,0})<0.2f,"roof blocks local sky visibility");
    const auto path=std::filesystem::temp_directory_path()/"3dg_lighting_build_test.3dglighting";
    Check(engine::SaveLightingBuildData(path.string(),inside,&error),"lighting asset saves transactionally");
    engine::LightingBuildData loaded;Check(engine::LoadLightingBuildData(path.string(),&loaded,&error),"lighting asset loads");
    Check(loaded.sourceHash==inside.sourceHash&&loaded.probes.size()==inside.probes.size(),"lighting metadata round trips");
    bool shRoundTrip=true;for(std::size_t i=0;i<4;++i)shRoundTrip&=glm::all(glm::epsilonEqual(loaded.probes.front().irradianceSH[i],inside.probes.front().irradianceSH[i],1e-6f));
    Check(shRoundTrip,"directional SH coefficients round trip");
    engine::DirectionalSkyRadiance directional;directional.zenith=glm::vec3(2.0f,0.2f,0.1f);directional.horizon=glm::vec3(0.1f);directional.ground=glm::vec3(0.01f);
    engine::LightingBuildData directionalData;
    Check(engine::BuildLightingProbes(open,directional,3,"Directional",settings,&directionalData,nullptr,&error),"directional lighting build succeeds");
    const auto& probe=directionalData.probes[directionalData.Index(directionalData.dimensions.x/2,directionalData.dimensions.y-1,directionalData.dimensions.z/2)];
    Check(EvaluateProbe(probe,{0,1,0}).r>EvaluateProbe(probe,{0,-1,0}).r,"SH probes preserve sky directionality");
    Check(std::isfinite(engine::SmoothFiniteLightAttenuation(0.0f,5.0f)),"finite attenuation is stable at the light origin");
    Check(engine::SmoothFiniteLightAttenuation(25.0f,5.0f)==0.0f,"finite attenuation reaches zero at its radius");
    Check(engine::SmoothFiniteLightAttenuation(36.0f,5.0f)==0.0f,"finite attenuation remains zero outside its radius");

    // Emissive transport is a separate path: disabling diffuse bounce must not
    // accidentally disable emissive surfaces. Preview intentionally disables both.
    std::vector<engine::LightingTriangle> emissiveSurface=open;
    for(auto& triangle:emissiveSurface)triangle.emissive=glm::vec3(8.0f,0.5f,0.1f);
    engine::LightingBuildSettings emissiveSettings=settings;
    emissiveSettings.quality=engine::LightingBuildQuality::Medium;
    emissiveSettings.raysPerProbe=32;
    emissiveSettings.indirectBounceEnabled=false;
    emissiveSettings.indirectBounceStrength=0.0f;
    emissiveSettings.emissiveContribution=1.0f;
    engine::LightingBuildData emissiveData;
    Check(engine::BuildLightingProbes(emissiveSurface,{0,0,0},4,"Emissive",emissiveSettings,&emissiveData,nullptr,&error),"emissive-only lighting build succeeds");
    Check(SumContribution(emissiveData,true)>0.01f,"emissive lighting remains active when diffuse bounce is disabled");
    engine::LightingBuildData previewData;
    emissiveSettings.quality=engine::LightingBuildQuality::Preview;
    Check(engine::BuildLightingProbes(emissiveSurface,{0,0,0},5,"Preview",emissiveSettings,&previewData,nullptr,&error),"preview lighting build succeeds");
    Check(SumContribution(previewData,true)==0.0f&&SumContribution(previewData,false)==0.0f,"preview quality skips bounce and emissive transport");

    // Parallel probe ranges must remain deterministic, and cancellation must
    // never replace the caller's last known-good data.
    engine::LightingBuildData deterministicA,deterministicB;
    emissiveSettings.quality=engine::LightingBuildQuality::Medium;
    Check(engine::BuildLightingProbes(emissiveSurface,{0,0,0},6,"Deterministic",emissiveSettings,&deterministicA,nullptr,&error),"first deterministic build succeeds");
    Check(engine::BuildLightingProbes(emissiveSurface,{0,0,0},6,"Deterministic",emissiveSettings,&deterministicB,nullptr,&error),"second deterministic build succeeds");
    bool deterministic=deterministicA.probes.size()==deterministicB.probes.size();
    for(std::size_t i=0;deterministic&&i<deterministicA.probes.size();++i)
        for(std::size_t c=0;c<4;++c)
            deterministic&=glm::all(glm::epsilonEqual(deterministicA.probes[i].irradianceSH[c],deterministicB.probes[i].irradianceSH[c],1e-6f));
    Check(deterministic,"multithreaded probe builds are deterministic");
    engine::LightingBuildProgress cancelledProgress;cancelledProgress.cancel=true;
    engine::LightingBuildData preserved=deterministicA;const auto preservedHash=preserved.sourceHash;
    error.clear();
    Check(!engine::BuildLightingProbes(emissiveSurface,{0,0,0},99,"Cancelled",emissiveSettings,&preserved,&cancelledProgress,&error),"cancelled lighting build reports failure");
    Check(preserved.sourceHash==preservedHash&&error.find("cancelled")!=std::string::npos,"cancellation preserves the previous lighting build");
    const std::string composed=engine::ComposePbrLightingShader("#version 330 core\n//__PBR_LIGHTING_COMMON__\nvoid main(){}");
    Check(composed.find("SmoothFiniteAttenuation")!=std::string::npos&&composed.find("//__PBR_LIGHTING_COMMON__")==std::string::npos,"shared PBR shader block is injected");
    Check(composed.find("BoxProjectedDirection")!=std::string::npos,"shared PBR shader includes reflection-probe parallax correction");
    Check(composed.find("LtcIntegrateQuad")!=std::string::npos&&composed.find("LtcRectangleLight")!=std::string::npos,"shared PBR shader includes area-integrated rectangle lighting");
    engine::ecs::ReflectionProbe reflectionProbe;
    Check(reflectionProbe.shape==engine::ecs::ReflectionProbe::Shape::Box,
          "reflection probes default to box influence volumes");
    Check(reflectionProbe.CaptureIsStale(1234),
          "an uncaptured reflection probe starts stale");
    reflectionProbe.bakedCubemapPath="Reflections/Room.3dgtex";
    reflectionProbe.captureSourceHash=1234;
    Check(!reflectionProbe.CaptureIsStale(1234),
          "matching reflection capture hashes remain valid");
    Check(reflectionProbe.CaptureIsStale(5678),
          "scene changes invalidate a reflection capture hash");
    const auto oldPath=std::filesystem::temp_directory_path()/"3dg_lighting_v1_test.3dglighting";
    {std::ofstream old(oldPath,std::ios::binary);old.write("3DGLITE1",8);}
    engine::LightingBuildData oldData;error.clear();
    Check(!engine::LoadLightingBuildData(oldPath.string(),&oldData,&error)&&error.find("outdated")!=std::string::npos,"legacy lighting data requests an explicit rebuild");
    std::error_code ec;std::filesystem::remove(path,ec);
    std::filesystem::remove(oldPath,ec);
    if(failures==0)std::cout<<"Lighting build tests passed\n";return failures==0?0:1;
}
