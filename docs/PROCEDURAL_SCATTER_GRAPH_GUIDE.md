# Procedural Scatter Graph Guide

The Procedural Scatter Graph creates deterministic vegetation, rocks, debris,
structures, or prop distributions and stores the rules as an engine-owned
`.3dgscatter` Content asset.

## Create a graph

Open **Panels > Level Design > Procedural Scatter Graph**. A new graph contains
a Region Input, Density, Random Transform, and Weighted Mesh Output. Set the
asset path, choose a static mesh for the output, and press **Save**. Double-click
the saved asset in Content to reopen it.

The graph header controls:

- **Seed**: identical graph data and seed produce identical placements.
- **Maximum Instances**: a safety cap for preview, baking, and runtime generation.
- **Region Minimum/Maximum**: the world-space distribution area.

## Nodes

- **Region Input** begins a placement stream inside the graph bounds.
- **Density** multiplies the number of candidates per square metre.
- **Height Filter** accepts a world-height interval.
- **Slope Filter** accepts a surface-angle interval in degrees.
- **Random Transform** controls per-axis scale, yaw, and surface alignment.
- **Exclusion Circle** removes candidates inside a world-space circle.
- **Weighted Mesh Output** selects an engine-owned static mesh. Multiple output
  nodes distribute instances according to their relative weights.

Select a node to edit it. Drag nodes with the left mouse button, pan with the
middle mouse button, and zoom with the wheel. The Input field connects a node to
an earlier stream. Preview points are temporary and never alter the level.

## Bake

Choose one of two targets:

- **Editable Objects** creates regular scene mesh objects. Use this for unique
  hero placement or when every result must be adjusted independently.
- **Foliage Instances** creates one generated foliage palette and one batched
  foliage actor. Use this for large vegetation and prop populations.

Editor baking evaluates the actual terrain height and normal. Save the scene
after baking. Regenerating with the same seed reproduces the distribution.

## Scripts

C++ scripts can evaluate a saved graph on a flat runtime plane:

```cpp
int count = GenerateScatterGraph(
    "Content/GameAssets/Scatter/Forest.3dgscatter",
    glm::vec3(0.0f), 0);
```

Lua uses the same API:

```lua
local count = Engine.GenerateScatterGraph(
    "Content/GameAssets/Scatter/Forest.3dgscatter", 0, 0, 0, 0)
```

The final argument overrides the seed; zero uses the asset seed. Runtime calls
create model entities and are suitable for moderate procedural layouts. Large
terrain-aware results should be baked to foliage in the editor for batching and
predictable packaged-game performance.
