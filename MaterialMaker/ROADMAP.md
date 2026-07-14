# Material Maker — improvement milestone

A roadmap for turning the Material Maker from a single-material property editor into
a real material-authoring tool. Milestone **M1 (live PBR preview)** is prototyped in
this commit; the remaining steps are ordered by value.

---

## M1 — Live PBR preview (prototyped)

**Problem.** The old preview (`DrawApproxPreview`, formerly `DrawPreview`) faked a
sphere with stacked gradient circles drawn on the ImGui draw list. It could not show
real metallic reflections, roughness response, normal detail or emissive bloom, so it
was not WYSIWYG — the authored material never matched what the engine actually
renders.

**What was added.**

- `MaterialPreview` (`include/MaterialMaker/MaterialPreview.h`,
  `src/MaterialPreview.cpp`): a small class that owns an off-screen
  `engine::Framebuffer`, a `PbrRenderer`, an `IBL`, a `ProceduralSky`, sphere/cube/
  plane meshes and a one-entity `Registry`. Each frame it pushes the live material
  onto the preview entity, renders the lit object into the framebuffer, and returns
  the colour-texture id.
- The panel (`MaterialMakerPanel::DrawPreview`) now shows that texture with
  `ImGui::Image` (V-flipped for GL's bottom-left origin), with **drag-to-orbit**, a
  **shape** selector (sphere / cube / plane), an **environment** slider (a
  `DayNightCycle` time-of-day that re-bakes the IBL), and a **Live** toggle. If the
  preview cannot be created it falls back to the old hand-drawn approximation, so the
  panel never breaks.
- Wiring: `MaterialMaker` now links the `engine` target (for `PbrRenderer`, `IBL`,
  `Framebuffer`, primitives and GLAD); the panel header stays engine-free by holding
  the preview behind a `unique_ptr` (forward-declared).

**Design notes.**

- GL resources are created lazily on the first `Render()` call, because a GL context
  is only guaranteed current while the ImGui panel is being drawn, not when the panel
  object is constructed.
- The preview renders with `tonemap = true` into an `RGBA8` target, so the texture is
  display-ready with no extra post pass. Shadows and frustum culling are disabled (a
  single centred object needs neither).
- IBL is **required** — the engine's PBR pass renders black without it — so the
  preview always bakes an environment map from the procedural sky, and re-bakes only
  when the environment slider changes.
- The previous framebuffer binding and viewport are saved and restored around the
  off-screen render, so the editor and ImGui keep drawing to the right target.

**Status.** Written against the current engine headers; **not yet compiled** here
(needs a local `cmake --build`, since it pulls in ImGui, GL and Assimp). Build the
`3DGEditor` target and open the Material Maker panel to verify.

**Known follow-ups within M1.** Texture maps are not yet fed into the preview (the
sphere shows the scalar material only) — that is M2. `ImTextureID` is assumed to be
the default `void*`; if the project configures it as `ImU64`, the cast in
`DrawPreview` still holds (C-style cast), but confirm on first build.

---

## M1b — Debug channel views + environment rig (implemented)

Built on top of the M1 preview.

- **Channel views.** A "View" selector renders the object as **Full PBR**, or a flat
  unlit **Albedo / Metallic / Roughness / Normals / AO** pass, so each property can be
  inspected on its own. The non-PBR views use a tiny embedded unlit shader
  (`kDebugVert` / `kDebugFrag` in `MaterialPreview.cpp`) that draws the mesh flat-
  coloured by the selected channel — no engine change required.
- **Environment rig** (an "Environment" section under the preview):
  **Time of day** re-bakes the IBL from the procedural sky; **Rotate** spins the
  sun/moon/key-light directions about Y so the sky *and* the reflections turn with
  the slider; **Light** scales the key-light intensity; **Ground + shadow** drops in
  a floor plane and enables the directional shadow so the object casts a contact
  shadow; **Background** sets the clear colour.

The render path now takes a `MaterialPreview::Settings` struct (size, camera yaw/
pitch, shape, channel, env time/rotation, light intensity, ground, background)
instead of loose arguments. Written against the current headers; **not compiled
here** — build the `3DGEditor` target to verify.

## M2 — Texture maps in the preview, with thumbnails (implemented)

The three map slots now load and render.

- `MaterialPreview` gained a **path-keyed texture cache** (`m_textures`,
  `unique_ptr<CachedTexture>` so addresses stay stable). `AcquireMap(path)` loads a
  texture (cached) and returns a `MapInfo` (GL id + size + ok/error); `ResolveMap`
  hands the material a `const Texture*` (or nullptr on empty/failed). Loading uses
  `engine::Texture(path)`, which already sniffs magic bytes and decodes
  **PNG / JPG / TGA** (the header comment claiming TGA-only is stale), wrapped in a
  try/catch so a bad file becomes a clean "could not decode" state instead of a
  throw.
- `Settings` gained `albedoMapPath` / `normalMapPath` / `metalRoughMapPath`; the Full
  PBR view resolves them onto the preview material each frame, so the sphere now
  shows the real maps.
- The panel shows a **48×48 thumbnail + dimensions** beside each map slot (or a red
  "could not decode" line), via `AcquireMap`.
- Engine: added a one-line `engine::Texture::ID()` accessor (like `Mesh::Vao()`) so
  the thumbnail can be drawn with `ImGui::Image`.

**Known limitation.** Primitive meshes (sphere/cube/plane) carry no tangents, so the
**normal map** preview is approximate on them — albedo and metal/rough are exact.
Debug channel views still show scalar values (they use the unlit shader, which does
not sample maps). Both are candidates for a small follow-up. Written against the
current headers; **not compiled here** — build `3DGEditor` to verify.

## M3 — Project-relative texture paths (implemented)

`SaveMaterialFile` now stores each texture-map path **relative** to the `.3dgmat`
directory (`MakeRelativePath`, forward-slashed via `generic_string`), and
`LoadMaterialFile` resolves them back to absolute against the file's directory
(`ResolveRelativePath`, via `weakly_canonical`). In-memory paths stay absolute so
the preview/loader keep working. A saved material now survives moving the project.
Cross-drive cases (Windows) fall back to keeping the absolute path. **Not compiled
here** — build `3DGEditor` to verify.

## M7a — Starter presets + PBR validation (implemented)

A first slice of M7 (the library gallery is still to come).

- **Presets.** A "Load a preset..." menu applies one of ten starters: **Gold,
  Silver, Copper, Aluminium, Iron, Chrome** (metallic, using measured base
  reflectances / F0), plus **Plastic, Rubber, Painted wood, Ceramic** dielectrics.
  `kPresets` table + `ApplyPreset` in `MaterialMakerPanel.cpp`.
- **PBR validation.** `DrawValidation` shows amber warnings for physically-
  implausible values — albedo too dark/bright for a non-metal, partial metallic,
  near-zero roughness, or a dark metal base colour — and a green "looks physically
  plausible" when clean.

**Not compiled here** — build `3DGEditor` to verify. Still to do for full M7: a
browsable material-library gallery (thumbnails from the M1 preview) with duplicate /
rename / search.

## M4 — Drag-drop from the content browser (implemented)

The editor's content browser already emits an ImGui drag payload named
**`3DGEDITOR_ASSET`** (the asset's file path; see `EditorDockspace.cpp`
`SetDragDropPayload`). Each of the three map slots now registers an
`ImGui::BeginDragDropTarget` that accepts that payload: dragging a texture from the
Content browser onto a slot validates it (`IsSupportedTexturePath`) and assigns it,
with a status line on success/failure. A hint line was added above the slots.
`acceptTextureDrop` lambda in `DrawTextureControls`. **Not compiled here.**

Still optional: a stand-alone **file dialog** for picking maps without the content
browser. ImGui has none built in, so it needs a small custom directory browser — a
follow-up; between paste, drag-drop, and the editor's "Use as Albedo/Normal/Metal-
Rough" buttons, path entry is already well covered.

## M5 — Emissive strength (HDR / bloom) (implemented)

`MaterialDocument` gained `emissiveStrength` (default 1). The panel adds an
**Emissive Strength** slider (0–20×) beside the 0–1 emissive colour; the effective
emissive = colour × strength is baked into the value the preview shows, the exported
C++ initializer, and the `.3dgmat` "emissive" field the engine consumes — so a
material can now glow above 1 and bloom. (On reload the baked HDR value comes back as
the colour with strength 1; keeping a separate strength through a round-trip would
need an extra JSON field and is a minor follow-up.) **Not compiled here.**

## M6 — Metal/rough channel packer (implemented)

New `TexturePacker` (`TexturePacker.h` / `.cpp`): `PackMetalRoughAO(metallic,
roughness, ao, output)` decodes separate grayscale PNG/JPG sources (via the engine's
`engine::image` decoders), reads each from its red channel, and writes one **ORM**
texture — **R = AO, G = roughness, B = metallic** (glTF convention) — as an
uncompressed 32-bit TGA the engine loads natively. Metallic/roughness are required
and must share a size; AO is optional (channel filled with 255 when absent).

The panel adds a **"Pack Metal/Rough (ORM)"** section (in the texture controls): paste
the metallic / roughness / (optional) AO source paths, click **Pack ORM →
Metal/Rough**, and the packed `<name>_ORM.tga` is written to the output folder and
assigned to the material's metal/rough slot automatically. Errors (size mismatch,
unsupported format, write failure) surface in the status line. **Not compiled here** —
build `3DGEditor` to verify. (TGA sources aren't decoded as inputs yet — PNG/JPG
only; adding a TGA reader is a minor follow-up.)

## M7 — Material library and presets

Presets shipped in **M7a** above. **M7b (library browser)** is now implemented: a
**"Library"** section lists every `.3dgmat` in the output folder (scanned with
`std::filesystem`), and clicking one loads it. **New** starts a fresh material,
**Duplicate** saves a `_copy`, and **Refresh** rescans; the currently-loaded file is
highlighted. `DrawLibraryControls` / `RefreshLibrary` in `MaterialMakerPanel.cpp`,
backed by an `m_libraryFiles` list. **Not compiled here.**

Still to do for the full gallery: **thumbnails** (render each material via the M1
preview into a persistent per-material texture) and **rename**, which need a small
thumbnail-texture cache rather than the single shared preview target.

## M8 — Apply to selection (already present in the editor)

This already exists: `EditorApp::DrawMaterialMakerTools` has **Save and Apply** and
**Apply Saved** buttons that save the `.3dgmat` and call
`EditorScene::SetSelectedMaterialAsset(path)` to assign it to the selected object.
This editor's objects reference a material *file path* (there is no inline per-object
`PbrMaterial`), so path-based apply is the correct model and no new code is needed.
A possible refinement — a "live apply" that tweaks the selected object without a save
round-trip — would require the editor to carry inline material values per object,
which is a larger editor-model change, not a MaterialMaker one.

## Import from model (implemented)

New `ModelMaterialImport` (`ModelMaterialImport.h` / `.cpp`): `ImportMaterialFromModel(
modelPath, materialIndex, out)` reads a material out of a glTF / OBJ / FBX via Assimp
(the same API `engine/src/graphics/Model.cpp` uses) — base colour (with a diffuse
fallback) → albedo, metallic/roughness factors, emissive colour, and the external
base-colour / normal / metal-rough texture maps resolved to absolute paths against the
model's directory (embedded textures are skipped). `CountModelMaterials` reports how
many a file has. The panel adds an **"Import from Model"** section: paste a model
path, choose a material index, click **Import Material**. Assimp is already linked
transitively through `engine`. **Not compiled here** — build `3DGEditor` to verify.

## Smaller polish

- ~~Harden the `.3dgmat` loader against key collisions.~~ **Implemented:** the
  top-level scalar/vector reads are now scoped to the text *before* the `"maps"`
  object, so a similarly-named key inside `maps`/`engineMapping` can't be picked up
  by a first-occurrence scan. (A full JSON parser is still a possible later swap.)
  **Not compiled here.**
- ~~Add a dirty / unsaved-changes indicator.~~ **Implemented:** the panel keeps a
  `m_savedSnapshot` of the last saved/loaded material and shows amber
  **"* Unsaved changes"** vs. green "No unsaved changes" (field-wise `SameMaterial`
  compare); the snapshot resets on Save / Load / Reset. **Not compiled here.**
