#include "engine/assets/IKRigAsset.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>

namespace engine {
namespace {
void Error(std::string* error, const std::string& text) { if (error) *error = text; }
bool Finite(const glm::vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
}

const char* IKGoalTypeName(IKGoalType type) {
    switch (type) {
    case IKGoalType::TwoBone: return "Two Bone";
    case IKGoalType::LookAt: return "Look At";
    case IKGoalType::Aim: return "Aim";
    case IKGoalType::WeaponAlignment: return "Weapon Alignment";
    }
    return "Two Bone";
}

void NormalizeIKRigAsset(IKRigAssetData& asset) {
    if (asset.name.empty()) asset.name = "NewIKRig";
    auto& feet = asset.feet;
    feet.traceUp = std::clamp(feet.traceUp, 0.0f, 10.0f);
    feet.traceDown = std::clamp(feet.traceDown, 0.0f, 10.0f);
    feet.footHeight = std::clamp(feet.footHeight, -1.0f, 1.0f);
    feet.pelvisWeight = std::clamp(feet.pelvisWeight, 0.0f, 1.0f);
    feet.maxPelvisDrop = std::clamp(feet.maxPelvisDrop, 0.0f, 5.0f);
    feet.weight = std::clamp(feet.weight, 0.0f, 1.0f);
    for (std::size_t i = 0; i < asset.goals.size(); ++i) {
        auto& goal = asset.goals[i];
        if (goal.name.empty()) goal.name = "IKGoal" + std::to_string(i + 1);
        goal.type = static_cast<IKGoalType>(std::clamp(static_cast<int>(goal.type), 0, 3));
        if (!Finite(goal.targetOffset)) goal.targetOffset = {0.0f, 1.0f, -1.0f};
        if (!Finite(goal.poleOffset)) goal.poleOffset = {0.0f, 0.0f, 1.0f};
        if (!Finite(goal.forwardAxis) || glm::dot(goal.forwardAxis, goal.forwardAxis) < 0.000001f)
            goal.forwardAxis = {0.0f, 0.0f, 1.0f};
        else goal.forwardAxis = glm::normalize(goal.forwardAxis);
        goal.targetOffset = glm::clamp(goal.targetOffset, glm::vec3(-10000.0f), glm::vec3(10000.0f));
        goal.poleOffset = glm::clamp(goal.poleOffset, glm::vec3(-10000.0f), glm::vec3(10000.0f));
        goal.weight = std::clamp(goal.weight, 0.0f, 1.0f);
        goal.maxAngleDegrees = std::clamp(goal.maxAngleDegrees, 0.0f, 180.0f);
        goal.interpolationSpeed = std::clamp(goal.interpolationSpeed, 0.0f, 1000.0f);
    }
}

bool ValidateIKRigAsset(const IKRigAssetData& asset, std::string* error) {
    if (asset.name.empty()) { Error(error, "IK rig needs a name."); return false; }
    if (!asset.skeletonId.Valid() && asset.skeletonPath.empty()) {
        Error(error, "IK rig needs a skeleton."); return false;
    }
    for (const auto& goal : asset.goals) {
        if (goal.name.empty() || goal.rootBone.empty()) {
            Error(error, "Every IK goal needs a name and root bone."); return false;
        }
        if (goal.type == IKGoalType::TwoBone
            && (goal.midBone.empty() || goal.endBone.empty())) {
            Error(error, "Two-bone goals need root, mid, and end bones."); return false;
        }
    }
    return true;
}

bool SaveIKRigAsset(const std::string& path, IKRigAssetData asset, std::string* error) {
    NormalizeIKRigAsset(asset);
    if (!ValidateIKRigAsset(asset, error)) return false;
    asset.header.type = AssetType::IKRig;
    asset.header.assetVersion = kIKRigAssetVersion;
    if (!asset.header.id.Valid()) asset.header.id = AssetHandle::Generate();
    asset.header.dependencies.clear();
    if (asset.skeletonId.Valid()) asset.header.dependencies.push_back(asset.skeletonId);
    std::error_code ec; std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    std::ofstream out(path);
    if (!out) { Error(error, "Could not create IK rig asset: " + path); return false; }
    out << "3DG_IK_RIG " << kIKRigAssetVersion << ' ' << asset.header.id.ToString() << '\n';
    out << "ASSET_DEPS " << asset.header.dependencies.size();
    for (const auto id : asset.header.dependencies) out << ' ' << id.ToString();
    out << '\n' << std::quoted(asset.name) << ' ' << std::quoted(asset.skeletonPath) << ' '
        << (asset.skeletonId.Valid() ? asset.skeletonId.ToString() : std::string("-")) << '\n';
    const auto& feet = asset.feet;
    out << feet.enabled << ' ' << std::quoted(feet.pelvisBone) << ' '
        << std::quoted(feet.leftUpperBone) << ' ' << std::quoted(feet.leftMidBone) << ' '
        << std::quoted(feet.leftFootBone) << ' ' << std::quoted(feet.rightUpperBone) << ' '
        << std::quoted(feet.rightMidBone) << ' ' << std::quoted(feet.rightFootBone) << '\n';
    out << feet.traceUp << ' ' << feet.traceDown << ' ' << feet.footHeight << ' '
        << feet.pelvisWeight << ' ' << feet.maxPelvisDrop << ' ' << feet.weight << '\n';
    out << asset.goals.size() << '\n';
    for (const auto& goal : asset.goals) {
        out << std::quoted(goal.name) << ' ' << static_cast<int>(goal.type) << ' ' << goal.enabled << ' '
            << std::quoted(goal.rootBone) << ' ' << std::quoted(goal.midBone) << ' '
            << std::quoted(goal.endBone) << '\n';
        out << goal.targetOffset.x << ' ' << goal.targetOffset.y << ' ' << goal.targetOffset.z << ' '
            << goal.poleOffset.x << ' ' << goal.poleOffset.y << ' ' << goal.poleOffset.z << ' '
            << goal.forwardAxis.x << ' ' << goal.forwardAxis.y << ' ' << goal.forwardAxis.z << ' '
            << goal.weight << ' ' << goal.maxAngleDegrees << ' ' << goal.interpolationSpeed << '\n';
    }
    if (!out) { Error(error, "Failed while writing IK rig asset."); return false; }
    return true;
}

bool LoadIKRigAsset(const std::string& path, IKRigAssetData* output, std::string* error) {
    if (!output) { Error(error, "IK rig output is null."); return false; }
    std::ifstream in(path); if (!in) { Error(error, "Could not open IK rig asset: " + path); return false; }
    IKRigAssetData asset; std::string magic, text; std::uint32_t version = 0;
    if (!(in >> magic >> version >> text) || magic != "3DG_IK_RIG" || version != kIKRigAssetVersion
        || !AssetHandle::Parse(text, &asset.header.id)) {
        Error(error, "Invalid IK rig asset header."); return false;
    }
    std::size_t dependencyCount = 0; in >> magic >> dependencyCount;
    if (magic != "ASSET_DEPS" || dependencyCount > 16) { Error(error, "Invalid IK rig dependencies."); return false; }
    asset.header.dependencies.resize(dependencyCount);
    for (auto& id : asset.header.dependencies) { in >> text; if (!AssetHandle::Parse(text, &id)) in.setstate(std::ios::failbit); }
    in >> std::quoted(asset.name) >> std::quoted(asset.skeletonPath) >> text;
    if (text != "-" && !AssetHandle::Parse(text, &asset.skeletonId)) in.setstate(std::ios::failbit);
    auto& feet = asset.feet;
    in >> feet.enabled >> std::quoted(feet.pelvisBone)
       >> std::quoted(feet.leftUpperBone) >> std::quoted(feet.leftMidBone) >> std::quoted(feet.leftFootBone)
       >> std::quoted(feet.rightUpperBone) >> std::quoted(feet.rightMidBone) >> std::quoted(feet.rightFootBone);
    in >> feet.traceUp >> feet.traceDown >> feet.footHeight >> feet.pelvisWeight
       >> feet.maxPelvisDrop >> feet.weight;
    std::size_t goalCount = 0; in >> goalCount;
    if (goalCount > 128) { Error(error, "IK rig contains too many goals."); return false; }
    asset.goals.resize(goalCount);
    for (auto& goal : asset.goals) {
        int type = 0;
        in >> std::quoted(goal.name) >> type >> goal.enabled >> std::quoted(goal.rootBone)
           >> std::quoted(goal.midBone) >> std::quoted(goal.endBone);
        goal.type = static_cast<IKGoalType>(type);
        in >> goal.targetOffset.x >> goal.targetOffset.y >> goal.targetOffset.z
           >> goal.poleOffset.x >> goal.poleOffset.y >> goal.poleOffset.z
           >> goal.forwardAxis.x >> goal.forwardAxis.y >> goal.forwardAxis.z
           >> goal.weight >> goal.maxAngleDegrees >> goal.interpolationSpeed;
    }
    if (!in) { Error(error, "IK rig asset is truncated or corrupt."); return false; }
    asset.header.type = AssetType::IKRig; asset.header.assetVersion = version;
    NormalizeIKRigAsset(asset);
    if (!ValidateIKRigAsset(asset, error)) return false;
    *output = std::move(asset); return true;
}

} // namespace engine
