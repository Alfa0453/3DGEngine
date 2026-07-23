#pragma once

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <unordered_map>

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

    // Draw an RGBA texture (by GL id) as a rectangle (pixels), tinted by
    // tint*alpha. Used for HUD image widgets. Rebinds the font atlas afterward.
    void Image(unsigned int textureId, float x, float y, float w, float h,
               const glm::vec3& tint, float alpha);

    // Draw a HUD rectangle through a compiled Unlit/UI shader graph.
    void CustomRect(Shader& shader, unsigned int textureId,
                    float x, float y, float w, float h, const glm::vec4& color,
                    const std::unordered_map<std::string, std::string>& parameters,
                    const std::unordered_map<std::string, int>& parameterTypes,
                    const std::unordered_map<std::string, const Texture*>& textures);

    void End();

    // Pixel width a string occupies at the given scale (widest line if multi-line).
    float Measure(const std::string& text, float scale) const;

    // --- TrueType fonts ------------------------------------------------------
    // Load a .ttf and bake it at `pixelHeight` (a high value like 48 gives crisp
    // text that scales down smoothly). Returns a font id (>= 1) to pass to
    // SetFont(), or -1 on failure. The built-in 8x8 bitmap font is always id 0.
    int  LoadFont(const std::string& path, int pixelHeight = 48);

    // Make a loaded font the active one for all following Text()/Measure() calls
    // -- this is the "use it everywhere" switch. id 0 restores the built-in font.
    // Returns false if the id is unknown.
    bool SetFont(int fontId);

    int  ActiveFont() const { return m_activeFont; }
    int  FontCount() const;                 // includes the built-in (always >= 1)
    bool FontIsBuiltin(int fontId) const { return fontId <= 0; }

    static constexpr int kGlyphPx = 8;  // built-in font cell size

private:
    // Bind whichever atlas the active font uses (built-in or a loaded TTF).
    void BindActiveAtlas() const;

    unsigned int m_vao = 0;
    unsigned int m_vbo = 0;
    std::unique_ptr<Shader> m_shader;   // built from inline GLSL
    std::unique_ptr<Texture> m_atlas;   // 128x48 built-in font atlas (16x6 cells)
    glm::mat4 m_projection{1.0f};

    // Loaded TrueType fonts (opaque, defined in the .cpp to keep GL out of here).
    struct FontStore;
    std::unique_ptr<FontStore> m_fonts;
    int m_activeFont = 0;               // 0 = built-in bitmap font
};

} // namespace engine
