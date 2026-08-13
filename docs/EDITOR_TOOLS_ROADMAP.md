# 3DG Engine Editor Tools Roadmap

This is the implementation tracker for the next generation of editor tools.
Tools are completed one at a time. A tool is marked complete only after its editor
workflow, persistence, runtime integration where applicable, tests, and Release build
have all been verified.

Status legend:

- **NEXT** — next tool to implement
- **PLANNED** — accepted and waiting
- **IN PROGRESS** — implementation has started
- **COMPLETE** — implemented and verified
- **DEFERRED** — intentionally postponed, with a reason recorded

## Implementation Order

| # | Status | Tool | Main outcome |
|---:|:---:|---|---|
| 1 | **COMPLETE** | Mesh Painting Tool | Paints persistent vertex colors and RGBA material masks directly on engine-owned static meshes. |
| 2 | COMPLETE | Decal Placement Tool | Surface-aligned decals for dirt, cracks, impacts, signs, puddles, and scorch marks. |
| 3 | COMPLETE | Optimization Auditor | Finds expensive meshes, materials, textures, lights, particles, terrain, and missing LODs; provides actionable fixes. |
| 4 | **COMPLETE** | Ragdoll Physics Editor | Per-bone bodies, constraints, collision limits, mass, and animation/ragdoll blending. |
| 5 | **COMPLETE** | Animation Retargeting Tool | Skeleton mapping, retarget profiles, pose correction, preview, and saved retargeted clips. |
| 6 | **COMPLETE** | Ability Editor | Data-driven attacks, spells, costs, cooldowns, targeting, animations, damage, audio, and effects. |
| 7 | **COMPLETE** | Runtime Property Inspector | Selects and edits live Play-mode components without overwriting authored scene data. |
| 8 | **COMPLETE** | Asset Dependency Viewer | Graphs references between scenes, prefabs, materials, scripts, characters, shaders, and textures. |
| 9 | **COMPLETE** | Weather Editor | Rain, snow, wind, fog, lightning, clouds, wet surfaces, particles, and transitions. |
| 10 | **COMPLETE** | Procedural Building Tool | Generates walls, floors, roofs, openings, columns, and storeys from saved editable footprints. |
| 11 | **COMPLETE** | Road Generator | Saved spline roads with lanes, shoulders, markings, curbs, sidewalks, barriers, end caps, and terrain conformation. |
| 12 | **COMPLETE** | Level Instance Tool | Converts selections into reusable, linked, placeable, and streamable level sections. |
| 13 | **COMPLETE** | World Partition Tool | Cell-based large-world organization, streaming preview, memory estimates, data layers, priorities, and load distances. |
| 14 | **COMPLETE** | Geometry Editing Tool | Extrude, inset, bevel, bridge, weld, subdivision, face deletion, topology analysis, and mesh repair. |
| 15 | **COMPLETE** | Procedural Scatter Graph | Engine-owned deterministic placement graphs with filters, exclusions, weighted outputs, preview, baking, and scripts. |
| 16 | **NEXT** | Lighting Analysis Tool | Light complexity, shadow coverage, exposure, overlap, unlit-area, and overdraw views. |
| 17 | PLANNED | Biome Editor | Reusable terrain, foliage, water, weather, lighting, particles, and ambient-audio presets. |
| 18 | PLANNED | Day/Night Timeline | Authors sky, sun, moon, clouds, fog, lighting, and environmental audio over time. |
| 19 | PLANNED | Cave and Tunnel Tool | Spline-generated cave/tunnel meshes, junctions, entrances, collision, and navigation. |
| 20 | PLANNED | Fence and Wall Painter | Viewport drawing of connected fences and walls with corners, posts, gates, and snapping. |
| 21 | PLANNED | Destruction Authoring Tool | Fracture pieces, strength, debris, damaged states, sound, particles, and collision. |
| 22 | PLANNED | Interactive Door and Lift Tool | Fast setup for doors, gates, elevators, platforms, switches, locks, and access conditions. |
| 23 | PLANNED | Portal and Teleport Tool | Teleporters, destination previews, seamless doors, and level-transition portals. |
| 24 | PLANNED | Quest Editor | Objectives, conditions, rewards, state, dialogue triggers, checkpoints, and debugging. |
| 25 | PLANNED | Dialogue Editor | Branching conversations, conditions, events, voice clips, portraits, and localization keys. |
| 26 | PLANNED | Inventory and Item Editor | Weapons, armor, consumables, pickups, currencies, statistics, icons, and effects. |
| 27 | PLANNED | Combat Editor | Damage types, combos, targeting, blocking, parrying, stagger, hit reactions, and immunity windows. |
| 28 | PLANNED | Spawn Manager | Spawn volumes, weighted groups, waves, pooling, difficulty scaling, and encounter controls. |
| 29 | PLANNED | Checkpoint and Save Editor | Visual configuration of persisted player, quest, world, and streamed-level state. |
| 30 | PLANNED | Interaction Editor | Prompts, ranges, inputs, conditions, animation requirements, and interaction events. |
| 31 | PLANNED | IK Rig Editor | Foot placement, hand targets, look-at, weapon alignment, aiming, and terrain adaptation. |
| 32 | PLANNED | Animation Timeline Editor | Clip trimming, looping, root motion, curves, events, additive setup, and playback ranges. |
| 33 | PLANNED | Pose Library | Saves, previews, mirrors, blends, tags, and reuses skeletal poses. |
| 34 | PLANNED | Character Equipment Editor | Equips weapons, armor, staffs, props, audio, and effects through sockets. |
| 35 | PLANNED | Render Debugger | Inspects render passes, depth, normals, material buffers, shadow maps, and draw calls. |
| 36 | PLANNED | Frame Capture Analyzer | Per-frame timeline for scripts, AI, physics, animation, rendering, particles, audio, and UI. |
| 37 | PLANNED | Memory Profiler | Tracks RAM and VRAM use by assets, scenes, runtime systems, caches, and streaming cells. |
| 38 | PLANNED | Collision Analyzer | Visualizes channels, responses, contacts, overlaps, penetration, and collision ownership. |
| 39 | PLANNED | Navigation Query Tool | Tests paths, agent sizes, costs, unreachable areas, links, and dynamic obstacles. |
| 40 | PLANNED | AI Perception Debugger | Displays sight, hearing, teams, targets, last-known positions, and perception history. |
| 41 | PLANNED | Automated Test Panel | Runs engine, gameplay, asset, scene, packaging, and performance tests from the editor. |
| 42 | PLANNED | Localization Editor | Translation keys, languages, subtitles, fonts, localized assets, and missing-text reports. |
| 43 | PLANNED | Asset Reference Repair Tool | Finds and repairs missing, renamed, moved, or invalid asset references. |
| 44 | PLANNED | Source Control Panel | Modified files, history, conflicts, branches, commits, and change lists. |
| 45 | PLANNED | Project Migration Tool | Safely upgrades old project, scene, component, and asset serialization formats. |
| 46 | PLANNED | Build Size Analyzer | Reports packaged size by asset and detects unused or duplicated content. |
| 47 | PLANNED | UI Resolution and Localization Preview | Tests HUDs at different resolutions, aspect ratios, DPI settings, and text lengths. |
| 48 | PLANNED | Plugin Manager | Registers and manages editor panels, importers, asset types, runtime systems, and extensions. |

## Completion Checklist For Every Tool

Each tool must satisfy all applicable items before its status becomes **COMPLETE**:

- Accessible from the grouped **Panels** menu with an icon and persistent panel state.
- Clear empty state, tooltips, validation, undo/redo, and safe handling of locked objects.
- Uses engine-owned assets and project-relative paths.
- Saves and reloads authored data without duplication or extension errors.
- Updates the scene or runtime immediately where live preview is expected.
- Avoids per-frame rescans, decoding, rebuilding, shader compilation, or unnecessary allocations.
- Provides script functions when the authored data is useful during gameplay.
- Works in editor Play and packaged builds where runtime behavior is required.
- Has focused automated tests for non-visual logic.
- Builds successfully in Release configuration.
- Has concise usage documentation or an in-editor help section.

## Completed Milestone: Optimization Auditor

Initial scope:

1. Add an **Optimization Auditor** panel under the debugging/analysis panel group.
2. Scan the open scene on demand, never recursively every frame.
3. Report object and scene-level findings with severity and estimated impact.
4. Audit triangle/vertex counts, material slots, texture sizes, draw-call pressure,
   shadow-casting lights, terrain resolution, particle limits, foliage density,
   collision complexity, missing LODs, and missing/invalid assets.
5. Allow selecting and framing the object responsible for a finding.
6. Add safe quick fixes only when the result is deterministic; otherwise show guidance.
7. Export the report to a project-relative text or JSON file.
8. Add tests for thresholds, severity sorting, and report generation.

The milestone is complete when the panel passes its tests and the Release editor build.

## Completed Milestone: Ragdoll Physics Editor

Initial scope:

1. Create and edit a physics body for each selected skeleton bone.
2. Visualize bodies and constraints directly on the character preview.
3. Provide capsule, box, and sphere body shapes with mass and damping controls.
4. Author parent-child angular limits and collision rules.
5. Generate a sensible starting rig automatically from a skeleton.
6. Preview animation-to-ragdoll and ragdoll-to-animation blending.
7. Persist the authored physics asset and use it in editor Play and packaged builds.
8. Add focused serialization and constraint tests, then verify the Release editor.

## Completed Milestone: Animation Retargeting Tool

Initial scope:

1. Select engine-owned source and target skeletons from searchable asset lists.
2. Auto-map bones by normalized names, hierarchy, and humanoid roles, with manual overrides.
3. Visualize both skeletons and highlight unmapped or conflicting bones.
4. Author source/target reference-pose corrections and reusable retarget profiles.
5. Preview retargeted clips beside the source animation with root-motion controls.
6. Batch-retarget selected animation clips without importing duplicate meshes or skeletons.
7. Save engine-owned retargeted clips and preserve their source/profile dependencies.
8. Add mapping, serialization, and animation-sampling tests, then verify editor and player Release builds.

## Completed Milestone: Ability Editor

Initial scope:

1. Create engine-owned ability assets for attacks, spells, movement skills, and interactions.
2. Author resource costs, cooldowns, charges, activation rules, targeting, and interruption behavior.
3. Select animation action clips, audio, particles, projectiles, camera effects, and HUD feedback.
4. Build ordered ability phases such as wind-up, cast, active, recovery, and cancel windows.
5. Author damage, healing, status effects, impulses, traces, areas, and team filtering.
6. Preview the timing timeline and validate missing or incompatible dependencies.
7. Expose native C++ and Lua functions to grant, activate, cancel, and query abilities.
8. Persist ability state in editor Play and packaged games, add focused tests, and verify Release builds.

## Completed Milestone: Runtime Property Inspector

Initial scope:

1. Select live entities and components while the editor is in Play mode.
2. Show runtime values beside their authored scene values without changing the saved level.
3. Edit supported runtime fields safely while paused or running.
4. Add Play pause, single-frame step, and refresh controls to make transient state inspectable.
5. Reset an individual field, component, or entity to its Play-start value.
6. Filter properties, highlight changed values, and keep a short runtime edit history.
7. Respect component ownership, entity destruction, scene reloads, and Play-mode shutdown.
8. Add focused snapshot/diff/reset tests and verify the Release editor and player builds.

## Completed Milestone: Asset Dependency Viewer

Initial scope:

1. Build a project-wide reference graph from the engine asset registry and authored asset dependencies.
2. Search and select scenes, prefabs, materials, scripts, characters, shaders, textures, and other engine assets.
3. Show both outgoing dependencies and incoming references without rescanning every frame.
4. Provide a navigable node graph plus compact tree and list views for large projects.
5. Highlight missing, stale, circular, duplicated, and unreferenced assets.
6. Open the selected asset in its matching editor and reveal its location in the Content browser.
7. Export dependency and unused-asset reports without changing or deleting project content.
8. Add focused graph/query tests and verify the Release editor and player builds.

## Completed Milestone: Weather Editor

Initial scope:

1. Author reusable weather presets for clear, rain, snow, fog, and storms.
2. Control precipitation, wind, cloud cover, fog, lightning, and wet-surface response.
3. Preview weather inside an isolated editor viewport without changing the level.
4. Blend between presets over time and expose transitions to gameplay scripts.
5. Connect weather to particles, audio, lighting, atmosphere, terrain, foliage, and water.
6. Store weather as an engine-owned asset that can be assigned from Game Mode or World Settings.
7. Add debug statistics, quality controls, and deterministic preview seeds.
8. Add focused serialization/transition tests and verify Release editor and player builds.

## Completed Milestone: Procedural Building Tool

Initial scope:

1. Draw and edit closed building footprints directly in an isolated preview and level viewport.
2. Generate floors, walls, ceilings, and roofs from configurable dimensions.
3. Place parametric doors, windows, arches, columns, and stair openings on wall segments.
4. Support multiple storeys, per-storey height, wall thickness, and reusable style presets.
5. Assign engine-owned materials and modular meshes by building surface or opening type.
6. Regenerate non-destructively when footprint points or settings change.
7. Bake the result into editable scene objects, a prefab, or a streamable level instance.
8. Add focused geometry/serialization tests and verify Release editor and player builds.

Delivered as the **Procedural Building Tool** panel and `.3dgbuilding` authored
asset. Generated pieces remain compatible with the existing Prefab Editor,
level-layer, modular-placement, and level-streaming workflows.

## Completed Milestone: Road Generator

Initial scope:

1. Draw and edit roads as splines in the isolated tool preview and level viewport.
2. Generate flexible road meshes with width, thickness, lanes, shoulders, and UV tiling.
3. Conform roads to terrain with configurable offset, smoothing, and cut/fill guidance.
4. Author lane markings, curbs, sidewalks, barriers, signs, and reusable road styles.
5. Build intersections, junctions, roundabouts, merges, and end caps without mesh gaps.
6. Assign engine materials and modular roadside meshes without per-frame asset rescans.
7. Regenerate non-destructively and bake to scene objects, prefabs, or streamed levels.
8. Add focused spline/geometry/serialization tests and verify Release editor and player builds.

Delivered as the **Road Generator** panel and `.3dgroad` authored asset. Generated
road pieces remain compatible with prefabs, level layers, modular intersection
meshes, and streamed-level workflows.

## Completed Milestone: Level Instance Tool

Initial scope:

1. Convert a selected group of level objects into a reusable level-instance asset.
2. Place multiple lightweight instances while retaining a link to the source level.
3. Edit the source in isolation and propagate changes to every placed instance.
4. Support per-instance transforms, visibility, streaming distance, and data layers.
5. Allow safe overrides and clearly distinguish inherited from overridden values.
6. Detect circular references, missing source levels, and incompatible scene data.
7. Break an instance back into editable scene objects without losing transforms.
8. Add focused serialization/reference tests and verify Release editor and player builds.

Delivered as the **Level Instance Tool** panel, backed directly by `.3dgworld`
streaming references. It exports multi-object selections as reusable `.scene`
sources, supports linked placement/duplication and streaming rules, validates
missing or self-referencing sources, opens sources for isolated editing, and can
break an instance back into editable objects while preserving its transform.

## Completed Milestone: World Partition Tool

Initial scope:

1. Divide large worlds into configurable streaming cells.
2. Assign level instances and loose actors to cells without duplicating assets.
3. Preview loaded, unloaded, and always-loaded cells in the editor viewport.
4. Estimate per-cell object, triangle, texture, and memory cost.
5. Configure loading ranges, priorities, and data-layer filters.
6. Detect oversized cells, cross-cell references, and streaming gaps.
7. Cook the partition layout into the existing level-streaming runtime.
8. Add partition/reference tests and verify Release editor and player builds.

Delivered as the **World Partition Tool** panel and version-3 `.3dgworld`
partition metadata. It assigns linked scenes to cells without duplicating them,
converts loose selections into cell levels, previews residency and active data
layers, estimates cell costs, validates gaps and oversized content, and drives
priority-aware runtime streaming while remaining backward-compatible with older
world manifests.

## Completed Milestone: Geometry Editing Tool

Initial scope:

1. Enter vertex, edge, and face edit modes for engine-owned static meshes.
2. Support component selection, box selection, and connected selection.
3. Extrude, inset, bevel, bridge, weld, and delete selected components.
4. Add edge loops and configurable subdivision for blockout refinement.
5. Recalculate normals/tangents and detect non-manifold or degenerate geometry.
6. Preserve material sections, vertex colors, UVs, collision, and mesh identity.
7. Save non-destructively with undo/redo and explicit apply/revert controls.
8. Add mesh-operation tests and verify Release editor and player builds.

Delivered inside the **Mesh Editor** as a static-mesh-only Geometry mode. It
provides component and connected selection; extrusion, inset, bevel,
subdivision, deletion, welding, and loop bridging; normal/tangent rebuilding;
degenerate cleanup and topology diagnostics; bounded undo/redo; and explicit
apply/revert. Operations preserve UV and lighting attributes, vertex paint,
material sections, and native asset identity.

## Completed Milestone: Procedural Scatter Graph

Initial scope:

1. Author deterministic scatter rules as reusable node graphs.
2. Provide input, filter, transform, density, mask, exclusion, and output nodes.
3. Preview generated instances without permanently changing the level.
4. Support terrain slope, height, layer, spline, and volume filters.
5. Use engine-owned static meshes and foliage assets as weighted outputs.
6. Bake to editable objects, foliage instances, or reusable level instances.
7. Save graphs as Content assets and expose generation to scripts.
8. Add deterministic graph tests and verify Release editor and player builds.

Delivered as the **Procedural Scatter Graph** panel and native `.3dgscatter`
asset. The editor provides a pannable/zoomable node canvas, deterministic seed,
region and instance limits, density, height and slope filters, exclusion circles,
random transforms, weighted static-mesh outputs, temporary preview, terrain-aware
evaluation, and baking to editable objects or a batched foliage actor. Assets
retain stable IDs and mesh dependencies, open by double-clicking in Content, and
can be evaluated through C++ or Lua scripts.

## Current Milestone: Lighting Analysis Tool

Initial scope:

1. Add light-complexity and overlapping-light viewport modes.
2. Visualize shadow coverage, cascade usage, and shadow-map resolution pressure.
3. Detect unlit areas and exposure ranges for indoor and outdoor scenes.
4. Add shader/material overdraw and transparency-cost visualization.
5. Report expensive lights, oversized influence radii, and redundant shadow casters.
6. Provide per-object and whole-level analysis with actionable recommendations.
7. Export a concise lighting-analysis report for optimization passes.
8. Add focused analysis tests and verify Release editor and player builds.
