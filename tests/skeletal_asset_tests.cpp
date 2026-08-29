#include <engine/assets/AssetRegistry.h>
#include <engine/assets/SkeletalAsset.h>

#include <cmath>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>

namespace {

void Check(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
}

} // namespace

int main() {
    namespace fs = std::filesystem;
    const fs::path source =
        fs::path(THREEDG_TEST_SOURCE_DIR)
        / "wizardscene" / "assets" / "models" / "WizardSM.FBX";
    Check(fs::is_regular_file(source), "skeletal FBX regression fixture exists");

    std::string error;
    engine::SkeletalMeshAssetData mesh;
    engine::SkeletonAssetData skeleton;
    std::vector<engine::AnimationAssetData> animations;
    engine::SkeletalImportResult imported;
    if (!engine::ImportSkeletalSource(
            source.string(), {}, &mesh, &skeleton, &animations,
            &imported, &error)) {
        std::cerr << "Skeletal import error: " << error << '\n';
        Check(false, "convert a rigged FBX into native skeletal CPU data");
    }
    Check(imported.skeletalMeshId.Valid()
              && imported.skeletonId.Valid()
              && imported.skeletalMeshId != imported.skeletonId
              && imported.boneCount > 1
              && imported.boneCount <= 128
              && imported.vertexCount > 0
              && imported.triangleCount > 0
              && !mesh.subMeshes.empty()
              && mesh.skeletonId == skeleton.header.id
              && mesh.skeleton.bones.size() == skeleton.skeleton.bones.size(),
          "skeletal conversion preserves geometry, rig, bounds and identities");

    for (const engine::SkeletalMeshSubMeshData& subMesh : mesh.subMeshes) {
        Check(subMesh.vertices.size() % engine::kSkeletalMeshVertexStride == 0,
              "skinned vertex buffer uses the native 16-float layout");
        for (std::size_t offset = 0; offset < subMesh.vertices.size();
             offset += engine::kSkeletalMeshVertexStride) {
            const float sum = subMesh.vertices[offset + 12]
                + subMesh.vertices[offset + 13]
                + subMesh.vertices[offset + 14]
                + subMesh.vertices[offset + 15];
            Check(std::abs(sum - 1.0f) < 0.001f,
                  "every skinned vertex has normalized bone weights");
        }
    }

    const fs::path root = fs::temp_directory_path() / "3dg_skeletal_asset_tests";
    const fs::path content = root / "Content";
    const fs::path base = content / "Characters" / "Wizard";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(base.parent_path(), ec);

    const fs::path skeletonPath = base.string() + ".3dgskel";
    const fs::path meshPath = base.string() + ".3dgskmesh";
    Check(engine::SaveSkeletonAsset(skeletonPath.string(), skeleton, &error),
          "save native skeleton payload");
    Check(engine::SaveSkeletalMeshAsset(meshPath.string(), mesh, &error),
          "save native skeletal mesh payload");
    engine::SkeletonAssetData loadedSkeleton;
    engine::SkeletalMeshAssetData loadedMesh;
    Check(engine::LoadSkeletonAsset(
              skeletonPath.string(), &loadedSkeleton, &error)
              && engine::LoadSkeletalMeshAsset(
                  meshPath.string(), &loadedMesh, &error)
              && loadedSkeleton.header.id == skeleton.header.id
              && loadedMesh.header.id == mesh.header.id
              && loadedMesh.skeletonId == skeleton.header.id
              && loadedMesh.subMeshes[0].vertices == mesh.subMeshes[0].vertices
              && loadedMesh.skeleton.bones[0].name
                     == skeleton.skeleton.bones[0].name,
          "skeleton and skeletal mesh assets round-trip without source parsing");

    if (!animations.empty()) {
        const fs::path animationPath = base.string() + "_Take.3dganim";
        Check(engine::SaveAnimationAsset(
                  animationPath.string(), animations[0], &error),
              "save native animation payload");
        engine::AnimationAssetData loadedAnimation;
        Check(engine::LoadAnimationAsset(
                  animationPath.string(), &loadedAnimation, &error)
                  && loadedAnimation.header.id == animations[0].header.id
                  && loadedAnimation.skeletonId == skeleton.header.id
                  && loadedAnimation.clips.size() == 1
                  && loadedAnimation.clips[0].channelBoneNames.size()
                         == skeleton.skeleton.bones.size(),
              "animation asset preserves named bone channels and skeleton dependency");
    }

    fs::remove_all(content, ec);
    fs::create_directories(base.parent_path(), ec);
    engine::AssetRegistry registry;
    engine::SkeletalImportResult first;
    Check(engine::ImportSkeletalAssetsToContent(
              source.string(), base.string(), content.string(), {},
              &registry, &first, &error),
          "complete skeletal import writes native asset family and registry");
    Check(fs::is_regular_file(first.skeletalMeshPath)
              && fs::is_regular_file(first.skeletonPath)
              && registry.Find(first.skeletalMeshId)
              && registry.Find(first.skeletonId)
              && registry.Find(first.skeletalMeshId)->dependencies.size()
                     == first.animationIds.size() + 1
                        + first.assignedMaterialSlotCount
              && registry.Referencers(first.skeletonId).size()
                     == first.animationIds.size() + 1,
          "registry records skeleton and animation dependencies");
    engine::SkeletalMeshAssetData generatedSkeletalMesh;
    Check(engine::LoadSkeletalMeshAsset(
              first.skeletalMeshPath, &generatedSkeletalMesh, &error)
              && first.assignedMaterialSlotCount
                    == generatedSkeletalMesh.materialSlots.size()
              && first.importedMaterialCount
                    == generatedSkeletalMesh.materialSlots.size(),
          "skeletal import emits standalone material slots with static-mesh parity");
    std::vector<engine::AssetHandle> firstMaterialIds;
    for (const engine::MeshMaterialSlot& slot : generatedSkeletalMesh.materialSlots)
        firstMaterialIds.push_back(slot.materialId);

    const fs::path animationOnlyBase =
        content / "Animations" / "WizardMotion";
    const fs::path animationSource =
        fs::path(THREEDG_TEST_SOURCE_DIR)
        / "wizardscene" / "assets" / "anims" / "WalkForwardAnim.FBX";
    Check(fs::is_regular_file(animationSource),
          "animation FBX regression fixture exists");
    fs::create_directories(animationOnlyBase.parent_path(), ec);
    engine::SkeletalImportOptions animationOnlyOptions;
    animationOnlyOptions.importSkeletalMesh = false;
    animationOnlyOptions.importSkeleton = false;
    animationOnlyOptions.importEmbeddedAnimations = true;
    animationOnlyOptions.reuseSkeletonPath =
        fs::relative(first.skeletonPath, content).string();
    engine::SkeletalImportResult animationOnly;
    const bool importedAnimationOnly = engine::ImportSkeletalAssetsToContent(
        animationSource.string(), animationOnlyBase.string(), content.string(),
        animationOnlyOptions, &registry, &animationOnly, &error);
    if (!importedAnimationOnly)
        std::cerr << "Animation-only import error: " << error << '\n';
    Check(importedAnimationOnly,
          "animation-only import reuses a selected engine skeleton");
    Check(!animationOnly.skeletalMeshId.Valid()
              && animationOnly.skeletalMeshPath.empty()
              && animationOnly.skeletonId == first.skeletonId
              && animationOnly.skeletonPath == first.skeletonPath
              && !fs::exists(animationOnlyBase.string() + ".3dgskmesh")
              && !fs::exists(animationOnlyBase.string() + ".3dgskel")
              && !animationOnly.animationPaths.empty(),
          "animation-only import emits no duplicate mesh or skeleton assets");
    for (std::size_t i = 0; i < animationOnly.animationPaths.size(); ++i) {
        engine::AnimationAssetData loadedAnimation;
        Check(fs::is_regular_file(animationOnly.animationPaths[i])
                  && engine::LoadAnimationAsset(
                      animationOnly.animationPaths[i], &loadedAnimation, &error)
                  && loadedAnimation.skeletonId == first.skeletonId
                  && registry.Find(animationOnly.animationIds[i])
                  && registry.Find(animationOnly.animationIds[i])->dependencies
                      == std::vector<engine::AssetHandle>{first.skeletonId},
              "animation-only assets depend on the reused skeleton identity");
    }

    const fs::path reusedMeshBase = content / "Characters" / "WizardReused";
    engine::SkeletalImportOptions reusedMeshOptions;
    reusedMeshOptions.importSkeletalMesh = true;
    reusedMeshOptions.importSkeleton = false;
    reusedMeshOptions.importEmbeddedAnimations = false;
    reusedMeshOptions.reuseSkeletonPath =
        fs::relative(first.skeletonPath, content).string();
    engine::SkeletalImportResult reusedMesh;
    Check(engine::ImportSkeletalAssetsToContent(
              source.string(), reusedMeshBase.string(), content.string(),
              reusedMeshOptions, &registry, &reusedMesh, &error)
              && reusedMesh.skeletalMeshId.Valid()
              && fs::is_regular_file(reusedMesh.skeletalMeshPath)
              && !fs::exists(reusedMeshBase.string() + ".3dgskel")
              && reusedMesh.skeletonId == first.skeletonId
              && reusedMesh.animationPaths.empty()
              && registry.Find(reusedMesh.skeletalMeshId)
              && std::find(
                  registry.Find(reusedMesh.skeletalMeshId)->dependencies.begin(),
                  registry.Find(reusedMesh.skeletalMeshId)->dependencies.end(),
                  first.skeletonId)
                    != registry.Find(reusedMesh.skeletalMeshId)->dependencies.end()
              && registry.Find(reusedMesh.skeletalMeshId)->dependencies.size()
                    == reusedMesh.assignedMaterialSlotCount + 1,
          "mesh output can reuse a skeleton without duplicating skeleton or animations");

    const engine::AssetHandle meshId = first.skeletalMeshId;
    const engine::AssetHandle skeletonId = first.skeletonId;
    const std::vector<engine::AssetHandle> animationIds = first.animationIds;
    fs::path staleAnimationPath;
    engine::AssetHandle staleAnimationId;
    if (!first.animationPaths.empty()) {
        engine::AnimationAssetData staleAnimation;
        Check(engine::LoadAnimationAsset(
                  first.animationPaths[0], &staleAnimation, &error),
              "load an animation to simulate a take removed upstream");
        staleAnimation.header.id = engine::AssetHandle::Generate();
        staleAnimationPath = base.string() + "_RemovedTake.3dganim";
        Check(engine::SaveAnimationAsset(
                  staleAnimationPath.string(), staleAnimation, &error),
              "write simulated stale generated animation");
        staleAnimationId = staleAnimation.header.id;
        engine::AssetRegistryEntry staleEntry;
        staleEntry.id = staleAnimationId;
        staleEntry.type = engine::AssetType::Animation;
        staleEntry.virtualPath = "/Game/Characters/Wizard_RemovedTake.3dganim";
        staleEntry.sourcePath = source.string();
        staleEntry.dependencies = {skeletonId};
        Check(registry.Register(std::move(staleEntry), &error),
              "register simulated stale generated animation");
    }
    engine::SkeletalImportResult reimported;
    Check(engine::ImportSkeletalAssetsToContent(
              source.string(), base.string(), content.string(), {},
              &registry, &reimported, &error)
              && reimported.skeletalMeshId == meshId
              && reimported.skeletonId == skeletonId
              && reimported.animationIds == animationIds,
          "skeletal reimport preserves mesh, skeleton and animation identities");
    engine::SkeletalMeshAssetData stableMaterialsMesh;
    std::vector<engine::AssetHandle> reimportedMaterialIds;
    Check(engine::LoadSkeletalMeshAsset(
              reimported.skeletalMeshPath, &stableMaterialsMesh, &error),
          "load reimported skeletal material slots");
    for (const engine::MeshMaterialSlot& slot : stableMaterialsMesh.materialSlots)
        reimportedMaterialIds.push_back(slot.materialId);
    Check(reimportedMaterialIds == firstMaterialIds
              && reimported.reusedMaterialCount == firstMaterialIds.size(),
          "skeletal reimport preserves generated material identities");
    if (staleAnimationId.Valid())
        Check(!registry.Find(staleAnimationId)
                  && !fs::exists(staleAnimationPath),
              "reimport removes generated clips no longer present in the source");

    fs::remove_all(root, ec);
    std::cout << "skeletal asset tests passed\n";
    return 0;
}
