#pragma once

#include "engine/assets/AssetIdentity.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace engine {

class AssetRegistry;

inline constexpr std::uint32_t kForgeMaterialImporterVersion = 1;

// One decoded record from a Material Forge .3dgtexpack source package.
// Pixels retain the package's top-row-first layout and declared channel format.
struct ForgeTextureRecord {
    std::string name;
    std::uint32_t level = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t channels = 0;
    std::uint32_t bitDepth = 0;
    std::vector<std::uint8_t> pixels;
};

struct ForgeTexturePackage {
    std::string materialName;
    std::string normalY = "positive";
    bool tileable = true;
    std::vector<ForgeTextureRecord> records;

    const ForgeTextureRecord* Find(const std::string& name,
                                   std::uint32_t level = 0) const;
};

struct ForgeMaterialImportResult {
    AssetHandle materialId;
    AssetHandle albedoMapId;
    AssetHandle normalMapId;
    AssetHandle metalRoughMapId;
    AssetHandle heightMapId;
    std::string materialPath;
    std::string albedoMapPath;
    std::string normalMapPath;
    std::string metalRoughMapPath;
    std::string heightMapPath;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::size_t sourceRecordCount = 0;
};

// Fully validates the package header, bounds, mip tables, zlib streams, and
// CRC-32 values before returning any texture data.
bool LoadForgeTexturePackage(const std::string& path,
                             ForgeTexturePackage* package,
                             std::string* error = nullptr);

// Cooks a Material Forge source package into the engine's normal assets:
// four per-texture .3dgtex files and one registered .3dgmat.
bool ImportForgeMaterialPackage(const std::string& sourcePath,
                                const std::string& destinationDirectory,
                                const std::string& contentRoot,
                                AssetRegistry* registry,
                                ForgeMaterialImportResult* result = nullptr,
                                std::string* error = nullptr);

} // namespace engine
