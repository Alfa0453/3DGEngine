#pragma once

#include "engine/graphics/Camera.h"

#include <glm/glm.hpp>

#include <vector>

namespace engine {

// Tiled ("Forward+") light culling for GL 3.3 (no compute shaders): the screen is
// divided into tiles, and each frame the CPU assigns every point light to the
// tiles it touches. The fragment shader then only loops over the lights in its own
// tile, so the scene can have far more than the old fixed cap. Light data lives in
// a uniform buffer; the per-tile light-index lists live in a texture-buffer object.
class ClusteredLights {
public:
    static constexpr int kTilesX     = 16;
    static constexpr int kTilesY     = 9;
    static constexpr int kTileStride = 64;    // [count, idx0 .. idx62] per tile
    static constexpr int kMaxLights  = 128;

    struct PointLight {
        glm::vec3 position;   // world space
        glm::vec3 color;      // radiance (colour * intensity)
        float     radius;     // influence radius (for culling)
    };

    ClusteredLights();
    ~ClusteredLights();

    ClusteredLights(const ClusteredLights&)            = delete;
    ClusteredLights& operator=(const ClusteredLights&) = delete;

    void Build(const Camera& camera, float aspect, int screenWidth, int screenHeight,
               const std::vector<PointLight>& lights);

    void BindLightUBO(unsigned int bindingPoint) const;   // light data
    void BindTileBuffer(unsigned int textureUnit) const;  // per-tile index lists

private:
    unsigned int m_ubo = 0;
    unsigned int m_tileBuf = 0, m_tileTex = 0;
};

} // namespace engine