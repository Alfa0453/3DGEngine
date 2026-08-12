#pragma once

#include "engine/assets/AssetIdentity.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace engine {

inline constexpr std::uint32_t kTerrainAssetVersion = 1;

struct TerrainAssetData {
    NativeAssetHeader header;
    std::string name = "Landscape";
    int resolution = 128;
    float size = 64.0f;
    float maxHeight = 8.0f;
    unsigned seed = 1337;
    int octaves = 5;
    float frequency = 2.0f;
    std::vector<float> heights;
    std::vector<std::uint8_t> paint;
    // Index zero is automatic terrain. Slots 1..5 are manual paint layers.
    std::string layerMaterials[6];
    bool grassEnabled = false;
    float grassDensity = 2.0f;
    float grassHeight = 0.6f;
    bool grassRandomizeHeight = false;
    float grassMinHeightScale = 0.75f;
    float grassMaxHeightScale = 1.25f;
    float grassWindStrength = 0.18f;
    float grassWindSpeed = 1.4f;
    glm::vec3 grassBaseColor{0.16f, 0.34f, 0.12f};
    glm::vec3 grassTipColor{0.42f, 0.68f, 0.28f};
};

bool ValidateTerrainAsset(const TerrainAssetData& asset, std::string* error = nullptr);
bool SaveTerrainAsset(const std::string& path, TerrainAssetData asset,
                      std::string* error = nullptr);
bool LoadTerrainAsset(const std::string& path, TerrainAssetData* asset,
                      std::string* error = nullptr);

} // namespace engine
