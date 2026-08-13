#pragma once

#include "engine/assets/AssetIdentity.h"
#include "engine/assets/StaticMeshAsset.h"

#include <glm/glm.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace engine {
inline constexpr std::uint32_t kCaveAssetVersion = 1;

struct CaveChamber { int pointIndex = 0; float radiusScale = 2.0f; float lengthScale = 1.5f; };
struct CaveAssetData {
    NativeAssetHeader header;
    std::string name = "Cave";
    std::vector<glm::vec3> points;
    bool closed = false;
    float width = 6.0f;
    float height = 4.0f;
    float wallThickness = 0.35f;
    float sampleSpacing = 1.0f;
    int radialSegments = 16;
    bool endCaps = false;
    bool createCollision = true;
    bool createNavigation = true;
    bool terrainEntrances = true;
    std::string wallMaterialPath, floorMaterialPath, ceilingMaterialPath, trimMaterialPath;
    AssetHandle wallMaterialId, floorMaterialId, ceilingMaterialId, trimMaterialId;
    std::string bakedMeshPath;
    AssetHandle bakedMeshId;
    std::vector<CaveChamber> chambers;
};

struct CaveGenerationStats { std::size_t vertices=0, triangles=0, rings=0; float length=0.0f; };
void NormalizeCaveAsset(CaveAssetData& cave);
bool ValidateCaveAsset(const CaveAssetData& cave, std::string* error=nullptr);
bool BuildCaveStaticMesh(const CaveAssetData& cave, StaticMeshAssetData* mesh,
                         CaveGenerationStats* stats=nullptr, std::string* error=nullptr);
bool SaveCaveAsset(const std::string& path, CaveAssetData cave, std::string* error=nullptr);
bool LoadCaveAsset(const std::string& path, CaveAssetData* cave, std::string* error=nullptr);
}
