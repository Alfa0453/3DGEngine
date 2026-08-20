#include <engine/graphics/LightingBuildData.h>

#include <cmath>
#include <filesystem>
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
    std::error_code ec;std::filesystem::remove(path,ec);
    if(failures==0)std::cout<<"Lighting build tests passed\n";return failures==0?0:1;
}
