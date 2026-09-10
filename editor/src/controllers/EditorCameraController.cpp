#include "EditorCameraController.h"

#include <engine/core/Window.h>
#include <engine/graphics/Camera.h>

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>

namespace {
float FiniteClamp(float value, float fallback, float minimum, float maximum) {
    return std::clamp(std::isfinite(value) ? value : fallback, minimum, maximum);
}
}

void EditorCameraController::NormalizeSettings() {
    m_settings.moveSpeed = FiniteClamp(m_settings.moveSpeed, 5.0f, 0.05f, 1000.0f);
    m_settings.boostMultiplier = FiniteClamp(m_settings.boostMultiplier, 2.4f, 1.0f, 20.0f);
    m_settings.lookSensitivity = FiniteClamp(m_settings.lookSensitivity, 0.1f, 0.001f, 2.0f);
    m_settings.scrollSpeed = FiniteClamp(m_settings.scrollSpeed, 1.0f, 0.01f, 100.0f);
    m_settings.panSpeed = FiniteClamp(m_settings.panSpeed, 0.02f, 0.001f, 2.0f);
}

void EditorCameraController::ResetSettings() {
    m_settings = Settings{};
}

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
    NormalizeSettings();
    const float boost = window.IsKeyPressed(GLFW_KEY_LEFT_SHIFT)
        ? m_settings.boostMultiplier : 1.0f;
    if (editMode && window.Native()) {
        double cursorX = 0.0;
        double cursorY = 0.0;
        glfwGetCursorPos(window.Native(), &cursorX, &cursorY);
        const float scrollY = window.ScrollDeltaY();
        if (scrollY != 0.0f
            && isViewportPoint(static_cast<float>(cursorX), static_cast<float>(cursorY))) {
            const float scrollDirection = m_settings.invertScroll ? -1.0f : 1.0f;
            camera.MoveForward(scrollY * m_settings.scrollSpeed * boost * scrollDirection);
        }
    }

    if (m_mouseLook) {
        const float cameraSpeed = m_settings.moveSpeed * boost * dt;
        const float lookY = window.MouseDeltaY() * m_settings.lookSensitivity
            * (m_settings.invertLookY ? 1.0f : -1.0f);
        camera.AddYawPitch(window.MouseDeltaX() * m_settings.lookSensitivity, lookY);
        if (window.IsKeyPressed(GLFW_KEY_W)) camera.MoveForward(cameraSpeed);
        if (window.IsKeyPressed(GLFW_KEY_S)) camera.MoveForward(-cameraSpeed);
        if (window.IsKeyPressed(GLFW_KEY_D)) camera.MoveRight(cameraSpeed);
        if (window.IsKeyPressed(GLFW_KEY_A)) camera.MoveRight(-cameraSpeed);
        if (window.IsKeyPressed(GLFW_KEY_SPACE)) camera.MoveUp(cameraSpeed);
        if (window.IsKeyPressed(GLFW_KEY_LEFT_CONTROL)) camera.MoveUp(-cameraSpeed);
    } else if (m_middleMousePanActive) {
        const float panSpeed = m_settings.panSpeed * boost;
        camera.MoveRight(-window.MouseDeltaX() * panSpeed);
        camera.MoveUp(window.MouseDeltaY() * panSpeed);
    }
}
