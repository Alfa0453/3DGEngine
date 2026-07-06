#pragma once

#include "engine/graphics/Shader.h"
#include "engine/graphics/ShadowCasters.h"

#include <glm/glm.hpp>

#include <functional>

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

    explicit CascadedShadow(int size = 2048);
    ~CascadedShadow();

    CascadedShadow(const CascadedShadow&)            = delete;
    CascadedShadow& operator=(const CascadedShadow&) = delete;

    void Generate(ecs::Registry& registry, const Camera& camera, float aspect,
                  const glm::vec3& lightDir, float shadowFar,
                  const std::function<void(const glm::mat4&)>& drawExtraCasters = {});

    void BindArray(unsigned int unit) const;        // sampler2DArray
    const glm::mat4& CascadeVP(int i) const { return m_vp[i]; }
    float SplitDepth(int i) const { return m_splits[i]; }   // view-space far (positive)
    int   Count() const { return kCascades; }

private:
    int m_size;
    unsigned int m_fbo = 0, m_texArray = 0;
    glm::mat4 m_vp[kCascades];
    float     m_splits[kCascades] = {0, 0, 0, 0};
    Shader    m_shader;
    ShadowCasterBatch m_batch;
};

} // namespace engine
