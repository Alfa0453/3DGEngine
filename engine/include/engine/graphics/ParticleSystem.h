#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace engine {

// One live particle. Colour and size are interpolated from start->end over its
// life; start/end are cached so per-frame updates are a single lerp.
struct Particle {
    glm::vec3 pos{0.0f};
    glm::vec3 vel{0.0f};
    glm::vec4 color{1.0f};
    float     size = 1.0f;
    float     age  = 0.0f;
    float     life = 1.0f;
    glm::vec4 startColor{1.0f}, endColor{1.0f};
    float     startSize = 1.0f, endSize = 0.0f;
};

enum class EmitShape { Point, Sphere, Cone };
enum class ParticleBlend { Additive, Alpha };

// How an emitter spawns and evolves particles. Sensible defaults make a warm
// upward "spark" fountain; tweak for fire / smoke / magic / etc.
struct EmitterConfig {
    float     rate = 60.0f;                     // continuous spawn (particles/sec); 0 = burst-only
    int       maxParticles = 2000;

    EmitShape shape = EmitShape::Cone;
    float     shapeRadius = 0.1f;               // spawn jitter (Point/Sphere) or cone base radius
    glm::vec3 direction{0.0f, 1.0f, 0.0f};      // cone axis (normalized on use)
    float     coneAngleDeg = 20.0f;

    float     speedMin = 1.5f, speedMax = 3.0f;
    float     lifeMin  = 0.7f, lifeMax   = 1.3f;

    glm::vec3 gravity{0.0f, -2.0f, 0.0f};
    float     drag = 0.0f;                      // velocity damping (per second)

    glm::vec4 startColor{2.0f, 1.2f, 0.4f, 1.0f};   // HDR (>1 to bloom)
    glm::vec4 endColor  {1.5f, 0.1f, 0.0f, 0.0f};    // fades to transparent
    float     startSize = 0.30f, endSize = 0.02f;

    ParticleBlend blend = ParticleBlend::Additive;
};

// A CPU particle emitter: owns a pool, spawns (continuous rate and/or bursts),
// integrates and ages them each Update. Rendering is separate (ParticleRenderer),
// so this is pure logic and unit-testable headless.
class ParticleEmitter {
public:
    EmitterConfig cfg;
    glm::vec3     position{0.0f};       // world spawn origin
    bool          emitting = true;      // continuous emission on/off (bursts always work)

    void Burst(int count);              // spawn `count` particles immediately
    void Update(float dt);              // spawn (rate) + integrate + age + remove dead
    void Clear();
    const std::vector<Particle>& Particles() const { return m_particles; }
    std::size_t Alive() const { return m_particles.size(); }

private:
    std::vector<Particle> m_particles;
    float       m_accum = 0.0f;
    std::uint32_t m_rng = 0x9E3779B9u;

    void SpawnOne();
    float Rand01();                     // xorshift32 in [0,1)
    glm::vec3 SampleDirection();        // within the cone (or the config direction)
    glm::vec3 SampleOffset();           // spawn position jitter by shape
};

} // namespace engine