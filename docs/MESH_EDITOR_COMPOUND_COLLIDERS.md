# Mesh Editor Compound Colliders

Imported static meshes can store a reusable set of collision shapes inside the
engine-owned `.3dgmesh` asset. Each scene instance receives that set when the
mesh is placed. This makes it possible to approximate an irregular mesh with
several inexpensive primitives instead of one oversized box or a costly triangle
mesh.

## Author collision on a mesh asset

1. Double-click an imported `.3dgmesh` in the Assets panel to open Mesh Editor.
2. Open the **Collision** tab.
3. Add one or more Box, Sphere, Capsule, Convex Hull, or Triangle Mesh shapes.
4. Select a shape in the list and edit its size, local position, local rotation,
   local scale, trigger state, friction, and restitution.
5. Use **Fit Selected To Mesh Bounds** as a starting point, then duplicate and
   resize shapes to cover separate parts of the model.
6. Click **Save Collider Set**.

The preview draws the selected shape in orange and the other shapes in green.
Collider offsets are mesh-local, so changing the mesh origin in Mesh Editor also
moves the stored offsets with the geometry. Reimporting source geometry preserves
the collider set.

## Use and override collision in a level

- A newly placed mesh automatically receives the saved collider set.
- Select an existing mesh instance and expand **Collider > Compound Shapes** in
  Inspector.
- Use **Reload Colliders From Mesh Asset** to replace the instance overrides with
  the current asset defaults.
- Edit or remove individual extra shapes directly in Inspector. These changes are
  per-instance and do not modify the source mesh asset.
- Add Box, Sphere, or Capsule shapes when one instance needs extra coverage.

The first authored shape is the primary collider for compatibility with existing
scripts and components. Every later shape belongs to the same entity and shares
the same rigid body. Physics contacts, triggers, ray traces, sphere traces,
overlap queries, CCD, scene save/load, and packaged runtime scenes all include the
complete set.

## Recommended practice

- Prefer a small number of boxes, spheres, and capsules for movable objects.
- Use Convex Hull when primitives cannot approximate a movable mesh well enough.
- Reserve Triangle Mesh collision for static level geometry.
- Keep overlapping shapes modest. Excessive overlap creates redundant contacts.
- Use per-instance Inspector edits for exceptional placements; keep common
  collision in the mesh asset so every new instance is correct by default.
