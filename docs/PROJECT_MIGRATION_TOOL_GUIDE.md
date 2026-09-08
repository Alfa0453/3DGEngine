# Project Migration Tool Guide

Open **Panels > Content Editors > Project Migration**. The tool examines the active
project and previews required format upgrades. Scanning is read-only and runs in the
background.

## Supported upgrades

The tool uses the engine's real legacy readers and current writers for:

- `Project.3dgproject` settings
- authored `.scene` files and their serialized components
- characters, animation graphs, animation clips, and prefabs
- particle systems, shaders, materials, and interaction assets
- engine-native static meshes, skeletal meshes, textures, and foliage assets

Current files are hidden by default. Enable **Show Current** to audit them. Invalid
files and files created by a newer engine remain visible but cannot be selected. The
tool never attempts to downgrade a future format. Generated `.autosave.scene` and
`.runtime.scene` files are excluded because they are derived data.

## Safe migration workflow

1. Save the currently open scene. A dirty open scene is forcibly excluded.
2. Choose **Scan Project** and review each proposed version change.
3. Use the file/type filter and clear any upgrade you do not want to apply.
4. Choose **Apply Selected Upgrades**.

Before writing, every selected file must load successfully. The tool then copies the
entire selection into a timestamped folder under:

`<Project>/Saved/ProjectMigrations/`

Each file is decoded through its legacy loader, written with the current serializer,
and loaded again to validate the result. If any step fails, every backed-up file is
restored. Successful migrations refresh the Content Browser and dependency viewer;
the current scene is reloaded when its clean on-disk file was migrated.

## Reports and recovery

Reports are written to:

`<Project>/Saved/Reports/ProjectMigration-<timestamp>.txt`

The report lists backups, version changes, failures, and rollback status. Backups are
retained after success so a migration can be audited or manually reversed later.

Project settings gain `project.format_version = 1`; the legacy misspelled
`project.asses` key is replaced by `project.assets` while preserving its value.
