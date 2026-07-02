#pragma once

class EditorMouseController {
public:
    struct ButtonState {
        bool down = false;
        bool pressed = false;
        bool released = false;
    };

    ButtonState UpdateAssetLeft(bool down);
    ButtonState UpdateViewportLeft(bool down);
    ButtonState UpdateRight(bool down);

    void ResetAssetLeft();
    void ResetViewportLeft();
    void ResetRight();

    void BeginGizmoDrag(int button, float x, float y);
    void EndGizmoDrag();
    bool GizmoActive() const { return m_gizmoActive; }
    bool GizmoActiveFor(int button) const { return m_gizmoActive && m_gizmoButton == button; }
    int GizmoButton() const { return m_gizmoButton; }
    float GizmoLastX() const { return m_gizmoLastX; }
    float GizmoLastY() const { return m_gizmoLastY; }
    void UpdateGizmoLast(float x, float y);

private:
    ButtonState UpdateButton(bool down, bool& previous);

    bool m_assetLeftPrev = false;
    bool m_viewportLeftPrev = false;
    bool m_rightPrev = false;
    bool m_gizmoActive = false;
    int m_gizmoButton = -1;
    float m_gizmoLastX = 0.0f;
    float m_gizmoLastY = 0.0f;
};
