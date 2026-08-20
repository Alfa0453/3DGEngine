#include <engine/assets/FenceWallAsset.h>
#include <engine/assets/AssetRegistry.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>

namespace {
void Check(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAILED: " << message << '\n'; std::exit(1); }
}
bool Near(float a, float b, float epsilon=.001f) {
    return std::abs(a-b) <= epsilon;
}
}

int main() {
    engine::FenceWallAssetData fence;
    fence.header.id=engine::AssetHandle::Generate();
    fence.name="CourtyardFence";
    fence.points={{.11f,0,0},{10.12f,2,0}};
    fence.panelLength=2.0f;
    fence.postSpacing=2.0f;
    fence.snapToGrid=true;
    fence.gridSize=.25f;
    fence.openings.push_back({0,5.0f,2.0f,true});
    fence.panelMeshId=engine::AssetHandle::Generate();
    fence.panelMeshPath="Content/Meshes/FencePanel.3dgmesh";

    engine::FenceGenerationStats stats;
    std::string error;
    const auto placements=engine::GenerateFenceWall(fence,&stats,&error);
    Check(error.empty()&&!placements.empty(),"generate a connected fence run");
    Check(stats.gates==1,"gate opening produces one gate placement");
    Check(stats.posts>=6,"regular and gate-edge posts are generated");
    Check(stats.panels>=4,"solid ranges around the gate are panelized");
    Check(Near(stats.length,std::sqrt(104.0f),.02f),"slope length is preserved");
    bool pitched=false;
    for(const auto& part:placements)
        if(part.kind==engine::FencePartKind::Panel)
            pitched|=std::abs(part.rotation.x)>.001f||std::abs(part.rotation.z)>.001f;
    Check(pitched,"follow-slope panels receive a pitched orientation");

    const auto root=std::filesystem::temp_directory_path()/"3dg_fence_wall_regression";
    std::filesystem::remove_all(root);
    const auto path=root/"FenceWallRegression.3dgfence";
    Check(engine::SaveFenceWallAsset(path.string(),fence,&error),"save fence asset");
    engine::FenceWallAssetData loaded;
    Check(engine::LoadFenceWallAsset(path.string(),&loaded,&error),"load fence asset");
    Check(loaded.header.type==engine::AssetType::FenceWall
          && loaded.header.id==fence.header.id,"stable fence asset identity");
    Check(loaded.points.size()==2&&Near(loaded.points[0].x,0.0f)
          && Near(loaded.points[1].x,10.0f),"grid snapping persists");
    Check(loaded.openings.size()==1&&loaded.openings[0].gate,
          "gate metadata round trips");
    Check(loaded.header.dependencies.size()==1
          && loaded.header.dependencies[0]==fence.panelMeshId,
          "mesh dependency round trips");
    engine::AssetRegistry registry;
    Check(registry.RebuildFromContent(root.string(),&error),
          "content registry scans fence assets");
    const auto* entry=registry.Find(fence.header.id);
    Check(entry&&entry->type==engine::AssetType::FenceWall
          && entry->dependencies.size()==1,
          "registry keeps fence type and dependencies");
    std::filesystem::remove_all(root);
    std::cout<<"fence/wall asset tests passed\n";
    return 0;
}
