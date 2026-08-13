#pragma once

#include "engine/ecs/Entity.h"

namespace engine {

class PhysicsWorld;
namespace ecs { class Registry; }

bool ActivateRagdoll(ecs::Registry& registry, PhysicsWorld& physics,
                     ecs::Entity entity);
bool RequestRagdollRecovery(ecs::Registry& registry, ecs::Entity entity);

// Creates physics bodies when an enabled ragdoll's Health dies. Call after
// UpdateHealth and before PhysicsWorld::Step.
void UpdateRagdollsBeforePhysics(ecs::Registry& registry, PhysicsWorld& physics);

// Converts the simulated body transforms back into skinning matrices. Call after
// PhysicsWorld::Step and before rendering.
void UpdateRagdollsAfterPhysics(ecs::Registry& registry, PhysicsWorld& physics,
                                float deltaTime = 1.0f / 60.0f);

} // namespace engine
