#include "EditorLineRenderer.h"

#include <engine/graphics/Shader.h>

#include <glad/glad.h>

#include <algorithm>

EditorLineRenderer::~EditorLineRenderer()
{
    if (m_vertexBuffer) glDeleteBuffers(1, &m_vertexBuffer);
    if (m_vertexArray) glDeleteVertexArrays(1, &m_vertexArray);
}

void EditorLineRenderer::Clear()
{
    m_vertices.clear();
}

void EditorLineRenderer::AddLine(const glm::vec3& a, const glm::vec3& b,
                                 const glm::vec3& color)
{
    const glm::vec3 vertices[] = {a, b};
    for (const glm::vec3& position : vertices) {
        m_vertices.push_back(position.x);
        m_vertices.push_back(position.y);
        m_vertices.push_back(position.z);
        m_vertices.push_back(color.r);
        m_vertices.push_back(color.g);
        m_vertices.push_back(color.b);
    }
}

bool EditorLineRenderer::EnsureGpuResources()
{
    if (!m_shader) {
        static const char* vertexSource = R"GLSL(
#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aColor;
uniform mat4 uViewProjection;
out vec3 vColor;
void main() {
    vColor = aColor;
    gl_Position = uViewProjection * vec4(aPosition, 1.0);
}
)GLSL";
        static const char* fragmentSource = R"GLSL(
#version 330 core
in vec3 vColor;
uniform float uOpacity;
out vec4 FragColor;
void main() {
    FragColor = vec4(vColor, uOpacity);
}
)GLSL";
        engine::ShaderCompileReport report;
        m_shader = engine::Shader::TryCompile(
            vertexSource, fragmentSource, report);
        if (!m_shader) return false;
    }

    if (!m_vertexArray) glGenVertexArrays(1, &m_vertexArray);
    if (!m_vertexBuffer) glGenBuffers(1, &m_vertexBuffer);
    return m_vertexArray != 0 && m_vertexBuffer != 0;
}

void EditorLineRenderer::Draw(const glm::mat4& viewProjection, float width,
                              bool showOccluded, bool depthTest)
{
    if (m_vertices.empty() || !EnsureGpuResources()) return;

    GLboolean depthEnabled = glIsEnabled(GL_DEPTH_TEST);
    GLboolean blendEnabled = glIsEnabled(GL_BLEND);
    GLboolean depthWrite = GL_TRUE;
    GLfloat oldWidth = 1.0f;
    GLint oldProgram = 0;
    GLint oldVertexArray = 0;
    GLint oldBlendSource = GL_ONE;
    GLint oldBlendDestination = GL_ZERO;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWrite);
    glGetFloatv(GL_LINE_WIDTH, &oldWidth);
    glGetIntegerv(GL_CURRENT_PROGRAM, &oldProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &oldVertexArray);
    glGetIntegerv(GL_BLEND_SRC_RGB, &oldBlendSource);
    glGetIntegerv(GL_BLEND_DST_RGB, &oldBlendDestination);

    glBindVertexArray(m_vertexArray);
    glBindBuffer(GL_ARRAY_BUFFER, m_vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(m_vertices.size() * sizeof(float)),
        m_vertices.data(), GL_DYNAMIC_DRAW);
    constexpr GLsizei stride = static_cast<GLsizei>(6 * sizeof(float));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
        reinterpret_cast<const void*>(3 * sizeof(float)));

    m_shader->Bind();
    m_shader->SetMat4("uViewProjection", viewProjection);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glLineWidth(std::max(width, 1.0f));
    const GLsizei vertexCount =
        static_cast<GLsizei>(m_vertices.size() / 6);

    if (!depthTest) {
        glDisable(GL_DEPTH_TEST);
        m_shader->SetFloat("uOpacity", 1.0f);
        glDrawArrays(GL_LINES, 0, vertexCount);
    } else if (showOccluded) {
        glDisable(GL_DEPTH_TEST);
        m_shader->SetFloat("uOpacity", 0.18f);
        glDrawArrays(GL_LINES, 0, vertexCount);
    }

    if (depthTest) {
        glEnable(GL_DEPTH_TEST);
        m_shader->SetFloat("uOpacity", 1.0f);
        glDrawArrays(GL_LINES, 0, vertexCount);
    }

    glDepthMask(depthWrite);
    glLineWidth(oldWidth);
    glBlendFunc(oldBlendSource, oldBlendDestination);
    if (!depthEnabled) glDisable(GL_DEPTH_TEST);
    if (!blendEnabled) glDisable(GL_BLEND);
    glUseProgram(static_cast<GLuint>(oldProgram));
    glBindVertexArray(static_cast<GLuint>(oldVertexArray));
}
