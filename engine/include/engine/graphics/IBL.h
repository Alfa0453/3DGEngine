#pragma once

#include "engine/graphics/Shader.h"
#include "engine/graphics/Mesh.h"

#include <glm/glm.hpp>

#include <functional>

namespace engine {

// Image-Based Lighting. Captures the environment (your sky) into a cubemap, then
// precomputes the two maps a PBR shader needs for ambient light:
//   * an irradiance cubemap   — diffuse ambient (the sky's light from every dir)
//   * a prefiltered cubemap   — specular reflections, blurred per roughness mip
// plus a BRDF integration LUT (computed once). Call Generate() whenever the
// environment changes (e.g. as the day/night sky shifts), then Bind() before the
// lit pass.
class IBL {
public:
    explicit IBL(int evSize = 256);
    ~IBL();

    IBL(const IBL&)            = delete;
    IBL& operator=(const IBL&) = delete;

    // `drawSky(view, projection)` must draw the environment for one cube face.
    void Generate(const std::function<void(const glm::mat4&, const glm::mat4&)>& drawSky);

    // Bind irradiance / prefilter / BRDF-LUT to the given texture units.
    void Bind(unsigned int irradianceUnit, unsigned int prefilterUnit, unsigned int brdfUnit) const;

    float MaxReflectionLod() const { return static_cast<float>(m_prefilterMips - 1); }

private:
    void RenderBrdfLUT();

    unsigned int m_captureFbo = 0, m_captureRbo = 0;
    unsigned int m_envCube = 0, m_irradiance = 0, m_prefilter = 0, m_brdfLUT = 0;
    int m_envSize;
    int m_irrSize       = 32;
    int m_preSize       = 128;
    int m_brdfSize       = 512;
    int m_prefilterMips = 5;

    Shader m_irradianceShader, m_prefilterShader, m_brdfShader;
    Mesh   m_cube, m_quad;
};

} // namespace engine