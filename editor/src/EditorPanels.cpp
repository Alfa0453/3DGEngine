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
    case Panel::PhysicsStatus: return "Physics Status";
    case Panel::GameplayDebug: return "Gameplay Debug";
    case Panel::AnimationPreview: return "Animation Preview";
    case Panel::Gizmo: return "Gizmo";
    case Panel::CameraManager: return "Camera Manager";
    case Panel::BehaviorGraph: return "Behavior Graph";
    case Panel::AudioEditor: return "Audio Editor";
    case Panel::AudioMixer: return "Audio Mixer";
    case Panel::ParticleEditor: return "Particle Editor";
    case Panel::ShaderEditor: return "Shader Editor";
    case Panel::Hud: return "HUD Editor";
    case Panel::Count:     break;
    }
    return "Panel";
}
