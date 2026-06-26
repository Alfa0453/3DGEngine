#pragma once

#include "engine/graphics/Mesh.h"

namespace engine {
namespace primitives {

// Factory functions for common shapes. Every primitive uses the vertex layout
// { position(3), normal(3), texcoord(2) }, so the meshes drop straight into the
// lit / phong_color shaders with no changes.
//
// Each returns a freshly built engine::Mesh (by move). Call them after the GL
// context exists (e.g. in OnInit).

// A unit cube spanning -0.5..0.5 on each axis. 24 vertices (per-face normals).
Mesh Cube();

// A unit quad in the XY plane (z = 0), facing +Z. Handy for sprites/decals.
Mesh Quad();

// A flat plane in the XZ plane (y = 0), facing +Y, `size` units across, with
// texture coordinates repeating `uvTiling` times. Handy for ground/floors.
Mesh Plane(float size = 1.0f, float uvTiling = 1.0f);

// A unit-diameter UV sphere (radius 0.5) with `segments` latitude bands (and
// 2*segments longitude bands). Normals point outward; UVs wrap longitude/latitude.
Mesh Sphere(int segments = 16);

} // namespace primitives
} // namespace engine