#include <engine/assets/AssetIdentity.h>
#include <engine/assets/AssetReference.h>
#include <engine/assets/AssetRegistry.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <utility>

namespace {

void Check(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
}

} // namespace

int main() {
    namespace fs = std::filesystem;
    std::string error;

    const engine::AssetHandle meshId = engine::AssetHandle::Generate();
    const engine::AssetHandle materialId = engine::AssetHandle::Generate();
    Check(meshId.Valid() && materialId.Valid() && meshId != materialId,
          "generated asset handles are valid and unique");
    engine::AssetHandle parsed;
    Check(engine::AssetHandle::Parse(meshId.ToString(), &parsed) && parsed == meshId,
          "asset handle string representation round-trips");
    Check(!engine::AssetHandle::Parse("not-an-asset-id", &parsed),
          "malformed asset handles are rejected");

    engine::NativeAssetHeader writtenHeader;
    writtenHeader.type = engine::AssetType::StaticMesh;
    writtenHeader.id = meshId;
    writtenHeader.assetVersion = 3;
    writtenHeader.importerVersion = 2;
    writtenHeader.sourceHash = 0x123456789abcdef0ull;
    writtenHeader.payloadSize = 4096;
    writtenHeader.flags = 7;
    writtenHeader.dependencies = {materialId};
    std::stringstream binary(std::ios::in | std::ios::out | std::ios::binary);
    Check(engine::WriteNativeAssetHeader(binary, writtenHeader, &error),
          "write common native asset header");
    binary.seekg(0);
    engine::NativeAssetHeader readHeader;
    Check(engine::ReadNativeAssetHeader(binary, &readHeader, &error)
          && readHeader.type == engine::AssetType::StaticMesh
          && readHeader.id == meshId
          && readHeader.assetVersion == 3
          && readHeader.importerVersion == 2
          && readHeader.sourceHash == writtenHeader.sourceHash
          && readHeader.payloadSize == 4096
          && readHeader.flags == 7
          && readHeader.dependencies.size() == 1
          && readHeader.dependencies[0] == materialId,
          "common native asset header preserves identity and dependencies");

    std::stringstream invalidBinary(std::ios::in | std::ios::out | std::ios::binary);
    invalidBinary << "not a native asset";
    invalidBinary.seekg(0);
    Check(!engine::ReadNativeAssetHeader(invalidBinary, &readHeader, &error),
          "foreign files are rejected by the native asset header reader");

    engine::AssetRegistry registry;
    engine::AssetRegistryEntry material;
    material.id = materialId;
    material.type = engine::AssetType::Material;
    material.virtualPath = "Content/Assets/Materials/Wizard.3dgmat";
    material.sourcePath = "SourceAssets/Wizard.fbx";
    material.sourceHash = 55;
    material.importerVersion = 1;
    Check(registry.Register(material, &error), "register material asset");

    engine::AssetRegistryEntry mesh;
    mesh.id = meshId;
    mesh.type = engine::AssetType::StaticMesh;
    mesh.virtualPath = "Assets/Meshes/Wizard.3dgmesh";
    mesh.sourcePath = "SourceAssets/Wizard.fbx";
    mesh.sourceHash = 88;
    mesh.importerVersion = 2;
    mesh.dependencies = {materialId};
    Check(registry.Register(mesh, &error), "register static mesh asset");
    Check(registry.Find(meshId)
          && registry.FindByPath("/Game/Assets/Meshes/Wizard.3dgmesh")
          && registry.FindByPath("assets/meshes/wizard.3dgmesh"),
          "registry resolves assets by stable ID and normalized path");
    Check(registry.Referencers(materialId).size() == 1
          && registry.Referencers(materialId)[0] == meshId,
          "registry reports reverse dependencies");

    engine::AssetRegistryEntry conflict = mesh;
    conflict.id = engine::AssetHandle::Generate();
    Check(!registry.Register(conflict, &error),
          "registry prevents two IDs from owning the same virtual path");
    Check(registry.Move(meshId, "/Game/Environment/Wizard.3dgmesh", &error)
          && registry.Find(meshId)
          && registry.Find(meshId)->id == meshId
          && registry.FindByPath("/Game/Environment/Wizard.3dgmesh"),
          "moving an asset preserves its stable ID");
    Check(registry.Validate().empty(), "valid registry has no dependency issues");

    const fs::path root = fs::temp_directory_path() / "3dg_asset_registry_tests";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "Assets" / "Meshes", ec);
    const fs::path registryPath = engine::AssetRegistry::DefaultRegistryPath(root.string());
    Check(registry.Save(registryPath.string(), &error), "save project asset registry");
    engine::AssetRegistry loaded;
    Check(loaded.Load(registryPath.string(), &error)
          && loaded.Entries().size() == 2
          && loaded.Find(meshId)
          && loaded.Find(meshId)->dependencies.size() == 1
          && loaded.Find(meshId)->dependencies[0] == materialId,
          "project asset registry round-trips IDs, types, paths and dependencies");

    const fs::path nativePath = root / "Assets" / "Meshes" / "Crate.3dgmesh";
    engine::NativeAssetHeader nativeHeader;
    nativeHeader.type = engine::AssetType::StaticMesh;
    nativeHeader.id = engine::AssetHandle::Generate();
    nativeHeader.sourceHash = 123;
    nativeHeader.importerVersion = 4;
    {
        std::ofstream output(nativePath, std::ios::binary);
        Check(engine::WriteNativeAssetHeader(output, nativeHeader, &error),
              "write native asset for registry rebuild");
    }
    engine::AssetRegistry rebuilt;
    Check(rebuilt.RebuildFromContent(root.string(), &error)
          && rebuilt.Entries().size() == 1
          && rebuilt.Find(nativeHeader.id)
          && rebuilt.Find(nativeHeader.id)->type == engine::AssetType::StaticMesh
          && rebuilt.Find(nativeHeader.id)->virtualPath
                 == "/Game/Assets/Meshes/Crate.3dgmesh",
          "registry rebuild discovers typed native assets from their headers");

    engine::AssetRegistryEntry missingDependency = *rebuilt.Find(nativeHeader.id);
    missingDependency.dependencies.push_back(engine::AssetHandle::Generate());
    Check(rebuilt.Register(std::move(missingDependency), &error)
          && !rebuilt.Validate().empty(),
          "registry validation reports missing dependencies");

    engine::AssetRegistry references;
    engine::AssetRegistryEntry referencedMesh;
    referencedMesh.id = engine::AssetHandle::Generate();
    referencedMesh.type = engine::AssetType::StaticMesh;
    referencedMesh.virtualPath = "/Game/Assets/Meshes/Before.3dgmesh";
    Check(references.Register(referencedMesh, &error),
          "register asset used by ID-first reference test");
    const engine::AssetReference reference = engine::MakeAssetReference(
        &references, root.string(),
        (root / "Assets" / "Meshes" / "Before.3dgmesh").string(),
        engine::AssetType::StaticMesh);
    Check(reference.id == referencedMesh.id
          && references.Move(
              referencedMesh.id,
              "/Game/Assets/Meshes/After.3dgmesh", &error)
          && engine::ResolveAssetReference(
              &references, root.string(), reference,
              engine::AssetType::StaticMesh)
                 == (root / "Assets" / "Meshes" / "After.3dgmesh")
                        .lexically_normal().string(),
          "ID-first references survive asset moves while retaining path fallback");

    const engine::AssetHandle clipId = engine::AssetHandle::Generate();
    const fs::path clipPath = root / "Assets" / "Animations" / "Attack.3dgclip";
    fs::create_directories(clipPath.parent_path(), ec);
    {
        std::ofstream output(clipPath);
        output << "3DG_CLIP 4 " << clipId.ToString() << '\n'
               << "\"Attack\" \"-\" \"-\" 0 0 1\n"
               << "ASSET_DEPS 1 " << referencedMesh.id.ToString() << '\n';
    }
    const engine::AssetHandle hudId = engine::AssetHandle::Generate();
    const engine::AssetHandle behaviorId = engine::AssetHandle::Generate();
    const engine::AssetHandle audioId = engine::AssetHandle::Generate();
    {
        std::ofstream(root / "Gameplay.hud")
            << "3DG_HUD 3 " << hudId.ToString()
            << "\nASSET_DEPS 0\n";
        std::ofstream(root / "Enemy.btgraph")
            << "3DG_BEHAVIOR_GRAPH 7 " << behaviorId.ToString()
            << "\nASSET_DEPS 0\n";
        std::ofstream(root / "Fire.3dgaudio")
            << "3DGAUDIO_CUE 2 " << audioId.ToString()
            << "\nASSET_DEPS 0\n";
    }
    Check(references.SynchronizeAuthoredAssets(root.string(), &error)
          && references.Find(nativeHeader.id)
          && references.Find(nativeHeader.id)->type
                 == engine::AssetType::StaticMesh
          && references.Find(clipId)
          && references.Find(clipId)->type
                 == engine::AssetType::AnimationClip
          && references.Find(clipId)->dependencies.size() == 1
          && references.Find(clipId)->dependencies[0] == referencedMesh.id,
          "Content synchronization registers native and authored asset IDs and dependencies");
    Check(references.Find(hudId)
              && references.Find(hudId)->type == engine::AssetType::Hud
          && references.Find(behaviorId)
              && references.Find(behaviorId)->type
                  == engine::AssetType::BehaviorTree
          && references.Find(audioId)
              && references.Find(audioId)->type
                  == engine::AssetType::Audio,
          "Content synchronization registers HUD, behavior, and audio assets");

    fs::remove_all(root, ec);
    std::cout << "asset registry tests passed\n";
    return 0;
}
