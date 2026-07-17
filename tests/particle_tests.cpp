#include "ParticleAsset.h"

#include <engine/ecs/Components.h>
#include <engine/ecs/Registry.h>
#include <engine/graphics/ParticleSystem.h>
#include <engine/graphics/RuntimeParticleSystem.h>
#include <engine/assets/ParticleAsset.h>
#include <engine/physics/PhysicsWorld.h>
#include <engine/physics/PhysicsComponents.h>

#include <cmath>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void Check(bool condition, const char* message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "[FAIL] " << message << '\n';
}

bool Near(float a, float b, float epsilon = 0.0001f) {
    return std::fabs(a - b) <= epsilon;
}

void TestDeterministicRestart() {
    engine::ParticleEmitter first;
    first.cfg.shape = engine::EmitShape::Sphere;
    first.cfg.shapeRadius = 2.0f;
    first.cfg.speedMin = 0.5f;
    first.cfg.speedMax = 4.0f;
    first.cfg.lifeMin = 2.0f;
    first.cfg.lifeMax = 3.0f;
    first.Burst(24);
    const std::vector<engine::Particle> reference = first.Particles();

    first.Clear();
    first.Burst(24);
    Check(first.Alive() == reference.size(), "Clear + Burst restores the same particle count");
    for (std::size_t i = 0; i < reference.size() && i < first.Alive(); ++i) {
        const engine::Particle& a = reference[i];
        const engine::Particle& b = first.Particles()[i];
        Check(glm::length(a.pos - b.pos) < 0.00001f, "Restart reproduces spawn positions");
        Check(glm::length(a.vel - b.vel) < 0.00001f, "Restart reproduces velocities");
        Check(Near(a.life, b.life), "Restart reproduces lifetimes");
    }
}

void TestLimitsAndCleanup() {
    engine::ParticleEmitter emitter;
    emitter.cfg.maxParticles = 7;
    emitter.cfg.lifeMin = 0.1f;
    emitter.cfg.lifeMax = 0.1f;
    emitter.Burst(100);
    Check(emitter.Alive() == 7, "Burst obeys maxParticles");
    emitter.Update(0.11f);
    Check(emitter.Alive() == 0, "Expired particles are removed");

    emitter.Clear();
    emitter.cfg.rate = 10.0f;
    emitter.cfg.lifeMin = 10.0f;
    emitter.cfg.lifeMax = 10.0f;
    emitter.Update(0.05f);
    Check(emitter.Alive() == 0, "Fractional emission is accumulated without early spawn");
    emitter.Update(0.05f);
    Check(emitter.Alive() == 1, "Fractional emission spawns at the expected boundary");
}

void TestShapeParityAndValidation() {
    engine::ParticleEmitter cone;
    cone.position = glm::vec3(2.0f, 3.0f, 4.0f);
    cone.cfg.shape = engine::EmitShape::Cone;
    cone.cfg.shapeRadius = 2.0f;
    cone.cfg.direction = glm::vec3(0, 1, 0);
    cone.cfg.rate = 0.0f;
    cone.cfg.lifeMin = cone.cfg.lifeMax = 5.0f;
    cone.Burst(128);
    for (const engine::Particle& particle : cone.Particles()) {
        const glm::vec3 offset = particle.pos - cone.position;
        Check(std::abs(offset.y) < 0.00001f,
              "Cone particles spawn on the base disc");
        Check(glm::length(glm::vec2(offset.x, offset.z)) <= 2.0001f,
              "Cone base spawn respects its radius");
    }

    engine::EmitterConfig invalid;
    invalid.rate = std::numeric_limits<float>::quiet_NaN();
    invalid.maxParticles = -10;
    invalid.speedMin = 5.0f;
    invalid.speedMax = -2.0f;
    invalid.lifeMin = -1.0f;
    invalid.lifeMax = 0.0f;
    invalid.textureColumns = 0;
    engine::SanitizeParticleConfig(invalid);
    Check(invalid.rate == 0.0f && invalid.maxParticles == 1,
          "Configuration sanitization repairs unsafe rate and capacity");
    Check(invalid.speedMin <= invalid.speedMax && invalid.lifeMin >= 0.001f,
          "Configuration sanitization orders ranges and repairs lifetime");
    Check(invalid.textureColumns == 1,
          "Configuration sanitization repairs texture sheet dimensions");

    engine::ParticleEmitter damping;
    damping.cfg.rate = 0.0f;
    damping.cfg.shapeRadius = 0.0f;
    damping.cfg.direction = glm::vec3(1, 0, 0);
    damping.cfg.coneAngleDeg = 0.0f;
    damping.cfg.speedMin = damping.cfg.speedMax = 10.0f;
    damping.cfg.lifeMin = damping.cfg.lifeMax = 5.0f;
    damping.cfg.gravity = glm::vec3(0.0f);
    damping.cfg.drag = 2.0f;
    damping.Burst(1);
    damping.Update(0.5f);
    Check(Near(damping.Particles()[0].vel.x, 10.0f * std::exp(-1.0f), 0.0002f),
          "CPU drag uses stable exponential damping");
}

void TestParticleCollisionResponses() {
    engine::ParticleCollisionShape ground;
    ground.type = engine::ParticleCollisionShape::Type::Plane;
    ground.normal = glm::vec3(0, 1, 0);
    ground.offset = 0.0f;
    const std::vector<engine::ParticleCollisionShape> colliders{ground};

    engine::ParticleEmitter bounce;
    bounce.position = glm::vec3(0.0f, 0.25f, 0.0f);
    bounce.cfg.shapeRadius = 0.0f;
    bounce.cfg.rate = 0.0f;
    bounce.cfg.direction = glm::vec3(0, -1, 0);
    bounce.cfg.coneAngleDeg = 0.0f;
    bounce.cfg.speedMin = bounce.cfg.speedMax = 2.0f;
    bounce.cfg.lifeMin = bounce.cfg.lifeMax = 5.0f;
    bounce.cfg.startSize = bounce.cfg.endSize = 0.1f;
    bounce.cfg.gravity = glm::vec3(0.0f);
    bounce.cfg.collisionEnabled = true;
    bounce.cfg.collisionBounce = 0.5f;
    bounce.cfg.collisionFriction = 0.0f;
    bounce.Burst(1);
    bounce.Update(0.2f, colliders);
    Check(bounce.LastCollisionCount() == 1, "Plane collision is reported");
    Check(bounce.Alive() == 1 && bounce.Particles()[0].vel.y > 0.0f,
          "Bounce response reflects particle velocity");

    engine::ParticleEmitter kill;
    kill.position = glm::vec3(0.0f, 0.1f, 0.0f);
    kill.cfg = bounce.cfg;
    kill.cfg.collisionResponse = engine::ParticleCollisionResponse::Kill;
    kill.Burst(1);
    kill.Update(0.1f, colliders);
    Check(kill.Alive() == 0, "Kill response removes particles on contact");
}

void TestTrailHistory() {
    engine::ParticleEmitter emitter;
    emitter.cfg.rate = 0.0f;
    emitter.cfg.shapeRadius = 0.0f;
    emitter.cfg.direction = glm::vec3(1, 0, 0);
    emitter.cfg.coneAngleDeg = 0.0f;
    emitter.cfg.speedMin = emitter.cfg.speedMax = 1.0f;
    emitter.cfg.lifeMin = emitter.cfg.lifeMax = 5.0f;
    emitter.cfg.gravity = glm::vec3(0.0f);
    emitter.cfg.trailsEnabled = true;
    emitter.cfg.trailSegments = 5;
    emitter.cfg.trailLength = 1.0f;
    emitter.Burst(1);
    for (int i = 0; i < 5; ++i) emitter.Update(0.25f);
    Check(emitter.Alive() == 1 && emitter.Particles()[0].trailCount == 5,
          "Trail history fills to its configured segment limit");
    const float before = emitter.Particles()[0].trailPositions[0].y;
    emitter.Translate(glm::vec3(0, 3, 0));
    Check(Near(emitter.Particles()[0].trailPositions[0].y, before + 3.0f),
          "Local-space translation moves particle trail history");
}

void TestRuntimeControlsAndEntityCleanup() {
    engine::ecs::Registry registry;
    const engine::ecs::Entity entity = registry.Create();
    registry.Add<engine::ecs::Transform>(entity, engine::ecs::Transform{});
    engine::ParticleSystemComponent system;
    system.autoplay = false;
    system.burstCount = 4;
    system.config.rate = 0.0f;
    system.config.lifeMin = 10.0f;
    system.config.lifeMax = 10.0f;
    registry.Add<engine::ParticleSystemComponent>(entity, system);

    Check(engine::PlayParticleSystem(registry, entity), "Runtime particle system can play");
    Check(engine::BurstParticleSystem(registry, entity), "Runtime particle system can burst");
    Check(registry.Get<engine::ParticleSystemComponent>(entity).emitter.Alive() == 4,
          "Runtime burst uses the configured burst count");
    Check(engine::StopParticleSystem(registry, entity, false), "Runtime particle system can stop");
    Check(engine::ClearParticleSystem(registry, entity), "Runtime particle system can clear");
    Check(registry.Get<engine::ParticleSystemComponent>(entity).emitter.Alive() == 0,
          "Clear removes all live runtime particles");

    registry.Destroy(entity);
    Check(!registry.Valid(entity), "Destroyed particle entity is invalidated");
    Check(!registry.Has<engine::ParticleSystemComponent>(entity),
          "Destroy removes the particle component from its pool");
}

void TestRuntimeColliderIntegration() {
    engine::ecs::Registry registry;
    const engine::ecs::Entity floor = registry.Create();
    registry.Add<engine::ecs::Transform>(floor, engine::ecs::Transform{});
    registry.Add<engine::ecs::Collider>(floor, engine::ecs::Collider::MakePlane(glm::vec3(0, 1, 0), 0.0f));

    const engine::ecs::Entity effect = registry.Create();
    engine::ecs::Transform effectTransform;
    effectTransform.position = glm::vec3(0.0f, 0.25f, 0.0f);
    registry.Add<engine::ecs::Transform>(effect, effectTransform);
    engine::ParticleSystemComponent system;
    system.burstCount = 1;
    system.loop = false;
    system.config.rate = 0.0f;
    system.config.shapeRadius = 0.0f;
    system.config.direction = glm::vec3(0, -1, 0);
    system.config.coneAngleDeg = 0.0f;
    system.config.speedMin = system.config.speedMax = 2.0f;
    system.config.lifeMin = system.config.lifeMax = 5.0f;
    system.config.startSize = system.config.endSize = 0.1f;
    system.config.gravity = glm::vec3(0.0f);
    system.config.collisionEnabled = true;
    system.config.collisionBounce = 0.5f;
    registry.Add<engine::ParticleSystemComponent>(effect, system);

    engine::UpdateParticleSystems(registry, 0.2f);
    const engine::ParticleSystemComponent& updated =
        registry.Get<engine::ParticleSystemComponent>(effect);
    Check(updated.emitter.Alive() == 1 && updated.emitter.Particles()[0].vel.y > 0.0f,
          "Runtime particle update collides with ECS physics colliders");
}

void TestMultiEmitterRuntime() {
    engine::ecs::Registry registry;
    const engine::ecs::Entity effectEntity = registry.Create();
    registry.Add<engine::ecs::Transform>(effectEntity, engine::ecs::Transform{});
    engine::ParticleEffectComponent effect;
    for (int i = 0; i < 2; ++i) {
        engine::ParticleEffectLayer layer;
        layer.name = i == 0 ? "Sparks" : "Smoke";
        layer.offset = glm::vec3(static_cast<float>(i), 0, 0);
        layer.system.config.rate = 0.0f;
        layer.system.config.lifeMin = layer.system.config.lifeMax = 5.0f;
        layer.system.burstCount = i + 2;
        effect.layers.push_back(layer);
    }
    registry.Add<engine::ParticleEffectComponent>(effectEntity, effect);
    engine::UpdateParticleSystems(registry, 0.016f);
    engine::ParticleEffectComponent& updated = registry.Get<engine::ParticleEffectComponent>(effectEntity);
    Check(updated.layers[0].system.emitter.Alive() == 2
          && updated.layers[1].system.emitter.Alive() == 3,
          "Multi-emitter runtime updates every enabled layer");
    Check(engine::ClearParticleSystem(registry, effectEntity),
          "Particle actions control a composed effect");
    Check(updated.layers[0].system.emitter.Alive() == 0
          && updated.layers[1].system.emitter.Alive() == 0,
          "Clear action affects every effect layer");
    Check(engine::SetParticleEmissionRate(registry, effectEntity, 17.0f)
          && Near(updated.layers[0].system.config.rate, 17.0f)
          && Near(updated.layers[1].system.config.rate, 17.0f),
          "Rate control affects every composed effect layer");
    Check(engine::SetParticleSimulationSpeed(registry, effectEntity, 0.5f)
          && Near(updated.layers[0].system.simulationSpeed, 0.5f)
          && Near(updated.layers[1].system.simulationSpeed, 0.5f),
          "Simulation speed control affects every composed effect layer");
    engine::PlayParticleSystem(registry, effectEntity, true);
    engine::UpdateParticleSystems(registry, 0.016f);
    Check(engine::IsParticleSystemPlaying(registry, effectEntity),
          "Layered effect reports its playing state");
    Check(engine::ParticleSystemAliveCount(registry, effectEntity) == 5,
          "Layered effect reports the sum of live particles");
}

void TestPlaybackTimingBoundaries() {
    engine::ecs::Registry registry;
    const engine::ecs::Entity entity = registry.Create();
    registry.Add<engine::ecs::Transform>(entity, engine::ecs::Transform{});
    engine::ParticleSystemComponent system;
    system.startDelay = 0.05f;
    system.duration = 0.075f;
    system.loop = true;
    system.config.rate = 20.0f;
    system.config.lifeMin = system.config.lifeMax = 5.0f;
    registry.Add<engine::ParticleSystemComponent>(entity, system);
    engine::UpdateParticleSystems(registry, 0.10f);
    const auto& afterDelay = registry.Get<engine::ParticleSystemComponent>(entity);
    Check(afterDelay.emitter.Alive() == 1,
          "Frame time remaining after start delay still simulates emission");
    engine::UpdateParticleSystems(registry, 0.10f);
    const auto& looped = registry.Get<engine::ParticleSystemComponent>(entity);
    Check(looped.elapsed < looped.duration,
          "Looping playback retains only the duration remainder");
}

void TestGameplayActions() {
    engine::ecs::Registry registry;
    const engine::ecs::Entity trigger = registry.Create();
    const engine::ecs::Entity target = registry.Create();
    registry.Add<engine::ecs::RuntimeName>(target, engine::ecs::RuntimeName{"ImpactFX"});
    registry.Add<engine::ecs::Transform>(target, engine::ecs::Transform{});
    engine::ParticleSystemComponent system;
    system.autoplay = false;
    system.burstCount = 3;
    system.config.rate = 0.0f;
    system.config.lifeMin = 10.0f;
    system.config.lifeMax = 10.0f;
    registry.Add<engine::ParticleSystemComponent>(target, system);
    registry.Add<engine::TriggerParticleAction>(trigger, engine::TriggerParticleAction{
        "ImpactFX", engine::ParticleAction::Burst, engine::ParticleAction::Clear});

    engine::CollisionEvent enter;
    enter.a = trigger;
    enter.b = target;
    enter.phase = engine::CollisionEvent::Phase::Enter;
    enter.trigger = true;
    engine::ProcessParticleCollisionEvents(registry, std::vector<engine::CollisionEvent>{enter});
    Check(registry.Get<engine::ParticleSystemComponent>(target).emitter.Alive() == 3,
          "Trigger enter runs the configured particle action");

    enter.phase = engine::CollisionEvent::Phase::Exit;
    engine::ProcessParticleCollisionEvents(registry, std::vector<engine::CollisionEvent>{enter});
    Check(registry.Get<engine::ParticleSystemComponent>(target).emitter.Alive() == 0,
          "Trigger exit runs the configured particle action");

    Check(engine::ProcessParticleAnimationEvent(registry, target, "Particle.Burst"),
          "Animation event controls its emitter");
    Check(engine::ProcessParticleAnimationEvent(registry, trigger, "Particle.Clear:ImpactFX"),
          "Animation event resolves a named emitter");
    Check(registry.Get<engine::ParticleSystemComponent>(target).emitter.Alive() == 0,
          "Named animation action affected the intended emitter");
}

void TestAssetRoundTripAndCompatibility() {
    const std::filesystem::path folder = std::filesystem::current_path() / "particle_test_output";
    const std::filesystem::path currentAsset = folder / "roundtrip.particle";
    const std::filesystem::path legacyAsset = folder / "legacy_v1.particle";
    const std::filesystem::path effectAsset = folder / "impact.particlefx";
    const std::filesystem::path runtimeAsset = folder / "runtime_roundtrip.particle";
    std::error_code ec;
    std::filesystem::remove_all(folder, ec);

    engine::ParticleSystemComponent source;
    source.enabled = false;
    source.prewarm = true;
    source.duration = 7.25f;
    source.burstCount = 13;
    source.config.shape = engine::EmitShape::Sphere;
    source.config.texturePath = "Content/Textures/smoke.png";
    source.config.textureColumns = 4;
    source.config.textureRows = 2;
    source.config.useSizeCurve = true;
    source.config.sizeCurve = {{0.0f, 0.8f, 0.4f, 1.0f}};
    source.config.cullingEnabled = false;
    source.config.boundsRadius = 12.5f;
    source.config.collisionEnabled = true;
    source.config.collisionResponse = engine::ParticleCollisionResponse::Kill;
    source.config.collisionRadius = 0.15f;
    source.config.collisionBounce = 0.8f;
    source.config.collisionFriction = 0.3f;
    source.config.collisionLifetimeLoss = 0.2f;
    source.config.trailsEnabled = true;
    source.config.trailSegments = 12;
    source.config.trailLength = 3.5f;
    source.config.trailWidth = 0.22f;
    source.config.trailOpacity = 0.6f;
    source.config.renderMode = engine::ParticleRenderMode::Mesh;
    source.config.meshShape = engine::ParticleMeshShape::Model;
    source.config.meshPath = "Content/Meshes/debris.obj";
    source.config.meshScale = 0.75f;
    source.config.meshAlignToVelocity = false;
    source.config.simulationBackend = engine::ParticleSimulationBackend::GPU;
    std::swap(source.config.modules[0], source.config.modules[4]);
    engine::NormalizeParticleModuleStack(source.config, false);
    engine::ParticleModule wind{engine::ParticleModuleType::Forces, true};
    wind.instanceId = 99;
    wind.name = "Side Wind";
    wind.parametersInitialized = true;
    wind.vectorValue = glm::vec3(1.5f, 0.0f, 0.0f);
    wind.valueA = 0.2f;
    source.config.modules.push_back(wind);
    engine::ParticleModule velocityBoost{engine::ParticleModuleType::InitialVelocity, true};
    velocityBoost.instanceId = 100;
    velocityBoost.name = "Velocity Boost";
    velocityBoost.parametersInitialized = true;
    velocityBoost.valueA = 1.0f;
    velocityBoost.valueB = 2.0f;
    source.config.modules.push_back(velocityBoost);
    engine::ParticleModule spin{engine::ParticleModuleType::Rotation, true};
    spin.instanceId = 101;
    spin.name = "Extra Spin";
    spin.parametersInitialized = true;
    spin.valueA = 10.0f;
    spin.valueB = 20.0f;
    spin.valueC = 30.0f;
    spin.valueD = 40.0f;
    source.config.modules.push_back(spin);
    engine::ParticleModule tint{engine::ParticleModuleType::ColorOverLife, true};
    tint.instanceId = 102;
    tint.name = "Cool Tint";
    tint.parametersInitialized = true;
    tint.colorValueA = glm::vec4(0.5f, 0.8f, 1.0f, 1.0f);
    tint.colorValueB = glm::vec4(0.25f, 0.6f, 1.0f, 1.0f);
    tint.curveEnabled = true;
    tint.curveValues = {{1.0f, 0.9f, 0.8f, 1.0f}};
    source.config.modules.push_back(tint);
    engine::ParticleModule sizePulse{engine::ParticleModuleType::SizeOverLife, true};
    sizePulse.instanceId = 103;
    sizePulse.name = "Size Pulse";
    sizePulse.parametersInitialized = true;
    sizePulse.valueA = 1.5f;
    sizePulse.valueB = 0.5f;
    sizePulse.curveEnabled = true;
    sizePulse.curveValues = {{1.0f, 0.75f, 0.6f, 1.0f}};
    source.config.modules.push_back(sizePulse);
    engine::NormalizeParticleModuleStack(source.config, true);
    std::string error;
    Check(particle_asset::Save(currentAsset.string(), source, &error), "Particle asset saves");
    engine::ParticleSystemComponent loaded;
    Check(particle_asset::Load(currentAsset.string(), &loaded, &error), "Particle asset reloads");
    Check(engine::SaveParticleAsset(runtimeAsset.string(), source, &error),
          "Engine particle asset saver writes runtime-compatible assets");
    engine::ParticleSystemComponent runtimeLoaded;
    Check(engine::LoadParticleAsset(runtimeAsset.string(), &runtimeLoaded, &error)
          && runtimeLoaded.config.modules.size() == source.config.modules.size(),
          "Engine particle asset saver and loader round trip module data");
    Check(loaded.enabled == source.enabled && loaded.prewarm == source.prewarm,
          "Asset round trip preserves playback flags");
    Check(loaded.config.shape == source.config.shape && loaded.burstCount == source.burstCount,
          "Asset round trip preserves emitter settings");
    Check(loaded.config.texturePath == source.config.texturePath,
          "Asset round trip preserves texture paths");
    Check(!loaded.config.cullingEnabled && Near(loaded.config.boundsRadius, 12.5f),
          "Asset round trip preserves culling bounds");
    Check(loaded.config.collisionEnabled
          && loaded.config.collisionResponse == engine::ParticleCollisionResponse::Kill
          && Near(loaded.config.collisionRadius, 0.15f)
          && Near(loaded.config.collisionLifetimeLoss, 0.2f),
          "Asset round trip preserves collision settings");
    Check(loaded.config.trailsEnabled && loaded.config.trailSegments == 12
          && Near(loaded.config.trailLength, 3.5f) && Near(loaded.config.trailWidth, 0.22f)
          && Near(loaded.config.trailOpacity, 0.6f),
          "Asset round trip preserves trail settings");
    Check(loaded.config.renderMode == engine::ParticleRenderMode::Mesh
          && loaded.config.meshShape == engine::ParticleMeshShape::Model
          && loaded.config.meshPath == source.config.meshPath
          && Near(loaded.config.meshScale, 0.75f)
          && !loaded.config.meshAlignToVelocity,
          "Asset round trip preserves mesh renderer settings");
    Check(loaded.config.simulationBackend == engine::ParticleSimulationBackend::GPU,
          "Asset round trip preserves simulation backend selection");
    Check(loaded.config.modules.size() == source.config.modules.size()
          && loaded.config.modules[0].type == source.config.modules[0].type
          && engine::IsParticleModuleEnabled(loaded.config, engine::ParticleModuleType::Collision)
          && engine::IsParticleModuleEnabled(loaded.config, engine::ParticleModuleType::Trails),
          "Asset round trip preserves module order and compiled optional state");
    const auto loadedWind = std::find_if(loaded.config.modules.begin(), loaded.config.modules.end(),
        [](const engine::ParticleModule& module) { return module.name == "Side Wind"; });
    Check(loadedWind != loaded.config.modules.end() && loadedWind->instanceId == 99
          && Near(loadedWind->vectorValue.x, 1.5f) && Near(loadedWind->valueA, 0.2f)
          && Near(loaded.config.gravity.x, 1.5f),
          "Duplicate Force modules preserve identity, parameters, and compiled contribution");
    const auto loadedVelocity = std::find_if(loaded.config.modules.begin(), loaded.config.modules.end(),
        [](const engine::ParticleModule& module) { return module.name == "Velocity Boost"; });
    const auto loadedSpin = std::find_if(loaded.config.modules.begin(), loaded.config.modules.end(),
        [](const engine::ParticleModule& module) { return module.name == "Extra Spin"; });
    Check(loadedVelocity != loaded.config.modules.end() && loadedVelocity->instanceId == 100
          && Near(loadedVelocity->valueB, 2.0f)
          && loadedSpin != loaded.config.modules.end() && loadedSpin->instanceId == 101
          && Near(loadedSpin->valueD, 40.0f)
          && Near(loaded.config.speedMin, source.config.speedMin)
          && Near(loaded.config.angularVelocityMaxDeg, source.config.angularVelocityMaxDeg),
          "Velocity and Rotation instances preserve parameters and compiled ranges");
    const auto loadedTint = std::find_if(loaded.config.modules.begin(), loaded.config.modules.end(),
        [](const engine::ParticleModule& module) { return module.name == "Cool Tint"; });
    const auto loadedPulse = std::find_if(loaded.config.modules.begin(), loaded.config.modules.end(),
        [](const engine::ParticleModule& module) { return module.name == "Size Pulse"; });
    Check(loadedTint != loaded.config.modules.end() && loadedTint->instanceId == 102
          && loadedTint->curveEnabled && Near(loadedTint->colorValueA.r, 0.5f)
          && loadedPulse != loaded.config.modules.end() && loadedPulse->instanceId == 103
          && Near(loadedPulse->valueA, 1.5f) && loadedPulse->curveEnabled
          && Near(loaded.config.startColor.r, source.config.startColor.r)
          && Near(loaded.config.startSize, source.config.startSize),
          "Color and Size instances preserve composited endpoints and curve parameters");
    bool stagesOrdered = true;
    for (std::size_t i = 1; i < loaded.config.modules.size(); ++i) {
        stagesOrdered &= static_cast<int>(loaded.config.modules[i - 1].stage)
            <= static_cast<int>(loaded.config.modules[i].stage);
    }
    Check(stagesOrdered && loadedWind != loaded.config.modules.end()
          && loadedWind->stage == engine::ParticleModuleStage::Update
          && loaded.config.modules.back().stage == engine::ParticleModuleStage::Render,
          "Particle modules preserve canonical Spawn, Update, and Render execution stages");
    const auto connections = engine::BuildParticleModuleConnections(loaded.config);
    const std::size_t enabledModules = static_cast<std::size_t>(std::count_if(
        loaded.config.modules.begin(), loaded.config.modules.end(),
        [](const engine::ParticleModule& module) { return module.enabled; }));
    const bool hasRenderConnection = std::any_of(connections.begin(), connections.end(),
        [](const engine::ParticleModuleConnection& connection) {
            return connection.channel == engine::ParticleModuleDataChannel::RenderData;
        });
    Check(engine::ValidateParticleModulePipeline(loaded.config).empty()
          && connections.size() + 1 == enabledModules && hasRenderConnection,
          "Compiled module graph has valid typed connections across execution stages");
    std::vector<engine::ParticleEffectLayer> effectLayers(2);
    effectLayers[0].name = "Flash";
    effectLayers[0].assetPath = currentAsset.string();
    effectLayers[1].name = "Offset Smoke";
    effectLayers[1].assetPath = currentAsset.string();
    effectLayers[1].offset = glm::vec3(1.0f, 2.0f, 3.0f);
    effectLayers[1].enabled = false;
    Check(particle_asset::SaveEffect(effectAsset.string(), effectLayers, &error),
          "Particle effect asset saves layer references");
    std::vector<engine::ParticleEffectLayer> loadedEffect;
    Check(particle_asset::LoadEffect(effectAsset.string(), &loadedEffect, &error),
          "Particle effect asset reloads referenced emitters");
    Check(loadedEffect.size() == 2 && loadedEffect[1].name == "Offset Smoke"
          && !loadedEffect[1].enabled && Near(loadedEffect[1].offset.y, 2.0f),
          "Effect round trip preserves layer order, state, and offsets");

    std::filesystem::create_directories(folder, ec);
    std::ofstream legacy(legacyAsset);
    legacy << "3DGParticle 1\n"
           << "1 1 1 0 5 0 1 1 2 0\n"
           << "60 100 0 0 0 1 0 20 1 2 1 2 0 -1 0 0\n"
           << "1 1 1 1 1 0 0 0 0.2 0.01 0\n"
           << "0 0 0 0 0 0 0 0.333333 0.666667 1 0 0.333333 0.666667 1\n"
           << "\"-\" 1 1 0 1\n";
    legacy.close();
    engine::ParticleSystemComponent legacyLoaded;
    Check(particle_asset::Load(legacyAsset.string(), &legacyLoaded, &error),
          "Version 1 particle assets remain readable");
    Check(legacyLoaded.config.cullingEnabled && Near(legacyLoaded.config.boundsRadius, 5.0f),
          "Legacy assets receive safe default bounds");
    Check(legacyLoaded.config.modules.size() == 10,
          "Legacy assets receive the default particle module stack");

    std::filesystem::remove_all(folder, ec);
}

} // namespace

int main() {
    TestDeterministicRestart();
    TestLimitsAndCleanup();
    TestShapeParityAndValidation();
    TestParticleCollisionResponses();
    TestTrailHistory();
    TestRuntimeControlsAndEntityCleanup();
    TestRuntimeColliderIntegration();
    TestMultiEmitterRuntime();
    TestPlaybackTimingBoundaries();
    TestGameplayActions();
    TestAssetRoundTripAndCompatibility();
    if (g_failures != 0) {
        std::cerr << g_failures << " particle regression check(s) failed.\n";
        return 1;
    }
    std::cout << "All particle regression checks passed.\n";
    return 0;
}
