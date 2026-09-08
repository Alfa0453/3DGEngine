#pragma once

// Font system upgrade -- pure HUD font decision logic (Phases 9/15/22/31).
//
// These are the exact resolution/migration/validation decisions the HUD render path, the migration
// loader, and Level Validation call. Isolating them here (renderer-free) lets them be unit-tested and
// keeps the eventual DrawHud/TextRenderer wiring a thin call into verified logic. No GL, header-only.

#include "engine/ui/Hud.h"

#include <functional>
#include <string>
#include <vector>

namespace engine {

// Mirror of TextRenderer::kGlyphPx (the built-in 8x8 bitmap cell). Kept local so this header does not
// pull in the GL TextRenderer; must stay in sync with TextRenderer::kGlyphPx.
inline constexpr float kBuiltinGlyphPx = 8.0f;

// Phase 9 migration: a widget's effective pixel font size. New HUDs author fontSize directly; legacy
// HUDs (v<=3) have fontSize == 0 and only textScale, so we derive the same visual size the built-in
// renderer produced: kGlyphPx * textScale. This keeps old HUDs visually identical after upgrade.
inline float EffectiveFontSize(const HudWidget& w) {
    if (w.fontSize > 0.0f) return w.fontSize;
    return kBuiltinGlyphPx * (w.textScale > 0.0f ? w.textScale : 1.0f);
}

// Result of resolving which font a widget draws with (Phase 15/22 fallback chain).
enum class FontResolution { WidgetFont, DefaultFont, BuiltIn };
struct ResolvedFont {
    AssetHandle    asset;               // invalid => built-in
    FontResolution source = FontResolution::BuiltIn;
    bool           usedFallback = false; // true when the widget's own font could not be used
};

// Resolve a widget's font with the required fallback order:
//   widget font (if set AND available) -> project default font (if valid AND available) -> built-in.
// `available` answers "is this font asset loadable?" (registry/cache lookup). A missing imported font
// therefore never blanks the text -- it falls back and flags usedFallback for diagnostics.
inline ResolvedFont ResolveWidgetFont(const HudWidget& w,
                                      const AssetHandle& projectDefaultFont,
                                      const std::function<bool(const AssetHandle&)>& available) {
    ResolvedFont r;
    if (w.fontAssetId.Valid()) {
        if (available && available(w.fontAssetId)) { r.asset = w.fontAssetId; r.source = FontResolution::WidgetFont; return r; }
        r.usedFallback = true;   // widget asked for a font that isn't available
    }
    if (projectDefaultFont.Valid() && available && available(projectDefaultFont)) {
        r.asset = projectDefaultFont; r.source = FontResolution::DefaultFont; return r;
    }
    r.source = FontResolution::BuiltIn;   // asset stays invalid -> built-in bitmap font
    return r;
}

// Phase 31: validate a HUD document's font references. Text/Button widgets that name a font asset
// which the resolver cannot find, or that have a nonsensical size, are reported. Returns false if any
// hard error was found. Reuses a simple diagnostics sink so it can feed Level Validation.
struct HudFontIssue { bool error = true; std::string widget; std::string detail; };

inline bool ValidateHudFonts(const HudDocument& doc,
                             const std::function<bool(const AssetHandle&)>& fontExists,
                             std::vector<HudFontIssue>& out) {
    bool ok = true;
    for (const HudWidget& w : doc.widgets) {
        const bool textual = (w.type == HudWidgetType::Text || w.type == HudWidgetType::Button);
        if (!textual) continue;
        if (w.fontAssetId.Valid()) {
            if (!fontExists || !fontExists(w.fontAssetId)) {
                out.push_back({true, w.name, "references a font asset that is missing or failed to import"});
                ok = false;   // hard error: dependency missing
            }
        }
        if (EffectiveFontSize(w) <= 0.0f) {
            out.push_back({true, w.name, "invalid font size (fontSize and textScale both non-positive)"});
            ok = false;
        } else if (EffectiveFontSize(w) > 512.0f) {
            out.push_back({false, w.name, "very large font size (>512px) -- likely a mistake"});
        }
    }
    return ok;
}

} // namespace engine
