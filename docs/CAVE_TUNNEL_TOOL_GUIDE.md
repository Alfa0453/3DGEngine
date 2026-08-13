# Cave and Tunnel Tool

The Cave and Tunnel Tool turns an editor spline into a reusable, engine-owned
`.3dgcave` asset and a baked `.3dgmesh` interior. Open it from **Panels > Level
Design > Cave and Tunnel Tool**.

## Author a cave

1. Create a spline in the level and edit its points into the tunnel centerline.
2. Open the tool, choose the spline, then select **Capture Spline**.
3. Set width, height, wall thickness, curve resolution, and radial segments.
4. Add chambers and choose which captured control point each chamber expands around.
5. Leave **End Caps** off for an open passage. Turn it on for a sealed tunnel or
   dead-end prototype.
6. Assign engine-owned materials. The wall material is applied to the generated
   interior; floor, ceiling, and trim references remain in the cave asset for
   layered cave shaders.
7. Enable collision and navigation floor generation as needed. **Terrain Entrances**
   snaps both open ends close to the landscape surface when building.
8. Save, then choose **Build / Rebuild**. Rebuilding preserves the baked mesh asset
   identity and replaces the previous generated scene objects.

The visible interior is one smooth inward-facing mesh. Collision is generated as
hidden floor, ceiling, left-wall, and right-wall strips, so the tunnel remains
walkable. Its floor can be included in normal navmesh generation.

Double-click a `.3dgcave` file in Content to reopen it. **Delete Generated** removes
the baked level objects without deleting the reusable cave asset.

## Runtime scripting

C++:

```cpp
const auto cave = SpawnCave("Content/GameAssets/Caves/Mine.3dgcave",
                            glm::vec3(0.0f));
```

Lua:

```lua
local cave = SpawnCave("Content/GameAssets/Caves/Mine.3dgcave", 0, 0, 0)
```

`SpawnCave` creates the baked visual mesh. For a playable level, build the cave in
the editor and save the generated collision/navigation pieces with the scene.
