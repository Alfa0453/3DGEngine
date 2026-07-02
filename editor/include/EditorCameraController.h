#pragma once

#include <functional>

namespace engine {
class Camera;
class Window;
}

class EditorCameraController {
public:
    using ViewportHitTest = std::function<bool(float, float)>;

    bool MouseLookActive() const { return m_mouseLook; }

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
};
