#pragma once

#include <glm/glm.hpp>

#include <memory>

namespace engine {

class Shader;
class Camera;
class ParticleEmitter;

// Draws particles as GPU-instanced, camera-facing billboards with a soft radial
// falloff. Output is linear HDR (no tone map), so bright particles bloom; run it
// AFTER the scene into the same HDR framebuffer, before PostProcess::RenderToScreen.
// Particles are depth-tested against the scene (so solids occlude them) but do not
// write depth, and blend additively or alpha per the emitter.
class ParticleRenderer {
public:
    ParticleRenderer();
    ~ParticleRenderer();
    ParticleRenderer(const ParticleRenderer&)            = delete;
    ParticleRenderer& operator=(const ParticleRenderer&) = delete;

    // Draw one emitter's live particles from the given camera.
    void Draw(const ParticleEmitter& emitter, const Camera& camera, float aspect);

private:
    unsigned int m_vao = 0;
    unsigned int m_quadVBO = 0;
    unsigned int m_instanceVBO = 0;
    std::unique_ptr<Shader> m_shader;
};

} // namespace engine