#pragma once

#include <glm/glm.hpp>

class EditorGizmo {
public:
    enum class Mode {
        Translate,
        Rotate,
        Scale
    };

    enum class Axis {
        X,
        Y,
        Z
    };

    void CycleMode();
    void SetMode(Mode mode) { m_mode = mode; }
    void SetAxis(Axis axis) { m_axis = axis; }

    Mode CurrentMode() const { return m_mode; }
    Axis CurrentAxis() const { return m_axis; }

    glm::vec3 AxisVector() const;
    const char* ModeName() const;
    const char* AxisName() const;

private:
    Mode m_mode = Mode::Translate;
    Axis m_axis = Axis::X;
};