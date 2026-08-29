#include <engine/assets/AssetRegistry.h>
#include <engine/assets/MaterialAssetLoader.h>
#include <engine/assets/StaticMeshAsset.h>
#include <engine/assets/TextureAsset.h>
#include <engine/physics/CollisionMesh.h>

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
    imported.collisionType = engine::StaticMeshCollisionType::TriangleMesh;
    engine::MeshMaterialSlot roundTripSlot;
    roundTripSlot.name = "Red";
    roundTripSlot.materialId = engine::AssetHandle::Generate();
    roundTripSlot.materialPath = "/Game/Materials/Red.3dgmat";
    imported.materialSlots.resize(imported.materials.size());
    for (std::size_t i = 0; i < imported.materialSlots.size(); ++i)
        imported.materialSlots[i].name = imported.materials[i].name.empty()
            ? "Material_" + std::to_string(i) : imported.materials[i].name;
    imported.materialSlots[static_cast<std::size_t>(importedMaterial)] = roundTripSlot;

    const bool savedRoundTrip =
        engine::SaveStaticMeshAsset(destination.string(), imported, &error);
    if (!savedRoundTrip) std::cerr << "Save error: " << error << '\n';
    Check(savedRoundTrip,
          "save versioned native static mesh");
    engine::StaticMeshAssetData loaded;
    Check(engine::LoadStaticMeshAsset(destination.string(), &loaded, &error)
              && loaded.header.id == imported.header.id
              && loaded.header.payloadSize > 0
              && loaded.subMeshes[0].vertices == imported.subMeshes[0].vertices
              && loaded.subMeshes[0].indices == imported.subMeshes[0].indices
              && loaded.subMeshes[0].vertexColors == imported.subMeshes[0].vertexColors
              && loaded.collisionType == engine::StaticMeshCollisionType::TriangleMesh
              && loaded.materialSlots.size() == imported.materials.size()
              && loaded.materialSlots[static_cast<std::size_t>(importedMaterial)].materialId
                     == roundTripSlot.materialId
              && loaded.materialSlots[static_cast<std::size_t>(importedMaterial)].materialPath
                     == roundTripSlot.materialPath
              && loaded.materials[0].diffuse == imported.materials[0].diffuse
              && loaded.textures.size() == 1
              && loaded.textures[0].rgba == imported.textures[0].rgba,
          "native static mesh round-trips identity, geometry, materials, textures and vertex paint");

    const auto collisionA = engine::physics::AcquireCollisionMesh(destination.string(), &error);
    const auto collisionB = engine::physics::AcquireCollisionMesh(destination.string(), &error);
    Check(collisionA && collisionB && collisionA.get() == collisionB.get()
              && collisionA->triangles.size() == 1,
          "collision mesh cooking is cached and shared across instances");
    engine::physics::InvalidateCollisionMesh(destination.string());

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
    engine::StaticMeshAssetData generatedMesh;
    Check(engine::LoadStaticMeshAsset(destination.string(), &generatedMesh, &error)
              && firstImport.importedMaterialCount >= 1
              && firstImport.importedTextureCount == 1
              && firstImport.assignedMaterialSlotCount
                    == generatedMesh.materialSlots.size()
              && !generatedMesh.materialSlots.empty(),
          "complete import generates standalone material and texture slots");
    const engine::MeshMaterialSlot generatedSlot = generatedMesh.materialSlots[0];
    std::string generatedMaterialRelative = generatedSlot.materialPath;
    if (generatedMaterialRelative.rfind("/Game/", 0) == 0)
        generatedMaterialRelative.erase(0, 6);
    const fs::path generatedMaterialPath =
        contentRoot / fs::path(generatedMaterialRelative);
    engine::RuntimeMaterialAsset artistMaterial;
    Check(engine::LoadMaterialAssetFile(
              generatedMaterialPath.string(), &artistMaterial, &error)
              && artistMaterial.id == generatedSlot.materialId,
          "generated material is a valid standalone engine asset");
    artistMaterial.material.albedo = {0.13f, 0.27f, 0.61f};
    Check(engine::SaveMaterialAssetFile(
              generatedMaterialPath.string(), artistMaterial, &error),
          "artist can edit a generated material independently of the mesh");

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

    engine::RuntimeMaterialAsset preservedMaterial;
    engine::StaticMeshAssetData reimportedMesh;
    Check(engine::LoadMaterialAssetFile(
              generatedMaterialPath.string(), &preservedMaterial, &error)
              && engine::LoadStaticMeshAsset(
                  destination.string(), &reimportedMesh, &error)
              && preservedMaterial.id == artistMaterial.id
              && preservedMaterial.material.albedo == artistMaterial.material.albedo
              && !reimportedMesh.materialSlots.empty()
              && reimportedMesh.materialSlots[0].materialId == generatedSlot.materialId
              && reimport.reusedMaterialCount >= 1
              && reimport.reusedTextureCount == 1,
          "default reimport updates textures while preserving artist material edits and IDs");

    engine::StaticMeshAssetData reloaded;
    Check(engine::LoadStaticMeshAsset(destination.string(), &reloaded, &error)
              && reloaded.header.id == originalId
              && reloaded.maximum[0] == 2.0f,
          "reimport replaces geometry without changing asset identity");

    const auto importWithOptions = [&](const char* packageName,
                                       const engine::StaticMeshImportOptions& options,
                                       engine::StaticMeshAssetData* mesh,
                                       engine::StaticMeshImportResult* result) {
        const fs::path optionRoot = root / packageName / "Content";
        const fs::path optionMesh = optionRoot / packageName
            / (std::string(packageName) + ".3dgmesh");
        engine::AssetRegistry optionRegistry;
        return engine::ImportStaticMeshToAsset(
                   source.string(), optionMesh.string(), optionRoot.string(), options,
                   &optionRegistry, result, &error)
            && engine::LoadStaticMeshAsset(optionMesh.string(), mesh, &error);
    };

    engine::StaticMeshImportOptions noMaterialsOptions;
    noMaterialsOptions.importMaterials = false;
    engine::StaticMeshAssetData noMaterialsMesh;
    engine::StaticMeshImportResult noMaterialsResult;
    Check(importWithOptions("NoMaterials", noMaterialsOptions,
                            &noMaterialsMesh, &noMaterialsResult)
              && noMaterialsResult.importedMaterialCount == 0
              && noMaterialsResult.importedTextureCount == 0
              && noMaterialsMesh.materialSlots.empty()
              && noMaterialsMesh.materials.empty()
              && noMaterialsMesh.textures.empty()
              && noMaterialsMesh.subMeshes[0].material == -1,
          "disabling material import leaves the mesh on its default surface");

    engine::StaticMeshImportOptions noTexturesOptions;
    noTexturesOptions.importTextures = false;
    engine::StaticMeshAssetData noTexturesMesh;
    engine::StaticMeshImportResult noTexturesResult;
    Check(importWithOptions("NoTextures", noTexturesOptions,
                            &noTexturesMesh, &noTexturesResult)
              && noTexturesResult.importedMaterialCount >= 1
              && noTexturesResult.importedTextureCount == 0
              && !noTexturesMesh.materialSlots.empty()
              && noTexturesMesh.materials.empty()
              && noTexturesMesh.textures.empty(),
          "disabling texture import creates scalar-only standalone materials");

    engine::StaticMeshImportOptions doNotApplyOptions;
    doNotApplyOptions.applyImportedMaterials = false;
    engine::StaticMeshAssetData doNotApplyMesh;
    engine::StaticMeshImportResult doNotApplyResult;
    Check(importWithOptions("DoNotApply", doNotApplyOptions,
                            &doNotApplyMesh, &doNotApplyResult)
              && doNotApplyResult.importedMaterialCount >= 1
              && doNotApplyResult.importedTextureCount == 1
              && doNotApplyMesh.materialSlots.empty()
              && doNotApplyMesh.subMeshes[0].material == -1,
          "disabling apply still creates assets without assigning mesh slots");

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
