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

void EditorPanels::ShowAll()
{
    m_open.fill(true);
}

void EditorPanels::HideAll()
{
    m_open.fill(false);
}

void EditorPanels::ResetDefaults()
{
    m_open = kDefaultOpen;
}

const char *EditorPanels::Name(Panel panel)
{
    switch (panel) {
    case Panel::Hierarchy: return "Hierarchy";
    case Panel::Inspector: return "Inspector";
    case Panel::WorldSettings: return "World Settings";
    case Panel::Assets:     return "Assets";
    case Panel::Console:   return "Console";
    case Panel::MaterialMaker: return "Material Maker";
    case Panel::PhysicsStatus: return "PhysicsStatus";
    case Panel::Count:     break;
    }
    return "Panel";
}
