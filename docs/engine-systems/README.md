# 3DGEngine systems reference

This reference documents the systems currently implemented in the `engine`,
`editor`, `game`, and `player` targets. It is based on the public headers and
their current implementations, not on the older roadmap.

The engine is C++17, uses OpenGL, and follows one central boundary:

- `engine` is the reusable runtime library.
- `game` contains project gameplay scripts and may depend on `engine`.
- `editor` authors assets and scenes and may depend on both.
- `player` loads exported runtime scenes without depending on editor code.

## Read by subject

1. [Core application and project architecture](01-core-architecture.md)
2. [ECS, components, systems, prefabs, scenes, worlds, and level streaming](02-ecs-scenes.md)
3. [Native assets, registry, importing, loading, and cooking](03-assets.md)
4. [Rendering, lighting, cameras, environment, terrain, and water](04-rendering.md)
5. [Materials, textures, and shader graphs](05-materials-shaders.md)
6. [Skeletal animation, graphs, blend spaces, actions, sockets, and characters](06-animation.md)
7. [Physics, collision filtering, queries, joints, controllers, and ragdolls](07-physics.md)
8. [AI, navigation, perception, behavior trees, blackboards, and BT scripts](08-ai-navigation.md)
9. [Gameplay framework, player controller, camera direction, and scripts](09-gameplay-scripting.md)
10. [Audio engine, sources, cues, mixer, music, and editor](10-audio.md)
11. [Particle simulation, modules, effects, rendering, and scripting](11-particles.md)
12. [Runtime UI, HUD documents, bindings, fonts, and HUD editor](12-ui-hud.md)
13. [Editor application, panels, controllers, asset editors, and Play mode](13-editor.md)
14. [Build, standalone player, packaging, diagnostics, and tests](14-build-runtime-testing.md)
15. [Public source coverage appendix](SOURCE_COVERAGE.md)

## Runtime frame flow

```text
Window events
  -> variable Update
       unscaled clock, time dilation, input, scripts, audio, animation, camera, UI
  -> zero or more fixed updates
       game mode, scripts, gameplay, AI, controllers, ragdoll activation,
       physics, projectile/contact processing
  -> Render
       shadows, PBR/static/skinned geometry, environment, particles,
       post process, HUD, debug/editor overlays
  -> swap buffers
```

Physics and gameplay that affect collision should run at the fixed step.
Presentation systems such as animation blending, audio listener updates, camera
effects, and UI normally run once per rendered frame.

Gameplay timers, scripts, animation, AI, particles, cameras, and physics consume
the globally scaled gameplay delta. Editor UI and audio mixing remain on
unscaled time. Hit-stop duration is also counted with unscaled time, allowing a
complete gameplay freeze to release reliably.

## Asset and scene flow

```text
Source file (FBX/OBJ/glTF/PNG/JPEG)
  -> editor import settings
  -> engine-owned native asset + stable AssetHandle
  -> AssetRegistry.3dgdb
  -> authored scene/character/material/graph references ID + fallback path
  -> RuntimeAssetManager resolves and caches GPU/runtime objects
  -> AssetCooker copies only dependencies reachable from the runtime scene
```

## Documentation conventions

- “Authored asset” means an editable document such as `.3dgmat`, `.3dgshader`,
  `.particle`, `.hud`, `.3dgcharacter`, `.3dgprefab`, `.3dgclip`,
  `.3dggraph`, or `.btgraph`.
- “Native asset” means an engine-owned binary such as `.3dgmesh`,
  `.3dgskmesh`, `.3dgskel`, `.3dganim`, or `.3dgtex`.
- “Runtime scene” means the exported scene consumed by Editor Play and the
  standalone player.
- Paths shown under `Content` are project-relative in authored data. Stable
  asset IDs are authoritative after import; paths remain readable fallbacks.

## Related task-oriented guides

- [First game tutorial](../TUTORIAL_FirstGame.md)
- [Fireball spell tutorial](../TUTORIAL_FireballSpell.md)
- [Audio system guide](../AUDIO_SYSTEM.md)
- [Existing engine manual](../ENGINE_MANUAL.md)
