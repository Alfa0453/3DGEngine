#include <engine/graphics/Terrain.h>
#include <engine/assets/TerrainAsset.h>

#include <cassert>
#include <cmath>
#include <iostream>
#include <filesystem>

namespace {
bool Near(float a, float b, float epsilon = 0.02f) {
    return std::abs(a - b) <= epsilon;
}

float Channel(const std::vector<unsigned char>& pixels, std::size_t pixel, int channel) {
    return pixels[pixel * 4 + static_cast<std::size_t>(channel)] / 255.0f;
}
} // namespace

int main() {
    const auto lods = engine::BuildTerrainLodIndices(129, 4);
    assert(lods.size() == 4u);
    assert(lods[0].size() / 3u == 64u * 64u * 2u);
    assert(lods[3].size() / 3u == 8u * 8u * 2u);
    for (const auto& lod : lods)
        for (std::uint32_t index : lod) assert(index < 129u * 129u);

    engine::Heightmap heightmap;
    heightmap.res = 2;
    heightmap.size = 1.0f;
    heightmap.maxHeight = 1.0f;
    heightmap.h.assign(4, 0.5f);

    engine::TerrainLayerSurface layers[6];
    engine::DefaultTerrainLayerSurfaces(layers);
    layers[2].albedo = {0.2f, 0.4f, 0.8f};
    layers[2].ao = 0.65f;
    layers[2].roughness = 0.25f;
    layers[2].metallic = 0.75f;

    std::vector<unsigned char> albedo, orm;
    engine::BuildTerrainSurfaceMaps(
        heightmap, std::vector<std::uint8_t>(4, 2), layers, albedo, orm, 4);
    assert(albedo.size() == 4u * 4u * 4u);
    assert(orm.size() == albedo.size());
    const std::size_t centre = 2u * 4u + 2u;
    assert(Near(Channel(albedo, centre, 0), 0.2f));
    assert(Near(Channel(albedo, centre, 1), 0.4f));
    assert(Near(Channel(albedo, centre, 2), 0.8f));
    assert(Near(Channel(orm, centre, 0), 0.65f));
    assert(Near(Channel(orm, centre, 1), 0.25f));
    assert(Near(Channel(orm, centre, 2), 0.75f));

    // Assigning a manual layer must not recolour unpainted automatic terrain.
    layers[1].albedo = {1.0f, 0.0f, 1.0f};
    engine::BuildTerrainSurfaceMaps(heightmap, {}, layers, albedo, orm, 4);
    assert(!(Near(Channel(albedo, centre, 0), 1.0f)
             && Near(Channel(albedo, centre, 2), 1.0f)));

    // Painted material textures are sampled and tiled, rather than reduced to
    // the old default terrain colour.
    layers[2].albedo = glm::vec3(1.0f);
    engine::TerrainLayerTexture textures[6];
    textures[2].albedoWidth = 2;
    textures[2].albedoHeight = 1;
    textures[2].tiling = glm::vec2(1.0f);
    textures[2].albedoRgba = {255, 0, 0, 255, 0, 0, 255, 255};
    engine::BuildTerrainSurfaceMaps(heightmap,
        std::vector<std::uint8_t>(4, 2), layers, albedo, orm, 4, textures);
    assert(Channel(albedo, centre, 2) > 0.9f);
    assert(Channel(albedo, centre, 0) < 0.1f);

    engine::TerrainAssetData asset;
    asset.name = "Test Landscape";
    asset.resolution = 2;
    asset.size = 12.0f;
    asset.maxHeight = 3.0f;
    asset.heights = {0.0f, 1.0f, 2.0f, 3.0f};
    asset.paint = {0, 1, 2, 3};
    asset.layerMaterials[2] = "GameAssets/Materials/Rock.3dgmat";
    asset.grassEnabled = true;
    asset.grassRandomizeHeight = true;
    const std::filesystem::path terrainPath =
        std::filesystem::temp_directory_path() / "3dg_terrain_asset_test.3dgterrain";
    std::string assetError;
    assert(engine::SaveTerrainAsset(terrainPath.string(), asset, &assetError));
    engine::TerrainAssetData loaded;
    assert(engine::LoadTerrainAsset(terrainPath.string(), &loaded, &assetError));
    assert(loaded.name == asset.name && loaded.heights == asset.heights);
    assert(loaded.paint == asset.paint && loaded.layerMaterials[2] == asset.layerMaterials[2]);
    assert(loaded.grassEnabled && loaded.grassRandomizeHeight);
    std::error_code ignored;
    std::filesystem::remove(terrainPath, ignored);

    engine::TerrainCameraConstraint cameraConstraint;
    glm::vec3 resolved = cameraConstraint.Resolve(
        {0.0f, 0.5f, 0.0f}, 1.0f, true, 1.0f / 60.0f, 0.35f, 8.0f);
    assert(Near(resolved.y, 1.35f, 0.001f));
    const glm::vec3 released = cameraConstraint.Resolve(
        {0.0f, 0.5f, 0.0f}, 0.0f, false, 1.0f / 60.0f, 0.35f, 8.0f);
    assert(released.y < resolved.y && released.y > 0.5f);

    std::cout << "terrain tests passed\n";
    return 0;
}
