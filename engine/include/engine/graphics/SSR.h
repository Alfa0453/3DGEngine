#pragma once

#include "engine/graphics/Framebuffer.h"
#include "engine/graphics/Shader.h"
#include "engine/graphics/Mesh.h"

#include <glm/glm.hpp>

namespace engine {

// Screen-space reflections. Ray-marches the view-space depth (from the SSAO
// prepass) along each pixel's reflection vector and, on a hit, mixes in the scene
// colour at that point — so glossy surfaces reflect other on-screen geometry,
// which sky-only IBL can't do. Runs as a post pass after the lit scene + sky.
class SSR {
public:
    SSR(int width, int height);

    SSR(const SSR&)            = delete;
    SSR& operator=(const SSR&) = delete;

    // Reads the scene colour + view-space position/normal, composites reflections,
    // and blits the result back into `dstFbo` (the HDR scene buffer).
    void Apply(unsigned int sceneColorTex, unsigned int gPosTex, unsigned int gNormalTex,
               const glm::mat4& projection, unsigned int dstFbo, int width, int height);
    void Resize(int width, int height);

    float intensity = 0.5f;   // overall reflection strength

private:
    int m_width, m_height;
    Framebuffer m_result;
    Shader m_shader;
    Mesh   m_quad;
};

} // namespace engine