#include "EditorCameraController.h"

#include <engine/core/Window.h>
#include <engine/graphics/Camera.h>

#include <GLFW/glfw3.h>

void EditorCameraController::TogglePinnedMouseLook() {
    m_mouseLookPinned = !m_mouseLookPinned;
}

void EditorCameraController::UpdateMouseCapture(engine::Window& window,
                                                bool editMode,
                                                const ViewportHitTest& isViewportPoint) {
    const bool rightMouseDown = window.Native()
        && glfwGetMouseButton(window.Native(), GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    const bool rightMousePressed = rightMouseDown && !m_rightMouseLookPrev;
    if (!rightMouseDown) {
        m_rightMouseLookActive = false;
    } else if (rightMousePressed && editMode && window.Native()) {
        double cursorX = 0.0;
        double cursorY = 0.0;
        glfwGetCursorPos(window.Native(), &cursorX, &cursorY);
        m_rightMouseLookActive = isViewportPoint(static_cast<float>(cursorX), static_cast<float>(cursorY));
    }
    m_rightMouseLookPrev = rightMouseDown;

    const bool shouldMouseLook = m_mouseLookPinned || m_rightMouseLookActive;
    if (m_mouseLook != shouldMouseLook) {
        m_mouseLook = shouldMouseLook;
        window.SetCursorCaptured(m_mouseLook);
    }

    const bool middleMouseDown = window.Native()
        && glfwGetMouseButton(window.Native(), GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS;
    const bool middleMousePressed = middleMouseDown && !m_middleMousePanPrev;
    if (!middleMouseDown || m_mouseLook) {
        m_middleMousePanActive = false;
    } else if (middleMousePressed && editMode && window.Native()) {
        double cursorX = 0.0;
        double cursorY = 0.0;
        glfwGetCursorPos(window.Native(), &cursorX, &cursorY);
        m_middleMousePanActive = isViewportPoint(static_cast<float>(cursorX), static_cast<float>(cursorY));
    }
    m_middleMousePanPrev = middleMouseDown;
}

void EditorCameraController::UpdateCamera(engine::Window& window,
                                          engine::Camera& camera,
                                          bool editMode,
                                          float dt,
                                          const ViewportHitTest& isViewportPoint) {
    if (editMode && window.Native()) {
        double cursorX = 0.0;
        double cursorY = 0.0;
        glfwGetCursorPos(window.Native(), &cursorX, &cursorY);
        const float scrollY = window.ScrollDeltaY();
        if (scrollY != 0.0f && isViewportPoint(static_cast<float>(cursorX), static_cast<float>(cursorY))) {
            const float zoomSpeed = window.IsKeyPressed(GLFW_KEY_LEFT_SHIFT) ? 2.0f : 1.0f;
            camera.MoveForward(scrollY * zoomSpeed);
        }
    }

    if (m_mouseLook) {
        const float cameraSpeed = (window.IsKeyPressed(GLFW_KEY_LEFT_SHIFT) ? 12.0f : 5.0f) * dt;
        camera.AddYawPitch(window.MouseDeltaX() * 0.1f, -window.MouseDeltaY() * 0.1f);
        if (window.IsKeyPressed(GLFW_KEY_W)) camera.MoveForward(cameraSpeed);
        if (window.IsKeyPressed(GLFW_KEY_S)) camera.MoveForward(-cameraSpeed);
        if (window.IsKeyPressed(GLFW_KEY_D)) camera.MoveRight(cameraSpeed);
        if (window.IsKeyPressed(GLFW_KEY_A)) camera.MoveRight(-cameraSpeed);
        if (window.IsKeyPressed(GLFW_KEY_SPACE)) camera.MoveUp(cameraSpeed);
        if (window.IsKeyPressed(GLFW_KEY_LEFT_CONTROL)) camera.MoveUp(-cameraSpeed);
    } else if (m_middleMousePanActive) {
        const float panSpeed = window.IsKeyPressed(GLFW_KEY_LEFT_SHIFT) ? 0.04f : 0.02f;
        camera.MoveRight(-window.MouseDeltaX() * panSpeed);
        camera.MoveUp(window.MouseDeltaY() * panSpeed);
    }
}
