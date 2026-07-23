#pragma once

#include "engine/graphics/VertexLayout.h"

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

    // (Re)upload geometry into THIS mesh's own GL objects, creating them on first
    // use and reallocating in place afterward. Unlike destroying + recreating a Mesh,
    // this never deletes the VAO/VBO/EBO, so it is safe to call every frame (e.g. when
    // a terrain is regenerated) with no risk of deleting a still-bound VAO. The vertex
    // count and index count may change between calls.
    void Upload(const std::vector<float>& vertices,
                const std::vector<std::uint32_t>& indices,
                const VertexLayout& layout);

    bool Valid() const { return m_vao != 0 && m_indexCount != 0; }

    // Replace the interleaved vertex data in place (same layout + vertex count;
    // indices unchanged). A cheap VBO sub-update -- used for live terrain sculpting
    // instead of recreating the whole Mesh each stroke.
    void UpdateVertices(const std::vector<float>& vertices);

    unsigned int IndexCount() const { return m_indexCount; }

    // The vertex array object, so callers can attach per-instance attributes for
    // instanced drawing (see PbrRenderer's instanced lit pass).
    unsigned int Vao() const { return m_vao; }

private:
    void Release();     // delete the GL objects (used by dtor + move-assign)

    unsigned int m_vao = 0;
    unsigned int m_vbo = 0;
    unsigned int m_ebo = 0;
    unsigned int m_indexCount = 0;
};

} // namespace engine