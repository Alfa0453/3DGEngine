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

// ECS Pass 2 (follow-up) -- a proxy for the ACTUAL PBR renderables the PbrRenderer draws
// (Transform + MeshPBR), as opposed to the lightweight MeshRenderer proxy above. It carries the
// world matrix, mesh, a compact material snapshot for the renderer's batch/textured/custom triage,
// and the revisions it was extracted from. `perObject` mirrors the renderer's decision: an object
// that is textured, non-opaque, or uses a custom shader can't join an instanced batch. The renderer
// still TryGet<MeshPBR> for the full material/textures of the handful it actually binds; this proxy
// drives culling + triage without a per-frame full ECS scan. GL-free (mesh/shader are opaque ptrs).
struct PbrRenderProxy {
    ecs::Entity   entity = ecs::kNull;
    glm::mat4     worldMatrix{1.0f};
    const Mesh*   mesh = nullptr;
    glm::vec3     albedo{0.8f};
    float         metallic = 0.0f, roughness = 0.5f, ao = 1.0f;
    glm::vec3     emissive{0.0f};
    int           blendMode = 0;         // ecs::PbrMaterial::BlendMode value
    bool          perObject = false;     // textured / non-opaque / custom -> not instanceable
    bool          customShader = false;
    bool          castShadow = true;
    Mobility      mobility = Mobility::Static;
    std::uint64_t transformRev = 0;
    std::uint64_t materialRev = 0;       // MeshPBR revision the snapshot was taken from
};

// Renderer triage: an object needs a per-object draw (vs an instanced batch) when it has any texture
// map, a non-opaque/masked blend mode, reduced opacity, or a custom shader.
inline bool PbrNeedsPerObject(const ecs::MeshPBR& m) {
    const ecs::PbrMaterial& mat = m.material;
    if (m.customShader) return true;
    if (mat.albedoMap || mat.normalMap || mat.metalRoughMap || mat.heightMap || mat.emissiveMap) return true;
    if (mat.blendMode != ecs::PbrMaterial::BlendMode::Opaque) return true;
    if (mat.opacity < 1.0f) return true;
    return false;
}

// Imported-model renderable (Transform + LoadedModelAsset), the second draw set the PbrRenderer
// handles. Carries the world matrix + the opaque Model pointer + the revision it was extracted from.
// Deliberately does NOT pull in Model.h (kept GL-free and harness-buildable): the renderer already
// dereferences the Model for bounds/submeshes at draw time, so the proxy only needs the pointer and
// the transform. A model reimport that swaps the pointer is caught here; in-place content changes are
// observed by the renderer through the same pointer.
struct ModelRenderProxy {
    ecs::Entity   entity = ecs::kNull;
    glm::mat4     worldMatrix{1.0f};
    const Model*  model = nullptr;
    bool          castShadow = true;
    Mobility      mobility = Mobility::Static;
    std::uint64_t transformRev = 0;
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

    // Incremental extraction of the PBR renderables (Transform + MeshPBR) the PbrRenderer draws.
    // Same incremental contract as SyncFrom: skips when neither the Transform nor MeshPBR pool changed
    // and nothing structural changed; otherwise touches only changed/added/removed proxies. Returns
    // the number of proxies added+updated+removed. This is the data-ready foundation for migrating the
    // renderer's gather off the raw registry view (behind a flag); it does not draw anything itself.
    int SyncPbrFrom(ecs::Registry& reg) {
        const std::uint64_t structRev  = reg.StructuralRevision();
        const std::uint64_t txPoolRev  = reg.PoolRevision<ecs::Transform>();
        const std::uint64_t matPoolRev = reg.PoolRevision<ecs::MeshPBR>();
        m_pbrStats = RenderSyncStats{};
        if (m_pbrSynced && structRev == m_lastPbrStructRev
            && txPoolRev == m_lastPbrTxRev && matPoolRev == m_lastPbrMatRev) {
            m_pbrStats.unchanged = static_cast<int>(m_pbrProxies.size());
            return 0;
        }
        int changes = 0;
        reg.view<ecs::Transform, ecs::MeshPBR>().each(
            [&](ecs::Entity e, ecs::Transform& t, ecs::MeshPBR& m) {
                const std::uint64_t txRev  = reg.Revision<ecs::Transform>(e);
                const std::uint64_t matRev = reg.Revision<ecs::MeshPBR>(e);
                auto it = m_pbrIndex.find(e);
                if (it == m_pbrIndex.end()) {
                    PbrRenderProxy p;
                    p.entity = e; p.worldMatrix = t.Model();
                    ExtractPbrMaterial(m, p);
                    p.mobility = MobilityOf(reg.TryGet<ecs::RigidBody>(e));
                    p.transformRev = txRev; p.materialRev = matRev;
                    m_pbrIndex.emplace(e, m_pbrProxies.size());
                    m_pbrProxies.push_back(p);
                    ++changes; ++m_pbrStats.added;
                } else {
                    PbrRenderProxy& p = m_pbrProxies[it->second];
                    const bool moved = p.transformRev != txRev;
                    const bool matChanged = p.materialRev != matRev;
                    if (moved)      { p.worldMatrix = t.Model(); p.transformRev = txRev; ++m_pbrStats.transformUpdates; }
                    if (matChanged) { ExtractPbrMaterial(m, p); p.materialRev = matRev; ++m_pbrStats.meshUpdates; }
                    if (moved || matChanged) ++changes; else ++m_pbrStats.unchanged;
                }
            });
        for (std::size_t i = 0; i < m_pbrProxies.size(); ) {
            const ecs::Entity e = m_pbrProxies[i].entity;
            const bool ok = reg.Valid(e) && reg.Has<ecs::Transform>(e) && reg.Has<ecs::MeshPBR>(e);
            if (ok) { ++i; continue; }
            const std::size_t last = m_pbrProxies.size() - 1;
            m_pbrIndex.erase(e);
            if (i != last) { m_pbrProxies[i] = m_pbrProxies[last]; m_pbrIndex[m_pbrProxies[i].entity] = i; }
            m_pbrProxies.pop_back();
            ++changes; ++m_pbrStats.removed;
        }
        m_lastPbrStructRev = structRev; m_lastPbrTxRev = txPoolRev; m_lastPbrMatRev = matPoolRev;
        m_pbrSynced = true;
        return changes;
    }
    const std::vector<PbrRenderProxy>& PbrProxies() const { return m_pbrProxies; }
    std::size_t PbrProxyCount() const { return m_pbrProxies.size(); }
    const RenderSyncStats& LastPbrSyncStats() const { return m_pbrStats; }
    // Parity: the PBR proxy set must equal a full ECS scan of Transform+MeshPBR entities.
    bool ParityCheckPbr(ecs::Registry& reg) const {
        std::size_t scanned = 0; bool ok = true;
        reg.view<ecs::Transform, ecs::MeshPBR>().each([&](ecs::Entity e, ecs::Transform&, ecs::MeshPBR&) {
            ++scanned;
            if (m_pbrIndex.find(e) == m_pbrIndex.end()) ok = false;
        });
        return ok && scanned == m_pbrProxies.size();
    }

    // Incremental extraction of imported-model renderables (Transform + LoadedModelAsset). Same
    // incremental contract; a swapped Model pointer (reimport/reassign) is treated as a change.
    int SyncModelsFrom(ecs::Registry& reg) {
        const std::uint64_t structRev  = reg.StructuralRevision();
        const std::uint64_t txPoolRev  = reg.PoolRevision<ecs::Transform>();
        const std::uint64_t modPoolRev = reg.PoolRevision<ecs::LoadedModelAsset>();
        m_modelStats = RenderSyncStats{};
        if (m_modelSynced && structRev == m_lastModelStructRev
            && txPoolRev == m_lastModelTxRev && modPoolRev == m_lastModelPoolRev) {
            m_modelStats.unchanged = static_cast<int>(m_modelProxies.size());
            return 0;
        }
        int changes = 0;
        reg.view<ecs::Transform, ecs::LoadedModelAsset>().each(
            [&](ecs::Entity e, ecs::Transform& t, ecs::LoadedModelAsset& la) {
                const std::uint64_t txRev = reg.Revision<ecs::Transform>(e);
                auto it = m_modelIndex.find(e);
                if (it == m_modelIndex.end()) {
                    ModelRenderProxy p;
                    p.entity = e; p.worldMatrix = t.Model(); p.model = la.model;
                    p.mobility = MobilityOf(reg.TryGet<ecs::RigidBody>(e)); p.transformRev = txRev;
                    m_modelIndex.emplace(e, m_modelProxies.size());
                    m_modelProxies.push_back(p);
                    ++changes; ++m_modelStats.added;
                } else {
                    ModelRenderProxy& p = m_modelProxies[it->second];
                    const bool moved = p.transformRev != txRev;
                    const bool modelChanged = p.model != la.model;
                    if (moved)        { p.worldMatrix = t.Model(); p.transformRev = txRev; ++m_modelStats.transformUpdates; }
                    if (modelChanged) { p.model = la.model; ++m_modelStats.meshUpdates; }
                    if (moved || modelChanged) ++changes; else ++m_modelStats.unchanged;
                }
            });
        for (std::size_t i = 0; i < m_modelProxies.size(); ) {
            const ecs::Entity e = m_modelProxies[i].entity;
            const bool ok = reg.Valid(e) && reg.Has<ecs::Transform>(e) && reg.Has<ecs::LoadedModelAsset>(e);
            if (ok) { ++i; continue; }
            const std::size_t last = m_modelProxies.size() - 1;
            m_modelIndex.erase(e);
            if (i != last) { m_modelProxies[i] = m_modelProxies[last]; m_modelIndex[m_modelProxies[i].entity] = i; }
            m_modelProxies.pop_back();
            ++changes; ++m_modelStats.removed;
        }
        m_lastModelStructRev = structRev; m_lastModelTxRev = txPoolRev; m_lastModelPoolRev = modPoolRev;
        m_modelSynced = true;
        return changes;
    }
    const std::vector<ModelRenderProxy>& ModelProxies() const { return m_modelProxies; }
    std::size_t ModelProxyCount() const { return m_modelProxies.size(); }
    const RenderSyncStats& LastModelSyncStats() const { return m_modelStats; }
    bool ParityCheckModels(ecs::Registry& reg) const {
        std::size_t scanned = 0; bool ok = true;
        reg.view<ecs::Transform, ecs::LoadedModelAsset>().each([&](ecs::Entity e, ecs::Transform&, ecs::LoadedModelAsset&) {
            ++scanned;
            if (m_modelIndex.find(e) == m_modelIndex.end()) ok = false;
        });
        return ok && scanned == m_modelProxies.size();
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
        m_pbrProxies.clear(); m_pbrIndex.clear();
        m_pbrSynced = false; m_lastPbrStructRev = 0; m_lastPbrTxRev = 0; m_lastPbrMatRev = 0;
        m_modelProxies.clear(); m_modelIndex.clear();
        m_modelSynced = false; m_lastModelStructRev = 0; m_lastModelTxRev = 0; m_lastModelPoolRev = 0;
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
    static void ExtractPbrMaterial(const ecs::MeshPBR& m, PbrRenderProxy& p) {
        p.mesh = m.mesh;
        p.albedo = m.material.albedo; p.metallic = m.material.metallic;
        p.roughness = m.material.roughness; p.ao = m.material.ao;
        p.emissive = m.material.emissive; p.blendMode = static_cast<int>(m.material.blendMode);
        p.customShader = (m.customShader != nullptr);
        p.perObject = PbrNeedsPerObject(m);
    }

    std::vector<RenderProxy> m_proxies;
    std::unordered_map<ecs::Entity, std::size_t> m_index;   // entity -> proxy slot
    std::uint64_t m_shadowCasterRevision = 0;
    std::uint64_t m_lastStructuralRev = 0;
    std::uint64_t m_lastTransformPoolRev = 0;
    bool          m_synced = false;
    RenderSyncStats m_stats;

    // PBR renderable track (Transform + MeshPBR).
    std::vector<PbrRenderProxy> m_pbrProxies;
    std::unordered_map<ecs::Entity, std::size_t> m_pbrIndex;
    std::uint64_t m_lastPbrStructRev = 0, m_lastPbrTxRev = 0, m_lastPbrMatRev = 0;
    bool          m_pbrSynced = false;
    RenderSyncStats m_pbrStats;

    // Imported-model renderable track (Transform + LoadedModelAsset).
    std::vector<ModelRenderProxy> m_modelProxies;
    std::unordered_map<ecs::Entity, std::size_t> m_modelIndex;
    std::uint64_t m_lastModelStructRev = 0, m_lastModelTxRev = 0, m_lastModelPoolRev = 0;
    bool          m_modelSynced = false;
    RenderSyncStats m_modelStats;
};

} // namespace engine
