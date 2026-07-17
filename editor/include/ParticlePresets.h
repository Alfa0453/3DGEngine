#pragma once

#include <engine/graphics/ParticleSystem.h>
#include <engine/graphics/GpuParticleSystem.h>

#include <string>
#include <vector>
#include <algorithm>
#include <cmath>

enum class ParticlePreset { Fire, Smoke, Sparks, Magic, Dust };

inline const char* ParticlePresetName(ParticlePreset preset) {
    switch (preset) {
    case ParticlePreset::Fire: return "Fire";
    case ParticlePreset::Smoke: return "Smoke";
    case ParticlePreset::Sparks: return "Sparks";
    case ParticlePreset::Magic: return "Magic";
    case ParticlePreset::Dust: return "Dust";
    }
    return "Particles";
}

inline engine::ParticleSystemComponent MakeParticlePreset(ParticlePreset preset) {
    engine::ParticleSystemComponent s;
    switch (preset) {
    case ParticlePreset::Fire:
        s.config.rate = 90.0f; s.config.maxParticles = 1200;
        s.config.shape = engine::EmitShape::Cone; s.config.shapeRadius = 0.22f;
        s.config.coneAngleDeg = 18.0f; s.config.speedMin = 0.8f; s.config.speedMax = 2.2f;
        s.config.lifeMin = 0.45f; s.config.lifeMax = 1.1f; s.config.gravity = {0.0f, 0.8f, 0.0f};
        s.config.startColor = {3.5f, 1.1f, 0.15f, 1.0f}; s.config.endColor = {0.7f, 0.02f, 0.0f, 0.0f};
        s.config.startSize = 0.38f; s.config.endSize = 0.04f; s.prewarm = true;
        break;
    case ParticlePreset::Smoke:
        s.config.rate = 28.0f; s.config.maxParticles = 700;
        s.config.shape = engine::EmitShape::Sphere; s.config.shapeRadius = 0.18f;
        s.config.speedMin = 0.25f; s.config.speedMax = 0.8f;
        s.config.lifeMin = 2.0f; s.config.lifeMax = 4.5f; s.config.gravity = {0.0f, 0.35f, 0.0f};
        s.config.drag = 0.18f; s.config.startColor = {0.3f, 0.32f, 0.35f, 0.55f};
        s.config.endColor = {0.12f, 0.13f, 0.15f, 0.0f}; s.config.startSize = 0.3f;
        s.config.endSize = 1.25f; s.config.blend = engine::ParticleBlend::Alpha; s.prewarm = true;
        break;
    case ParticlePreset::Sparks:
        s.config.rate = 0.0f; s.config.maxParticles = 500;
        s.config.shape = engine::EmitShape::Cone; s.config.shapeRadius = 0.08f;
        s.config.coneAngleDeg = 42.0f; s.config.speedMin = 4.0f; s.config.speedMax = 9.0f;
        s.config.lifeMin = 0.35f; s.config.lifeMax = 1.15f; s.config.gravity = {0.0f, -8.0f, 0.0f};
        s.config.startColor = {4.0f, 2.2f, 0.45f, 1.0f}; s.config.endColor = {1.2f, 0.08f, 0.0f, 0.0f};
        s.config.startSize = 0.1f; s.config.endSize = 0.015f; s.burstCount = 80; s.loop = false;
        s.config.trailsEnabled = true; s.config.trailSegments = 7; s.config.trailLength = 1.2f;
        s.config.trailWidth = 0.065f; s.config.trailOpacity = 0.8f;
        break;
    case ParticlePreset::Magic:
        s.config.rate = 55.0f; s.config.maxParticles = 1500;
        s.config.shape = engine::EmitShape::Sphere; s.config.shapeRadius = 0.65f;
        s.config.speedMin = 0.15f; s.config.speedMax = 0.9f;
        s.config.lifeMin = 1.2f; s.config.lifeMax = 2.5f; s.config.gravity = {0.0f, 0.25f, 0.0f};
        s.config.drag = 0.35f; s.config.startColor = {1.2f, 0.25f, 3.6f, 1.0f};
        s.config.endColor = {0.1f, 1.4f, 3.0f, 0.0f}; s.config.startSize = 0.2f;
        s.config.endSize = 0.04f; s.prewarm = true;
        break;
    case ParticlePreset::Dust:
        s.config.rate = 18.0f; s.config.maxParticles = 500;
        s.config.shape = engine::EmitShape::Sphere; s.config.shapeRadius = 1.2f;
        s.config.speedMin = 0.05f; s.config.speedMax = 0.3f;
        s.config.lifeMin = 3.0f; s.config.lifeMax = 7.0f; s.config.gravity = {0.0f, 0.03f, 0.0f};
        s.config.drag = 0.6f; s.config.startColor = {0.72f, 0.62f, 0.45f, 0.35f};
        s.config.endColor = {0.55f, 0.48f, 0.36f, 0.0f}; s.config.startSize = 0.08f;
        s.config.endSize = 0.14f; s.config.blend = engine::ParticleBlend::Alpha; s.prewarm = true;
        break;
    }
    return s;
}

inline std::vector<std::string> ValidateParticleSettings(const engine::ParticleSystemComponent& s) {
    std::vector<std::string> warnings;
    if (s.config.rate <= 0.0f && s.burstCount <= 0)
        warnings.emplace_back("Nothing will be emitted: Rate and Burst Count are both zero.");
    if (s.config.direction.x == 0.0f && s.config.direction.y == 0.0f && s.config.direction.z == 0.0f)
        warnings.emplace_back("Direction is zero; the emitter will fall back to world up.");
    if (s.prewarm && !s.loop)
        warnings.emplace_back("Prewarm is normally used with Loop enabled.");
    if (s.config.maxParticles < s.config.rate * s.config.lifeMax)
        warnings.emplace_back("Maximum may clip particles before the configured rate reaches steady state.");
    if (s.burstInterval > 0.0f && s.burstCount <= 0)
        warnings.emplace_back("Burst Interval has no effect while Burst Count is zero.");
    if (s.simulationSpeed <= 0.0f)
        warnings.emplace_back("Simulation Speed is zero; the effect is paused.");
    if (s.config.texturePath.empty()
        && (s.config.textureColumns > 1 || s.config.textureRows > 1))
        warnings.emplace_back("Flipbook rows and columns have no effect without a texture.");
    if (!s.config.texturePath.empty()
        && s.config.textureColumns * s.config.textureRows > 1
        && s.config.textureFps <= 0.0f)
        warnings.emplace_back("Flipbook frame rate is zero; only the first frame is shown.");
    const float expectedTravel = std::max(std::abs(s.config.speedMin), std::abs(s.config.speedMax))
        * s.config.lifeMax + s.config.shapeRadius;
    if (s.config.cullingEnabled && s.config.boundsRadius < expectedTravel)
        warnings.emplace_back("Bounds Radius may be too small for the configured speed and lifetime.");
    if (s.config.collisionEnabled && s.config.collisionRadius <= 0.0f
        && s.config.startSize <= 0.0f && s.config.endSize <= 0.0f)
        warnings.emplace_back("Collision has no effective radius because particle size and Collision Radius are zero.");
    if (s.config.trailsEnabled && s.config.trailWidth <= 0.0f)
        warnings.emplace_back("Trails are enabled but Trail Width is zero.");
    if (s.config.renderMode == engine::ParticleRenderMode::Mesh
        && s.config.meshShape == engine::ParticleMeshShape::Model
        && s.config.meshPath.empty())
        warnings.emplace_back("Mesh rendering uses Model Asset but no model has been assigned; a cube will be shown.");
    if (s.config.renderMode == engine::ParticleRenderMode::Mesh && s.config.meshScale <= 0.0f)
        warnings.emplace_back("Mesh Scale must be greater than zero.");
    if (s.config.simulationBackend == engine::ParticleSimulationBackend::GPU
        && !engine::IsGpuParticleSimulationSupported())
        warnings.emplace_back("GPU Compute was requested but OpenGL 4.3 is unavailable; CPU fallback will be used.");
    return warnings;
}
