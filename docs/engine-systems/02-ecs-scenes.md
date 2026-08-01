# ECS, components, systems, prefabs, scenes, worlds, and level streaming

## Registry model

`engine::ecs::Registry` stores integer `Entity` identifiers and type-specific
component pools. The important operations are:

- `Create()` and `Destroy(entity)`;
- `Add<T>`, `Remove<T>`, `Has<T>`, `Get<T>`, and `TryGet<T>`;
- multi-component `view<T...>()` iteration.

Components are data. Behavior lives in systems, controllers, or scripts. This
keeps runtime data serializable and lets the editor clone a scene registry for
Play mode.

## Core scene components

| Component | Purpose |
|---|---|
| `Transform` | Position, quaternion rotation, scale, and model matrix |
| `RuntimeName` | Stable authored object name used for lookups and bindings |
| `MeshRenderer` | Primitive mesh pointer plus flat tint |
| `MeshPBR` | Primitive mesh plus full PBR material |
| `ModelAsset` / `SkinnedModelAsset` | Unresolved authored asset references |
| `LoadedModelAsset` | Resolved shared static model |
| `AnimatedModel` | Resolved skeletal model and per-entity animation state |
| `MaterialAsset` | Authored material path and instance overrides |
| `LoadedMaterialAsset` | Resolved material maps and optional shaders |
| `Light` | Directional, point, spot, or area light data |
| `LinearVelocity` / `AngularVelocity` | Simple transform motion |
| `Rotator` / `Mover` | Reusable authored gameplay motion |
| `AudioSource` | Serializable audio-source settings |
| `TriggerAudioAction` | Collision-driven no-code audio action |

Physics, gameplay, AI, particles, and scripts add their own components covered
in their respective chapters.

## Built-in ECS systems

`ecs/Systems.h` contains rendering bridges:

- `RenderMeshes` draws `Transform + MeshRenderer`.
- `RenderLoadedModels` draws imported `LoadedModelAsset` objects, selects a
  custom material shader when present, uploads instance parameters, and applies
  complete model-material overrides.
- `LoadedModelMaterialOverride` converts PBR material values to the legacy
  imported-model material interface.

`ecs/RuntimeSystems.h` contains small stateless updates:

- `UpdateRuntimeMotion` applies linear and angular velocity.
- `UpdateGameplay` updates `Mover` and `Rotator`, using rigid-body velocity for
  dynamic or kinematic objects where appropriate.

Gameplay-specific systems such as health, projectiles, attachments, scripts,
and ragdolls are documented separately.

## Editor scene

`EditorScene` is the authoring representation. Each object contains:

- an ECS entity;
- its primitive or imported model reference;
- material and material-instance overrides;
- model-only orientation, position, and scale offsets;
- animation, physics, audio, particle, gameplay, script, AI, terrain, camera,
  and prefab authoring state.

World transforms belong to the scene instance. Render-only model offsets fix
asset orientation or visual alignment without rotating the collider,
controller, navigation direction, or world axes.

The editor records undoable changes, selection, locks, visibility, component
add/remove operations, and scene serialization. A locked object rejects
mutating Inspector and editor commands.

## Scene serialization and loading

The data flow is:

1. `EditorScene` saves the editable scene.
2. `RuntimeSceneExporter` writes a runtime-compatible scene.
3. `RuntimeSceneLoader::Load` parses it into a `RuntimeSceneLoader::Scene`.
4. `RuntimeSceneLoader::Instantiate` creates ECS entities and serializable
   components using the supplied primitive meshes.
5. `RuntimeAssetManager::ResolveRegistryAssets` resolves model, skeletal,
   texture, material, and shader references.
6. Host-owned systems build transient state such as live audio voices, particle
   emitters, behavior trees, controllers, and physics caches.

The scene description contains environment settings, game-mode settings,
navigation bounds and agents, trigger actions, camera zones, joints, terrain,
camera presets and sequences, lights, and entity records.

Stable asset IDs are stored with readable fallback paths. On load, the ID is
resolved first so renamed or moved assets continue to work.

## Worlds and level-as-asset streaming

A `.3dgworld` manifest treats a level as an engine-owned scene asset. A world
contains one always-resident persistent scene and any number of placed streamed
levels. Each `LevelRef` stores:

- a stable scene asset ID and readable fallback path;
- a placement transform applied to the entire level;
- local bounds used for streaming distance tests;
- load and unload radii;
- a `Distance`, `AlwaysLoaded`, or `Manual` streaming rule.

`LevelStreamingManager` instantiates active levels into the shared ECS registry
and records exactly which entities each level owns. Unloading destroys that
entity group while leaving shared GPU assets cached. Distance streaming uses
separate load and unload radii to prevent rapid boundary thrashing, and the
manager performs at most one activation or deactivation per update to reduce
frame spikes.

Placed level transforms affect entity transforms, mover origins and axes,
rotator and light directions, navigation bounds and patrol points, joint
anchors, and camera presets. The persistent level owns world environment and
game-mode state; streamed levels cannot replace it when they activate.

Activation hooks let the editor or player create host-owned state for newly
loaded entities, such as physics bodies, audio voices, animation runtime data,
navigation data, particle emitters, and scripts. The before-deactivate hook must
release that transient state before the entities are destroyed.

Manual streaming can be connected to doors, transitions, or script logic with
`LoadLevel` and `UnloadLevel`. `UnloadAll` is used before replacing a world or
shutting down the runtime.

## Prefabs

There are two prefab concepts:

- `engine::ecs::PrefabLibrary` is the low-level runtime ECS prefab facility.
- The editor `.3dgprefab` asset captures an `EditorScene::Object` component
  configuration for reusable authored objects.

The Prefab Editor workflow is:

1. Configure an object in the scene.
2. Open **Prefab Editor**.
3. **Capture from Selected**.
4. Save it under `Content/Prefabs`.
5. Add linked instances with **Add to Scene**.

A prefab captures model, material, render-only model offsets, physics,
movement, health, and script configuration. Applying a prefab intentionally
does not overwrite the instance’s world transform or name. Saving the prefab
re-applies its component configuration to linked scene instances.

Use a Character Asset instead of a generic prefab when the object requires a
skeleton, animation graph, sockets, attachments, player settings, or AI setup.

## Empty objects and system hosts

An Empty object has a transform and may carry scripts or other non-rendering
components. It is appropriate for score managers, game-state coordinators,
spawn managers, audio controllers, and other world systems. Its visibility
toggle is treated as “Active in Play,” and it does not create visible geometry.

## Common ordering constraints

- Instantiate components before resolving assets.
- Apply a streamed level's placement before building host-owned physics,
  navigation, animation, or audio state.
- Resolve skeletal assets before constructing animation poses or sockets.
- Run health bookkeeping before checking `justDied`.
- Activate ragdolls before physics and synchronize ragdoll poses after physics.
- Process collision events only after `PhysicsWorld::Step`.
- Shut down scripts and audio voices before replacing the runtime registry.
- Release per-level transient systems before destroying a streamed level's
  entities.
