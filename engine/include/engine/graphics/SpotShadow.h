#pragma once

#include "engine/graphics/Shader.h"
#include "engine/graphics/ShadowCasters.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace engine {

namespace ecs { class Registry; }

// Perspective shadow maps for spotlights. A spotlight is a cone pointing one way,
// so a single 2D depth map (rendered through the cone's frustum) suffices — like
// the directional sun's, but with a perspective projection. Supports up to kMax.
class SpotShadow {
public:
    static constexpr int kMax = 4;

    struct Spot {
        glm::vec3 position;
        glm::vec3 direction;
        float     outerAngle;   // degrees (the cone half-angle for the frustum)
        float     range;        // far plane
    };

    explicit SpotShadow(int size = 1024);
    ~SpotShadow();

    SpotShadow(const SpotShadow&)            = delete;
    SpotShadow& operator=(const SpotShadow&) = delete;

    void Generate(ecs::Registry& registry, const std::vector<Spot>& spots);
    void BindMaps(unsigned int startUnit) const;
    const glm::mat4& LightVP(int i) const { return m_vp[i]; }
    int Count() const { return m_count; }
    std::uint32_t MapsRenderedLastFrame() const { return m_renderedLastFrame; }
    std::uint32_t MapsReusedLastFrame() const { return m_reusedLastFrame; }
    std::uint64_t MemoryBytes() const;
    void Invalidate() { m_cacheValid = false; }

private:
    int m_size;
    int m_count = 0;
    unsigned int m_fbo = 0;
    unsigned int m_maps[kMax] = {0, 0, 0, 0};
    glm::mat4 m_vp[kMax];
    std::uint64_t m_casterRevision = 0;
    std::uint64_t m_lightSignatures[kMax] = {0, 0, 0, 0};
    bool m_cacheValid = false;
    std::uint32_t m_renderedLastFrame = 0;
    std::uint32_t m_reusedLastFrame = 0;
    Shader m_shader;
    ShadowCasterBatch m_batch;
};

} // namespace engine
