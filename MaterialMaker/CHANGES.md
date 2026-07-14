# Material Maker — change summary & build/verify checklist

A run of improvements was made to the Material Maker in this session. **None of it
has been compiled yet** (the work was done without a local toolchain). This file is
the hand-off: what changed, how to build it, and what to check.

Full per-feature rationale is in `ROADMAP.md`.

---

## Files added

| File | Purpose |
|------|---------|
| `include/MaterialMaker/MaterialPreview.h`, `src/MaterialPreview.cpp` | Live offscreen **PBR preview** (sphere/cube/plane) via `PbrRenderer` + `IBL` into a `Framebuffer`; **channel views** (albedo/metallic/roughness/normals/AO) through an embedded unlit shader; **environment rig** (time-of-day, rotate, light, ground+shadow, background); **texture-map cache** (`AcquireMap`/`ResolveMap`). |
| `include/MaterialMaker/TexturePacker.h`, `src/TexturePacker.cpp` | **ORM channel packer**: combine separate metallic/roughness/AO PNG/JPG into one `R=AO,G=roughness,B=metallic` TGA. |
| `include/MaterialMaker/ModelMaterialImport.h`, `src/ModelMaterialImport.cpp` | **Import a material from a model** (glTF/OBJ/FBX) via Assimp. |
| `ROADMAP.md` | The improvement roadmap / milestone log. |

## Files modified

| File | Change |
|------|--------|
| `include/MaterialMaker/MaterialMakerPanel.h`, `src/MaterialMakerPanel.cpp` | Preview integration (`ImGui::Image`, drag-orbit), channel/env controls, map **thumbnails**, **drag-drop** from the content browser (`3DGEDITOR_ASSET`), **presets** + **PBR validation**, **ORM packer** UI, **library browser**, **import-from-model** UI, **emissive-strength** slider, **unsaved-changes** indicator. Added `~MaterialMakerPanel()` (PIMPL), `<memory>`/`<vector>`. |
| `include/MaterialMaker/MaterialDocument.h`, `src/MaterialDocument.cpp` | `emissiveStrength` field; **project-relative** texture paths on save/load; emissive bakes color×strength in `ToJson`/`ToCppInitializer`; **JSON-loader hardening** (top-level reads scoped before `"maps"`). Added `<system_error>`. |
| `CMakeLists.txt` | Link the **`engine`** target (brings glad/glm/imgui/assimp) and add the three new sources. |

## Engine change (one line)

| File | Change |
|------|--------|
| `engine/include/engine/graphics/Texture.h` | Added `unsigned int ID() const { return m_id; }` (like `Mesh::Vao()`) so thumbnails can be drawn with `ImGui::Image`. Non-breaking. |

---

## Build

```
cmake -S . -B build
cmake --build build
# run the editor (target name 3DGEditor), open Window > Material Maker
```

If the build fails, the most likely spots (see "Risks" below) are the Assimp PBR-key
macros and the `ImTextureID` cast — both are noted with fixes.

## Verify (per feature, in the panel)

- **Live preview** renders a lit sphere reacting to albedo/metallic/roughness/AO.
- **View** dropdown switches to flat Albedo/Metallic/Roughness/Normals/AO passes.
- **Environment** section: time-of-day changes lighting/reflections; Rotate spins the
  sky; Light scales brightness; Ground+shadow adds a contact shadow; Background works.
- **Texture maps**: paste a path, drag a texture from the Content browser, or use the
  editor's "Use as …" buttons → thumbnail + size appear; the map shows on the sphere
  (albedo/metal-rough exact; **normal maps approximate** on primitives — see Risks).
- **Pack ORM**: paste metallic + roughness (+ optional AO) → writes `<name>_ORM.tga`
  and assigns it.
- **Preset** menu applies gold/copper/etc.; **validation** line flags implausible values.
- **Emissive Strength** > 1 makes the material bloom in the preview.
- **Library** lists saved `.3dgmat`; click loads; New/Duplicate/Refresh work.
- **Import from Model**: paste a model path, pick an index, Import populates the doc.
- **Unsaved changes** indicator flips amber on edit, green after Save/Load.

---

## Risks / things to check first if it doesn't compile

1. **Assimp PBR keys** (`ModelMaterialImport.cpp`): `AI_MATKEY_BASE_COLOR`,
   `AI_MATKEY_METALLIC_FACTOR`, `AI_MATKEY_ROUGHNESS_FACTOR` should be in
   `<assimp/material.h>` for Assimp 5.4. If not found, add `#include
   <assimp/pbrmaterial.h>`.
2. **`ImTextureID` cast** (`MaterialMakerPanel.cpp`, `MaterialPreview.cpp` thumbnails):
   assumes the default `void*`. If your ImGui config uses `ImU64`, the C-style
   `(ImTextureID)(std::intptr_t)tex` still works; just confirm.
3. **Normal maps on primitives**: the sphere/cube/plane carry no tangents, so
   tangent-space normal mapping is approximate in the preview (albedo/metal-rough are
   exact). Not a bug — a fidelity limit; fix = add tangents to primitives or a
   derivative-based TBN in the shader.
4. **TexturePacker inputs**: PNG/JPG only (uses `engine::image` decoders); TGA sources
   aren't decoded as inputs yet.

## Not done (candidates for later)

- M7 **thumbnails in the library** + rename (needs a persistent per-material texture
  cache, not the single shared preview target).
- **glTF material export** / asset-DB registration with stable GUIDs.
- A standalone **file dialog** (ImGui has none built-in).
- Engine-side: **normal-map strength + green-channel flip**, **UV tiling**, advanced
  BRDF lobes (clearcoat / anisotropy / sheen / transmission).
