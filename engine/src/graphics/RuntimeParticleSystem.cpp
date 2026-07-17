#include "engine/graphics/RuntimeParticleSystem.h"
#include "engine/graphics/GpuParticleSystem.h"

#include "engine/ecs/Components.h"
#include "engine/ecs/Registry.h"
#include "engine/physics/PhysicsWorld.h"
#include "engine/physics/PhysicsComponents.h"

#include <algorithm>
#include <cmath>

namespace engine {
namespace {

float ConsumeStartDelay(ParticleSystemComponent& system, float dt) {
    const float delay = std::max(system.startDelay, 0.0f);
    if (system.delayElapsed >= delay) return dt;
    const float consumed = std::min(dt, delay - system.delayElapsed);
    system.delayElapsed += consumed;
    return dt - consumed;
}

int ConsumeRepeatingBursts(ParticleSystemComponent& system, float dt) {
    if (system.burstCount <= 0 || system.burstInterval <= 0.0f || dt <= 0.0f) return 0;
    const double total = static_cast<double>(system.burstElapsed) + dt;
    const int repeats = static_cast<int>(std::min<double>(
        std::floor(total / system.burstInterval),
        std::max(system.config.maxParticles / std::max(system.burstCount, 1) + 1, 1)));
    system.burstElapsed = static_cast<float>(std::fmod(total, system.burstInterval));
    return repeats * system.burstCount;
}

void AdvanceDuration(ParticleSystemComponent& system, float activeDt) {
    system.elapsed += activeDt;
    if (system.duration <= 0.0f || system.elapsed < system.duration) return;
    if (system.loop) {
        system.elapsed = std::fmod(system.elapsed, system.duration);
        system.initialBurstFired = false;
        system.burstElapsed = 0.0f;
    } else {
        system.elapsed = system.duration;
        system.playing = false;
        system.emitter.emitting = false;
    }
}

void RestoreCpuFallback(ParticleSystemComponent& system, const glm::vec3& position) {
    system.emitter.Clear();
    system.emitter.cfg = system.config;
    system.emitter.position = position;
    system.emitter.emitting = system.playing && system.config.rate > 0.0f;
    const float warmTime = std::clamp(system.elapsed, 0.0f, 5.0f);
    if (system.initialBurstFired && system.burstCount > 0)
        system.emitter.Burst(system.burstCount);
    for (float simulated = 0.0f; simulated < warmTime;) {
        const float step = std::min(1.0f / 60.0f, warmTime - simulated);
        system.emitter.Update(step);
        simulated += step;
    }
    system.gpuBackendActive = false;
}

void ResetPlayback(ParticleSystemComponent& system, const glm::vec3& position) {
    SanitizeParticleConfig(system.config);
    system.duration = std::max(FiniteParticleValue(system.duration, 5.0f), 0.0f);
    system.startDelay = std::max(FiniteParticleValue(system.startDelay, 0.0f), 0.0f);
    system.simulationSpeed = std::clamp(
        FiniteParticleValue(system.simulationSpeed, 1.0f), 0.0f, 1000.0f);
    system.burstCount = std::clamp(system.burstCount, 0, system.config.maxParticles);
    system.burstInterval = std::max(FiniteParticleValue(system.burstInterval, 0.0f), 0.0f);
    system.emitter.Clear();
    system.emitter.cfg = system.config;
    system.emitter.position = position;
    system.emitter.emitting = false;
    system.initialized = true;
    system.playing = system.enabled && system.autoplay;
    system.initialBurstFired = false;
    system.elapsed = 0.0f;
    system.delayElapsed = 0.0f;
    system.burstElapsed = 0.0f;
    system.lastPosition = position;
    system.gpuSpawnAccumulator = 0.0f;
    system.gpuPendingBurst = 0;
    system.gpuBackendActive = false;
    if (system.gpuEmitter) system.gpuEmitter->Clear();

    const bool expectedGpu = system.config.simulationBackend != ParticleSimulationBackend::CPU
        && IsGpuParticleConfigurationSupported(system.config);
    if (system.prewarm && system.playing && !expectedGpu) {
        // Warm one effect duration (bounded to avoid an accidental editor/runtime
        // stall). Fixed steps keep the result stable across frame rates.
        const float warmTime = std::clamp(
            system.duration > 0.0f ? system.duration : std::max(system.config.lifeMax, 1.0f),
            0.0f, 30.0f);
        system.emitter.emitting = system.config.rate > 0.0f;
        if (system.burstCount > 0) system.emitter.Burst(system.burstCount);
        float burstClock = 0.0f;
        for (float t = 0.0f; t < warmTime; ) {
            const float step = std::min(1.0f / 60.0f, warmTime - t);
            if (system.burstCount > 0 && system.burstInterval > 0.0f) {
                burstClock += step;
                while (burstClock >= system.burstInterval) {
                    system.emitter.Burst(system.burstCount);
                    burstClock -= system.burstInterval;
                }
            }
            system.emitter.Update(step);
            t += step;
        }
        system.emitter.emitting = false;
        system.initialBurstFired = system.burstCount > 0;
    }
}

bool UpdateGpuPlayback(ParticleSystemComponent& system, const glm::vec3& position,
                       const glm::vec3& delta, float frameDt, bool parentEnabled,
                       const std::vector<ParticleCollisionShape>& collisionShapes) {
    const bool requested = system.config.simulationBackend != ParticleSimulationBackend::CPU;
    const bool usable = requested && IsGpuParticleConfigurationSupported(system.config);
    if (!usable) {
        if (system.gpuBackendActive) RestoreCpuFallback(system, position);
        system.gpuBackendActive = false;
        return false;
    }
    if (!system.gpuEmitter) system.gpuEmitter = std::make_shared<GpuParticleEmitter>();
    if (!system.gpuEmitter->Prepare(system.config)) {
        if (system.gpuBackendActive) RestoreCpuFallback(system, position);
        system.gpuBackendActive = false;
        return false;
    }
    if (!system.gpuBackendActive || system.gpuEmitter->Capacity() != system.config.maxParticles) {
        system.gpuEmitter->Reset(system.config, position);
        system.gpuSpawnAccumulator = 0.0f;
        system.gpuBackendActive = true;
        system.emitter.Clear();
        if (system.prewarm && system.playing) {
            const float warmTime = std::clamp(
                system.duration > 0.0f ? system.duration : std::max(system.config.lifeMax, 1.0f),
                0.0f, 30.0f);
            system.gpuEmitter->Prewarm(system.config, position, warmTime,
                                       system.burstCount, system.burstInterval,
                                       collisionShapes);
            system.initialBurstFired = system.burstCount > 0;
        }
    }

    const float dt = frameDt * std::max(system.simulationSpeed, 0.0f);
    int spawnCount = std::max(system.gpuPendingBurst, 0);
    system.gpuPendingBurst = 0;
    const bool active = parentEnabled && system.enabled && system.playing;
    const float activeDt = active ? ConsumeStartDelay(system, dt) : 0.0f;
    if (active && activeDt > 0.0f) {
        if (!system.initialBurstFired && system.burstCount > 0) {
            spawnCount += system.burstCount;
            system.initialBurstFired = true;
        }
        spawnCount += ConsumeRepeatingBursts(system, activeDt);
        system.gpuSpawnAccumulator += std::max(system.config.rate, 0.0f) * activeDt;
        const int continuous = static_cast<int>(system.gpuSpawnAccumulator);
        spawnCount += continuous;
        system.gpuSpawnAccumulator -= static_cast<float>(continuous);
        AdvanceDuration(system, activeDt);
    }
    const float updateDt = active ? activeDt : dt;
    system.gpuEmitter->Update(system.config, position, delta, system.localSpace, updateDt, spawnCount,
                              collisionShapes);
    return true;
}

} // namespace

void UpdateParticleSystems(ecs::Registry& registry, float dt) {
    const float frameDt = std::max(dt, 0.0f);
    std::vector<ParticleCollisionShape> collisionShapes;
    std::vector<ecs::Entity> collisionOwners;
    bool needsCollisionShapes = false;
    registry.view<ParticleSystemComponent>().each(
        [&needsCollisionShapes](ecs::Entity, ParticleSystemComponent& system) {
            needsCollisionShapes |= system.config.collisionEnabled;
        });
    registry.view<ParticleEffectComponent>().each(
        [&needsCollisionShapes](ecs::Entity, ParticleEffectComponent& effect) {
            for (const ParticleEffectLayer& layer : effect.layers)
                needsCollisionShapes |= layer.system.config.collisionEnabled;
        });
    if (needsCollisionShapes) registry.view<ecs::Collider, ecs::Transform>().each(
        [&collisionShapes, &collisionOwners](ecs::Entity owner, ecs::Collider& collider,
                                             ecs::Transform& transform) {
            if (collider.isTrigger) return;
            ParticleCollisionShape shape;
            const glm::vec3 scale = glm::abs(transform.scale);
            shape.center = transform.position;
            shape.rotation = transform.rotation;
            if (collider.shape == ecs::ColliderShape::Plane) {
                shape.type = ParticleCollisionShape::Type::Plane;
                shape.normal = collider.planeNormal;
                shape.offset = collider.planeOffset;
            } else if (collider.shape == ecs::ColliderShape::Sphere) {
                shape.type = ParticleCollisionShape::Type::Sphere;
                shape.radius = collider.radius * std::max({scale.x, scale.y, scale.z});
            } else {
                shape.type = ParticleCollisionShape::Type::Box;
                if (collider.shape == ecs::ColliderShape::Capsule) {
                    shape.halfExtents = glm::vec3(collider.radius,
                        collider.halfHeight + collider.radius, collider.radius) * scale;
                } else if (collider.shape == ecs::ColliderShape::Cylinder
                           || collider.shape == ecs::ColliderShape::Cone) {
                    shape.halfExtents = glm::vec3(collider.radius, collider.halfHeight,
                                                  collider.radius) * scale;
                } else if (collider.shape == ecs::ColliderShape::Torus) {
                    const float outer = collider.majorRadius + collider.minorRadius;
                    shape.halfExtents = glm::vec3(outer, collider.minorRadius, outer) * scale;
                } else {
                    shape.halfExtents = collider.halfExtents * scale;
                }
            }
            collisionShapes.push_back(shape);
            collisionOwners.push_back(owner);
        });
    registry.view<ParticleSystemComponent, ecs::Transform>().each(
        [frameDt, &collisionShapes, &collisionOwners](ecs::Entity entity,
            ParticleSystemComponent& system, ecs::Transform& transform) {
            if (!system.initialized) ResetPlayback(system, transform.position);
            SanitizeParticleConfig(system.config);

            const glm::vec3 delta = transform.position - system.lastPosition;
            if (system.localSpace && glm::dot(delta, delta) > 0.0f)
                system.emitter.Translate(delta);
            system.lastPosition = transform.position;
            system.emitter.position = transform.position;
            system.emitter.cfg = system.config;

            std::vector<ParticleCollisionShape> filtered;
            if (system.config.collisionEnabled) {
                filtered.reserve(collisionShapes.size());
                for (std::size_t i = 0; i < collisionShapes.size(); ++i)
                    if (collisionOwners[i] != entity) filtered.push_back(collisionShapes[i]);
            }

            if (UpdateGpuPlayback(system, transform.position, delta, frameDt, true, filtered)) return;

            const float scaledDt = frameDt * std::max(system.simulationSpeed, 0.0f);
            auto updateEmitter = [&](float step) {
                if (!system.config.collisionEnabled) {
                    system.emitter.Update(step);
                    return;
                }
                system.emitter.Update(step, filtered);
            };
            if (!system.enabled) {
                system.emitter.emitting = false;
                updateEmitter(scaledDt);
                return;
            }
            if (!system.playing) {
                system.emitter.emitting = false;
                updateEmitter(scaledDt);
                return;
            }

            const float activeDt = ConsumeStartDelay(system, scaledDt);
            if (activeDt <= 0.0f) {
                system.emitter.emitting = false;
                return;
            }

            if (!system.initialBurstFired && system.burstCount > 0) {
                system.emitter.Burst(system.burstCount);
                system.initialBurstFired = true;
            }
            const int repeatedBurst = ConsumeRepeatingBursts(system, activeDt);
            if (repeatedBurst > 0) system.emitter.Burst(repeatedBurst);

            system.emitter.emitting = system.config.rate > 0.0f;
            updateEmitter(activeDt);
            AdvanceDuration(system, activeDt);
        });
    registry.view<ParticleEffectComponent, ecs::Transform>().each(
        [frameDt, &collisionShapes, &collisionOwners](ecs::Entity entity,
            ParticleEffectComponent& effect, ecs::Transform& transform) {
            for (ParticleEffectLayer& layer : effect.layers) {
                ParticleSystemComponent& system = layer.system;
                const glm::vec3 position = transform.position + transform.rotation * layer.offset;
                if (!system.initialized) ResetPlayback(system, position);
                SanitizeParticleConfig(system.config);
                const glm::vec3 delta = position - system.lastPosition;
                if (system.localSpace && glm::dot(delta, delta) > 0.0f) system.emitter.Translate(delta);
                system.lastPosition = position;
                system.emitter.position = position;
                system.emitter.cfg = system.config;
                std::vector<ParticleCollisionShape> filtered;
                if (system.config.collisionEnabled) {
                    filtered.reserve(collisionShapes.size());
                    for (std::size_t i = 0; i < collisionShapes.size(); ++i)
                        if (collisionOwners[i] != entity) filtered.push_back(collisionShapes[i]);
                }
                if (UpdateGpuPlayback(system, position, delta, frameDt,
                                      effect.enabled && layer.enabled, filtered)) continue;
                const float scaledDt = frameDt * std::max(system.simulationSpeed, 0.0f);
                auto update = [&](float step) {
                    if (system.config.collisionEnabled) system.emitter.Update(step, filtered);
                    else system.emitter.Update(step);
                };
                if (!effect.enabled || !layer.enabled || !system.enabled || !system.playing) {
                    system.emitter.emitting = false;
                    update(scaledDt);
                    continue;
                }
                const float activeDt = ConsumeStartDelay(system, scaledDt);
                if (activeDt <= 0.0f) {
                    system.emitter.emitting = false;
                    continue;
                }
                if (!system.initialBurstFired && system.burstCount > 0) {
                    system.emitter.Burst(system.burstCount);
                    system.initialBurstFired = true;
                }
                const int repeatedBurst = ConsumeRepeatingBursts(system, activeDt);
                if (repeatedBurst > 0) system.emitter.Burst(repeatedBurst);
                system.emitter.emitting = system.config.rate > 0.0f;
                update(activeDt);
                AdvanceDuration(system, activeDt);
            }
        });
}

void RestartParticleSystem(ecs::Registry& registry, ecs::Entity entity) {
    ParticleSystemComponent* system = registry.TryGet<ParticleSystemComponent>(entity);
    const ecs::Transform* transform = registry.TryGet<ecs::Transform>(entity);
    if (system && transform) {
        ResetPlayback(*system, transform->position);
        system->playing = system->enabled;
    }
    ParticleEffectComponent* effect = registry.TryGet<ParticleEffectComponent>(entity);
    if (effect && transform) {
        for (ParticleEffectLayer& layer : effect->layers) {
            ResetPlayback(layer.system,
                transform->position + transform->rotation * layer.offset);
            layer.system.playing = effect->enabled && layer.enabled && layer.system.enabled;
        }
    }
}

bool PlayParticleSystem(ecs::Registry& registry, ecs::Entity entity, bool restart) {
    ParticleSystemComponent* system = registry.TryGet<ParticleSystemComponent>(entity);
    const ecs::Transform* transform = registry.TryGet<ecs::Transform>(entity);
    ParticleEffectComponent* effect = registry.TryGet<ParticleEffectComponent>(entity);
    bool effectHandled = false;
    if (effect && transform) {
        effect->enabled = true;
        for (ParticleEffectLayer& layer : effect->layers) {
            if (restart || !layer.system.initialized)
                ResetPlayback(layer.system, transform->position + transform->rotation * layer.offset);
            layer.system.enabled = true;
            layer.system.playing = layer.enabled;
        }
        effectHandled = true;
    }
    if (!system || !transform) return effectHandled;
    if (restart || !system->initialized) ResetPlayback(*system, transform->position);
    system->enabled = true;
    system->playing = true;
    return true;
}

bool StopParticleSystem(ecs::Registry& registry, ecs::Entity entity, bool clear) {
    ParticleSystemComponent* system = registry.TryGet<ParticleSystemComponent>(entity);
    ParticleEffectComponent* effect = registry.TryGet<ParticleEffectComponent>(entity);
    bool effectHandled = false;
    if (effect) {
        for (ParticleEffectLayer& layer : effect->layers) {
            layer.system.playing = false;
            layer.system.emitter.emitting = false;
            if (clear) {
                layer.system.emitter.Clear();
                if (layer.system.gpuEmitter) layer.system.gpuEmitter->Clear();
                layer.system.gpuPendingBurst = 0;
            }
        }
        effectHandled = true;
    }
    if (!system) return effectHandled;
    system->playing = false;
    system->emitter.emitting = false;
    if (clear) {
        system->emitter.Clear();
        if (system->gpuEmitter) system->gpuEmitter->Clear();
        system->gpuPendingBurst = 0;
    }
    return true;
}

bool BurstParticleSystem(ecs::Registry& registry, ecs::Entity entity, int count) {
    ParticleSystemComponent* system = registry.TryGet<ParticleSystemComponent>(entity);
    const ecs::Transform* transform = registry.TryGet<ecs::Transform>(entity);
    ParticleEffectComponent* effect = registry.TryGet<ParticleEffectComponent>(entity);
    bool effectBurst = false;
    if (effect && transform) {
        for (ParticleEffectLayer& layer : effect->layers) {
            if (!layer.enabled) continue;
            ParticleSystemComponent& child = layer.system;
            if (!child.initialized)
                ResetPlayback(child, transform->position + transform->rotation * layer.offset);
            const int resolved = count > 0 ? count : child.burstCount;
            if (resolved > 0) {
                if (child.gpuBackendActive) child.gpuPendingBurst += resolved;
                else child.emitter.Burst(resolved);
                child.initialBurstFired = true; effectBurst = true;
            }
        }
    }
    if (!system || !transform) return effectBurst;
    if (!system->initialized) {
        const bool shouldPlay = system->enabled && system->autoplay;
        ResetPlayback(*system, transform->position);
        system->playing = shouldPlay;
    }
    system->emitter.cfg = system->config;
    system->emitter.position = transform->position;
    const int resolvedCount = count > 0 ? count : system->burstCount;
    if (resolvedCount <= 0) return false;
    if (system->gpuBackendActive) system->gpuPendingBurst += resolvedCount;
    else system->emitter.Burst(resolvedCount);
    system->initialBurstFired = true;
    return true;
}

bool ClearParticleSystem(ecs::Registry& registry, ecs::Entity entity) {
    ParticleSystemComponent* system = registry.TryGet<ParticleSystemComponent>(entity);
    ParticleEffectComponent* effect = registry.TryGet<ParticleEffectComponent>(entity);
    bool effectHandled = false;
    if (effect) {
        for (ParticleEffectLayer& layer : effect->layers) {
            layer.system.emitter.Clear();
            if (layer.system.gpuEmitter) layer.system.gpuEmitter->Clear();
            layer.system.gpuPendingBurst = 0;
        }
        effectHandled = true;
    }
    if (!system) return effectHandled;
    system->emitter.Clear();
    if (system->gpuEmitter) system->gpuEmitter->Clear();
    system->gpuPendingBurst = 0;
    return true;
}

bool SetParticleSystemEnabled(ecs::Registry& registry, ecs::Entity entity, bool enabled) {
    ParticleSystemComponent* system = registry.TryGet<ParticleSystemComponent>(entity);
    ParticleEffectComponent* effect = registry.TryGet<ParticleEffectComponent>(entity);
    bool effectHandled = false;
    if (effect) {
        effect->enabled = enabled;
        effectHandled = true;
    }
    if (!system) return effectHandled;
    system->enabled = enabled;
    if (!enabled) {
        system->playing = false;
        system->emitter.emitting = false;
    }
    return true;
}

bool SetParticleEmissionRate(ecs::Registry& registry, ecs::Entity entity, float particlesPerSecond) {
    ParticleSystemComponent* system = registry.TryGet<ParticleSystemComponent>(entity);
    ParticleEffectComponent* effect = registry.TryGet<ParticleEffectComponent>(entity);
    const float rate = std::clamp(FiniteParticleValue(particlesPerSecond, 0.0f),
                                  0.0f, 1000000.0f);
    bool handled = false;
    if (effect) {
        for (ParticleEffectLayer& layer : effect->layers) {
            layer.system.config.rate = rate;
            layer.system.emitter.cfg.rate = rate;
        }
        handled = true;
    }
    if (system) {
        system->config.rate = rate;
        system->emitter.cfg.rate = rate;
        handled = true;
    }
    return handled;
}

bool SetParticleSimulationSpeed(ecs::Registry& registry, ecs::Entity entity, float speed) {
    ParticleSystemComponent* system = registry.TryGet<ParticleSystemComponent>(entity);
    ParticleEffectComponent* effect = registry.TryGet<ParticleEffectComponent>(entity);
    const float safe = std::clamp(FiniteParticleValue(speed, 0.0f), 0.0f, 1000.0f);
    bool handled = false;
    if (effect) {
        for (ParticleEffectLayer& layer : effect->layers) layer.system.simulationSpeed = safe;
        handled = true;
    }
    if (system) {
        system->simulationSpeed = safe;
        handled = true;
    }
    return handled;
}

bool IsParticleSystemPlaying(const ecs::Registry& registry, ecs::Entity entity) {
    if (const ParticleSystemComponent* system =
            registry.TryGet<ParticleSystemComponent>(entity)) {
        if (system->enabled && system->playing) return true;
    }
    if (const ParticleEffectComponent* effect =
            registry.TryGet<ParticleEffectComponent>(entity)) {
        if (!effect->enabled) return false;
        for (const ParticleEffectLayer& layer : effect->layers)
            if (layer.enabled && layer.system.enabled && layer.system.playing) return true;
    }
    return false;
}

std::size_t ParticleSystemAliveCount(const ecs::Registry& registry, ecs::Entity entity) {
    auto alive = [](const ParticleSystemComponent& system) {
        return system.gpuBackendActive && system.gpuEmitter
            ? system.gpuEmitter->Alive() : system.emitter.Alive();
    };
    std::size_t count = 0;
    if (const ParticleSystemComponent* system =
            registry.TryGet<ParticleSystemComponent>(entity))
        count += alive(*system);
    if (const ParticleEffectComponent* effect =
            registry.TryGet<ParticleEffectComponent>(entity))
        for (const ParticleEffectLayer& layer : effect->layers)
            if (layer.enabled) count += alive(layer.system);
    return count;
}

bool ApplyParticleAction(ecs::Registry& registry, ecs::Entity entity, ParticleAction action) {
    switch (action) {
    case ParticleAction::Play: return PlayParticleSystem(registry, entity, false);
    case ParticleAction::Restart: return PlayParticleSystem(registry, entity, true);
    case ParticleAction::Stop: return StopParticleSystem(registry, entity, false);
    case ParticleAction::Burst: return BurstParticleSystem(registry, entity, 0);
    case ParticleAction::Clear: return ClearParticleSystem(registry, entity);
    case ParticleAction::None: break;
    }
    return false;
}

bool ProcessParticleAnimationEvent(ecs::Registry& registry, ecs::Entity emitter,
                                   const std::string& eventName) {
    // Particle.Play, Particle.Restart:ExplosionFX, Particle.Stop, etc.
    constexpr const char* prefix = "Particle.";
    if (eventName.rfind(prefix, 0) != 0) return false;
    const std::size_t separator = eventName.find(':');
    const std::string command = eventName.substr(9, separator == std::string::npos
        ? std::string::npos : separator - 9);
    ecs::Entity target = emitter;
    if (separator != std::string::npos && separator + 1 < eventName.size()) {
        const std::string targetName = eventName.substr(separator + 1);
        target = ecs::kNull;
        registry.view<ecs::RuntimeName>().each(
            [&](ecs::Entity entity, ecs::RuntimeName& name) {
                if (target == ecs::kNull && name.value == targetName) target = entity;
            });
    }
    ParticleAction action = ParticleAction::None;
    if (command == "Play") action = ParticleAction::Play;
    else if (command == "Restart") action = ParticleAction::Restart;
    else if (command == "Stop") action = ParticleAction::Stop;
    else if (command == "Burst") action = ParticleAction::Burst;
    else if (command == "Clear") action = ParticleAction::Clear;
    return ApplyParticleAction(registry, target, action);
}

void ProcessParticleCollisionEvents(ecs::Registry& registry,
                                    const std::vector<CollisionEvent>& events) {
    for (const CollisionEvent& event : events) {
        if (!event.trigger || event.phase == CollisionEvent::Phase::Stay) continue;
        const ecs::Entity candidates[] = {event.a, event.b};
        for (const ecs::Entity trigger : candidates) {
            const TriggerParticleAction* binding = registry.TryGet<TriggerParticleAction>(trigger);
            if (!binding || binding->targetName.empty()) continue;
            ecs::Entity target = ecs::kNull;
            registry.view<ecs::RuntimeName>().each([&](ecs::Entity entity, ecs::RuntimeName& name) {
                if (target == ecs::kNull && name.value == binding->targetName) target = entity;
            });
            const ParticleAction action = event.phase == CollisionEvent::Phase::Exit
                ? binding->onExit : binding->onEnter;
            ApplyParticleAction(registry, target, action);
        }
    }
}

} // namespace engine
