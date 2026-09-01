#pragma once

// ECS Pass 5 (follow-up) -- concrete ValidateECS bridge hooks for the physics subsystem. These wire
// the generic EcsValidator hook mechanism to the real PhysicsWorld so ValidateECS can detect stale
// physics bridges: a joint that names a dead entity, or a static-collider proxy left in the cache
// after its owning entity was destroyed. Read-only over both the Registry and the PhysicsWorld; no
// pointers are exposed. GL-free, header-only.

#include "engine/ecs/EcsValidator.h"
#include "engine/physics/PhysicsWorld.h"

#include <string>
#include <unordered_set>

namespace engine {
namespace physics {

// A joint's body handles (a and b; b == kNull means "attached to the world") must reference live
// entities. A dangling joint after an entity is destroyed is a real crash risk in the solver.
inline ecs::ValidationHook MakeJointEntityHook(const PhysicsWorld& world) {
    return [&world](const ecs::Registry& reg, ecs::ValidationReport& report) {
        std::size_t index = 0;
        for (const auto& joint : world.Joints()) {
            const auto check = [&](ecs::Entity e, const char* which) {
                if (e != ecs::kNull && !reg.Valid(e))
                    report.add("invalid-joint-entity",
                               "joint " + std::to_string(index) + " body " + which
                               + " references dead entity index " + std::to_string(ecs::EntityIndex(e)));
            };
            check(joint.a, "A");
            check(joint.b, "B");
            ++index;
        }
    };
}

// Every entity that owns a static-collider proxy in the physics cache must still be a live entity.
// The cache prunes unseen entries on the next Step, so a stale entry is transient -- but surfacing it
// catches a destroy path that never re-stepped physics (e.g. an edit-time teardown).
inline ecs::ValidationHook MakeStaticProxyHook(const PhysicsWorld& world) {
    return [&world](const ecs::Registry& reg, ecs::ValidationReport& report) {
        world.ForEachStaticProxyEntity([&](ecs::Entity e) {
            if (!reg.Valid(e))
                report.add("stale-physics-proxy",
                           "static-collider proxy for dead entity index " + std::to_string(ecs::EntityIndex(e)));
        });
    };
}

// Convenience: register both physics hooks on a validator in one call.
inline void RegisterPhysicsValidationHooks(ecs::EcsValidator& validator, const PhysicsWorld& world) {
    validator.AddHook("physics-joints", MakeJointEntityHook(world));
    validator.AddHook("physics-static-proxies", MakeStaticProxyHook(world));
}

} // namespace physics
} // namespace engine
