#include "EditorTransformController.h"

#include "EditorGizmo.h"
#include "EditorScene.h"

#include <engine/core/Window.h>

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>

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
        if (const engine::ecs::Transform* transform = scene.SelectedTransform()) {
            const glm::vec3 directionAxis = gizmo.CurrentSpace() == EditorGizmo::Space::Local
                ? glm::mat3_cast(transform->rotation) * axis : axis;
            scene.MoveSelected(directionAxis * (amount * 2.0f));
        }
        break;
    case EditorGizmo::Mode::Rotate:
        if (const engine::ecs::Transform* transform = scene.SelectedTransform()) {
            engine::ecs::Transform edited = *transform;
            const glm::quat delta = glm::angleAxis(glm::radians(amount * 90.0f), glm::normalize(axis));
            edited.rotation = glm::normalize(gizmo.CurrentSpace() == EditorGizmo::Space::Local
                ? edited.rotation * delta : delta * edited.rotation);
            scene.SetSelectedTransform(edited);
        }
        break;
    case EditorGizmo::Mode::Scale:
        if (gizmo.CurrentAxis() == EditorGizmo::Axis::All) scene.ScaleSelected(1.0f + amount);
        else scene.ScaleSelectedAxis(axis, 1.0f + amount);
        break;
    }
}

void EditorTransformController::ApplyGizmoDrag(EditorScene& scene, const EditorGizmo& gizmo, float pixels) {
    const glm::vec3 axis = gizmo.AxisVector();

    auto snapped = [&](float delta, float increment) {
        if (!gizmo.SnappingEnabled()) return delta;
        m_dragRemainder += delta;
        const float steps = std::trunc(m_dragRemainder / increment);
        const float applied = steps * increment;
        m_dragRemainder -= applied;
        return applied;
    };

    switch (gizmo.CurrentMode()) {
    case EditorGizmo::Mode::Translate:
        if (const engine::ecs::Transform* transform = scene.SelectedTransform()) {
            const float amount = snapped(pixels * 0.01f, gizmo.TranslationSnap());
            if (amount == 0.0f) break;
            const glm::vec3 directionAxis = gizmo.CurrentSpace() == EditorGizmo::Space::Local
                ? glm::mat3_cast(transform->rotation) * axis : axis;
            scene.MoveSelected(directionAxis * amount);
        }
        break;
    case EditorGizmo::Mode::Rotate:
        if (const engine::ecs::Transform* transform = scene.SelectedTransform()) {
            const float degrees = snapped(pixels * 0.35f, gizmo.RotationSnap());
            if (degrees == 0.0f) break;
            engine::ecs::Transform edited = *transform;
            const glm::quat delta = glm::angleAxis(glm::radians(degrees), glm::normalize(axis));
            edited.rotation = glm::normalize(gizmo.CurrentSpace() == EditorGizmo::Space::Local
                ? edited.rotation * delta : delta * edited.rotation);
            scene.SetSelectedTransform(edited);
        }
        break;
    case EditorGizmo::Mode::Scale:
        {
            const float scaleDelta = snapped(pixels * 0.0025f, gizmo.ScaleSnap());
            if (scaleDelta == 0.0f) break;
            float factor = 1.0f + scaleDelta;
            if (factor < 0.05f) {
                factor = 0.05f;
            }
            if (gizmo.CurrentAxis() == EditorGizmo::Axis::All) scene.ScaleSelected(factor);
            else scene.ScaleSelectedAxis(axis, factor);
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
