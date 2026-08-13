#pragma once

#include <engine/ecs/Components.h>
#include <engine/ecs/Registry.h>
#include <engine/gameplay/AbilitySystem.h>
#include <engine/gameplay/GameplayComponents.h>
#include <engine/physics/PhysicsComponents.h>

#include <optional>

// Copyable, renderer-independent snapshot used by the Runtime Property Inspector.
// It deliberately covers mutable gameplay data rather than GPU/resource pointers.
struct RuntimeEntitySnapshot {
    enum class Component {
        All, Name, Transform, LinearVelocity, AngularVelocity, RigidBody,
        Collider, Health, Rotator, Mover, AbilityResource, Projectile
    };

    std::optional<engine::ecs::RuntimeName> name;
    std::optional<engine::ecs::Transform> transform;
    std::optional<engine::ecs::LinearVelocity> linearVelocity;
    std::optional<engine::ecs::AngularVelocity> angularVelocity;
    std::optional<engine::ecs::RigidBody> rigidBody;
    std::optional<engine::ecs::Collider> collider;
    std::optional<engine::Health> health;
    std::optional<engine::ecs::Rotator> rotator;
    std::optional<engine::ecs::Mover> mover;
    std::optional<engine::AbilityResource> abilityResource;
    std::optional<engine::Projectile> projectile;

    static RuntimeEntitySnapshot Capture(const engine::ecs::Registry& registry,
                                         engine::ecs::Entity entity);
    void Restore(engine::ecs::Registry& registry, engine::ecs::Entity entity,
                 Component component = Component::All) const;
    bool Changed(const engine::ecs::Registry& registry, engine::ecs::Entity entity,
                 Component component = Component::All) const;
};
