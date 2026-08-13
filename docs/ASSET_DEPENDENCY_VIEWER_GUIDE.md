# Asset Dependency Viewer

Open **Panels > Debug & Diagnostics > Asset Dependency Viewer** to inspect how
engine-owned and authored assets refer to each other.

## Main workflow

1. Use **Sync Registry** after importing, moving, deleting, or resaving assets.
2. Search by asset name, path, type, or stable asset ID.
3. Optionally select an asset type or enable **Findings only**.
4. Select an asset and switch between **Graph**, **Tree**, and **List** views.
5. Use **Open Asset** to open it in its matching editor, or **Reveal in Content**
   to select it in the Content browser.
6. Use **Export Report** to write a non-destructive text report under
   `Content/Reports/AssetDependencies.txt`.

## Findings

- **Missing** means an asset ID is referenced but is absent from the registry.
- **Stale path** means the registered Content file or original import source no
  longer exists.
- **Circular** means a dependency chain eventually refers back to itself.
- **Duplicate reference** means one asset lists the same dependency repeatedly.
- **Duplicate content** means same-type imported assets share a source hash.
- **Unreferenced** means no other registered asset points to the asset. Scenes and
  world manifests are roots and are not reported as unused merely for having no
  incoming reference.

The viewer never deletes or rewrites assets. Its report is intended for review
before cleanup or packaging.

## Prefabs

Newly saved prefab files use prefab format version 3. They receive a stable asset
ID and store references to captured meshes, materials, characters, audio, and
particle assets. Existing version 1 or 2 prefabs still load; save them once in the
Prefab Editor to upgrade them and include them in dependency analysis.
