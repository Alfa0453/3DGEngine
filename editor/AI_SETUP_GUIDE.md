# Setting up AI in the 3DGEditor

A practical guide to giving a scene object a brain — from a one-minute patrolling
guard to a fully authored behaviour tree with custom C++ logic and live debugging.

There are two ways to drive an agent, and you can mix them:

1. **The built-in NavAgent brain** — a ready-made patrol / chase / search controller.
   Tick a box, drop some waypoints, pick a target, press Play. No graph, no code.
2. **A behaviour tree** — a visual node graph you author in the Behavior Graph panel,
   optionally extended with your own C++ nodes and a shared blackboard. This runs *in
   place of* the built-in brain when you assign a `.btgraph` asset to the agent.

Start with option 1 to get something moving, then graduate to option 2 when you want
custom behaviour.

---

## 1. Quick start — a patrolling guard that chases (no graph, no code)

1. **Add an object** to act as the agent (a cube is fine) and give it a name in the
   Hierarchy.
2. Select it and open the **Inspector**. Find the **AI Agent** section and tick
   **Nav Agent**.
3. Tune the movement fields:
   - **Speed** — how fast it moves.
   - **Max Force** — steering responsiveness (turn sharpness).
   - **Reach Radius** — how close counts as "arrived" at a waypoint.
   - **Repath (s)** — how often it re-plans while chasing.
4. **Author a patrol loop.** Move the object to the first waypoint position and click
   **Add Waypoint (object pos)**. Move it again, add another, and so on. The waypoints
   form a loop it walks in order. **Clear Waypoints** starts over. In the viewport the
   patrol path is drawn as green markers + connectors while the agent is selected.
5. **Give it something to chase (optional).** Set **Chase Target** to another object
   (e.g. your player). Then set:
   - **Vision Range** — how far it can see.
   - **Vision Half-Angle** — half the field-of-view cone (45 = a 90° cone).
   The vision cone is drawn in the viewport when a target is set.
6. Press **Play**. The agent patrols; when the target enters its cone **and** has
   line of sight, it chases; when it loses sight it searches the last-known spot, then
   returns to patrolling.

**Factions (auto-targeting).** Instead of hand-picking one chase target, give agents a
**Team** id and tick **Auto-target nearest hostile** (both in the AI Agent section).
Each frame the agent acquires the nearest living agent on a *different non-zero team*
as its target — so two enemy factions fight without you wiring targets. Team **0** is
neutral (never targets, never auto-targeted). Explicit Chase Target still works for
things without a team, like the player.

**Crowds.** When several agents are active they automatically push apart in the XZ
plane, so a group chasing the same target spreads out instead of stacking on one spot.
This is on by default and needs no setup.

**Notes**
- Keep the agent's own collider small or absent — a big collider can block its own
  line-of-sight ray. The target needs a collider to be "seen".
- The agent drives its Transform directly, so don't also give it a dynamic rigid body,
  or physics will fight the AI.

---

## 2. Navigation — grid vs navmesh

Chasing and searching use pathfinding over your **static** colliders. Two nav sources
are available; pick one in the **Scene Debug** panel:

- **AI: Use Navmesh** off (default) → agents path on a **grid** baked from static
  box/sphere/capsule colliders.
- **AI: Use Navmesh** on → agents path on a **navmesh** (funnel-smoothed, corner-
  cutting-free) baked from the same colliders. Smoother routes.

The nav source is baked when you press Play (it re-reads the scene each time). Turn on
**AI Debug** (same panel) to see the obstacle grid (red cells) or the walkable navmesh
polygons (blue outlines) during Play.

---

## 3. Behaviour trees — the Behavior Graph panel

For anything beyond patrol/chase, author a behaviour tree.

### Opening the panel
Press **F11**, or **View → Behavior Graph**.

### Building a tree
- **Add a node:** drag from a node's bottom **output pin** into empty canvas space and
  a create menu appears (Composite / Task / Condition / Decorator / Subtree). The new
  node is created and linked as a child. You can also **right-click empty space** to
  add a loose node, or use the **Add Node** dropdown in the toolbar.
- **Link nodes:** drag from a parent's output pin onto another node.
- **Set the root:** select a node and click **Set as Root** in the inspector. The root
  is outlined in gold. A tree with no valid root shows "no valid root" in the toolbar.
- **Move / pan:** drag a node to move it; drag empty canvas to pan.
- **Edit a node:** select it; the inspector shows its parameter, key/script picker
  (where relevant), its children, and its attachments.
- **Delete:** select and click **Delete Node**.

### How composites decide
- **Sequence** runs its children in order and succeeds only if **all** succeed; it
  stops at the first that fails or is still running.
- **Selector** runs its children in order and succeeds at the **first** that doesn't
  fail — so put high-priority branches first (e.g. "chase if you see the player" above
  "patrol"). The tree re-evaluates from the top every tick, so a higher branch
  automatically **preempts** a lower one the moment its condition becomes true.

### Saving and assigning
1. Give the graph a name in the toolbar and click **Save** — it writes a `.btgraph`
   file to your project's asset folder.
2. Select your agent object, and in the **AI Agent** section drag the `.btgraph` from
   the Content browser onto **Brain (drop .btgraph)** (or use Clear Brain to remove it).
3. Press Play. When an agent has a brain asset, the tree runs instead of the built-in
   patrol/chase brain. Speed, waypoints, target and vision from the NavAgent fields are
   still fed to the tree as context.

---

## 4. Built-in node reference

Everything below is available with **no code**.

**Tasks** (leaves that do something and report Success / Failure / Running)
- **Chase** — path to the target, re-planning; Success when reached.
- **Patrol** — follow the patrol loop forever.
- **Move To Target** — path to the target once; Success when reached.
- **Flee** — steer directly away from the target.
- **Wander** — steering wander.
- **Wait** — idle for N seconds, then Success.
- **Idle** — stop moving.
- **Attack** — deal N damage to the chase target if it's within reach radius (needs a
  target with a Health component). Success on a hit, Failure otherwise.
- **Set Bool / Set Float** — write a value to a blackboard key.

**Conditions** (Success/Failure gates)
- **See Target?** — is the target currently visible.
- **Target Within?** — is the target within N metres.
- **Check Bool?** — blackboard key equals a value.
- **Float >= ? / Float < ?** — compare a blackboard float against a threshold.
- **Health Below?** — self's HP fraction is below N (e.g. flee when < 0.3).
- **Target Dead?** — the chase target's Health is dead.

**Decorators** (attach to a node via **+ Decorator** in the inspector; they gate or
modify it)
- **Inverter** — flip Success/Failure.
- **Succeeder** — force Success once the child finishes.
- **Repeat** — repeat the child N times (or forever).
- **Cooldown** — block the node for N seconds after it succeeds.
- **Time Limit** — fail the node if it runs longer than N seconds.
- **Random Chance** — enter the node with probability 0..1.
- **See Target? / Target Within? / Check Bool? / Float >= ? / Float < ?** — the
  conditions above, used as entry gates.

**Services** (attach via **+ Service**; run in the background while the branch is
active)
- **Repath** — re-plan the path toward the target every N seconds.

Attached decorators render as amber bars on the node; services as blue bars.

---

## 5. The blackboard — sharing data

The blackboard is a named, typed value store every node and script on one agent
shares. It's how nodes coordinate: one task writes a value, another node reads it.

**Define keys** in the **Blackboard** section at the top of the panel: click
**+ Add Key**, name it (no spaces), pick a type (Bool / Int / Float / Vec3 / String),
and set an initial value. These seed the agent's blackboard when Play starts.

**Use keys with no code** via the blackboard nodes: a **Set Bool/Float** task writes a
key; a **Check Bool? / Float >= ? / Float < ?** condition (as a node or a decorator)
branches on one. The key is chosen from a dropdown of what you defined.

Example: define `alerted` (Bool). On the branch that spots the enemy, add a **Set
Bool** task → `alerted = 1`. On another branch, attach a **Check Bool?** decorator →
`alerted == 1` to gate it.

---

## 6. Custom C++ nodes — writing your own tasks / decorators / services

When the built-ins aren't enough, write native C++ nodes. They live in
`editor/btscripts/` (see `btscripts/README.md`).

1. **Copy a template** from `editor/btscripts/templates/` (`TaskTemplate.h`,
   `DecoratorTemplate.h`, or `ServiceTemplate.h`) into `editor/btscripts/`, and rename
   the file and the class.
2. **Write your logic** — override `Tick()` (task/service) or `Check()` (decorator).
   You have the full blackboard context `c`: the steering body (`c.agent`), target
   (`c.targetPos`, `c.seesTarget`), nav (`c.grid`/`c.mesh`, `c.Plan(...)`), the
   blackboard (`c.blackboard`), and ECS access (`c.registry`, `c.self`,
   `c.targetEntity`). Set `c.steer` to move; set `c.facing` to aim.
3. **Register it** in `editor/src/GameBtScripts.cpp`: add `#include "YourFile.h"` and
   one line `r.Register("YourName", []{ return std::make_unique<YourClass>(); });`.
4. **Rebuild.** Your class now appears in the **Script Class** dropdown of a
   **Script Task** node, or a **Script Decorator** / **Script Service** attachment.

Three examples ship (`StrafeTarget`, `FaceTarget`, `NearTarget` in the engine; plus
`FleeTarget` and `NotAtTarget` in `btscripts/`) so the pickers are never empty.

---

## 7. Reusing behaviours — subtrees

Build a small behaviour once and reuse it. Add a **Subtree** node, and in the inspector
point **Subtree .btgraph** at another saved graph's path. That graph runs in place of
the node. Compose big behaviours from small ones (e.g. a "combat" subtree used by
several agents). Self-references are depth-capped, so an accidental loop fails safely.

---

## 8. Debugging — watch the tree think

With the Behavior Graph panel open, press **Play**:

- **Node highlighting** — each node's border lights up live: **yellow** = running,
  **green** = succeeded this tick, **red** = failed. You can watch which branch is
  active frame by frame. The toolbar shows which agent is being debugged.
- **Live blackboard** — expand the **Blackboard** section to see every key's current
  value updating in real time.

For this to line up, keep the panel showing the same graph the agent is running (the
normal author → Save → assign → Play → watch flow). The debugger follows the first
graph-driven agent.

Also useful: the **AI Debug** toggle (Scene Debug panel) draws the nav source, each
agent's live path, a state-coloured marker, and a "sees target" indicator in the
viewport.

---

## 9. Troubleshooting

- **Agent doesn't move / falls through the floor** — it probably has a dynamic rigid
  body fighting the AI. Remove it; AI agents are kinematic (they set their Transform).
- **Agent never sees the target** — check the vision cone range/angle, that the target
  has a collider, and that the agent's own collider isn't blocking its line of sight
  (keep it small). Confirm the Chase Target is set.
- **Chase goes nowhere** — the nav source may not cover the area. Make sure there are
  static colliders (and a ground plane) so a grid/navmesh can bake, and that the
  target is reachable.
- **A `.btgraph` won't load** — check the path assigned to the agent's Brain; the log
  reports load failures by path.
- **A Script node does nothing** — confirm the class is registered in
  `GameBtScripts.cpp` and you rebuilt; unregistered names fail safely (the node just
  fails).
- **"no valid root"** — select a node and click **Set as Root**.

---

## 10. Where things live

| Concern | Location |
|--------|----------|
| Behaviour-tree assets | `<project>/assets/*.btgraph` (saved from the panel) |
| Your custom BT scripts | `editor/btscripts/` (+ register in `editor/src/GameBtScripts.cpp`) |
| Script templates | `editor/btscripts/templates/` |
| Engine AI runtime | `engine/include/engine/ai/`, `engine/src/ai/` |
| Roadmap / feature log | `editor/AI_ROADMAP.md` |
