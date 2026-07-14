#include "engine/graphics/Terrain.h"

#include "engine/graphics/VertexLayout.h"

#include <algorithm>
#include <cmath>

namespace engine {
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
                       float maxHeight, unsigned seed) {
    m_hm = GenerateFbmHeightmap(res, size, origin, maxHeight, seed);
    std::vector<float> vv; std::vector<std::uint32_t> ii;
    BuildTerrainMeshData(m_hm, vv, ii);
    m_mesh.emplace(vv, ii, VertexLayout{ {3}, {3}, {2} });
    const int texRes = 256;
    const std::vector<unsigned char> px = BuildTerrainAlbedo(m_hm, texRes);
    m_albedo.emplace(px.data(), texRes, texRes);
}

} // namespace engine
