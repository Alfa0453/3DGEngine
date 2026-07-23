#pragma once

#include "engine/graphics/CascadedShadow.h"
#include "engine/graphics/PointShadow.h"
#include "engine/graphics/SpotShadow.h"
#include "engine/graphics/ClusteredLight.h"

#include <glm/glm.hpp>

#include <functional>
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
        // Extra sun-shadow casters (non-ECS, e.g. skinned models): called per cascade
        // with that cascade's view-projection. See SkinnedRenderer::DrawDepth.
        std::function<void(const glm::mat4&)> shadowCasters;
        bool frustumCull = true;   // skip MeshPBR entities outside the camera frustum
        bool instancing  = true;   // batch untextured meshes; false = per-object (fallback)
        // Shadow frustum. If radius <= 0 it is fitted automatically to the
        // MeshPBR entities each frame.
        glm::vec3 shadowCenter{0.0f};
        float     shadowRadius = -1.0f;
        bool      tonemap = true;   // false = output linear HDR (for post-processing)
        const IBL*  ibl   = nullptr;   // image-based ambient lighting (optional)
        const SSAO* ssao = nullptr;    // screen-space ambient occlusion (optional)
        bool        skylightOcclusion = false;
        float       skylightOcclusionStrength = 0.90f;
        float       minimumSkylight = 0.06f;
        bool        pointShadows = true;    // omnidirectional shadows for point lights
        bool        spotShadows  = true;  // perspective shadows for spotlights
        bool        directionalShadows = true; // cascaded shadows for the directional sun
        float       shadowSoftness = 2.5f; // PCSS sun-shadow softness (light size)
        // How far from the camera the sun's cascaded shadows reach (view units).
        // Beyond this, geometry no longer casts/receives sun shadows -- raise it if
        // shadows "pop in" only near the player (they follow the camera). Larger
        // values spread the same shadow-map resolution over more area, so bump the
        // shadow map size too (PbrRenderer ctor) if they get soft.
        float       shadowDistance = 140.0f;

        // Animated world-space cloud shadows modulate direct sunlight only.
        bool  cloudShadows = false;
        float cloudShadowStrength = 0.45f;
        float cloudShadowScale = 0.035f;
        float cloudCoverage = 0.45f;
        float cloudDensity = 0.75f;
        float cloudSoftness = 0.18f;
        float cloudWindSpeed = 0.025f;
        float cloudWindDirectionDegrees = 25.0f;

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

    // The sun's cascaded shadow map from the most recent Render() call, for
    // sharing with other passes (e.g. SkinnedRenderer) that light the same scene.
    const CascadedShadow& Cascade() const { return m_cascade; }

private:
    CascadedShadow          m_cascade;
    PointShadow             m_pointShadow;
    SpotShadow              m_spotShadow;
    ClusteredLights         m_clustered;
    std::unique_ptr<Shader> m_pbr;
    unsigned int            m_instanceVBO = 0;   // per-instance data for batching
};

} // namespace engine
