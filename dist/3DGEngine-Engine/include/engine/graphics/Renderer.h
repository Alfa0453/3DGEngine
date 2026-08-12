#pragma once

#include <glm/glm.hpp>

namespace engine{

class Mesh;   // forward declaration — Renderer.cpp includes the full Mesh.h
// A thin layer over the OpenGL render state. Today it owns the clear colour,
// enables depth testing, clears the screen each frame, and issues draw calls.
// As the engine grows this is where shader/material binding, render-target
// management and draw-call batching will live, so the game never touches GL.
class Renderer {
public:
    void Init();                           // one-time GL state (depth test, ...)

    void SetClearColor(const glm::vec4& color) { m_clearColor = color; }

    void Clear() const;                    // clear the colour + depth buffers
    void Draw(const Mesh& mesh) const;     // issue one indexed draw call
    void SetMultisample(bool enabled) const;   // toggle MSAA on the default framebuffer

private:
    glm::vec4 m_clearColor{0.06f, 0.07f, 0.09f, 1.0f};
};

} // namespace engine