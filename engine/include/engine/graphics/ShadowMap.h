#pragma once

#include <glm/glm.hpp>

namespace engine {

// A depth-only framebuffer for directional shadow mapping. Render the scene into
// it from the light's point of view (BeginDepthPass/EndDepthPass), then sample
// BindDepthTexture() in the main shader to test whether a fragment is shadowed.
class ShadowMap {
public:
    explicit ShadowMap(int size = 2048);
    ~ShadowMap();

    ShadowMap(const ShadowMap&)            = delete;
    ShadowMap& operator=(const ShadowMap&) = delete;
    ShadowMap(ShadowMap&& other) noexcept;
    ShadowMap& operator=(ShadowMap&& other) noexcept;

    // Bind the depth FBO and set the viewport to the shadow-map size; clears it.
    void BeginDepthPass() const;
    // Restore the default framebuffer and the given screen viewport.
    void EndDepthPass(int screenWidth, int screenHeight) const;

    void BindDepthTexture(unsigned int unit) const;
    int Size() const { return m_size; }

    // Orthographic light-space view-projection for a directional light that
    // covers a sphere (center, radius) — used to transform world -> shadow space.
    static glm::mat4 LightSpaceMatrix(const glm::vec3& lightDir, 
                                      const glm::vec3& center, float radius);

private:
    void Release();
    unsigned int    m_fbo      = 0;
    unsigned int    m_depthTex = 0;
    int             m_size     = 0;
    mutable int     m_prevFbo  = 0;
};

} // namespace engine