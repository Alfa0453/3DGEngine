#pragma once

namespace engine {

class PhysicsWorld;
namespace ecs { class Registry; }

// Creates physics bodies when an enabled ragdoll's Health dies. Call after
// UpdateHealth and before PhysicsWorld::Step.
void UpdateRagdollsBeforePhysics(ecs::Registry& registry, PhysicsWorld& physics);

// Converts the simulated body transforms back into skinning matrices. Call after
// PhysicsWorld::Step and before rendering.
void UpdateRagdollsAfterPhysics(ecs::Registry& registry);

} // namespace engine
