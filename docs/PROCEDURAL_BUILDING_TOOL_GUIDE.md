# Procedural Building Tool

The Procedural Building Tool creates reusable, editable buildings from a closed
footprint. Open it from **Panels > Level Design > Procedural Building Tool**.

## Quick Start

1. Choose **Basic**, **Cottage**, **Town House**, or **Tower**.
2. Drag the numbered corners in the footprint preview, or edit their X/Z values.
3. Set the storey count, storey height, wall thickness, floors, ceilings, roof,
   columns, and collision.
4. Add doors, windows, arches, or stairwell openings. A wall segment is the edge
   beginning at the numbered footprint corner; storeys are numbered from zero in
   opening settings.
5. Assign engine materials independently to walls, floors/ceilings, and the roof.
6. Press **Save**. The asset is stored under
   `Content/GameAssets/Buildings/<Name>.3dgbuilding`.
7. Press **Generate / Rebuild in Level**.

The blue wireframe in the level viewport shows the current footprint and total
height while the panel is open.

## Non-Destructive Regeneration

Generated pieces use the prefix `Building_<Name>_`. Rebuilding removes only
pieces with that prefix and creates the updated shell. Other scene objects are
not touched. Keep **Replace Existing** enabled for the normal iterative workflow.

Each wall, floor, ceiling, column, and roof section remains an ordinary scene
object after generation. This means it can be selected, edited, duplicated, put
into a level layer, captured by the Prefab Editor, or moved into a streamed level
with the existing level tools.

## Openings

- **Door** removes a full-height opening from floor level and creates a lintel.
- **Window** creates sill and lintel pieces around its opening.
- **Arch** reserves a door-like opening suitable for placing a modular arch mesh.
- **Stairwell** splits the selected storey's floor into pieces around a rectangular
  opening. Width and Opening Depth control the hole.

Opening positions use a normalized **Along Wall** value: `0` is the segment's
first corner, `0.5` is its middle, and `1` is its final corner.

## Baking and Reuse

To make a reusable detailed building, generate it, add modular door/window/roof
meshes with the Modular Placement tool, select the completed pieces, then capture
them in the Prefab Editor. For a large building, place its scene objects in a
dedicated level layer or streamed level instance.

## Current Geometry Notes

Walls follow any valid closed footprint. Floor, ceiling, roof, and stairwell slabs
use the footprint's rectangular bounds, so rectangular and convex footprints give
the cleanest enclosed results. Custom roof shapes and decorative modular meshes
can be added after generation without altering the saved procedural definition.
