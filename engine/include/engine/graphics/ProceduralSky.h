#pragma once

#include "engine/graphics/Mesh.h"
#include "engine/graphics/Shader.h"
#include "engine/graphics/DayNightCycle.h"

#include <glm/glm.hpp>

namespace engine {

// A sky rendered procedurally in the shader (no cubemap), so it can change every
// frame — gradient + sun disc/glow + moon + night stars, all driven by a
// DayNightCycle::Sample. This is what makes a day/night cycle smooth: just pass a
// new sample each frame.
class ProceduralSky {
public:
    ProceduralSky();

    ProceduralSky(const ProceduralSky&)            = delete;
    ProceduralSky& operator=(const ProceduralSky&) = delete;
    ProceduralSky(ProceduralSky&&) noexcept            = default;
    ProceduralSky& operator=(ProceduralSky&&) noexcept = default;

    // Draw behind the scene. tonemap=false outputs linear HDR for a post pass.
    void Draw(const glm::mat4& view, const glm::mat4& projectio,
              const DayNightCycle::Sample& sky, bool tonemap = true);

private:
    Mesh   m_cube;
    Shader m_shader;
};

} // namespace engine