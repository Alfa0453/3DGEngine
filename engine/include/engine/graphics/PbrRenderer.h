#pragma once

#include "engine/graphics/CascadedShadow.h"
#include "engine/graphics/PointShadow.h"
#include "engine/graphics/SpotShadow.h"
#include "engine/graphics/ClusteredLight.h"

#include <glm/glm.hpp>

#include <functional>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace engine {

class Shader;
class Camera;
class IBL;
class SSAO;
class Mesh;
class LightingProbeGrid;
class ReflectionProbeSystem;
class LtcLut;
namespace ecs { class Registry; struct Transform; struct MeshPBR; }

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
        bool backfaceCull = true;  // cull closed-mesh backfaces; false = draw two-sided
                                   // (e.g. the Material Maker preview shows both sides)
        bool instancing  = true;   // batch untextured meshes; false = per-object (fallback)
        // Shadow frustum. If radius <= 0 it is fitted automatically to the
        // MeshPBR entities each frame.
        glm::vec3 shadowCenter{0.0f};
        float     shadowRadius = -1.0f;
        bool      tonemap = true;   // false = output linear HDR (for post-processing)
        const IBL*  ibl   = nullptr;   // image-based ambient lighting (optional)
        float       globalIblIntensity = 1.0f;
        float       globalReflectionIntensity = 1.0f;
        const SSAO* ssao = nullptr;    // screen-space ambient occlusion (optional)
        bool        skylightOcclusion = false;
        float       skylightOcclusionStrength = 1.0f;
        float       minimumSkylight = 0.0f;
        float       specularOcclusionStrength = 0.85f;
        float       localProbeInfluence = 1.0f;
        int         lightingDebugMode = 0;
        const LightingProbeGrid* lightingGrid = nullptr;
        bool        probeVisibilityWeighting = false;
        float       probeVisibilityMaxDistance = 60.0f;
        ReflectionProbeSystem* reflectionProbes = nullptr;
        bool        pointShadows = true;    // omnidirectional shadows for point lights
        bool        spotShadows  = true;  // perspective shadows for spotlights
        bool        directionalShadows = true; // cascaded shadows for the directional sun
        bool        forceDirectionalShadowUpdate = false; // editor diagnostic
        float       shadowSoftness = 2.5f; // PCSS sun-shadow softness (light size)
        int         shadowBlockerSamples = 16;
        int         shadowFilterSamples = 32;
        // Advance the PCSS sample rotation per frame so the temporal-AA pass can average the
        // shadow's residual grain to smooth. Set this to match PostProcess::Settings::taa;
        // leave it false when temporal accumulation is off or the shadow will flicker.
        bool        temporalAccumulation = false;
        int         maxShadowedLocalLights = 4;
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
    struct ShadowStats {
        std::uint32_t cascadesRendered = 0, cascadesReused = 0;
        std::uint32_t pointMapsRendered = 0, pointMapsReused = 0;
        std::uint32_t spotMapsRendered = 0, spotMapsReused = 0;
        std::uint64_t memoryBytes = 0;
    };
    ShadowStats GetShadowStats() const {
        return {m_cascade.CascadesRenderedLastFrame(), m_cascade.CascadesReusedLastFrame(),
                m_pointShadow.MapsRenderedLastFrame(), m_pointShadow.MapsReusedLastFrame(),
                m_spotShadow.MapsRenderedLastFrame(), m_spotShadow.MapsReusedLastFrame(),
                m_cascade.MemoryBytes() + m_pointShadow.MemoryBytes() + m_spotShadow.MemoryBytes()};
    }
    void InvalidateShadowCache() {
        m_cascade.Invalidate();
        m_pointShadow.Invalidate();
        m_spotShadow.Invalidate();
    }

private:
    CascadedShadow          m_cascade;
    unsigned int            m_frameCounter = 0;   // drives the per-frame PCSS rotation advance
    PointShadow             m_pointShadow;
    SpotShadow              m_spotShadow;
    ClusteredLights         m_clustered;
    std::unique_ptr<Shader> m_pbr;
    std::unique_ptr<LtcLut> m_ltcLut;
    unsigned int            m_instanceVBO = 0;   // per-instance data for batching
    std::size_t             m_instanceCapacity = 0;
    std::vector<glm::vec3> m_pointPositions;
    std::vector<ClusteredLights::PointLight> m_clusterLights;
    std::vector<glm::vec3> m_spotPositions;
    std::vector<glm::vec3> m_spotDirections;
    std::vector<glm::vec3> m_spotColors;
    std::vector<float> m_spotCosInner;
    std::vector<float> m_spotCosOuter;
    std::vector<float> m_spotRanges;
    std::vector<glm::vec3> m_areaPositions;
    std::vector<glm::vec3> m_areaColors;
    std::vector<float> m_areaRadii;
    std::vector<glm::vec3> m_areaRights,m_areaUps;
    std::vector<glm::vec2> m_areaHalfSizes;
    std::vector<int> m_areaShapes,m_areaTwoSided;
    std::vector<SpotShadow::Spot> m_spotShadowLights;
    std::unordered_map<const Mesh*, std::vector<float>> m_meshBatches;
    std::vector<std::pair<ecs::Transform*, ecs::MeshPBR*>> m_texturedObjects;
    std::vector<std::pair<ecs::Transform*, ecs::MeshPBR*>> m_customObjects;
};

} // namespace engine
