#include "engine/graphics/Mesh.h"
#include "engine/graphics/GpuProfiler.h"

#include <glad/glad.h>

#include <cstddef>
#include <algorithm>
#include <limits>

namespace engine {

Mesh::Mesh(const std::vector<float> &vertices, const std::vector<std::uint32_t> &indices, const VertexLayout &layout)
    : m_indexCount(static_cast<unsigned int>(indices.size()))
{
    const std::size_t floatsPerVertex =
        static_cast<std::size_t>(layout.Stride()) / sizeof(float);
    m_vertexStrideBytes = layout.Stride();
    m_vertexCount = floatsPerVertex == 0u ? 0u : vertices.size() / floatsPerVertex;
    UpdateBounds(vertices, layout);

    glGenVertexArrays(1, &m_vao);
    glBindVertexArray(m_vao);

    glGenBuffers(1, &m_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)),
                 vertices.data(), GL_STATIC_DRAW);

    glGenBuffers(1, &m_ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size() * sizeof(std::uint32_t)),
                 indices.data(), GL_STATIC_DRAW);

    // Walk the layout, configuring one attribute per entry. The running offset
    // tracks where each attribute starts within a vertex; the attribute index
    // doubles as the shader location (matching layout(location = N) in GLSL).
    const GLsizei stride = static_cast<GLsizei>(layout.Stride());
    std::size_t   offset = 0;
    unsigned int location = 0;
    for (const VertexAttribute& attr : layout.Attributes()) {
        glEnableVertexAttribArray(location);
        glVertexAttribPointer(location, static_cast<GLint>(attr.componentCount),
                              GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<const void*>(offset));
        offset += attr.componentCount * sizeof(float);
        ++location;
    }

    glBindVertexArray(0);  // leave no VAO bound
}

Mesh::~Mesh()
{
    Release();
}

std::vector<float> Mesh::ReadbackVertices() const
{
    const std::size_t strideFloats = VertexStrideFloats();
    if (m_vbo == 0 || m_vertexCount == 0 || strideFloats == 0) {
        return {};
    }
    std::vector<float> out(m_vertexCount * strideFloats);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glGetBufferSubData(GL_ARRAY_BUFFER, 0,
                       static_cast<GLsizeiptr>(out.size() * sizeof(float)),
                       out.data());
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return out;
}

std::vector<std::uint32_t> Mesh::ReadbackIndices() const
{
    if (m_ebo == 0 || m_indexCount == 0) {
        return {};
    }
    std::vector<std::uint32_t> out(m_indexCount);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glGetBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0,
                       static_cast<GLsizeiptr>(out.size() * sizeof(std::uint32_t)),
                       out.data());
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    return out;
}

Mesh::Mesh(Mesh &&other) noexcept
    : m_vao(other.m_vao), m_vbo(other.m_vbo), m_ebo(other.m_ebo),
      m_lodEbos(std::move(other.m_lodEbos)),
      m_lodIndexCounts(std::move(other.m_lodIndexCounts)),
      m_vertexCount(other.m_vertexCount), m_indexCount(other.m_indexCount),
      m_vertexStrideBytes(other.m_vertexStrideBytes),
      m_boundsCenter(other.m_boundsCenter), m_boundsRadius(other.m_boundsRadius),
      m_twoSided(other.m_twoSided)
{
    other.m_vao = other.m_vbo = other.m_ebo = 0;
    other.m_vertexCount = 0;
    other.m_indexCount = 0;
    other.m_vertexStrideBytes = 0;
    other.m_lodEbos.clear();
    other.m_lodIndexCounts.clear();
    other.m_boundsCenter = glm::vec3(0.0f);
    other.m_boundsRadius = 0.0f;
}
Mesh &Mesh::operator=(Mesh &&other) noexcept
{
    if (this != &other)
    {
        Release();
        m_vao = other.m_vao;
        m_vbo = other.m_vbo;
        m_ebo = other.m_ebo;
        m_lodEbos = std::move(other.m_lodEbos);
        m_lodIndexCounts = std::move(other.m_lodIndexCounts);
        m_vertexCount = other.m_vertexCount;
        m_indexCount = other.m_indexCount;
        m_vertexStrideBytes = other.m_vertexStrideBytes;
        m_boundsCenter = other.m_boundsCenter;
        m_boundsRadius = other.m_boundsRadius;
        m_twoSided = other.m_twoSided;
        other.m_vao = other.m_vbo = other.m_ebo = 0;
        other.m_vertexCount = 0;
        other.m_indexCount = 0;
        other.m_vertexStrideBytes = 0;
        other.m_lodEbos.clear();
        other.m_lodIndexCounts.clear();
        other.m_boundsCenter = glm::vec3(0.0f);
        other.m_boundsRadius = 0.0f;
    }
    return *this;
}
void Mesh::Draw() const
{
    DrawLod(0);
}

unsigned int Mesh::IndexCount(int level) const
{
    if (level <= 0 || m_lodIndexCounts.empty()) return m_indexCount;
    const int clamped = std::clamp(
        level - 1, 0, static_cast<int>(m_lodIndexCounts.size()) - 1);
    return m_lodIndexCounts[static_cast<std::size_t>(clamped)];
}

int Mesh::LodForTriangleBudget(std::size_t triangleBudget) const
{
    const std::size_t budget = std::max<std::size_t>(triangleBudget, 1u);
    int level = 0;
    while (level < MaxLod()
        && static_cast<std::size_t>(IndexCount(level)) / 3u > budget)
        ++level;
    return level;
}

void Mesh::DrawLod(int level) const
{
    if (!m_vao || !m_indexCount) return;   // empty (not yet uploaded)
    BindLod(level);
    GpuProfiler::RecordDrawCall();
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(IndexCount(level)),
                   GL_UNSIGNED_INT, nullptr);
}

void Mesh::BindLod(int level) const
{
    glBindVertexArray(m_vao);
    if (level > 0 && !m_lodEbos.empty()) {
        const int clamped = std::clamp(
            level - 1, 0, static_cast<int>(m_lodEbos.size()) - 1);
        const std::size_t index = static_cast<std::size_t>(clamped);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_lodEbos[index]);
    } else {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    }
}
void Mesh::Upload(const std::vector<float>& vertices,
                  const std::vector<std::uint32_t>& indices,
                  const VertexLayout& layout)
{
    if (!m_vao) glGenVertexArrays(1, &m_vao);
    if (!m_vbo) glGenBuffers(1, &m_vbo);
    if (!m_ebo) glGenBuffers(1, &m_ebo);

    glBindVertexArray(m_vao);

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)),
                 vertices.data(), GL_DYNAMIC_DRAW);   // reallocates; safe on size changes

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(indices.size() * sizeof(std::uint32_t)),
                 indices.data(), GL_DYNAMIC_DRAW);

    const GLsizei stride = static_cast<GLsizei>(layout.Stride());
    std::size_t   offset = 0;
    unsigned int  location = 0;
    for (const VertexAttribute& attr : layout.Attributes()) {
        glEnableVertexAttribArray(location);
        glVertexAttribPointer(location, static_cast<GLint>(attr.componentCount),
                              GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<const void*>(offset));
        offset += attr.componentCount * sizeof(float);
        ++location;
    }

    const std::size_t floatsPerVertex =
        static_cast<std::size_t>(layout.Stride()) / sizeof(float);
    m_vertexStrideBytes = layout.Stride();
    m_vertexCount = floatsPerVertex == 0u ? 0u : vertices.size() / floatsPerVertex;
    m_indexCount = static_cast<unsigned int>(indices.size());
    UpdateBounds(vertices, layout);
    glBindVertexArray(0);
}

void Mesh::UploadLodIndices(
    const std::vector<std::vector<std::uint32_t>>& lodIndices)
{
    if (!m_lodEbos.empty())
        glDeleteBuffers(static_cast<GLsizei>(m_lodEbos.size()), m_lodEbos.data());
    m_lodEbos.clear();
    m_lodIndexCounts.clear();
    if (!m_vao || lodIndices.empty()) return;

    m_lodEbos.resize(lodIndices.size(), 0u);
    m_lodIndexCounts.reserve(lodIndices.size());
    glGenBuffers(static_cast<GLsizei>(m_lodEbos.size()), m_lodEbos.data());
    glBindVertexArray(m_vao);
    for (std::size_t i = 0; i < lodIndices.size(); ++i) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_lodEbos[i]);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(lodIndices[i].size() * sizeof(std::uint32_t)),
            lodIndices[i].data(), GL_STATIC_DRAW);
        m_lodIndexCounts.push_back(
            static_cast<unsigned int>(lodIndices[i].size()));
    }
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBindVertexArray(0);
}

void Mesh::UpdateBounds(const std::vector<float>& vertices,
                        const VertexLayout& layout)
{
    m_boundsCenter = glm::vec3(0.0f);
    m_boundsRadius = 0.0f;
    if (layout.Attributes().empty()
        || layout.Attributes().front().componentCount < 3u) return;
    const std::size_t stride =
        static_cast<std::size_t>(layout.Stride()) / sizeof(float);
    if (stride < 3u || vertices.size() < 3u) return;

    glm::vec3 minimum(std::numeric_limits<float>::max());
    glm::vec3 maximum(std::numeric_limits<float>::lowest());
    for (std::size_t offset = 0; offset + 2u < vertices.size(); offset += stride) {
        const glm::vec3 position(
            vertices[offset], vertices[offset + 1u], vertices[offset + 2u]);
        minimum = glm::min(minimum, position);
        maximum = glm::max(maximum, position);
    }
    m_boundsCenter = (minimum + maximum) * 0.5f;
    // Half the AABB diagonal is conservative for every vertex and avoids another
    // full pass over large terrain vertex buffers.
    m_boundsRadius = 0.5f * glm::length(maximum - minimum);
}

void Mesh::UpdateVertices(const std::vector<float>& vertices)
{
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    static_cast<GLsizeiptr>(vertices.size() * sizeof(float)), vertices.data());
}
void Mesh::Release()
{
    // Empty meshes are used as editor placeholders and by headless tools/tests.
    // GLAD may not have been initialized in those processes, so even binding VAO
    // zero would call through a null OpenGL function pointer.
    if (m_vao == 0 && m_vbo == 0 && m_ebo == 0 && m_lodEbos.empty()) {
        m_lodIndexCounts.clear();
        m_vertexCount = 0;
        m_indexCount = 0;
        m_vertexStrideBytes = 0;
        m_boundsCenter = glm::vec3(0.0f);
        m_boundsRadius = 0.0f;
        return;
    }
    // glDelete* ignore 0, but guarding makes the intent explicit.
    // Mesh draws keep their VAO bound to avoid a per-draw state change; clear it
    // at lifetime boundaries before deleting/replacing the object.
    glBindVertexArray(0);
    if (m_ebo) glDeleteBuffers(1, &m_ebo);
    if (!m_lodEbos.empty())
        glDeleteBuffers(static_cast<GLsizei>(m_lodEbos.size()), m_lodEbos.data());
    if (m_vbo) glDeleteBuffers(1, &m_vbo);
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
    m_vao = m_vbo = m_ebo = 0;
    m_lodEbos.clear();
    m_lodIndexCounts.clear();
    m_vertexCount = 0;
    m_indexCount = 0;
    m_vertexStrideBytes = 0;
    m_boundsCenter = glm::vec3(0.0f);
    m_boundsRadius = 0.0f;
}
void Mesh::UpdateVertices(const std::vector<float>& vertices,
                          const VertexLayout& layout)
{
    UpdateVertices(vertices);
    UpdateBounds(vertices, layout);
}
void Mesh::UpdateVertexRange(std::size_t firstVertex,
                             const std::vector<float>& vertices)
{
    if (!m_vbo || vertices.empty()) return;
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    // Terrain vertices use the mesh's original interleaved layout. The caller
    // supplies complete vertices, so the byte offset is derived from the stored
    // vertex stride captured during Upload.
    glBufferSubData(GL_ARRAY_BUFFER,
        static_cast<GLintptr>(firstVertex * m_vertexStrideBytes),
        static_cast<GLsizeiptr>(vertices.size() * sizeof(float)), vertices.data());
}

} // namespace engine
