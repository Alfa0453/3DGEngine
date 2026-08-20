#pragma once

#include "engine/assets/AssetIdentity.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace engine {

inline constexpr std::uint32_t kFenceWallAssetVersion = 1;

enum class FenceWallMode : std::uint8_t { Fence = 0, Wall = 1 };
enum class FencePartKind : std::uint8_t { Panel = 0, Post = 1, Gate = 2 };

struct FenceOpening {
    int segmentIndex = 0;
    float centerDistance = 1.5f;
    float width = 1.2f;
    bool gate = true;
};

struct FenceWallAssetData {
    NativeAssetHeader header;
    std::string name = "Fence";
    FenceWallMode mode = FenceWallMode::Fence;
    std::vector<glm::vec3> points;
    bool closed = false;

    float height = 1.8f;
    float thickness = 0.12f;
    float panelLength = 2.0f;
    float postSpacing = 2.0f;
    float postWidth = 0.16f;
    float postHeightExtra = 0.15f;
    float gateHeight = 1.7f;
    bool createPosts = true;
    bool createCollision = true;
    bool followSlope = true;
    bool snapToGrid = true;
    float gridSize = 0.25f;

    std::string panelMeshPath;
    std::string postMeshPath;
    std::string gateMeshPath;
    std::string panelMaterialPath;
    std::string postMaterialPath;
    std::string gateMaterialPath;
    AssetHandle panelMeshId;
    AssetHandle postMeshId;
    AssetHandle gateMeshId;
    AssetHandle panelMaterialId;
    AssetHandle postMaterialId;
    AssetHandle gateMaterialId;
    std::vector<FenceOpening> openings;
};

struct FencePlacement {
    FencePartKind kind = FencePartKind::Panel;
    std::string suffix;
    glm::vec3 position{0.0f};
    glm::vec3 scale{1.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    bool collision = true;
};

struct FenceGenerationStats {
    std::size_t panels = 0;
    std::size_t posts = 0;
    std::size_t gates = 0;
    float length = 0.0f;
};

void NormalizeFenceWallAsset(FenceWallAssetData& asset);
bool ValidateFenceWallAsset(const FenceWallAssetData& asset,
                            std::string* error = nullptr);
std::vector<FencePlacement> GenerateFenceWall(
    const FenceWallAssetData& asset, FenceGenerationStats* stats = nullptr,
    std::string* error = nullptr);
bool SaveFenceWallAsset(const std::string& path, FenceWallAssetData asset,
                        std::string* error = nullptr);
bool LoadFenceWallAsset(const std::string& path, FenceWallAssetData* asset,
                        std::string* error = nullptr);

} // namespace engine
