#pragma once

#include "engine/ecs/Entity.h"

#include <glm/glm.hpp>

#include <vector>

namespace engine {
namespace ecs { class Registry; }

// One projectile strike this frame (for VFX / audio / scoring).
struct ProjectileHit {
    ecs::Entity projectile = ecs::kNull;
    ecs::Entity target     = ecs::kNull;
    glm::vec3   point{0.0f};
    float       damage = 0.0f;
    bool        lethal = false;     // this hit dropped the target's hp to 0
};

// --- Reusable gameplay systems (operate on the Registry; hold no state) -------

// Update Health bookkeeping: set `alive` and pulse `justDied` for one frame when a
// health drops to zero. Run once per frame before/after damage is applied.
void UpdateHealth(ecs::Registry& registry);

// Move every Projectile along its Transform, damage the first Health entity it
// overlaps (skipping its owner), and destroy spent projectiles (hit or out of
// range). Returns the strikes for the caller to react to (spawn particles, etc.).
std::vector<ProjectileHit> UpdateProjectiles(ecs::Registry& registry, float dt);

// Drive every Attachment: set the child's Transform from its parent's Transform,
// or from a parent bone when boneIndex >= 0 (needs the parent's AnimatedModel).
void UpdateAttachments(ecs::Registry& registry);

} // namespace engine