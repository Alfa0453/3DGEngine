#pragma once

namespace engine {

// An off-screen render target: a colour texture (HDR by default) plus an optional
// depth buffer. Render into it with Bind(), then sample its colour texture in a
// later pass. The foundation for post-processing (HDR, bloom, …).
class Framebuffer {
public:
    // `internalFormat` is the colour texture's GL internal format (e.g.
    // GL_RGBA16F for HDR, GL_RGBA8 for LDR). With `depth`, a depth renderbuffer
    // is attached so 3D geometry depth-tests correctly.
    Framebuffer(int widh, int height, unsigned int iternalFormat, bool depth);
    ~Framebuffer();

    Framebuffer(const Framebuffer&)            = delete;
    Framebuffer& operator=(const Framebuffer&) = delete;
    Framebuffer(Framebuffer&& other) noexcept;
    Framebuffer& operator=(Framebuffer&& other) noexcept;

    void Bind() const;                                  // bind + set viewport
    static void BindDefault(int sreenWidth, int screenHeight);
    void BindColorTexture(unsigned int unit) const;
    void Resize(int width, int height);

    int Width()  const { return m_width; }
    int Height() const { return m_height; }
    unsigned int ColorTexture() const { return m_colorTex; }
    unsigned int FboId()        const { return m_fbo; }

private:
    void Create();
    void Release();

    unsigned int m_fbo      = 0;
    unsigned int m_colorTex = 0;
    unsigned int m_depthRbo = 0;
    int          m_width    = 0;
    int          m_height   = 0;
    unsigned int m_format   = 0;
    bool         m_hasDepth = false;
};

} // namespace engine