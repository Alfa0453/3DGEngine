# Level Instance Tool

Open **Panels > Level Design > Level Instance Tool**. The tool reuses the engine's
streamed-world references, so editor instances and packaged-game streaming use the
same source level, placement, and load rules.

## Create a reusable level from a selection

1. Select one or more scene objects.
2. Enter an asset name under **Create From Selection**.
3. Leave **Replace selected objects with instance** enabled for a true conversion.
4. Click **Create Linked Level From Selection**.

The source scene is saved under `Content/GameAssets/Levels`. Its objects are stored
relative to the selection centre and a linked instance preserves their world-space
placement. Internal physics joints between selected objects are retained.

## Place and stream instances

Use **Add Instance**, choose a source level from the searchable project list, then
edit Location, Rotation, and Scale. Choose one streaming rule:

- **Distance** loads near the instance bounds and unloads farther away.
- **Always Loaded** keeps the instance resident.
- **Manual** lets gameplay scripts or transitions control residency.

Duplicate creates another lightweight placement linked to the same source. Use
**Open Source** to edit the source scene; all placements read the updated scene.
Use **Break Into Editable Objects** to merge a placement into the current scene
while preserving its world transform and resolving duplicate object names.

The Validation section reports missing sources, current/persistent-level self
references, and invalid streaming hysteresis before the world is cooked.
