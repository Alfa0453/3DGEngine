#pragma once

#include "engine/graphics/Shader.h"
#include "engine/graphics/Mesh.h"

#include <glm/glm.hpp>

#include <vector>

namespace engine {

class Camera;
namespace ecs { class Registry; }

// Screen-space ambient occlusion. Renders a view-space position+normal prepass of
// the MeshPBR entities, estimates how occluded each pixel is by nearby geometry,
// and blurs it into an AO texture. PbrRenderer multiplies its ambient/IBL term by
// that, darkening creases and contact points. Symmetric with PbrRenderer: hand it
// the registry + camera.
class SSAO {
public:
    SSAO(int width, int height);
    ~SSAO();

    SSAO(const SSAO&)           = delete;
    SSAO& operator=(const SSAO&) = delete;

    void Generate(ecs::Registry& registry, const Camera& camera, float aspect, int width, int height);
    void Resize(int width, int height);
    void BindAO(unsigned int unit) const;   // the blurred AO texture

    float radius = 0.5f;
    float bias   = 0.025f;

private:
    void CreateTargets();
    void ReleaseTargets();

    int m_width, m_height;
    unsigned int m_gFbo = 0, m_gPos = 0, m_gNormal = 0, m_gDepth = 0;
    unsigned int m_ssaoFbo = 0, m_ssaoTex = 0;
    unsigned int m_blurFbo = 0, m_blurTex = 0;
    unsigned int m_noiseTex = 0;

    std::vector<glm::vec3> m_kernel;
    Shader m_geom, m_ssao, m_blur;
    Mesh   m_quad;
};

} // namespace engine