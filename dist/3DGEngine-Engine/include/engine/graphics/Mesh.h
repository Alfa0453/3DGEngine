#pragma once

#include "engine/graphics/VertexLayout.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace engine {

// Owns one piece of geometry on the GPU: a vertex array (VAO), a vertex buffer
// (VBO) and an index buffer (EBO). Move-only, because it owns GL resources.
//
// Construct it with interleaved float vertex data, a list of indices, and a
// VertexLayout describing the vertex format; then call Draw(). All the buffer
// bookkeeping that used to clutter the game now lives here, once.
class Mesh {
public:
    Mesh() = default;   // empty, no GL objects yet; fill with Upload()
    Mesh(const std::vector<float>& vertices,
         const std::vector<std::uint32_t>& indices,
         const VertexLayout& layout);
    ~Mesh();

    Mesh(const Mesh&)            = delete;
    Mesh& operator=(const Mesh&) = delete;
    Mesh(Mesh&& other) noexcept;
    Mesh& operator=(Mesh&& other) noexcept;

    // Bind the VAO and issue the indexed draw call.
    void Draw() const;
    void DrawLod(int level) const;
    void BindLod(int level) const;

    // (Re)upload geometry into THIS mesh's own GL objects, creating them on first
    // use and reallocating in place afterward. Unlike destroying + recreating a Mesh,
    // this never deletes the VAO/VBO/EBO, so it is safe to call every frame (e.g. when
    // a terrain is regenerated) with no risk of deleting a still-bound VAO. The vertex
    // count and index count may change between calls.
    void Upload(const std::vector<float>& vertices,
                const std::vector<std::uint32_t>& indices,
                const VertexLayout& layout);
    void UploadLodIndices(
        const std::vector<std::vector<std::uint32_t>>& lodIndices);

    bool Valid() const { return m_vao != 0 && m_indexCount != 0; }

    // Replace the interleaved vertex data in place (same layout + vertex count;
    // indices unchanged). A cheap VBO sub-update -- used for live terrain sculpting
    // instead of recreating the whole Mesh each stroke.
    void UpdateVertices(const std::vector<float>& vertices);
    void UpdateVertices(const std::vector<float>& vertices,
                        const VertexLayout& layout);
    void UpdateVertexRange(std::size_t firstVertex,
                           const std::vector<float>& vertices);

    // Geometry statistics retained alongside the GPU buffers. Keeping these on
    // Mesh avoids a costly/readback-only OpenGL query and works for primitives,
    // imported models, terrain, particles, and dynamically uploaded geometry.
    std::size_t VertexCount() const { return m_vertexCount; }
    unsigned int IndexCount() const { return m_indexCount; }
    unsigned int IndexCount(int level) const;
    int MaxLod() const { return static_cast<int>(m_lodEbos.size()); }
    int LodForTriangleBudget(std::size_t triangleBudget) const;
    std::size_t TriangleCount() const {
        return static_cast<std::size_t>(m_indexCount) / 3u;
    }

    // Local-space geometry bounds, calculated when vertex data is uploaded.
    // Renderers use the sphere for conservative frustum culling; unlike the old
    // unit-object assumption this remains correct for terrain and imported meshes.
    const glm::vec3& BoundsCenter() const { return m_boundsCenter; }
    float BoundsRadius() const { return m_boundsRadius; }
    void SetBounds(const glm::vec3& center, float radius) {
        m_boundsCenter = center;
        m_boundsRadius = std::max(radius, 0.0f);
    }

    // Flat, single-face geometry (a plane/quad) is visible from both sides, so the
    // renderer skips backface culling for it. Closed solids leave this false.
    bool TwoSided() const { return m_twoSided; }
    void SetTwoSided(bool twoSided) { m_twoSided = twoSided; }

    // The vertex array object, so callers can attach per-instance attributes for
    // instanced drawing (see PbrRenderer's instanced lit pass).
    unsigned int Vao() const { return m_vao; }

    // Number of floats per vertex (stride / sizeof(float)). Primitives are 8
    // (position3/normal3/uv2); imported models are 11 (+ tangent3).
    std::size_t VertexStrideFloats() const {
        return m_vertexStrideBytes == 0u ? 0u : m_vertexStrideBytes / sizeof(float);
    }

    // Read the interleaved vertex floats / index data back from the GPU buffers.
    // Editor-time only (used by the mesh-merge baker) — a GPU→CPU readback is far
    // too slow for per-frame use but fine for a one-shot bake. Returns empty when
    // there is no geometry. The caller must know the vertex layout (see
    // VertexStrideFloats) to interpret the floats.
    std::vector<float> ReadbackVertices() const;
    std::vector<std::uint32_t> ReadbackIndices() const;

private:
    void Release();     // delete the GL objects (used by dtor + move-assign)
    void UpdateBounds(const std::vector<float>& vertices,
                      const VertexLayout& layout);

    unsigned int m_vao = 0;
    unsigned int m_vbo = 0;
    unsigned int m_ebo = 0;
    std::vector<unsigned int> m_lodEbos;
    std::vector<unsigned int> m_lodIndexCounts;
    std::size_t m_vertexCount = 0;
    unsigned int m_indexCount = 0;
    std::size_t m_vertexStrideBytes = 0;
    glm::vec3 m_boundsCenter{0.0f};
    float m_boundsRadius = 0.0f;
    bool m_twoSided = false;
};

} // namespace engine
