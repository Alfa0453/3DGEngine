#include "RuntimePropertySnapshot.h"

#include <glm/gtx/quaternion.hpp>
#include <cmath>

namespace {
constexpr float kEpsilon = 0.0001f;
bool Equal(float a, float b) { return std::abs(a - b) <= kEpsilon; }
bool Equal(const glm::vec3& a, const glm::vec3& b) {
    return glm::all(glm::lessThanEqual(glm::abs(a - b), glm::vec3(kEpsilon)));
}
bool Equal(const glm::quat& a, const glm::quat& b) {
    return std::abs(glm::dot(glm::normalize(a), glm::normalize(b))) >= 1.0f - kEpsilon;
}

template<class T> void Put(engine::ecs::Registry& r, engine::ecs::Entity e,
                           const std::optional<T>& value) {
    if (value) r.Add<T>(e, *value); else r.Remove<T>(e);
}
template<class T> bool PresenceChanged(const engine::ecs::Registry& r,
                                       engine::ecs::Entity e,
                                       const std::optional<T>& value) {
    return r.Has<T>(e) != value.has_value();
}
}

RuntimeEntitySnapshot RuntimeEntitySnapshot::Capture(const engine::ecs::Registry& r,
                                                     engine::ecs::Entity e) {
    RuntimeEntitySnapshot s;
#define CAPTURE(T, field) if (const auto* v = r.TryGet<T>(e)) s.field = *v
    CAPTURE(engine::ecs::RuntimeName, name);
    CAPTURE(engine::ecs::Transform, transform);
    CAPTURE(engine::ecs::LinearVelocity, linearVelocity);
    CAPTURE(engine::ecs::AngularVelocity, angularVelocity);
    CAPTURE(engine::ecs::RigidBody, rigidBody);
    CAPTURE(engine::ecs::Collider, collider);
    CAPTURE(engine::Health, health);
    CAPTURE(engine::ecs::Rotator, rotator);
    CAPTURE(engine::ecs::Mover, mover);
    CAPTURE(engine::AbilityResource, abilityResource);
    CAPTURE(engine::Projectile, projectile);
#undef CAPTURE
    return s;
}

void RuntimeEntitySnapshot::Restore(engine::ecs::Registry& r, engine::ecs::Entity e,
                                    Component c) const {
    if (!r.Valid(e)) return;
#define RESTORE(kind, T, field) if (c == Component::All || c == Component::kind) Put<T>(r, e, field)
    RESTORE(Name, engine::ecs::RuntimeName, name);
    RESTORE(Transform, engine::ecs::Transform, transform);
    RESTORE(LinearVelocity, engine::ecs::LinearVelocity, linearVelocity);
    RESTORE(AngularVelocity, engine::ecs::AngularVelocity, angularVelocity);
    RESTORE(RigidBody, engine::ecs::RigidBody, rigidBody);
    RESTORE(Collider, engine::ecs::Collider, collider);
    RESTORE(Health, engine::Health, health);
    RESTORE(Rotator, engine::ecs::Rotator, rotator);
    RESTORE(Mover, engine::ecs::Mover, mover);
    RESTORE(AbilityResource, engine::AbilityResource, abilityResource);
    RESTORE(Projectile, engine::Projectile, projectile);
#undef RESTORE
}

bool RuntimeEntitySnapshot::Changed(const engine::ecs::Registry& r, engine::ecs::Entity e,
                                    Component c) const {
    if (!r.Valid(e)) return true;
    auto check = [c](Component wanted) { return c == Component::All || c == wanted; };
    if (check(Component::Name)) {
        if (PresenceChanged(r,e,name)) return true;
        if (name && r.TryGet<engine::ecs::RuntimeName>(e)->value != name->value) return true;
    }
    if (check(Component::Transform)) {
        if (PresenceChanged(r,e,transform)) return true;
        if (transform) { const auto& v=*r.TryGet<engine::ecs::Transform>(e);
            if (!Equal(v.position,transform->position)||!Equal(v.scale,transform->scale)||!Equal(v.rotation,transform->rotation)) return true; }
    }
    if (check(Component::LinearVelocity)) {
        if (PresenceChanged(r,e,linearVelocity)) return true;
        if (linearVelocity && !Equal(r.TryGet<engine::ecs::LinearVelocity>(e)->velocity,linearVelocity->velocity)) return true;
    }
    if (check(Component::AngularVelocity)) {
        if (PresenceChanged(r,e,angularVelocity)) return true;
        if (angularVelocity) { const auto& v=*r.TryGet<engine::ecs::AngularVelocity>(e);
            if(!Equal(v.axis,angularVelocity->axis)||!Equal(v.radiansPerSecond,angularVelocity->radiansPerSecond)) return true; }
    }
    if (check(Component::RigidBody)) {
        if (PresenceChanged(r,e,rigidBody)) return true;
        if (rigidBody) { const auto& v=*r.TryGet<engine::ecs::RigidBody>(e); const auto& b=*rigidBody;
            if(!Equal(v.velocity,b.velocity)||!Equal(v.invMass,b.invMass)||v.useGravity!=b.useGravity||v.kinematic!=b.kinematic||v.sleeping!=b.sleeping||!Equal(v.linearDamping,b.linearDamping)||!Equal(v.angularDamping,b.angularDamping)) return true; }
    }
    if (check(Component::Collider)) {
        if (PresenceChanged(r,e,collider)) return true;
        if (collider) { const auto& v=*r.TryGet<engine::ecs::Collider>(e); const auto& b=*collider;
            if(v.shape!=b.shape||!Equal(v.radius,b.radius)||!Equal(v.halfExtents,b.halfExtents)||!Equal(v.halfHeight,b.halfHeight)||!Equal(v.restitution,b.restitution)||!Equal(v.friction,b.friction)||v.isTrigger!=b.isTrigger||v.layer!=b.layer||v.mask!=b.mask) return true; }
    }
    if (check(Component::Health)) {
        if (PresenceChanged(r,e,health)) return true;
        if (health) { const auto& v=*r.TryGet<engine::Health>(e); if(!Equal(v.hp,health->hp)||!Equal(v.maxHp,health->maxHp)||v.alive!=health->alive) return true; }
    }
    if (check(Component::Rotator)) {
        if (PresenceChanged(r,e,rotator)) return true;
        if (rotator) { const auto& v=*r.TryGet<engine::ecs::Rotator>(e); if(!Equal(v.axis,rotator->axis)||!Equal(v.radiansPerSecond,rotator->radiansPerSecond)) return true; }
    }
    if (check(Component::Mover)) {
        if (PresenceChanged(r,e,mover)) return true;
        if (mover) { const auto& v=*r.TryGet<engine::ecs::Mover>(e); if(!Equal(v.axis,mover->axis)||!Equal(v.distance,mover->distance)||!Equal(v.speed,mover->speed)||!Equal(v.phase,mover->phase)||!Equal(v.origin,mover->origin)) return true; }
    }
    if (check(Component::AbilityResource)) {
        if (PresenceChanged(r,e,abilityResource)) return true;
        if (abilityResource) { const auto& v=*r.TryGet<engine::AbilityResource>(e); const auto& b=*abilityResource;
            if(!Equal(v.mana,b.mana)||!Equal(v.maxMana,b.maxMana)||!Equal(v.stamina,b.stamina)||!Equal(v.maxStamina,b.maxStamina)) return true; }
    }
    if (check(Component::Projectile)) {
        if (PresenceChanged(r,e,projectile)) return true;
        if (projectile) { const auto& v=*r.TryGet<engine::Projectile>(e); const auto& b=*projectile;
            if(!Equal(v.dir,b.dir)||!Equal(v.speed,b.speed)||!Equal(v.range,b.range)||!Equal(v.traveled,b.traveled)||!Equal(v.damage,b.damage)||!Equal(v.radius,b.radius)) return true; }
    }
    return false;
}
