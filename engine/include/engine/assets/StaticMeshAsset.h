#pragma once

#include "engine/assets/AssetIdentity.h"
#include "engine/physics/PhysicsComponents.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace engine {

class AssetRegistry;

inline constexpr std::uint32_t kStaticMeshAssetVersion = 6;
inline constexpr std::uint32_t kStaticMeshImporterVersion = 2;
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
    // PBR source values retained during import. Legacy assets derive roughness
    // from shininess when loaded; new imports use Assimp's glTF values when
    // present and the Phong conversion otherwise.
    float metallic = 0.0f;
    float roughness = 0.5f;
    float ao = 1.0f;
    float opacity = 1.0f;
    std::int32_t alphaMode = 0; // ecs::PbrMaterial::BlendMode numeric value
    std::int32_t diffuseMap = -1;
    std::int32_t normalMap = -1;
    std::int32_t specularMap = -1;
    std::int32_t emissiveMap = -1;
    std::int32_t metalRoughMap = -1;
    std::int32_t metallicMap = -1;
    std::int32_t roughnessMap = -1;
    std::int32_t aoMap = -1;
    std::int32_t heightMap = -1;
};

// Stable indirection between source material indices and standalone engine
// material assets. Submeshes continue to store a compact slot index.
struct MeshMaterialSlot {
    std::string name;
    AssetHandle materialId;
    // Content-relative fallback. AssetHandle remains the primary identity so
    // Content moves and renames do not break the mesh.
    std::string materialPath;
};

struct StaticMeshSubMeshData {
    std::int32_t material = -1;
    // Interleaved position3 / normal3 / uv2 / tangent3.
    std::vector<float> vertices;
    std::vector<std::uint32_t> indices;
    // Optional normalized RGBA paint, four floats per vertex. Empty means
    // unpainted white and keeps newly imported assets compact.
    std::vector<float> vertexColors;
    // Open surfaces such as planes, cards, leaves and cloth must remain visible
    // from either side. Import detects geometric boundary edges and persists the
    // result so every renderer and shadow pass uses the same culling policy.
    bool twoSided = false;
};

enum class StaticMeshCollisionType : std::uint32_t {
    None = 0, Box = 1, Sphere = 2, Capsule = 3, ConvexHull = 4, TriangleMesh = 5
};

struct StaticMeshAssetData {
    NativeAssetHeader header;
    std::array<float, 3> minimum{{0.0f, 0.0f, 0.0f}};
    std::array<float, 3> maximum{{0.0f, 0.0f, 0.0f}};
    std::vector<StaticMeshMaterialData> materials;
    std::vector<StaticMeshTextureData> textures;
    std::vector<MeshMaterialSlot> materialSlots;
    std::vector<StaticMeshSubMeshData> subMeshes;
    StaticMeshCollisionType collisionType = StaticMeshCollisionType::None;
    // Authored compound collision. The first shape becomes the instance's
    // primary Collider; remaining shapes become AdditionalColliders.
    std::vector<ecs::Collider> colliders;
};

struct StaticMeshImportOptions {
    float uniformScale = 1.0f;
    bool generateSmoothNormals = true;
    bool generateTangents = true;
    bool joinIdenticalVertices = true;
    bool flipUVs = false;
    bool detectOpenMeshesAsTwoSided = true;
    bool importMaterials = true;
    bool importTextures = true;
    bool applyImportedMaterials = true;
    bool createMaterialFolder = true;
    bool createTextureFolder = true;
    bool reuseExistingMaterials = true;
    bool reuseExistingTextures = true;
    bool keepLegacyEmbeddedFallback = false;
    enum class MaterialReimportPolicy {
        PreserveExisting = 0,
        UpdateSourceProperties = 1,
        Recreate = 2
    };
    MaterialReimportPolicy materialReimportPolicy =
        MaterialReimportPolicy::PreserveExisting;
};

// Uses position-welded triangle edges, so UV/normal seams on a closed solid do
// not incorrectly classify it as open geometry.
bool StaticMeshSubMeshIsOpen(const StaticMeshSubMeshData& subMesh);

struct StaticMeshImportResult {
    AssetHandle id;
    std::string outputPath;
    std::uint64_t sourceHash = 0;
    std::size_t subMeshCount = 0;
    std::size_t vertexCount = 0;
    std::size_t triangleCount = 0;
    std::size_t embeddedTextureCount = 0;
    std::size_t importedMaterialCount = 0;
    std::size_t importedTextureCount = 0;
    std::size_t reusedMaterialCount = 0;
    std::size_t reusedTextureCount = 0;
    std::size_t failedTextureCount = 0;
    std::size_t assignedMaterialSlotCount = 0;
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
