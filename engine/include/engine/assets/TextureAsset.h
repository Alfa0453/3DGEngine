#pragma once

#include "engine/assets/AssetIdentity.h"

#include <cstdint>
#include <string>
#include <vector>

namespace engine {

class AssetRegistry;

inline constexpr std::uint32_t kTextureAssetVersion = 2;
inline constexpr std::uint32_t kLegacyTextureAssetVersion = 1;
inline constexpr std::uint32_t kTextureImporterVersion = 1;

struct TextureMipData {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    // Tightly packed RGBA8, bottom row first for direct OpenGL upload.
    std::vector<std::uint8_t> rgba;
};

struct TextureAssetData {
    NativeAssetHeader header;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    bool smooth = true;
    bool srgb = true;
    // Tightly packed RGBA8, bottom row first for direct OpenGL upload.
    std::vector<std::uint8_t> rgba;
    // Optional ordered levels after the base image. Version 2 assets preserve
    // map-aware authoring mips; version 1 assets load with this list empty.
    std::vector<TextureMipData> mipmaps;
};

struct TextureImportResult {
    AssetHandle id;
    std::string outputPath;
    std::uint64_t sourceHash = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

struct TextureImportOptions {
    bool smooth = true;
    bool srgb = true;
};

bool SaveTextureAsset(const std::string& path, TextureAssetData asset,
                      std::string* error = nullptr);
bool LoadTextureAsset(const std::string& path, TextureAssetData* asset,
                      std::string* error = nullptr);
bool ImportTextureSource(const std::string& sourcePath, TextureAssetData* asset,
                         std::string* error = nullptr);
bool ImportTextureSource(const std::string& sourcePath,
                         const TextureImportOptions& options,
                         TextureAssetData* asset,
                         std::string* error = nullptr);
bool ImportTextureToAsset(const std::string& sourcePath,
                          const std::string& destinationPath,
                          const std::string& contentRoot,
                          AssetRegistry* registry,
                          TextureImportResult* result = nullptr,
                          std::string* error = nullptr);
bool ImportTextureToAsset(const std::string& sourcePath,
                          const std::string& destinationPath,
                          const std::string& contentRoot,
                          const TextureImportOptions& options,
                          AssetRegistry* registry,
                          TextureImportResult* result = nullptr,
                          std::string* error = nullptr);

} // namespace engine
