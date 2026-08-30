# Portal and Teleport Tool

The Portal and Teleport Tool creates reusable `.3dgportal` assets. Open it from
**Panels > Level Design > Portal and Teleport Tool**, choose a mode and destination,
save, select an object in the level, then press **Apply to Selected**.

## Portal modes

- **Same-Level Teleport** moves an actor to a named destination object. The arrival
  offset is evaluated in that destination's local space.
- **Level Transition** requests a packaged runtime scene load.
- **Seamless Door** uses the same transition request while allowing a game-specific
  streaming or transition presentation.

Safe Arrival keeps the actor slightly above the destination. Align Facing applies
the destination orientation plus the authored arrival rotation. Preserve Velocity
is useful for launch portals; leave it disabled for ordinary doors.

## Script activation

Native C++ scripts call:

```cpp
const auto portal = FindObject("DungeonPortal");
if (IsPortalReady(portal)) UsePortal(portal, "DungeonKey");
```

Lua scripts call:

```lua
if Engine.IsPortalReady("DungeonPortal") then
    Engine.UsePortal("DungeonPortal", "DungeonKey")
end
```

Portal cooldown prevents repeated activation. A missing access tag or destination
fails safely without moving the actor. Portal assets, references, and dependencies
are preserved by editor scenes and packaged runtime scenes.
