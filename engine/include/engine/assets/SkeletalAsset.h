#pragma once

#include "engine/animation/Skeleton.h"
#include "engine/assets/AssetIdentity.h"
#include "engine/assets/StaticMeshAsset.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace engine {

class AssetRegistry;

inline constexpr std::uint32_t kSkeletonAssetVersion = 1;
inline constexpr std::uint32_t kAnimationAssetVersion = 1;
inline constexpr std::uint32_t kSkeletalMeshAssetVersion = 1;
inline constexpr std::uint32_t kSkeletalImporterVersion = 1;
inline constexpr std::uint32_t kSkeletalMeshVertexStride = 16;

struct SkeletonAssetData {
    NativeAssetHeader header;
    Skeleton skeleton;
};

struct NamedAnimationClipData {
    Animation animation;
    // Parallel to animation.channels. Names make a .3dganim portable across
    // compatible skeletons whose bone ordering differs.
    std::vector<std::string> channelBoneNames;
};

struct AnimationAssetData {
    NativeAssetHeader header;
    AssetHandle skeletonId;
    std::vector<NamedAnimationClipData> clips;
};

struct SkeletalMeshSubMeshData {
    std::int32_t material = -1;
    // position3 / normal3 / uv2 / boneIds4 / weights4
    std::vector<float> vertices;
    std::vector<std::uint32_t> indices;
};

struct SkeletalMeshAssetData {
    NativeAssetHeader header;
    AssetHandle skeletonId;
    Skeleton skeleton;
    std::vector<NamedAnimationClipData> embeddedAnimations;
    std::array<float, 3> minimum{{0.0f, 0.0f, 0.0f}};
    std::array<float, 3> maximum{{0.0f, 0.0f, 0.0f}};
    std::vector<StaticMeshMaterialData> materials;
    std::vector<StaticMeshTextureData> textures;
    std::vector<SkeletalMeshSubMeshData> subMeshes;
};

struct SkeletalImportOptions {
    float uniformScale = 1.0f;
    bool generateSmoothNormals = true;
    bool joinIdenticalVertices = true;
    bool flipUVs = false;
    bool importEmbeddedAnimations = true;
};

struct SkeletalImportResult {
    AssetHandle skeletalMeshId;
    AssetHandle skeletonId;
    std::string skeletalMeshPath;
    std::string skeletonPath;
    std::vector<AssetHandle> animationIds;
    std::vector<std::string> animationPaths;
    std::uint64_t sourceHash = 0;
    std::size_t boneCount = 0;
    std::size_t vertexCount = 0;
    std::size_t triangleCount = 0;
};

struct ModelSourceInfo {
    bool hasBones = false;
    std::size_t meshCount = 0;
    std::size_t animationCount = 0;

    bool IsSkeletal() const { return hasBones || animationCount > 0; }
};

// Lightweight source inspection used by the editor import settings window.
// It does not create assets or modify the source file.
bool InspectModelSource(const std::string& sourcePath, ModelSourceInfo* info,
                        std::string* error = nullptr);

bool SaveSkeletonAsset(const std::string& path, SkeletonAssetData asset,
                       std::string* error = nullptr);
bool LoadSkeletonAsset(const std::string& path, SkeletonAssetData* asset,
                       std::string* error = nullptr);
bool SaveAnimationAsset(const std::string& path, AnimationAssetData asset,
                        std::string* error = nullptr);
bool LoadAnimationAsset(const std::string& path, AnimationAssetData* asset,
                        std::string* error = nullptr);
bool SaveSkeletalMeshAsset(const std::string& path, SkeletalMeshAssetData asset,
                           std::string* error = nullptr);
bool LoadSkeletalMeshAsset(const std::string& path, SkeletalMeshAssetData* asset,
                           std::string* error = nullptr);

bool ImportSkeletalSource(const std::string& sourcePath,
                          const SkeletalImportOptions& options,
                          SkeletalMeshAssetData* mesh,
                          SkeletonAssetData* skeleton,
                          std::vector<AnimationAssetData>* animations,
                          SkeletalImportResult* result = nullptr,
                          std::string* error = nullptr);

// destinationBase is a path without an extension. The importer writes
// destinationBase.3dgskmesh, destinationBase.3dgskel, and one .3dganim per clip.
bool ImportSkeletalAssetsToContent(
    const std::string& sourcePath, const std::string& destinationBase,
    const std::string& contentRoot, const SkeletalImportOptions& options,
    AssetRegistry* registry, SkeletalImportResult* result = nullptr,
    std::string* error = nullptr);

} // namespace engine
