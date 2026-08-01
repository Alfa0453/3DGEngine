# Editor

## Purpose

The editor is the authoring host for projects, scenes, engine-owned assets,
components, scripts, characters, AI, animation, particles, shaders, audio,
cameras, and runtime preview.

## Editor shell

`EditorApp` coordinates the window, renderer, scene, panels, project state,
input, play mode, and asset tools. `EditorDockspace` builds the ImGui docking
layout. The viewport remains the central scene view while tool panels can be
docked, floated, shown, or hidden.

The main editor state is split among:

- `EditorProject`: project paths and project settings;
- `EditorScene`: editable objects and authored component data;
- `EditorRuntimeController`: Play/Stop lifetime and runtime preview;
- `EditorContentController`: content browser operations and asset opening;
- `EditorAssets`: content classification and editor asset helpers;
- `EditorViewport`: viewport bounds, focus, picking, and render presentation.

## Scene interaction

The scene tools include:

- hierarchy selection and filtering;
- inspector component editing;
- transform gizmos for move, rotate, and scale;
- world/local orientation;
- snapping;
- camera navigation and framing;
- drag-and-drop asset placement;
- line-based collision, socket, navigation, and debug overlays.

Model import orientation belongs to render/model transform data. Scene
translation, rotation, and scale remain the object’s world transform. This
separation prevents a model-axis correction from changing gizmo directions.

## Project and scene workflow

Projects define a `Content` root. Scenes are stored beneath Content, normally in
`Content/Scenes`. Save overwrites the active scene; Save As creates a new
scene. The last successfully saved scene is recorded and restored when the
project is reopened.

Play mode creates a runtime representation from the authored scene. Stopping
Play restores the editor state rather than retaining transient runtime changes.

## Content browser

The Assets panel supports:

- folder navigation and creation;
- rename, copy, cut, paste, delete;
- system file browsing for imports;
- selection of the destination project folder;
- import and reimport;
- display names without exposing the entire absolute path;
- asset-specific icons and classifications;
- double-click routing to the correct editor.

Engine-owned asset metadata allows imports to remain stable even if the source
file is moved outside the project. Registry identity and dependencies are used
by authoring tools and cooked builds.

## Asset opening

Double-clicking an asset should route as follows:

| Asset | Panel |
|---|---|
| scene | Scene loader |
| material | Material Maker |
| shader or shader graph | Shader Editor |
| particle | Particle Editor |
| HUD | HUD Editor |
| character | Character Editor |
| behavior tree graph | Behavior Graph |
| animation graph | Graph Editor |
| action clip | Clip Editor |
| prefab | Prefab tools |
| audio/cue | Audio Editor |

## Inspector

The Inspector displays only properties relevant to the selected object and its
components. Rendering and animation sections must follow the selected asset
type—for example, skeletal animation preview controls are hidden for static
meshes and primitives.

Components are added through the Add Component workflow. Empty objects provide
a transform and component host without a visible primitive.

## Specialized panels

### World and game

- **World Settings:** atmosphere, sky, clouds, lighting, shadows,
  post-processing, and debug visibility distances.
- **Game Mode Settings:** player, gameplay HUD, startup behavior, and a camera
  mode chosen before Play.
- **Camera Manager:** saved cameras, shake preview, camera zones, and sequences.

### Visual content

- **Material Maker:** PBR materials and engine texture selection.
- **Shader Editor:** typed node graph, compilation, parameters, textures, and
  domain output.
- **Particle Editor:** isolated effect preview and module stack.
- **HUD Editor:** widget layout, bindings, and actions.

### Character and animation

- **Character Editor:** model, materials, collider, movement, camera, animation
  graph, sockets, attachments, scripts, and AI configuration.
- **Graph Editor:** animation states, blend spaces, parameters, and multi-term
  transitions.
- **Clip Editor:** standalone action clips, playback rules, and animation events.
- **Animation Preview:** direct clip/model inspection and diagnostics.

Character preview transforms are asset-level render corrections. When a
character is placed in a scene, its scene transform is edited independently.

### AI

- **Behavior Graph:** behavior-tree nodes, decorators, services, tasks,
  blackboard, integrated BT scripts, save/overwrite, pan, and zoom.
- **Gameplay Debug:** AI state, perception, navigation, physics, and gameplay
  overlays.

### Audio

- **Audio Editor:** source, waveform, cue, and adaptive-music authoring.
- **Audio Mixer:** buses, effects, snapshots, and runtime monitoring.

### Engineering and diagnostics

- **Console:** engine and editor log.
- **Profiler:** CPU/GPU frame costs.
- **Physics Status:** bodies, contacts, and simulation status.
- **Script API:** available script-facing operations.
- **Script Debug:** attached scripts, compile/runtime state, and errors.

## Script authoring

The editor stores user-facing scripts in `Content/Scripts` while integrating
them with the shared game module. Script creation:

1. validates a class name;
2. generates the selected template;
3. updates generated registration/includes;
4. exposes the script in saved-script dropdowns;
5. invokes the project compiler;
6. reports actionable errors in the Console.

Objects and character assets can attach multiple scripts. Attach operations
must respect object locking and show why an attachment was rejected.

External editor choices include Visual Studio Code, Visual Studio, and Rider.

## Debug drawing

Editor guides use line rendering instead of temporary cube meshes. The global
and per-system debug toggles cover:

- colliders;
- physics contacts;
- navigation areas;
- AI paths and vision;
- sockets and attachments;
- camera rails and volumes.

Debug guides should not appear in gameplay unless their Play-mode toggle is
enabled.

## Important source files

- `editor/include/EditorApp.h`
- `editor/include/EditorProject.h`
- `editor/include/EditorScene.h`
- `editor/include/EditorRuntimeController.h`
- `editor/include/EditorContentController.h`
- `editor/include/EditorPanels.h`
- `editor/include/EditorViewport.h`
- `editor/include/EditorGizmo.h`
- `editor/include/EditorLineRenderer.h`

