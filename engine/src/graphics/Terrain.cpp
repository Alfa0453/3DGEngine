#include "engine/graphics/Terrain.h"

#include "engine/graphics/VertexLayout.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>

namespace engine {

// --- Terrain debug trace -----------------------------------------------------
// Writes to D:/C++_Projects/3DGEngine/terrain_trace.log, TRUNCATING it on the first
// call each run (mode "w") so the file only ever contains the current run -- no stale
// lines to confuse diagnosis. Every line is flushed immediately, so after a crash the
// LAST line is the last step that ran; the crash is between it and the next step.
// Remove once the terrain issue is resolved.
static void TTrace(const char* fmt, ...) {
    static std::FILE* file = [] {
        std::FILE* f = nullptr;
#ifdef _MSC_VER
        fopen_s(&f, "D:/C++_Projects/3DGEngine/terrain_trace.log", "w");
#else
        f = std::fopen("D:/C++_Projects/3DGEngine/terrain_trace.log", "w");
#endif
        return f;
    }();
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
    const float cell = hm.size / static_cast<float>(R - 1);
    vv.reserve(static_cast<std::size_t>(R) * R * 8);
    for (int j = 0; j < R; ++j) {
        for (int i = 0; i < R; ++i) {
            const float x = hm.origin.x + static_cast<float>(i) * cell;
            const float z = hm.origin.z + static_cast<float>(j) * cell;
            const float y = hm.origin.y + hm.At(i, j);
            const glm::vec3 n = hm.NormalAt(i, j);
            vv.insert(vv.end(), {x, y, z, n.x, n.y, n.z,
                                 static_cast<float>(i) / static_cast<float>(R - 1),
                                 static_cast<float>(j) / static_cast<float>(R - 1)});
        }
    }
    ii.reserve(static_cast<std::size_t>(R - 1) * (R - 1) * 6);
    for (int j = 0; j < R - 1; ++j) {
        for (int i = 0; i < R - 1; ++i) {
            const std::uint32_t a = static_cast<std::uint32_t>(j) * R + i;
            const std::uint32_t b = a + 1, c = a + R, d = c + 1;
            ii.insert(ii.end(), {a, c, b, b, c, d});
        }
    }
}

void DefaultTerrainLayerColors(glm::vec3 outColors[6]) {
    outColors[0] = glm::vec3(0.0f, 0.0f, 0.0f);
    outColors[1] = glm::vec3(0.30f, 0.50f, 0.20f);   // grass
    outColors[2] = glm::vec3(0.50f, 0.48f, 0.45f);   // rock
    outColors[3] = glm::vec3(0.45f, 0.33f, 0.20f);   // dirt
    outColors[4] = glm::vec3(0.90f, 0.92f, 0.95f);   // snow
    outColors[5] = glm::vec3(0.80f, 0.72f, 0.50f);   // sand
}

std::vector<unsigned char> BuildTerrainAlbedo(const Heightmap& hm,
                                              const std::vector<std::uint8_t>& paint,
                                              const glm::vec3 layerColors[6], int texRes) {
    std::vector<unsigned char> px(static_cast<std::size_t>(texRes) * texRes * 4, 255);
    const glm::vec3 grass(0.28f, 0.42f, 0.20f), rock(0.42f, 0.40f, 0.38f), snow(0.92f, 0.94f, 0.98f);
    const glm::vec3 sand(0.55f, 0.52f, 0.36f);
    const int res = hm.res;
    const bool havePaint = (static_cast<int>(paint.size()) == res * res) && res > 1;
    const int denom = std::max(1, texRes - 1);

    for (int ty = 0; ty < texRes; ++ty) {
        for (int tx = 0; tx < texRes; ++tx) {
            const int i = tx * (res - 1) / denom;
            const int j = ty * (res - 1) / denom;
            const float hf = (hm.maxHeight > 0.0f) ? hm.At(i, j) / hm.maxHeight : 0.0f;
            const float slope = 1.0f - hm.NormalAt(i, j).y;
            glm::vec3 c = glm::mix(grass, snow, glm::smoothstep(0.55f, 0.85f, hf));
            c = glm::mix(c, sand, glm::smoothstep(0.10f, 0.02f, hf));
            c = glm::mix(c, rock, glm::smoothstep(0.35f, 0.6f, slope));
            if (havePaint) {
                const std::uint8_t layer = paint[static_cast<std::size_t>(j) * res + i];
                if (layer >= 1 && layer <= 5) c = layerColors[layer];
            }
            const std::size_t o = (static_cast<std::size_t>(ty) * texRes + tx) * 4;
            px[o + 0] = static_cast<unsigned char>(glm::clamp(c.r, 0.0f, 1.0f) * 255.0f);
            px[o + 1] = static_cast<unsigned char>(glm::clamp(c.g, 0.0f, 1.0f) * 255.0f);
            px[o + 2] = static_cast<unsigned char>(glm::clamp(c.b, 0.0f, 1.0f) * 255.0f);
            px[o + 3] = 255;
        }
    }
    return px;
}

// ---------------------------------------------------------------------------
// Terrain (GL resources)
// ---------------------------------------------------------------------------

void Terrain::Generate(int res, float size, const glm::vec3& origin,
                       float maxHeight, unsigned seed, int octaves, float baseFrequency) {
    TTrace("Generate: begin res=%d size=%.2f maxH=%.2f seed=%u oct=%d freq=%.3f",
           res, size, maxHeight, seed, octaves, baseFrequency);
    m_hm = GenerateFbmHeightmap(res, size, origin, maxHeight, seed, octaves, baseFrequency);
    TTrace("Generate: fbm done res=%d h=%zu", m_hm.res, m_hm.h.size());
    Rebuild();
    TTrace("Generate: end (ready=%d)", Ready() ? 1 : 0);
}

void Terrain::SetHeightmap(const Heightmap& hm) {
    TTrace("SetHeightmap: begin res=%d h=%zu", hm.res, hm.h.size());
    m_hm = hm;
    if (m_hm.res < 2) { TTrace("SetHeightmap: skip res<2"); return; }
    // Paint sized for a different resolution no longer applies -- drop it.
    if (static_cast<int>(m_paint.size()) != m_hm.res * m_hm.res) m_paint.clear();
    Rebuild();
    TTrace("SetHeightmap: end (ready=%d)", Ready() ? 1 : 0);
}

void Terrain::SetPaint(const std::vector<std::uint8_t>& paint) {
    TTrace("SetPaint: begin size=%zu", paint.size());
    m_paint = paint;
    RebuildAlbedo();
    TTrace("SetPaint: end");
}

void Terrain::Rebuild() {
    TTrace("Rebuild: begin res=%d h=%zu", m_hm.res, m_hm.h.size());
    if (m_hm.res < 2 || m_hm.h.empty()) { TTrace("Rebuild: skip empty"); return; }
    std::vector<float> vv;
    std::vector<std::uint32_t> ii;
    BuildTerrainMeshData(m_hm, vv, ii);
    TTrace("Rebuild: meshData vv=%zu ii=%zu", vv.size(), ii.size());
    if (vv.empty() || ii.empty()) { TTrace("Rebuild: skip no geometry"); return; }
    TTrace("Rebuild: Upload... (vao=%u before)", m_mesh.Vao());
    m_mesh.Upload(vv, ii, TerrainLayout());   // in place: never destroys the VAO
    TTrace("Rebuild: Upload done (vao=%u indexCount=%u)", m_mesh.Vao(), m_mesh.IndexCount());
    RebuildAlbedo();
    TTrace("Rebuild: end");
}

void Terrain::SetLayerColors(const glm::vec3 colors[6]) {
    bool changed = false;
    for (int i = 0; i < 6; ++i) {
        if (m_layerColors[i] != colors[i]) { m_layerColors[i] = colors[i]; changed = true; }
    }
    if (changed && m_albedo.has_value()) {
        RebuildAlbedo();   // recolour with the new layer palette
    }
}

void Terrain::RebuildAlbedo() {
    const int texRes = 256;
    TTrace("RebuildAlbedo: begin (paint=%zu)", m_paint.size());
    std::vector<unsigned char> px = BuildTerrainAlbedo(m_hm, m_paint, m_layerColors, texRes);
    TTrace("RebuildAlbedo: albedo built px=%zu hasAlbedo=%d", px.size(), m_albedo.has_value() ? 1 : 0);
    if (m_albedo.has_value() && m_albedo->Width() == texRes && m_albedo->Height() == texRes) {
        TTrace("RebuildAlbedo: Update...");
        m_albedo->Update(px.data(), texRes, texRes);   // in place, no new GL texture
        TTrace("RebuildAlbedo: Update done");
    } else {
        TTrace("RebuildAlbedo: emplace...");
        m_albedo.emplace(px.data(), texRes, texRes);
        TTrace("RebuildAlbedo: emplace done");
    }
}

} // namespace engine
