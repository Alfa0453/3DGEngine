#pragma once

#include "engine/ecs/Entity.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace engine {

// --- Reusable gameplay components (data only; behaviour lives in systems) -----

// Hit points. Damage() reduces hp; the health system flips `alive`/`justDied` so a
// game can react to death exactly once (play a clip, drop loot, respawn).
struct Health {
    float hp    = 100.0f;
    float maxHp = 100.0f;
    bool  alive = true;
    bool  justDied = false;         // set for one frame when hp crosses 0
    void  Damage(float d) { if (alive) hp -= d; }
    void  Reset(float full) { hp = maxHp = full; alive = true; justDied = false; }
};

enum class RagdollBodyShape { Sphere, Box, Capsule };
enum class RagdollJointType { Ball, Hinge };

struct RagdollBodyDefinition {
    std::string boneName;
    RagdollBodyShape shape = RagdollBodyShape::Capsule;
    glm::vec3 localPosition{0.0f};
    glm::vec3 localRotationDegrees{0.0f};
    glm::vec3 halfExtents{0.12f, 0.2f, 0.12f};
    float radius = 0.1f;
    float halfHeight = 0.15f;
    float massWeight = 1.0f;
    bool enabled = true;
};

struct RagdollConstraintDefinition {
    std::string parentBoneName;
    std::string childBoneName;
    RagdollJointType type = RagdollJointType::Ball;
    glm::vec3 axis{1.0f, 0.0f, 0.0f};
    float swingLimitDegrees = 45.0f;
    float twistMinDegrees = -35.0f;
    float twistMaxDegrees = 35.0f;
    bool collideConnected = false;
};

// Skeletal ragdoll configuration and runtime state. An authored physics asset
// supplies stable per-bone bodies and joints; an empty body list deliberately
// retains the original procedural generator as a backwards-compatible fallback.
struct Ragdoll {
    bool  enabled = true;
    bool  activateOnDeath = true;
    bool  active = false;
    float totalMass = 65.0f;
    float bodyRadiusScale = 0.18f;
    float linearDamping = 0.25f;
    float angularDamping = 1.4f;
    float deathImpulse = 1.5f;
    float blendInDuration = 0.18f;
    float blendOutDuration = 0.28f;
    bool recoverWhenRevived = true;
    int   maxBodies = 16;
    std::string assetPath;
    std::string skeletonPath;
    std::vector<RagdollBodyDefinition> bodies;
    std::vector<RagdollConstraintDefinition> constraints;

    struct Part {
        ecs::Entity entity = ecs::kNull;
        int bone = -1;
        int parentPart = -1;
    };
    std::vector<Part> parts;
    std::vector<int> boneDrivers;
    std::vector<glm::mat4> boneFromBody;
    std::vector<glm::mat4> transitionPose;
    float blendAlpha = 0.0f;
    bool recovering = false;
    bool pendingCleanup = false;
    bool rootColliderWasTrigger = false;
    std::uint32_t rootColliderMask = 0;
};

// A moving projectile entity. Travels along `dir` at `speed`, expiring past `range`
// or when its swept collision sphere strikes a solid collider (its `owner` is
// ignored). Damage is applied only when that first collider has a living Health
// component; walls consume the projectile without damaging actors behind them.
struct Projectile {
    glm::vec3   dir{0.0f, 0.0f, 1.0f};
    float       speed    = 10.0f;
    float       range    = 12.0f;
    float       traveled = 0.0f;
    float       damage   = 25.0f;
    float       radius   = 0.12f;
    ecs::Entity owner    = ecs::kNull;
};

// Rigidly attach this entity to a parent: its Transform is set each frame from the
// parent's Transform (times `offset`). If `boneIndex >= 0` and the parent has an
// AnimatedModel, it follows that bone instead (a weapon in a hand).
struct Attachment {
    ecs::Entity parent = ecs::kNull;
    glm::mat4   offset{1.0f};
    int         boneIndex = -1;
};

} // namespace engine
