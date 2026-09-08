# Build Size Analyzer

The Build Size Analyzer explains where a packaged game's disk space goes before a
release is distributed. Open **Panels > Debug & Diagnostics > Build Size Analyzer**.
The tool is read-only: analyzing or exporting a report never changes Content, cooked
data, or a package.

## Analyze a build

Use **Package Output** to inspect the folder configured in Project Packaging, or use
**Browse Folder** for a particular staged build. **ZIP** opens a completed packaged
archive. **Project Content** is useful earlier in production, before a build exists.
Press **Analyze** after choosing the target.

Folder scans run in the background. Their packed-size column is an estimate based on
the type of each file. ZIP scans read the archive's central directory and therefore
show its actual compressed and uncompressed sizes without extracting it.

## Understand the results

The summary separates runtime files, scenes and worlds, meshes, animation, textures,
materials and shaders, audio, scripts, VFX, UI and localization, configuration, and
other files. Set a total build budget and optional category budgets in MiB. An **Over**
marker identifies a category that has exceeded its budget.

The file table can be searched and sorted by size, path, or category. **Issues only**
shows unused or duplicated content. **Unused** and **Duplicates** narrow that further.

- For a cooked folder, unused means an asset-like file under `Content` that is not in
  the cook manifest. Runtime scenes and dynamically loaded Lua scripts are handled as
  valid package content.
- For the project's Content folder, unused engine assets come from the asset registry:
  the asset is not a scene or world and has no registered referencer.
- Duplicate groups contain files with the same byte size and content fingerprint. The
  avoidable-size total keeps one representative and counts only the extra copies.
- ZIP duplicate groups use the archive CRC and uncompressed size. Folder scans use a
  streaming 64-bit content fingerprint.

“Unused” is a review signal, not permission to delete a file. Assets loaded by a custom
native path may not be represented by registry dependencies. Confirm usage before
removing content.

## Export a budget report

**Export Text Report** creates a readable category and budget summary. **Export CSV**
creates a per-file table suitable for a spreadsheet or release checklist. Reports are
written to `Saved/Reports/BuildSize-<timestamp>.*` inside the active project.

Compare reports between release candidates to catch unexpectedly large imports,
duplicate texture sets, debug symbols shipped by mistake, or a category that is
growing faster than its assigned budget.
