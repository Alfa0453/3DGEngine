#pragma once

// ECS Pass 4 -- System scheduler, phases & safe concurrency.
//
// A deterministic, SERIAL-BY-DEFAULT scheduler layered on top of the Pass-1 Registry. It does NOT
// make the Registry globally thread-safe and it does NOT run existing systems in parallel just
// because they compile: every system starts ParallelSafe = false and stays serial until its access
// metadata (and a human audit) proves otherwise. In serial mode the scheduler runs systems in phase
// order, and within a phase in registration order -- i.e. byte-for-byte the current engine order.
//
// Phase order is DERIVED from the engine's real fixed-step loop (EditorApp::StepPlayPhysics), not
// invented:
//   PlayerController -> FixedUpdateScripts -> Gameplay -> RuntimeMotion -> AI -> gameplay systems
//   -> RagdollBeforePhysics -> Animation(+FootIK) -> Buoyancy -> PhysicsWorld::Step
//   -> RagdollAfterPhysics -> PhysicsEvents -> GameMode::Update
// Note in particular: skinned Animation runs BEFORE the physics Step here (it sets ragdoll targets),
// so Animation sits in a pre-physics slot -- the spec's "likely" ordering is overridden by the real
// order, as instructed.
//
// Concurrency model (opt-in):
//   * Systems declare Reads<T...> / Writes<T...>. Two systems conflict if their write sets overlap,
//     or one reads what the other writes. Conflict-free + both ParallelSafe => may overlap.
//   * Component DATA writes happen during a system's run (immediate, like today). Only STRUCTURAL
//     changes (create/destroy/add/remove) are deferred: a parallel system records them into its own
//     CommandRecorder and never touches the Registry structurally while running.
//   * At each phase barrier the recorders are merged into the Registry in ascending REGISTRATION
//     order (never worker-completion order) and flushed -- so entity ids, destruction order and
//     event order are deterministic and identical for 1 worker or N workers.
//   * The job runner is pluggable and defaults to serial. The threaded runner spawns transient
//     join-based threads (the same pattern the lighting bake already uses) -- it does NOT stand up a
//     second persistent pool, and it never competes with Physics for shared state (Physics is just
//     an ordered phase the scheduler runs alone).
//
// Header-only, GL-free.

#include "engine/ecs/Registry.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace engine {
namespace ecs {

// ---- phases (ascending run order; derived from the real fixed-step loop) ---------------------
enum class SystemPhase : std::uint8_t {
    StructuralFlush = 0, // apply any structural commands queued last frame
    PreUpdate,           // player controller / input sampling
    Gameplay,            // fixed-update scripts, gameplay, runtime motion, abilities, combat, ...
    AI,                  // behavior trees, navigation
    PrePhysics,          // ragdoll pre-physics, skinned animation + foot IK, buoyancy
    Physics,             // PhysicsWorld::Step (always run alone; never parallelized by the scheduler)
    PhysicsWriteback,    // ragdoll after-physics, transform write-back from the solver
    PostPhysics,         // physics events, GameMode update
    Animation,           // reserved: late/blend animation (empty by default -- real anim is PrePhysics)
    TransformFinalize,   // hierarchy/world-matrix finalize before extraction
    RenderExtraction,    // RenderScene::SyncFrom (Pass 2)
    Render,              // render submit
    Count
};

// ---- runtime component access ids (NOT persisted; scheduling only) ---------------------------
inline std::uint32_t& AccessTypeCounter() { static std::uint32_t c = 0; return c; }
template <class T> std::uint32_t AccessTypeId() { static std::uint32_t id = AccessTypeCounter()++; return id; }

// A small sorted set of component access ids with cheap intersection.
struct AccessSet {
    std::vector<std::uint32_t> ids;
    void add(std::uint32_t id) {
        auto it = std::lower_bound(ids.begin(), ids.end(), id);
        if (it == ids.end() || *it != id) ids.insert(it, id);
    }
    bool intersects(const AccessSet& o) const {
        std::size_t i = 0, j = 0;
        while (i < ids.size() && j < o.ids.size()) {
            if (ids[i] == o.ids[j]) return true;
            if (ids[i] < o.ids[j]) ++i; else ++j;
        }
        return false;
    }
};
template <class... Ts> AccessSet Reads()  { AccessSet s; (s.add(AccessTypeId<Ts>()), ...); return s; }
template <class... Ts> AccessSet Writes() { AccessSet s; (s.add(AccessTypeId<Ts>()), ...); return s; }

// ---- deferred structural recorder (per system, thread-local) ---------------------------------
// A parallel system records structural intent here instead of mutating the Registry. Create()
// returns a LOCAL handle resolved to a real Entity only at merge time, in registration order, so
// created-entity identity is deterministic regardless of which worker ran the system.
class CommandRecorder {
public:
    struct Handle { bool deferred = false; Entity value = kNull; };   // deferred => index into m_creates

    Handle Create() { Handle h; h.deferred = true; h.value = static_cast<Entity>(m_creates++); return h; }
    static Handle Of(Entity e) { return Handle{false, e}; }

    template <class T> void Add(Handle target, T value = T{}) {
        m_addOps.push_back({target, [value](Registry& r, Entity e) { if (r.Valid(e)) { if (r.Has<T>(e)) r.Get<T>(e) = value; else r.Add(e, value); } }});
    }
    template <class T> void Add(Entity target, T value = T{}) { Add<T>(Of(target), std::move(value)); }
    template <class T> void Remove(Entity target) {
        m_removeOps.push_back({Of(target), [](Registry& r, Entity e) { if (r.Valid(e) && r.Has<T>(e)) r.Remove<T>(e); }});
    }
    void Destroy(Entity target) { m_destroyOps.push_back(Of(target)); }

    bool empty() const { return m_creates == 0 && m_addOps.empty() && m_removeOps.empty() && m_destroyOps.empty(); }
    void clear() { m_creates = 0; m_addOps.clear(); m_removeOps.clear(); m_destroyOps.clear(); m_resolved.clear(); }

    // Pass 1: create entities + apply adds/removes (adds/removes first, matching Registry flush order).
    void ApplyCreatesAndMutations(Registry& reg) {
        m_resolved.resize(m_creates);
        for (std::uint32_t i = 0; i < m_creates; ++i) m_resolved[i] = reg.Create();
        for (auto& op : m_addOps)    op.fn(reg, resolve(op.target));
        for (auto& op : m_removeOps) op.fn(reg, resolve(op.target));
    }
    // Pass 2: destroys (applied after every system's creates/adds, matching "destroys last").
    void ApplyDestroys(Registry& reg) {
        for (const Handle& h : m_destroyOps) { Entity e = resolve(h); if (reg.Valid(e)) reg.Destroy(e); }
    }

private:
    struct Op { Handle target; std::function<void(Registry&, Entity)> fn; };
    Entity resolve(const Handle& h) const { return h.deferred ? (h.value < m_resolved.size() ? m_resolved[h.value] : kNull) : h.value; }
    std::uint32_t        m_creates = 0;
    std::vector<Op>      m_addOps;
    std::vector<Op>      m_removeOps;
    std::vector<Handle>  m_destroyOps;
    std::vector<Entity>  m_resolved;
};

// ---- system descriptor -----------------------------------------------------------------------
struct ScheduleContext {
    Registry&        reg;
    CommandRecorder& cmd;    // record structural changes here (deferred, merged at barrier)
    int              worker = 0;
    float            dt = 0.0f;   // per-run delta time (fixed step), for time-stepped systems
};

struct SystemDescriptor {
    std::string  name;
    SystemPhase  phase = SystemPhase::Gameplay;
    AccessSet    reads;
    AccessSet    writes;
    std::vector<std::string> runAfter;   // explicit dependencies (names)
    bool         parallelSafe = false;   // DEFAULT SERIAL -- opt in only after audit
    std::function<void(ScheduleContext&)> run;
};

// ---- job runner (pluggable; default serial; threaded = transient join-based, no standing pool) --
struct JobRunner {
    virtual ~JobRunner() = default;
    virtual void RunJobs(const std::vector<std::function<void()>>& jobs) = 0;
};
struct SerialJobRunner : JobRunner {
    void RunJobs(const std::vector<std::function<void()>>& jobs) override { for (auto& j : jobs) j(); }
};
struct ThreadJobRunner : JobRunner {
    explicit ThreadJobRunner(unsigned maxThreads) : m_max(std::max(1u, maxThreads)) {}
    void RunJobs(const std::vector<std::function<void()>>& jobs) override {
        if (jobs.size() <= 1 || m_max == 1) { for (auto& j : jobs) j(); return; }
        // Transient threads, joined here -- mirrors the existing lighting-bake fan-out. One batch at
        // a time, so at most (jobs) threads live briefly; no persistent competing pool.
        std::vector<std::thread> ts; ts.reserve(std::min<std::size_t>(jobs.size(), m_max));
        std::atomic<std::size_t> next{0};
        const unsigned n = static_cast<unsigned>(std::min<std::size_t>(jobs.size(), m_max));
        auto worker = [&] { for (;;) { std::size_t i = next.fetch_add(1); if (i >= jobs.size()) break; jobs[i](); } };
        for (unsigned t = 1; t < n; ++t) ts.emplace_back(worker);
        worker();                       // run one share on the calling thread too
        for (auto& t : ts) t.join();
    }
    unsigned m_max;
};

// ---- scheduler -------------------------------------------------------------------------------
class SystemScheduler {
public:
    void Add(SystemDescriptor d) { m_systems.push_back(std::move(d)); }
    void Clear() { m_systems.clear(); }
    std::size_t SystemCount() const { return m_systems.size(); }

    // Serial execution: exactly phase order, then registration order within a phase. Reproduces the
    // current engine behavior. This is the default and the correctness reference.
    void RunSerial(Registry& reg, float dt = 0.0f) { SerialJobRunner r; RunWith(reg, r, dt); }

    // Parallel execution with `workers` transient threads. Conflict-free + ParallelSafe systems in
    // the same phase may overlap; everything else stays serialized in registration order. Results
    // are guaranteed identical to RunSerial (see header notes).
    void RunParallel(Registry& reg, unsigned workers, float dt = 0.0f) {
        if (workers <= 1) { RunSerial(reg, dt); return; }
        ThreadJobRunner r(workers); RunWith(reg, r, dt);
    }

    void RunWith(Registry& reg, JobRunner& runner, float dt = 0.0f) {
        m_dt = dt;
        for (std::uint8_t ph = 0; ph < static_cast<std::uint8_t>(SystemPhase::Count); ++ph)
            RunPhase(reg, runner, static_cast<SystemPhase>(ph));
    }

private:
    bool Conflicts(const SystemDescriptor& a, const SystemDescriptor& b) const {
        return a.writes.intersects(b.writes)   // write-write
            || a.reads.intersects(b.writes)    // read-write
            || b.reads.intersects(a.writes);   // write-read
    }

    void RunPhase(Registry& reg, JobRunner& runner, SystemPhase phase) {
        // Gather this phase's systems in registration order (indices into m_systems).
        std::vector<std::size_t> sys;
        for (std::size_t i = 0; i < m_systems.size(); ++i)
            if (m_systems[i].phase == phase) sys.push_back(i);
        if (sys.empty()) return;

        // Deterministic batching that PRESERVES serial (registration) order for every conflicting or
        // dependent pair -- so parallel results equal serial results. Rules, applied in registration
        // order:
        //   * A system may never sit in a batch that runs before an EARLIER-registered system it
        //     conflicts with or depends on: its floor batch is max(prevConflict.batch+1).
        //   * A non-ParallelSafe system is a total barrier: it takes its own new batch after
        //     everything placed so far, and nothing later may slip before it.
        //   * A ParallelSafe system joins the earliest batch >= its floor whose members are all
        //     ParallelSafe and conflict-free with it; otherwise a fresh batch.
        struct Batch { std::vector<std::size_t> members; bool parallel = true; };
        std::vector<Batch> batches;
        std::vector<int> batchOf(sys.size(), -1);   // batch index per position k in `sys`
        std::size_t floorBatch = 0;                 // no system may be placed before this batch
        for (std::size_t k = 0; k < sys.size(); ++k) {
            const std::size_t idx = sys[k];
            const SystemDescriptor& s = m_systems[idx];
            std::size_t minBatch = floorBatch;
            for (std::size_t j = 0; j < k; ++j) {
                const SystemDescriptor& o = m_systems[sys[j]];
                if (Conflicts(s, o) || DependsOn(s, o))
                    minBatch = std::max<std::size_t>(minBatch, static_cast<std::size_t>(batchOf[j]) + 1);
            }
            std::size_t placed = batches.size();
            if (!s.parallelSafe) {
                placed = std::max<std::size_t>(minBatch, batches.size());   // new barrier batch at end
                while (batches.size() <= placed) batches.push_back({});
                batches[placed].parallel = false;
                floorBatch = placed + 1;                                    // barrier: later systems come after
            } else {
                for (std::size_t b = minBatch; b < batches.size(); ++b) {
                    if (!batches[b].parallel) continue;
                    bool ok = true;
                    for (std::size_t m : batches[b].members)
                        if (Conflicts(s, m_systems[m])) { ok = false; break; }
                    if (ok) { placed = b; break; }
                }
                if (placed == batches.size() || placed < minBatch) {
                    placed = std::max<std::size_t>(minBatch, batches.size());
                    while (batches.size() <= placed) batches.push_back({});
                }
            }
            batches[placed].members.push_back(idx);
            batchOf[k] = static_cast<int>(placed);
        }

        // One CommandRecorder per system (indexed by registration position within the phase) so the
        // structural merge is order-stable regardless of which worker executed which system.
        std::vector<CommandRecorder> recorders(sys.size());
        std::vector<std::size_t> recIndex(m_systems.size(), 0);
        for (std::size_t k = 0; k < sys.size(); ++k) recIndex[sys[k]] = k;

        // Execute batches in order; within a batch, run members concurrently.
        for (Batch& batch : batches) {
            std::vector<std::function<void()>> jobs;
            jobs.reserve(batch.members.size());
            int worker = 0;
            for (std::size_t idx : batch.members) {
                CommandRecorder& rec = recorders[recIndex[idx]];
                SystemDescriptor& s = m_systems[idx];
                const int w = worker++;
                const float dt = m_dt;
                jobs.push_back([&reg, &rec, &s, w, dt] {
                    if (s.run) { ScheduleContext ctx{reg, rec, w, dt}; s.run(ctx); }
                });
            }
            runner.RunJobs(jobs);
        }

        // Barrier: deterministic structural merge in registration order (creates+mutations, then
        // destroys), then a single flush of any Registry-queued commands.
        for (std::size_t k = 0; k < recorders.size(); ++k) recorders[k].ApplyCreatesAndMutations(reg);
        for (std::size_t k = 0; k < recorders.size(); ++k) recorders[k].ApplyDestroys(reg);
        reg.FlushStructuralCommands();
    }

    bool DependsOn(const SystemDescriptor& a, const SystemDescriptor& b) const {
        for (const std::string& dep : a.runAfter) if (dep == b.name) return true;
        return false;
    }

    std::vector<SystemDescriptor> m_systems;
    float m_dt = 0.0f;   // delta time for the current Run (threaded into ScheduleContext)
};

} // namespace ecs
} // namespace engine
