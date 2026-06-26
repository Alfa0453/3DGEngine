#pragma once

#include <string>

namespace engine {

// Owns a 2D texture on the GPU. Loads an uncompressed TGA image from disk,
// uploads it, and generates mipmaps. Move-only, because it owns a GL resource
// (same ownership rules as Mesh and Shader).
//
// We use TGA rather than a library like stb_image so the engine stays
// dependency-free and self-contained; TGA's uncompressed format is trivial to
// parse and is still used in game tooling. A real project would usually pull in
// stb_image to also read PNG/JPG.
class Texture {
public:
    // Load an uncompressed 24/32-bit TGA. `smooth` picks linear+mipmaps (true)
    // vs nearest (false).
    explicit Texture(const std::string& path, bool smooth = true);

    // Build from tightly-packed RGBA pixels (width*height*4 bytes). With
    // smooth=false you get crisp nearest-neighbour sampling (good for a pixel
    // font) and clamping; with true, linear + mipmaps + repeat.
    Texture(const unsigned char* rgbaPixels, int width, int height, bool smooth = true);

    ~Texture();

    Texture(const Texture&)            = delete;
    Texture& operator=(const Texture&) = delete;
    Texture(Texture&& other) noexcept;
    Texture& operator=(Texture&& other) noexcept;

    // Make this texture active on the given texture unit (0, 1, ...). The
    // shader's sampler uniform must be set to the same unit
    void Bind(unsigned int unit = 0) const;

    int Width() const { return m_width; }
    int Height() const { return m_height; }

private:
    // Create the GL texture object from RGBA pixels.
    void Create(const unsigned char* rgba, int width, int height, bool smooth);
    void Release();

    unsigned int m_id = 0;
    int m_width = 0;
    int m_height = 0;
};

} // namespace engine