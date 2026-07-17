# Particle System Editor — implementation milestone

Status: **Complete** — runtime, editor authoring, reusable assets, CPU/GPU simulation,
debugging, scripting, staged modules, and compiled pipeline visualization delivered.

Objective: turn the existing `ParticleEmitter`, `EmitterConfig`, and
`ParticleRenderer` runtime into a complete editor-authored Particle System component
with live preview, reusable assets, runtime scene support, and gameplay control.

## Definition of done

The milestone is complete when a user can add a Particle System component to an
object, edit every supported emitter property with immediate viewport feedback, save
the effect as a reusable asset, reload the scene, enter Play mode, and see the same
effect in an exported runtime scene without writing code.

## M1 — Runtime component and scene pipeline *(implemented)*

- Add an ECS `ParticleSystemComponent` containing `EmitterConfig`, playback flags,
  burst settings, local/world-space mode, and a runtime `ParticleEmitter` instance.
- Add Particle System authoring data to `EditorScene::Object` with backward-compatible
  scene versioning.
- Serialize the component through `RuntimeSceneExporter` and
  `RuntimeSceneLoader::EntityDesc`, then instantiate it in the runtime registry.
- Add a runtime system that updates emitter position from `Transform`, advances
  particles, handles autoplay/loop/burst behavior, and cleans up destroyed entities.
- Render all runtime emitters through `ParticleRenderer` in editor Play mode and in
  standalone runtime usage.

Acceptance criteria:

- A saved scene reproduces the same emitter settings after reload.
- Runtime export validation reports the number of particle systems.
- Moving an entity moves its emitter correctly in local-space mode.
- Entering and leaving Play mode does not leak particles or GPU resources.

## M2 — Particle System component Inspector *(implemented)*

- Add Particle System to the component-based Add menu.
- Add grouped Inspector controls:
  - **Playback:** enabled, autoplay, loop, duration, start delay, simulation speed,
    prewarm, local/world space.
  - **Emission:** rate, maximum particles, burst count, burst interval.
  - **Shape:** Point/Sphere/Cone, radius, direction, cone angle.
  - **Motion:** speed range, lifetime range, gravity, drag.
  - **Appearance:** start/end colour, start/end size, Additive/Alpha blend.
- Clamp invalid ranges and display clear warnings instead of accepting broken data.
- Support undo/redo, duplicate, copy/paste, component removal, and dirty-scene state.
- Add Reset and sensible Fire, Smoke, Sparks, Magic, and Dust presets.

Acceptance criteria:

- Every current `EmitterConfig` field is editable.
- Multi-field changes produce one coherent undo operation where appropriate.
- Presets create usable effects and remain fully editable.

## M3 — Dedicated Particle Editor panel *(implemented)*

- Register a **Particle Editor** panel in `EditorPanels` and the Panels menu.
- Provide Play/Pause/Restart/Stop, one-shot Burst, Clear, and simulation-time scrub.
- Display live statistics: alive particles, configured maximum, emission rate,
  estimated overdraw, and preview FPS.
- Add a large isolated preview viewport with orbit camera, grid, background colour,
  optional ground plane, and bloom toggle.
- Add curve editors for size-over-life and colour/alpha-over-life. Extend the runtime
  from the current start/end interpolation to sampled curves while preserving old
  two-key behavior.
- Add drag-and-drop texture assignment and render textured billboards, retaining the
  current soft radial particle as the default.

Acceptance criteria:

- Editing a value updates both the isolated preview and scene viewport immediately.
- Preview transport is deterministic after Restart when using the same random seed.
- The panel remains responsive at the configured maximum-particle count.

## M4 — Reusable particle assets *(implemented)*

- Add a versioned `.particle` asset format for emitter settings, curves, texture,
  material/blend mode, bounds, and metadata.
- Add New, Save, Save As, Revert, and unsaved-change protection to the panel.
- Recognize `.particle` files in the Assets panel with an appropriate asset type.
- Support dragging a particle asset onto the viewport to create an object and dragging
  one onto an existing Particle System component to assign it.
- Allow per-instance overrides without modifying the source asset; provide Revert
  Override and Apply to Asset actions.
- Validate missing textures and malformed/unsupported asset versions gracefully.

Acceptance criteria:

- One particle asset can drive multiple scene instances.
- Editing the asset refreshes all non-overridden instances.
- Missing assets show a diagnostic and do not crash scene loading.

## M5 — Debugging, scripting, and polish *(implemented)*

- Draw emitter shapes, direction, bounds, and culling state in the scene viewport.
- Add selection-only and global particle debug toggles.
- Expose script helpers: Play, Pause, Stop, Restart, Burst, Clear, SetEmissionRate,
  SetColour, and query Alive count.
- Add no-code trigger and animation-event actions for Play, Stop, Restart, and Burst.
- Add CPU update and GPU draw timing to the editor performance/debug panel.
- Add configurable simulation bounds and frustum culling.
- Add tests for emitter determinism, burst/max limits, serialization round trips,
  runtime instantiation, and component cleanup.

Implemented in the core pass: selected-emitter shape/direction/bounds guides,
configurable bounds and frustum culling, script/runtime controls, trigger-volume and
animation-event actions, reusable runtime-scene bindings, plus CPU/GPU draw statistics.
The automated regression suite now covers deterministic restart, emission limits,
particle cleanup, runtime controls, trigger/animation actions, asset round trips,
and version-1 asset compatibility. The Debug > Particles menu now provides selected
or scene-wide shape, direction, bounds, and culling-state overlays.

Acceptance criteria:

- Particle effects can be controlled from scripts, trigger volumes, and animation
  notifications.
- Debug views make emitter shape and culling problems visible.
- Automated tests cover the scene and asset compatibility boundaries.

## Recommended implementation order

1. Runtime ECS component and update/render integration.
2. Scene/runtime serialization with round-trip tests.
3. Add menu and Inspector authoring controls.
4. Dedicated preview panel and transport.
5. Curves, textures, and reusable `.particle` assets.
6. Trigger/script integration, debug overlays, culling, and profiling.

## Intentional exclusions

- Volumetric particle renderers (billboards, trail ribbons, built-in meshes, and model assets are now supported).
- Scene-depth collision (runtime physics-collider collision is now supported).
- Arbitrary shader-code nodes and cyclic module graphs. The delivered staged module
  stack compiles a validated typed linear pipeline shared by CPU and GPU backends.

Reference-based multi-emitter `.particlefx` assets and mesh particles are now supported.
The GPU compute backend now handles billboard emission, motion, colour, size, rotation,
four-key lifetime curves, textures, and flipbook animation on OpenGL 4.3, with automatic
CPU fallback. Plane, sphere, and oriented-box collision also run on GPU for scenes with
up to 32 relevant colliders. GPU-resident motion history and camera-facing trail ribbons
are also supported. Built-in and imported mesh particles use SSBO-driven instanced GPU
rendering with velocity alignment. GPU prewarm uses batched fixed-step compute dispatches
without per-step readbacks, completing GPU parity for the current particle feature set.
A Niagara-style authoring milestone is now underway. The first foundation pass adds
an ordered module stack to every emitter with required Spawn/Update/Render modules,
toggleable Color/Size-over-life, Collision, and Trails modules, and persistence in
`.particle`, editor-scene, and exported runtime-scene formats. Module state compiles
into the existing CPU/GPU emitter flags, so stack-authored effects remain compatible
with both simulation backends. Selecting a module now opens a focused property view
for only that module, while playback stays available as a system-level section. The
particle editor now has a searchable Add Module library. Optional modules can be
removed, searched, and inserted again; required pipeline modules remain protected.
Force, Initial Velocity, and Rotation modules now support duplicate instances with
stable IDs, individual names, per-instance enable state, and additive compilation.
Their vector/range parameters persist through assets, editor scenes, and runtime
exports. Colour and Size modules now also support named duplicate layers with
multiplicative endpoint composition and composited four-key curves. The stack now
uses explicit Spawn, Update, and Render stages, with canonical grouping and movement
limited to the selected stage. The editor exposes the compiled typed connections
between enabled modules and validates required modules, unique identities, and stage
ordering before the pipeline is used.

## Closure

The particle-system milestone is closed. Effects can be authored without code,
previewed in isolation, saved and layered as reusable assets, controlled by gameplay,
debugged in-scene, and exported with equivalent CPU or GPU behavior. Future work in
this area should be driven by a concrete rendering requirement—such as volumetric
simulation or custom shader nodes—rather than treated as unfinished core functionality.
