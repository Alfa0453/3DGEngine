#pragma once

#include <functional>

namespace engine {
class Camera;
class Window;
}

class EditorCameraController {
public:
    using ViewportHitTest = std::function<bool(float, float)>;

    struct Settings {
        float moveSpeed = 5.0f;
        float boostMultiplier = 2.4f;
        float lookSensitivity = 0.1f;
        float scrollSpeed = 1.0f;
        float panSpeed = 0.02f;
        bool invertLookY = false;
        bool invertScroll = false;
    };

    bool MouseLookActive() const { return m_mouseLook; }
    Settings& NavigationSettings() { return m_settings; }
    const Settings& NavigationSettings() const { return m_settings; }
    void NormalizeSettings();
    void ResetSettings();

    void TogglePinnedMouseLook();
    void UpdateMouseCapture(engine::Window& window, bool editMode, const ViewportHitTest& isViewportPoint);
    void UpdateCamera(engine::Window& window, engine::Camera& camera, bool editMode, float dt, const ViewportHitTest& isViewportPoint);

private:
    bool m_mouseLook = false;
    bool m_mouseLookPinned = false;
    bool m_rightMouseLookActive = false;
    bool m_rightMouseLookPrev = false;
    bool m_middleMousePanActive = false;
    bool m_middleMousePanPrev = false;
    Settings m_settings;
};
