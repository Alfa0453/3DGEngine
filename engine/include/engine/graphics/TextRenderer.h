#pragma once

#include <glm/glm.hpp>

#include <memory>
#include <string>

namespace engine {
class Shader;
class Texture;

// Draws 2D bitmap text on top of the scene. It owns a built-in 8x8 font (baked
// into a texture atlas at construction), a tiny 2D shader, and a dynamic vertex
// buffer it rebuilds each call.
//
// Usage per frame:
//     text.Begin(width, height);
//     text.Text("Score 3 : 2", x, y, scale, color);
//     text.End();
//
// Coordinates are in pixels with (0,0) at the top-left. A glyph is 8*scale
// pixels tall/wide (the font is monospaced).
class TextRenderer {
public:
    TextRenderer();
    ~TextRenderer();

    TextRenderer(const TextRenderer&)            = delete;
    TextRenderer& operator=(const TextRenderer&) = delete;

    // Set up the 2D pass: orthographic projection, alpha blending, depth test
    // off. Restores 3D state in End().
    void Begin(int screenWidth, int screenHeight);
    void Text(const std::string& text, float x, float y, float scale, const glm::vec3& color);

    // Draw a solid filled rectangle (pixels). Handy for dim overlays and bars.
    void FillRect(float x, float y, float w, float h, const glm::vec3& color, float alpha);

    void End();

    // Pixel width a string occupies at the given scale (monospace).
    float Measure(const std::string& text, float scale) const;

    static constexpr int kGlyphPx = 8;  // font cell size

private:
    unsigned int m_vao = 0;
    unsigned int m_vbo = 0;
    std::unique_ptr<Shader> m_shader;   // built from inline GLSL
    std::unique_ptr<Texture> m_atlas;   // 128x48 font atlas (16x6 cells)
};

} // namespace engine