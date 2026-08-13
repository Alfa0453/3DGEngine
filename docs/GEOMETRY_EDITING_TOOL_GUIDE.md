# Geometry Editing Tool Guide

The Geometry Editing Tool is built into the **Mesh Editor** and edits an
engine-owned `.3dgmesh` asset directly. It is intended for blockout changes,
small repairs, pivot correction, and vertex-paint preparation without returning
to an external modelling package.

## Open and select

1. Double-click a static `.3dgmesh` in the Content panel.
2. Select **Geometry** in the Mesh Editor.
3. Orbit with the right mouse button and zoom with the wheel.
4. Choose **Vertex**, **Edge**, or **Face** mode.
5. Click a component. Shift-click adds or removes components.

Use **Select All**, **Connected**, and **Clear** to manage the selection.
Connected selection stays within its material section. Edge mode selects the
face adjoining the picked edge so the nearby topology can be refined.

Geometry editing is static-mesh-only. Changing skeletal topology would
invalidate skin weights and animation data.

## Operations

- **Extrude Faces** creates displaced caps and side walls along face normals.
- **Inset Faces** creates smaller center faces and connected border rings.
- **Bevel Faces** combines inset and extrusion.
- **Subdivide / Add Edge Loops** splits each selected triangle into four.
- **Delete Faces** removes selected triangles.
- **Weld Coincident Vertices** merges vertices within the chosen tolerance.
- **Bridge Split Vertex Selection** joins two equal vertex chains in one submesh.
- **Recalculate Normals + Tangents** repairs lighting vectors from geometry and UVs.
- **Remove Degenerate Faces** removes zero-area or repeated-index triangles.

The topology summary reports boundary edges, non-manifold edges, and degenerate
faces. Boundaries are valid for an intentionally open mesh. Non-manifold edges
are shared by more than two faces and should normally be repaired.

## Asset safety

New vertices interpolate the full engine vertex record, including UVs, normals,
tangents, and vertex colors. Operations remain in their original submesh, which
preserves material sections. Applying rewrites the same native asset, preserving
its asset identity and existing level references.

Use **Undo Geometry** and **Redo Geometry** while working. Changes remain in
preview until **Apply Geometry To Asset** is pressed. **Revert Geometry** restores
the mesh loaded when the editor opened. Reimporting can replace destructive
geometry edits, so major production topology work still belongs in a dedicated
modelling application.

Recommended finish: remove degenerates, rebuild normals/tangents, confirm the
non-manifold count, apply, and test the mesh material and collision in its level.
