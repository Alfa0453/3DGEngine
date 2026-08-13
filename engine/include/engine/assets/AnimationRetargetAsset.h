#pragma once

#include "engine/animation/Skeleton.h"
#include "engine/assets/AssetIdentity.h"

#include <glm/vec3.hpp>

#include <string>
#include <vector>

namespace engine {

struct RetargetBoneMapping {
    std::string sourceBone;
    std::string targetBone;
    glm::vec3 rotationOffsetDegrees{0.0f};
    float translationScale = 1.0f;
    bool transferTranslation = false;
};

struct AnimationRetargetAssetData {
    int version = 1;
    AssetHandle assetId;
    std::string name = "RetargetProfile";
    std::string sourceSkeletonPath;
    std::string targetSkeletonPath;
    AssetHandle sourceSkeletonId;
    AssetHandle targetSkeletonId;
    std::string sourceRootBone;
    std::string targetRootBone;
    bool transferRootMotion = true;
    float globalScale = 1.0f;
    std::vector<RetargetBoneMapping> mappings;
};

std::string NormalizeRetargetBoneName(const std::string& name);
std::vector<RetargetBoneMapping> BuildAutomaticRetargetMap(
    const Skeleton& source, const Skeleton& target);
bool RetargetAnimationClip(const Skeleton& sourceSkeleton,
                           const Animation& sourceAnimation,
                           const Skeleton& targetSkeleton,
                           const AnimationRetargetAssetData& profile,
                           Animation* output,
                           std::string* error = nullptr);
bool SaveAnimationRetargetAsset(const std::string& path,
                                AnimationRetargetAssetData& asset,
                                std::string* error = nullptr);
bool LoadAnimationRetargetAsset(const std::string& path,
                                AnimationRetargetAssetData* asset,
                                std::string* error = nullptr);

} // namespace engine
