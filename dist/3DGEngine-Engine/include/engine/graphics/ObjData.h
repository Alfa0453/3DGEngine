#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace engine::obj {

// A material as it appears in a .mtl file. Texture is kept as a (resolved) path
// here; the GPU Texture is created later, by the GL-side Model loader.
struct MatDef {
    std::string name;
    glm::vec3   diffuse{0.8f};
    glm::vec3   specular{0.2f};
    float       shininess = 32.0f;
    std::string diffuseMapPath;   // empty if none
};

// All triangles that share one material, as indices into ObjData::vertices.
struct Group {
    int                        material = -1;   // index into ObjData::materials
    std::vector<std::uint32_t> indices;
};

// The CPU-side result of parsing an .obj: one interleaved vertex buffer
// (position3, normal3, uv2 — matching VertexLayout{3,3,2}), the triangle groups
// that index into it, and the materials they reference. No OpenGL involved, so
// this is unit-testable headlessly.
struct ObjData {
    std::vector<float>  vertices;   // 8 floats per vertex
    std::vector<Group>  groups;
    std::vector<MatDef> materials;

    static constexpr int kFloatsPerVertex = 8;
    std::size_t VertexCount() const { return vertices.size() / kFloatsPerVertex; }
};

// Parse a Wavefront .obj (and any .mtl it names, resolved relative to the .obj).
// Faces are fan-triangulated; missing normals are generated (smooth); missing
// UVs default to (0,0). Throws std::runtime_error if the .obj cannot be opened.
ObjData ParseOBJ(const std::string& path);

} // namespace engine::obj