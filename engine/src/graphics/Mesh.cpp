#include "engine/graphics/Mesh.h"

#include <glad/glad.h>

#include <cstddef>

namespace engine {

Mesh::Mesh(const std::vector<float> &vertices, const std::vector<std::uint32_t> &indices, const VertexLayout &layout)
    : m_indexCount(static_cast<unsigned int>(indices.size()))
{
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

Mesh::Mesh(Mesh &&other) noexcept
    : m_vao(other.m_vao), m_vbo(other.m_vbo), m_ebo(other.m_ebo),
      m_indexCount(other.m_indexCount)
{
    other.m_vao = other.m_vbo = other.m_ebo = 0;
    other.m_indexCount = 0;
}
Mesh &Mesh::operator=(Mesh &&other) noexcept
{
    if (this != &other)
    {
        Release();
        m_vao = other.m_vao;
        m_vbo = other.m_vbo;
        m_ebo = other.m_ebo;
        m_indexCount = other.m_indexCount;
        other.m_vao = other.m_vbo = other.m_ebo = 0;
        other.m_indexCount = 0;
    }
    return *this;
}
void Mesh::Draw() const
{
    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(m_indexCount), GL_UNSIGNED_INT, nullptr);
}
void Mesh::UpdateVertices(const std::vector<float>& vertices)
{
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    static_cast<GLsizeiptr>(vertices.size() * sizeof(float)), vertices.data());
}
void Mesh::Release()
{
    // glDelete* ignore 0, but guarding makes the intent explicit.
    if (m_ebo) glDeleteBuffers(1, &m_ebo);
    if (m_vbo) glDeleteBuffers(1, &m_vbo);
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
    m_vao = m_vbo = m_ebo = 0;
    m_indexCount = 0;
}

} // namespace engine
