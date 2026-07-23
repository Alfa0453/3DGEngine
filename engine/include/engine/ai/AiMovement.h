#pragma once

#include <glm/glm.hpp>

#include "engine/ecs/Entity.h"

namespace engine {
class PhysicsWorld;
struct AnimatedModel;
namespace ecs { class Registry; }

namespace ai {

enum class AiMovementMode { Grounded = 0, Falling = 1, Flying = 2 };

// Runtime movement state and authored ground-movement settings for an AI agent.
// Navigation supplies horizontal intent; this component owns gravity, floor
// probing, slopes, small steps, and the grounded/falling/flying state.
struct AiMovementComponent {
    AiMovementMode mode = AiMovementMode::Grounded;
    float gravity = -9.81f;
    float maxFallSpeed = 35.0f;
    float groundProbeDistance = 0.25f;
    float stepHeight = 0.35f;
    float maxSlopeDegrees = 50.0f;
    float verticalVelocity = 0.0f;
    glm::vec3 groundNormal{0.0f, 1.0f, 0.0f};

    bool IsGrounded() const { return mode == AiMovementMode::Grounded; }
    bool IsFalling() const { return mode == AiMovementMode::Falling; }
    bool IsFlying() const { return mode == AiMovementMode::Flying; }
};

glm::vec3 MoveAiAgent(PhysicsWorld& physics,
                      ecs::Registry& registry,
                      ecs::Entity agent,
                      const glm::vec3& start,
                      const glm::vec3& navigationDesired,
                      float dt,
                      AiMovementComponent& movement,
                      bool hasTerrainGround = false,
                      float terrainGroundY = 0.0f);

// Publishes engine-owned locomotion inputs to the animation state graph.
void UpdateAiAnimationParameters(AnimatedModel& animated,
                                 const AiMovementComponent& movement,
                                 const glm::vec3& worldVelocity);

} // namespace ai
} // namespace engine
