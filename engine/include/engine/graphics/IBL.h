#pragma once

#include "engine/graphics/Shader.h"
#include "engine/graphics/Mesh.h"

#include <glm/glm.hpp>

#include <functional>
#include <string>
#include <cstdint>

namespace engine {

struct ReflectionCaptureMetadata {
    std::uint64_t stableIdHigh = 0;
    std::uint64_t stableIdLow = 0;
    std::uint64_t sourceSceneHash = 0;
    glm::vec3 boxExtents{1.0f};
    float radius = 1.0f;
    float blendDistance = 0.25f;
    float intensity = 1.0f;
    std::uint32_t shape = 0;
    std::uint32_t includeSky = 1;
};

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

    // Captures scene radiance at a world position and reuses the same GGX
    // importance-sampling shader used by the global environment. The returned
    // cubemap is owned by the caller and contains a complete roughness mip chain.
    unsigned int CapturePrefiltered(
        const glm::vec3& position, int resolution,
        const std::function<void(const glm::mat4&, const glm::mat4&)>& drawScene);
    static bool SavePrefilteredCubemap(const std::string& path, unsigned int cubemap,
                                       int resolution, int mipCount,
                                       std::string* error = nullptr,
                                       const ReflectionCaptureMetadata* metadata = nullptr);
    static unsigned int LoadPrefilteredCubemap(const std::string& path,
                                               int* resolution = nullptr,
                                               int* mipCount = nullptr,
                                               std::string* error = nullptr,
                                               ReflectionCaptureMetadata* metadata = nullptr);

    // Bind irradiance / prefilter / BRDF-LUT to the given texture units.
    void Bind(unsigned int irradianceUnit, unsigned int prefilterUnit, unsigned int brdfUnit) const;
    // Bind only the roughness-prefiltered environment cubemap. Lightweight forward
    // effects such as water do not need the diffuse irradiance map or BRDF LUT.
    void BindPrefilter(unsigned int unit) const;

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
