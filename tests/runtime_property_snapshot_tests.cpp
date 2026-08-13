#include "RuntimePropertySnapshot.h"

#include <cassert>
#include <iostream>

int main() {
    engine::ecs::Registry registry;
    const auto entity = registry.Create();
    registry.Add<engine::ecs::RuntimeName>(entity, {"Wizard"});
    registry.Add<engine::ecs::Transform>(entity, {{1,2,3},{1,1,1},{1,0,0,0}});
    registry.Add<engine::Health>(entity, {80,100,true,false});
    registry.Add<engine::AbilityResource>(entity, {50,100,25,100});

    const RuntimeEntitySnapshot start = RuntimeEntitySnapshot::Capture(registry, entity);
    assert(!start.Changed(registry, entity));

    registry.Get<engine::ecs::Transform>(entity).position.x = 9.0f;
    registry.Get<engine::Health>(entity).hp = 12.0f;
    assert(start.Changed(registry, entity));
    assert(start.Changed(registry, entity, RuntimeEntitySnapshot::Component::Transform));
    assert(start.Changed(registry, entity, RuntimeEntitySnapshot::Component::Health));

    start.Restore(registry, entity, RuntimeEntitySnapshot::Component::Transform);
    assert(registry.Get<engine::ecs::Transform>(entity).position.x == 1.0f);
    assert(start.Changed(registry, entity));
    start.Restore(registry, entity, RuntimeEntitySnapshot::Component::Health);
    assert(!start.Changed(registry, entity));

    registry.Add<engine::ecs::RigidBody>(entity, engine::ecs::RigidBody::Dynamic(5.0f));
    assert(start.Changed(registry, entity, RuntimeEntitySnapshot::Component::RigidBody));
    start.Restore(registry, entity, RuntimeEntitySnapshot::Component::RigidBody);
    assert(!registry.Has<engine::ecs::RigidBody>(entity));

    registry.Get<engine::AbilityResource>(entity).mana = 1.0f;
    start.Restore(registry, entity);
    assert(registry.Get<engine::AbilityResource>(entity).mana == 50.0f);
    assert(!start.Changed(registry, entity));

    std::cout << "runtime property snapshot tests passed\n";
    return 0;
}
