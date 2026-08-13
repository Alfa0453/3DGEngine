#pragma once

#include "engine/assets/AssetIdentity.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <functional>
#include <string>
#include <vector>

namespace engine {

struct BiomeLayerRule {
    std::string name = "Ground";
    std::string materialPath;
    AssetHandle materialId;
    float heightMin = 0.0f, heightMax = 1.0f;
    float slopeMinDegrees = 0.0f, slopeMaxDegrees = 90.0f;
    float moistureMin = 0.0f, moistureMax = 1.0f;
    float temperatureMin = 0.0f, temperatureMax = 1.0f;
};

struct BiomeFoliageRule {
    std::string name = "Foliage";
    std::string meshPath;
    AssetHandle meshId;
    float weight = 1.0f;
    float density = 0.02f;
    glm::vec2 scaleRange{0.8f, 1.2f};
    float heightMin = 0.0f, heightMax = 1.0f;
    float slopeMinDegrees = 0.0f, slopeMaxDegrees = 45.0f;
    float moistureMin = 0.0f, moistureMax = 1.0f;
    float temperatureMin = 0.0f, temperatureMax = 1.0f;
    bool alignToSurface = true;
    bool castShadows = true;
};

struct BiomeAssetData {
    int version = 1;
    AssetHandle assetId;
    std::string name = "New Biome";
    unsigned seed = 1337;
    float previewWorldSize = 64.0f;
    int maximumInstances = 12000;
    float transitionDistance = 4.0f;
    float moisture = 0.5f;
    float temperature = 0.5f;
    std::vector<BiomeLayerRule> layers;
    std::vector<BiomeFoliageRule> foliage;
    std::string weatherPath;
    AssetHandle weatherId;
    bool waterEnabled = false;
    float waterLevel = 0.0f;
    std::string waterMaterialPath;
    AssetHandle waterMaterialId;
    std::string particlePath;
    AssetHandle particleId;
    std::string ambientAudioPath;
    AssetHandle ambientAudioId;
};

struct BiomeSurfaceSample {
    float height = 0.0f;
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    float normalizedHeight = 0.0f;
    float moisture = 0.5f;
    float temperature = 0.5f;
};

struct BiomePlacement {
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};
    std::string meshPath;
    AssetHandle meshId;
    std::size_t foliageRule = 0;
};

using BiomeSurfaceSampler = std::function<BiomeSurfaceSample(float x, float z)>;

void NormalizeBiome(BiomeAssetData& biome);
bool ValidateBiome(const BiomeAssetData& biome, std::string* error = nullptr);
std::vector<BiomePlacement> EvaluateBiome(
    const BiomeAssetData& biome, const BiomeSurfaceSampler& surface,
    const glm::vec3& worldOffset = glm::vec3(0.0f), unsigned seedOverride = 0);
bool SaveBiomeAsset(const std::string& path, BiomeAssetData& biome,
                    std::string* error = nullptr);
bool LoadBiomeAsset(const std::string& path, BiomeAssetData* biome,
                    std::string* error = nullptr);

} // namespace engine
