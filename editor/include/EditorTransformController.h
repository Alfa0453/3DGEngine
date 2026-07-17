#pragma once

namespace engine {
class Window;
}

class EditorGizmo;
class EditorScene;

class EditorTransformController {
public:
    void UpdateKeyboardShortcuts(engine::Window& window, EditorScene& scene, const EditorGizmo& gizmo, bool editMode, float dt);
    void ApplyGizmoNudge(EditorScene& scene, const EditorGizmo& gizmo, float direction, float dt) const;
    void BeginGizmoDrag() { m_dragRemainder = 0.0f; }
    void EndGizmoDrag() { m_dragRemainder = 0.0f; }
    void ApplyGizmoDrag(EditorScene& scene, const EditorGizmo& gizmo, float pixels);

private:
    bool IsTransformEditActive(const engine::Window& window) const;

    bool m_wasTransformEditing = false;
    float m_dragRemainder = 0.0f;
};
