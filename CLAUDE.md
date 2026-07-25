# 3DGEngine — Working Notes for Claude

A from-scratch C++17 / OpenGL 3.3 game engine with a Dear ImGui editor. Read this first
before exploring — it captures the layout, the serialization scheme, and the repeatable
patterns so you don't have to re-derive them each session.

## Build & environment

- **The build sandbox is disabled** — you cannot compile here. Make the edits; the user
  clean-rebuilds and reports errors. Don't add "verify by building" steps.
- **Prefer header-only additions** where possible; adding a `.cpp` forces a CMake
  reconfigure (a new source file must be added to the relevant `CMakeLists.txt`).
- After editing/creating a file, present it and show the changed lines (user preference).

## Project layout

- `engine/` — static lib. Subdirs: `ecs/`, `graphics/`, `animation/`, `physics/`,
  `gameplay/`, `ai/`, `assets/`, `scene/`, `math/`, `audio/`.
- `editor/` — the 3DGEditor (Dear ImGui). Big files: `EditorApp.cpp` (play loop, panel
  wiring, scene render), `EditorScene.cpp` (editor scene + serialization + undo),
  `EditorDockspace.cpp` (most panel UIs + inspector), `CharacterEditorPanel.cpp`.
- `player/` — `RuntimePlayerApp` standalone runtime for packaged builds.
- `game/` — shared game module (script registration). `MaterialMaker/` — material authoring.
- `viewer/`, `wizardscene/`, demos.

## Serialization scheme (the main cost driver — read carefully)

Four text formats, each with a leading `MAGIC <version>` and version-gated reads. Strings
use `std::quoted` with a `"-"` sentinel for empty (helper `StoredPath`).

| Format | Magic | Current ver | Writer / Reader |
|---|---|---|---|
| Editor scene | `3DGEditorScene` | **95** | `EditorScene.cpp` Save / Load |
| Runtime scene | `3DGRuntimeScene` | **67** | `RuntimeSceneExporter.cpp` / `engine RuntimeSceneLoader.cpp` |
| Character asset | `3DG_CHARACTER` | **13** | `CharacterAsset.cpp` Save / Load |
| Animation clip | `3DG_CLIP` | **1** | `AnimationClipAsset.cpp` |
| Animation graph | `3DG_GRAPH` | **3** | `AnimationGraphAsset.cpp` |

Asset-file formats (`.3dgclip`, `.3dggraph`, `.3dgcharacter`, `.3dgmat`) use their own
`MAGIC <version>` + named tail blocks; only editor/runtime **scene** files use the
`version >= N` field-gating scheme. Bump the asset's `version` when adding fields there.

Notes:
- Editor scene / runtime scene: **bump the version AND the max-version check** in the
  loader (`version > N` guard + the "expected 1..N" message).
- New fields are read behind `if (version >= N)`; old files stay loadable.
- Editor scene object record: new tail/variable-length blocks go after the existing ones;
  add a local var in Load, read it, and assign to `m_objects.back()` — and also copy it in
  the **Duplicate** path.
- Character asset uses named tail blocks (`MODEL_OFFSET`, `PLAYER_FACING`, `SOCKETS`,
  `ATTACHMENTS`) gated by `loadedVersion`.

### Checklist: add a field that must reach Play / packaged builds

1. `EditorScene::Object` (`editor/include/EditorScene.h`) — add the field.
2. `EditorScene.cpp` — write it in Save; in Load add a local, read it gated by a **new**
   version, assign to `m_objects.back()`, and copy it in the Duplicate path. Bump the
   `3DGEditorScene` version + the `version > N` check.
3. `RuntimeSceneExporter.cpp` — write it; bump `3DGRuntimeScene`.
4. `engine RuntimeSceneLoader.h` `EntityDesc` + `RuntimeSceneLoader.cpp` — read gated,
   bump the max-version check, and add it to the component **aggregate init** (positional
   `SkinnedModelAsset{...}` — order must match the struct).
5. `engine ecs Components.h` — add to the ecs component if the runtime consumes it.
6. Consume it in `RuntimeAssetManager::ResolveRegistryAssets` and/or the renderers.

### Checklist: add a character-authored field

Also do: `CharacterAsset` (`.h` field + `.cpp` Save/Load tail block + version bump),
`CharacterAsset::Apply` (bake onto the selected object), `CharacterAsset::Capture` (read
back), and the Character Editor UI. Placed characters live-sync via
`object.characterAssetPath` (see below), so Apply is re-run on edit.

## Key systems

- **ECS**: `engine::ecs::Registry`, components in `Components.h` (Transform, MeshPBR,
  MeshRenderer, Collider, RigidBody, Light, ModelAsset, MaterialAsset, SkinnedModelAsset,
  AnimatedModel).
- **Skinned characters**: `SkinnedModel` (skeleton + clips). `AnimatedModel` (component:
  `model*`, `controller`, `pose`, `renderOffset`, `attachments`). `ResolveRegistryAssets`
  (`RuntimeAssetManager.cpp`) turns a `SkinnedModelAsset` into an `AnimatedModel` (loads
  model, merges animation sources, resolves attachments/materials).
- **Render offset**: `MakeModelRenderOffset(pos, euler, scale, center)` in `AnimatedModel.h`
  — a render-only mesh transform that stands up an off-axis rig **without** rotating the
  collider (the object Transform is untouched).
- **Animation asset layering (current model)**: `.3dgclip` (a source FBX + one clip +
  strip/loop/speed) → `.3dggraph` (its own clip list built from `.3dgclip` assets + states
  + params + transitions + blend spaces) → `.3dgcharacter` (rig/collider/controller + an
  `animationGraphPath` reference). **The Character Editor is animation-free** — its Animation
  tab is just a `.3dggraph` picker (the old inline authoring is dead `else if (false)` code).
  Authoring lives in the **Clip Editor** and **Graph Editor** panels (each with a live
  skinned preview: `RuntimeAssetManager` + `SkinnedRenderer` + `Framebuffer`, orbit/zoom).
- **Bake on placement**: `CharacterAsset::Apply` loads the referenced `.3dggraph` and bakes
  its clips into `object.animationSources` and its states/params/transitions onto the object
  via the existing scene setters — so the runtime/scene/packaged pipeline is **unchanged**
  (still `SkinnedModelAsset` → `BuildAnimationController`, clips merged by bone name). No
  graph is set → falls back to the character's legacy inline animation fields.
- **Animation graph runtime**: `AnimationController` (states / params / transitions / 1D & 2D
  blend spaces); `editor::BuildAnimationController` (`AnimationGraphBuilder.h`) maps authored
  data → controller. The play loop (`EditorApp.cpp`, ~`ConfigurePlayPlayerController` region)
  drives parameters each frame: `Speed`, `Direction`, `IsMoving`, `IsStopping`, `IsGrounded`,
  `IsFalling`, `VerticalSpeed`, `Acceleration`, `Deceleration`, `TurnRate`. **Bool/Trigger
  transitions must use `compare == Equal`** (the `GreaterOrEqual` default is always true for
  0/1 values); the Clip/Graph/Character transition UIs now enforce this automatically.
- **Sockets & attachments**: `CharacterSocket` (name + bone + offset) and
  `CharacterAttachment` (model + socket + material) live on the character. On Apply they
  bake into `EditorScene::ModelAttachment` (bone + offset + material), resolved to
  `AnimatedModel::ModelAttachment`. Socket world = `characterMatrix * pose[bone] *
  inverse(bone.offset) * localOffset`. Drawn by `DrawAnimatedModelAttachments`.
- **Player controller**: `PlayerController` (`engine/gameplay`). `facingMode`
  (CameraRelative = strafe, MovementDirection = free-orbit camera) + `turnSpeed`. Wired in
  `EditorApp::ConfigurePlayPlayerController` and `RuntimePlayerApp`.
- **Live scene sync**: a placed character stores `object.characterAssetPath`; when that
  object is selected and edited in the Character Editor, `Apply` is re-run with
  `scene.SuppressUndo(true)` so socket/transform/material edits update the scene in real
  time.
- **Rendering**: `PbrRenderer` (cascaded/point/spot shadows), `SkinnedRenderer`
  (`DrawScene`, `DrawSceneDepth`, plain `Draw`; supports a material tint + albedo override).
  The static model shader convention is `uViewProj / uModel / uNormalMat / uLightPos /
  uLightColor / uViewPos`; `DrawModel(model, shader, tint, albedoOverride)`.

## Editor panels

`EditorPanels::Panel` enum + `Name()` (`editor/EditorPanels.*`). The Panels menu
auto-lists every enum entry. App-owned panels (MaterialMaker, CharacterEditor, ClipEditor,
GraphEditor, BehaviorGraph, ParticleEditor, ShaderEditor, Hud) are `break` cases in the
`EditorDockspace` draw loop and are drawn by `EditorApp::Draw<Name>Panel()` instead. To add
a panel: enum entry + `kDefaultOpen` entry (keep counts equal) + `Name()` case + a
`break` case in the draw switch + an `EditorApp` member/draw method + `CMakeLists.txt`.

## Conventions & gotchas

- The editor viewport holds a **paused idle** for characters by default
  (`m_previewSceneAnimations`, toggle: View → "Animate Characters in Editor"); animation
  only runs in Play.
- PDFs/guides are delivered as **print-ready A4 HTML** in `docs/` (the PDF sandbox is
  unavailable here); the user exports via Ctrl+P → Save as PDF.
- When adding a field to `SkinnedModelAsset` (or any struct built by positional
  aggregate-init in `RuntimeSceneLoader.cpp`), update that init in lockstep — order matters.
