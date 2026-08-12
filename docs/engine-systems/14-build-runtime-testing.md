# Build, Runtime, Export, and Testing

## Build structure

The repository builds several major targets:

- `engine`: reusable runtime library;
- `editor`: authoring application;
- `game`: shared user-script/game module;
- `player`: standalone runtime application;
- test executables registered with CTest.

CMake defines target dependencies and platform libraries. Runtime features
should live in `engine`; authoring-only behavior should live in `editor`.
Gameplay scripts belong to the shared game module so both Editor Play and the
standalone player execute the same code.

## Core dependencies

The engine integrates graphics, window/input, UI, asset import, audio, and
physics libraries through its CMake configuration. Optional capabilities must
degrade safely—for example, GPU particles fall back to CPU and audio device
failure does not prevent editor startup.

## Runtime startup

The standalone player:

1. resolves the project and runtime content paths;
2. loads runtime configuration and game-mode settings;
3. loads the startup scene;
4. loads an optional `.3dgworld` manifest and its persistent level;
5. initializes registered C++ and Lua gameplay scripts;
6. constructs runtime and level-streaming systems;
7. enters the application update/render loop.

Editor Play follows the same runtime rules but starts from an exported clone of
the currently authored scene.

## Export and cooking

Runtime export converts editor-authored state into player-readable data.
Cooking:

- resolves asset references;
- validates dependency closure;
- copies or transforms runtime payloads;
- removes editor-only state;
- writes registry/runtime metadata;
- reports missing assets before packaging.

The player must not depend on source files outside the project after cooking.
Use engine asset identity or project-relative content paths.

## Packaging from the editor

Use **Project > Package Project...** to turn the active project into a
standalone build without leaving the editor. Packaging settings are stored in
the project's `Project.3dgproject` file and include:

- output folder;
- Release, RelWithDebInfo, or Debug configuration;
- optional static C/C++ runtime linking;
- cleaning of the previous staged package;
- optional ZIP creation.

The editor saves the active scene, refreshes the asset registry, cooks the
dependency closure, then starts a background build. The build uses
`Intermediate/Packaging/Build`, so package-specific CMake settings do not
reconfigure the editor's active build directory. The staged runnable folder and
optional ZIP are written beneath the selected output folder. Detailed command
output is recorded in `Intermediate/Packaging/package.log`, while completion or
failure is reported in the editor Console.

## Validation

Validate before export to catch:

- missing asset dependencies;
- invalid scene references;
- unresolved scripts;
- shader compile failures;
- malformed animation, behavior, character, HUD, or particle assets;
- assets that still reference an external import source as their runtime data.

Warnings about absent registry dependencies should be repaired by importing or
reassigning the referenced engine asset, then saving the owner asset.

## Automated tests

The current test targets cover:

| Target | Area |
|---|---|
| `particle_tests` | particle data and simulation behavior |
| `animation_movement_tests` | animation-driven movement |
| `camera_manager_tests` | camera selection and blending |
| `audio_system_tests` | audio types, cues, and runtime behavior |
| `shader_asset_tests` | shader asset serialization/validation |
| `material_shader_tests` | material and shader integration |
| `script_system_tests` | script lifecycle and API |
| `world_streaming_tests` | world manifests, streaming policy, transforms, activation, and unload |
| `runtime_ai_tests` | AI and behavior runtime |
| `image_decode_tests` | texture decode paths, including PNG handling |
| `editor_assets_tests` | editor content classification/routing |
| `asset_registry_tests` | identity and dependency registry |
| `asset_cooker_tests` | runtime cooking |
| `static_mesh_asset_tests` | static mesh native assets |
| `skeletal_asset_tests` | skeletal native assets |

Run the full suite after changing shared serialization or runtime ownership.
Run the focused target while iterating on a single system.

## Runtime diagnostics

The editor exposes:

- CPU and GPU profiler timings;
- engine Console logs;
- asset validation messages;
- script compile and runtime state;
- physics and AI debug overlays;
- particle and audio runtime statistics.

A frame-cost investigation should distinguish:

- scene submission/build time;
- CPU rendering;
- GPU scene rendering;
- editor UI cost;
- physics, animation, AI, particle, and audio updates.

This prevents optimizing the wrong subsystem.

## Extension checklist

When adding a new engine asset or runtime system:

1. define its runtime data and ownership;
2. define serialization and versioning;
3. register its extension/type in asset classification;
4. add registry dependency extraction;
5. add import and reimport if it has source data;
6. add Content-browser double-click routing;
7. add validation and cooking;
8. expose only necessary script APIs;
9. add editor authoring UI;
10. add focused tests;
11. document its runtime ordering and failure behavior.

## Important source files

- root `CMakeLists.txt`
- `engine/CMakeLists.txt`
- `editor/CMakeLists.txt`
- `game/CMakeLists.txt`
- `player/CMakeLists.txt`
- `tests/CMakeLists.txt`
- `engine/include/engine/core/Application.h`
- `engine/include/engine/assets/AssetCooker.h`
- `engine/include/engine/scene/RuntimeSceneLoader.h`
- `engine/include/engine/scene/WorldManifest.h`
- `engine/include/engine/scene/LevelStreamingManager.h`
- `editor/include/RuntimeSceneExporter.h`
