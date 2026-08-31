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
| 16 | **COMPLETE** | Lighting Analysis Tool | Light complexity, shadow coverage, exposure, overlap, unlit-area, and overdraw views. |
| 17 | **COMPLETE** | Biome Editor | Reusable terrain, foliage, water, weather, lighting, particles, and ambient-audio presets. |
| 18 | **COMPLETE** | Day/Night Timeline | Authors sky, sun, moon, clouds, fog, lighting, and environmental audio over time. |
| 19 | **COMPLETE** | Cave and Tunnel Tool | Spline-generated cave/tunnel meshes, chambers, entrances, collision, and navigation floors. |
| 20 | **COMPLETE** | Fence and Wall Painter | Viewport drawing of connected fences and walls with corners, posts, gates, and snapping. |
| 21 | **COMPLETE** | Destruction Authoring Tool | Fracture pieces, strength, debris, damaged states, sound, particles, and collision. |
| 22 | **COMPLETE** | Interactive Door and Lift Tool | Fast setup for doors, gates, elevators, platforms, switches, locks, and access conditions. |
| 23 | COMPLETE | Portal and Teleport Tool | Teleporters, destination previews, safe arrivals, scripted activation, and packaged level-transition portals. |
| 24 | COMPLETE | Quest Editor | Objectives, conditions, rewards, persistent state, dialogue triggers, checkpoints, and live debugging. |
| 25 | **COMPLETE** | Dialogue Editor | Branching conversations, conditions, events, voice clips, portraits, and localization keys. |
| 26 | **COMPLETE** | Inventory and Item Editor | Weapons, armor, consumables, pickups, currencies, statistics, icons, and effects. |
| 27 | **COMPLETE** | Combat Editor | Damage types, combos, targeting, blocking, parrying, stagger, hit reactions, and immunity windows. |
| 28 | **COMPLETE** | Spawn Manager | Spawn volumes, weighted groups, waves, pooling, difficulty scaling, and encounter controls. |
| 29 | **NEXT** | Checkpoint and Save Editor | Visual configuration of persisted player, quest, world, and streamed-level state. |
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

## Completed Milestone: Lighting Analysis Tool

Initial scope:

1. Add light-complexity and overlapping-light viewport modes.
2. Visualize shadow coverage, cascade usage, and shadow-map resolution pressure.
3. Detect unlit areas and exposure ranges for indoor and outdoor scenes.
4. Add shader/material overdraw and transparency-cost visualization.
5. Report expensive lights, oversized influence radii, and redundant shadow casters.
6. Provide per-object and whole-level analysis with actionable recommendations.
7. Export a concise lighting-analysis report for optimization passes.
8. Add focused analysis tests and verify Release editor and player builds.

Delivered as the **Lighting Analysis** panel under Debug & Diagnostics. The tool
samples the authored level on a configurable world-space grid and provides live
viewport modes for light complexity, shadow coverage, exposure, unlit areas, and
transparent-material cost. It reports local-light overlap, redundant shadow
casters, oversized influence ranges, directional cascade pressure, unlit and
overexposed coverage, transparency/transmission cost, and material overdraw risks.
Findings include actionable recommendations, can frame their affected scene object,
and export to `Content/Reports/LightingAnalysis.txt`. The calculation core is
deterministic, clamps unsafe settings, and is covered by focused regression tests.

## Completed Milestone: Biome Editor

Initial scope:

1. Author reusable biome assets that bundle terrain-layer rules and environment presets.
2. Combine weighted foliage, rocks, decals, water, weather, particles, and ambient audio.
3. Define height, slope, moisture, temperature, mask, spline, and exclusion rules.
4. Preview biome coverage and transitions in a dedicated viewport and in the level.
5. Blend multiple biomes without hard seams and support deterministic variation.
6. Apply biomes non-destructively to selected landscapes or bake optimized instances.
7. Save engine-owned Content assets and expose biome application to C++ and Lua.
8. Add deterministic biome tests and verify Release editor and player builds.

Delivered as the **Biome Editor** under Level Design and as engine-owned
`.3dgbiome` assets. Biomes combine normalized terrain-layer ranges, deterministic
weighted foliage rules, moisture and temperature controls, transition width,
weather, water, particles, and ambient audio. The dedicated preview visualizes
surface coverage and population before applying it to a selected landscape.
Application updates landscape materials and environment settings and rebuilds
named biome-generated foliage, water, particle, and audio actors so repeated
applications remain predictable. Asset IDs and dependencies survive save/load;
Content double-click opens the editor; C++ and Lua can generate deterministic
biome population at runtime. Focused asset tests and Release editor/player builds
pass.

## Completed Milestone: Day/Night Timeline

Initial scope:

1. Author reusable environment timelines with a normalized 24-hour track.
2. Keyframe sun, moon, sky, clouds, fog, exposure, wind, and ambient audio.
3. Provide smooth interpolation, loop controls, time scale, pause, and time jumps.
4. Preview and scrub timelines in a dedicated editor viewport and the level.
5. Add event markers for sunrise, sunset, weather changes, and gameplay callbacks.
6. Bind a timeline as the level default while allowing script overrides.
7. Save engine-owned Content assets and expose complete C++ and Lua controls.
8. Add timeline interpolation/serialization tests and verify Release builds.

Delivered as the **Day/Night Timeline** panel under World & Gameplay and as
engine-owned `.3dgdaynight` assets. The editor provides a normalized 24-hour
track, smooth circular interpolation across midnight, key capture from the active
level, a dedicated sky preview, level scrubbing, playback, loop/rate/day-length
controls, environment audio selection, and named event markers. A saved timeline
can be assigned as the level default and autoplays in packaged runtime scenes.
Scripts can load, play, pause, stop, seek, change rate, query time, and consume
timeline events in C++ and Lua. Focused serialization, interpolation, event, and
runtime-control tests pass, as do Release editor and player builds.

## Completed Milestone: Cave and Tunnel Tool

Initial scope:

1. Draw cave and tunnel centerlines with the existing spline editing workflow.
2. Generate smooth enclosed meshes with editable width, height, wall thickness, and resolution.
3. Support branches, junctions, chambers, entrances, dead ends, and vertical shafts.
4. Assign floor, wall, ceiling, trim, wetness, and detail materials by engine asset.
5. Conform entrances to terrain and optionally carve or hide intersecting landscape regions.
6. Generate optimized collision, navigation surfaces, portals, and streaming sections.
7. Save reusable engine-owned cave assets and expose spline/profile controls to scripts.
8. Add mesh/topology tests and verify Release editor and player builds.

Delivered as the **Cave and Tunnel Tool** under Level Design and as engine-owned
`.3dgcave` assets. Existing scene splines provide the centerline, while adjustable
elliptical profiles, smooth arc-length sampling, chambers, vertical paths, open or
capped ends, and terrain-conformed entrances produce an inward-facing baked
`.3dgmesh`. Hidden floor, ceiling, and side-wall collision strips keep the interior
walkable; optional floor generation feeds the normal navigation build. Rebuilding
reuses the baked mesh identity and replaces prior generated objects. Content
double-click reopens cave assets, and C++/Lua can spawn their baked visual mesh at
runtime. Larger branch networks can be assembled from intersecting authored cave
splines while keeping each branch independently rebuildable.

## Completed Milestone: Fence and Wall Painter

Delivered as the **Fence and Wall Painter** under Level Design and as reusable
`.3dgfence` assets. The tool creates an editable level spline for viewport drawing,
captures or rebuilds from live spline points, snaps endpoints to a configurable grid,
tiles exact-length panels, follows slopes, deduplicates corner posts, inserts gates
and empty openings, and supports engine-owned panel/post/gate meshes and materials.
Generated pieces are normal editable scene objects with optional World Static box
collision, so they work in Editor Play and packaged levels without a special runtime
renderer. Saved assets retain stable IDs and dependencies, reopen by Content
double-click, participate in unsaved-document handling, and are covered by focused
generation, slope, snapping, gate, serialization, and registry tests.

## Completed Milestone: Destruction Authoring Tool

Delivered as the **Destruction Authoring Tool** under Level Design and as reusable
`.3dgdestruction` assets. The tool captures a selected mesh, authors ordered damaged
states, deterministic fracture grids, health and impact thresholds, debris mass,
collision, impulses, lifetime, replacement meshes/materials, and particle/audio
effects. It offers an isolated fracture preview plus removable dynamic level-preview
chunks. At runtime, native C++ and Lua can configure, damage, impact, query, and react
to destructibles; state changes replace visuals and spawn effects, while final breaks
create physics debris and clean it up after the authored lifetime. Stable asset IDs,
dependencies, Content double-click, dirty-document handling, focused tests, and the
Release editor build are verified.

## Completed Milestone: Interactive Door and Lift Tool

Delivered under Level Design with engine-owned `.3dginteraction` assets. Authors can
start from hinged-door, sliding-door, gate, elevator, or moving-platform presets;
edit local pivots, axes, travel, timing, easing, looping, auto-close, prompts, access
tags, locks, sounds, and action clips; and scrub or play an isolated motion preview.
Applying an asset configures the selected object as a kinematic interactive object.
That binding persists in editor scenes, exports to runtime scenes, resolves through
stable asset IDs, and runs in both editor Play mode and packaged games. Native C++
and Lua can open, close, toggle, lock, and query an interaction. Registry discovery,
Content double-click, dependency tracking, dirty-document saving, focused regression
tests, the complete Release build, and all tests are verified.

## Completed Milestone: Portal and Teleport Tool

Implemented as reusable `.3dgportal` assets with same-level, level-transition, and
seamless-door modes. The editor provides searchable destination objects and levels,
arrival offset/rotation controls, a destination preview, access tags, cooldowns,
automatic activation, safe arrival, velocity/facing controls, audio, and transition
effects. Bindings persist in editor scenes and packaged scenes. Native C++ and Lua
can activate and query portals, and automatic portals use the configured game-mode
player. Asset discovery, dependency tracking, tests, Release builds, and startup are
verified.

## Completed Milestone: Quest Editor

Implemented as reusable `.3dgquest` assets with start conditions, objective
prerequisites, optional objectives, checkpoints, dialogue triggers, four reward
types, repeatable quests, serialized runtime state, editor simulation, scene
binding, packaging, stable dependencies, and native C++/Lua control.

## Completed Milestone: Dialogue Editor

Implemented as reusable `.3dgdialogue` assets with speakers, localized lines and
choices, conditional branching, portraits, voice clips, camera hooks, script events,
scene bindings, packaged runtime loading, serialized conversation state, native
C++/Lua control, flow visualization, and live conversation debugging.

## Completed Milestone: Inventory and Item Editor

Implemented as engine-owned `.3dgitem` assets with searchable icons, meshes,
pickup prefabs, abilities, animations, audio, particles, statistics, tags, and use
effects. Runtime inventories support slots, weight, stacking, unique items,
consumption, equipment, events, saved state, editor starting loadouts, packaged
scene loading, and native C++/Lua control.

## Completed Milestone: Combat Editor

Implemented as engine-owned `.3dgcombat` profiles with teams, friendly-fire rules,
damage types, resistances, ordered combos, action clips, input and hit windows,
blocking, parrying, immunity, poise, stagger, hit reactions, particles, audio,
scene binding, packaged runtime loading, native C++/Lua control, dependency
tracking, timeline authoring, and live combat debugging.

## Completed Milestone: Spawn Manager

Implemented as engine-owned `.3dgspawn` encounters with point, box, and sphere
volumes, deterministic weighted groups, difficulty gates, ordered waves, delays,
clear conditions, player-entry and automatic triggers, concurrent/total limits,
dead-entity recycling, prefab cooking dependencies, scene binding, packaged
runtime loading, native C++/Lua control, preview statistics, and lifecycle events.

## Next Milestone: Checkpoint and Save Editor

Planned scope: visual save checkpoints, persisted component selection, player and
quest state, streamed-level state, respawn rules, slots, and live save debugging.
