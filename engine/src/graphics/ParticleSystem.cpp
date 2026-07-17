#include "engine/graphics/ParticleSystem.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

namespace engine {

namespace {
float SampleCurve(const std::array<float, 4>& curve, float t) {
    const float scaled = std::clamp(t, 0.0f, 1.0f) * 3.0f;
    const int segment = std::min(static_cast<int>(scaled), 2);
    return glm::mix(curve[static_cast<std::size_t>(segment)],
                    curve[static_cast<std::size_t>(segment + 1)], scaled - segment);
}
}

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
    if (cfg.shape == EmitShape::Cone) {
        glm::vec3 axis = cfg.direction;
        axis = glm::length(axis) > 1.0e-6f
            ? glm::normalize(axis) : glm::vec3(0.0f, 1.0f, 0.0f);
        const glm::vec3 helper = std::abs(axis.y) < 0.99f
            ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
        const glm::vec3 tangent = glm::normalize(glm::cross(helper, axis));
        const glm::vec3 bitangent = glm::cross(axis, tangent);
        const float angle = 2.0f * glm::pi<float>() * Rand01();
        const float radius = std::max(cfg.shapeRadius, 0.0f) * std::sqrt(Rand01());
        return (tangent * std::cos(angle) + bitangent * std::sin(angle)) * radius;
    }
    // Random unit vector.
    const float z = Rand01() * 2.0f - 1.0f;
    const float a = 2.0f * glm::pi<float>() * Rand01();
    const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
    const glm::vec3 dir(r * std::cos(a), r * std::sin(a), z);
    // Point/Sphere jitter uniformly within the configured spherical volume.
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
    const float rotationDeg = cfg.rotationMinDeg + (cfg.rotationMaxDeg - cfg.rotationMinDeg) * Rand01();
    const float angularDeg = cfg.angularVelocityMinDeg
        + (cfg.angularVelocityMaxDeg - cfg.angularVelocityMinDeg) * Rand01();
    p.rotation = glm::radians(rotationDeg);
    p.angularVelocity = glm::radians(angularDeg);
    p.frame = 0.0f;
    p.trailPositions[0] = p.pos;
    p.trailCount = 1;
    p.trailDistanceAccumulator = 0.0f;
    m_particles.push_back(p);
}

void ParticleEmitter::Burst(int count) {
    SanitizeParticleConfig(cfg);
    count = std::clamp(count, 0, cfg.maxParticles);
    for (int i = 0; i < count; ++i) SpawnOne();
}

void ParticleEmitter::Update(float dt) {
    static const std::vector<ParticleCollisionShape> noColliders;
    Update(dt, noColliders);
}

void ParticleEmitter::Update(float dt, const std::vector<ParticleCollisionShape>& collisionShapes) {
    SanitizeParticleConfig(cfg);
    m_lastCollisionCount = 0;
    if (dt <= 0.0f) return;

    // 1) Continuous spawn.
    if (emitting && cfg.rate > 0.0f) {
        m_accum += cfg.rate * dt;
        while (m_accum >= 1.0f) { SpawnOne(); m_accum -= 1.0f; }
    }

    // 2) Integrate + age + interpolate.
    // Exponential damping is stable for large time steps and matches the GPU path.
    const float damp = std::exp(-std::max(cfg.drag, 0.0f) * dt);
    for (Particle& p : m_particles) {
        const glm::vec3 previousPosition = p.pos;
        p.vel += cfg.gravity * dt;
        p.vel *= damp;
        p.pos += p.vel * dt;
        if (cfg.collisionEnabled) {
            const float particleRadius = std::max(cfg.collisionRadius, p.size * 0.5f);
            for (const ParticleCollisionShape& shape : collisionShapes) {
                glm::vec3 normal(0.0f);
                float penetration = 0.0f;
                if (shape.type == ParticleCollisionShape::Type::Plane) {
                    const glm::vec3 n = glm::dot(shape.normal, shape.normal) > 1.0e-8f
                        ? glm::normalize(shape.normal) : glm::vec3(0, 1, 0);
                    const float distance = glm::dot(n, p.pos) - shape.offset;
                    if (distance < particleRadius
                        && glm::dot(n, previousPosition) - shape.offset >= -particleRadius) {
                        normal = n;
                        penetration = particleRadius - distance;
                    }
                } else if (shape.type == ParticleCollisionShape::Type::Sphere) {
                    const glm::vec3 delta = p.pos - shape.center;
                    const float combined = std::max(shape.radius, 0.0f) + particleRadius;
                    const float distance2 = glm::dot(delta, delta);
                    if (distance2 < combined * combined) {
                        const float distance = std::sqrt(std::max(distance2, 0.0f));
                        normal = distance > 1.0e-5f ? delta / distance : glm::vec3(0, 1, 0);
                        penetration = combined - distance;
                    }
                } else {
                    const glm::quat inverseRotation = glm::conjugate(glm::normalize(shape.rotation));
                    glm::vec3 local = inverseRotation * (p.pos - shape.center);
                    const glm::vec3 expanded = glm::max(shape.halfExtents, glm::vec3(0.0f))
                        + glm::vec3(particleRadius);
                    if (std::abs(local.x) <= expanded.x && std::abs(local.y) <= expanded.y
                        && std::abs(local.z) <= expanded.z) {
                        const glm::vec3 depths = expanded - glm::abs(local);
                        glm::vec3 localNormal(0.0f);
                        if (depths.x <= depths.y && depths.x <= depths.z) {
                            localNormal.x = local.x < 0.0f ? -1.0f : 1.0f; penetration = depths.x;
                        } else if (depths.y <= depths.z) {
                            localNormal.y = local.y < 0.0f ? -1.0f : 1.0f; penetration = depths.y;
                        } else {
                            localNormal.z = local.z < 0.0f ? -1.0f : 1.0f; penetration = depths.z;
                        }
                        normal = shape.rotation * localNormal;
                    }
                }
                if (penetration <= 0.0f) continue;
                ++m_lastCollisionCount;
                if (cfg.collisionResponse == ParticleCollisionResponse::Kill) {
                    p.age = p.life;
                    break;
                }
                p.pos += normal * penetration;
                const float normalSpeed = glm::dot(p.vel, normal);
                if (normalSpeed < 0.0f) {
                    const glm::vec3 normalVelocity = normal * normalSpeed;
                    const glm::vec3 tangentVelocity = p.vel - normalVelocity;
                    p.vel = tangentVelocity * (1.0f - std::clamp(cfg.collisionFriction, 0.0f, 1.0f))
                        - normalVelocity * std::max(cfg.collisionBounce, 0.0f);
                }
                const float loss = std::clamp(cfg.collisionLifetimeLoss, 0.0f, 1.0f);
                p.age += (p.life - p.age) * loss;
            }
        }
        if (cfg.trailsEnabled) {
            const int segments = std::clamp(cfg.trailSegments, 2, 16);
            const float spacing = std::max(cfg.trailLength / static_cast<float>(segments - 1), 0.001f);
            p.trailDistanceAccumulator += glm::length(p.pos - previousPosition);
            if (p.trailCount <= 0) {
                p.trailPositions[0] = p.pos;
                p.trailCount = 1;
            } else if (p.trailDistanceAccumulator >= spacing) {
                const int last = std::min(p.trailCount, segments - 1);
                for (int i = last; i > 0; --i) p.trailPositions[static_cast<std::size_t>(i)] =
                    p.trailPositions[static_cast<std::size_t>(i - 1)];
                p.trailPositions[0] = p.pos;
                p.trailCount = std::min(p.trailCount + 1, segments);
                p.trailDistanceAccumulator = std::fmod(p.trailDistanceAccumulator, spacing);
            }
        } else {
            p.trailPositions[0] = p.pos;
            p.trailCount = 1;
        }
        p.age += dt;
        const float t = (p.life > 0.0f) ? std::min(p.age / p.life, 1.0f) : 1.0f;
        const float colorT = cfg.useColorCurve ? SampleCurve(cfg.colorCurve, t) : t;
        const float sizeT = cfg.useSizeCurve ? SampleCurve(cfg.sizeCurve, t) : t;
        p.color = glm::mix(p.startColor, p.endColor, colorT);
        p.size  = glm::mix(p.startSize, p.endSize, sizeT);
        p.rotation += p.angularVelocity * dt;
        p.frame = p.age * std::max(cfg.textureFps, 0.0f);
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

void ParticleEmitter::Clear() {
    m_particles.clear();
    m_accum = 0.0f;
    m_lastCollisionCount = 0;
    m_rng = 0x9E3779B9u; // deterministic Restart/scrubbing
}

void ParticleEmitter::Translate(const glm::vec3& delta) {
    for (Particle& particle : m_particles) {
        particle.pos += delta;
        for (int i = 0; i < particle.trailCount; ++i)
            particle.trailPositions[static_cast<std::size_t>(i)] += delta;
    }
}

}
