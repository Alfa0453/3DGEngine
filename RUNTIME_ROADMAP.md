# Standalone Runtime Player — Roadmap

## Goal

A **game mode**: a standalone executable (`player`) that boots an editor-authored,
exported scene and runs it with **no editor** — no ImGui, no gizmos, no editor
panels. This is how a finished game ships, and it is the only real proof that the
*engine* and the *editor* are separable: "Play in editor" and "run the game" should
eventually execute the *same* engine systems.

The editor already has all the runtime pieces (physics, scripts, gameplay systems,
AI, animation, particles, audio, HUD); its Play mode builds a runtime registry and
ticks them. The player reuses those engine systems directly. The editor exports the
`3DGRuntimeScene` format (via `RuntimeSceneExporter`), and the engine already loads
it (`RuntimeSceneLoader::Load` + `Instantiate`). The player consumes that.

## Architecture

```
editor  ──(F7 export)──►  scene.3dgscene ──►  player (this target)
                                               ├─ RuntimeSceneLoader::Load / Instantiate  → ecs::Registry
                                               ├─ engine systems (physics, scripts, AI, animation, particles, audio)
                                               ├─ PbrRenderer + ProceduralSky + PostProcess + IBL
                                               └─ engine::DrawHud  (the scene's .hud)
```

The player is a thin `engine::Application` subclass. It holds **no gameplay logic** —
gameplay lives in scripts and the ECS systems, exactly as in the editor's Play mode.

## Milestones

### RM1 — Boot & Render  ✅ (this milestone)
- New `player/` executable target, linking `engine` (no editor, no ImGui).
- Loads a runtime scene from a path (CLI arg or config), `Instantiate` into a registry.
- Converts the loaded `MeshRenderer` components to `MeshPBR` (default material from the
  authored colour) so the PBR renderer draws them; lights + the environment sun come in
  from the loader.
- Renders geometry + lights + procedural sky + fog + IBL with a **free-fly dev camera**
  (WASD + Q/E, hold RMB to look, Shift to sprint).
- On-screen status overlay (scene name, entity count, fps) and clear on-screen error if
  the scene fails to load.
- **No simulation yet** — this milestone proves the engine renders an exported scene
  standalone.

### RM2 — Simulation
- Drive the fixed-step gameplay loop from `OnFixedUpdate`, ticking the same engine
  systems the editor's Play mode ticks: `PhysicsWorld::Step`, `UpdateScripts`,
  `ecs::UpdateGameplay`, `UpdateHealth`, `ecs::UpdateRuntimeMotion`, AI, `UpdateAnimations`,
  particles, audio.
- Spawn/track the player entity (Player Start) and switch the free-fly camera to a
  follow/first-person camera driven by `PlayerController`.
- Terrain floor snap (terrain support in the runtime format, if exported).

### RM3 — Presentation & Input parity
- Load and draw the scene's HUD (`engine::DrawHud`) with health + named-value bindings,
  matching the editor's play HUD.
- Environment parity: cascaded/point/spot shadows, post-process stack, MSAA/FXAA,
  render scale — read from the exported environment.
- Input polish: cursor capture for mouse-look, pause (freeze simulation), quit.

### RM4 — Game framework & packaging  (in progress)
- ✅ Player controller + PlayerStart + first/third-person camera (RM4a).
- ✅ Authored player-controller settings exported into the runtime format (v48).
- ✅ **Packaging** — install rules + CPack produce a shippable folder/zip:
  ```
  # optional: bundle your exported game content beside the exe
  cmake -S . -B build -DPLAYER_GAME_DIR=E:/path/to/exported_game -DGAMEENGINE_STATIC_RUNTIME=ON
  cmake --build build --target player
  cmake --install build --prefix dist --component player   # -> dist/player[.exe] + player.cfg + game/
  cd build && cpack                                          # -> 3DGPlayer-<ver>-<os>.zip
  ```
  `player.cfg` (beside the exe) sets the window + `player.scene`; relative scene paths
  resolve next to `player.exe`, so the folder runs from anywhere. Only the `player`
  target has install rules, so the package excludes the editor and demos.
- Startup scene + window settings from config/CLI — ✅ (`player <scene>` or `player.cfg`).
- Pause menu and quit-to-menu via HUD buttons — ✅ (P frees the cursor; Exit/Restart work).
- Remaining: scene switching / level loading at runtime; custom-script factory
  registration so game scripts run in the player; particle/audio + skinned-model
  rendering; an optional **GameMode** rules hook (player spawn rules, win/lose, score,
  menu → playing → paused → game-over state machine).

## Notes
- The player must never link the editor or ImGui — that boundary is the whole point.
- Keep the per-frame systems identical to the editor's Play path so behaviour matches
  1:1; long-term, factor the shared tick into an engine-side "world runtime" both call.
