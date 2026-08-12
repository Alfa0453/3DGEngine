#pragma once

#include "engine/ecs/Entity.h"
#include "engine/graphics/ParticleSystem.h"

#include <string>
#include <vector>

namespace engine {
namespace ecs {
class Registry;
}
struct CollisionEvent;

void UpdateParticleSystems(ecs::Registry& registry, float dt);
void RestartParticleSystem(ecs::Registry& registry, ecs::Entity entity);
bool PlayParticleSystem(ecs::Registry& registry, ecs::Entity entity, bool restart = false);
bool StopParticleSystem(ecs::Registry& registry, ecs::Entity entity, bool clear = false);
bool BurstParticleSystem(ecs::Registry& registry, ecs::Entity entity, int count = 0);
bool ClearParticleSystem(ecs::Registry& registry, ecs::Entity entity);
bool SetParticleSystemEnabled(ecs::Registry& registry, ecs::Entity entity, bool enabled);
bool SetParticleEmissionRate(ecs::Registry& registry, ecs::Entity entity, float particlesPerSecond);
bool SetParticleSimulationSpeed(ecs::Registry& registry, ecs::Entity entity, float speed);
bool IsParticleSystemPlaying(const ecs::Registry& registry, ecs::Entity entity);
std::size_t ParticleSystemAliveCount(const ecs::Registry& registry, ecs::Entity entity);
bool ApplyParticleAction(ecs::Registry& registry, ecs::Entity entity, ParticleAction action);
bool ProcessParticleAnimationEvent(ecs::Registry& registry, ecs::Entity emitter,
                                   const std::string& eventName);
void ProcessParticleCollisionEvents(ecs::Registry& registry,
                                    const std::vector<CollisionEvent>& events);

} // namespace engine
