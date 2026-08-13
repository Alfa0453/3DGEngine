#pragma once

#include "engine/assets/AssetIdentity.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace engine {

inline constexpr std::uint32_t kScatterGraphAssetVersion = 1;

enum class ScatterNodeType : std::uint32_t {
    Region = 0,
    Density,
    HeightFilter,
    SlopeFilter,
    Transform,
    ExclusionCircle,
    MeshOutput
};

struct ScatterGraphNode {
    std::uint32_t id = 0;
    ScatterNodeType type = ScatterNodeType::Region;
    std::uint32_t input = 0;
    std::string name;
    glm::vec2 editorPosition{0.0f};
    bool enabled = true;

    // Node-specific normalized data. Unused fields are harmless and keep the
    // asset forwards-compatible without a variant-heavy serialization format.
    float density = 0.25f;
    float minimum = 0.0f;
    float maximum = 1.0f;
    glm::vec3 position{0.0f};
    float radius = 2.0f;
    glm::vec3 minimumScale{0.85f};
    glm::vec3 maximumScale{1.15f};
    float minimumYaw = 0.0f;
    float maximumYaw = 360.0f;
    bool alignToSurface = true;
    std::string meshPath;
    AssetHandle meshId;
    float weight = 1.0f;
};

struct ScatterGraphAssetData {
    NativeAssetHeader header;
    std::string name = "Scatter Graph";
    std::uint32_t seed = 1337;
    std::uint32_t maximumInstances = 10000;
    glm::vec3 regionMinimum{-10.0f, -100000.0f, -10.0f};
    glm::vec3 regionMaximum{10.0f, 100000.0f, 10.0f};
    std::vector<ScatterGraphNode> nodes;
};

struct ScatterSurfaceSample {
    float height = 0.0f;
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    float layerMask = 1.0f;
    bool valid = true;
};

struct ScatterPlacement {
    std::string meshPath;
    AssetHandle meshId;
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};
};

using ScatterSurfaceSampler =
    std::function<ScatterSurfaceSample(float worldX, float worldZ)>;

bool ValidateScatterGraphAsset(const ScatterGraphAssetData& asset,
                               std::string* error = nullptr);
bool SaveScatterGraphAsset(const std::string& path, ScatterGraphAssetData asset,
                           std::string* error = nullptr);
bool LoadScatterGraphAsset(const std::string& path, ScatterGraphAssetData* asset,
                           std::string* error = nullptr);

std::vector<ScatterPlacement> EvaluateScatterGraph(
    const ScatterGraphAssetData& asset,
    const ScatterSurfaceSampler& sampleSurface = {},
    const glm::vec3& worldOffset = glm::vec3(0.0f),
    std::uint32_t seedOverride = 0);

const char* ScatterNodeTypeName(ScatterNodeType type);

} // namespace engine
