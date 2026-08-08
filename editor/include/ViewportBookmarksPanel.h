#pragma once

#include <cstddef>
#include <string>

class EditorScene;

class ViewportBookmarksPanel {
public:
    enum class Action { None, Capture, Visit, Overwrite, Rename, Delete, FrameSelected };
    struct Result {
        Action action = Action::None;
        std::size_t index = static_cast<std::size_t>(-1);
        std::string name;
        float blendDuration = 0.3f;
    };

    Result Draw(const EditorScene& scene, bool* open);

private:
    std::size_t m_selected = static_cast<std::size_t>(-1);
    char m_name[96] = "View";
    char m_filter[96]{};
    float m_blendDuration = 0.3f;
};
