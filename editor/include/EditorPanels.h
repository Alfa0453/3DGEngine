#pragma once

#include <array>

class EditorPanels {
public:
    enum class Panel {
        Hierarchy,
        Inspector,
        WorldSettings,
        Assets,
        Console,
        MaterialMaker,
        Count
    };

    bool IsOpen(Panel panel) const;
    void SetOpen(Panel panel, bool open);
    void Toggle(Panel panel);

    static const char* Name(Panel panel);

private:
    static constexpr int kPanelCount = static_cast<int>(Panel::Count);
    std::array<bool, kPanelCount> m_open{{true, true, false, true, true, false}};       
};