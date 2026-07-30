#pragma once

#include "engine/assets/AssetIdentity.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace engine {

class AssetRegistry;

inline constexpr std::uint32_t kStaticMeshAssetVersion = 1;
inline constexpr std::uint32_t kStaticMeshImporterVersion = 1;
inline constexpr std::uint32_t kStaticMeshVertexStride = 11;

// CPU-side representation stored in a .3dgmesh. Textures are embedded as
// bottom-up RGBA8 pixels so a packaged game does not need the original FBX,
// OBJ, glTF, or its source texture files.
struct StaticMeshTextureData {
    std::string name;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgba;
};

struct StaticMeshMaterialData {
    std::string name;
    std::array<float, 3> diffuse{{0.8f, 0.8f, 0.8f}};
    std::array<float, 3> specular{{0.2f, 0.2f, 0.2f}};
    std::array<float, 3> emissive{{0.0f, 0.0f, 0.0f}};
    float shininess = 32.0f;
    std::int32_t diffuseMap = -1;
    std::int32_t normalMap = -1;
    std::int32_t specularMap = -1;
    std::int32_t emissiveMap = -1;
};

struct StaticMeshSubMeshData {
    std::int32_t material = -1;
    // Interleaved position3 / normal3 / uv2 / tangent3.
    std::vector<float> vertices;
    std::vector<std::uint32_t> indices;
};

struct StaticMeshAssetData {
    NativeAssetHeader header;
    std::array<float, 3> minimum{{0.0f, 0.0f, 0.0f}};
    std::array<float, 3> maximum{{0.0f, 0.0f, 0.0f}};
    std::vector<StaticMeshMaterialData> materials;
    std::vector<StaticMeshTextureData> textures;
    std::vector<StaticMeshSubMeshData> subMeshes;
};

struct StaticMeshImportOptions {
    float uniformScale = 1.0f;
    bool generateSmoothNormals = true;
    bool generateTangents = true;
    bool joinIdenticalVertices = true;
    bool flipUVs = false;
};

struct StaticMeshImportResult {
    AssetHandle id;
    std::string outputPath;
    std::uint64_t sourceHash = 0;
    std::size_t subMeshCount = 0;
    std::size_t vertexCount = 0;
    std::size_t triangleCount = 0;
    std::size_t embeddedTextureCount = 0;
};

bool ValidateStaticMeshAsset(const StaticMeshAssetData& asset,
                             std::string* error = nullptr);
bool SaveStaticMeshAsset(const std::string& path, StaticMeshAssetData asset,
                         std::string* error = nullptr);
bool LoadStaticMeshAsset(const std::string& path, StaticMeshAssetData* asset,
                         std::string* error = nullptr);

// Converts a supported raw model to CPU data. This rejects files containing
// bones so skeletal meshes cannot silently lose their skinning information.
bool ImportStaticMeshSource(const std::string& sourcePath,
                            const StaticMeshImportOptions& options,
                            StaticMeshAssetData* asset,
                            StaticMeshImportResult* result = nullptr,
                            std::string* error = nullptr);

// Complete editor import/reimport operation. An existing valid destination
// keeps its AssetHandle, making reimport safe for future ID-based references.
bool ImportStaticMeshToAsset(const std::string& sourcePath,
                             const std::string& destinationPath,
                             const std::string& contentRoot,
                             const StaticMeshImportOptions& options,
                             AssetRegistry* registry,
                             StaticMeshImportResult* result = nullptr,
                             std::string* error = nullptr);

std::uint64_t HashAssetSourceFile(const std::string& path,
                                  std::string* error = nullptr);

} // namespace engine
