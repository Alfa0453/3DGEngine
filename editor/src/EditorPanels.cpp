#include "EditorPanels.h"

bool EditorPanels::IsOpen(Panel panel) const
{
    return m_open[static_cast<int>(panel)];
}

void EditorPanels::SetOpen(Panel panel, bool open)
{
    m_open[static_cast<int>(panel)] = open;
}

void EditorPanels::Toggle(Panel panel)
{
    SetOpen(panel, !IsOpen(panel));
}

const char *EditorPanels::Name(Panel panel)
{
    switch (panel) {
    case Panel::Hierarchy: return "Hierarchy";
    case Panel::Inspector: return "Inspector";
    case Panel::Assets:     return "Assets";
    case Panel::Console:   return "Console";
    case Panel::Count:     break;
    }
    return "Panel";
}
