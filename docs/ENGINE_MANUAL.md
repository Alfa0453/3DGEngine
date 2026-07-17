# 3DGEngine — Engine Manual

A from-scratch C++17 / OpenGL 3.3 game engine: a physically-based renderer, a
data-oriented ECS, a deterministic physics solver, behaviour-tree AI, skeletal
animation, audio, a full Dear ImGui editor, and a standalone runtime player that
ships editor-authored scenes as a game.

This manual is a reference to the engine's architecture and systems. For a
hands-on walkthrough, see **[TUTORIAL_FirstGame.md](TUTORIAL_FirstGame.md)**.

---

## 1. Architecture

The project is split into a reusable **engine** static library plus several
executables that link it:

```
engine/         the reusable engine (static library)  ── everything below builds on this
editor/         the Dear ImGui scene editor ("3DGEditor")
player/         the standalone runtime player (ships a scene as a game)
character/ ecsgame/ navdemo/ terraindemo/ ...   focused demos, one behaviour each
tests/          unit tests
```

The golden rule: **engine code never depends on editor code.** The editor and the
player both consume the engine; the player links *only* the engine (no ImGui), which
is what proves the engine and editor are separable. Gameplay lives in scripts and
ECS systems, so the same simulation runs identically in the editor's Play mode and
in the standalone player.

Dependencies (GLFW, GLM, Assimp, Dear ImGui, GLAD) are fetched and built
automatically by CMake `FetchContent` — no system installs required.

### Building

```bash
cmake -S . -B build
cmake --build build
# executables land in build/bin/
```

Useful options:

- `-DGAMEENGINE_STATIC_RUNTIME=ON` — statically link the C/C++ runtime so a packaged
  build needs no redistributable.
- `-DPLAYER_GAME_DIR=<path>` — bundle a folder of exported game content beside the
  player when installing (see §16).

---

## 2. The application loop

Every executable subclasses `engine::Application`, which owns the window and runs a
fixed-timestep loop. You override only the hooks you need:

```cpp
class MyGame : public engine::Application {
protected:
    void OnInit()               override;   // load resources (GL context exists here)
    void OnUpdate(float dt)     override;   // once per frame, variable dt: input, camera
    void OnFixedUpdate(float h) override;   // 0+ times/frame at a FIXED step: physics, gameplay
    void OnRender()             override;   // draw the frame
    void OnShutdown()           override;   // release resources
};
```

The loop each frame is: `OnUpdate(frameTime)` → accumulate → `OnFixedUpdate(h)` zero or
more times at the fixed step (default **1/120 s**) → `OnRender()`. Putting physics and
gameplay in `OnFixedUpdate` makes the simulation deterministic and frame-rate
independent; `InterpolationAlpha()` gives the render-time blend factor between fixed
states.

Supporting core types: `engine::Window` (GLFW wrapper: input, cursor capture, vsync),
`engine::Config` (`key = value` files, `GetInt/GetBool/GetString`), and
`engine::ExecutableDir()` (locate assets beside the binary so packaged builds run from
any working directory).

---

## 3. The ECS

State lives in an `engine::ecs::Registry`: entities are ids, components are plain
structs, and systems are free functions over the registry.

```cpp
engine::ecs::Registry reg;
engine::ecs::Entity e = reg.Create();
reg.Add<Transform>(e, Transform{});
reg.Add<MeshPBR>(e, MeshPBR{&mesh, material});

reg.view<Transform, MeshPBR>().each([](Entity e, Transform& t, MeshPBR& m) { /* ... */ });

if (Transform* t = reg.TryGet<Transform>(e)) t->position.y += 1.0f;
reg.Has<Health>(e);   reg.Get<Health>(e);   reg.Remove<Collider>(e);
```

Core components (`engine/ecs/Components.h`): `Transform` (position / scale / rotation
quaternion), `MeshRenderer` (mesh + flat colour — the editor's authoring form),
`MeshPBR` (mesh + `PbrMaterial` — the PBR renderer's form), `Light`, `RuntimeName`,
plus asset-reference handles (`ModelAsset`, `SkinnedModelAsset`, `MaterialAsset`) and
their resolved forms (`LoadedModelAsset`, `LoadedMaterialAsset`, `AnimatedModel`).

---

## 4. Rendering

The renderer is a forward physically-based (Cook-Torrance) pipeline.

**`engine::PbrRenderer`** draws every `Transform + MeshPBR` entity plus `Light`
entities. One call renders the shadow passes and the lit scene:

```cpp
engine::PbrRenderer::Options opt;
opt.ambient       = sample.ambient;
opt.ibl           = &ibl;               // image-based ambient (optional)
opt.fog           = true; opt.fogDensity = 0.01f;
opt.directionalShadows = true;          // cascaded sun shadows (default on)
opt.pointShadows  = true; opt.spotShadows = true;
opt.shadowCasters = [&](const glm::mat4& lightVP){ skinned.DrawSceneDepth(reg, lightVP); };
pbr.Render(reg, camera, aspect, width, height, opt);
```

`PbrMaterial` is a full metallic-roughness material (albedo, metallic, roughness, ao,
emissive, normal/metal-rough/height maps, clearcoat, transmission, sheen, blend mode,
uv transform). Lights are `Directional`, `Point`, `Spot`, or `Area`.

Other rendering systems:

- **`ProceduralSky` + `DayNightCycle`** — an analytic sky; `DayNightCycle::At(timeOfDay)`
  returns a `Sample` with the sun direction/colour, ambient, and gradient colours that
  drive lighting and fog for a time of day.
- **`IBL`** — bakes an irradiance/prefilter environment from the sky for ambient light
  and reflections.
- **Shadows** — cascaded shadow maps for the sun, plus omnidirectional point and
  perspective spot shadows.
- **`PostProcess`** — HDR pipeline with ACES tonemapping, bloom, FXAA, and a
  render-scale control; `BeginScene()` / `RenderToScreen()` bracket the 3D pass.
- **`SSAO`, `SSR`** — screen-space ambient occlusion and reflections.
- **`SkinnedRenderer`** — GPU-skinned `AnimatedModel` characters, lit to match the PBR
  world (`DrawScene` with a `SkinnedLighting` context; `DrawSceneDepth` for shadows).
- **`RenderLoadedModels` (ecs/Systems.h)** — draws imported multi-mesh models
  (`LoadedModelAsset`) with a caller-bound shader.
- **`Terrain`** — an fBm heightmap meshed with height/slope/paint albedo, walkable via a
  surface-height query.
- **Particles** — `ParticleRenderer` with CPU and GPU particle systems, driven by
  `ParticleSystemComponent` / `ParticleEffectComponent`.
- **`TextRenderer`** — a built-in bitmap-font 2D layer (`Text`, `FillRect`, `Image`) used
  for overlays and the HUD.

---

## 5. Physics

**`engine::PhysicsWorld::Step(registry, dt)`** integrates every `RigidBody`, detects and
resolves contacts, and solves joints. Call it from `OnFixedUpdate`.

- **`RigidBody`** — velocity, `invMass` (0 = immovable), gravity toggle, linear/angular
  damping, **CCD** (swept anti-tunnelling), full angular dynamics (inertia tensors for
  box/sphere/capsule), sleeping, and a **kinematic** mode (moved by its own velocity,
  pushes dynamics but is never pushed — moving platforms, elevators).
- **`Collider`** — Sphere, Plane, Box, Capsule, Cylinder, Cone, Pyramid, Torus,
  Staircase; per-surface `restitution` + `friction`; `isTrigger` (overlap-only); and
  **collision layers/masks** (`layer` + `mask` bitfields filter which pairs interact).
- **Solver** — warm-started, accumulating sequential impulses with two-axis Coulomb
  friction and a spatial-hash broad phase; stacks settle without relying on damping.
- **Joints** — Distance (rigid or rope), Spring, Ball, Hinge, with world or body anchors.
- **Queries** — `Raycast`, `SphereCast`, `OverlapSphere`, and `ApplyRadialImpulse`
  (explosions), all layer-filterable.
- **Contact events** — `Events()` returns Enter / Stay / Exit events per pair, each with
  contact point, normal, and impulse magnitude (scale hit sounds/damage by impact).
- **`CharacterController`** — a kinematic capsule with slope limits and step-up, the basis
  of `PlayerController`.

---

## 6. Gameplay

**Scripts.** Attach a `NativeScriptComponent` (by class name) to an entity; write the
class as an `engine::Script` subclass with `OnCreate` / `OnUpdate(dt)` /
`OnFixedUpdate(dt)` / `OnDestroy` and a typed `ScriptContext` for reaching the entity,
its components, input, and audio. Register the class in the global registry so the name
resolves:

```cpp
engine::ScriptRegistry::Instance().Register("Coin", [] { return std::make_unique<Coin>(); });
```

`engine::UpdateScripts(reg, dt)` creates instances and runs `OnUpdate`;
`engine::FixedUpdateScripts(reg, h)` runs `OnFixedUpdate`; `engine::ShutdownScripts(reg)`
tears them down.

**Built-in gameplay systems** (`GameplaySystems.h`, `ecs/RuntimeSystems.h`):
`UpdateGameplay` (Rotators + Movers), `UpdateHealth` (flips `alive` / `justDied`),
`UpdateRuntimeMotion` (linear/angular velocity), `UpdateProjectiles`, `UpdateAttachments`.

**`PlayerController`** — a ready-made first/third-person player: a kinematic capsule
driven by camera-relative movement, mouse-look, jump/sprint, a spring-arm camera with
collision, shoulder offset, and lock-on. Feed it a `PlayerInput` each fixed step and read
`CameraPosition()` / `CameraTarget()` / `CapsuleTransform()` for rendering.

**`GameMode`** — a process-global game-rules singleton (`engine::GameMode::Instance()`).
It tracks a `GameState` (Playing / Paused / GameOver / Victory), a score, and a play
clock, and has a built-in "lose when the player dies" rule. Scripts drive it directly:

```cpp
engine::GameMode::Instance().AddScore(100);
if (allCoinsCollected) engine::GameMode::Instance().Win("Level Clear!");
```

The runtime calls `Update(reg, playerEntity, dt)` each step, freezes the simulation once
the state isn't Playing, and reads `State()` / `Score()` / `Message()` for the HUD and
end screen.

---

## 7. AI

Data-driven **behaviour trees** built in the editor's node graph and stored as
`.btgraph`. A `BehaviorTree<AgentContext>` runs over an `AgentContext` that carries a
typed **Blackboard** (bool/int/float/vec3/string/entity). Nodes include composites
(sequence/selector/parallel), decorators, conditions (e.g. `HealthBelow`, `TargetDead`),
and **script nodes** — native C++ tasks/decorators/services registered with
`engine::ai::BtScriptRegistry::Instance()` (built-ins via `RegisterExampleBtScripts()`).

Supporting AI: `AiAgent` (built-in patrol/chase/search), perception, steering, A*
pathfinding over a baked **NavGrid** or funnel-smoothed **NavMesh**, and faction/team
targeting.

---

## 8. Animation

Skeletal animation with GPU skinning. An `AnimatedModel` component holds the skinned
model, the current pose, and an **`AnimationController`** — a state machine with states,
parameters, transitions (with conditions, fade, exit time, priority, interrupt),
1-D blend spaces, root motion, layered action clips (upper-body masks), and animation
**events** (notifies) that fire callbacks at clip times. `engine::UpdateAnimations(reg, dt)`
advances every controller and refills the bone matrices the `SkinnedRenderer` consumes.

---

## 9. Audio

`engine::AudioEngine` (a miniaudio backend) plus `engine::RuntimeAudioSystem`, which
observes the registry: `AudioSource` components play spatialised sounds, and
`TriggerAudioAction` plays sounds on collision/trigger enter/exit by consuming the
physics `Events()`. A mixer, music, and one-shot playback round it out.

---

## 10. Camera & cinematics

`engine::Camera` (view/projection, look-at) with `CameraBlend`, `CameraShake`,
`CameraSequence` (keyframed camera shots with easing and timeline events),
`CameraDirector` (orchestrates gameplay vs cinematic cameras, input locking, skippable
cutscenes), and cinematic cues (play animation clips, sounds, etc. along a sequence).

---

## 11. HUD & UI

A data-driven, UMG-style HUD. A `HudDocument` is a list of widgets — **Text**, **Bar**,
**Button**, **Image**, **Panel** — positioned by a 9-point anchor + pixel offset, scaled
to the window. Widgets bind to live values through a `HudContext`:

- health (fraction for bars, `hp/max` text),
- named floats/strings (`score`, `time`, `gamestate`, `gamemessage`, `fps`, …),
- images resolved through the asset manager,
- buttons that fire actions (Exit / Restart / Emit Event) when the cursor is free.

`engine::DrawHud(text, doc, ctx, w, h)` draws it in one call. Authoring is the editor's
**HUD Editor** panel (drag widgets on a canvas, edit properties, pick images from the
content folder), and the HUD is saved as a reusable `.hud` file referenced by the scene.

---

## 12. Assets

`engine::RuntimeAssetManager` loads and caches models, skinned models, textures,
materials, and shaders by path, and — crucially — `ResolveRegistryAssets(registry)`
resolves a scene's authored asset **references** (`ModelAsset` / `SkinnedModelAsset` /
`MaterialAsset`) into loaded GPU assets (`LoadedModelAsset` / `LoadedMaterialAsset` /
`AnimatedModel`) in one pass. Materials (`.mat`) are authored in the editor's **Material
Maker**; custom shaders are authored in the **Shader Editor**.

---

## 13. Scenes

Two scene formats:

- **`3DGEditorScene`** — the editor's rich, versioned save format (objects, lights,
  terrain, cameras, environment, HUD reference, physics settings, per-object
  scripts/animation/audio/particles).
- **`3DGRuntimeScene`** — a slimmer runtime format the editor **exports** (menu / F7) via
  `RuntimeSceneExporter`. The engine loads it with `RuntimeSceneLoader::Load()` and
  `Instantiate(scene, registry, meshes)`, which populates the registry with transforms,
  meshes, lights (including the environment sun), physics, scripts, particles, audio,
  animation, player-controller settings, camera presets/sequences, and the HUD reference.

`Instantiate` produces `MeshRenderer` + asset-reference components; call
`ResolveRegistryAssets` to turn them into render-ready assets (§12).

---

## 14. The editor

`3DGEditor` is the authoring tool: a viewport with gizmos, a hierarchy and inspector,
undo/redo, an asset browser, and specialised panels — **World Settings** (environment,
shadows, AA, render scale, physics), **Material Maker**, **Shader Editor**, **Behavior
Graph**, **Particle Editor**, **HUD Editor**, **Camera Manager**, **Audio Editor/Mixer**,
a **Profiler** (GPU + CPU timings), and physics/gameplay debug overlays. **Play mode**
builds a runtime registry and ticks the same engine systems the standalone player uses,
so what you test is what you ship. **Export** writes the runtime scene the player runs.

---

## 15. The runtime player

`player/` is a thin `engine::Application` that boots an exported scene and runs it with
no editor and no ImGui — the "game mode." It loads the scene, resolves assets, renders
(materials, static models, skinned characters, particles), simulates (physics, scripts,
AI, animation), plays (player controller, camera/cinematics, audio, HUD), and enforces
`GameMode` rules. Scene selection: `player <scene>` or the `player.scene` key in
`player.cfg` (paths resolve next to the executable).

**The game module.** Scenes store scripts by class name, so the classes must be compiled
and registered. This lives in the shared **`game/`** static library that *both* the editor
and the player link — write a script once and it runs in Play mode and in the shipped game
(the Unreal "game module" pattern). Put script headers in `game/include/game/scripts/` and
register them in `game/src/GameModule.cpp`:

```cpp
void RegisterGameModule() {
    engine::ScriptRegistry::Instance().Register("Coin", [] { return std::make_unique<Coin>(); });
    // engine::ai::BtScriptRegistry::Instance().Register("ChaseAndShoot", ...);
}
```

Both the editor and the player call `RegisterGameModule()` at startup; the player shows an
on-screen count of any scene scripts that aren't registered. Develop it like any C++
project: open the folder in Visual Studio (native CMake) or generate a solution with
`cmake -S . -B build-vs -G "Visual Studio 17 2022"`, then F5-debug with breakpoints in
your scripts.

---

## 16. Packaging

Ship a standalone folder / zip with CMake install + CPack (only the `player` component is
packaged, not the editor or demos):

```bash
cmake -S . -B build -DPLAYER_GAME_DIR=E:/path/to/exported_game -DGAMEENGINE_STATIC_RUNTIME=ON
cmake --build build --target player
cmake --install build --prefix dist --component player   # -> player[.exe] + player.cfg + game/
cd build && cpack                                         # -> 3DGPlayer-<ver>-<os>.zip
```

The result is `player[.exe]` + `player.cfg` + your `game/` content, self-contained
because the engine and its dependencies are static libraries.

---

## 17. Extending the engine

- **New gameplay** → write an `engine::Script` subclass and register it; or add a
  component + a system (a free function over the registry) and call it from the loop.
- **New AI behaviour** → author a behaviour tree, or write a `BtScript` task/decorator/
  service and register it.
- **New material/shader** → author in Material Maker / Shader Editor; the PBR path and
  `RenderLoadedModels` honour custom shaders + parameters per material.
- **New game rules** → drive `engine::GameMode` from scripts (score, win/lose, state).

See **[TUTORIAL_FirstGame.md](TUTORIAL_FirstGame.md)** to build and ship a complete game
using these systems.
