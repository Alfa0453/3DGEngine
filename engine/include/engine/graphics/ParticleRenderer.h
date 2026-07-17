#pragma once

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace engine {

class Shader;
class Camera;
class ParticleEmitter;
struct ParticleSystemComponent;
class Texture;
class Mesh;
class Model;

// Draws particles as GPU-instanced, camera-facing billboards with a soft radial
// falloff. Output is linear HDR (no tone map), so bright particles bloom; run it
// AFTER the scene into the same HDR framebuffer, before PostProcess::RenderToScreen.
// Particles are depth-tested against the scene (so solids occlude them) but do not
// write depth, and blend additively or alpha per the emitter.
class ParticleRenderer {
public:
    struct Stats {
        int drawCalls = 0;
        int culledEmitters = 0;
        std::size_t particles = 0;
        double cpuMilliseconds = 0.0;
        double gpuMilliseconds = 0.0;
    };
    ParticleRenderer();
    ~ParticleRenderer();
    ParticleRenderer(const ParticleRenderer&)            = delete;
    ParticleRenderer& operator=(const ParticleRenderer&) = delete;

    // Draw one emitter's live particles from the given camera.
    void Draw(const ParticleEmitter& emitter, const Camera& camera, float aspect);
    void Draw(const ParticleSystemComponent& system, const Camera& camera, float aspect);
    void ResetStats() { m_stats = {}; }
    const Stats& GetStats() const { return m_stats; }

private:
    unsigned int m_vao = 0;
    unsigned int m_quadVBO = 0;
    unsigned int m_instanceVBO = 0;
    unsigned int m_timerQuery = 0;
    unsigned int m_trailVao = 0;
    unsigned int m_trailVbo = 0;
    std::unique_ptr<Shader> m_shader;
    std::unique_ptr<Shader> m_trailShader;
    std::unique_ptr<Shader> m_meshShader;
    std::unique_ptr<Mesh> m_particleCube;
    std::unique_ptr<Mesh> m_particleSphere;
    std::unique_ptr<Mesh> m_particleCone;
    std::unique_ptr<Mesh> m_particleCylinder;
    std::unordered_map<std::string, std::unique_ptr<Model>> m_models;
    std::unordered_map<std::string, std::unique_ptr<Texture>> m_textures;
    std::unordered_set<std::string> m_failedTextures;
    Stats m_stats;
};

} // namespace engine
