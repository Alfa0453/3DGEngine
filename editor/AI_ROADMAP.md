# Editor AI integration — implementation milestone

A plan to surface the engine's AI subsystem in the editor. The engine already ships
a complete, headless-tested AI toolkit; **the editor currently exposes none of it.**
This roadmap turns that toolkit into something you can author on a scene object and
watch run in Play mode.

Status: **M1–M7 implemented**. The editor now exposes the whole engine AI toolkit:
authorable patrol/chase agents, perception, nav (grid + navmesh), debug overlays,
and a data-driven behaviour-tree node editor.

- **M7 (done):** a visual behaviour-tree authoring panel + a data-driven runtime.
  - *Engine runtime.* New `engine/ai/BehaviorGraph.{h,cpp}`: an `AgentContext`
    blackboard, a fixed serializable node vocabulary (`BtNodeType`: Sequence/Selector/
    Inverter/Succeeder/Repeat/SeesTarget/TargetWithin/Chase/Patrol/MoveToTarget/Wait/
    Idle), a `BehaviorGraph` (node list + root), and `BuildBehaviorTree()` that binds
    each data node to a built-in over the existing `BehaviorTree<AgentContext>`. Plus
    `Save/LoadBehaviorGraph` (`.btgraph` text files). Also fixed a latent
    `Selector::Tick` fall-off-the-end bug in `BehaviorTree.h`.
  - *Editor panel.* New `BehaviorGraphPanel` (`editor/…/BehaviorGraphPanel.{h,cpp}`):
    a hand-rolled ImGui node canvas (draggable nodes, bezier links, drag-from-pin to
    wire children, per-node param inspector, pan, Save/Load) — no third-party node-
    editor dependency. Registered as the `BehaviorGraph` panel (F11 / View menu).
  - *Authoring + play.* `EditorScene::Object` gained `navAgentBrainAsset` (scene v40);
    the inspector's AI Agent section takes a `.btgraph` via drag-drop. At Play,
    `BuildPlayAgents` loads the graph and `UpdateAI` ticks a `BehaviorTree<AgentContext>`
    (a `PlayAgent::useGraph` branch) instead of the built-in `AiAgent`; the debug
    overlay reads the graph agent's blackboard.

- **M7 attachments (done):** nodes now carry Unreal-style **attached decorators**
  (gates/result-modifiers: See-Target, Target-Within, Inverter, Succeeder, Repeat) and
  **services** (background ticks: Repath). They render as bars on the node, are added
  from the inspector, serialize in `.btgraph` v2 (v1 still loads), and compile into the
  runtime by wrapping the node (decorators outermost, services inside, node innermost).
- **Pin-drop creation (done):** dragging a node's output pin onto empty space (or
  right-clicking the canvas) opens a categorized create menu (Composite / Task /
  Condition / Decorator) and auto-links the new node.

- **M7 scripting (done):** author your own tasks/decorators/services in C++.
  `engine/ai/BtScript.{h,cpp}` adds a `BtScript` base (`OnEnter` / `Tick`->status /
  `Check`->bool / `OnExit`) + a `BtScriptRegistry` (register by name). New node roles
  `ScriptTask` (leaf), `ScriptDecorator` (attachment gate), `ScriptService` (attachment
  tick) bind to a registered class by name — picked from a combo in the inspector.
  `AgentContext` now also carries `registry` / `self` / `targetEntity`, so a script can
  reach any ECS component, not just the steering blackboard. Graphs serialize the
  script names in `.btgraph` v3 (v1/v2 still load). `RegisterExampleBtScripts()`
  ships StrafeTarget (task), FaceTarget (service), NearTarget (decorator) as templates.

- **M7 blackboard (done):** `engine/ai/Blackboard.h` — a typed named key-value store
  (Bool/Int/Float/Vec3/String/Entity, `SetX`/`GetX(key,default)`/`HasX`) held on
  `AgentContext` as `c.blackboard`, shared by every node and script for the play
  session. Graphs author initial keys/values (`BehaviorGraph::blackboard`, edited in
  the panel's "Blackboard" section, serialized in `.btgraph` v4) which seed the live
  blackboard at Play via `SeedBlackboard()`. Scripts read/write it to coordinate.

- **M7 blackboard nodes (done):** no-code blackboard access — `Set Bool` / `Set Float`
  tasks and `Check Bool?` / `Compare Float?` conditions (usable standalone or as
  decorator attachments), with the key chosen from the blackboard schema in a dropdown
  and the value/threshold as the node param. Serialized in `.btgraph` v5. Lets you
  branch on blackboard state without writing C++.

- **M7 richer library (done):** added tasks Flee, Wander; decorators Cooldown (block
  after success), Time Limit (fail if it runs too long), Random Chance (probabilistic
  gate); and a Float-below condition. No new fields/format bump (reuse param/key).

- **M7 subtrees (done):** a `Subtree` node runs another `.btgraph` in place. Its asset
  path is stored in the node's `script` field (no format change). `BuildBehaviorTree`
  takes an optional `SubtreeResolver` callback; the editor supplies one that loads +
  caches referenced graphs (`m_playBtGraphCache`), and the recursive builder switches
  into the sub-graph. Depth-capped to break reference cycles.

- **M7 live debugger (done):** each top-level node is wrapped in a `ProbeNode` that
  records its per-tick status into `AgentContext::nodeStatus`; the host clears that
  buffer each frame before ticking. During Play the panel colours node borders
  (yellow running / green success / red failure) for the first graph agent and shows
  a live blackboard watch (`Blackboard::Snapshot()`). Node indices line up as long as
  the panel is showing the graph that agent is running.

- **M7 observer aborts (done):** `Selector` and `Sequence` now track the child left
  Running last tick and call `Reset()` on it when a different branch takes over, so a
  preempted subtree starts clean (timers/latches/path scratch cleared) instead of
  carrying stale state. Combined with the tree's existing reactivity (higher-priority
  branches preempt, conditional decorators abort a running branch when their condition
  flips), this gives full UE-style abort behaviour. Limitation: a preempted script
  task's `OnExit` doesn't fire (context-free `Reset`); its state still resets. A
  `Reset(Ctx&)` upgrade would close that if needed.

The four requested directions (richer library, subtrees, live debugger, observer
aborts) are all implemented. The AI/BT system is feature-complete for authoring,
extension, debugging, and reactive behaviour.

- **M6 (done):** both halves now shipped.
  - *Navmesh agent (engine change).* `ai::AiAgent` gained a second
    `Update(dt, target, seesTarget, const NavMesh&)` overload. Its brain was
    refactored into one private `Step(dt, target, seesTarget, Planner)` shared by
    the `NavGrid` and `NavMesh` overloads (the `NavGrid` overload plans with
    `AStar::FindPathWorld`, the `NavMesh` overload with `NavMesh::FindPath`), so both
    stay behaviourally identical. Non-breaking: the old grid `Update` is unchanged.
  - *Editor bake + toggle.* `EditorApp::BakePlayNavMesh` rasterizes the same static
    box/sphere/capsule colliders into `NavObstacle`s and calls
    `NavMeshBuilder::Build`. A **"AI: Use Navmesh"** checkbox (Scene Debug panel,
    `m_useNavMesh`) routes `UpdateAI` and the bake through the navmesh path; off, it
    stays on the grid. Applied on the next Play.
  - *Overlay.* `EditorViewport::DrawNavMeshOverlay` outlines the walkable polygons
    (blue), shown in Play when navmesh mode is on; the red grid overlay shows when
    it's off. `EditorViewport::DrawNavGridOverlay` (blocked cells, capped 4000) still
    covers the grid path.
- **"Show AI debug" + "Use Navmesh" toggles (done):** `m_showAiDebug` and
  `m_useNavMesh` are now `Checkbox`es in the dockspace Scene Debug panel (via
  `DockspaceContext::showAiDebug` / `useNavMesh`), matching the physics-guide toggle.

- **M5 (done):** `EditorViewport::DrawAiAgentDebugGuides` — in Play mode each agent
  gets a floating state-coloured marker (green patrol / red chase / amber search,
  brighter when it sees its target) and its live A* path drawn as segments + a goal
  marker. Fed from `m_playAgents` (`GetState`/`SeesTarget`/`Path`), gated by
  `m_showAiDebug` (default on).

- **M4 (done):** `EditorViewport::DrawNavAgentGuides` draws the selected agent's
  patrol path (markers + looped segments) and, when it has a chase target, its vision
  cone boundary rays — using the existing guide shader + cube mesh, called from the
  edit-mode render. Cone direction comes from the object's authored rotation.

- **M1 (done):** `NavAgent` component on `EditorScene::Object` (enabled, patrol points,
  speed, force, reach, repath), serialized (scene v37), inspector "AI Agent" section,
  and a play-loop `UpdateAI` that drives patrol via `ai::AiAgent` (writes Position/
  Facing to the Transform).
- **M2 (done):** chase target + vision cone (scene v38); `CanSee` line-of-sight in
  `UpdateAI`; `BakePlayNavGrid` rasterizes static box/sphere colliders into a
  `NavGrid` so chase/search A* works. Note: agents self-occlude if they carry a large
  collider — keep the agent's collider small (the eye is offset up+forward to help).

---

## What the engine already provides (`engine/ai/`)

- `NavGrid` + `AStar` — tile-based A* pathfinding (grid cells → world waypoints).
- `NavMesh` + `NavMeshBuilder` — convex-polygon navmesh with polygon A* + funnel;
  `NavMeshBuilder::Build(NavBuildConfig, obstacles)` voxelizes a bounds and
  region-merges walkable cells into a mesh.
- `Steering` — Reynolds behaviors (seek/arrive/flee/pursue/wander/avoid/follow-path).
- `Perception` — `VisionCone` + `CanSee(eye, forward, cone, target, targetEntity,
  world, registry)` (cone test + line-of-sight raycast through `PhysicsWorld`).
- `StateMachine`, `BehaviorTree` — generic decision structures.
- `AiAgent` — a ready-made patrol/chase/search brain: `Update(dt, targetPos,
  seesTarget, NavGrid)`, exposing `Position()`, `Facing()`, `GetState()`, `Path()`.

Key API note: **`AiAgent` consumes a `NavGrid`**, while `NavMeshBuilder` produces a
`NavMesh`. So the fastest route to a working agent uses a baked **NavGrid**; the
**navmesh** path (smoother, funnel-based) is a later, more advanced milestone that
needs a navmesh-consuming agent variant.

## Design decisions

- **AI as a component.** Follow the existing editor pattern (physics / animation /
  script are per-object components on `EditorScene::Object`). Add a **`NavAgent`**
  component authored in the inspector and serialized with the scene.
- **A play-loop AI system.** Add an `UpdateAI(...)` step to the Play fixed loop,
  alongside `UpdateScripts` / `UpdateAnimations` / physics. Without it, authored
  agents do nothing (the same gap animation had before it was fixed).
- **One nav source per scene.** Bake a `NavGrid` (M1) or `NavMesh` (M6) from the
  scene's static colliders; store it with the scene / rebuild on Play.
- **Perception through physics.** Use `ai::CanSee` against a designated target so
  chase/search is driven by real line-of-sight.
- **Reuse existing viewport tooling.** Waypoints, vision cones, paths and the nav
  overlay draw through `EditorViewport`'s guide shader; waypoint editing reuses the
  gizmo + picking already in the editor.

---

## M1 — NavAgent component + Play-mode AI, patrol only

The foundation: drop an agent on an object, bake a grid, hit Play, watch it patrol.

- **Component.** Add a `NavAgent` struct to `EditorScene::Object` (enabled flag,
  patrol waypoints `vector<vec3>`, `maxSpeed`, `maxForce`, `reachRadius`,
  `repathInterval`). Serialize it in `EditorScene.cpp` (bump the scene version) and
  in `RuntimeSceneExporter.cpp` / `RuntimeSceneLoader.cpp`.
- **Runtime state.** In the play registry, attach an `ai::AiAgent` (seeded from the
  component) per agent entity — either a new ECS component wrapping `AiAgent`, or a
  side table keyed by entity in `EditorApp`.
- **Nav source.** Add `BakeNavGrid()` — gather static (non-trigger) box/sphere
  colliders into `ai::NavObstacle`s, choose bounds + cell size, and build a
  `NavGrid` (mark obstacle cells). Store it on the play session.
- **Play update.** New `UpdateAI(dt)` in the play fixed loop: for each agent, call
  `AiAgent.Update(dt, target, seesTarget=false, grid)` (patrol only for M1), then
  copy `Position()` into the entity `Transform`, and orient from `Facing()`.
- **Done when:** an object with patrol waypoints walks its loop in Play.

## M2 — Perception + targets (chase / search)

- **Target selection.** A simple way to mark what an agent pursues: a per-agent
  "target" object reference, or a **faction/team tag** component so agents chase the
  nearest entity of a hostile team (and a "player" tag for the play controller).
- **Perception.** Each step compute `seesTarget = ai::CanSee(eye, facing, cone,
  targetPos, targetEntity, m_playPhysics, *m_playRegistry)` and feed it to
  `AiAgent.Update`. Expose `VisionCone` (range, half-angle) on the component.
- **Done when:** an agent patrols, spots the target through line-of-sight, chases,
  then searches the last-known position when it loses sight.

## M3 — Inspector panel

- Add an **"AI Agent"** section to the object inspector (`EditorDockspace.cpp`,
  beside Physics/Animation/Script): enable toggle, speeds, vision range/angle,
  chase/search radii, repath interval, and a target/faction picker. Undo-aware via
  the existing `SetSelected*` + transform-edit snapshot pattern.

## M4 — Viewport authoring

- **Patrol path editing.** With an agent selected, click in the viewport to append a
  waypoint (reuse `SceneDropPosition` / ray-to-ground), drag waypoints with the
  gizmo, and draw the path as a polyline + markers.
- **Vision-cone gizmo.** Draw the selected agent's cone (range + half-angle) and a
  LOS ray to its target through `EditorViewport`'s guide shader.
- **Path preview (edit mode).** Pick agent + target, run A* on the baked grid, and
  draw the resulting corridor so pathfinding is debuggable before Play.

## M5 — Play-mode AI debug overlay

- Draw each agent's live state as colour (green Patrol / red Chase / yellow Search),
  its current `Path()`, a last-known-target marker, and a "sees target" indicator —
  an in-editor version of the standalone `aidemo`. Gated behind a "Show AI debug"
  toggle like the physics-event guides.

## M6 — Navmesh baking (advanced pathfinding)

- **Bake.** "Bake Navmesh" button → `NavMeshBuilder::Build` over the static-collider
  obstacles; store the `NavMesh` with the scene.
- **Overlay.** Draw the walkable polygons in the viewport (like `gennavdemo`).
- **Navmesh agent.** A navmesh-consuming agent (polygon A* + funnel) for smoother
  paths than the grid — either extend `AiAgent` to accept a `NavMesh`, or add a
  parallel controller. This is the step that unlocks corner-cutting-free routes.

## M7 — Behavior authoring

- **Presets first.** A brain selector on the NavAgent (patrol / chase / search with
  tunable params) — cheap, covers most cases.
- **Node graph (ambitious).** A visual editor panel for `BehaviorTree` /
  `StateMachine`, saved per agent (a node-graph the way a material graph would work).
  Large; do only if data-driven behaviors become a goal.

---

## Where each piece hooks in

| Concern | File(s) |
|--------|---------|
| Component data + serialization | `EditorScene.h/.cpp` (`Object`, Save/Load, version bump) |
| Runtime export/load | `RuntimeSceneExporter.cpp`, `engine/scene/RuntimeSceneLoader.*` |
| Play build + AI update + bake | `EditorApp.cpp` (`BuildPlayRuntimePreview`, the play fixed loop, `UpdateAI`, `BakeNavGrid`/`BakeNavMesh`) |
| Inspector UI | `EditorDockspace.cpp` |
| Viewport guides / gizmo / picking | `EditorViewport.*`, existing gizmo + `SceneDropPosition` |
| Engine AI (already done) | `engine/ai/*` — no changes needed for M1–M5 |

## Verification

The engine AI logic (A*, navmesh funnel, steering, FSM, perception) is already
unit-tested headless in the engine. The editor work is verified by building
`3DGEditor` and play-testing: an agent patrols (M1), chases on sight and searches on
loss (M2), and the overlays/gizmos render (M4–M5). As with the rest of the D:-tree
work, code is written against the real headers but must be compiled locally.

## Suggested order

M1 → M2 → M3 → (M4, M5 in either order) → M6 → M7. M1–M3 give a usable, authorable
patrol/chase agent; M4–M5 make it pleasant to author and debug; M6–M7 are the
advanced pathfinding and data-driven-behavior tiers.
