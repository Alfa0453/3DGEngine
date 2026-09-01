#pragma once

#include "engine/graphics/Framebuffer.h"
#include "engine/graphics/Shader.h"
#include "engine/graphics/Mesh.h"

#include <glm/glm.hpp>
#include <cstdint>

namespace engine {

// Conservative screen-space diffuse bounce used only as a high-frequency
// complement to baked/dynamic irradiance probes. It intentionally runs at half
// resolution and never replaces the probe fallback for off-screen radiance.
class SSGI {
public:
    SSGI(int width, int height);
    void Resize(int width, int height);
    void Generate(unsigned int sceneColor, unsigned int viewPosition,
                  unsigned int viewNormal, const glm::mat4& projection);
    unsigned int Texture() const { return m_filtered.ColorTexture(); }
    unsigned int RawTexture() const { return m_raw.ColorTexture(); }
    double LastMilliseconds() const { return m_lastMilliseconds; }
    double LastDenoiseMilliseconds() const { return m_lastDenoiseMilliseconds; }
    std::uint64_t MemoryBytes() const { return static_cast<std::uint64_t>(m_width) * m_height * 16u; }

    float rayLength = 3.0f;
    int steps = 12;
    float thickness = 0.20f;
    float intensity = 0.35f;

private:
    int m_width = 1, m_height = 1;
    Framebuffer m_raw;
    Framebuffer m_filtered;
    Shader m_trace;
    Shader m_denoise;
    Mesh m_quad;
    double m_lastMilliseconds = 0.0;
    double m_lastDenoiseMilliseconds = 0.0;
};

} // namespace engine
