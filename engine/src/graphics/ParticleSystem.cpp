#include "engine/graphics/ParticleSystem.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

namespace engine {

float ParticleEmitter::Rand01() {
    // xorshift32 -> [0,1)
    std::uint32_t x = m_rng;
    x ^= x << 13; x ^=x >> 17; x ^=x << 5;
    m_rng = x;
    return static_cast<float>(x & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}

glm::vec3 ParticleEmitter::SampleDirection() {
    glm::vec3 axis = cfg.direction;
    const float al = glm::length(axis);
    axis = (al > 1e-6f) ? axis / al : glm::vec3(0.0f, 1.0f, 0.0f);

    const float cosMax = std::cos(glm::radians(std::max(cfg.coneAngleDeg, 0.0f)));
    const float cosA = 1.0f - Rand01() * (1.0f - cosMax);       // uniform on the cap
    const float sinA = std::sqrt(std::max(0.0f, 1.0f - cosA * cosA));
    const float phi = 2.0f * glm::pi<float>() * Rand01();
    const glm::vec3 local(sinA * std::cos(phi), sinA * std::sin(phi), cosA);
    
    // Orthonormal basis with +z == axis.
    const glm::vec3 up = (std::fabs(axis.y) < 0.99f) ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    const glm::vec3 t1 = glm::normalize(glm::cross(up, axis));
    const glm::vec3 t2 = glm::cross(axis, t1);
    return glm::normalize(t1 * local.x + t2 * local.y + axis * local.z);
}

glm::vec3 ParticleEmitter::SampleOffset() {
    if (cfg.shape == EmitShape::Point && cfg.shapeRadius <= 0.0f) return glm::vec3(0.0f);
    // Random unit vector.
    const float z = Rand01() * 2.0f - 1.0f;
    const float a = 2.0f * glm::pi<float>() * Rand01();
    const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
    const glm::vec3 dir(r * std::cos(a), r * std::sin(a), z);
    // Point/Sphere: jitter within the radius; Cone: small disc at the base.
    const float rad = cfg.shapeRadius * std::cbrt(Rand01());
    return dir * rad;
}

void ParticleEmitter::SpawnOne() {
    if (static_cast<int>(m_particles.size()) >= cfg.maxParticles) return;
    Particle p;
    p.pos = position + SampleOffset();
    const float speed = cfg.speedMin + (cfg.speedMax - cfg.speedMin) * Rand01();
    p.vel = SampleDirection() * speed;
    p.life = cfg.lifeMin + (cfg.lifeMax - cfg.lifeMin) * Rand01();
    p.age = 0.0f;
    p.startColor = cfg.startColor; p.endColor = cfg.endColor;
    p.startSize  = cfg.startSize;   p.endSize  = cfg.endSize;
    p.color = p.startColor;
    p.size  = p.startSize;
    m_particles.push_back(p);
}

void ParticleEmitter::Burst(int count) {
    for (int i = 0; i < count; ++i) SpawnOne();
}

void ParticleEmitter::Update(float dt) {
    if (dt <= 0.0f) return;

    // 1) Continuous spawn.
    if (emitting && cfg.rate > 0.0f) {
        m_accum += cfg.rate * dt;
        while (m_accum >= 1.0f) { SpawnOne(); m_accum -= 1.0f; }
    }

    // 2) Integrate + age + interpolate.
    const float damp = std::max(0.0f, 1.0f - cfg.drag * dt);
    for (Particle& p : m_particles) {
        p.vel += cfg.gravity * dt;
        p.vel *= damp;
        p.pos += p.vel * dt;
        p.age += dt;
        const float t = (p.life > 0.0f) ? std::min(p.age / p.life, 1.0f) : 1.0f;
        p.color = glm::mix(p.startColor, p.endColor, t);
        p.size  = glm::mix(p.startSize, p.endSize, t);
    }

    // 3) Remove dead (swap-and-pop compaction).
    for (std::size_t i = 0; i < m_particles.size();) {
        if (m_particles[i].age >= m_particles[i].life) {
            m_particles[i] = m_particles.back();
            m_particles.pop_back();
        } else {
            ++i;
        }
    }
}

void ParticleEmitter::Clear() { m_particles.clear(); m_accum = 0.0f; }

}