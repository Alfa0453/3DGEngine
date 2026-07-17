# Shader Editor — proposed implementation milestone

Status: **Complete — Surface, Post Process, Particle, and Unlit/UI domains are implemented and verified**

## Objective

Add a dedicated Shader Editor that lets a developer create reusable shaders without
editing renderer source code. The editor will provide a typed visual node graph,
generated GLSL inspection, compiler diagnostics, live preview, material integration,
runtime asset loading, and safe fallback behavior.

The Shader Editor complements the Material Maker:

- **Material Maker** authors values and textures for a material instance.
- **Shader Editor** authors the GPU program and the parameters exposed by that
  program.
- A `.3dgmat` material may reference a `.3dgshader` shader asset and store values for
  its exposed parameters.

## Definition of done

The milestone is complete when a developer can:

1. Create a Surface shader asset in the Shader Editor.
2. Build a typed node graph and inspect its generated GLSL.
3. Compile it without restarting the editor and see readable node/source diagnostics.
4. Preview it on a sphere, cube, plane, or imported model under the engine PBR
   environment.
5. Expose scalar, vector, colour, texture, and boolean parameters to Material Maker
   and the object Inspector.
6. Assign the shader through a material asset, save the scene, export it, and obtain
   equivalent rendering in Play mode and the standalone runtime.
7. Recover safely from a missing or invalid shader through a visible fallback
   material rather than an invisible object or editor crash.

## Architectural contract

### Shader domains

Every shader asset declares one domain. A domain defines legal nodes, generated
stages, required engine bindings, and output fields.

Initial domain:

- **Surface:** opaque, masked, and transparent PBR materials for static and skinned
  geometry.

Later domains:

- **Post Process:** full-screen effects with scene-colour/depth inputs.
- **Particle:** billboard, ribbon, and mesh-particle shading.
- **Unlit/UI:** lightweight colour and texture shaders.
- **Compute:** intentionally deferred until resource binding and dispatch safety are
  designed.

### Asset model

Use a versioned `.3dgshader` asset containing:

- schema and asset version;
- stable shader and node IDs;
- domain, blend mode, render state, and feature flags;
- typed nodes, pins, links, positions, comments, and groups;
- exposed parameter definitions and defaults;
- texture defaults and sampler settings;
- optional graph metadata and preview settings;
- generated-source hash and compiler metadata, but not machine-specific GL handles.

Generated GLSL is derived data. The graph remains the authoritative source.

### Compilation pipeline

```text
Graph validation
    -> typed intermediate representation
    -> domain feature analysis
    -> vertex/fragment variant generation
    -> GLSL compilation and program link
    -> reflected parameter layout
    -> cached runtime shader program
```

The compiler must never replace a working program with a failed one. Compilation
occurs on the render thread while an OpenGL context is current. File parsing, graph
validation, and source generation may run off-thread.

### Engine binding contract

Domain templates own engine-required inputs such as:

- object, view, projection, and previous-frame matrices;
- camera position and time;
- lights, shadows, IBL, fog, and clustered-light data;
- skinning matrices and vertex attributes;
- scene depth/colour for compatible domains;
- material textures and exposed parameters.

User graphs produce domain outputs instead of manually redefining engine bindings.
This protects compatibility with static rendering, skinned rendering, depth passes,
shadow casters, picking, outlines, and runtime batching.

## M1 — Runtime shader asset and safe compilation foundation

Deliverables:

- Add `ShaderAsset`, `ShaderDomain`, `ShaderParameter`, graph node/pin/link data, and
  a versioned `.3dgshader` loader/saver.
- Extend `engine::Shader` with non-throwing compile results containing stage,
  severity, generated line, message, and driver log.
- Retain the existing throwing constructor for current engine code.
- Add a runtime shader cache keyed by asset path, source hash, domain variant, and
  relevant render features.
- Add explicit fallback shaders for invalid Surface, Post Process, Particle, and
  Unlit assets.
- Add dependency tracking and render-thread hot reload.
- Validate missing files, unsupported versions, duplicate IDs, invalid pin types,
  cycles, disconnected required outputs, and unsafe resource counts.

Acceptance criteria:

- Invalid GLSL produces diagnostics without terminating the editor.
- The previously valid program remains active after a failed hot reload.
- Shader assets survive save/load and project relocation through relative paths.
- Runtime packaging discovers shader and texture dependencies.

## M2 — Dedicated Shader Editor panel and live preview

Deliverables:

- Register **Shader Editor** in the Panels menu and dockspace.
- Add New, Open Selected, Save, Save As, Revert, Duplicate, and unsaved-change
  protection.
- Add an isolated preview with sphere, cube, plane, and imported-model shapes.
- Reuse the Material Maker environment controls: orbit camera, time of day,
  environment rotation, light intensity, ground/shadow, background, and bloom.
- Add Compile, Auto Compile, Apply, and Restore Last Valid controls.
- Display compilation status, duration, generated variant, parameter count, texture
  count, and program cache state.
- Provide generated vertex and fragment source views with line numbers, search,
  copy, and diagnostic navigation.
- Map generated GLSL lines back to graph node IDs.

Acceptance criteria:

- A graph edit updates the preview without restarting the editor.
- Selecting a compiler error focuses the responsible node and generated source line.
- Preview compilation never replaces the scene renderer’s active program.
- Camera and framebuffer state are restored after off-screen preview rendering.

## M3 — Typed visual graph

Initial node library:

- **Inputs:** UV, world/local position, normal, tangent, view direction, camera
  position, object colour, vertex colour, time, delta time.
- **Parameters:** float, integer, boolean, vector2/3/4, colour, texture2D.
- **Math:** add, subtract, multiply, divide, min/max, clamp, saturate, power, square
  root, absolute, sign, floor, fraction, modulo.
- **Vector:** compose, split, swizzle, dot, cross, normalize, length, reflect, lerp.
- **Texture:** sample texture, normal-map decode, UV transform, channel mask.
- **Utility:** one-minus, remap, smoothstep, Fresnel, noise, comparison, select.
- **Surface:** PBR output, unlit output, normal, emissive, opacity, alpha cutoff,
  clearcoat, transmission, subsurface, sheen, anisotropy, and displacement inputs.

Graph workflow:

- typed pins and compatible automatic conversions;
- create-node search palette;
- drag connections, reconnect, delete, duplicate, copy/paste;
- multi-select, box select, comments, frames, alignment, and auto-layout;
- undo/redo integrated with editor history;
- zoom, pan, minimap, breadcrumbs, and keyboard navigation;
- cycle prevention and clear connection errors;
- constant folding and removal of unreachable nodes.

Acceptance criteria:

- Graph serialization preserves IDs, connections, positions, parameter identities,
  and comments.
- Type-invalid and cyclic graphs cannot compile.
- Identical graphs generate deterministic source and hashes.
- Generated shader code contains only nodes reachable from the domain output.

## M4 — Material, renderer, and scene integration

Implementation status: **Complete**

The material format now stores a relocatable shader reference and reflected
parameter values. Material Maker can assign a selected shader, the Inspector
authors per-object overrides, and editor/runtime scene formats preserve those
overrides. Static primitives, imported static models, and skinned animated
models select the appropriate graph shader variant in Edit and Play modes.
Custom shaders use isolated per-object draws; standard opaque materials retain
the existing batching path. Transparent objects retain back-to-front sorting
and depth-write behavior, while masked standard material data continues through
the engine shadow-caster path.

Deliverables:

- Add an optional shader reference to `.3dgmat` and runtime material assets.
- Material Maker discovers reflected shader parameters and presents appropriate
  controls and texture slots.
- Object Inspector shows material-instance overrides without modifying the source
  material.
- Integrate Surface assets with `PbrRenderer` and `SkinnedRenderer`.
- Generate or select compatible variants for:
  - static and skinned vertices;
  - opaque, masked, and transparent passes;
  - directional, point, spot, and cascaded shadows;
  - depth-only and picking passes;
  - instanced and per-object rendering.
- Define when a custom shader remains batchable and when it requires a per-object
  draw.
- Serialize material shader references and overrides through editor scenes and
  runtime exports.

Acceptance criteria:

- Static and animated objects render the same material correctly.
- Masked materials cast masked shadows.
- Transparent materials retain sorting and depth behavior.
- Saving, reloading, entering Play mode, and runtime export preserve all parameter
  values and texture assignments.

## M5 — Additional shader domains

### Post Process

Implementation status: **Complete**

- Generates a fullscreen vertex variant instead of a geometry/material variant.
- Exposes Scene Color, Scene Depth, Screen UV, Texel Size, Exposure, Time, and
  Delta Time graph inputs.
- Uses sampleable depth textures on engine framebuffers.
- Extends the geometry prepass with view-space normals and per-pixel motion
  velocity derived from previous camera and object transforms.
- Supplies neutral-normal and zero-velocity fallbacks when optional geometry
  inputs are unavailable.
- Runs graph-authored effects in an ordered, full-resolution HDR ping-pong stack
  before bloom, exposure, tone mapping, gamma, and FXAA.
- Supports reflected scalar, vector, colour, boolean, and texture parameters.
- Previews Post Process graphs against the Shader Editor's rendered model scene.
- World Settings assigns selected Post Process shaders and supports enable,
  reorder, remove, and reflected parameter controls.
- Editor scene version 74 and runtime scene version 46 preserve effect order,
  enabled state, shader paths, parameter types, and parameter values.
- Provides explicit-UV colour, depth, normal, and velocity sampling plus
  pixel-offset UV helpers based on render-target texel size.

- Scene colour, depth, normal, velocity, and exposure inputs.
- Ordered post-process stack assets.
- Resolution-independent UV and texel-size helpers.
- Per-effect enable state, parameters, and runtime blending.

### Particle

Implementation status: **Complete**

- Particle colour, age, normalized lifetime, velocity, size, rotation, frame, UV,
  trail coordinates, and soft-depth inputs.
- Billboard graph output with reflected scalar, vector, colour, boolean, and texture
  parameters in the particle editor, scene preview, and Play mode.
- Graph-shaded systems automatically use the CPU backend; unsupported GPU, ribbon,
  and mesh variants retain their safe built-in renderers instead of disappearing.
- Particle asset format version 12 preserves shader references and overrides.

### Unlit/UI

Implementation status: **Complete**

- Colour, opacity, texture, UV, clipping, and signed-distance helpers.
- HUD panels, images, bars, and buttons can use Unlit shader assets with reflected
  parameters and texture bindings.
- HUD asset format version 2 preserves shader references and overrides while still
  loading version 1 documents.
- Text remains on the dedicated bitmap-font path; custom UI rectangles use isolated
  draws so they cannot corrupt text batching.

Acceptance criteria:

- Each domain exposes only valid inputs and outputs.
- Unsupported nodes produce authoring errors before GLSL compilation.
- Domain assets use the same parameter reflection, cache, diagnostics, and fallback
  infrastructure.

## M6 — Production hardening, debugging, and tests

Implementation status: **Complete for the current OpenGL renderer scope**

The runtime retains the last valid program on reload failure, caches source variants,
reports stage/line diagnostics, maps generated lines to graph nodes, exposes generated
source and compile state in the editor, enforces graph/resource limits, rejects
domain-invalid nodes before compilation, and provides visible domain fallbacks.
Regression coverage now includes serialization, validation, deterministic generation,
post-process bindings, particle attribute contracts, UI bindings, runtime scene
records, source hashing, and safe file diagnostics.

Disk-backed cross-session program binaries, compute shaders, unrestricted render
passes, and non-OpenGL backends remain intentional exclusions rather than incomplete
work in this milestone.

Deliverables:

- Shader asset dependency viewer and “Find Materials Using This Shader”.
- Variant and permutation inspector.
- Runtime shader/material debug view showing active program, parameters, textures,
  compile hash, cache hits, and fallback state.
- Configurable compile debounce and manual compile mode for large graphs.
- Disk-backed generated-source cache with engine/compiler version invalidation.
- Parameter migration by stable ID when a shader changes.
- Rename-safe parameter aliases and orphaned-value diagnostics.
- Compile-time limits for nodes, textures, uniforms, generated source, and variants.
- Graceful graphics-driver capability checks.

Automated coverage:

- asset round trips and legacy-version migration;
- graph validation, cycle detection, and type conversion;
- deterministic source generation and hashing;
- diagnostic line-to-node mapping;
- parameter reflection and migration;
- runtime cache and hot reload;
- invalid shader fallback;
- static/skinned/shadow variant compilation;
- material and scene export round trips;
- OpenGL-context smoke tests for every supported domain.

Acceptance criteria:

- No invalid shader can make an object silently disappear.
- Shader reload does not leak programs or invalidate working materials.
- Tests cover asset, compiler, renderer, and scene compatibility boundaries.
- Debug tools identify the exact shader variant used by a selected object.

## Recommended implementation order

1. Freeze Surface-domain engine bindings and variant requirements.
2. Implement the shared `.3dgshader` model, loader/saver, validator, and tests.
3. Add non-throwing compilation, diagnostic parsing, fallback programs, and caching.
4. Build the panel shell, source views, compilation controls, and live preview.
5. Implement the typed graph and deterministic GLSL generator.
6. Integrate shader references and reflected parameters with Material Maker.
7. Integrate static, skinned, shadow, depth, picking, and runtime scene variants.
8. Add Post Process, Particle, and Unlit/UI domains.
9. Complete debugging, packaging, migration, performance, and GL smoke tests.

## Risks and mitigations

- **Renderer coupling:** current PBR and skinned shaders are embedded in specialized
  renderers. Define domain templates before exposing graph nodes.
- **Variant explosion:** derive variants only from reachable features and cache by a
  stable feature key.
- **Driver-specific diagnostics:** preserve raw logs while parsing common NVIDIA,
  AMD, Intel, and Mesa formats.
- **Editor instability:** compile only with a current GL context and retain the last
  valid program.
- **Broken shadows/skinning:** user graphs produce material outputs; engine templates
  retain geometry and pass contracts.
- **Asset migration:** use stable parameter/node IDs and versioned migration tests.
- **Performance regressions:** expose batchability, variant count, compile time, and
  cache state in the editor.

## Intentional first-release exclusions

- Cyclic graphs and feedback loops.
- Arbitrary compute dispatch and unrestricted image/buffer writes.
- Geometry and tessellation shader authoring.
- HLSL, SPIR-V, Vulkan, Direct3D, or cross-compilation.
- User-defined render passes and arbitrary OpenGL state.
- Editing engine-reserved uniforms and descriptor bindings.
- Executing untrusted downloaded shader assets without validation.

These exclusions keep the first release compatible with the engine’s current OpenGL
renderer while leaving clear extension points.

## Approval gate

Implementation must not begin until the user gives an explicit go-ahead. The first
implementation pass should cover M1 only, followed by a build and asset/compiler
regression tests before work starts on the visual panel.
