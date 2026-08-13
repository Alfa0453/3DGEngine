# Biome Editor Guide

The Biome Editor creates reusable `.3dgbiome` assets that can populate and dress
a landscape consistently. Open it from **Panels > Level Design > Biome Editor**,
or double-click a biome asset in Content.

## Create a biome

1. Press **New** and name the biome.
2. Set the deterministic seed, preview size, population limit, transition width,
   moisture, and temperature.
3. Add up to five terrain layers. Choose an engine material for each layer and
   set its normalized height, slope, moisture, and temperature ranges.
4. Add foliage rules. Choose an engine mesh, weight, density, scale range,
   surface filters, normal alignment, and shadow behavior.
5. Optionally assign engine weather, water material, particle, and ambient-audio
   assets.
6. Inspect the coverage and placement preview, then save the biome under Content.

All surface ranges are normalized from `0` to `1`; slope is normalized from flat
to vertical. Overlapping rules create natural transitions. The transition-width
setting controls how broadly neighboring biome rules feather into one another.

## Apply to a landscape

Select a landscape in the level and press **Apply to Selected Landscape**. The
editor applies the terrain layer materials and weather, then deterministically
creates the biome's foliage, water, particle, and ambient-audio actors. Applying
the same biome again replaces its previously generated actors instead of stacking
duplicates. Save the biome before applying it.

## Script usage

C++:

```cpp
const int spawned = GenerateBiome(
    "Content/GameAssets/Biomes/Forest.3dgbiome",
    glm::vec3(0.0f),
    0); // zero uses the asset seed
```

Lua:

```lua
local spawned = Engine.GenerateBiome(
    "Content/GameAssets/Biomes/Forest.3dgbiome",
    0.0, 0.0, 0.0,
    0)
```

Runtime generation creates the deterministic population from the saved biome.
For final landscapes, applying and saving the level in the editor is preferred so
the generated foliage can use the normal batched foliage path.
