#pragma once

// Engine loading screen -- the on-screen menu + progress bar.
//
// Draws a full-screen loading overlay (dark backdrop, title, a track+fill progress bar, the
// percentage, and the current subsystem being loaded) using ONLY the existing TextRenderer 2D
// primitives that DrawHud already uses (Begin/FillRect/Text/Measure/End). The bar geometry comes from
// the unit-tested ComputeLoadingBar, and the percentage/label come from the thread-safe
// LoadingProgress model, so the loader thread can update progress while this draws each frame.
// Header-only (no new .cpp / CMake reconfigure); it calls TextRenderer methods that link normally.

#include "engine/core/LoadingProgress.h"
#include "engine/graphics/TextRenderer.h"

#include <glm/glm.hpp>
#include <string>

namespace engine {

struct LoadingScreenStyle {
    glm::vec3 background{0.04f, 0.05f, 0.07f};   float backgroundAlpha = 1.0f;
    glm::vec3 barTrack{0.14f, 0.15f, 0.18f};     float barTrackAlpha = 1.0f;
    glm::vec3 barFill{0.30f, 0.68f, 0.95f};      float barFillAlpha = 1.0f;
    glm::vec3 titleColor{0.92f, 0.94f, 0.98f};
    glm::vec3 labelColor{0.72f, 0.76f, 0.82f};
    std::string title = "Loading";
    float titleScale = 3.0f;
    float labelScale = 1.5f;
    float barWidth  = 520.0f;
    float barHeight = 18.0f;
};

// Draw one frame of the loading screen. Call between frames while a load runs; it manages its own
// text.Begin()/End() just like DrawHud.
inline void DrawLoadingScreen(TextRenderer& text, const LoadingProgress& progress,
                              int screenW, int screenH, const LoadingScreenStyle& style = {}) {
    const float w = static_cast<float>(screenW), h = static_cast<float>(screenH);
    const float cx = w * 0.5f, cy = h * 0.5f;
    const float fraction = progress.Fraction();

    text.Begin(screenW, screenH);

    // Full-screen backdrop.
    text.FillRect(0.0f, 0.0f, w, h, style.background, style.backgroundAlpha);

    // Title, centered above the bar.
    const float titleW = text.Measure(style.title, style.titleScale);
    const float titleH = static_cast<float>(TextRenderer::kGlyphPx) * style.titleScale;
    text.Text(style.title, cx - titleW * 0.5f, cy - 90.0f - titleH, style.titleScale, style.titleColor);

    // Progress bar (track + fill), centered.
    const LoadingBarGeometry g = ComputeLoadingBar(cx, cy, style.barWidth, style.barHeight, fraction);
    text.FillRect(g.trackX, g.trackY, g.trackW, g.trackH, style.barTrack, style.barTrackAlpha);
    if (g.fillW > 0.0f) text.FillRect(g.fillX, g.fillY, g.fillW, g.fillH, style.barFill, style.barFillAlpha);

    // Percentage, centered just below the bar.
    const std::string pct = std::to_string(progress.Percent()) + "%";
    const float pctW = text.Measure(pct, style.labelScale);
    text.Text(pct, cx - pctW * 0.5f, g.trackY + g.trackH + 12.0f, style.labelScale, style.titleColor);

    // Current subsystem label ("Loading meshes", ...), centered under the percentage.
    const std::string label = progress.CurrentLabel();
    if (!label.empty()) {
        const float labW = text.Measure(label, style.labelScale);
        const float labH = static_cast<float>(TextRenderer::kGlyphPx) * style.labelScale;
        text.Text(label, cx - labW * 0.5f, g.trackY + g.trackH + 12.0f + labH + 8.0f,
                  style.labelScale, style.labelColor);
    }

    text.End();
}

} // namespace engine
