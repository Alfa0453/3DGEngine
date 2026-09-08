# 3DGEngine Plugin Manager

The Plugin Manager is available from **Panels > Content Editors > Plugin Manager**.
It discovers plugin descriptors in two locations:

- `<Engine>/Plugins` for engine-wide plugins.
- `<Project>/Plugins` for project-specific plugins.

Discovery runs in the background. The manager shows descriptor, dependency, version,
and engine API problems before any project setting is changed.

## Plugin descriptor

Each plugin has one `.3dgplugin` text file in its root folder. Plugin API 1 uses
`key = value` entries:

```ini
id = com.example.gameplay_helpers
name = Gameplay Helpers
version = 1.2.0
author = Example Studio
description = Adds shared gameplay systems and editor utilities.
engine_api = 1
type = both
required = false
enabled_by_default = false
dependencies = com.example.foundation>=1.0.0
optional_dependencies = com.example.debug_tools>=1.1.0
cmake_directory = .
```

`id` may contain letters, numbers, `_`, `-`, and `.`. `type` may be `runtime`,
`editor`, `both`, or `content`. Dependencies are comma-separated and may specify a
minimum version with `>=`.

Native plugins must provide a CMake directory containing `CMakeLists.txt`. API 1 does
not dynamically load a standalone DLL. A prebuilt library therefore needs a companion
CMake file that imports and links it. Declared CMake and binary paths must remain inside
the plugin's own folder.

## CMake integration

The plugin CMake file runs after the engine, game, editor, and player targets exist.
It can add sources or link a separate library to the appropriate targets. For example:

```cmake
add_library(gameplay_helpers STATIC
    src/GameplayHelpers.cpp
)
target_include_directories(gameplay_helpers PUBLIC include)
target_link_libraries(gameplay_helpers PUBLIC engine)

target_link_libraries(game PRIVATE gameplay_helpers)
target_link_libraries(3DGEditor PRIVATE gameplay_helpers)
target_link_libraries(player PRIVATE gameplay_helpers)
```

Plugins should use unique target names and must not modify files outside their own
folder or the active project's generated/build directories.

## Enabling plugins

1. Open a saved project.
2. Open the Plugin Manager and select **Refresh**.
3. Enable or disable plugins.
4. Resolve every error on enabled plugins. Missing optional dependencies remain valid.
5. Select **Apply Changes**.
6. Reconfigure/rebuild the project and restart the editor for native changes.

Settings are stored as `plugins.<id>.enabled` in the project file. The manager writes
the enabled native source directories to
`Intermediate/Plugins/EnabledPlugins.cmake`; the root engine build consumes that file
when `THREEDG_ACTIVE_PROJECT_ROOT` is configured.

Apply is transactional: the project file is backed up, generated integration is
published only after settings succeed, and project settings are restored if publishing
the integration fails.

## Validation

Apply is blocked for enabled plugins with:

- an invalid or duplicate ID;
- a missing version;
- an incompatible `engine_api`;
- a missing native CMake directory or declared binary;
- a missing, disabled, or too-old required dependency;
- a dependency cycle.

Required plugins cannot be disabled. Content-only plugins may omit CMake integration;
their content mounting and registration behavior is defined by that plugin.

## Complete example

`Plugins/GameplayUtilities` is an engine-wide, disabled-by-default example. It includes
the descriptor, CMake target wiring, a public helper library, host bootstrap code, and
four ready-to-attach native scripts. Enable it through the Plugin Manager, apply the
change, reconfigure the active project build, then rebuild and restart the editor.

Linked native plugins register through `engine/plugins/LinkedPlugin.h`. The game module
replays these callbacks whenever script registries are rebuilt, so plugin-provided
classes remain available after project switches and native-script reloads.
