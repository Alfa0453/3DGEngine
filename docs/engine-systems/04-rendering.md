# Rendering, cameras, environment, terrain, and water

## Rendering layers

The engine has three related drawing paths:

1. Primitive `MeshRenderer` objects through the basic `Renderer`.
2. Imported static `Model` objects through `DrawModel` and
   `RenderLoadedModels`.
3. ECS PBR and skeletal scenes through `PbrRenderer` and `SkinnedRenderer`.

The editor combines these paths because a scene may contain primitives,
engine-owned imported meshes, skeletal characters, terrain, water, particles,
and editor-only guides.

## GPU resource wrappers

| Type | Responsibility |
|---|---|
| `Shader` | Compile/link GLSL, bind program, upload uniforms, report diagnostics |
| `VertexLayout` | Attribute ordering and stride |
| `Mesh` | VAO/VBO/EBO ownership and indexed drawing |
| `Texture` | 2D image upload, mipmaps, updates, and binding |
| `Cubemap` | Six-face environment texture |
| `Framebuffer` | Off-screen color/depth render target and resizing |
| `Model` | Imported/native submeshes, materials, textures, and bounds |
| `SkinnedModel` | Skeleton, weighted submeshes, and animation clips |

These objects are move-only where they own OpenGL resources. Creation,
destruction, and updates require a live context.

## Basic renderer and models

`Renderer::Draw` submits a `Mesh`. The caller binds the shader and sets
view/projection and per-object uniforms.

`Model` stores multiple `SubMesh` records and imported Phong-style materials.
`DrawModel` binds each submesh’s maps and can apply:

- a color tint;
- an albedo texture override;
- a complete `ModelMaterialOverride`.

Scene-assigned materials use the complete override so albedo, specular,
emissive, shininess, diffuse map, and normal map replace rather than merely
multiply the embedded values.

The default imported-model shader converts linear lighting to display space
with tone mapping and gamma correction. Without this, valid dark material
values appear nearly black.

## PBR renderer

`PbrRenderer` draws `Transform + MeshPBR` entities with Cook–Torrance
metallic/roughness shading. Its options control:

- ambient and fog;
- image-based lighting;
- directional, point, and spot shadows;
- shadow distance;
- extra shadow casters such as skinned models;
- screen-space and environment integrations supplied by the host.

`PbrMaterial` contains albedo, metallic, roughness, AO, emissive, opacity,
normal/metal-rough/height maps, UV transforms, clearcoat, transmission,
subsurface, sheen, anisotropy, displacement, and blend-mode settings.

The loaded-material bridge also supports a generated shader graph and
per-instance parameter overrides.

## Lighting

`ecs::Light` supports:

- directional lights using `direction`;
- point lights using the entity transform as position;
- spot lights using position, direction, inner/outer angles, and range;
- area lights using a source radius.

`ClusteredLights` implements CPU-built Forward+ light culling for OpenGL 3.3:
the screen is divided into a 16×9 tile grid, with up to 128 point lights and a
fixed per-tile light list. Light data is uploaded through a uniform buffer and
tile indices through a texture buffer.

## Shadows

| System | Use |
|---|---|
| `ShadowMap` | Basic directional depth map |
| `CascadedShadow` | Four camera-fitted sun cascades |
| `PointShadow` | Omnidirectional cube shadow for point lights |
| `SpotShadow` | Perspective shadow for spot lights |
| `ShadowCasterBatch` | Shared static-caster submission |

Cascades preserve detail close to the camera while covering the configured
shadow distance. Skinned and other custom geometry must be supplied through
the extra-caster callback to appear in those passes.

Shadow disappearance distance is authored in World Settings and exported as
the environment `shadowDistance`.

Indoor darkness is produced by direct shadowing plus skylight occlusion and
minimum-skylight settings. The system is dynamic; it is not a UE-style baked
lightmap pipeline.

## Image-based lighting

`IBL` builds irradiance and prefiltered environment resources used for diffuse
ambient light and specular reflections. A scene can derive them from the
procedural sky. Rebuild only when the environment changes enough to justify
the GPU work.

## Cameras

`Camera` is a yaw/pitch perspective camera with movement, `LookAt`, view and
projection matrices, FOV, and near/far planes.

Related systems:

- `CameraPose`: serializable position, target, FOV, and clip planes.
- `CameraBlend`: timed transitions with easing.
- `CameraSequencePlayer`: ordered travel and hold shots, looping, seeking,
  Catmull-Rom paths, skipping, and shot events.
- `CameraShake`: additive local translation, rotation, and FOV impulses.
- `CameraDirector`: gameplay command/event bridge for named sequences.
- `PlayerController`: first-person, third-person, isometric, and platformer
  gameplay rigs with camera collision, shoulder view, and lock-on.

The scene Camera Manager stores presets and cinematic sequences. Camera mode
is selected before Play through the character or Game Mode settings; gameplay
input does not toggle camera mode.

## Post-processing

`Framebuffer` provides HDR inputs. `PostProcess` owns the full-screen pipeline
and supports:

- tone mapping;
- bloom;
- FXAA;
- render scale;
- an ordered stack of authored post-process shader effects.

`SSAO` estimates contact and cavity occlusion from screen-space depth/normal
data. `SSR` traces reflections in screen space. Their quality depends on the
depth buffer, camera matrices, render resolution, and authored thresholds.

`GpuProfiler` measures sequential named GPU scopes using delayed query readback
so profiling does not stall the current frame.

## Procedural sky and world environment

`DayNightCycle::At(t)` returns sun/moon direction and radiance, ambient fill,
day factor, sky horizon/zenith colors, and disc colors.

`ProceduralSky` renders that environment and supports authored clouds. World
settings control time of day, skylight, sun drive/intensity, clouds, fog, and
shadow behavior.

Cloud parameters include coverage, density, scale, softness, wind speed and
direction, horizon height, tint, shadow enable, shadow strength, and shadow
world scale. Cloud shadows modulate sun lighting independently of whether
debug overlays are visible.

## Terrain and grass

`Heightmap` stores resolution, size, origin, and height values and provides
surface queries.

`Terrain` can:

- generate procedural fBm height;
- accept imported or edited heightmaps;
- create a render mesh;
- build an albedo texture from height, slope, and paint layers;
- answer `HeightAt(x,z)` for gameplay and navigation.

The editor supports landscape/heightmap import, terrain generation, layer
paint, and material-derived layer colors.

`GrassField` builds GPU-instanced procedural blades from terrain paint and a
style palette. Density, blade dimensions, colors, wind, layer, and maximum
blade count are configurable. Grass instances are rebuilt when their terrain
or style input changes.

## Water

`Water` owns a configurable grid patch with procedural waves, deep/shallow
absorption, screen-space refraction, rough IBL reflections, sun specular,
shore/contact foam, shallow-water caustics, spline-directed flow, and distance
culling. It updates animation time, draws as a transparent forward pass, and
exposes `HeightAt` for buoyancy and surface-contact effects.

The editor and standalone runtime capture opaque scene colour and depth once
before all water bodies render. When a camera crosses below a water surface,
the HDR compositor smoothly blends underwater tint, depth fog, distortion,
caustics, vignette, and the underwater audio snapshot. Authored water bodies
are included in runtime scene exports and streamed levels; dynamic rigid bodies
receive buoyancy and drag in both editor Play and standalone builds.

Transparent water should render after opaque geometry and before UI. Depth
write and blend state must be restored after the pass.

## Culling and debug drawing

`Frustum` extracts six view planes and tests spheres or AABBs. Particle systems
also have authored bounds and culling controls.

Editor visualization uses `EditorLineRenderer` for collider outlines, sockets,
empty-object icons, navigation overlays, camera rails, AI vision, gizmos, and
selection outlines. These guides are editor/debug presentation and do not
become runtime mesh geometry.
