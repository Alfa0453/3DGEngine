#include <engine/assets/AssetRegistry.h>
#include <engine/assets/DestructionAsset.h>
#include <engine/ecs/Components.h>
#include <engine/ecs/Registry.h>
#include <engine/gameplay/DestructionSystem.h>

#include <cassert>
#include <cmath>
#include <filesystem>
#include <iostream>

namespace {
void Check(bool condition,const char* message){if(!condition){std::cerr<<"FAILED: "<<message<<'\n';std::abort();}}
bool Near(float a,float b){return std::abs(a-b)<.0001f;}
}

int main(){
    engine::DestructionAssetData asset;asset.header.id=engine::AssetHandle::Generate();asset.name="BreakableCrate";asset.bounds={2,3,4};asset.chunksX=2;asset.chunksY=3;asset.chunksZ=2;asset.maxHealth=100;asset.debrisLifetime=.1f;asset.sourceMeshPath="Content/Crate.3dgmesh";asset.sourceMeshId=engine::AssetHandle::Generate();asset.states.push_back({"Cracked",.65f});asset.states.push_back({"Critical",.25f});
    engine::NormalizeDestructionAsset(asset);std::string error;Check(engine::ValidateDestructionAsset(asset,&error),"valid destruction asset");
    const auto a=engine::GenerateDestructionChunks(asset,{0,0,0});const auto b=engine::GenerateDestructionChunks(asset,{0,0,0});Check(a.size()==12&&b.size()==a.size(),"fracture grid count");for(std::size_t i=0;i<a.size();++i)Check(a[i].localCenter==b[i].localCenter&&a[i].impulseDirection==b[i].impulseDirection,"fracture generation deterministic");
    Check(engine::DestructionStateForHealth(asset,80)==-1,"healthy state");Check(engine::DestructionStateForHealth(asset,60)==0,"first damage state");Check(engine::DestructionStateForHealth(asset,20)==1,"critical state");Check(engine::DestructionStateForHealth(asset,0)==2,"broken state");
    const auto root=std::filesystem::temp_directory_path()/"3dg_destruction_test";std::filesystem::create_directories(root);const auto path=root/"Crate.3dgdestruction";Check(engine::SaveDestructionAsset(path.string(),asset,&error),"save destruction asset");engine::DestructionAssetData loaded;Check(engine::LoadDestructionAsset(path.string(),&loaded,&error),"load destruction asset");Check(loaded.header.id==asset.header.id&&loaded.header.dependencies.size()==1,"identity and dependencies round trip");Check(loaded.states.size()==2&&Near(loaded.bounds.y,3),"state and bounds round trip");
    engine::AssetRegistry registryAssets;Check(registryAssets.RebuildFromContent(root.string(),&error),"scan destruction registry");const auto* entry=registryAssets.Find(asset.header.id);Check(entry&&entry->type==engine::AssetType::Destruction,"destruction registry type");
    engine::ecs::Registry registry;const auto object=registry.Create();registry.Add<engine::ecs::Transform>(object,{});registry.Add<engine::ecs::ModelAsset>(object,{asset.sourceMeshPath});Check(engine::ConfigureDestructible(registry,object,loaded,path.string()),"configure runtime destructible");Check(!engine::DamageDestructible(registry,object,40),"damage state does not break");Check(Near(engine::DestructibleHealth(registry,object),60),"damage updates health");Check(engine::DamageDestructible(registry,object,100,{0,0,0},{1,0,0}),"lethal damage breaks");Check(engine::IsDestructibleBroken(registry,object),"broken query");Check(!registry.Has<engine::ecs::ModelAsset>(object),"broken source hidden");const auto* models=registry.TryPool<engine::ecs::ModelAsset>();Check(models&&models->dense.size()==12,"runtime debris spawned");const auto events=engine::ConsumeDestructionEvents(registry,object);Check(events.size()==2&&events.back().type==engine::DestructionRuntimeEvent::Type::Broken,"damage and break events emitted");engine::UpdateDestruction(registry,.2f);Check(registry.AliveCount()==1,"debris lifetime cleanup");
    std::filesystem::remove_all(root);std::cout<<"Destruction asset tests passed\n";return 0;
}
