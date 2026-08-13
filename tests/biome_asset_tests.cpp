#include <engine/assets/BiomeAsset.h>

#include <filesystem>
#include <iostream>

namespace {
bool Require(bool value, const char* message) {
    if (!value) std::cerr << "FAILED: " << message << '\n';
    return value;
}
}

int main() {
    engine::BiomeAssetData biome;
    biome.name = "Forest";
    biome.previewWorldSize = 20.0f;
    biome.maximumInstances = 200;
    engine::BiomeLayerRule layer;
    layer.materialPath = "Content/Forest.3dgmat";
    layer.materialId = engine::AssetHandle::Generate();
    biome.layers.push_back(layer);
    engine::BiomeFoliageRule trees;
    trees.meshPath = "Content/Tree.3dgmesh";
    trees.meshId = engine::AssetHandle::Generate();
    trees.density = 0.08f;
    trees.scaleRange = {0.8f, 1.4f};
    biome.foliage.push_back(trees);

    const auto surface = [](float x, float) {
        engine::BiomeSurfaceSample sample;
        sample.height = 2.0f;
        sample.normalizedHeight = x < 0.0f ? 0.25f : 0.75f;
        return sample;
    };
    const auto a = engine::EvaluateBiome(biome, surface);
    const auto b = engine::EvaluateBiome(biome, surface);
    if (!Require(!a.empty(), "biome generated no deterministic placements")) return 1;
    if (!Require(a.size() == b.size(), "same seed changed placement count")) return 1;
    if (!Require(a.front().position == b.front().position, "same seed changed placement position")) return 1;
    const auto c = engine::EvaluateBiome(biome, surface, {}, 42);
    if (!Require(c.empty() || c.front().position != a.front().position,
                 "seed override did not change placement")) return 1;

    biome.foliage[0].heightMax = 0.4f;
    const auto filtered = engine::EvaluateBiome(biome, surface);
    for (const auto& placement : filtered)
        if (!Require(placement.position.x < 0.0f, "height filter accepted invalid sample")) return 1;

    const std::filesystem::path path = std::filesystem::temp_directory_path()
        / "3dg_biome_asset_test.3dgbiome";
    std::string error;
    if (!Require(engine::SaveBiomeAsset(path.string(), biome, &error), "biome save failed")) return 1;
    engine::BiomeAssetData loaded;
    if (!Require(engine::LoadBiomeAsset(path.string(), &loaded, &error), "biome load failed")) return 1;
    if (!Require(loaded.assetId.Valid(), "saved biome has no stable ID")) return 1;
    if (!Require(loaded.layers.size() == 1 && loaded.foliage.size() == 1,
                 "biome rules did not round trip")) return 1;
    if (!Require(loaded.layers[0].materialId == layer.materialId
                 && loaded.foliage[0].meshId == trees.meshId,
                 "biome dependencies did not round trip")) return 1;
    std::error_code ec; std::filesystem::remove(path, ec);
    return 0;
}
