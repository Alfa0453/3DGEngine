#include <engine/assets/AnimationRetargetAsset.h>
#include <engine/assets/AssetRegistry.h>

#include <cmath>
#include <filesystem>
#include <iostream>

namespace {
int failures = 0;
void Check(bool value, const char* message) {
    if (!value) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}

engine::Skeleton SourceSkeleton() {
    engine::Skeleton s;
    s.bones.push_back({"mixamorig:Hips", -1});
    s.bones.push_back({"mixamorig:Spine", 0});
    s.bones.push_back({"mixamorig:Head", 1});
    return s;
}
engine::Skeleton TargetSkeleton() {
    engine::Skeleton s;
    s.bones.push_back({"Root", -1});
    s.bones.push_back({"Spine", 0});
    s.bones.push_back({"Head", 1});
    return s;
}
}

int main() {
    const engine::Skeleton source = SourceSkeleton();
    const engine::Skeleton target = TargetSkeleton();
    const auto automatic = engine::BuildAutomaticRetargetMap(source, target);
    Check(automatic.size() == 2, "normalized mapping finds spine and head");
    Check(engine::NormalizeRetargetBoneName("mixamorig:Spine") == "spine",
          "namespace and rig prefix are normalized");

    engine::AnimationRetargetAssetData profile;
    profile.name = "WizardToKnight";
    profile.sourceSkeletonPath = "GameAssets/Source.3dgskel";
    profile.targetSkeletonPath = "GameAssets/Target.3dgskel";
    profile.sourceSkeletonId = engine::AssetHandle::Generate();
    profile.targetSkeletonId = engine::AssetHandle::Generate();
    profile.sourceRootBone = "mixamorig:Hips";
    profile.targetRootBone = "Root";
    profile.mappings = automatic;
    engine::RetargetBoneMapping root;
    root.sourceBone = "mixamorig:Hips";
    root.targetBone = "Root";
    root.transferTranslation = true;
    root.translationScale = 2.0f;
    profile.mappings.insert(profile.mappings.begin(), root);

    engine::Animation animation;
    animation.name = "Walk";
    animation.duration = 1.0f;
    animation.ticksPerSecond = 1.0f;
    animation.channels.resize(source.Count());
    animation.channels[0].positions.push_back({0.0f, {1.0f, 0.0f, 0.0f}});
    animation.channels[1].rotations.push_back({0.0f, {1.0f, 0.0f, 0.0f, 0.0f}});
    engine::Animation retargeted;
    std::string error;
    Check(engine::RetargetAnimationClip(source, animation, target, profile,
                                        &retargeted, &error),
          "animation retarget succeeds");
    Check(retargeted.channels.size() == target.Count(),
          "retarget output follows target skeleton ordering");
    Check(std::abs(retargeted.channels[0].positions[0].value.x - 2.0f) < 0.001f,
          "root translation scale is applied");

    const auto content = std::filesystem::temp_directory_path() / "3dg_retarget_test_content";
    const auto path = content / "Profiles" / "test.3dgretarget";
    Check(engine::SaveAnimationRetargetAsset(path.string(), profile, &error),
          "retarget profile saves");
    engine::AnimationRetargetAssetData loaded;
    Check(engine::LoadAnimationRetargetAsset(path.string(), &loaded, &error),
          "retarget profile loads");
    Check(loaded.mappings.size() == profile.mappings.size(),
          "retarget mappings survive round trip");
    Check(loaded.sourceSkeletonId == profile.sourceSkeletonId
          && loaded.targetSkeletonId == profile.targetSkeletonId,
          "retarget dependencies survive round trip");
    engine::AssetRegistry registry;
    Check(registry.RebuildFromContent(content.string(), &error),
          "asset registry scans retarget profiles");
    const engine::AssetRegistryEntry* entry = registry.Find(profile.assetId);
    Check(entry && entry->type == engine::AssetType::AnimationRetarget,
          "retarget profile has the correct registry type");
    Check(entry && entry->dependencies.size() == 2,
          "registry records source and target skeleton dependencies");
    std::error_code ec; std::filesystem::remove_all(content, ec);
    if (failures) return 1;
    std::cout << "animation retarget tests passed\n";
    return 0;
}
