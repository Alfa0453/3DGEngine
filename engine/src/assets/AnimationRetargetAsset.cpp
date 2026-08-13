#include "engine/assets/AnimationRetargetAsset.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <unordered_map>

namespace engine {
namespace {
void SetError(std::string* error, const std::string& message) {
    if (error) *error = message;
}

std::string LeafName(std::string name) {
    const std::size_t split = name.find_last_of("|:/\\");
    if (split != std::string::npos) name.erase(0, split + 1);
    return name;
}
}

std::string NormalizeRetargetBoneName(const std::string& value) {
    std::string name = LeafName(value);
    std::string result;
    result.reserve(name.size());
    for (unsigned char c : name)
        if (std::isalnum(c)) result.push_back(static_cast<char>(std::tolower(c)));
    static const char* prefixes[] = {"mixamorig", "bip001", "bip01", "armature"};
    for (const char* prefix : prefixes) {
        const std::string p(prefix);
        if (result.rfind(p, 0) == 0) { result.erase(0, p.size()); break; }
    }
    return result;
}

std::vector<RetargetBoneMapping> BuildAutomaticRetargetMap(
    const Skeleton& source, const Skeleton& target) {
    std::unordered_multimap<std::string, int> targets;
    for (std::size_t i = 0; i < target.bones.size(); ++i)
        targets.emplace(NormalizeRetargetBoneName(target.bones[i].name),
                        static_cast<int>(i));
    std::vector<RetargetBoneMapping> result;
    for (const Bone& sourceBone : source.bones) {
        const std::string key = NormalizeRetargetBoneName(sourceBone.name);
        auto range = targets.equal_range(key);
        if (range.first == range.second) continue;
        int chosen = range.first->second;
        RetargetBoneMapping mapping;
        mapping.sourceBone = sourceBone.name;
        mapping.targetBone = target.bones[static_cast<std::size_t>(chosen)].name;
        mapping.transferTranslation = sourceBone.parent < 0;
        result.push_back(std::move(mapping));
    }
    return result;
}

bool RetargetAnimationClip(const Skeleton& sourceSkeleton,
                           const Animation& sourceAnimation,
                           const Skeleton& targetSkeleton,
                           const AnimationRetargetAssetData& profile,
                           Animation* output, std::string* error) {
    if (!output || sourceSkeleton.bones.empty() || targetSkeleton.bones.empty()) {
        SetError(error, "Retargeting requires source and target skeletons.");
        return false;
    }
    Animation result;
    result.name = sourceAnimation.name;
    result.duration = sourceAnimation.duration;
    result.ticksPerSecond = sourceAnimation.ticksPerSecond;
    result.channels.resize(targetSkeleton.bones.size());
    for (const RetargetBoneMapping& mapping : profile.mappings) {
        const int sourceIndex = sourceSkeleton.Find(mapping.sourceBone);
        const int targetIndex = targetSkeleton.Find(mapping.targetBone);
        if (sourceIndex < 0 || targetIndex < 0
            || static_cast<std::size_t>(sourceIndex) >= sourceAnimation.channels.size())
            continue;
        const BoneChannel& source = sourceAnimation.channels[static_cast<std::size_t>(sourceIndex)];
        BoneChannel& target = result.channels[static_cast<std::size_t>(targetIndex)];
        target.scales = source.scales;
        target.rotations = source.rotations;
        const glm::quat correction = glm::quat(glm::radians(mapping.rotationOffsetDegrees));
        for (QuatKey& key : target.rotations)
            key.value = glm::normalize(correction * key.value);
        const bool isRoot = mapping.sourceBone == profile.sourceRootBone
                         || sourceSkeleton.bones[static_cast<std::size_t>(sourceIndex)].parent < 0;
        if (mapping.transferTranslation && (!isRoot || profile.transferRootMotion)) {
            target.positions = source.positions;
            const float scale = std::max(profile.globalScale, 0.0001f)
                              * std::max(mapping.translationScale, 0.0001f);
            for (VecKey& key : target.positions) key.value *= scale;
        }
    }
    *output = std::move(result);
    SetError(error, {});
    return true;
}

bool SaveAnimationRetargetAsset(const std::string& path,
                                AnimationRetargetAssetData& asset,
                                std::string* error) {
    if (!asset.assetId.Valid()) asset.assetId = AssetHandle::Generate();
    std::error_code ec;
    const std::filesystem::path p(path);
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
    std::ofstream out(path, std::ios::trunc);
    if (!out) { SetError(error, "Could not write retarget profile: " + path); return false; }
    out << "3DG_RETARGET 1 " << asset.assetId.ToString() << '\n'
        << std::quoted(asset.name) << ' '
        << std::quoted(asset.sourceSkeletonPath) << ' '
        << std::quoted(asset.targetSkeletonPath) << '\n'
        << (asset.sourceSkeletonId.Valid() ? asset.sourceSkeletonId.ToString() : "-") << ' '
        << (asset.targetSkeletonId.Valid() ? asset.targetSkeletonId.ToString() : "-") << '\n'
        << std::quoted(asset.sourceRootBone) << ' ' << std::quoted(asset.targetRootBone) << ' '
        << (asset.transferRootMotion ? 1 : 0) << ' ' << asset.globalScale << '\n'
        << "MAPPINGS " << asset.mappings.size() << '\n';
    for (const RetargetBoneMapping& mapping : asset.mappings)
        out << std::quoted(mapping.sourceBone) << ' ' << std::quoted(mapping.targetBone) << ' '
            << mapping.rotationOffsetDegrees.x << ' ' << mapping.rotationOffsetDegrees.y << ' '
            << mapping.rotationOffsetDegrees.z << ' ' << mapping.translationScale << ' '
            << (mapping.transferTranslation ? 1 : 0) << '\n';
    const int deps = (asset.sourceSkeletonId.Valid() ? 1 : 0)
                   + (asset.targetSkeletonId.Valid() ? 1 : 0);
    out << "ASSET_DEPS " << deps;
    if (asset.sourceSkeletonId.Valid()) out << ' ' << asset.sourceSkeletonId.ToString();
    if (asset.targetSkeletonId.Valid()) out << ' ' << asset.targetSkeletonId.ToString();
    out << '\n';
    SetError(error, {});
    return static_cast<bool>(out);
}

bool LoadAnimationRetargetAsset(const std::string& path,
                                AnimationRetargetAssetData* asset,
                                std::string* error) {
    if (!asset) { SetError(error, "Retarget profile output is null."); return false; }
    std::ifstream in(path);
    std::string magic, id, tag;
    int version = 0, rootMotion = 1;
    AnimationRetargetAssetData loaded;
    if (!(in >> magic >> version >> id) || magic != "3DG_RETARGET" || version != 1
        || !AssetHandle::Parse(id, &loaded.assetId)) {
        SetError(error, "Invalid retarget profile: " + path); return false;
    }
    in >> std::quoted(loaded.name) >> std::quoted(loaded.sourceSkeletonPath)
       >> std::quoted(loaded.targetSkeletonPath);
    std::string sourceId, targetId;
    in >> sourceId >> targetId;
    if ((sourceId != "-" && !AssetHandle::Parse(sourceId, &loaded.sourceSkeletonId))
        || (targetId != "-" && !AssetHandle::Parse(targetId, &loaded.targetSkeletonId))) {
        SetError(error, "Retarget skeleton identity is invalid."); return false;
    }
    in >> std::quoted(loaded.sourceRootBone) >> std::quoted(loaded.targetRootBone)
       >> rootMotion >> loaded.globalScale >> tag;
    std::size_t count = 0;
    in >> count;
    if (!in || tag != "MAPPINGS" || count > 4096) {
        SetError(error, "Retarget mapping data is invalid."); return false;
    }
    loaded.transferRootMotion = rootMotion != 0;
    loaded.mappings.resize(count);
    for (RetargetBoneMapping& mapping : loaded.mappings) {
        int transfer = 0;
        in >> std::quoted(mapping.sourceBone) >> std::quoted(mapping.targetBone)
           >> mapping.rotationOffsetDegrees.x >> mapping.rotationOffsetDegrees.y
           >> mapping.rotationOffsetDegrees.z >> mapping.translationScale >> transfer;
        mapping.transferTranslation = transfer != 0;
    }
    if (!in) { SetError(error, "Retarget profile is incomplete."); return false; }
    *asset = std::move(loaded);
    SetError(error, {});
    return true;
}

} // namespace engine
