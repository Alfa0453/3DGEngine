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
        PhysicsStatus,
        Count
    };

    bool IsOpen(Panel panel) const;
    void SetOpen(Panel panel, bool open);
    void Toggle(Panel panel);
    void ShowAll();
    void HideAll();
    void ResetDefaults();

    static const char* Name(Panel panel);

private:
    static constexpr int kPanelCount = static_cast<int>(Panel::Count);
    static constexpr std::array<bool, kPanelCount> kDefaultOpen{{true, true, false, true, true, false}};
    std::array<bool, kPanelCount> m_open{kDefaultOpen};      
};