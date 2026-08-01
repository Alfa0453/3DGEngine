#include "engine/graphics/Terrain.h"

#include "engine/graphics/VertexLayout.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace engine {

namespace {

// Hash-based value noise. Deterministic 2D integer hash -> [0,1).
float Hash2(int x, int y, unsigned seed) {
    unsigned h = static_cast<unsigned>(x) * 374761393u + static_cast<unsigned>(y) * 668265263u
               + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return static_cast<float>(h & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}
float Smooth(float t) { return t * t * (3.0f - 2.0f * t); }   // smoothstep

float ValueNoise(float x, float y, unsigned seed) {
    const int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
    const float fx = Smooth(x - static_cast<float>(x0)), fy = Smooth(y - static_cast<float>(y0));
    const float v00 = Hash2(x0, y0, seed),     v10 = Hash2(x0 + 1, y0, seed);
    const float v01 = Hash2(x0, y0 + 1, seed), v11 = Hash2(x0 + 1, y0 + 1, seed);
    const float a = v00 + (v10 - v00) * fx;
    const float b = v01 + (v11 - v01) * fx;
    return a + (b - a) * fy;
}

const engine::VertexLayout& TerrainLayout() {
    static const engine::VertexLayout layout{{3}, {3}, {2}};   // pos, normal, uv
    return layout;
}

void BuildTerrainVertices(const Heightmap& hm, std::vector<float>& vertices) {
    vertices.clear();
    const int R = hm.res;
    if (R < 2 || hm.h.empty()) return;
    const float cell = hm.size / static_cast<float>(R - 1);
    vertices.reserve(static_cast<std::size_t>(R) * R * 8u);
    for (int j = 0; j < R; ++j) {
        for (int i = 0; i < R; ++i) {
            const float x = hm.origin.x + static_cast<float>(i) * cell;
            const float z = hm.origin.z + static_cast<float>(j) * cell;
            const float y = hm.origin.y + hm.At(i, j);
            const glm::vec3 n = hm.NormalAt(i, j);
            vertices.insert(vertices.end(), {x, y, z, n.x, n.y, n.z,
                static_cast<float>(i) / static_cast<float>(R - 1),
                static_cast<float>(j) / static_cast<float>(R - 1)});
        }
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Heightmap
// ---------------------------------------------------------------------------

float Heightmap::At(int i, int j) const {
    if (res < 1 || h.empty()) return 0.0f;
    i = std::clamp(i, 0, res - 1);
    j = std::clamp(j, 0, res - 1);
    return h[static_cast<std::size_t>(j) * res + i];
}

float Heightmap::HeightAt(float worldX, float worldZ) const {
    if (res < 2 || h.empty() || size <= 0.0f) return origin.y;
    const float gx = (worldX - origin.x) / size * static_cast<float>(res - 1);
    const float gz = (worldZ - origin.z) / size * static_cast<float>(res - 1);
    const int i0 = static_cast<int>(std::floor(gx)), j0 = static_cast<int>(std::floor(gz));
    const float fx = gx - static_cast<float>(i0), fz = gz - static_cast<float>(j0);
    const float h00 = At(i0, j0),     h10 = At(i0 + 1, j0);
    const float h01 = At(i0, j0 + 1), h11 = At(i0 + 1, j0 + 1);
    const float a = h00 + (h10 - h00) * fx;
    const float b = h01 + (h11 - h01) * fx;
    return origin.y + a + (b - a) * fz;
}

glm::vec3 Heightmap::NormalAt(int i, int j) const {
    if (res < 2) return glm::vec3(0.0f, 1.0f, 0.0f);
    const float cell = size / static_cast<float>(res - 1);
    const float hl = At(i - 1, j), hr = At(i + 1, j);
    const float hd = At(i, j - 1), hu = At(i, j + 1);
    return glm::normalize(glm::vec3(hl - hr, 2.0f * cell, hd - hu));
}

// ---------------------------------------------------------------------------
// Free builders (no GL)
// ---------------------------------------------------------------------------

Heightmap GenerateFbmHeightmap(int res, float size, const glm::vec3& origin,
                               float maxHeight, unsigned seed, int octaves, float baseFrequency) {
    Heightmap hm;
    hm.res = std::max(2, res);
    hm.size = std::max(size, 0.01f);
    hm.origin = origin;
    hm.maxHeight = std::max(maxHeight, 0.0f);
    hm.h.assign(static_cast<std::size_t>(hm.res) * hm.res, 0.0f);
    const int oct = std::max(1, octaves);
    const float denom = static_cast<float>(hm.res - 1);

    for (int j = 0; j < hm.res; ++j) {
        for (int i = 0; i < hm.res; ++i) {
            const float u = static_cast<float>(i) / denom;
            const float v = static_cast<float>(j) / denom;
            float amp = 1.0f, freq = baseFrequency, sum = 0.0f, norm = 0.0f;
            for (int o = 0; o < oct; ++o) {
                sum  += amp * ValueNoise(u * freq, v * freq, seed + static_cast<unsigned>(o) * 101u);
                norm += amp;
                amp  *= 0.5f;
                freq *= 2.0f;
            }
            float e = (norm > 0.0f) ? sum / norm : 0.0f;          // [0,1]
            e = std::clamp(e, 0.0f, 1.0f);
            e = std::pow(e, 1.6f);                                 // flatten valleys, sharpen peaks
            hm.h[static_cast<std::size_t>(j) * hm.res + i] = e * hm.maxHeight;
        }
    }
    return hm;
}

void BuildTerrainMeshData(const Heightmap& hm,
                          std::vector<float>& vv, std::vector<std::uint32_t>& ii) {
    vv.clear();
    ii.clear();
    const int R = hm.res;
    if (R < 2 || hm.h.empty()) return;
    BuildTerrainVertices(hm, vv);
    ii.reserve(static_cast<std::size_t>(R - 1) * (R - 1) * 6);
    for (int j = 0; j < R - 1; ++j) {
        for (int i = 0; i < R - 1; ++i) {
            const std::uint32_t a = static_cast<std::uint32_t>(j) * R + i;
            const std::uint32_t b = a + 1, c = a + R, d = c + 1;
            ii.insert(ii.end(), {a, c, b, b, c, d});
        }
    }
}

std::vector<std::vector<std::uint32_t>> BuildTerrainLodIndices(
    int resolution, int maxLevels) {
    std::vector<std::vector<std::uint32_t>> levels;
    const int R = std::max(resolution, 0);
    if (R < 3 || maxLevels <= 0) return levels;
    for (int level = 1; level <= maxLevels; ++level) {
        const int step = 1 << level;
        if (step >= R - 1) break;
        const int cells = (R - 2 + step) / step;
        std::vector<std::uint32_t> indices;
        indices.reserve(static_cast<std::size_t>(cells) * cells * 6u);
        for (int j = 0; j < R - 1; j += step) {
            const int j1 = std::min(j + step, R - 1);
            for (int i = 0; i < R - 1; i += step) {
                const int i1 = std::min(i + step, R - 1);
                const std::uint32_t a = static_cast<std::uint32_t>(j * R + i);
                const std::uint32_t b = static_cast<std::uint32_t>(j * R + i1);
                const std::uint32_t c = static_cast<std::uint32_t>(j1 * R + i);
                const std::uint32_t d = static_cast<std::uint32_t>(j1 * R + i1);
                indices.insert(indices.end(), {a, c, b, b, c, d});
            }
        }
        levels.push_back(std::move(indices));
    }
    return levels;
}

void DefaultTerrainLayerColors(glm::vec3 outColors[6]) {
    outColors[0] = glm::vec3(0.0f, 0.0f, 0.0f);
    outColors[1] = glm::vec3(0.30f, 0.50f, 0.20f);   // grass
    outColors[2] = glm::vec3(0.50f, 0.48f, 0.45f);   // rock
    outColors[3] = glm::vec3(0.45f, 0.33f, 0.20f);   // dirt
    outColors[4] = glm::vec3(0.90f, 0.92f, 0.95f);   // snow
    outColors[5] = glm::vec3(0.80f, 0.72f, 0.50f);   // sand
}

void DefaultTerrainLayerSurfaces(TerrainLayerSurface outSurfaces[6]) {
    glm::vec3 colors[6];
    DefaultTerrainLayerColors(colors);
    const float roughness[6] = {0.90f, 0.92f, 0.78f, 0.96f, 0.72f, 0.94f};
    for (int i = 0; i < 6; ++i) {
        outSurfaces[i].albedo = colors[i];
        outSurfaces[i].ao = 1.0f;
        outSurfaces[i].roughness = roughness[i];
        outSurfaces[i].metallic = 0.0f;
    }
}

glm::vec3 TerrainCameraConstraint::Resolve(const glm::vec3& desired,
                                           float surfaceY, bool overTerrain,
                                           float dt, float clearance,
                                           float releaseSpeed) {
    const float minimum = overTerrain
        ? surfaceY + std::max(clearance, 0.0f)
        : -std::numeric_limits<float>::infinity();
    const bool blocked = overTerrain && desired.y < minimum;
    const float target = blocked ? minimum : desired.y;

    if (!m_active && !blocked) return desired;
    if (!m_active) {
        m_height = target;
        m_active = true;
    } else if (target >= m_height) {
        // Never interpolate upward through the terrain.
        m_height = target;
    } else {
        const float alpha = releaseSpeed > 0.0f
            ? 1.0f - std::exp(-releaseSpeed * std::max(dt, 0.0f))
            : 1.0f;
        m_height += (target - m_height) * alpha;
    }

    glm::vec3 resolved = desired;
    resolved.y = overTerrain ? std::max(m_height, minimum) : m_height;
    if (!blocked && std::abs(resolved.y - desired.y) < 0.002f) {
        resolved.y = desired.y;
        m_active = false;
    }
    return resolved;
}

namespace {
TerrainLayerSurface MixSurface(const TerrainLayerSurface& a,
                               const TerrainLayerSurface& b, float t) {
    TerrainLayerSurface result;
    result.albedo = glm::mix(a.albedo, b.albedo, t);
    result.ao = glm::mix(a.ao, b.ao, t);
    result.roughness = glm::mix(a.roughness, b.roughness, t);
    result.metallic = glm::mix(a.metallic, b.metallic, t);
    return result;
}

void AccumulateSurface(TerrainLayerSurface& dst, float& total,
                       const TerrainLayerSurface& source, float weight) {
    if (weight <= 0.0f) return;
    dst.albedo += source.albedo * weight;
    dst.ao += source.ao * weight;
    dst.roughness += source.roughness * weight;
    dst.metallic += source.metallic * weight;
    total += weight;
}
} // namespace

void BuildTerrainSurfaceMaps(const Heightmap& hm,
                             const std::vector<std::uint8_t>& paint,
                             const TerrainLayerSurface layers[6],
                             std::vector<unsigned char>& albedo,
                             std::vector<unsigned char>& orm, int texRes) {
    texRes = std::max(texRes, 1);
    albedo.assign(static_cast<std::size_t>(texRes) * texRes * 4, 255);
    orm.assign(static_cast<std::size_t>(texRes) * texRes * 4, 255);
    const int res = hm.res;
    const bool havePaint = res > 1 && paint.size() == static_cast<std::size_t>(res) * res;
    const float texDenom = static_cast<float>(std::max(1, texRes - 1));

    for (int ty = 0; ty < texRes; ++ty) {
        for (int tx = 0; tx < texRes; ++tx) {
            const float gx = static_cast<float>(tx) / texDenom * std::max(res - 1, 0);
            const float gz = static_cast<float>(ty) / texDenom * std::max(res - 1, 0);
            const int i0 = static_cast<int>(std::floor(gx));
            const int j0 = static_cast<int>(std::floor(gz));
            const float fx = gx - i0, fz = gz - j0;
            const int ic = std::clamp(static_cast<int>(std::round(gx)), 0, std::max(res - 1, 0));
            const int jc = std::clamp(static_cast<int>(std::round(gz)), 0, std::max(res - 1, 0));
            const float hf = hm.maxHeight > 0.0f ? hm.At(ic, jc) / hm.maxHeight : 0.0f;
            const float slope = 1.0f - hm.NormalAt(ic, jc).y;

            TerrainLayerSurface surface = MixSurface(
                layers[1], layers[4], glm::smoothstep(0.55f, 0.85f, hf));
            surface = MixSurface(surface, layers[5], glm::smoothstep(0.10f, 0.02f, hf));
            surface = MixSurface(surface, layers[2], glm::smoothstep(0.35f, 0.60f, slope));

            if (havePaint) {
                TerrainLayerSurface painted{};
                painted.albedo = glm::vec3(0.0f);
                painted.ao = painted.roughness = painted.metallic = 0.0f;
                float paintedWeight = 0.0f;
                const int is[4] = {i0, i0 + 1, i0, i0 + 1};
                const int js[4] = {j0, j0, j0 + 1, j0 + 1};
                const float ws[4] = {(1.0f-fx)*(1.0f-fz), fx*(1.0f-fz),
                                     (1.0f-fx)*fz, fx*fz};
                for (int corner = 0; corner < 4; ++corner) {
                    const int i = std::clamp(is[corner], 0, res - 1);
                    const int j = std::clamp(js[corner], 0, res - 1);
                    const std::uint8_t layer = paint[static_cast<std::size_t>(j) * res + i];
                    if (layer >= 1 && layer <= 5)
                        AccumulateSurface(painted, paintedWeight, layers[layer], ws[corner]);
                }
                if (paintedWeight > 0.0001f) {
                    painted.albedo /= paintedWeight;
                    painted.ao /= paintedWeight;
                    painted.roughness /= paintedWeight;
                    painted.metallic /= paintedWeight;
                    surface = MixSurface(surface, painted, glm::clamp(paintedWeight, 0.0f, 1.0f));
                }
            }

            const std::size_t o = (static_cast<std::size_t>(ty) * texRes + tx) * 4;
            auto byte = [](float value) {
                return static_cast<unsigned char>(glm::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
            };
            albedo[o] = byte(surface.albedo.r); albedo[o + 1] = byte(surface.albedo.g);
            albedo[o + 2] = byte(surface.albedo.b); albedo[o + 3] = 255;
            orm[o] = byte(surface.ao); orm[o + 1] = byte(surface.roughness);
            orm[o + 2] = byte(surface.metallic); orm[o + 3] = 255;
        }
    }
}

std::vector<unsigned char> BuildTerrainAlbedo(const Heightmap& hm,
                                              const std::vector<std::uint8_t>& paint,
                                              const glm::vec3 layerColors[6], int texRes) {
    TerrainLayerSurface layers[6];
    DefaultTerrainLayerSurfaces(layers);
    for (int i = 0; i < 6; ++i) layers[i].albedo = layerColors[i];
    std::vector<unsigned char> albedo, orm;
    BuildTerrainSurfaceMaps(hm, paint, layers, albedo, orm, texRes);
    return albedo;
}

// ---------------------------------------------------------------------------
// Terrain (GL resources)
// ---------------------------------------------------------------------------

void Terrain::Generate(int res, float size, const glm::vec3& origin,
                       float maxHeight, unsigned seed, int octaves, float baseFrequency) {
    m_hm = GenerateFbmHeightmap(res, size, origin, maxHeight, seed, octaves, baseFrequency);
    Rebuild();
}

void Terrain::SetHeightmap(const Heightmap& hm) {
    const bool sameTopology = m_mesh.Valid()
        && m_hm.res == hm.res && m_hm.size == hm.size
        && m_hm.origin == hm.origin;
    m_hm = hm;
    if (m_hm.res < 2) return;
    // Paint sized for a different resolution no longer applies -- drop it.
    if (static_cast<int>(m_paint.size()) != m_hm.res * m_hm.res) m_paint.clear();
    if (sameTopology && !m_hm.h.empty()) {
        std::vector<float> vertices;
        BuildTerrainVertices(m_hm, vertices);
        m_mesh.UpdateVertices(vertices, TerrainLayout());
        RebuildAlbedo();
    } else {
        Rebuild();
    }
}

void Terrain::CommitHeightRegion(int minI, int minJ, int maxI, int maxJ) {
    const int R = m_hm.res;
    if (!m_mesh.Valid() || R < 2 || m_hm.h.empty()) return;
    minI = std::clamp(minI - 1, 0, R - 1);
    maxI = std::clamp(maxI + 1, 0, R - 1);
    minJ = std::clamp(minJ - 1, 0, R - 1);
    maxJ = std::clamp(maxJ + 1, 0, R - 1);
    if (minI > maxI || minJ > maxJ) return;

    const float cell = m_hm.size / static_cast<float>(R - 1);
    const int width = maxI - minI + 1;
    std::vector<float> row;
    row.reserve(static_cast<std::size_t>(width) * 8u);
    for (int j = minJ; j <= maxJ; ++j) {
        row.clear();
        for (int i = minI; i <= maxI; ++i) {
            const float x = m_hm.origin.x + static_cast<float>(i) * cell;
            const float z = m_hm.origin.z + static_cast<float>(j) * cell;
            const float y = m_hm.origin.y + m_hm.At(i, j);
            const glm::vec3 n = m_hm.NormalAt(i, j);
            row.insert(row.end(), {x, y, z, n.x, n.y, n.z,
                static_cast<float>(i) / static_cast<float>(R - 1),
                static_cast<float>(j) / static_cast<float>(R - 1)});
        }
        m_mesh.UpdateVertexRange(
            static_cast<std::size_t>(j) * R + minI, row);
    }
    const glm::vec3 half(m_hm.size * 0.5f, m_hm.maxHeight * 0.5f,
                         m_hm.size * 0.5f);
    m_mesh.SetBounds(m_hm.origin + half, glm::length(half));
    RebuildAlbedo();
}

void Terrain::SetPaint(const std::vector<std::uint8_t>& paint) {
    m_paint = paint;
    RebuildAlbedo();
}

void Terrain::Rebuild() {
    if (m_hm.res < 2 || m_hm.h.empty()) return;
    std::vector<float> vv;
    std::vector<std::uint32_t> ii;
    BuildTerrainMeshData(m_hm, vv, ii);
    if (vv.empty() || ii.empty()) return;
    m_mesh.Upload(vv, ii, TerrainLayout());   // in place: never destroys the VAO
    m_mesh.UploadLodIndices(BuildTerrainLodIndices(m_hm.res));
    const glm::vec3 half(m_hm.size * 0.5f, m_hm.maxHeight * 0.5f,
                         m_hm.size * 0.5f);
    m_mesh.SetBounds(m_hm.origin + half, glm::length(half));
    RebuildAlbedo();
}

void Terrain::SetLayerColors(const glm::vec3 colors[6]) {
    TerrainLayerSurface surfaces[6];
    for (int i = 0; i < 6; ++i) {
        surfaces[i] = m_layerSurfaces[i];
        surfaces[i].albedo = colors[i];
    }
    SetLayerSurfaces(surfaces);
}

void Terrain::SetLayerSurfaces(const TerrainLayerSurface surfaces[6]) {
    bool changed = false;
    for (int i = 0; i < 6; ++i) {
        const TerrainLayerSurface& a = m_layerSurfaces[i];
        const TerrainLayerSurface& b = surfaces[i];
        if (a.albedo != b.albedo || a.ao != b.ao || a.roughness != b.roughness
            || a.metallic != b.metallic) {
            m_layerSurfaces[i] = b;
            changed = true;
        }
    }
    if (changed && m_albedo.has_value()) {
        RebuildAlbedo();   // recolour with the new layer palette
    }
}

void Terrain::RebuildAlbedo() {
    const int texRes = 256;
    std::vector<unsigned char> px, orm;
    BuildTerrainSurfaceMaps(m_hm, m_paint, m_layerSurfaces, px, orm, texRes);
    if (m_albedo.has_value() && m_albedo->Width() == texRes && m_albedo->Height() == texRes) {
        m_albedo->Update(px.data(), texRes, texRes);   // in place, no new GL texture
    } else {
        m_albedo.emplace(px.data(), texRes, texRes);
    }
    if (m_surfaceMap.has_value() && m_surfaceMap->Width() == texRes
        && m_surfaceMap->Height() == texRes)
        m_surfaceMap->Update(orm.data(), texRes, texRes);
    else
        m_surfaceMap.emplace(orm.data(), texRes, texRes);
}

} // namespace engine
