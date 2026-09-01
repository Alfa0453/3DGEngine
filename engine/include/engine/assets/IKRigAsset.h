#pragma once

#include "engine/assets/AssetIdentity.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace engine {

inline constexpr std::uint32_t kIKRigAssetVersion = 1;

enum class IKGoalType : std::uint32_t {
    TwoBone = 0,
    LookAt,
    Aim,
    WeaponAlignment
};

struct IKGoalDefinition {
    std::string name = "IKGoal";
    IKGoalType type = IKGoalType::TwoBone;
    bool enabled = true;
    std::string rootBone;
    std::string midBone;
    std::string endBone;
    glm::vec3 targetOffset{0.0f, 1.0f, -1.0f};
    glm::vec3 poleOffset{0.0f, 0.0f, 1.0f};
    glm::vec3 forwardAxis{0.0f, 0.0f, 1.0f};
    float weight = 1.0f;
    float maxAngleDegrees = 90.0f;
    float interpolationSpeed = 12.0f;
};

struct IKFootPlacementDefinition {
    bool enabled = false;
    std::string pelvisBone;
    std::string leftUpperBone, leftMidBone, leftFootBone;
    std::string rightUpperBone, rightMidBone, rightFootBone;
    float traceUp = 0.5f;
    float traceDown = 0.8f;
    float footHeight = 0.02f;
    float pelvisWeight = 1.0f;
    float maxPelvisDrop = 0.5f;
    float weight = 1.0f;
};

struct IKRigAssetData {
    NativeAssetHeader header;
    std::string name = "NewIKRig";
    AssetHandle skeletonId;
    std::string skeletonPath;
    IKFootPlacementDefinition feet;
    std::vector<IKGoalDefinition> goals;
};

void NormalizeIKRigAsset(IKRigAssetData& asset);
bool ValidateIKRigAsset(const IKRigAssetData& asset, std::string* error = nullptr);
bool SaveIKRigAsset(const std::string& path, IKRigAssetData asset,
                    std::string* error = nullptr);
bool LoadIKRigAsset(const std::string& path, IKRigAssetData* asset,
                    std::string* error = nullptr);
const char* IKGoalTypeName(IKGoalType type);

} // namespace engine
