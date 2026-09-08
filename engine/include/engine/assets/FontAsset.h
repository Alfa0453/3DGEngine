#pragma once

// Font system upgrade -- the cooked ".3dgfont" native asset (Phases 2/4/5).
//
// A FontAsset is a project-owned, cooked font: glyph metrics + a baked coverage atlas + font-wide
// metrics, produced once by the TTF importer and reused at runtime. It follows the engine's
// NativeAssetHeader convention (stable AssetHandle identity, container/asset versions, dependency
// list) exactly like every other .3dg* asset -- no second asset database. It NEVER stores GL texture
// ids or absolute OS paths; the atlas is stored as raw R8 coverage bytes so the runtime can rebuild
// the GPU texture deterministically, and the source .ttf is referenced as a tracked dependency.
//
// Header-only, GL-free. Save/Load are stream-based so they are unit-testable without a filesystem.

#include "engine/assets/AssetIdentity.h"

#include <cstdint>
#include <istream>
#include <ostream>
#include <string>
#include <vector>

namespace engine {

// One baked glyph. All metrics are in pixels at the atlas bake size; the runtime scales by
// requestedSize / bakeSize. UVs are normalized into the atlas [0,1].
struct FontGlyph {
    std::uint32_t codepoint = 0;
    float advance  = 0.0f;   // pen advance to the next glyph
    float bearingX = 0.0f;   // left side bearing
    float bearingY = 0.0f;   // top bearing (baseline to glyph top)
    float width    = 0.0f;   // glyph quad size (px)
    float height   = 0.0f;
    float u0 = 0.0f, v0 = 0.0f, u1 = 0.0f, v1 = 0.0f;   // atlas UV rect
};

struct FontAsset {
    static constexpr std::uint32_t kFontAssetVersion = 1;
    static constexpr char kMagic[8] = { '3','D','G','F','O','N','T','\0' };

    NativeAssetHeader header;          // type == AssetType::Font; header.dependencies holds the .ttf
    std::string   sourcePath;          // project-relative source .ttf (informational; identity is the dep handle)
    float         bakeSize   = 48.0f;  // pixel height the atlas was baked at (default bake size)
    // Font-wide vertical metrics (px at bakeSize) -- real TrueType metrics for proportional layout.
    float         ascent  = 0.0f;
    float         descent = 0.0f;      // stored as a positive distance below baseline
    float         lineGap = 0.0f;
    std::uint32_t atlasWidth  = 0;
    std::uint32_t atlasHeight = 0;
    std::vector<FontGlyph>    glyphs;      // sorted by codepoint for binary search
    std::vector<std::uint8_t> atlas;       // R8 coverage, atlasWidth*atlasHeight bytes (cooked; Phase 4)

    float LineHeight() const { return ascent + descent + lineGap; }

    // Locate a glyph by codepoint (binary search over the sorted table). Null when absent -- callers
    // fall back to the replacement glyph (U+FFFD) then '?'.
    const FontGlyph* Find(std::uint32_t codepoint) const {
        std::size_t lo = 0, hi = glyphs.size();
        while (lo < hi) {
            const std::size_t mid = (lo + hi) / 2;
            if (glyphs[mid].codepoint < codepoint) lo = mid + 1;
            else hi = mid;
        }
        if (lo < glyphs.size() && glyphs[lo].codepoint == codepoint) return &glyphs[lo];
        return nullptr;
    }
    // The glyph used to render an unsupported codepoint: U+FFFD if baked, else '?', else nullptr.
    const FontGlyph* Replacement() const {
        if (const FontGlyph* g = Find(0xFFFD)) return g;
        return Find('?');
    }

    // Serialize header + payload. `error` is filled on failure. No GL ids, no absolute paths.
    bool Save(std::ostream& os, std::string* error = nullptr) const {
        NativeAssetHeader h = header;
        h.type = AssetType::Font;
        h.assetVersion = kFontAssetVersion;
        if (!WriteNativeAssetHeader(os, h, error)) return false;
        os.write(kMagic, sizeof(kMagic));
        WriteString(os, sourcePath);
        WriteF(os, bakeSize); WriteF(os, ascent); WriteF(os, descent); WriteF(os, lineGap);
        WriteU32(os, atlasWidth); WriteU32(os, atlasHeight);
        WriteU32(os, static_cast<std::uint32_t>(glyphs.size()));
        for (const FontGlyph& g : glyphs) {
            WriteU32(os, g.codepoint);
            WriteF(os, g.advance); WriteF(os, g.bearingX); WriteF(os, g.bearingY);
            WriteF(os, g.width); WriteF(os, g.height);
            WriteF(os, g.u0); WriteF(os, g.v0); WriteF(os, g.u1); WriteF(os, g.v1);
        }
        WriteU32(os, static_cast<std::uint32_t>(atlas.size()));
        if (!atlas.empty()) os.write(reinterpret_cast<const char*>(atlas.data()),
                                     static_cast<std::streamsize>(atlas.size()));
        return static_cast<bool>(os);
    }

    bool Load(std::istream& is, std::string* error = nullptr) {
        if (!ReadNativeAssetHeader(is, &header, error)) return false;
        if (header.type != AssetType::Font) { SetErr(error, "not a Font asset"); return false; }
        char magic[8] = {};
        is.read(magic, sizeof(magic));
        for (int i = 0; i < 8; ++i) if (magic[i] != kMagic[i]) { SetErr(error, "bad .3dgfont magic"); return false; }
        sourcePath = ReadString(is);
        bakeSize = ReadF(is); ascent = ReadF(is); descent = ReadF(is); lineGap = ReadF(is);
        atlasWidth = ReadU32(is); atlasHeight = ReadU32(is);
        const std::uint32_t n = ReadU32(is);
        glyphs.clear(); glyphs.reserve(n);
        for (std::uint32_t i = 0; i < n; ++i) {
            FontGlyph g;
            g.codepoint = ReadU32(is);
            g.advance = ReadF(is); g.bearingX = ReadF(is); g.bearingY = ReadF(is);
            g.width = ReadF(is); g.height = ReadF(is);
            g.u0 = ReadF(is); g.v0 = ReadF(is); g.u1 = ReadF(is); g.v1 = ReadF(is);
            glyphs.push_back(g);
        }
        const std::uint32_t bytes = ReadU32(is);
        atlas.resize(bytes);
        if (bytes) is.read(reinterpret_cast<char*>(atlas.data()), static_cast<std::streamsize>(bytes));
        return static_cast<bool>(is);
    }

private:
    static void SetErr(std::string* e, const char* m) { if (e) *e = m; }
    static void WriteU32(std::ostream& os, std::uint32_t v) { os.write(reinterpret_cast<const char*>(&v), 4); }
    static void WriteF(std::ostream& os, float v) { os.write(reinterpret_cast<const char*>(&v), 4); }
    static void WriteString(std::ostream& os, const std::string& s) {
        WriteU32(os, static_cast<std::uint32_t>(s.size()));
        if (!s.empty()) os.write(s.data(), static_cast<std::streamsize>(s.size()));
    }
    static std::uint32_t ReadU32(std::istream& is) { std::uint32_t v = 0; is.read(reinterpret_cast<char*>(&v), 4); return v; }
    static float ReadF(std::istream& is) { float v = 0; is.read(reinterpret_cast<char*>(&v), 4); return v; }
    static std::string ReadString(std::istream& is) {
        const std::uint32_t n = ReadU32(is); std::string s; s.resize(n);
        if (n) is.read(&s[0], static_cast<std::streamsize>(n));
        return s;
    }
};

} // namespace engine
