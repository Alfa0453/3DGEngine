# Behaviour-Tree scripts

This folder is where you write the C++ logic for your custom behaviour-tree nodes —
**tasks, decorators, and services** — that show up in the Behavior Graph panel (F11).

## How it works

Each script is one header file defining a class that derives from
`engine::ai::BtScript`. You register it by name, and it appears in the node's
**Script Class** picker. Scripts are native C++ (compiled in, not hot-loaded), like
the gameplay scripts.

## Add a new script (3 steps)

1. **Copy a template** from `templates/` into this folder and rename the file + class
   (e.g. `templates/TaskTemplate.h` → `ChaseAndShoot.h`, class `ChaseAndShoot`).
2. **Write your logic** in the `Tick()` / `Check()` you overrode.
3. **Register it** in `../src/GameBtScripts.cpp`: add `#include "ChaseAndShoot.h"` at
   the top and one line inside `RegisterGameBtScripts()`:
   ```cpp
   r.Register("ChaseAndShoot", [] { return std::make_unique<ChaseAndShoot>(); });
   ```
   Then rebuild. No `CMakeLists.txt` edit is needed — headers in this folder are
   pulled in by `GameBtScripts.cpp`, which is already in the build.

## The three roles

| Role      | Override            | Returns              | Used as            |
|-----------|---------------------|----------------------|--------------------|
| Task      | `Tick(ctx, dt)`     | Success/Failure/Running | a **Script Task** node |
| Decorator | `Check(ctx)`        | `bool` (false blocks) | a **Script Decorator** attachment |
| Service   | `Tick(ctx, dt)`     | (ignored)            | a **Script Service** attachment |

`OnEnter(ctx)` / `OnExit(ctx)` are optional setup/teardown hooks on any of them.

## What you get on the blackboard (`engine::ai::AgentContext& c`)

- `c.agent` — steering body: `position`, `velocity`, `maxSpeed`, `maxForce`.
- `c.targetPos`, `c.seesTarget` — the pursued point and whether it's visible now.
- `c.grid`, `c.mesh`, `c.Plan(from, to)` — nav sources + a path query.
- `c.path`, `c.pathIndex`, `c.patrol` — path scratch and the patrol loop.
- `c.registry`, `c.self`, `c.targetEntity` — ECS access to read/write components.
- **Outputs:** set `c.steer` (an acceleration) to move the agent; set `c.facing` to aim.

Steering helpers in `engine/ai/Steering.h`: `Seek`, `Flee`, `Arrive`, `Pursue`,
`Evade`, `Wander`, `FollowPath`.

## Blackboard — sharing data between nodes

`c.blackboard` is a named, typed key-value store shared by every node and script on
one agent. Use it to coordinate: one task writes a value, a decorator or another task
reads it. It persists for the whole play session.

```cpp
// write (in a task)
c.blackboard.SetBool("alerted", true);
c.blackboard.SetVec3("lastSeen", c.targetPos);

// read (in a decorator gate)
bool Check(engine::ai::AgentContext& c) override {
    return c.blackboard.GetBool("alerted", /*default*/ false);
}
```

Types: `Bool`, `Int`, `Float`, `Vec3`, `String`, `Entity` — each has `SetX(key, v)`,
`GetX(key, default)` and `HasX(key)`. You can pre-define initial keys/values in the
**Blackboard** section at the top of the Behavior Graph panel; those seed `c.blackboard`
when Play starts. Keys created only at runtime work too — just `SetX` them.

See `FleeTarget.h` and `NotAtTarget.h` in this folder for working examples.
