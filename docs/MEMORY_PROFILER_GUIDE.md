# Memory Profiler

The Memory Profiler shows where editor and runtime memory is owned. Open it from
**Panels > Debug & Diagnostics > Memory Profiler**.

## Memory totals

- **Process private** is the memory committed specifically for the editor process.
- **Working set** is the portion currently resident in physical RAM.
- **Tracked CPU** is memory whose owner is known to the engine, including asset
  metadata, animation keys, ECS component pools, terrain data, and runtime caches.
- **Tracked GPU** is the calculated size of engine-owned geometry, textures, render
  targets, lighting data, and other retained graphics resources.
- **Driver VRAM** is the GPU driver's total process/device reading when the active
  OpenGL driver exposes it. On unsupported hardware the panel keeps showing the
  portable engine-tracked GPU total.

The process value will normally be higher than the tracked CPU value. It also includes
allocators, executable code, DLLs, driver allocations, temporary work buffers, and UI
state that do not have a stable engine owner.

## Ownership table

Expand a category to inspect individual owners. The panel covers:

- edit-mode and Play-mode static models, skeletal models and animation data;
- standalone textures, materials, foliage assets, and shader assets;
- edit and Play ECS component pools;
- terrain height/paint data, geometry, and generated surface textures;
- shadow maps, GTAO, SSGI, post-processing and main scene targets;
- dynamic/baked lighting and reflection probes;
- grass instances, live particles, behavior-tree caches, and authored streaming cells.

World cells shown as **not resident** report their scene file's disk footprint for
planning. They are excluded from resident CPU/GPU totals. The editor Play preview is a
single in-memory scene; packaged worlds use the runtime streaming manager for actual
cell residency.

Use the filter to find an asset path, component type, or category. Counts mean assets,
entities, instances, particles, or probes according to the row.

## Budgets and leak checks

Set RAM and VRAM budgets for the current target platform. The panel highlights either
budget when exceeded. Its histories sample every quarter second, limiting diagnostic
overhead while still making long-term growth visible.

Select **Set Baseline**, perform the operation under investigation, and inspect the
signed deltas. For example:

1. Set a baseline before entering Play.
2. Enter and exit Play several times.
3. A stable result should return close to the original private and tracked totals.
4. Expand a growing category to identify the retained owner.

Use **Clear Baseline** before testing a different workflow. Asset caches intentionally
retain resources until their owning asset manager is cleared, so an increase after the
first load is not automatically a leak.

## Accuracy notes

Geometry sizes use retained vertex/index counts, texture sizes include the standard
mipmap chain, and engine render systems expose their allocated target sizes directly.
Driver and shader-program internals are not portable to query, so they remain part of
process/driver totals rather than being assigned an invented owner.
