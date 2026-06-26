#include "engine/graphics/Renderer.h"
#include "engine/graphics/Mesh.h"

#include <glad/glad.h>

namespace engine {

void Renderer::Init() {
    // Keep the fragment nearest the camera. Set once; applies to every draw.
    glEnable(GL_DEPTH_TEST);
}

void Renderer::Clear() const {
    glClearColor(m_clearColor.r, m_clearColor.g, m_clearColor.b, m_clearColor.a);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Renderer::Draw(const Mesh& mesh) const {
    // The caller has already bound the shader and set its uniforms; we just
    // issue the geometry. (Shader/material binding will move here later.)
    mesh.Draw();
}

} // namespace engine