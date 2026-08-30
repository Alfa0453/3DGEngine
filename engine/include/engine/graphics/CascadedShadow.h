#pragma once

#include "engine/graphics/Shader.h"
#include "engine/graphics/ShadowCasters.h"

#include <glm/glm.hpp>

#include <functional>
#include <cstdint>
#include <array>

namespace engine {

class Camera;
namespace ecs { class Registry; }

// Cascaded shadow maps for the directional sun. The camera's view distance is
// split into a few ranges ("cascades"); each gets its own depth map fitted
// tightly to that slice, so near shadows are high-resolution and far ones still
// covered. Stored as a depth texture array; the shader picks the cascade per
// fragment from its view-space depth.
class CascadedShadow {
public:
    static constexpr int kCascades = 4;

    // 4096 per-cascade: at 2048 a close caster's silhouette only spanned a few hundred
    // texels, so its shadow edge was visibly stair-stepped. Quadrupling texel density
    // (~256 MB for the 4-layer 32F array) is the single biggest reduction in silhouette
    // jaggedness short of virtual shadow maps.
    explicit CascadedShadow(int size = 4096);
    ~CascadedShadow();

    CascadedShadow(const CascadedShadow&)            = delete;
    CascadedShadow& operator=(const CascadedShadow&) = delete;

    void Generate(ecs::Registry& registry, const Camera& camera, float aspect,
                  const glm::vec3& lightDir, float shadowFar,
                  const std::function<void(const glm::mat4&)>& drawExtraCasters = {});

    void BindArray(unsigned int unit) const;        // sampler2DArray
    const glm::mat4& CascadeVP(int i) const { return m_vp[i]; }
    float SplitDepth(int i) const { return m_splits[i]; }   // view-space far (positive)
    float WorldTexelSize(int i) const { return m_worldTexelSize[i]; }
    float DepthRange(int i) const { return m_depthRange[i]; }
    int   Count() const { return kCascades; }
    std::uint32_t CascadesRenderedLastFrame() const { return m_renderedLastFrame; }
    std::uint32_t CascadesReusedLastFrame() const { return m_reusedLastFrame; }
    std::uint64_t MemoryBytes() const;
    void SetUpdateIntervals(const std::array<std::uint32_t, kCascades>& intervals);
    void SetForceUpdateEveryFrame(bool enabled) { m_forceUpdateEveryFrame = enabled; }
    bool ForceUpdateEveryFrame() const { return m_forceUpdateEveryFrame; }
    void Invalidate() { m_cacheValid = false; }

private:
    int m_size;
    unsigned int m_fbo = 0, m_texArray = 0;
    glm::mat4 m_vp[kCascades];
    float     m_splits[kCascades] = {0, 0, 0, 0};
    // World units represented by one shadow texel and by the full normalized
    // depth interval. These make PCSS softness independent of cascade coverage.
    float     m_worldTexelSize[kCascades] = {1, 1, 1, 1};
    float     m_depthRange[kCascades] = {1, 1, 1, 1};
    Shader    m_shader;
    ShadowCasterBatch m_batch;
    std::array<std::uint32_t, kCascades> m_updateIntervals{{1, 2, 4, 8}};
    std::array<std::uint64_t, kCascades> m_lastUpdateFrame{{0, 0, 0, 0}};
    std::uint64_t m_frameIndex = 0;
    std::uint64_t m_casterRevision = 0;
    glm::vec3 m_lastLightDirection{0.0f};
    float m_lastShadowFar = 0.0f;
    bool m_cacheValid = false;
    bool m_forceUpdateEveryFrame = false;
    std::uint32_t m_renderedLastFrame = 0;
    std::uint32_t m_reusedLastFrame = 0;
};

} // namespace engine
