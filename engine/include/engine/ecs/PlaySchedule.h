#pragma once

// ECS Pass 4 (follow-up) -- scheduler ADOPTION scaffold for the fixed-step play loop.
//
// This registers the engine's real, pure-ECS gameplay systems into a SystemScheduler in the exact
// order the play loop runs them (EditorApp::StepPlayPhysics), with honest read/write access sets
// where they've been audited. It is an ADOPTION PATH, not a rewrite: the live loop keeps calling the
// systems directly today. When you're ready, a call site can build this schedule once and
// `RunSerial(reg, step)` it in place of the direct calls -- serial mode reproduces the current order
// byte-for-byte (all systems ParallelSafe = false). Only after auditing a system for thread-safety
// and filling its access set do you flip its ParallelSafe to true.
//
// SCOPE: only the pure `(Registry&, float)` / `(Registry&)` systems are registered here. The impure
// steps -- FixedUpdateScripts, UpdateAI, the PhysicsWorld::Step, ragdoll/animation, GameMode -- take
// audio/camera/physics arguments or shared singletons and stay outside the ECS scheduler as ordered
// phases the engine drives itself (Physics is one such ordered phase; the scheduler never runs it
// concurrently). GL-free, header-only.

#include "engine/ecs/SystemScheduler.h"
#include "engine/ecs/Components.h"
#include "engine/ecs/RuntimeSystems.h"          // UpdateGameplay, UpdateRuntimeMotion (inline)
#include "engine/physics/PhysicsComponents.h"

#include "engine/gameplay/GameplaySystems.h"     // UpdateHealth, UpdateProjectilesInPlace, UpdateAbilities
#include "engine/gameplay/CombatSystem.h"        // UpdateCombat
#include "engine/gameplay/SpawnSystem.h"         // UpdateSpawnManagers
#include "engine/gameplay/DestructionSystem.h"   // UpdateDestruction
#include "engine/gameplay/InteractionSystem.h"   // UpdateInteractions
#include "engine/gameplay/PortalSystem.h"        // UpdatePortals

namespace engine {
namespace ecs {

// Register the pure-ECS Gameplay-phase systems in current-loop order. `player` is forwarded to the
// spawn system exactly as the loop passes it. Everything stays ParallelSafe = false (serial), so
// building + RunSerial(reg, step) here is behavior-identical to the current direct calls.
inline void BuildGameplaySchedule(SystemScheduler& sched, Entity player = kNull) {
    // 1) UpdateGameplay -- Mover/Rotator drive Transform (or a RigidBody's velocity when present).
    {
        SystemDescriptor s;
        s.name = "UpdateGameplay"; s.phase = SystemPhase::Gameplay;
        s.writes = Writes<Transform, Mover, Rotator, RigidBody>();
        s.reads  = Reads<Collider>();
        s.run = [](ScheduleContext& c) { UpdateGameplay(c.reg, c.dt); };
        sched.Add(std::move(s));
    }
    // 2) UpdateRuntimeMotion -- integrate LinearVelocity/AngularVelocity into Transform.
    {
        SystemDescriptor s;
        s.name = "UpdateRuntimeMotion"; s.phase = SystemPhase::Gameplay;
        s.writes = Writes<Transform>();
        s.reads  = Reads<LinearVelocity, AngularVelocity>();
        s.run = [](ScheduleContext& c) { UpdateRuntimeMotion(c.reg, c.dt); };
        sched.Add(std::move(s));
    }
    // 3..N) The remaining pure gameplay systems, in loop order. Access sets are left UNDECLARED
    //       (empty) and ParallelSafe stays false -- they run serial, exactly as today, until each is
    //       individually audited and its Reads/Writes filled in. Do not flip ParallelSafe on any of
    //       these without declaring its access set first.
    auto addSerial = [&](const char* name, std::function<void(ScheduleContext&)> run) {
        SystemDescriptor s; s.name = name; s.phase = SystemPhase::Gameplay; s.run = std::move(run);
        sched.Add(std::move(s));
    };
    addSerial("UpdateAbilities",       [](ScheduleContext& c) { UpdateAbilities(c.reg, c.dt); });
    addSerial("UpdateCombat",          [](ScheduleContext& c) { UpdateCombat(c.reg, c.dt); });
    addSerial("UpdateSpawnManagers",   [player](ScheduleContext& c) { UpdateSpawnManagers(c.reg, c.dt, player); });
    addSerial("UpdateDestruction",     [](ScheduleContext& c) { UpdateDestruction(c.reg, c.dt); });
    addSerial("UpdateInteractions",    [](ScheduleContext& c) { UpdateInteractions(c.reg, c.dt); });
    addSerial("UpdatePortals",         [](ScheduleContext& c) { UpdatePortals(c.reg, c.dt); });
    addSerial("UpdateProjectilesInPlace", [](ScheduleContext& c) { UpdateProjectilesInPlace(c.reg, c.dt); });
    addSerial("UpdateHealth",          [](ScheduleContext& c) { UpdateHealth(c.reg); });
}

} // namespace ecs
} // namespace engine
