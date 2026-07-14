# Editor AI integration — implementation milestone

A plan to surface the engine's AI subsystem in the editor. The engine already ships
a complete, headless-tested AI toolkit; **the editor currently exposes none of it.**
This roadmap turns that toolkit into something you can author on a scene object and
watch run in Play mode.

Status: **planning** (nothing here is built yet).

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
