#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <memory>
#include <random>
#include <vector>

namespace engine {

class Shader;

// A simple CPU-simulated particle system rendered as additive GL points.
// Particles live in world space, age out over their lifetime, and are recycled
// from a fixed-capacity pool (no per-frame allocation once warmed up).
//
// Typical use: Emit() a trail or EmitBurst() an explosion, Update(dt) each
// frame, then Render(viewProj, pointScale) after the opaque scene.
class ParticleSystem {
public:
    explicit ParticleSystem(std::size_t capacity = 2048);
    ~ParticleSystem();

    ParticleSystem(const ParticleSystem&)            = delete;
    ParticleSystem& operator=(const ParticleSystem&) = delete;

    // Spawn one particle. Dropped silently if the pool is full.
    void Emit(const glm::vec3& pos, const glm::vec3& vel,
              const glm::vec4& color, float life, float size);

    // Spawn `count` particles flying outward from `pos` in random directions.
    void EmitBurst(const glm::vec3& pos, const glm::vec3& color, int count,
                  float speed, float life, float size);

    // Integrate, age, and recycle dead particles.
    void Update(float dt);

    // Draw all live particles. `pointScale` converts a particle's world size to
    // pixels (typically viewportHeight * 0.5 / tan(fov/2)).
    void Render(const glm::mat4& viewProj, float pointScale);

    std::size_t AliveCount() const { return m_particles.size(); }

private:
    struct Particle{
        glm::vec3 pos;
        glm::vec3 vel;
        glm::vec4 color;  // rgb + base alpha
        float life;       // seconds remaining
        float maxLife;    // seconds at spawn (for the fade curve)
        float size;       // world-space size
    };
    
    std::vector<Particle> m_particles;
    std::size_t m_capacity;

    unsigned int m_vao = 0;
    unsigned int m_vbo = 0;
    std::unique_ptr<Shader> m_shader;
    std::mt19937 m_rng;
};

} // namespace engine