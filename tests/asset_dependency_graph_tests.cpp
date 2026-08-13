#include "AssetDependencyGraph.h"

#include <cassert>
#include <iostream>

namespace {
engine::AssetHandle Id(std::uint64_t value){return {0,value};}
engine::AssetRegistryEntry Entry(std::uint64_t id,engine::AssetType type,const char* path){engine::AssetRegistryEntry e;e.id=Id(id);e.type=type;e.virtualPath=path;return e;}
}

int main(){
    engine::AssetRegistry registry;std::string error;
    auto material=Entry(1,engine::AssetType::Material,"/Game/Materials/M.3dgmat");material.dependencies={Id(2),Id(99),Id(2)};material.sourceHash=77;
    auto texture=Entry(2,engine::AssetType::Texture,"/Game/Textures/T.3dgtex");texture.dependencies={Id(3)};texture.sourceHash=88;
    auto shader=Entry(3,engine::AssetType::Shader,"/Game/Shaders/S.3dgshader");shader.dependencies={Id(1)};
    auto unused=Entry(4,engine::AssetType::Audio,"/Game/Audio/U.3dgaudio");
    auto duplicate=Entry(5,engine::AssetType::Material,"/Game/Materials/M_Copy.3dgmat");duplicate.sourceHash=77;
    assert(registry.Register(material,&error));assert(registry.Register(texture,&error));assert(registry.Register(shader,&error));assert(registry.Register(unused,&error));assert(registry.Register(duplicate,&error));
    AssetDependencyGraph graph;graph.Build(registry);
    assert(graph.Nodes().size()==5);assert(graph.Find(Id(2))->incoming.size()==1);
    assert(graph.HasCircularDependency(Id(1)));assert(graph.HasCircularDependency(Id(2)));assert(graph.HasCircularDependency(Id(3)));
    bool missing=false,duplicateRef=false,duplicateContent=false,unreferenced=false;
    for(const auto& issue:graph.Issues()){
        missing|=issue.kind==AssetDependencyGraph::IssueKind::Missing;
        duplicateRef|=issue.kind==AssetDependencyGraph::IssueKind::DuplicateDependency;
        duplicateContent|=issue.kind==AssetDependencyGraph::IssueKind::DuplicateContent;
        unreferenced|=issue.kind==AssetDependencyGraph::IssueKind::Unreferenced&&issue.asset==Id(4);
    }
    assert(missing&&duplicateRef&&duplicateContent&&unreferenced);
    assert(graph.Search("materials").size()==1);
    assert(graph.Search("",engine::AssetType::Texture).size()==1);
    assert(graph.Search("",engine::AssetType::Unknown,true).size()==5);
    std::cout<<"asset dependency graph tests passed\n";return 0;
}
