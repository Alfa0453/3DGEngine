#include <engine/assets/ScatterGraphAsset.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>

namespace {
void Check(bool condition,const char*message){if(condition)return;std::cerr<<"FAILED: "<<message<<'\n';std::exit(1);}
engine::ScatterGraphAssetData MakeGraph(){
    engine::ScatterGraphAssetData graph;graph.name="Forest";graph.seed=42;graph.maximumInstances=250;
    graph.regionMinimum={-5,-10,-5};graph.regionMaximum={5,10,5};
    engine::ScatterGraphNode density;density.id=1;density.type=engine::ScatterNodeType::Density;density.density=.5f;
    engine::ScatterGraphNode height;height.id=2;height.type=engine::ScatterNodeType::HeightFilter;height.input=1;height.minimum=-1;height.maximum=2;
    engine::ScatterGraphNode slope;slope.id=3;slope.type=engine::ScatterNodeType::SlopeFilter;slope.input=2;slope.minimum=0;slope.maximum=25;
    engine::ScatterGraphNode transform;transform.id=4;transform.type=engine::ScatterNodeType::Transform;transform.input=3;transform.minimumScale=glm::vec3(.8f);transform.maximumScale=glm::vec3(1.2f);
    engine::ScatterGraphNode oak;oak.id=5;oak.type=engine::ScatterNodeType::MeshOutput;oak.input=4;oak.meshPath="Trees/Oak.3dgmesh";oak.meshId={1,2};oak.weight=3;
    engine::ScatterGraphNode pine=oak;pine.id=6;pine.meshPath="Trees/Pine.3dgmesh";pine.meshId={3,4};pine.weight=1;
    graph.nodes={density,height,slope,transform,oak,pine};return graph;
}
}

int main(){
    auto graph=MakeGraph();std::string error;
    Check(engine::ValidateScatterGraphAsset(graph,&error),"valid graph accepted");
    auto sampler=[](float x,float z){engine::ScatterSurfaceSample s;s.height=.1f*x;s.normal=glm::normalize(glm::vec3(-.1f,1,0));if(x*x+z*z<1)s.layerMask=0;return s;};
    const auto a=engine::EvaluateScatterGraph(graph,sampler),b=engine::EvaluateScatterGraph(graph,sampler);
    Check(!a.empty()&&a.size()==b.size(),"deterministic evaluator produces placements");
    for(std::size_t i=0;i<a.size();++i)Check(a[i].position==b[i].position&&a[i].meshPath==b[i].meshPath,"same seed produces identical results");
    const auto c=engine::EvaluateScatterGraph(graph,sampler,{},99);
    Check(!c.empty()&&c.front().position!=a.front().position,"seed override changes distribution");
    engine::ScatterGraphNode exclusion;exclusion.id=7;exclusion.type=engine::ScatterNodeType::ExclusionCircle;exclusion.position={0,0,0};exclusion.radius=100;graph.nodes.push_back(exclusion);
    Check(engine::EvaluateScatterGraph(graph,sampler).empty(),"exclusion node removes candidates");graph.nodes.pop_back();
    const auto root=std::filesystem::temp_directory_path()/"3dg_scatter_graph_tests";std::error_code ec;std::filesystem::remove_all(root,ec);const auto path=root/"Forest.3dgscatter";
    Check(engine::SaveScatterGraphAsset(path.string(),graph,&error),"save native scatter graph");engine::ScatterGraphAssetData loaded;
    Check(engine::LoadScatterGraphAsset(path.string(),&loaded,&error)&&loaded.header.type==engine::AssetType::ScatterGraph&&loaded.header.id.Valid()&&loaded.header.dependencies.size()==2&&loaded.nodes.size()==graph.nodes.size(),"scatter graph round trips identity nodes and dependencies");
    loaded.nodes.back().input=999;Check(!engine::ValidateScatterGraphAsset(loaded,&error),"missing node connection rejected");std::filesystem::remove_all(root,ec);
    std::cout<<"scatter graph tests passed\n";return 0;
}
