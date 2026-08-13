#include <engine/assets/CaveAsset.h>

#include <cmath>
#include <filesystem>
#include <iostream>

namespace {
int failures = 0;
void Check(bool value, const char* message) {
    if (!value) { ++failures; std::cerr << message << '\n'; }
}
}

int main() {
    engine::CaveAssetData cave;
    cave.name = "RegressionCave";
    cave.points = {{0,0,0}, {0,0,5}, {4,1,12}};
    cave.width = 6.0f; cave.height = 4.0f; cave.sampleSpacing = 0.75f;
    cave.radialSegments = 12;
    cave.wallMaterialPath = "Materials/Rock.3dgmat";
    cave.wallMaterialId = {7, 11};
    cave.chambers.push_back({1, 2.0f, 1.5f});

    std::string error;
    Check(engine::ValidateCaveAsset(cave, &error), "valid cave rejected");
    engine::StaticMeshAssetData mesh;
    engine::CaveGenerationStats stats;
    Check(engine::BuildCaveStaticMesh(cave, &mesh, &stats, &error),
          "cave mesh generation failed");
    Check(mesh.header.type == engine::AssetType::StaticMesh,
          "generated mesh has wrong type");
    Check(stats.vertices > 24 && stats.triangles > 24 && stats.length > 10.0f,
          "generated cave geometry is unexpectedly small");
    Check(!mesh.subMeshes.empty() && mesh.subMeshes.front().vertices.size() % 11 == 0,
          "generated cave vertex layout is invalid");
    if (!mesh.subMeshes.empty() && mesh.subMeshes.front().vertices.size() >= 11) {
        const auto& v = mesh.subMeshes.front().vertices;
        const float radialDot = v[0] * v[3] + v[1] * v[4];
        Check(radialDot < 0.0f, "cave normals do not face inward");
    }

    const auto root = std::filesystem::temp_directory_path() / "3dg_cave_asset_test";
    const auto path = root / "Regression.3dgcave";
    std::error_code ec; std::filesystem::remove_all(root, ec);
    Check(engine::SaveCaveAsset(path.string(), cave, &error), "cave save failed");
    engine::CaveAssetData loaded;
    Check(engine::LoadCaveAsset(path.string(), &loaded, &error), "cave load failed");
    Check(loaded.header.type == engine::AssetType::Cave && loaded.header.id.Valid(),
          "cave identity did not round trip");
    Check(loaded.points.size() == cave.points.size() && loaded.chambers.size() == 1,
          "cave shape did not round trip");
    Check(loaded.header.dependencies.size() == 1
          && loaded.header.dependencies.front() == cave.wallMaterialId,
          "cave dependency did not round trip");

    engine::CaveAssetData invalid;
    invalid.name = "Invalid"; invalid.points = {{0,0,0}};
    Check(!engine::ValidateCaveAsset(invalid, &error), "one-point cave was accepted");
    std::filesystem::remove_all(root, ec);
    return failures == 0 ? 0 : 1;
}
