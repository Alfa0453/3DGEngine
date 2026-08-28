#pragma once

#include "engine/graphics/Shader.h"
#include "engine/graphics/Mesh.h"

#include <glm/glm.hpp>

#include <vector>
#include <cstdint>
#include <unordered_map>

namespace engine {

class Camera;
namespace ecs { class Registry; }

// Ground-truth ambient occlusion (the historical SSAO class name is retained to
// keep the public renderer API/source compatibility).  The pass performs a
// deterministic horizon search and produces both filtered AO and a view-space
// bent normal for indirect lighting.
class SSAO {
public:
    SSAO(int width, int height);
    ~SSAO();

    SSAO(const SSAO&)           = delete;
    SSAO& operator=(const SSAO&) = delete;

    void Generate(ecs::Registry& registry, const Camera& camera, float aspect, int width, int height);
    void Resize(int width, int height);
    void BindAO(unsigned int unit) const;   // the blurred AO texture
    void BindRawAO(unsigned int unit) const;
    void BindBentNormal(unsigned int unit) const;
    unsigned int RawAOTexture() const { return m_ssaoTex; }
    unsigned int FilteredAOTexture() const { return m_blurTex; }
    unsigned int RawBentNormalTexture() const { return m_bentNormalTex; }
    unsigned int BentNormalTexture() const { return m_filteredBentNormalTex; }
    unsigned int PositionTexture() const { return m_gPos; }    // view-space position
    unsigned int NormalTexture()   const { return m_gNormal; }  // view-space normal
    unsigned int VelocityTexture() const { return m_gVelocity; } // screen UV motion
    double LastGpuMilliseconds() const { return m_lastGpuMilliseconds; }
    std::uint64_t MemoryBytes() const {
        const std::uint64_t pixels = static_cast<std::uint64_t>(m_width) * m_height;
        return pixels * (8u + 8u + 4u + 3u + 2u + 6u + 2u + 6u);
    }

    float radius = 0.5f;
    float bias   = 0.025f;

private:
    void CreateTargets();
    void ReleaseTargets();

    int m_width, m_height;
    unsigned int m_gFbo = 0, m_gPos = 0, m_gNormal = 0, m_gVelocity = 0, m_gDepth = 0;
    unsigned int m_ssaoFbo = 0, m_ssaoTex = 0, m_bentNormalTex = 0;
    unsigned int m_blurFbo = 0, m_blurTex = 0, m_filteredBentNormalTex = 0;
    Shader m_geom, m_ssao, m_blur;
    Mesh   m_quad;
    glm::mat4 m_previousViewProjection{1.0f};
    bool m_hasPreviousFrame = false;
    std::unordered_map<std::uint32_t, glm::mat4> m_previousModels;
    std::unordered_map<std::uint32_t, glm::mat4> m_currentModels;
    unsigned int m_timestampQueries[3][2]{};
    bool m_timestampSubmitted[3]{};
    int m_timestampIndex = 0;
    double m_lastGpuMilliseconds = 0.0;
};

} // namespace engine
