# Asset Reference Repair Tool Guide

The Asset Reference Repair Tool finds references that became invalid after assets were
moved, renamed, deleted, reimported, or copied outside the Content Browser.

## Running a scan

Open **Panels > Content Editors > Asset Reference Repair** and press **Scan Project**.
The scan is read-only. It checks:

- paths stored inside authored engine assets;
- stable asset IDs stored beside resolvable paths;
- registry entries whose Content file moved or disappeared;
- dependencies that are absent from the registry;
- missing original import sources.

Use the search field and issue-type filter to narrow the table. Selecting an owner
reveals that asset in the Content Browser. Findings marked **manual repair required**
are intentionally never changed automatically.

## Applying repairs safely

Repairable findings are selected by default. Clear any repair you do not want, then:

1. Press **Preview Selected**.
2. Review the number of selected repairs and the backup notice.
3. Press **Apply Selected Repairs**.

The tool verifies that every old value still exists at its scanned location before it
writes anything. It then backs up each affected asset beneath
`Saved/AssetReferenceRepair/<timestamp>`. File edits and registry moves are applied as
one operation; a failure restores the already-written files and the previous registry.
After success, the Content Browser and Asset Dependency Viewer are refreshed.

An audit log is written to `Saved/Reports/AssetReferenceRepair_<timestamp>.txt`.

## Issue meanings

- **Missing path:** The referenced file no longer exists. A repair is offered only
  when exactly one Content asset has the same filename.
- **Mismatched ID:** The path resolves, but the adjacent stable asset ID belongs to a
  different or deleted asset. The resolved asset's registered ID is proposed.
- **Moved asset:** A registry path is stale, but an authored asset with the same stable
  ID exists elsewhere in Content.
- **Missing dependency:** A dependency ID has no registry entry. Restore/reimport the
  dependency or choose its replacement manually.
- **Missing import source:** The engine-owned asset still exists, but its external
  source file is unavailable. Locate the source or change it through Reimport.

Commit or otherwise preserve important project work before bulk repairs. Backups make
recovery straightforward, but source control remains the best way to review large
content changes.
