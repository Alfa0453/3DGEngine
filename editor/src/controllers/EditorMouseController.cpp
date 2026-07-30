#include "EditorMouseController.h"

EditorMouseController::ButtonState EditorMouseController::UpdateAssetLeft(bool down) {
    return UpdateButton(down, m_assetLeftPrev);
}

EditorMouseController::ButtonState EditorMouseController::UpdateViewportLeft(bool down) {
    return UpdateButton(down, m_viewportLeftPrev);
}

EditorMouseController::ButtonState EditorMouseController::UpdateRight(bool down) {
    return UpdateButton(down, m_rightPrev);
}

void EditorMouseController::ResetAssetLeft() {
    m_assetLeftPrev = false;
}

void EditorMouseController::ResetViewportLeft() {
    m_viewportLeftPrev = false;
}

void EditorMouseController::ResetRight() {
    m_rightPrev = false;
}

void EditorMouseController::BeginGizmoDrag(int button, float x, float y) {
    m_gizmoActive = true;
    m_gizmoButton = button;
    m_gizmoLastX = x;
    m_gizmoLastY = y;
}

void EditorMouseController::EndGizmoDrag() {
    m_gizmoActive = false;
    m_gizmoButton = -1;
}

void EditorMouseController::UpdateGizmoLast(float x, float y) {
    m_gizmoLastX = x;
    m_gizmoLastY = y;
}

EditorMouseController::ButtonState EditorMouseController::UpdateButton(bool down, bool& previous) {
    ButtonState state;
    state.down = down;
    state.pressed = down && !previous;
    state.released = !down && previous;
    previous = down;
    return state;
}
