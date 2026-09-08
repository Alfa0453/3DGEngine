# Navigation Query Tool

Open **Panels > Debug & Diagnostics > Navigation Query Tool**.

## Test a route

1. Rebuild the navigation preview if the level has changed.
2. Enter Start and Goal coordinates, or copy either point from the selected object
   or editor camera.
3. Choose **NavMesh** for polygon/funnel routing. In Play mode, **NavGrid** tests the
   same A* grid used by agents.
4. Press **Run query**.

The result reports reachability, waypoints, horizontal path length, traversal cost,
whether endpoints were on navigation, polygon IDs, connected-region count, and portal
count. Off-navigation endpoints may be snapped by the current lightweight navmesh;
the result calls that out explicitly.

The viewport shows a green start marker, magenta goal marker, cyan route, waypoint
markers, and red markers where live dynamic colliders intersect the route.

## Test an agent size

Set **Agent radius** and **Cell size**, then press **Rebuild for agent**. Static
obstacles are expanded by the radius during the real editor or Play navmesh bake.
Larger agents therefore lose corridors they cannot fit through. Agent height is
available as a planned-clearance value, but the current navigation builder is XZ-only
and does not voxelize overhead clearance; the panel states this limitation.

## Test a proposed navigation link

Expand **Test off-mesh link**, enable it, and set its endpoints, direction, and
traversal cost. If the ordinary route is unreachable, the query attempts the link as
a jump, ladder, or teleport connection and includes its cost. This link is diagnostic
only; it is not saved into the level or used by runtime agents yet.

## Dynamic obstacles

Static collision is included in the navigation bake. Dynamic and kinematic collider
footprints remain live, so after a path is found the tool checks each route segment
against those bodies expanded by the selected agent radius. Intersections are reported
and marked red without changing the baked navigation data.

All analysis runs on demand or while this panel is visible. It performs no work in
packaged games.
