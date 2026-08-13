#include <engine/assets/AssetRegistry.h>
#include <engine/assets/StaticMeshAsset.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

void Check(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
}

void WriteTriangleObj(const std::filesystem::path& path, float width) {
    std::ofstream output(path, std::ios::trunc);
    output << "mtllib Triangle.mtl\n"
           << "o Triangle\n"
           << "v 0 0 0\n"
           << "v " << width << " 0 0\n"
           << "v 0 1 0\n"
           << "vt 0 0\n"
           << "vt 1 0\n"
           << "vt 0 1\n"
           << "vn 0 0 1\n"
           << "usemtl Red\n"
           << "f 1/1/1 2/2/1 3/3/1\n";
}

} // namespace

int main() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "3dg_static_mesh_asset_tests";
    const fs::path sourceRoot = root / "Source";
    const fs::path contentRoot = root / "Content";
    const fs::path destination = contentRoot / "Meshes" / "Triangle.3dgmesh";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(sourceRoot, ec);
    fs::create_directories(destination.parent_path(), ec);

    {
        std::ofstream material(sourceRoot / "Triangle.mtl");
        material << "newmtl Red\n"
                 << "Kd 0.8 0.1 0.05\n"
                 << "Ks 0.2 0.2 0.2\n"
                 << "Ns 24\n"
                 << "map_Kd Pixel.tga\n";
        std::array<unsigned char, 18> header{};
        header[2] = 2;
        header[12] = 1;
        header[14] = 1;
        header[16] = 24;
        std::ofstream texture(sourceRoot / "Pixel.tga", std::ios::binary);
        texture.write(reinterpret_cast<const char*>(header.data()), header.size());
        const std::array<unsigned char, 3> redBgr{{0, 0, 255}};
        texture.write(reinterpret_cast<const char*>(redBgr.data()), redBgr.size());
    }
    const fs::path source = sourceRoot / "Triangle.obj";
    WriteTriangleObj(source, 1.0f);

    std::string error;
    engine::StaticMeshAssetData imported;
    engine::StaticMeshImportResult sourceResult;
    Check(engine::ImportStaticMeshSource(
              source.string(), {}, &imported, &sourceResult, &error),
          "import OBJ into static mesh CPU data");
    const std::int32_t importedMaterial = imported.subMeshes.empty()
        ? -1 : imported.subMeshes[0].material;
    Check(imported.header.type == engine::AssetType::StaticMesh
              && imported.header.id.Valid()
              && imported.header.sourceHash != 0
              && imported.subMeshes.size() == 1
              && imported.subMeshes[0].vertices.size()
                     == 3 * engine::kStaticMeshVertexStride
              && imported.subMeshes[0].indices.size() == 3
              && imported.materials.size() >= 1
              && imported.textures.size() == 1
              && importedMaterial >= 0
              && imported.materials[static_cast<std::size_t>(importedMaterial)]
                     .diffuseMap == 0
              && imported.textures[0].rgba
                     == std::vector<std::uint8_t>({255, 0, 0, 255})
              && sourceResult.vertexCount == 3
              && sourceResult.triangleCount == 1,
          "OBJ conversion preserves geometry, materials, textures and statistics");

    imported.subMeshes[0].vertexColors = {
        1.0f, 0.0f, 0.0f, 1.0f,
        0.0f, 1.0f, 0.0f, 0.5f,
        0.0f, 0.0f, 1.0f, 0.0f};

    Check(engine::SaveStaticMeshAsset(destination.string(), imported, &error),
          "save versioned native static mesh");
    engine::StaticMeshAssetData loaded;
    Check(engine::LoadStaticMeshAsset(destination.string(), &loaded, &error)
              && loaded.header.id == imported.header.id
              && loaded.header.payloadSize > 0
              && loaded.subMeshes[0].vertices == imported.subMeshes[0].vertices
              && loaded.subMeshes[0].indices == imported.subMeshes[0].indices
              && loaded.subMeshes[0].vertexColors == imported.subMeshes[0].vertexColors
              && loaded.materials[0].diffuse == imported.materials[0].diffuse
              && loaded.textures.size() == 1
              && loaded.textures[0].rgba == imported.textures[0].rgba,
          "native static mesh round-trips identity, geometry, materials, textures and vertex paint");

    engine::StaticMeshAssetData invalidPaint = imported;
    invalidPaint.subMeshes[0].vertexColors.pop_back();
    Check(!engine::SaveStaticMeshAsset(
              (contentRoot / "Meshes" / "InvalidPaint.3dgmesh").string(),
              invalidPaint, &error),
          "static mesh rejects vertex paint whose count does not match the mesh");

    engine::AssetRegistry registry;
    fs::remove(destination, ec);
    engine::StaticMeshImportResult firstImport;
    const bool completeImport = engine::ImportStaticMeshToAsset(
        source.string(), destination.string(), contentRoot.string(), {},
        &registry, &firstImport, &error);
    if (!completeImport) std::cerr << "Import error: " << error << '\n';
    Check(completeImport,
          "complete static mesh import writes asset and registry");
    const engine::AssetRegistryEntry* registered = registry.Find(firstImport.id);
    Check(fs::is_regular_file(destination)
              && registered
              && registered->type == engine::AssetType::StaticMesh
              && registered->virtualPath == "/Game/Meshes/Triangle.3dgmesh"
              && registered->sourceHash == firstImport.sourceHash
              && fs::is_regular_file(
                  engine::AssetRegistry::DefaultRegistryPath(contentRoot.string())),
          "static mesh import registers stable identity and source metadata");

    const engine::AssetHandle originalId = firstImport.id;
    const std::uint64_t originalHash = firstImport.sourceHash;
    WriteTriangleObj(source, 2.0f);
    engine::StaticMeshImportResult reimport;
    Check(engine::ImportStaticMeshToAsset(
              source.string(), destination.string(), contentRoot.string(), {},
              &registry, &reimport, &error)
              && reimport.id == originalId
              && reimport.sourceHash != originalHash
              && registry.Find(originalId)
              && registry.Find(originalId)->sourceHash == reimport.sourceHash,
          "reimport preserves AssetHandle while refreshing source hash");

    engine::StaticMeshAssetData reloaded;
    Check(engine::LoadStaticMeshAsset(destination.string(), &reloaded, &error)
              && reloaded.header.id == originalId
              && reloaded.maximum[0] == 2.0f,
          "reimport replaces geometry without changing asset identity");

    {
        std::ofstream corrupt(destination, std::ios::binary | std::ios::app);
        corrupt.put('\0');
    }
    // Extra trailing data is outside payloadSize and deliberately ignored: the
    // typed payload itself is still complete. Truncating it must fail.
    const auto size = fs::file_size(destination);
    fs::resize_file(destination, size - 8, ec);
    Check(!engine::LoadStaticMeshAsset(destination.string(), &reloaded, &error),
          "truncated static mesh payload is rejected");

    fs::remove_all(root, ec);
    std::cout << "static mesh asset tests passed\n";
    return 0;
}
