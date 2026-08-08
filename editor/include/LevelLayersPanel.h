#pragma once

#include <string>

class EditorScene;

class LevelLayersPanel {
public:
    void Draw(EditorScene& scene, bool* open);

private:
    std::string m_activeLayer = "Default";
    char m_newLayer[64]{};
    char m_renameLayer[64]{};
    char m_filter[96]{};
    bool m_showObjects = true;
    bool m_renameInitialized = false;
};
