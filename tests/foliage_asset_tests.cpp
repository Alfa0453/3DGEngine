#include <engine/assets/FoliageAsset.h>

#include <cmath>
#include <filesystem>
#include <iostream>

namespace {
int failures = 0;
void Check(bool value, const char* message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
bool Near(float a, float b) { return std::abs(a - b) < 0.0001f; }
}

int main() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "3dg_foliage_asset_tests";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);

    engine::FoliageAssetData asset;
    asset.name = "Forest Palette";
    engine::FoliageTypeAsset tree;
    tree.name = "Pine";
    tree.meshPath = "Content/Assets/Meshes/Pine.3dgmesh";
    tree.meshId = engine::AssetHandle::Generate();
    tree.materialPath = "Content/Assets/Materials/Pine.3dgmat";
    tree.materialId = engine::AssetHandle::Generate();
    tree.density = 0.65f;
    tree.minScale = glm::vec3(0.8f, 0.9f, 0.8f);
    tree.maxScale = glm::vec3(1.2f, 1.5f, 1.2f);
    tree.minimumSpacing = 2.5f;
    tree.cullStartDistance = 90.0f;
    tree.cullEndDistance = 140.0f;
    tree.lod1MeshPath = "Content/Assets/Meshes/Pine_LOD1.3dgmesh";
    tree.lod1MeshId = engine::AssetHandle::Generate();
    tree.lod2MeshPath = "Content/Assets/Meshes/Pine_LOD2.3dgmesh";
    tree.lod2MeshId = engine::AssetHandle::Generate();
    tree.lod1Distance = 42.0f;
    tree.lod2Distance = 86.0f;
    tree.collisionEnabled = true;
    asset.types.push_back(tree);

    const fs::path path = root / "Forest.3dgfoliage";
    std::string error;
    Check(engine::SaveFoliageAsset(path.string(), asset, &error), "save foliage asset");

    engine::FoliageAssetData loaded;
    Check(engine::LoadFoliageAsset(path.string(), &loaded, &error), "load foliage asset");
    Check(loaded.header.type == engine::AssetType::Foliage, "native foliage type");
    Check(loaded.header.id.Valid(), "stable foliage ID");
    Check(loaded.header.dependencies.size() == 4, "mesh, material, and LOD dependencies");
    Check(loaded.name == "Forest Palette" && loaded.types.size() == 1, "palette data");
    if (!loaded.types.empty()) {
        const auto& value = loaded.types.front();
        Check(value.name == "Pine" && value.meshPath == tree.meshPath, "type paths");
        Check(Near(value.density, 0.65f) && Near(value.maximumSlopeDegrees, 50.0f),
              "placement settings");
        Check(Near(value.maxScale.y, 1.5f) && value.collisionEnabled,
              "scale and collision settings");
        Check(value.lod1MeshPath == tree.lod1MeshPath && value.lod2MeshPath == tree.lod2MeshPath
                  && Near(value.lod1Distance, 42.0f) && Near(value.lod2Distance, 86.0f),
              "LOD settings");
    }

    fs::remove_all(root, ec);
    if (failures) return 1;
    std::cout << "foliage asset tests passed\n";
    return 0;
}
