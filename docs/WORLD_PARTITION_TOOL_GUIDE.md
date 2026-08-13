# World Partition Tool

Open **Panels > Level Design > World Partition Tool**. The tool divides a streamed
world into square cells while retaining the existing `.scene` source assets and
`.3dgworld` runtime pipeline.

## Setup

1. Enable World Partition.
2. Choose the cell size and world-grid origin.
3. Set default load and unload ranges. The unload range remains at least as large
   as the load range to prevent rapid border toggling.
4. Click **Assign / Refresh Cells**. Use the second button when every distance-based
   instance should also receive the default ranges.

Linked level instances are assigned from their placed bounds centre. Their source
assets are not copied. Higher streaming-priority values load first when several
cells become eligible during the same frame.

## Loose scene actors

Select actors in the main level, choose a target cell and cell-level name, then use
**Create Cell Level From Selection**. The editor saves those actors under
`Content/GameAssets/WorldCells`, replaces them with a linked streamed level, and
preserves their world-space placement and internal physics joints.

## Preview and budgets

Move **Preview Viewer** to simulate the player position. The yellow marker shows
its cell and the panel reports predicted resident instances. Blue cells are
distance/manual cells, amber cells contain always-loaded content, and green is the
selected cell. The table estimates source bytes, objects, and triangles when that
metadata is available in the editor scene.

Active data-layer checkboxes determine which authored groups may stream. Disabled
layers unload at runtime. Validation reports stale cell assignments, oversized
level bounds, duplicate placements, and load distances that can leave gaps at cell
corners. Save the world and use the existing World Editor cooker for packaging.
