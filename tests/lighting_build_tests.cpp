#include <engine/graphics/LightingBuildData.h>
#include <engine/graphics/PbrLightingCommon.h>
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
}

int main(){
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
    const std::string composed=engine::ComposePbrLightingShader("#version 330 core\n//__PBR_LIGHTING_COMMON__\nvoid main(){}");
    Check(composed.find("SmoothFiniteAttenuation")!=std::string::npos&&composed.find("//__PBR_LIGHTING_COMMON__")==std::string::npos,"shared PBR shader block is injected");
    const auto oldPath=std::filesystem::temp_directory_path()/"3dg_lighting_v1_test.3dglighting";
    {std::ofstream old(oldPath,std::ios::binary);old.write("3DGLITE1",8);}
    engine::LightingBuildData oldData;error.clear();
    Check(!engine::LoadLightingBuildData(oldPath.string(),&oldData,&error)&&error.find("outdated")!=std::string::npos,"legacy lighting data requests an explicit rebuild");
    std::error_code ec;std::filesystem::remove(path,ec);
    std::filesystem::remove(oldPath,ec);
    if(failures==0)std::cout<<"Lighting build tests passed\n";return failures==0?0:1;
}
