#pragma once

#include "engine/graphics/Shader.h"
#include "engine/graphics/ShadowCasters.h"

#include <glm/glm.hpp>

#include <vector>

namespace engine {

namespace ecs { class Registry; }

// Omnidirectional shadow maps for point lights. For each light it renders the
// scene's distance-to-light into the six faces of a cubemap; the PBR shader then
// samples the cube in the fragment->light direction and compares distances to
// decide if the fragment is shadowed. Supports up to kMax shadow-casting points.
class PointShadow {
public:
    static constexpr int kMax = 4;

    explicit PointShadow(int faceSize = 512);
    ~PointShadow();

    PointShadow(const PointShadow&)            = delete;
    PointShadow& operator=(const PointShadow&) = delete;

    // Render distance cubemaps for the first kMax light positions.
    void Generate(ecs::Registry& registry, const std::vector<glm::vec3>& lightPositions, float farPlane);

    // Bind the cube textures to units [startUnit, startUnit+kMax).
    void BindCubes(unsigned int startUnit) const;

    int   Count()    const { return m_count; }
    float FarPlane() const { return m_far; }

private:
    int   m_faceSize;
    int   m_count = 0;
    float m_far   = 50.0f;
    unsigned int m_fbo = 0, m_depthRbo = 0;
    unsigned int m_cubes[kMax] = {0, 0, 0, 0};
    Shader m_shader;
    ShadowCasterBatch m_batch;
};

} // namespace engine