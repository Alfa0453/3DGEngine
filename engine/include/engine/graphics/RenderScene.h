#pragma once

// ECS Pass 2 -- RenderScene extraction foundation (Phases 9-11).
//
// A runtime cache of render proxies extracted FROM the ECS. It is deliberately a SEPARATE cache,
// not stored inside authored components: the renderer reads proxies, the ECS stays the source of
// truth. This is the FOUNDATION only -- it is populated and validated here, but the existing
// PbrRenderer Registry path is left untouched (Phase 10: "keep the current path working; validate
// parity before migrating passes"). Wiring PbrRenderer/ShadowCasters/ReflectionProbeSystem to read
// from RenderScene is the deferred renderer-migration step.
//
// Incremental: SyncFrom() uses Pass-1 component/structural revisions so a frame in which nothing
// moved does zero extraction work; a moved Transform updates just that proxy; a created/destroyed
// entity adds/removes just that proxy. A monotonic ShadowCasterRevision() bumps only when
// shadow-relevant state changes, so shadow systems can invalidate incrementally (Phase 13).
//
// Header-only, GL-free (stores a const Mesh* opaque pointer). Renderer-side data (GPU handles,
// material bindings) is layered on top by the renderer, not here.

#include "engine/ecs/Registry.h"
#include "engine/ecs/Components.h"
#include "engine/physics/PhysicsComponents.h"   // ecs::RigidBody, for mobility inference

#include <glm/glm.hpp>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace engine {

// One authoritative mobility concept shared by physics and rendering (Phase 17). Inferred from the
// RigidBody component so no duplicate authored component is introduced.
enum class Mobility : std::uint8_t { Static, Kinematic, Dynamic };

inline Mobility MobilityOf(const ecs::RigidBody* rb) {
    if (!rb) return Mobility::Static;                 // no body: authored/static transform
    if (rb->kinematic) return Mobility::Kinematic;
    return (rb->invMass > 0.0f) ? Mobility::Dynamic : Mobility::Static;
}

// A renderable extracted from one entity. Bounds/GPU data are filled by the renderer layer later;
// here we cache the transform, mesh reference, mobility and the revision we extracted from.
struct RenderProxy {
    ecs::Entity   entity = ecs::kNull;
    glm::mat4     worldMatrix{1.0f};
    const Mesh*   mesh = nullptr;
    glm::vec3     color{1.0f};
    Mobility      mobility = Mobility::Static;
    bool          castShadow = true;
    std::uint64_t transformRev = 0;   // Transform revision this proxy was extracted from
};

// Pass 5: per-sync render-extraction profiling (added/removed proxies, transform-only vs mesh
// updates, and unchanged static objects skipped).
struct RenderSyncStats {
    int added = 0;
    int removed = 0;
    int transformUpdates = 0;
    int meshUpdates = 0;
    int unchanged = 0;
};

class RenderScene {
public:
    // Rebuild/refresh proxies from the registry. Incremental: skips entirely when nothing structural
    // and no transform changed since the last sync; otherwise touches only changed/added/removed
    // proxies. Returns the number of proxies added+updated+removed this call (0 == fully cached).
    int SyncFrom(ecs::Registry& reg) {
        const std::uint64_t structRev = reg.StructuralRevision();
        const std::uint64_t txPoolRev = reg.PoolRevision<ecs::Transform>();
        m_stats = RenderSyncStats{};   // Pass 5: per-sync extraction profiling
        // Fast out: no structural change AND no Transform pool change since last sync -> nothing to do.
        if (m_synced && structRev == m_lastStructuralRev && txPoolRev == m_lastTransformPoolRev) {
            m_stats.unchanged = static_cast<int>(m_proxies.size());
            return 0;
        }

        int changes = 0;

        // 1) Add/update proxies for every renderable (Transform + MeshRenderer). The smallest-pool
        //    view drives from whichever of the two is rarer.
        reg.view<ecs::Transform, ecs::MeshRenderer>().each(
            [&](ecs::Entity e, ecs::Transform& t, ecs::MeshRenderer& mr) {
                const std::uint64_t rev = reg.Revision<ecs::Transform>(e);
                auto it = m_index.find(e);
                if (it == m_index.end()) {                       // new renderable -> add proxy
                    RenderProxy p;
                    p.entity = e; p.worldMatrix = t.Model(); p.mesh = mr.mesh; p.color = mr.color;
                    p.mobility = MobilityOf(reg.TryGet<ecs::RigidBody>(e));
                    p.transformRev = rev;
                    m_index.emplace(e, m_proxies.size());
                    m_proxies.push_back(p);
                    if (p.castShadow) ++m_shadowCasterRevision;   // a new caster changes the shadow set
                    ++changes; ++m_stats.added;
                } else {                                          // existing -> refresh only if changed
                    RenderProxy& p = m_proxies[it->second];
                    const bool moved = p.transformRev != rev;
                    const bool meshChanged = p.mesh != mr.mesh;
                    if (moved || meshChanged) {
                        if (moved) { p.worldMatrix = t.Model(); p.transformRev = rev; ++m_stats.transformUpdates; }
                        if (meshChanged) { p.mesh = mr.mesh; ++m_stats.meshUpdates; }
                        p.color = mr.color;
                        p.mobility = MobilityOf(reg.TryGet<ecs::RigidBody>(e));
                        if (p.castShadow) ++m_shadowCasterRevision;   // caster transform/mesh changed
                        ++changes;
                    } else {
                        ++m_stats.unchanged;
                    }
                }
            });

        // 2) Remove proxies whose entity was destroyed or lost its renderable components.
        for (std::size_t i = 0; i < m_proxies.size(); ) {
            const ecs::Entity e = m_proxies[i].entity;
            const bool stillRenderable = reg.Valid(e)
                && reg.Has<ecs::Transform>(e) && reg.Has<ecs::MeshRenderer>(e);
            if (stillRenderable) { ++i; continue; }
            if (m_proxies[i].castShadow) ++m_shadowCasterRevision;   // a removed caster changes the set
            // swap-pop, keep the index map consistent
            const std::size_t last = m_proxies.size() - 1;
            m_index.erase(e);
            if (i != last) { m_proxies[i] = m_proxies[last]; m_index[m_proxies[i].entity] = i; }
            m_proxies.pop_back();
            ++changes; ++m_stats.removed;
        }

        m_lastStructuralRev = structRev;
        m_lastTransformPoolRev = txPoolRev;
        m_synced = true;
        return changes;
    }

    // Bumps only when the shadow-casting set changes (caster added/removed, transform or mesh
    // changed). Shadow systems compare this to their last-seen value to invalidate incrementally.
    std::uint64_t ShadowCasterRevision() const { return m_shadowCasterRevision; }

    // Pass 5: metrics from the most recent SyncFrom (render-extraction profiling).
    const RenderSyncStats& LastSyncStats() const { return m_stats; }

    const std::vector<RenderProxy>& Proxies() const { return m_proxies; }
    std::size_t ProxyCount() const { return m_proxies.size(); }
    const RenderProxy* Find(ecs::Entity e) const {
        auto it = m_index.find(e); return it == m_index.end() ? nullptr : &m_proxies[it->second];
    }

    void Clear() {
        m_proxies.clear(); m_index.clear();
        m_synced = false; m_lastStructuralRev = 0; m_lastTransformPoolRev = 0;
        ++m_shadowCasterRevision;   // the whole caster set went away
    }

    // Phase 10 developer parity check: the RenderScene proxy set must exactly equal a full ECS scan
    // of renderable entities (same count AND same entity IDs). Returns true on parity. Intended for
    // debug builds while the renderer still runs the old Registry path in parallel.
    bool ParityCheck(ecs::Registry& reg) const {
        std::size_t scanned = 0;
        bool ok = true;
        reg.view<ecs::Transform, ecs::MeshRenderer>().each(
            [&](ecs::Entity e, ecs::Transform&, ecs::MeshRenderer&) {
                ++scanned;
                if (!Find(e)) ok = false;                 // ECS renderable missing from RenderScene
            });
        return ok && scanned == m_proxies.size();         // and no stale proxies
    }

private:
    std::vector<RenderProxy> m_proxies;
    std::unordered_map<ecs::Entity, std::size_t> m_index;   // entity -> proxy slot
    std::uint64_t m_shadowCasterRevision = 0;
    std::uint64_t m_lastStructuralRev = 0;
    std::uint64_t m_lastTransformPoolRev = 0;
    bool          m_synced = false;
    RenderSyncStats m_stats;
};

} // namespace engine
