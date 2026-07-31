# Native assets, registry, importing, loading, and cooking

## Asset identity

`AssetHandle` is a stable 128-bit identifier. It is embedded in engine-owned
assets and authored metadata and remains unchanged when an asset is renamed or
moved.

`AssetType` currently identifies static meshes, skeletal meshes, skeletons,
animations, materials, textures, audio, shaders, particles, particle effects,
HUDs, characters, animation clips, animation graphs, behavior trees, scenes,
scripts, terrain, and fonts.

An `AssetReference` stores:

- the authoritative `AssetHandle`;
- a readable fallback path for legacy projects and diagnostics.

`ResolveAssetReference` resolves by ID first, validates the expected type, then
falls back to the path.

## Native container

All binary engine-owned files begin with `NativeAssetHeader`, containing:

- container and asset versions;
- asset type and stable ID;
- importer version and source hash;
- payload size and flags;
- dependency IDs.

Current native extensions:

| Extension | Asset |
|---|---|
| `.3dgmesh` | Static mesh |
| `.3dgskmesh` | Skeletal mesh |
| `.3dgskel` | Skeleton |
| `.3dganim` | Animation clips |
| `.3dgtex` | Texture |

Raw FBX, OBJ, glTF, Collada, PLY, STL, PNG, and JPEG files are import sources,
not the preferred runtime representation.

## Asset registry

`AssetRegistry` maps IDs and normalized virtual paths to
`AssetRegistryEntry` records. Each entry stores the type, virtual path, source
file, source hash, importer version, and dependency IDs.

Principal operations:

- `Register`, `Remove`, and `Move`;
- `Find`, `FindByPath`, `ByType`, and `Referencers`;
- `Validate` for duplicate, missing, and type/dependency issues;
- `Save` and `Load`;
- `RebuildFromContent`;
- `SynchronizeAuthoredAssets`.

The project registry is `Content/AssetRegistry.3dgdb`. Editor startup
synchronizes both native and authored assets before validation. This repairs
registries after assets are copied or moved within Content and prevents valid
native texture dependencies from being reported as missing.

## Static-mesh import

`ImportStaticMeshToAsset`:

1. Uses Assimp to load a supported source.
2. Rejects skeletal sources so bone data is not silently discarded.
3. Applies scale, normals, tangent, vertex-join, and UV options.
4. Builds submeshes with position, normal, UV, and tangent data.
5. Embeds imported material data and RGBA textures.
6. Computes bounds, source hash, and statistics.
7. Preserves the existing destination ID during reimport.
8. Updates the registry.

A `.3dgmesh` contains geometry and its imported material set. A scene-assigned
`.3dgmat` is an object override and is stored by the scene, Character Asset, or
Prefab—not baked into the mesh during ordinary material assignment.

## Skeletal import

`ImportSkeletalAssetsToContent` writes a coordinated set:

- `.3dgskmesh` for skinned vertices, skeleton copy, materials, and optionally
  embedded animations;
- `.3dgskel` for the reusable skeleton;
- one `.3dganim` per imported animation clip.

The mesh and animation assets depend on the skeleton ID. Animation channels
also retain bone names so compatible skeletons can merge clips even when bone
indices differ.

`InspectModelSource` performs a lightweight pre-import check and reports mesh,
bone, and animation counts. The editor uses this to choose static or skeletal
import settings.

## Texture import and decoding

`ImportTextureToAsset` decodes the source into bottom-up RGBA8 pixels and
stores smoothing and sRGB flags in `.3dgtex`. Reimport preserves the ID.

The image layer supports PNG—including 16-bit PNG input—JPEG, and the texture
loader’s supported TGA path. `TextureAssetData` is the CPU representation;
`Texture` owns the uploaded OpenGL object.

Use sRGB for color/albedo textures. Use linear sampling for normal,
metal/roughness, AO, height, data masks, and most lookup textures.

## Runtime asset manager

`RuntimeAssetManager` owns caches for:

- static `Model`;
- `SkinnedModel`;
- `Texture`;
- `RuntimeMaterialAsset`;
- generated runtime shader programs.

Load functions return stable non-owning pointers to cached objects. Reload
replaces model GPU data in place so existing ECS pointers remain valid during
editor reimport.

The skeletal loader can merge additional animation sources by bone name and
cache each unique model-plus-animation combination separately.

`ResolveRegistryAssets(registry)` converts unresolved ECS asset components into
loaded components and returns counts plus per-asset errors.

## Authored asset formats

| Extension | Editor/runtime role |
|---|---|
| `.3dgmat` | PBR material and optional shader parameters |
| `.3dgshader` | Typed shader graph |
| `.particle` | Particle system |
| `.particlefx` | Multi-layer particle effect |
| `.hud` | Runtime HUD |
| `.3dgcharacter` | Character configuration |
| `.3dgprefab` | Reusable object component template |
| `.3dgclip` | Reusable animation clip/action |
| `.3dggraph` | Animation state graph and blend spaces |
| `.btgraph` | Behavior tree and blackboard |
| `.3dgaudio` | Gameplay audio cue |
| `.3dgmusic` | Adaptive music states |
| `.3dgmixer` | Mixer preset |
| `.scene` | Editable or exported scene |

Double-click routing in the Content browser opens each supported authored
asset in its corresponding editor panel.

## Import and reimport workflow

The Content browser’s **Import Asset** button opens the native OS file browser.
After selection, the import settings window identifies the source type, exposes
the relevant options, and lets the developer choose or create the destination
Content folder.

**Reimport** uses the recorded source path and prior import settings. Native
asset IDs remain stable, preserving scene, material, graph, and prefab
references.

## Cooking and packaging

`AssetCooker::CookRuntimeScene` walks the runtime scene’s stable dependency
graph and copies only reachable assets to a relocatable output Content folder.
It writes a subset registry with the same IDs. `AssetCookResult` reports the
runtime scene and every cooked asset’s type, path, size, and hash.

Before packaging:

1. Save authored assets.
2. Save and export the scene.
3. Validate the asset registry.
4. Resolve all missing dependencies.
5. Cook the exported runtime scene.
6. Launch the standalone player against the cooked output.

## Failure modes

- **Missing dependency warning:** the referenced ID is absent from the registry.
  Refresh/rebuild the registry; if the file is gone, reassign the reference.
- **Asset appears after move but fails at runtime:** the authored data may still
  contain a legacy path without a valid ID. Re-save it after registry refresh.
- **Reimport changes references:** indicates the destination’s original header
  was invalid or bypassed; normal reimport preserves the handle.
- **Material texture is black:** check sRGB mode, data-map channel assignment,
  decode errors, and whether the material references the native `.3dgtex`.
- **Imported mesh loses animation:** it was imported as static; reimport it
  through skeletal settings.

