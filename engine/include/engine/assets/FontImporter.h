#pragma once

// Font system upgrade -- TTF importer (Phases 3/4/12).
//
// Imports a .ttf source into a cooked FontAsset (.3dgfont) using the engine's own dependency-free,
// GL-free TrueType rasterizer (engine::truetype). The bake happens on the CPU, so importing needs no
// renderer and produces a portable atlas. Malformed fonts and unsupported .otf/CFF outlines (which
// have no `glyf` table) fail gracefully with a diagnostic rather than crashing. The result carries a
// ready-to-Save FontAsset whose atlas is R8 coverage (the alpha channel of the RGBA bake) -- no GL
// texture ids, no absolute paths. Header-only.

#include "engine/assets/FontAsset.h"
#include "engine/graphics/TrueType.h"

#include <algorithm>
#include <fstream>
#include <string>
#include <vector>

namespace engine {

struct FontImportSettings {
    float bakeSize = 48.0f;   // pixel height to rasterize at (the .3dgfont default bake size)
    // Glyph range is currently the rasterizer's default (Basic Latin 32..126); wider ranges are a
    // forward-compatible extension point (Phase 20) once the baker exposes a range parameter.
};

struct FontImportResult {
    bool        ok = false;
    std::string error;
    FontAsset   asset;
};

// Convert a CPU-baked TrueType face into a cooked FontAsset. `sourceDep` is the AssetHandle of the
// source .ttf (tracked as a dependency); `sourcePath` is stored for editor display only.
inline FontAsset FontAssetFromBaked(const truetype::BakedFont& baked,
                                    const AssetHandle& sourceDep, const std::string& sourcePath,
                                    float bakeSize) {
    FontAsset fa;
    fa.header.type = AssetType::Font;
    if (sourceDep.Valid()) fa.header.dependencies.push_back(sourceDep);
    fa.sourcePath = sourcePath;
    fa.bakeSize   = bakeSize;
    fa.ascent     = baked.ascent;                       // px above baseline (positive)
    fa.descent    = -baked.descent;                     // baked.descent is negative below baseline -> store positive
    fa.lineGap    = std::max(0.0f, baked.lineHeight - (fa.ascent + fa.descent));
    fa.atlasWidth  = static_cast<std::uint32_t>(baked.atlasW);
    fa.atlasHeight = static_cast<std::uint32_t>(baked.atlasH);

    // R8 coverage = alpha channel of the white RGBA bake (no GL texture id stored).
    const std::size_t px = static_cast<std::size_t>(baked.atlasW) * baked.atlasH;
    fa.atlas.resize(px);
    for (std::size_t i = 0; i < px; ++i) fa.atlas[i] = baked.atlasRGBA[i * 4 + 3];

    fa.glyphs.reserve(baked.glyphs.size());
    for (const auto& kv : baked.glyphs) {
        const truetype::Glyph& g = kv.second;
        FontGlyph fg;
        fg.codepoint = static_cast<std::uint32_t>(kv.first);
        fg.advance   = g.advance;
        fg.bearingX  = g.drawXOff;         // pen -> quad left
        fg.bearingY  = -g.drawYOff;        // baked drawYOff is negative above baseline -> positive bearing up
        fg.width     = static_cast<float>(g.w);
        fg.height    = static_cast<float>(g.h);
        fg.u0 = g.u0; fg.v0 = g.v0; fg.u1 = g.u1; fg.v1 = g.v1;
        fa.glyphs.push_back(fg);
    }
    std::sort(fa.glyphs.begin(), fa.glyphs.end(),
              [](const FontGlyph& a, const FontGlyph& b) { return a.codepoint < b.codepoint; });
    return fa;
}

inline FontImportResult ImportFontFromMemory(const unsigned char* data, std::size_t size,
                                             const FontImportSettings& settings,
                                             const AssetHandle& sourceDep,
                                             const std::string& sourcePath) {
    FontImportResult r;
    if (!data || size < 12) { r.error = "font file is empty or too small to be a valid font"; return r; }
    const int px = static_cast<int>(settings.bakeSize <= 0.0f ? 48.0f : settings.bakeSize);
    const truetype::BakedFont baked = truetype::BakeTrueTypeFontFromMemory(data, size, px);
    if (!baked.ok) {
        // The rasterizer requires TrueType `glyf` outlines; .otf/CFF fonts and malformed files fail here.
        r.error = "unsupported or malformed font: no TrueType glyph outlines were found "
                  "(.otf / CFF outline fonts are not supported -- import a .ttf)";
        return r;
    }
    if (baked.glyphs.empty()) { r.error = "font imported but produced no glyphs"; return r; }
    r.asset = FontAssetFromBaked(baked, sourceDep, sourcePath, settings.bakeSize);
    r.ok = true;
    return r;
}

inline FontImportResult ImportFontFromFile(const std::string& ttfPath,
                                           const FontImportSettings& settings,
                                           const AssetHandle& sourceDep = {},
                                           const std::string& projectRelativePath = {}) {
    std::ifstream in(ttfPath, std::ios::binary);
    if (!in) { FontImportResult r; r.error = "could not open font file: " + ttfPath; return r; }
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)),
                                     std::istreambuf_iterator<char>());
    return ImportFontFromMemory(bytes.data(), bytes.size(), settings, sourceDep,
                                projectRelativePath.empty() ? ttfPath : projectRelativePath);
}

} // namespace engine
