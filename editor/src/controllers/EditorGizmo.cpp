#include "EditorGizmo.h"

#include <algorithm>

void EditorGizmo::CycleMode()
{
    switch (m_mode) {
    case Mode::Translate:
        m_mode = Mode::Rotate;
        break;
    case Mode::Rotate:
        m_mode = Mode::Scale;
        break;
    case Mode::Scale:
        m_mode = Mode::Translate;
        break;
    }
    if (m_mode == Mode::Scale) m_space = Space::Local;
    if (m_mode != Mode::Scale && m_axis == Axis::All) m_axis = Axis::X;
}

glm::vec3 EditorGizmo::AxisVector() const
{
    switch (m_axis) {
    case Axis::X: return glm::vec3(1.0f, 0.0f, 0.0f);
    case Axis::Y: return glm::vec3(0.0f, 1.0f, 0.0f);
    case Axis::Z: return glm::vec3(0.0f, 0.0f, 1.0f);
    case Axis::All: return glm::vec3(1.0f);
    }
    return glm::vec3(1.0f, 0.0f, 0.0f);
}

const char *EditorGizmo::ModeName() const
{
    switch (m_mode) {
    case Mode::Translate: return "Translate";
    case Mode::Rotate: return "Rotate";
    case Mode::Scale: return "Scale";
    }
    return "Translate";
}

const char *EditorGizmo::AxisName() const
{
    switch (m_axis) {
    case Axis::X: return "X";
    case Axis::Y: return "Y";
    case Axis::Z: return "Z";
    case Axis::All: return "All";
    }
    return "X";
}

void EditorGizmo::SetMode(Mode mode)
{
    m_mode = mode;
    if (m_mode == Mode::Scale) m_space = Space::Local;
    if (m_mode != Mode::Scale && m_axis == Axis::All) m_axis = Axis::X;
}

const char* EditorGizmo::SpaceName() const
{
    return m_space == Space::World ? "World" : "Local";
}

void EditorGizmo::SetTranslationSnap(float value) { m_translationSnap = std::max(value, 0.001f); }
void EditorGizmo::SetRotationSnap(float value) { m_rotationSnap = std::max(value, 0.1f); }
void EditorGizmo::SetScaleSnap(float value) { m_scaleSnap = std::max(value, 0.001f); }
void EditorGizmo::SetVisualScale(float value) { m_visualScale = std::clamp(value, 0.5f, 2.5f); }
