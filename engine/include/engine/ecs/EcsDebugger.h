#pragma once

// ECS Pass 5 -- ECS debugger. A read-only snapshot of one entity for an inspector overlay: its
// handle/index/generation, alive & pending-destroy state, the components it owns with their
// per-component revisions, and (via opt-in bridge hooks) a STRING summary of any runtime bridge --
// physics proxy, render proxy, script instance, animation instance. Bridges return descriptive
// strings/ids only; per the pass rule, no runtime system exposes raw pointers here. GL-free.

#include "engine/ecs/Registry.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace engine {
namespace ecs {

struct ComponentSnapshot {
    std::string   typeName;
    std::uint64_t revision = 0;
};

struct EntitySnapshot {
    Entity        entity = kNull;
    std::uint32_t index = 0;
    std::uint32_t generation = 0;
    bool          alive = false;
    bool          pendingDestroy = false;
    std::vector<ComponentSnapshot> components;
    // Bridge summaries (filled by registered hooks; empty when the entity has no such bridge).
    std::vector<std::pair<std::string, std::string>> bridges;   // (bridge name, summary)
};

class EcsDebugger {
public:
    // A bridge hook reports a subsystem's view of an entity as a string (or leaves it empty). It is
    // handed the Registry + entity + the snapshot to append to. Read-only.
    using BridgeHook = std::function<void(const Registry&, Entity, EntitySnapshot&)>;
    void AddBridge(std::string name, BridgeHook hook) { m_bridges.push_back({std::move(name), std::move(hook)}); }
    void ClearBridges() { m_bridges.clear(); }

    EntitySnapshot Inspect(const Registry& reg, Entity e) const {
        EntitySnapshot s;
        s.entity = e;
        s.index = EntityIndex(e);
        s.generation = EntityGeneration(e);
        s.alive = reg.Valid(e);
        s.pendingDestroy = reg.IsPendingDestroy(e);

        // Components + per-component revisions (type-erased over the pools). Only meaningful when the
        // handle is live; a stale handle reports alive=false and no components.
        if (s.alive) {
            reg.ForEachPool([&](const IPool& pool) {
                if (pool.Has(e))
                    s.components.push_back({pool.TypeName(), pool.EntityRevision(e)});
            });
        }

        for (const auto& b : m_bridges) b.second(reg, e, s);
        return s;
    }

private:
    std::vector<std::pair<std::string, BridgeHook>> m_bridges;
};

} // namespace ecs
} // namespace engine
