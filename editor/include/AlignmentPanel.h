#pragma once

class EditorScene;

class AlignmentPanel {
public:
    void Draw(EditorScene& scene, bool* open);

private:
    int m_axis = 0;
    float m_lineSpacing = 2.0f;
    int m_gridColumns = 4;
    float m_gridSpacingX = 2.0f;
    float m_gridSpacingZ = 2.0f;
    bool m_preservePrimary = true;
};
