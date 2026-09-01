#include <engine/assets/AssetRegistry.h>
#include <engine/assets/PoseLibraryAsset.h>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <filesystem>
#include <iostream>

namespace { int failures=0; void Check(bool v,const char* m){if(!v){++failures;std::cerr<<"FAIL: "<<m<<'\n';}} bool Near(float a,float b){return std::abs(a-b)<.002f;} }

int main(){
    engine::PoseLibraryAssetData library;library.header.id=engine::AssetHandle::Generate();library.name="WizardPoses";library.skeletonId=engine::AssetHandle::Generate();library.skeletonPath="GameAssets/Skeletons/Wizard.3dgskel";
    engine::PoseLibraryPose cast;cast.name="Cast";cast.tags={"magic","combat","magic"};
    engine::PoseLibraryBone left;left.name="LeftHand";left.local.pos={-1,2,3};left.local.rot=glm::angleAxis(glm::radians(30.f),glm::vec3(0,1,0));cast.bones.push_back(left);library.poses.push_back(cast);
    engine::NormalizePoseLibraryAsset(library);Check(library.poses[0].tags.size()==2,"tags normalize");Check(library.header.dependencies.size()==1,"skeleton dependency recorded");
    std::string error;Check(engine::ValidatePoseLibraryAsset(library,&error),"valid library");
    const auto root=std::filesystem::temp_directory_path()/"3dg_pose_library_test";std::filesystem::create_directories(root);const auto path=root/"Wizard.3dgpose";
    Check(engine::SavePoseLibraryAsset(path.string(),library,&error),"save library");engine::PoseLibraryAssetData loaded;Check(engine::LoadPoseLibraryAsset(path.string(),&loaded,&error),"load library");
    Check(loaded.header.id==library.header.id&&engine::FindPose(loaded,"Cast"),"identity and pose round trip");
    const auto mirrored=engine::MirrorPose(*engine::FindPose(loaded,"Cast"));Check(mirrored.bones[0].name=="RightHand"&&Near(mirrored.bones[0].local.pos.x,1.f),"mirror swaps side and axis");
    engine::Skeleton skeleton;engine::Bone rootBone;rootBone.name="Root";rootBone.parent=-1;rootBone.localBind=glm::translate(glm::mat4(1),glm::vec3(0,4,0));skeleton.bones.push_back(rootBone);engine::Bone hand;hand.name="LeftHand";hand.parent=0;hand.localBind=glm::mat4(1);skeleton.bones.push_back(hand);
    std::vector<engine::BoneLocal> resolved;engine::ResolvePoseForSkeleton(*engine::FindPose(loaded,"Cast"),skeleton,resolved);Check(resolved.size()==2&&Near(resolved[0].pos.y,4)&&Near(resolved[1].pos.y,2),"name resolution and bind fallback");
    engine::AssetRegistry registry;Check(registry.RebuildFromContent(root.string(),&error),"registry scan");const auto* entry=registry.Find(library.header.id);Check(entry&&entry->type==engine::AssetType::PoseLibrary,"registry pose type");
    std::filesystem::remove_all(root);if(failures)return 1;std::cout<<"Pose library tests passed\n";return 0;
}
