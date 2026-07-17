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
        Z,
        All
    };

    enum class Space {
        World,
        Local
    };

    void CycleMode();
    void SetMode(Mode mode);
    void SetAxis(Axis axis) { m_axis = axis; }
    void SetSpace(Space space) { m_space = space; }
    void ToggleSpace() { m_space = m_space == Space::World ? Space::Local : Space::World; }

    Mode CurrentMode() const { return m_mode; }
    Axis CurrentAxis() const { return m_axis; }
    Space CurrentSpace() const { return m_space; }

    bool SnappingEnabled() const { return m_snapping; }
    void SetSnappingEnabled(bool enabled) { m_snapping = enabled; }
    float TranslationSnap() const { return m_translationSnap; }
    float RotationSnap() const { return m_rotationSnap; }
    float ScaleSnap() const { return m_scaleSnap; }
    void SetTranslationSnap(float value);
    void SetRotationSnap(float value);
    void SetScaleSnap(float value);
    float VisualScale() const { return m_visualScale; }
    void SetVisualScale(float value);

    glm::vec3 AxisVector() const;
    const char* ModeName() const;
    const char* AxisName() const;
    const char* SpaceName() const;

private:
    Mode m_mode = Mode::Translate;
    Axis m_axis = Axis::X;
    Space m_space = Space::Local;
    bool m_snapping = false;
    float m_translationSnap = 0.5f;
    float m_rotationSnap = 15.0f;
    float m_scaleSnap = 0.1f;
    float m_visualScale = 1.0f;
};
