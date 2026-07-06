#pragma once

#include <string>

// Forward-declare GLFW's window type so this header does NOT pull in the whole
// GLFW/OpenGL API. Anything that includes Window.h stays lightweight; only the
// .cpp needs the real <GLFW/glfw3.h>.
struct GLFWwindow;

namespace engine {

// Plain data describing how to create a window. Aggregate-initialisable, with
// sensible defaults, so callers can override only what they care about.
struct WindowProps {
    std::string title = "GameEngine";
    int width = 1280;
    int height = 720;
    bool vsync = true;  // cap the frame rate to the monitor refresh
};

// An RAII wrapper around a GLFW window and its OpenGL context.
//
//   * Construction creates the OS window, makes its GL context current, and
//     loads the OpenGL function pointers via GLAD.
//   * Destruction tears the window down and, when the last window closes,
//     shuts GLFW down too.
//
// A window owns a unique operating-system resource, so it is non-copyable.
class Window {
public:
    explicit Window(const WindowProps& props = {});
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // Swap the back buffer to the screen and pump the OS event queue.
    // Call once per frame.
    void Update();

    bool ShouldClose() const;
    void SetShouldClose(bool value);

    // Replace the window's title-bar text (used here as a simple scoreboard).
    void SetTitle(const std::string& title);

    int Width() const { return m_data.width; }
    int Height() const { return m_data.height; }

    // Width / height, guarding against a zero-height (minimised) window.
    float AspectRatio() const {
        return m_data.height == 0
            ? 1.0f
            : static_cast<float>(m_data.width) / static_cast<float>(m_data.height);
    }

    // --- Input -----------------------------------------------------------
    // True while `key` (a GLFW_KEY_* code) is held down this frame.
    bool IsKeyPressed(int key) const;
    bool IsMouseButtonPressed(int button) const;

    // Lock the cursor to the window and hide it (FPS-style mouse look), or
    // release it back to a normal desktop cursor.
    void SetCursorCaptured(bool captured);

    // Mouse movement since the previous frame, in pixels. Reset automatically
    // at the start of each Update(), so read it in OnUpdate.
    float MouseDeltaX() const { return static_cast<float>(m_data.mouseDeltaX); }
    float MouseDeltaY() const { return static_cast<float>(m_data.mouseDeltaY); }

    // Mouse wheel movement since the previous frame. Positive Y means wheel up,
    // negative Y means wheel down.
    float ScrollDeltaY() const { return static_cast<float>(m_data.scrollDeltaY); }

    // --- Display options -------------------------------------------------
    // Switch between windowed and borderless fullscreen on the primary monitor.
    void ToggleFullscreen();
    bool IsFullscreen() const { return m_data.fullscreen; }

    // Vertical sync: cap the frame rate to the monitor refresh.
    void SetVSync(bool enabled);
    void ToggleVSync() { SetVSync(!m_data.vsync); }
    bool IsVSync() const { return m_data.vsync; }

    // True while the window is minimised (or has a zero-size framebuffer), so
    // the app can skip rendering.
    bool IsMinimized() const;

    // Escape hatch for systems that need the raw handle (e.g. input polling).
    GLFWwindow* Native() const { return m_window; }

private:
    GLFWwindow* m_window = nullptr;

    // State we want GLFW's C callbacks to reach. We hand a pointer to this
    // struct to glfwSetWindowUserPointer, then recover it inside callbacks.
    struct Data {
        std::string title;
        int         width  = 0;
        int         height = 0;
        bool        vsync  = true;

        // Mouse-look bookkeeping.
        double mouseDeltaX = 0.0;   // accumulated movement this frame
        double mouseDeltaY = 0.0;
        double lastMouseX  = 0.0;   // previous cursor position
        double lastMouseY  = 0.0;
        double scrollDeltaY = 0.0;  // accumulated wheel movement this frame
        bool  firstMouse   = true;  // skip the huge delta on the first event
        int   settleFrames = 0;     // discard mouse motion right after capture

        // Display state.
        bool fullscreen = false;
        int  windowedX = 0, windowedY = 0;  // saved placement before fullscreen
        int  windowedW = 0, windowedH = 0;
    } m_data;
};

} // namespace engine