#pragma once

#include "engine/ecs/Entity.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <tuple>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace engine {
namespace ecs {

// ============================================================================
// ECS Pass 1 additions (all ADDITIVE -- every pre-existing API below is kept):
//   * per-component revisions + per-pool revision + registry structural revision
//   * Patch<T> / MarkUpdated<T> tracked mutation
//   * change records (added/updated/removed) with an optional mutation source
//   * smallest-pool view iteration (callback argument order unchanged)
//   * deferred structural commands (DestroyDeferred / AddDeferred / RemoveDeferred + flush)
//   * ReserveEntities / Reserve<T> / Begin-EndBulkMutation
//   * lightweight profiler counters + debug Validate()
// The storage remains a sparse set; Entity representation is unchanged.
// ============================================================================

// Where a change came from (Phase 18). Recorded on change entries; defaults to Unknown so existing
// writes need not specify it. Pass 2 uses this more heavily.
enum class ChangeSource : std::uint8_t {
    Unknown = 0, Editor, Gameplay, Script, Physics, Animation, Hierarchy, SceneLoad, Import
};

// One entry in a pool's change list: which entity changed and (optionally) the source.
struct ChangeEntry {
    Entity       entity = kNull;
    ChangeSource source = ChangeSource::Unknown;
};

// --- Component storage: a sparse set ---------------------------------------
//
// A sparse set keeps two arrays:
//   * `dense`  — a *packed* list of the entities that have this component (and a
//                parallel `comps` array of the component data). Packed = fast,
//                cache-friendly iteration with no gaps.
//   * `sparse` — indexed by an entity's INDEX, giving the position of that entity
//                inside `dense` (or kInvalid). This makes has/get O(1).
//   * `revisions` — Pass 1: parallel to dense/comps; a monotonically increasing
//                stamp of when each component last changed. Moved together with the
//                component on swap-remove so it stays aligned.
//
// Removal is swap-and-pop: move the last element into the hole and fix one
// pointer, so it stays O(1) and `dense` stays packed (order is not preserved).

class IPool {
public:
    virtual ~IPool() = default;
    virtual void Remove(Entity e) = 0;
    virtual bool Has(Entity e) const = 0;
    // Copy this component (if present on `src`) onto `dst`. Used by Registry::Clone.
    virtual void CopyComponent(Entity src, Entity dist) = 0;
    // Pass 1:
    virtual void ClearChangeRecords() = 0;   // drop this epoch's added/updated/removed lists
    virtual bool Validate() const = 0;        // sparse-set invariant check (debug)
};

template <class T>
class Pool : public IPool {
public:
    static constexpr std::uint32_t kInvalid = 0xFFFFFFFFu;

    std::vector<std::uint32_t> sparse;  // entity index -> position in dense
    std::vector<Entity>        dense;   // packed entities that own a T
    std::vector<T>             comps;   // component data, parallel to dense
    std::vector<std::uint64_t> revisions;  // Pass 1: last-change stamp, parallel to dense/comps

    // Pass 1 pool metadata.
    std::uint64_t poolRevision = 0;     // bumps on any add / remove / tracked update (Phase 2)
    std::uint64_t epochStart   = 0;     // poolRevision at the last ClearChangeRecords (update dedup)
    std::vector<ChangeEntry> addedList, updatedList, removedList;   // change records (Phase 6)
    ChangeSource  recordSource = ChangeSource::Unknown;  // stamped onto new records (set by Registry)
    bool          trackChanges = true;  // false during bulk mutation: skip list appends, keep revisions

    bool Has(Entity e) const override {
        const std::uint32_t i = EntityIndex(e);
        return i < sparse.size() && sparse[i] != kInvalid && dense[sparse[i]] == e;
    }

    T& Add(Entity e, T value) {
        const std::uint32_t i = EntityIndex(e);
        if (i >= sparse.size()) sparse.resize(i + 1, kInvalid);
        if (sparse[i] != kInvalid) {                 // already present: overwrite == tracked update
            const std::uint32_t slot = sparse[i];
            comps[slot] = std::move(value);
            const bool firstThisEpoch = revisions[slot] <= epochStart;
            revisions[slot] = ++poolRevision;
            if (trackChanges && firstThisEpoch) updatedList.push_back({e, recordSource});
            return comps[slot];
        }
        sparse[i] = static_cast<std::uint32_t>(dense.size());
        dense.push_back(e);
        comps.push_back(std::move(value));
        revisions.push_back(++poolRevision);         // keep revisions aligned with dense/comps
        if (trackChanges) addedList.push_back({e, recordSource});
        return comps.back();
    }

    T& Get(Entity e) { return comps[sparse[EntityIndex(e)]]; }
    const T& Get(Entity e) const { return comps[sparse[EntityIndex(e)]]; }

    void CopyComponent(Entity src, Entity dst) override {
        if (!Has(src)) return;
        if constexpr (std::is_copy_constructible_v<T>)
            Add(dst, comps[sparse[EntityIndex(src)]]);  // copy the component value
         // (move-only components can't be cloned and are skipped)
    }

    void Remove(Entity e) override {
        const std::uint32_t i = EntityIndex(e);
        if (i >= sparse.size() || sparse[i] == kInvalid) return;
        const std::uint32_t hole = sparse[i];
        const std::uint32_t last = static_cast<std::uint32_t>(dense.size() - 1);
        dense[hole] = dense[last];                      // move last element into the hole
        comps[hole] = std::move(comps[last]);
        revisions[hole] = revisions[last];              // move the revision with it (stay aligned)
        sparse[EntityIndex(dense[hole])] = hole;        // repoint the moved entity
        dense.pop_back();
        comps.pop_back();
        revisions.pop_back();
        sparse[i] = kInvalid;
        ++poolRevision;
        if (trackChanges) removedList.push_back({e, recordSource});
    }

    // Pass 1: record a direct write as a tracked update (Phase 5). Bumps the component + pool
    // revision and adds the entity to the update list once per epoch.
    void MarkUpdated(Entity e) {
        const std::uint32_t i = EntityIndex(e);
        if (i >= sparse.size() || sparse[i] == kInvalid) return;
        const std::uint32_t slot = sparse[i];
        const bool firstThisEpoch = revisions[slot] <= epochStart;
        revisions[slot] = ++poolRevision;
        if (trackChanges && firstThisEpoch) updatedList.push_back({e, recordSource});
    }

    // Revision of a component, or 0 (documented invalid/default) when absent -- never crashes.
    std::uint64_t RevisionOf(Entity e) const {
        const std::uint32_t i = EntityIndex(e);
        if (i >= sparse.size() || sparse[i] == kInvalid) return 0;
        return revisions[sparse[i]];
    }

    void Reserve(std::size_t n) { dense.reserve(n); comps.reserve(n); revisions.reserve(n); }

    void ClearChangeRecords() override {
        addedList.clear(); updatedList.clear(); removedList.clear();
        epochStart = poolRevision;
    }

    bool Validate() const override {
        if (dense.size() != comps.size() || dense.size() != revisions.size()) return false;
        for (std::size_t k = 0; k < dense.size(); ++k) {
            const std::uint32_t idx = EntityIndex(dense[k]);
            if (idx >= sparse.size() || sparse[idx] != k) return false;
        }
        return true;
    }
};

template <class... Cs> class View;  // forward declaration

// Low-overhead development counters (Phase 19). Not intended for shipping instrumentation.
struct RegistryStats {
    std::uint64_t entityCreates = 0;
    std::uint64_t entityDestroys = 0;
    std::uint64_t componentsAdded = 0;
    std::uint64_t componentsRemoved = 0;
    std::uint64_t componentsUpdated = 0;
    std::uint64_t structuralCommandsQueued = 0;
    std::uint64_t structuralCommandsApplied = 0;
    std::uint64_t viewInvocations = 0;
    std::uint64_t viewEntitiesTested = 0;
};

// --- The Registry: owns entities and component pools -----------------------

class Registry {
public:
    // Create a fresh entity, reusing a recycled slot if one is free.
    Entity Create() {
        std::uint32_t index;
        if (!m_free.empty()) {
            index = m_free.back();
            m_free.pop_back();
        } else {
            index = static_cast<std::uint32_t>(m_generations.size());
            m_generations.push_back(0);
        }
        ++m_structuralRevision;
        ++m_stats.entityCreates;
        return MakeEntity(index, m_generations[index]);
    }

    // True if `e` still refers to a live entity (generation matches).
    bool Valid(Entity e) const {
        const std::uint32_t i = EntityIndex(e);
        return i < m_generations.size() && m_generations[i] == EntityGeneration(e);
    }

    // Destroy `e`: drop all its components, bump its generation (invalidating any
    // outstanding handles), and free its slot for reuse.
    void Destroy(Entity e) {
        if (!Valid(e)) return;
        for (auto& kv : m_pools) { kv.second->Remove(e); }
        ++m_generations[EntityIndex(e)];
        m_free.push_back(EntityIndex(e));
        ++m_structuralRevision;
        ++m_stats.entityDestroys;
    }

    // Create a new entity that is a COPY of `src`: every component on `src` is
    // copied onto the new entity (the prototype pattern for prefabs). Returns the
    // new entity (empty if `src` is invalid).
    Entity Clone(Entity src) {
        Entity dst = Create();
        if (Valid(src))
            for (auto& kv : m_pools) kv.second->CopyComponent(src, dst);
        return dst;
    }

    template <class T> T& Add(Entity e, T value = T{}) {
        Pool<T>& p = Assure<T>();
        p.recordSource = m_changeSource; p.trackChanges = !m_bulk;
        const bool wasPresent = p.Has(e);
        T& ref = p.Add(e, std::move(value));
        if (!wasPresent) { ++m_structuralRevision; ++m_stats.componentsAdded; }
        else             { ++m_stats.componentsUpdated; }
        return ref;
    }
    template <class T, class... Args> T& Emplace(Entity e, Args&&... args) {
        return Add<T>(e, T{std::forward<Args>(args)...});
    }
    template <class T> bool Has(Entity e) { auto* p = TryPool<T>(); return p && p->Has(e); }
    template <class T> bool Has(Entity e) const { auto* p = TryPool<T>(); return p && p->Has(e); }
    template <class T> T&   Get(Entity e) { return Assure<T>().Get(e); }
    template <class T> T*   TryGet(Entity e) {
        auto* p = TryPool<T>();
        return (p && p->Has(e)) ? &p->Get(e) : nullptr;
    }
    template <class T> const T* TryGet(Entity e) const {
        auto* p = TryPool<T>();
        return (p && p->Has(e)) ? &p->Get(e) : nullptr;
    }
    template <class T> void Remove(Entity e) {
        if (auto* p = TryPool<T>()) {
            if (!p->Has(e)) return;
            p->recordSource = m_changeSource; p->trackChanges = !m_bulk;
            p->Remove(e);
            ++m_structuralRevision; ++m_stats.componentsRemoved;
        }
    }

    // Iterate entities that have every component in Cs (see View below).
    template <class... Cs> View<Cs...> view() { return View<Cs...>(*this); }

    std::size_t AliveCount() const { return m_generations.size() - m_free.size(); }

    template <class T> Pool<T>* TryPool() {
        auto it = m_pools.find(std::type_index(typeid(T)));
        return it == m_pools.end() ? nullptr : static_cast<Pool<T>*>(it->second.get());
    }
    template <class T> const Pool<T>* TryPool() const {
        auto it = m_pools.find(std::type_index(typeid(T)));
        return it == m_pools.end() ? nullptr : static_cast<const Pool<T>*>(it->second.get());
    }

    // ---- Pass 1: revisions -------------------------------------------------
    std::uint64_t StructuralRevision() const { return m_structuralRevision; }
    template <class T> std::uint64_t PoolRevision() const {
        auto* p = TryPool<T>(); return p ? p->poolRevision : 0;
    }
    template <class T> std::uint64_t Revision(Entity e) const {
        auto* p = TryPool<T>(); return p ? p->RevisionOf(e) : 0;
    }

    // ---- Pass 1: tracked mutation -----------------------------------------
    // Patch runs `fn(T&)` then records the component as updated. Returns false if absent.
    template <class T, class F> bool Patch(Entity e, F&& fn) {
        auto* p = TryPool<T>();
        if (!p || !p->Has(e)) return false;
        fn(p->Get(e));
        p->recordSource = m_changeSource; p->trackChanges = !m_bulk;
        p->MarkUpdated(e);
        ++m_stats.componentsUpdated;
        return true;
    }
    // Record a direct write (Get<T>(e).x = ...) as an update. Does not modify the component.
    template <class T> void MarkUpdated(Entity e) {
        if (auto* p = TryPool<T>()) {
            p->recordSource = m_changeSource; p->trackChanges = !m_bulk;
            p->MarkUpdated(e); ++m_stats.componentsUpdated;
        }
    }
    template <class T> void MarkUpdated(const Entity* entities, std::size_t count) {
        auto* p = TryPool<T>();
        if (!p) return;
        p->recordSource = m_changeSource; p->trackChanges = !m_bulk;
        for (std::size_t k = 0; k < count; ++k) p->MarkUpdated(entities[k]);
        m_stats.componentsUpdated += count;
    }
    template <class T> void MarkUpdated(const std::vector<Entity>& entities) {
        MarkUpdated<T>(entities.data(), entities.size());
    }

    // ---- Pass 1: change records (multi-consumer via revisions or read-only lists) ----
    // These lists accumulate until ClearChangeRecords(); no consumer clears them, so multiple
    // systems (e.g. Physics and Renderer) can both read the same Transform changes in a frame.
    template <class T> const std::vector<ChangeEntry>* Added() const {
        auto* p = TryPool<T>(); return p ? &p->addedList : nullptr; }
    template <class T> const std::vector<ChangeEntry>* Updated() const {
        auto* p = TryPool<T>(); return p ? &p->updatedList : nullptr; }
    template <class T> const std::vector<ChangeEntry>* Removed() const {
        auto* p = TryPool<T>(); return p ? &p->removedList : nullptr; }
    // Clear this epoch's change lists for one type or all types (call once per frame by the owner).
    template <class T> void ClearChangeRecords() { if (auto* p = TryPool<T>()) p->ClearChangeRecords(); }
    void ClearAllChangeRecords() { for (auto& kv : m_pools) kv.second->ClearChangeRecords(); }

    // ---- Pass 1: mutation source (Phase 18) --------------------------------
    void SetChangeSource(ChangeSource s) { m_changeSource = s; }
    ChangeSource CurrentChangeSource() const { return m_changeSource; }

    // ---- Pass 1: deferred structural commands (Phases 9-12) ----------------
    // These queue work applied at FlushStructuralCommands(). Immediate Create/Destroy/Add/Remove are
    // untouched. Deferred ops re-validate the entity at flush time, so a double-destroy, an add to a
    // dead/stale entity, or a remove of an absent component are all safe no-ops.
    void DestroyDeferred(Entity e) { m_deferredDestroys.push_back(e); ++m_stats.structuralCommandsQueued; }
    template <class T> void AddDeferred(Entity e, T value = T{}) {
        m_deferredOps.emplace_back([e, value = std::move(value)](Registry& r) mutable {
            if (r.Valid(e)) r.Add<T>(e, std::move(value));
        });
        ++m_stats.structuralCommandsQueued;
    }
    template <class T> void RemoveDeferred(Entity e) {
        m_deferredOps.emplace_back([e](Registry& r) { if (r.Valid(e)) r.Remove<T>(e); });
        ++m_stats.structuralCommandsQueued;
    }
    // Apply adds/removes first, then destroys last (so a component op never races its own destroy).
    void FlushStructuralCommands() {
        for (auto& op : m_deferredOps) { op(*this); ++m_stats.structuralCommandsApplied; }
        m_deferredOps.clear();
        for (Entity e : m_deferredDestroys) { Destroy(e); ++m_stats.structuralCommandsApplied; }
        m_deferredDestroys.clear();
    }

    // ---- Pass 1: reserve + bulk mutation (Phases 16-17) --------------------
    void ReserveEntities(std::size_t count) { m_generations.reserve(count); m_free.reserve(count); }
    template <class T> void Reserve(std::size_t count) { Assure<T>().Reserve(count); }
    // While bulk mutation is active, change LISTS are not appended (revisions/structure still update),
    // to batch notification work during scene load / clone / procedural generation. Correctness and
    // sparse-set validity are unaffected.
    void BeginBulkMutation() { m_bulk = true; }
    void EndBulkMutation() { m_bulk = false; ++m_structuralRevision; }
    bool BulkMutating() const { return m_bulk; }

    // ---- Pass 1: profiler + validation ------------------------------------
    const RegistryStats& Stats() const { return m_stats; }
    void ResetStats() { m_stats = RegistryStats{}; }
    bool Validate() const { for (auto& kv : m_pools) if (!kv.second->Validate()) return false; return true; }

private:
    template <class... Cs> friend class View;   // views bump viewInvocations / viewEntitiesTested

    template <class T> Pool<T>& Assure() {
        const std::type_index ti(typeid(T));
        auto it = m_pools.find(ti);
        if (it == m_pools.end())
            it = m_pools.emplace(ti, std::make_unique<Pool<T>>()).first;
        return *static_cast<Pool<T>*>(it->second.get());
    }

    std::vector<std::uint8_t>  m_generations;  // generation per index
    std::vector<std::uint32_t> m_free;         // recycled indices
    std::unordered_map<std::type_index, std::unique_ptr<IPool>> m_pools;

    std::uint64_t m_structuralRevision = 0;    // Phase 3: bumps on create/destroy/add/remove
    ChangeSource  m_changeSource = ChangeSource::Unknown;
    bool          m_bulk = false;
    std::vector<std::function<void(Registry&)>> m_deferredOps;   // deferred add/remove
    std::vector<Entity> m_deferredDestroys;                       // deferred destroys (applied last)
    mutable RegistryStats m_stats;
};

// --- View: iterate entities that have all of Cs ----------------------------
//
//   reg.view<Position, Velocity>().each([](Entity e, Position& p, Velocity& v) {
//       p.value += v.value * dt;
//   });
//
// Pass 1: the view now iterates whichever participating pool is SMALLEST (fewest entities) and
// checks the rest, so you no longer have to hand-order the arguments rarest-first. The callback
// still receives the components in the EXACT order you requested (Position&, Velocity& above),
// regardless of which pool was chosen to drive the loop. Iterating in reverse keeps it safe to
// Remove the current entity during the loop. If any required pool is missing, the view is empty and
// no pools are created by querying it.
template <class... Cs>
class View {
public:
    explicit View(Registry& reg) : m_reg(&reg) {}

    bool empty() const {
        bool anyNull = false;
        std::size_t minSize = 0; bool first = true;
        (void)std::initializer_list<int>{ (([&] {
            const Pool<Cs>* p = m_reg->template TryPool<Cs>();
            if (!p) { anyNull = true; return; }
            if (first || p->dense.size() < minSize) { minSize = p->dense.size(); first = false; }
        }(), 0))... };
        return anyNull || first || minSize == 0;
    }

    template <class F> void each(F&& func) {
        ++m_reg->m_stats.viewInvocations;
        // Gather the pools; a missing pool means an empty view.
        auto pools = std::make_tuple(m_reg->template TryPool<Cs>()...);
        bool anyNull = false;
        forEachIndex(std::index_sequence_for<Cs...>{}, [&](auto I) {
            if (std::get<I.value>(pools) == nullptr) anyNull = true;
        });
        if (anyNull) return;

        // Pick the smallest pool to drive iteration.
        std::size_t sizes[sizeof...(Cs)];
        forEachIndex(std::index_sequence_for<Cs...>{}, [&](auto I) {
            sizes[I.value] = std::get<I.value>(pools)->dense.size();
        });
        std::size_t minIdx = 0;
        for (std::size_t i = 1; i < sizeof...(Cs); ++i) if (sizes[i] < sizes[minIdx]) minIdx = i;

        // Drive the loop from the smallest pool; dispatch to the matching typed iterator.
        forEachIndex(std::index_sequence_for<Cs...>{}, [&](auto I) {
            if (I.value == minIdx) iterateFrom<I.value>(pools, func);
        });
    }

private:
    template <std::size_t... Is, class Fn>
    static void forEachIndex(std::index_sequence<Is...>, Fn&& fn) {
        (void)std::initializer_list<int>{ (fn(std::integral_constant<std::size_t, Is>{}), 0)... };
    }

    template <std::size_t I, class Tuple, class F>
    void iterateFrom(Tuple& pools, F& func) {
        auto* lead = std::get<I>(pools);
        for (std::size_t k = lead->dense.size(); k-- > 0; ) {
            const Entity e = lead->dense[k];
            ++m_reg->m_stats.viewEntitiesTested;
            if ((m_reg->template Has<Cs>(e) && ...))
                func(e, m_reg->template Get<Cs>(e)...);
        }
    }

    Registry* m_reg;
};

// A standalone structural command buffer (Phase 9). A thin, explicit wrapper over the Registry's
// deferred operations for call sites that prefer an object they hand around and flush. Entities are
// created immediately (Create is always safe); component and destroy operations are deferred.
class CommandBuffer {
public:
    explicit CommandBuffer(Registry& reg) : m_reg(&reg) {}
    Entity Create() { return m_reg->Create(); }
    void   Destroy(Entity e) { m_reg->DestroyDeferred(e); }
    template <class T> void Add(Entity e, T value = T{}) { m_reg->AddDeferred<T>(e, std::move(value)); }
    template <class T> void Remove(Entity e) { m_reg->RemoveDeferred<T>(e); }
    void Flush() { m_reg->FlushStructuralCommands(); }
private:
    Registry* m_reg;
};

} // namespace ecs
} // namespace engine
