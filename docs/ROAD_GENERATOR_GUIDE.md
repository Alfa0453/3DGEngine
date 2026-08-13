# Road Generator

Open **Panels > Level Design > Road Generator**. The generator turns an editor
spline into a reusable road while keeping every generated part editable.

## Workflow

1. Add and shape a spline in the level.
2. Select that spline in the Road Generator.
3. Choose Basic, Country, City, or Highway as a starting style.
4. Set road width, curve resolution, lanes, shoulders, markings, curbs,
   sidewalks, barriers, and end caps.
5. Optionally enable **Conform to Terrain** and set a Surface Offset.
6. Assign engine materials for each surface.
7. Save the `.3dgroad` asset and press **Generate / Rebuild**.

Road assets are saved under `Content/GameAssets/Roads`. Double-clicking one in
the Content browser opens this panel.

## Non-Destructive Rebuilding

Generated objects use `Road_<RoadName>_`. Rebuilding removes only the previous
objects with that prefix. The source spline and unrelated scene content remain
untouched. Generated pieces work with level layers, prefabs, streamed levels,
alignment, and normal transform editing.

## Curve Quality and Performance

**Curve Resolution** is the approximate length of each generated span. Smaller
values make bends smoother but create more objects and draw calls. Start around
1.5 metres and reduce it only on tight curves. Use the Optimization Auditor after
generating long roads.

## Terrain and Intersections

Terrain conformation samples the landscape beneath every generated piece. For a
junction, create intersecting splines and cover the center with modular junction
meshes. End caps can be disabled when attaching intersection meshes.
