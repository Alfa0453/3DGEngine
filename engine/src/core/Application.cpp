#include "engine/core/Application.h"

#include <GLFW/glfw3.h>  // glfwGetTime — a high-resolution clock GLFW exposes

namespace engine {

engine::Application::Application(const WindowProps &props)
    : m_window(std::make_unique<Window>(props))
{
}
void engine::Application::Run()
{
    m_running = true;
    OnInit();

    float lastTime = static_cast<float>(glfwGetTime());

    while (m_running && !m_window->ShouldClose()) {
        const float now = static_cast<float>(glfwGetTime());
        float frameTime = now - lastTime;
        lastTime = now;

        // Guard against the "spiral of death": if one frame takes very long
        // (a stall, dragging the window), cap it so the fixed loop below cannot
        // run an unbounded number of steps trying to catch up.
        if (frameTime > 0.25f) frameTime = 0.25f;

        // Per-frame, variable-rate work (input, camera, UI intent).
        OnUpdate(frameTime);    // advance the simulation

        // Fixed-rate physics. Bank the elapsed time and spend it one fixed step
        // at a time, so the simulation always advances by exactly m_fixedDelta.
        m_accumulator += frameTime;
        while (m_accumulator >= m_fixedDelta) {
            OnFixedUpdate(m_fixedDelta);
            m_accumulator -= m_fixedDelta;
        }

        // Whatever time is left over (< one step) becomes the interpolation
        // factor, so rendering can blend between the last two fixed states.
        m_alpha = m_accumulator / m_fixedDelta;

        OnRender();             // draw the current state
        m_window->Update();     // present the frame + handle events
    }

    OnShutdown();
}

} // namespace engine