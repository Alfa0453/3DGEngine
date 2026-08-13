# Runtime Property Inspector

Open **Panels > Debug & Diagnostics > Runtime Property Inspector**, then enter
Play mode. The panel reads and edits the isolated Play registry; it never writes
these changes to the authored scene or its assets.

## Inspect live state

1. Select a named or dynamically spawned entity in the left column.
2. Use the entity filter to find an actor, projectile, trigger, or runtime object.
3. Expand a component to compare its **Start** values with its current live values.
4. Fields changed since Play began use an amber highlight. Changed entities are
   highlighted in the entity list as well.
5. Use the property filter to search component and field names such as `health`,
   `hp`, `velocity`, `mana`, `collider`, or `damage`.

The initial supported mutable components are Transform, Health, Rigid Body,
Collider, Linear Velocity, Rotator, Mover, Ability Resources, and Projectile.
Newly spawned entities are discovered automatically and receive a snapshot when
first inspected.

## Pause, edit, and step

- **Pause Play** freezes gameplay and the fixed simulation.
- **Step One Frame** advances one fixed simulation frame while remaining paused.
- Edit a field while paused to examine a precise state or test an alternative value.
- **Reset** beside a field restores only that field.
- **Reset Component** restores the complete component.
- **Reset Entity** restores all supported components to their Play-start snapshot.

If a supported component was added after Play began, resetting it removes that
runtime-only component. Destroyed entities disappear safely from the list.

## Runtime edit history

The panel records the latest field and reset operations for the current Play
session. The history clears automatically when Play ends and can also be cleared
manually. It is diagnostic history, not an undo stack and not saved scene data.
