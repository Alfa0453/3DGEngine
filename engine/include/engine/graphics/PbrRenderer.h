#pragma once

#include "engine/graphics/CascadedShadow.h"
#include "engine/graphics/PointShadow.h"
#include "engine/graphics/SpotShadow.h"
#include "engine/graphics/ClusteredLight.h"

#include <glm/glm.hpp>

#include <memory>

namespace engine {

class Shader;
class Camera;
class IBL;
class SSAO;
namespace ecs { class Registry; }

// A drop-in physically-based scene renderer. Give it an ECS registry and a
// camera and it draws every MeshPBR entity, lit by every Light entity, with a
// shadow-mapped directional sun. The shaders and shadow map are owned internally
// (embedded GLSL — no asset files needed), so adding PBR lighting to any app is:
//
//     PbrRenderer pbr;                 // once, after GL init
//     ...
//     renderer.Clear();
//     pbr.Render(registry, camera, aspect, width, height);
//     skybox.Draw(view, proj);         // optional
//
class PbrRenderer {
public:
    struct Options {
        glm::vec3 ambient{0.03f, 0.03f, 0.03f};  // sky/fill light
        // Shadow frustum. If radius <= 0 it is fitted automatically to the
        // MeshPBR entities each frame.
        glm::vec3 shadowCenter{0.0f};
        float     shadowRadius = -1.0f;
        bool      tonemap = true;   // false = output linear HDR (for post-processing)
        const IBL*  ibl   = nullptr;   // image-based ambient lighting (optional)
        const SSAO* ssao = nullptr;    // screen-space ambient occlusion (optional)
        bool        pointShadows = true;    // omnidirectional shadows for point lights
        bool        spotShadows  = true;  // perspective shadows for spotlights
        bool        directionalShadows = true; // cascaded shadows for the directional sun
        float       shadowSoftness = 2.5f; // PCSS sun-shadow softness (light size)

        // Distance + height fog (applied to lit geometry in linear HDR).
        bool      fog = false;
        glm::vec3 fogColor{0.6f, 0.7f, 0.8f};
        float     fogDensity       = 0.02f;   // distance falloff  
        float     fogHeight        = 0.0f;    // height where fog is densest
        float     fogHeightFalloff = 0.12f;   // how fast fog thins with height
    };

    explicit PbrRenderer(int shadowSize = 2048);
    ~PbrRenderer();

    PbrRenderer(const PbrRenderer&)            = delete;
    PbrRenderer& operator=(const PbrRenderer&) = delete;

    // Renders the shadow pass + the lit scene into the current framebuffer.
    // The caller clears beforehand and may draw a skybox / HUD afterwards.
    void Render(ecs::Registry& registry, const Camera& camera, float aspect,
                int screenWidth, int screenHeight, const Options& options);
    // Same, with default options.
    void Render(ecs::Registry& registry, const Camera& camera, float aspect,
                int screenWidth, int screenHeight);

private:
    CascadedShadow          m_cascade;
    PointShadow             m_pointShadow;
    SpotShadow              m_spotShadow;
    ClusteredLights         m_clustered;
    std::unique_ptr<Shader> m_pbr;
};

} // namespace engine