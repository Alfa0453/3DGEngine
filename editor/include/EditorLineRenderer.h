#pragma once

#include <glm/glm.hpp>

#include <memory>
#include <vector>

namespace engine { class Shader; }

// Lightweight editor-only wire renderer. Lines are accumulated on the CPU and
// submitted in one draw call, avoiding the hundreds of cube draws previously
// used to imitate collider edges.
class EditorLineRenderer {
public:
    EditorLineRenderer() = default;
    ~EditorLineRenderer();

    EditorLineRenderer(const EditorLineRenderer&) = delete;
    EditorLineRenderer& operator=(const EditorLineRenderer&) = delete;

    void Clear();
    void AddLine(const glm::vec3& a, const glm::vec3& b,
                 const glm::vec3& color);
    void Draw(const glm::mat4& viewProjection, float width = 1.5f,
              bool showOccluded = true);

private:
    bool EnsureGpuResources();

    std::vector<float> m_vertices;
    std::unique_ptr<engine::Shader> m_shader;
    unsigned int m_vertexArray = 0;
    unsigned int m_vertexBuffer = 0;
};
