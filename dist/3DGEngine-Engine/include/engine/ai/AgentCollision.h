#pragma once

#include <glm/glm.hpp>

#include "engine/ecs/Entity.h"

namespace engine {
class PhysicsWorld;
namespace ecs { class Registry; }

namespace ai {

float AgentCollisionRadius(const ecs::Registry& registry, ecs::Entity agent);

// Moves a navigation agent through the collision world using a swept horizontal
// volume. Solid walls block the move, glancing movement slides along them, and
// trigger/ignored collision-channel responses remain non-blocking.
glm::vec3 MoveAgentWithCollision(PhysicsWorld& physics,
                                 ecs::Registry& registry,
                                 ecs::Entity agent,
                                 const glm::vec3& start,
                                 const glm::vec3& desired,
                                 int slideIterations = 3);

} // namespace ai
} // namespace engine
