#pragma once

#include "engine/animation/Animator.h"
#include "engine/assets/AssetIdentity.h"

#include <cstdint>
#include <string>
#include <vector>

namespace engine {

inline constexpr std::uint32_t kPoseLibraryAssetVersion = 1;

struct PoseLibraryBone {
    std::string name;
    BoneLocal local;
};

struct PoseLibraryPose {
    std::string name = "NewPose";
    std::vector<std::string> tags;
    std::vector<PoseLibraryBone> bones;
};

struct PoseLibraryAssetData {
    NativeAssetHeader header;
    std::string name = "NewPoseLibrary";
    AssetHandle skeletonId;
    std::string skeletonPath;
    std::string previewModelPath;
    std::vector<PoseLibraryPose> poses;
};

void NormalizePoseLibraryAsset(PoseLibraryAssetData& asset);
bool ValidatePoseLibraryAsset(const PoseLibraryAssetData& asset, std::string* error = nullptr);
bool SavePoseLibraryAsset(const std::string& path, PoseLibraryAssetData asset,
                          std::string* error = nullptr);
bool LoadPoseLibraryAsset(const std::string& path, PoseLibraryAssetData* asset,
                          std::string* error = nullptr);
const PoseLibraryPose* FindPose(const PoseLibraryAssetData& asset, const std::string& name);
PoseLibraryPose MirrorPose(const PoseLibraryPose& source, const std::string& mirroredName = {});
PoseLibraryPose BlendPoses(const PoseLibraryPose& a, const PoseLibraryPose& b,
                           float weight, const std::string& name = {});
void ResolvePoseForSkeleton(const PoseLibraryPose& pose, const Skeleton& skeleton,
                            std::vector<BoneLocal>& output);

} // namespace engine
