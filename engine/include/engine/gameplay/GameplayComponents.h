#pragma once

#include "engine/ecs/Entity.h"

#include <glm/glm.hpp>

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

// A moving projectile entity. Travels along `dir` at `speed`, expiring past `range`
// or when it strikes a Health entity within `radius` (its `owner` is ignored). The
// projectile system moves it (updating its Transform) and applies the damage.
struct Projectile {
    glm::vec3   dir{0.0f, 0.0f, 1.0f};
    float       speed    = 10.0f;
    float       range    = 12.0f;
    float       traveled = 0.0f;
    float       damage   = 25.0f;
    float       radius   = 1.0f;
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