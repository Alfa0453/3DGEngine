#pragma once

// ECS Pass 5 -- ECS profiler. A read-only snapshot of Registry health/throughput for an editor
// overlay or a log line. Pure aggregation over the Pass-1 stats surface + the Pass-5 IPool metrics;
// it never mutates the Registry and holds no entity/component references. GL-free, header-only.

#include "engine/ecs/Registry.h"

#include <cstdint>
#include <string>
#include <vector>
#include <sstream>

namespace engine {
namespace ecs {

struct PoolMetrics {
    std::string   typeName;
    std::size_t   entityCount = 0;
    std::size_t   capacity    = 0;
    std::size_t   memoryBytes = 0;
    std::uint64_t revision    = 0;
    std::size_t   added       = 0;   // change records this epoch
    std::size_t   updated     = 0;
    std::size_t   removed     = 0;
};

struct EcsProfile {
    // Entities.
    std::size_t entitiesAlive     = 0;
    std::size_t entitiesCapacity  = 0;   // highest index ever used + 1
    std::size_t freeSlots         = 0;   // recycled indices ready to reuse
    std::size_t pendingDestroy    = 0;   // queued deferred destroys
    std::size_t deferredOps       = 0;   // queued deferred add/remove
    // Structural (cumulative counters since last ResetStats).
    std::uint64_t entityCreates = 0, entityDestroys = 0;
    std::uint64_t componentsAdded = 0, componentsUpdated = 0, componentsRemoved = 0;
    std::uint64_t structuralQueued = 0, structuralApplied = 0;
    // Views.
    std::uint64_t viewInvocations = 0, viewEntitiesTested = 0, viewMatches = 0;
    // Components.
    std::size_t poolCount = 0;
    std::size_t totalComponentMemory = 0;
    std::vector<PoolMetrics> pools;
};

inline EcsProfile CaptureEcsProfile(const Registry& reg) {
    EcsProfile p;
    p.entitiesAlive    = reg.AliveCount();
    p.entitiesCapacity = reg.EntityCapacity();
    p.freeSlots        = reg.FreeSlotCount();
    p.pendingDestroy   = reg.PendingDestroyCount();
    p.deferredOps      = reg.DeferredOpCount();
    p.poolCount        = reg.PoolCount();

    const RegistryStats& s = reg.Stats();
    p.entityCreates = s.entityCreates; p.entityDestroys = s.entityDestroys;
    p.componentsAdded = s.componentsAdded; p.componentsUpdated = s.componentsUpdated;
    p.componentsRemoved = s.componentsRemoved;
    p.structuralQueued = s.structuralCommandsQueued; p.structuralApplied = s.structuralCommandsApplied;
    p.viewInvocations = s.viewInvocations; p.viewEntitiesTested = s.viewEntitiesTested;
    p.viewMatches = s.viewMatches;

    reg.ForEachPool([&](const IPool& pool) {
        PoolMetrics m;
        m.typeName    = pool.TypeName();
        m.entityCount = pool.Count();
        m.capacity    = pool.Capacity();
        m.memoryBytes = pool.MemoryBytes();
        m.revision    = pool.PoolRevisionValue();
        m.added       = pool.AddedCount();
        m.updated     = pool.UpdatedCount();
        m.removed     = pool.RemovedCount();
        p.totalComponentMemory += m.memoryBytes;
        p.pools.push_back(std::move(m));
    });
    return p;
}

// Compact multi-line summary for a log/console. (Editor overlay reads the struct fields directly.)
inline std::string FormatEcsProfile(const EcsProfile& p) {
    std::ostringstream os;
    os << "ECS: alive=" << p.entitiesAlive << " cap=" << p.entitiesCapacity
       << " free=" << p.freeSlots << " pendingDestroy=" << p.pendingDestroy
       << " deferredOps=" << p.deferredOps << "\n";
    os << "  structural: created=" << p.entityCreates << " destroyed=" << p.entityDestroys
       << " added=" << p.componentsAdded << " updated=" << p.componentsUpdated
       << " removed=" << p.componentsRemoved
       << " queued=" << p.structuralQueued << " applied=" << p.structuralApplied << "\n";
    os << "  views: calls=" << p.viewInvocations << " tested=" << p.viewEntitiesTested
       << " matches=" << p.viewMatches << "\n";
    os << "  pools=" << p.poolCount << " componentMem=" << p.totalComponentMemory << " bytes\n";
    for (const PoolMetrics& m : p.pools) {
        os << "    " << m.typeName << ": n=" << m.entityCount << " cap=" << m.capacity
           << " mem=" << m.memoryBytes << " rev=" << m.revision
           << " (+"<< m.added << " ~" << m.updated << " -" << m.removed << ")\n";
    }
    return os.str();
}

} // namespace ecs
} // namespace engine
