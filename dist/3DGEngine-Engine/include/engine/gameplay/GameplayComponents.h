#pragma once

#include "engine/ecs/Entity.h"

#include <glm/glm.hpp>

#include <cstdint>
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

// Procedural skeletal ragdoll configuration and runtime state. When enabled, the
// ragdoll system replaces animation with a small set of physics-driven bone
// bodies after Health reports death. The runtime vectors are built on activation
// and are intentionally not authored or serialized.
struct Ragdoll {
    bool  enabled = true;
    bool  activateOnDeath = true;
    bool  active = false;
    float totalMass = 65.0f;
    float bodyRadiusScale = 0.18f;
    float linearDamping = 0.25f;
    float angularDamping = 1.4f;
    float deathImpulse = 1.5f;
    int   maxBodies = 16;

    struct Part {
        ecs::Entity entity = ecs::kNull;
        int bone = -1;
        int parentPart = -1;
    };
    std::vector<Part> parts;
    std::vector<int> boneDrivers;
    std::vector<glm::mat4> boneFromBody;
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
