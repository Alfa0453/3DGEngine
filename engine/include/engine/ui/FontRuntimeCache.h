#pragma once

// Font system upgrade -- runtime font resource + cache (Phases 5/6/27/34).
//
// A RuntimeFont pairs a stable font AssetHandle with the TRANSIENT renderer font id that
// TextRenderer::LoadFont returned. That id is a per-process handle -- it is NEVER serialized into
// .hud / scene / prefab / .3dgfont (only the AssetHandle is). The FontRuntimeCache keeps one
// RuntimeFont per (AssetHandle, size bucket), so many widgets sharing a font reuse a single baked
// atlas instead of re-baking/uploading per widget or per frame. Reimporting a font Invalidate()s its
// entries so all referencing widgets pick up the rebuild without an editor restart. Header-only,
// GL-free (the renderer id is an opaque int).

#include "engine/assets/AssetIdentity.h"

#include <cstdint>
#include <unordered_map>

namespace engine {

struct RuntimeFont {
    AssetHandle   asset;                 // stable identity (the serialized reference)
    int           rendererFontId = 0;    // TRANSIENT TextRenderer id (0 = built-in); never serialized
    float         bakeSize = 0.0f;       // px height this atlas was baked at
    float         ascent = 0.0f, descent = 0.0f, lineGap = 0.0f;
    std::uint32_t atlasWidth = 0, atlasHeight = 0;
    std::uint32_t glyphCount = 0;
    bool          valid = false;

    float LineHeight() const { return ascent + descent + lineGap; }
    std::size_t AtlasBytes() const { return static_cast<std::size_t>(atlasWidth) * atlasHeight; }
};

struct FontCacheStats {
    std::uint32_t fontsLoaded = 0;   // distinct (font,size) atlases currently resident
    std::uint64_t cacheHits = 0;
    std::uint64_t cacheMisses = 0;
    std::size_t   atlasBytes = 0;    // total resident atlas memory (R8 coverage)
};

class FontRuntimeCache {
public:
    // Look up an already-baked runtime font. Returns nullptr on a miss (the caller then bakes via the
    // RuntimeAssetManager and calls Insert). `sizeBucket` distinguishes atlases baked at different
    // sizes for the same font (0 = the font's default bake size).
    const RuntimeFont* Get(const AssetHandle& asset, int sizeBucket = 0) {
        const Key k{asset, sizeBucket};
        auto it = m_fonts.find(k);
        if (it != m_fonts.end()) { ++m_stats.cacheHits; return &it->second; }
        ++m_stats.cacheMisses;
        return nullptr;
    }

    // Store a freshly baked runtime font; returns the stored reference (stable until Invalidate/Clear).
    RuntimeFont& Insert(const RuntimeFont& font, int sizeBucket = 0) {
        const Key k{font.asset, sizeBucket};
        auto it = m_fonts.find(k);
        if (it == m_fonts.end()) { ++m_stats.fontsLoaded; m_stats.atlasBytes += font.AtlasBytes(); }
        else { m_stats.atlasBytes = m_stats.atlasBytes - it->second.AtlasBytes() + font.AtlasBytes(); }
        m_fonts[k] = font;
        return m_fonts[k];
    }

    // Drop every size bucket of one font (Phase 27: reimport/hot reload). Widgets referencing the
    // AssetHandle will miss on their next Get and rebake against the new .3dgfont.
    int Invalidate(const AssetHandle& asset) {
        int removed = 0;
        for (auto it = m_fonts.begin(); it != m_fonts.end();) {
            if (it->first.asset == asset) {
                m_stats.atlasBytes -= it->second.AtlasBytes();
                if (m_stats.fontsLoaded) --m_stats.fontsLoaded;
                it = m_fonts.erase(it); ++removed;
            } else ++it;
        }
        return removed;
    }

    void Clear() { m_fonts.clear(); m_stats = FontCacheStats{}; }
    std::size_t Size() const { return m_fonts.size(); }
    const FontCacheStats& Stats() const { return m_stats; }

private:
    struct Key {
        AssetHandle asset; int sizeBucket = 0;
        bool operator==(const Key& o) const { return asset == o.asset && sizeBucket == o.sizeBucket; }
    };
    struct KeyHash {
        std::size_t operator()(const Key& k) const noexcept {
            return AssetHandleHash{}(k.asset) ^ (static_cast<std::size_t>(k.sizeBucket) * 0x9E3779B9u);
        }
    };
    std::unordered_map<Key, RuntimeFont, KeyHash> m_fonts;
    FontCacheStats m_stats;
};

} // namespace engine
