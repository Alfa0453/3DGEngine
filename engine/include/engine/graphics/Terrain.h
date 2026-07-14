#pragma once

#include "engine/graphics/Mesh.h"
#include "engine/graphics/Texture.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace engine {

// A square grid of heights sampled over a world-space area. Pure data: generation
// and the HeightAt query are CPU-only (no GL), so they're unit-testable.
struct Heightmap {
    int       res  = 128;                 // vertices per side
    float     size = 64.0f;               // world extent (square)
    glm::vec3 origin{0.0f};               // world position of the (0,0) corner
    float     maxHeight = 8.0f;
    std::vector<float> h;                 // res*res heights (row-major, [0,maxHeight])

    float At(int i, int j) const;                 // clamped grid sample
    float HeightAt(float worldX, float worldZ) const;  // bilinear world query
    glm::vec3 NormalAt(int i, int j) const;       // finite-difference normal
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

// Build an RGBA albedo image (texRes x texRes) coloured by height + slope:
// grass low/flat, rock on steep slopes, snow on the peaks.
std::vector<unsigned char> BuildTerrainAlbedo(const Heightmap& hm, int texRes = 256);

// A renderable terrain: owns the generated Mesh + albedo Texture and keeps the
// Heightmap for queries. Generate() needs a live GL context.
class Terrain {
public:
    void Generate(int res, float size, const glm::vec3& origin,
                  float maxHeight, unsigned seed);

    float HeightAt(float x, float z) const { return m_hm.HeightAt(x, z); }
    const Heightmap& Map()   const { return m_hm; }
    const engine::Mesh& GetMesh() const { return *m_mesh; }
    const Texture&   Albedo() const { return *m_albedo; }

private:
    Heightmap m_hm;
    std::optional<engine::Mesh> m_mesh;
    std::optional<Texture>      m_albedo;
};

} // namespace engine