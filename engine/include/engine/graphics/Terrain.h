#pragma once

#include "engine/graphics/Mesh.h"
#include "engine/graphics/Texture.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <vector>

namespace engine {

// Compact material data used by a terrain layer. Terrain blends these values into
// generated albedo and glTF-style ORM maps, so painted materials retain their PBR
// response instead of being reduced to a flat colour.
struct TerrainLayerSurface {
    glm::vec3 albedo{0.5f};
    float ao = 1.0f;
    float roughness = 0.85f;
    float metallic = 0.0f;
};

// CPU texture data baked into the terrain's generated surface maps. This lets
// terrain paint use engine-owned material textures while keeping terrain a
// single renderer submission instead of one draw per painted layer.
struct TerrainLayerTexture {
    int albedoWidth = 0;
    int albedoHeight = 0;
    int ormWidth = 0;
    int ormHeight = 0;
    std::vector<std::uint8_t> albedoRgba;
    std::vector<std::uint8_t> ormRgba;
    glm::vec2 tiling{8.0f};
};

// A square grid of heights over a world-space area. Pure data: generation and the
// HeightAt query are CPU-only (no GL), so they're unit-testable.
struct Heightmap {
    int       res  = 128;                 // vertices per side
    float     size = 64.0f;               // world extent (square)
    glm::vec3 origin{0.0f};               // world position of the (0,0) corner
    float     maxHeight = 8.0f;
    std::vector<float> h;                 // res*res heights (row-major, [0,maxHeight])

    bool  Empty() const { return h.empty() || res < 2; }
    float At(int i, int j) const;                      // clamped grid sample
    float HeightAt(float worldX, float worldZ) const;  // bilinear world query
    glm::vec3 NormalAt(int i, int j) const;            // finite-difference normal
};

// Procedural fractal (fBm value-noise) heightmap. Deterministic per seed.
Heightmap GenerateFbmHeightmap(int res, float size, const glm::vec3& origin,
                               float maxHeight, unsigned seed,
                               int octaves = 5, float baseFrequency = 2.0f);

// Build interleaved {pos3, normal3, uv2} vertex data + indices for a heightmap grid
// (no GL). UVs run 0..1 across the whole terrain.
void BuildTerrainMeshData(const Heightmap& hm,
                          std::vector<float>& outVertices,
                          std::vector<std::uint32_t>& outIndices);

// Index-only terrain LODs. They reuse the full-resolution vertex buffer and
// progressively skip grid vertices, so runtime switching has no vertex uploads.
std::vector<std::vector<std::uint32_t>> BuildTerrainLodIndices(
    int resolution, int maxLevels = 4);

// Build an RGBA albedo image (texRes x texRes) coloured by height + slope, with an
// optional per-vertex paint overlay (paint sized res*res; 0 = auto colour, 1..5 = a
// painted layer). `layerColors` holds the colour for layers 0..5 (index 0 unused); the
// editor fills 1..5 from either a default palette or an assigned material's colour.
std::vector<unsigned char> BuildTerrainAlbedo(const Heightmap& hm,
                                              const std::vector<std::uint8_t>& paint,
                                              const glm::vec3 layerColors[6],
                                              int texRes = 256);

// Build both textures used by the PBR terrain material. `outOrm` follows the
// engine/glTF convention: R = AO, G = roughness, B = metallic.
void BuildTerrainSurfaceMaps(const Heightmap& hm,
                             const std::vector<std::uint8_t>& paint,
                             const TerrainLayerSurface layers[6],
                             std::vector<unsigned char>& outAlbedo,
                             std::vector<unsigned char>& outOrm,
                             int texRes = 256,
                             const TerrainLayerTexture* textures = nullptr);

// The default paint-layer palette (index 0 unused; 1 grass, 2 rock, 3 dirt, 4 snow,
// 5 sand). Used when a layer has no material assigned.
void DefaultTerrainLayerColors(glm::vec3 outColors[6]);
void DefaultTerrainLayerSurfaces(TerrainLayerSurface outSurfaces[6]);

// Stateful terrain clearance for editor and gameplay cameras. Upward correction is
// immediate (the near plane never enters the ground); release/downhill motion is
// damped to prevent a visible camera snap.
class TerrainCameraConstraint {
public:
    glm::vec3 Resolve(const glm::vec3& desired, float surfaceY, bool overTerrain,
                      float dt, float clearance = 0.35f, float releaseSpeed = 10.0f);
    void Reset() { m_active = false; m_height = 0.0f; }
    bool Active() const { return m_active; }

private:
    bool m_active = false;
    float m_height = 0.0f;
};

// A renderable terrain: owns a Mesh + albedo Texture and keeps the Heightmap for
// queries. Generation/rebuilds need a live GL context. The Mesh is uploaded in place
// (never destroyed + recreated), so regenerating every frame is safe.
class Terrain {
public:
    // Build from procedural fBm noise.
    void Generate(int res, float size, const glm::vec3& origin,
                  float maxHeight, unsigned seed,
                  int octaves = 5, float baseFrequency = 2.0f);

    // Replace the heights (e.g. after sculpting or loading) and rebuild mesh + albedo.
    void SetHeightmap(const Heightmap& hm);

    // High-frequency sculpt path. Edit MutableMap().h, then commit the affected
    // grid rectangle. Only those GPU rows (plus a normal border) are refreshed;
    // topology, LOD buffers, and untouched vertices remain resident.
    Heightmap& MutableMap() { return m_hm; }
    void CommitHeightRegion(int minI, int minJ, int maxI, int maxJ);

    // Per-vertex paint layer (0 = auto height/slope colour, else a fixed layer:
    // 1 grass, 2 rock, 3 dirt, 4 snow, 5 sand). Rebuilds only the albedo. Sized
    // res*res (row-major); empty clears painting.
    void SetPaint(const std::vector<std::uint8_t>& paint);
    const std::vector<std::uint8_t>& Paint() const { return m_paint; }

    // Colour for each paint layer (1..5); index 0 is ignored. The editor sets these
    // from assigned materials (falling back to the default palette). Rebuilds the
    // albedo only if the colours actually changed.
    void SetLayerColors(const glm::vec3 colors[6]);
    void SetLayerSurfaces(const TerrainLayerSurface surfaces[6]);
    void SetLayerTextures(const TerrainLayerTexture textures[6]);

    float HeightAt(float x, float z) const { return m_hm.HeightAt(x, z); }
    const Heightmap& Map() const { return m_hm; }

    bool HasMesh()   const { return m_mesh.Valid(); }
    bool HasAlbedo() const { return m_albedo.has_value(); }
    bool HasSurfaceMap() const { return m_surfaceMap.has_value(); }
    bool Ready()     const { return HasMesh() && HasAlbedo() && HasSurfaceMap(); }
    const Mesh&    GetMesh() const { return m_mesh; }        // check Ready() first
    const Texture& Albedo()  const { return *m_albedo; }     // check Ready() first
    const Texture& SurfaceMap() const { return *m_surfaceMap; }

private:
    void Rebuild();          // rebuild mesh + albedo from m_hm (+ m_paint)
    void RebuildAlbedo();    // rebuild only the albedo texture

    Heightmap                 m_hm;
    std::vector<std::uint8_t> m_paint;   // res*res; 0 = auto
    Mesh                      m_mesh;    // persistent; uploaded in place
    std::optional<Texture>    m_albedo;
    std::optional<Texture>    m_surfaceMap;
    TerrainLayerSurface      m_layerSurfaces[6] = {
        {{0.0f, 0.0f, 0.0f}, 1.0f, 0.90f, 0.0f},
        {{0.30f, 0.50f, 0.20f}, 1.0f, 0.92f, 0.0f},
        {{0.50f, 0.48f, 0.45f}, 1.0f, 0.78f, 0.0f},
        {{0.45f, 0.33f, 0.20f}, 1.0f, 0.96f, 0.0f},
        {{0.90f, 0.92f, 0.95f}, 1.0f, 0.72f, 0.0f},
        {{0.80f, 0.72f, 0.50f}, 1.0f, 0.94f, 0.0f},
    };
    TerrainLayerTexture      m_layerTextures[6];
};

} // namespace engine
