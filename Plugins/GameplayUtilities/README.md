# Gameplay Utilities Example Plugin

This engine-wide Plugin API 1 example demonstrates:

- `.3dgplugin` discovery and validation;
- per-project enable/disable settings;
- CMake build integration;
- a small public C++ utility library;
- native scripts registered in both Editor Play and the standalone player;
- registration that survives project switches and script-registry reloads.

## Enable it

1. Open a saved project in 3DG Editor.
2. Open **Panels > Content Editors > Plugin Manager**.
3. Select **Refresh** and enable **Gameplay Utilities Example**.
4. Select **Apply Changes**.
5. Reconfigure the engine build with that project as `THREEDG_ACTIVE_PROJECT_ROOT`.
6. Rebuild and restart the editor.

The following class names then appear as **Registered** entries in the Inspector's
**Saved Script** dropdown:

| Script | Purpose | Optional Inspector fields |
|---|---|---|
| `GU_AutoRotate` | Rotates an object around world Y. | `degreesPerSecond` (Float, default 90) |
| `GU_HealthRegeneration` | Restores an object's Health after taking damage. | `healthPerSecond` (Float, 5), `delayAfterDamage` (Float, 2) |
| `GU_DestroyAfter` | Destroys the attached object after a delay. | `lifetime` (Float, 3) |
| `GU_Bobbing` | Moves an object smoothly up and down. | `amplitude` (Float, 0.25), `frequency` (Float, 1) |

Attach a class by its exact name. Add any optional fields through the Inspector; every
script has safe defaults when fields are omitted.

## Use the public helpers

Project native scripts can include:

```cpp
#include <GameplayUtilities/GameplayUtilities.h>
```

Available examples include `Saturate`, `HealthRatio`, `Heal`, `MoveTowards`, and
`RotateWorldY`. The plugin links its public include directory and implementation into
the game, project script module, editor, and standalone player targets.

## Folder anatomy

- `GameplayUtilities.3dgplugin` contains discovery metadata.
- `CMakeLists.txt` connects the plugin to engine targets.
- `include/GameplayUtilities` is the plugin's public API.
- `src/GameplayUtilities.cpp` implements reusable helpers.
- `src/GameplayUtilitiesBootstrap.cpp` registers ready-to-attach scripts.

Disabling the plugin and applying changes removes its build integration from that
project after the next reconfigure, rebuild, and restart.
