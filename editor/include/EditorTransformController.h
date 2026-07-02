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
    void ApplyGizmoDrag(EditorScene& scene, const EditorGizmo& gizmo, float pixels) const;

private:
    bool IsTransformEditActive(const engine::Window& window) const;

    bool m_wasTransformEditing = false;
};
