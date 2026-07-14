#include "engine/core/Window.h"

// GLAD must be included before GLFW: it provides the OpenGL headers, and GLFW
// detects that they are already present and does not include its own.
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <iostream>
#include <stdexcept>

namespace engine {
namespace {

// GLFW is a process-wide C library. We init it once for the first window and
// terminate it after the last one is destroyed, tracked by this counter.
int s_windowCount = 0;

void GLFWErrorCallback(int code, const char* description) {
    std::cerr << "[GLFW] Error " << code << ": " << description << '\n';
}

} // anonymous namespace

Window::Window(const WindowProps& props) {
    m_data.title = props.title;
    m_data.width = props.width;
    m_data.height = props.height;
    m_data.vsync = props.vsync;

    // ---- Initialise GLFW (first window only) -----------------------------
    if (s_windowCount == 0) {
        glfwSetErrorCallback(GLFWErrorCallback);
        if (!glfwInit()) {
            throw std::runtime_error("Failed to initialise GLFW");
        }
    }

    // Request an OpenGL 3.3 Core profile context. "Core" drops the old
    // fixed-function pipeline and forces the modern shader-based API.
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_STENCIL_BITS, 8);
#ifdef __APPLE__
    // macOS only grants a Core context when forward-compatibility is set.
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    // ---- Create the window ----------------------------------------------
    m_window = glfwCreateWindow(m_data.width, m_data.height, m_data.title.c_str(), nullptr, nullptr);
    if (!m_window) {
        if (s_windowCount == 0) glfwTerminate();
        throw std::runtime_error("Failed to create GLFW window");
    }
    ++s_windowCount;

    // A GL context is bound to a thread, not a window. Make ours current so
    // subsequent GL calls target this window.
    glfwMakeContextCurrent(m_window);

    // ---- Load OpenGL function pointers via GLAD --------------------------
    // The driver exposes GL functions at runtime; GLAD fetches their addresses
    // using GLFW's loader. Nothing GL-related works before this succeeds.
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        throw std::runtime_error("Failed to initialize GLAD (OpenGL loader)");
    }

    std::cout << "OpenGL " << glGetString(GL_VERSION)
              << " | "      << glGetString(GL_RENDERER) << '\n';

    // ---- Wire up callbacks ----------------------------------------------
    // Stash a pointer to m_data so static callbacks can read/write our state.
    glfwSetWindowUserPointer(m_window, &m_data);

    // Keep the GL viewport matched to the framebuffer when the window resizes.
    // (Framebuffer size != window size on high-DPI displays, so we use the
    // framebuffer callback, not the window-size one.)
    glfwSetFramebufferSizeCallback(m_window,
        [](GLFWwindow* window, int width, int height) {
            auto* data = static_cast<Data*>(glfwGetWindowUserPointer(window));
            data->width = width;
            data->height = height;
            glViewport(0, 0, width, height);
        }
    );

    // Accumulate raw mouse movement for FPS-style look. We compute a delta from
    // the previous position rather than reading absolute coordinates.
    glfwSetCursorPosCallback(m_window,
        [](GLFWwindow* win, double x, double y){
            auto* data = static_cast<Data*>(glfwGetWindowUserPointer(win));
            if (data->firstMouse)
            {
                data->lastMouseX = x;
                data->lastMouseY = y;
                data->firstMouse = false;
            }
            data->mouseDeltaX += x - data->lastMouseX;
            data->mouseDeltaY += y - data->lastMouseY;
            data->lastMouseX = x;
            data->lastMouseY = y;
        });

    glfwSetScrollCallback(m_window,
        [](GLFWwindow* win, double, double yoffset) {
            auto* data = static_cast<Data*>(glfwGetWindowUserPointer(win));
            data->scrollDeltaY += yoffset;
        });

    SetVSync(m_data.vsync);
}

Window::~Window() {
    if (m_window) {
        glfwDestroyWindow(m_window);
        --s_windowCount;
    }
    if (s_windowCount == 0) {
        glfwTerminate();
    }
}

void Window::Update() {
    // Clear last frame's accumulated mouse movement, then pump events so the
    // cursor callback fills it with this frame's movement (ready for next
    // frame's OnUpdate).
    m_data.mouseDeltaX = 0.0;
    m_data.mouseDeltaY = 0.0;
    m_data.scrollDeltaY = 0.0;

    glfwSwapBuffers(m_window);  // present the frame we just drew
    glfwPollEvents();           // deliver input / resize / close events

    // Right after the cursor is captured, GLFW re-centres it and can report a
    // huge one-off jump. Discard the motion gathered during those first frames
    // so the camera does not snap on startup.
    if (m_data.settleFrames > 0) {
        --m_data.settleFrames;
        m_data.mouseDeltaX = 0.0;
        m_data.mouseDeltaY = 0.0;
    }
}

bool Window::ShouldClose() const {
    return glfwWindowShouldClose(m_window) != 0;
}

void Window::SetShouldClose(bool value) {
    glfwSetWindowShouldClose(m_window, value ? GLFW_TRUE : GLFW_FALSE);
}

void Window::SetTitle(const std::string &title)
{
    m_data.title = title;
    glfwSetWindowTitle(m_window, title.c_str());
}
bool Window::IsKeyPressed(int key) const
{
    return glfwGetKey(m_window, key) == GLFW_PRESS;
}

bool Window::IsMouseButtonPressed(int button) const {
    return glfwGetMouseButton(m_window, button) == GLFW_PRESS;
}

float Window::MouseX() const {
    double x, y;
    glfwGetCursorPos(m_window, &x, &y);
    return static_cast<float>(x);
}

float Window::MouseY() const {
    double x, y;
    glfwGetCursorPos(m_window, &x, &y);
    return static_cast<float>(y);
}
void Window::SetCursorCaptured(bool captured)
{
    glfwSetInputMode(m_window, GLFW_CURSOR,
                     captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);

    if (captured) {
        // Prefer raw, unaccelerated motion when available: it gives clean
        // relative deltas and avoids the cursor-recentre artefact entirely.
        if (glfwRawMouseMotionSupported())
            glfwSetInputMode(m_window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);

        // Seed the baseline with the cursor's current position so the first
        // delta is measured from the right place...
        glfwGetCursorPos(m_window, &m_data.lastMouseX, &m_data.lastMouseY);
        // ...and ignore the next couple of frames while GLFW settles.
        m_data.settleFrames = 2;
    }
    // Re-arm first-mouse handling so re-capturing does not cause a jump.
    m_data.firstMouse = true;
    m_data.mouseDeltaX = 0.0;
    m_data.mouseDeltaY = 0.0;
}
void Window::SetVSync(bool enabled) {
    glfwSwapInterval(enabled ? 1 : 0);
    m_data.vsync = enabled;
}

void Window::ToggleFullscreen()
{
    if (!m_data.fullscreen) {
        // Remember the windowed placement so we can restore it later.
        glfwGetWindowPos(m_window, &m_data.windowedX, &m_data.windowedY);
        glfwGetWindowSize(m_window, &m_data.windowedW, &m_data.windowedH);

        GLFWmonitor*       monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode    = glfwGetVideoMode(monitor);
        glfwSetWindowMonitor(m_window, monitor, 0, 0,
                             mode->width, mode->height, mode->refreshRate);
        m_data.fullscreen = true;
    } else {
        glfwSetWindowMonitor(m_window, nullptr, m_data.windowedX, m_data.windowedY,
                             m_data.windowedW, m_data.windowedH, 0);
        m_data.fullscreen = false;
    }
    // Switching monitors can reset the swap interval, so re-apply vsync.
    SetVSync(m_data.vsync);
}

bool Window::IsMinimized() const 
{
    return glfwGetWindowAttrib(m_window, GLFW_ICONIFIED) != 0
        || m_data.width == 0 || m_data.height == 0;
}

} // namespace engine
