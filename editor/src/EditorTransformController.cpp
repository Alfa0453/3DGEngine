#include "EditorTransformController.h"

#include "EditorGizmo.h"
#include "EditorScene.h"

#include <engine/core/Window.h>

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

void EditorTransformController::UpdateKeyboardShortcuts(engine::Window& window,
                                                        EditorScene& scene,
                                                        const EditorGizmo& gizmo,
                                                        bool editMode,
                                                        float dt) {
    const float objectSpeed = 2.0f * dt;
    if (editMode) {
        const bool transformEditing = IsTransformEditActive(window);
        if (transformEditing && !m_wasTransformEditing) {
            scene.BeginTransformEdit();
        }

        if (window.IsKeyPressed(GLFW_KEY_LEFT)) scene.MoveSelected(glm::vec3(-objectSpeed, 0.0f, 0.0f));
        if (window.IsKeyPressed(GLFW_KEY_RIGHT)) scene.MoveSelected(glm::vec3(objectSpeed, 0.0f, 0.0f));
        if (window.IsKeyPressed(GLFW_KEY_UP)) scene.MoveSelected(glm::vec3(0.0f, 0.0f, -objectSpeed));
        if (window.IsKeyPressed(GLFW_KEY_DOWN)) scene.MoveSelected(glm::vec3(0.0f, 0.0f, objectSpeed));
        if (window.IsKeyPressed(GLFW_KEY_A)) scene.MoveSelected(glm::vec3(-objectSpeed, 0.0f, 0.0f));
        if (window.IsKeyPressed(GLFW_KEY_D)) scene.MoveSelected(glm::vec3(objectSpeed, 0.0f, 0.0f));
        if (window.IsKeyPressed(GLFW_KEY_W)) scene.MoveSelected(glm::vec3(0.0f, 0.0f, -objectSpeed));
        if (window.IsKeyPressed(GLFW_KEY_S)) scene.MoveSelected(glm::vec3(0.0f, 0.0f, objectSpeed));
        if (window.IsKeyPressed(GLFW_KEY_Q)) scene.MoveSelected(glm::vec3(0.0f, objectSpeed, 0.0f));
        if (window.IsKeyPressed(GLFW_KEY_E)) scene.MoveSelected(glm::vec3(0.0f, -objectSpeed, 0.0f));
        if (window.IsKeyPressed(GLFW_KEY_J)) scene.RotateSelectedYaw(-90.0f * dt);
        if (window.IsKeyPressed(GLFW_KEY_L)) scene.RotateSelectedYaw(90.0f * dt);
        if (window.IsKeyPressed(GLFW_KEY_EQUAL) || window.IsKeyPressed(GLFW_KEY_KP_ADD)) {
            scene.ScaleSelectedAxis(gizmo.AxisVector(), 1.0f + dt);
        }
        if (window.IsKeyPressed(GLFW_KEY_MINUS) || window.IsKeyPressed(GLFW_KEY_KP_SUBTRACT)) {
            scene.ScaleSelectedAxis(gizmo.AxisVector(), 1.0f - dt);
        }
        if (window.IsKeyPressed(GLFW_KEY_COMMA)) {
            ApplyGizmoNudge(scene, gizmo, -1.0f, dt);
        }
        if (window.IsKeyPressed(GLFW_KEY_PERIOD)) {
            ApplyGizmoNudge(scene, gizmo, 1.0f, dt);
        }

        if (!transformEditing && m_wasTransformEditing) {
            scene.EndTransformEdit();
        }
        m_wasTransformEditing = transformEditing;
    } else if (m_wasTransformEditing) {
        scene.EndTransformEdit();
        m_wasTransformEditing = false;
    }
}

void EditorTransformController::ApplyGizmoNudge(EditorScene& scene,
                                                const EditorGizmo& gizmo,
                                                float direction,
                                                float dt) const {
    const float amount = direction * dt;
    const glm::vec3 axis = gizmo.AxisVector();

    switch (gizmo.CurrentMode()) {
    case EditorGizmo::Mode::Translate:
        scene.MoveSelected(axis * (amount * 2.0f));
        break;
    case EditorGizmo::Mode::Rotate:
        scene.RotateSelected(axis, amount * 90.0f);
        break;
    case EditorGizmo::Mode::Scale:
        scene.ScaleSelectedAxis(axis, 1.0f + amount);
        break;
    }
}

void EditorTransformController::ApplyGizmoDrag(EditorScene& scene, const EditorGizmo& gizmo, float pixels) const {
    const float amount = pixels * 0.01f;
    const glm::vec3 axis = gizmo.AxisVector();

    switch (gizmo.CurrentMode()) {
    case EditorGizmo::Mode::Translate:
        scene.MoveSelected(axis * amount);
        break;
    case EditorGizmo::Mode::Rotate:
        scene.RotateSelected(axis, pixels * 0.35f);
        break;
    case EditorGizmo::Mode::Scale:
        {
            float factor = 1.0f + amount * 0.25f;
            if (factor < 0.05f) {
                factor = 0.05f;
            }
            scene.ScaleSelectedAxis(axis, factor);
        }
        break;
    }
}

bool EditorTransformController::IsTransformEditActive(const engine::Window& window) const {
    return window.IsKeyPressed(GLFW_KEY_LEFT)
        || window.IsKeyPressed(GLFW_KEY_RIGHT)
        || window.IsKeyPressed(GLFW_KEY_UP)
        || window.IsKeyPressed(GLFW_KEY_DOWN)
        || window.IsKeyPressed(GLFW_KEY_A)
        || window.IsKeyPressed(GLFW_KEY_D)
        || window.IsKeyPressed(GLFW_KEY_W)
        || window.IsKeyPressed(GLFW_KEY_S)
        || window.IsKeyPressed(GLFW_KEY_Q)
        || window.IsKeyPressed(GLFW_KEY_E)
        || window.IsKeyPressed(GLFW_KEY_J)
        || window.IsKeyPressed(GLFW_KEY_L)
        || window.IsKeyPressed(GLFW_KEY_EQUAL)
        || window.IsKeyPressed(GLFW_KEY_KP_ADD)
        || window.IsKeyPressed(GLFW_KEY_MINUS)
        || window.IsKeyPressed(GLFW_KEY_KP_SUBTRACT)
        || window.IsKeyPressed(GLFW_KEY_COMMA)
        || window.IsKeyPressed(GLFW_KEY_PERIOD);
}
