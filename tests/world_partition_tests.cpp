#include "WorldPartitionPanel.h"

#include <engine/scene/LevelStreamingManager.h>

#include <filesystem>
#include <iostream>

namespace { bool Check(bool v,const char* m){if(!v)std::cerr<<m<<'\n';return v;} }

int main(){
    engine::WorldManifest world;world.persistentScenePath="Persistent.scene";world.partition.enabled=true;
    world.partition.cellSize=100;world.partition.origin={-50,-50};world.partition.defaultLoadRadius=90;world.partition.defaultUnloadRadius=130;world.partition.activeDataLayers={"Gameplay"};
    engine::LevelRef a;a.scenePath="A.scene";a.worldTransform[3]=glm::vec4(75,0,75,1);a.dataLayer="Gameplay";a.boundsMin={-10,0,-10};a.boundsMax={10,10,10};
    engine::LevelRef b=a;b.scenePath="B.scene";b.worldTransform[3]=glm::vec4(-75,0,-75,1);b.dataLayer="Decoration";b.streamingPriority=8;
    world.levels={a,b};WorldPartitionPanel::AssignCells(world,true);
    if(!Check(world.levels[0].partitionX==1&&world.levels[0].partitionZ==1,"positive cell assignment failed"))return 1;
    if(!Check(world.levels[1].partitionX==-1&&world.levels[1].partitionZ==-1,"negative cell assignment failed"))return 1;
    if(!Check(world.levels[0].loadRadius==90&&world.levels[0].unloadRadius==130,"default ranges were not applied"))return 1;
    if(!Check(WorldPartitionPanel::BuildCells(world).size()==2,"cell aggregation failed"))return 1;
    if(!Check(WorldPartitionPanel::Validate(world).empty(),"valid partition reported issues"))return 1;

    const auto path=std::filesystem::temp_directory_path()/"3dg_partition_world.3dgworld";std::string error;
    if(!Check(engine::SaveWorldManifest(path.string(),world,&error),"world v3 save failed"))return 1;
    engine::WorldManifest loaded;if(!Check(engine::LoadWorldManifest(path.string(),&loaded,&error),"world v3 load failed"))return 1;
    if(!Check(loaded.partition.enabled&&loaded.partition.cellSize==100&&loaded.partition.activeDataLayers.size()==1,"partition settings did not round trip"))return 1;
    if(!Check(loaded.levels[1].partitionX==-1&&loaded.levels[1].streamingPriority==8,"cell metadata did not round trip"))return 1;
    std::error_code ec;std::filesystem::remove(path,ec);return 0;
}
