#pragma once

#include "engine/ecs/Registry.h"
#include "engine/ecs/Components.h"

#include <glm/glm.hpp>

#include <functional>
#include <string>
#include <unordered_map>

namespace engine {

// A named library of entity templates. A "prefab" is a builder that adds the
// components for one kind of thing to a fresh entity; Spawn() creates an entity and
// runs the builder, so a fully-configured instance is a single call. Define the
// template once, spawn it as many times as you like.
//
//   lib.Define("enemy", [&](Registry& r, Entity e) {
//       r.Add<Transform>(e, ...); r.Add<MeshPBR>(e, {mesh, mat});
//       r.Add<Health>(e, {60, 60}); r.Add<Enemy>(e, {});
//   });
//   lib.Spawn("enemy", reg, position);   // <- one call, fully built
//
// (For duplicating an existing entity instead of a named template, use
//  Registry::Clone.)
class PrefabLibrary {
public:
    using Builder = std::function<void(ecs::Registry&, ecs::Entity)>;

    void Define(const std::string& name, Builder builder) { m_prefabs[name] = std::move(builder); }
    bool Has(const std::string& name) const { return m_prefabs.find(name) != m_prefabs.end(); }
    std::size_t Count() const { return m_prefabs.size(); }

    // Spawn an instance; returns kNull if the prefab name is unknown.
    ecs::Entity Spawn(const std::string& name, ecs::Registry& reg) const {
        const auto it = m_prefabs.find(name);
        if (it == m_prefabs.end()) return ecs::kNull;
        const ecs::Entity e = reg.Create();
        it->second(reg, e);
        return e;
    }

    // Spawn and place: sets Transform.position (adding a Transform if the prefab
    // didn't create one).
    ecs::Entity Spawn(const std::string& name, ecs::Registry& reg, const glm::vec3& position) const {
        const ecs::Entity e = Spawn(name, reg);
        if (e != ecs::kNull) {
            if (!reg.Has<ecs::Transform>(e)) reg.Add<ecs::Transform>(e);
            reg.Get<ecs::Transform>(e).position = position;
        }
        return e;
    }

private:
    std::unordered_map<std::string, Builder> m_prefabs;
};

} // namespace engine