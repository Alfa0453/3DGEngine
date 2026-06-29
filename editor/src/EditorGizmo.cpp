#include "EditorGizmo.h"

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
}

glm::vec3 EditorGizmo::AxisVector() const
{
    switch (m_axis) {
    case Axis::X: return glm::vec3(1.0f, 0.0f, 0.0f);
    case Axis::Y: return glm::vec3(0.0f, 1.0f, 0.0f);
    case Axis::Z: return glm::vec3(0.0f, 0.0f, 1.0f);
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
    }
    return "X";
}
