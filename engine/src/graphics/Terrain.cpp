#include "engine/graphics/Terrain.h"

#include "engine/graphics/VertexLayout.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>

namespace engine {

// TEMPORARY terrain crash tracing. Writes to its OWN file (terrain_engine.log) so it
// can never collide with the editor's separately-held terrain_debug.log handle. If this
// file stays empty after a crash while terrain_debug.log grows, the engine library was
// NOT rebuilt -- do a full/clean rebuild so this translation unit is recompiled.
static void TerrainTrace(const char* fmt, ...) {
    static std::FILE* file = std::fopen("D:/C++_Projects/3DGEngine/terrain_engine.log", "a");
    if (!file) return;
    std::va_list args;
    va_start(args, fmt);
    std::vfprintf(file, fmt, args);
    va_end(args);
    std::fputc('\n', file);
    std::fflush(file);
}

namespace {

// Hash-based value noise. Deterministic 2D integer hash -> [0,1).
float Hash2(int x, int y, unsigned seed) {
    unsigned h = static_cast<unsigned>(x) * 374761393u + static_cast<unsigned>(y) * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return static_cast<float>(h & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}
float Smooth(float t) { return t * t * (3.0f - 2.0f * t); }   // smoothstep

// Value noise at (x,y) in "cell" units.
float ValueNoise(float x, float y, unsigned seed) {
    const int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
    const float fx = Smooth(x - x0), fy = Smooth(y - y0);
    const float v00 = Hash2(x0, y0, seed),     v10 = Hash2(x0 + 1, y0, seed);
    const float v01 = Hash2(x0, y0 + 1, seed), v11 = Hash2(x0 + 1, y0 + 1, seed);
    const float a = v00 + (v10 - v00) * fx;
    const float b = v01 + (v11 - v01) * fx;
    return a + (b - a) * fy;
}

} // namespace

float Heightmap::At(int i, int j) const {
    i = std::clamp(i, 0, res - 1);
    j = std::clamp(j, 0, res - 1);
    return h[static_cast<std::size_t>(j) * res + i];
}

float Heightmap::HeightAt(float worldX, float worldZ) const {
    // World -> grid coordinates.
    const float gx = (worldX - origin.x) / size * (res - 1);
    const float gz = (worldZ - origin.z) / size * (res - 1);
    const int i0 = static_cast<int>(std::floor(gx)), j0 = static_cast<int>(std::floor(gz));
    const float fx = gx - i0, fz = gz - j0;
    const float h00 = At(i0, j0),     h10 = At(i0 + 1, j0);
    const float h01 = At(i0, j0 + 1), h11 = At(i0 + 1, j0 + 1);
    const float a = h00 + (h10 - h00) * fx;
    const float b = h01 + (h11 - h01) * fx;
    return origin.y + a + (b - a) * fz;
}

glm::vec3 Heightmap::NormalAt(int i, int j) const {
    const float cell = size / (res - 1);
    const float hl = At(i - 1, j), hr = At(i + 1, j);
    const float hd = At(i, j - 1), hu = At(i, j + 1);
    return glm::normalize(glm::vec3(hl - hr, 2.0f * cell, hd - hu));
}

Heightmap GenerateFbmHeightmap(int res, float size, const glm::vec3& origin,
                               float maxHeight, unsigned seed, int octaves, float baseFrequency) {
    Heightmap hm;
    hm.res = std::max(2, res);
    hm.size = size;
    hm.origin = origin;
    hm.maxHeight = maxHeight;
    hm.h.assign(static_cast<std::size_t>(hm.res) * hm.res, 0.0f);

    for (int j = 0; j < hm.res; ++j) {
        for (int i = 0; i < hm.res; ++i) {
            const float u = static_cast<float>(i) / (hm.res - 1);
            const float v = static_cast<float>(j) / (hm.res - 1);
            float amp = 1.0f, freq = baseFrequency, sum = 0.0f, norm = 0.0f;
            for (int o = 0; o < octaves; ++o) {
                sum  += amp * ValueNoise(u * freq, v * freq, seed + static_cast<unsigned>(o) * 101u);
                norm += amp;
                amp  *= 0.5f;
                freq *= 2.0f;
            }
            float e = (norm > 0.0f) ? sum / norm : 0.0f;    // [0,1]
            e = std::pow(e, 1.6f);                            // flatten valleys, sharpen peaks
            hm.h[static_cast<std::size_t>(j) * hm.res + i] = e * maxHeight;
        }
    }
    return hm;
}

void BuildTerrainMeshData(const Heightmap& hm,
                          std::vector<float>& vv, std::vector<std::uint32_t>& ii) {
    vv.clear(); ii.clear();
    const int R = hm.res;
    const float cell = hm.size / (R - 1);
    vv.reserve(static_cast<std::size_t>(R) * R * 8);
    for (int j = 0; j < R; ++j) {
        for (int i = 0; i < R; ++i) {
            const float x = hm.origin.x + i * cell;
            const float z = hm.origin.z + j * cell;
            const float y = hm.origin.y + hm.At(i, j);
            const glm::vec3 n = hm.NormalAt(i, j);
            vv.insert(vv.end(), { x, y, z, n.x, n.y, n.z,
                                  static_cast<float>(i) / (R - 1), static_cast<float>(j) / (R - 1) });
        }
    }
    for (int j = 0; j < R - 1; ++j) {
        for (int i = 0; i < R - 1; ++i) {
            const std::uint32_t a = static_cast<std::uint32_t>(j) * R + i;
            const std::uint32_t b = a + 1, c = a + R, d = c + 1;
            ii.insert(ii.end(), { a, c, b, b, c, d });
        }
    }
}

std::vector<unsigned char> BuildTerrainAlbedo(const Heightmap& hm, int texRes) {
    std::vector<unsigned char> px(static_cast<std::size_t>(texRes) * texRes * 4, 255);
    const glm::vec3 grass(0.28f, 0.42f, 0.20f), rock(0.42f, 0.40f, 0.38f), snow(0.92f, 0.94f, 0.98f);
    const glm::vec3 sand(0.55f, 0.52f, 0.36f);
    for (int ty = 0; ty < texRes; ++ty) {
        for (int tx = 0; tx < texRes; ++tx) {
            const int i = tx * (hm.res - 1) / std::max(1, texRes - 1);
            const int j = ty * (hm.res - 1) / std::max(1, texRes - 1);
            const float hf = (hm.maxHeight > 0.0f) ? hm.At(i, j) / hm.maxHeight : 0.0f;
            const float slope = 1.0f - hm.NormalAt(i, j).y;           // 0 flat .. 1 vertical
            glm::vec3 c = glm::mix(grass, snow, glm::smoothstep(0.55f, 0.85f, hf));
            c = glm::mix(c, sand, glm::smoothstep(0.10f, 0.02f, hf));  // low = sandy
            c = glm::mix(c, rock, glm::smoothstep(0.35f, 0.6f, slope)); // steep = rock
            const std::size_t o = (static_cast<std::size_t>(ty) * texRes + tx) * 4;
            px[o + 0] = static_cast<unsigned char>(glm::clamp(c.r, 0.0f, 1.0f) * 255.0f);
            px[o + 1] = static_cast<unsigned char>(glm::clamp(c.g, 0.0f, 1.0f) * 255.0f);
            px[o + 2] = static_cast<unsigned char>(glm::clamp(c.b, 0.0f, 1.0f) * 255.0f);
            px[o + 3] = 255;
        }
    }
    return px;
}

void Terrain::Generate(int res, float size, const glm::vec3& origin,
                       float maxHeight, unsigned seed, int octaves, float baseFrequency) {
    TerrainTrace("  Terrain::Generate: fbm... res=%d oct=%d freq=%.3f", res, octaves, baseFrequency);
    Heightmap hm = GenerateFbmHeightmap(res, size, origin, maxHeight, seed, octaves, baseFrequency);
    TerrainTrace("  Terrain::Generate: fbm done h=%zu", hm.h.size());
    SetHeightmap(hm);
    TerrainTrace("  Terrain::Generate: SetHeightmap returned");
}

void Terrain::SetHeightmap(const Heightmap& hm) {
    const bool sameTopology = m_mesh.has_value() && m_hm.res == hm.res;   // m_hm is still the old one
    TerrainTrace("  SetHeightmap: begin sameTopology=%d oldRes=%d newRes=%d", sameTopology ? 1 : 0, m_hm.res, hm.res);
    m_hm = hm;
    std::vector<float> vv; std::vector<std::uint32_t> ii;
    BuildTerrainMeshData(m_hm, vv, ii);
    TerrainTrace("  SetHeightmap: meshData vv=%zu ii=%zu", vv.size(), ii.size());
    if (sameTopology) {
        m_mesh->UpdateVertices(vv);   // in-place VBO update (indices/topology unchanged)
        TerrainTrace("  SetHeightmap: UpdateVertices done");
    } else {
        m_mesh.emplace(vv, ii, VertexLayout{ {3}, {3}, {2} });
        TerrainTrace("  SetHeightmap: mesh emplace done");
    }
    RebuildAlbedo();
    TerrainTrace("  SetHeightmap: RebuildAlbedo done");
}

void Terrain::SetPaint(const std::vector<std::uint8_t>& paint) {
    m_paint = paint;
    RebuildAlbedo();
}

void Terrain::RebuildAlbedo() {
    const int texRes = 256;
    std::vector<unsigned char> px = BuildTerrainAlbedo(m_hm, texRes);   // height/slope base
    const int res = m_hm.res;
    if (!m_paint.empty() && static_cast<int>(m_paint.size()) == res * res && res > 1) {
        // Layer colours (index 1..5). Overlaid where the nearest vertex is painted.
        static const glm::vec3 kLayer[6] = {
            {0.0f, 0.0f, 0.0f},           // 0 unused (auto)
            {0.30f, 0.50f, 0.20f},        // 1 grass
            {0.50f, 0.48f, 0.45f},        // 2 rock
            {0.45f, 0.33f, 0.20f},        // 3 dirt
            {0.90f, 0.92f, 0.95f},        // 4 snow
            {0.80f, 0.72f, 0.50f},        // 5 sand
        };
        for (int ty = 0; ty < texRes; ++ty) {
            const int j = ty * (res - 1) / (texRes - 1);
            for (int tx = 0; tx < texRes; ++tx) {
                const int i = tx * (res - 1) / (texRes - 1);
                const std::uint8_t layer = m_paint[static_cast<std::size_t>(j) * res + i];
                if (layer == 0 || layer > 5) continue;
                const glm::vec3& c = kLayer[layer];
                const std::size_t idx = (static_cast<std::size_t>(ty) * texRes + tx) * 4;
                px[idx + 0] = static_cast<unsigned char>(c.r * 255.0f);
                px[idx + 1] = static_cast<unsigned char>(c.g * 255.0f);
                px[idx + 2] = static_cast<unsigned char>(c.b * 255.0f);
                px[idx + 3] = 255;
            }
        }
    }
    if (m_albedo.has_value() && m_albedo->Width() == texRes && m_albedo->Height() == texRes) {
        m_albedo->Update(px.data(), texRes, texRes);   // in-place, no new GL texture
    } else {
        m_albedo.emplace(px.data(), texRes, texRes);
    }
}

} // namespace engine
