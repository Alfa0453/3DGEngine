#pragma once

#include "engine/assets/AssetIdentity.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace engine {

inline constexpr std::uint32_t kFoliageAssetVersion = 2;

// One paintable entry in a foliage palette. Placement instances live in the
// scene component; the asset owns reusable mesh, material, and placement rules.
struct FoliageTypeAsset {
    std::string name = "Foliage Type";
    std::string meshPath;
    AssetHandle meshId;
    std::string materialPath;
    AssetHandle materialId;
    // Optional replacement meshes selected by camera distance. Missing LODs
    // gracefully fall back to the primary mesh.
    std::string lod1MeshPath;
    AssetHandle lod1MeshId;
    std::string lod2MeshPath;
    AssetHandle lod2MeshId;

    float density = 1.0f;
    glm::vec3 minScale{0.85f};
    glm::vec3 maxScale{1.15f};
    glm::vec3 minRotation{0.0f};
    glm::vec3 maxRotation{0.0f, 360.0f, 0.0f};
    float minimumSpacing = 0.5f;
    float minimumSlopeDegrees = 0.0f;
    float maximumSlopeDegrees = 50.0f;
    float minimumWorldHeight = -100000.0f;
    float maximumWorldHeight = 100000.0f;
    float cullStartDistance = 80.0f;
    float cullEndDistance = 120.0f;
    float lod1Distance = 35.0f;
    float lod2Distance = 75.0f;
    float windStrength = 0.0f;
    bool alignToSurface = true;
    bool randomYaw = true;
    bool castShadows = true;
    bool collisionEnabled = false;
};

struct FoliageAssetData {
    NativeAssetHeader header;
    std::string name = "Foliage";
    std::vector<FoliageTypeAsset> types;
};

bool ValidateFoliageAsset(const FoliageAssetData& asset,
                          std::string* error = nullptr);
bool SaveFoliageAsset(const std::string& path, FoliageAssetData asset,
                      std::string* error = nullptr);
bool LoadFoliageAsset(const std::string& path, FoliageAssetData* asset,
                      std::string* error = nullptr);

} // namespace engine
