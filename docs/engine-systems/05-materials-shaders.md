# Materials, textures, and shader graphs

## Material asset

A `.3dgmat` stores a `RuntimeMaterialAsset`:

- stable material ID and name;
- full `PbrMaterial` surface values;
- albedo, normal, metal/rough, and height texture references;
- optional shader-graph reference;
- typed shader parameter values and texture references.

Texture and shader references use IDs plus fallback paths. Material saves
record those dependencies in the asset registry.

## Standard PBR channels

| Channel | Meaning |
|---|---|
| Albedo | Linear/sRGB base surface color |
| Metallic | 0 dielectric to 1 metal |
| Roughness | Microfacet spread; low is sharp |
| AO | Ambient-occlusion multiplier |
| Normal | Tangent-space normal perturbation |
| Metal/Rough | Packed data texture used for surface response |
| Height | Parallax/displacement input |
| Emissive | Light-independent output color |
| Emissive strength | Emission multiplier |
| Opacity | Transparent blend control |
| Clearcoat | Secondary glossy layer |
| Transmission | Light passing through surface |
| Subsurface | Diffuse scattering approximation |
| Sheen | Grazing fabric-like lobe |
| Anisotropy | Directional highlight response |
| Displacement | Geometry/surface offset amount |

Color textures normally use sRGB. Normal, roughness, metallic, AO, height, and
packed data maps must be treated as linear data.

## Material Maker

The Material Maker authors `.3dgmat` documents and provides:

- isolated lit preview;
- environment rotation, light, and background controls;
- standard and advanced PBR fields;
- native engine-texture selection;
- texture-path paste/drop support;
- normal, packed metal/rough, and height maps;
- ORM packing from separate grayscale sources;
- presets, reset, load, save, and generated C++ snippets;
- optional shader assignment and parameter editing.

Saving the material writes the asset. Applying it in the Inspector writes the
material reference to that object’s scene record. To preserve the mesh and
material as one reusable configuration, capture the object as a Prefab or
Character Asset.

## Material instances

Scene objects may override parameters from the assigned shader-backed
material. Overrides live on the object, not in the shared material document.
They are serialized with the scene and uploaded by
`UploadLoadedMaterialShaderParameters`.

This allows multiple objects to share one material and vary color, scalar,
vector, boolean, or texture parameters without duplicating the material.

## Shader assets

A `.3dgshader` contains a typed `ShaderAsset`:

- domain: Surface, Post Process, Particle, or Unlit;
- blend mode;
- graph nodes, pins, and links;
- named typed parameters;
- graph-local IDs separate from the stable project asset ID.

`ValidateShaderAsset` checks required outputs, pin compatibility, links, and
graph integrity. `GenerateShaderSource` walks only nodes reachable from the
active domain output, detects cycles, emits deterministic GLSL, maps fragment
lines back to node IDs, and can generate a skinned variant.

## Shader domains

| Domain | Intended output |
|---|---|
| Surface | Lit material surface |
| Unlit | Color independent of scene lighting |
| Particle | Particle-specific render inputs |
| Post Process | Full-screen effect sampling the previous scene color |

Choose the domain before building the graph because output pins and generated
pipeline assumptions differ.

## Shader Editor

The Shader Editor provides:

- live isolated preview;
- typed node graph and compatible-node prompt when dragging from a pin;
- right-click node actions;
- undo, redo, delete, duplicate, copy, paste, framing, alignment, and layout;
- parameters and texture selection from engine-owned assets;
- generated vertex/fragment source and node-linked diagnostics;
- automatic compile, manual compile, apply, last-valid restore, and fallback;
- surface, unlit, particle, and post-process previews.

Object Color is preview input, not an authored constant. Use a Color/Vector
parameter or constant-color node when the shader needs a saved color value.

## Runtime shader manager

`RuntimeShaderManager` caches programs by asset path and variant. A compile or
reload stores:

- the program;
- diagnostics;
- asset/source hashes;
- dependency hashes;
- whether a fallback is active.

If hot reload fails after a valid compile, the previous valid program remains
active. A first-time failure receives a visible domain fallback rather than a
null program. `DependenciesChanged` detects included or referenced file changes.

## Texture packing

The Material Maker can pack an ORM texture:

- red = ambient occlusion;
- green = roughness;
- blue = metallic.

The shader and material loader must agree on this channel convention. A
roughness texture should not be pasted into the Normal Map slot; doing so
produces invalid lighting even when decoding succeeds.

## Imported-model material override

Native `.3dgmesh` files may contain embedded imported materials and textures.
A scene-assigned `.3dgmat` supplies a complete override to all imported
submeshes in the current implementation. It is not permanently written back
into the `.3dgmesh`.

The display path tone-maps and gamma-corrects the linear result. Very low
albedo values will still look dark by design but no longer collapse to black
solely because display conversion was missing.

## Troubleshooting

- **Material does not change:** confirm the object stores the `.3dgmat` path/ID,
  the runtime cache resolved it, and the correct rendering path receives the
  complete override.
- **Black material:** inspect albedo magnitude, light/skylight, normal-map
  validity, sRGB flags, shader diagnostics, and texture decode errors.
- **Could not decode image:** import the source as `.3dgtex`; verify supported
  PNG/JPEG/TGA encoding and whether it is a valid image rather than a packed or
  mislabeled file.
- **Missing dependency:** refresh the Content browser/registry and re-save the
  material after assigning native texture assets.
- **Shader compile failure:** select the diagnostic to locate its graph node,
  repair the typed link or missing required output, and retain the last valid
  program until the new compile succeeds.

