#pragma once

#include "engine/assets/StaticMeshAsset.h"

#include <string>
#include <vector>

namespace engine {

class AssetRegistry;

struct ImportedMaterialGenerationOptions {
    bool importMaterials = true;
    bool importTextures = true;
    bool applyImportedMaterials = true;
    bool createMaterialFolder = true;
    bool createTextureFolder = true;
    bool reuseExistingMaterials = true;
    bool reuseExistingTextures = true;
    StaticMeshImportOptions::MaterialReimportPolicy materialReimportPolicy =
        StaticMeshImportOptions::MaterialReimportPolicy::PreserveExisting;
};

struct ImportedMaterialGenerationStats {
    std::size_t importedMaterials = 0;
    std::size_t importedTextures = 0;
    std::size_t reusedMaterials = 0;
    std::size_t reusedTextures = 0;
    std::size_t failedTextures = 0;
    std::size_t assignedSlots = 0;
};

// Converts the decoded source records retained by the model importer into
// standalone engine assets. The registry passed here should be a transaction
// copy; callers publish it only after the mesh and generated files commit.
bool CreateImportedMaterialAssets(
    const std::string& sourceModelPath,
    const std::string& meshAssetPath,
    const std::string& contentRoot,
    const ImportedMaterialGenerationOptions& options,
    const std::vector<StaticMeshMaterialData>& sourceMaterials,
    const std::vector<StaticMeshTextureData>& sourceTextures,
    AssetRegistry* registry,
    std::vector<MeshMaterialSlot>* materialSlots,
    std::vector<AssetHandle>* dependencies,
    ImportedMaterialGenerationStats* stats = nullptr,
    std::string* error = nullptr);

} // namespace engine
