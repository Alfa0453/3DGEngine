#include "EditorPanels.h"
#include "EditorPanelIcons.h"

#include <array>
#include <string>

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

namespace {
const char* PlainPanelName(EditorPanels::Panel panel)
{
    using Panel = EditorPanels::Panel;
    switch (panel) {
    case Panel::Hierarchy: return "Hierarchy";
    case Panel::Inspector: return "Inspector";
    case Panel::WorldSettings: return "World Settings";
    case Panel::GameModeSettings: return "Game Mode Settings";
    case Panel::Assets:     return "Assets";
    case Panel::Console:   return "Console";
    case Panel::MaterialMaker: return "Material Maker";
    case Panel::PhysicsStatus: return "Physics Status";
    case Panel::GameplayDebug: return "Gameplay Debug";
    case Panel::OptimizationAuditor: return "Optimization Auditor";
    case Panel::LightingAnalysis: return "Lighting Analysis";
    case Panel::AnimationPreview: return "Animation Preview";
    case Panel::Gizmo: return "Gizmo";
    case Panel::CameraManager: return "Camera Manager";
    case Panel::BehaviorGraph: return "Behavior Graph";
    case Panel::AudioEditor: return "Audio Editor";
    case Panel::AudioMixer: return "Audio Mixer";
    case Panel::ParticleEditor: return "Particle Editor";
    case Panel::ShaderEditor: return "Shader Editor";
    case Panel::Hud: return "HUD Editor";
    case Panel::CharacterEditor: return "Character Editor";
    case Panel::ClipEditor: return "Clip Editor";
    case Panel::GraphEditor: return "Animation Graph Editor";
    case Panel::MeshEditor: return "Mesh Editor";
    case Panel::DecalPlacement: return "Decal Placement";
    case Panel::ModularPlacement: return "Modular Placement";
    case Panel::PrefabPalette: return "Prefab Palette";
    case Panel::RoomBuilder: return "Room Builder";
    case Panel::ScatterPaint: return "Scatter & Paint";
    case Panel::ArrayTool: return "Smart Duplicate & Array";
    case Panel::Measurement: return "Measurement & Ruler";
    case Panel::LevelValidation: return "Level Validation & Cleanup";
    case Panel::LevelVariants: return "Level Snapshots & Variants";
    case Panel::LevelLayers: return "Level Layers & Visibility";
    case Panel::ViewportBookmarks: return "Viewport Bookmarks & Navigation";
    case Panel::Blockout: return "Blockout & Shape Editing";
    case Panel::Alignment: return "Object Alignment & Distribution";
    case Panel::SplineBuilder: return "Spline Road & Fence Builder";
    case Panel::Viewport: return "Viewport";
    case Panel::Prefab: return "Prefab Editor";
    case Panel::ScriptApi: return "Script API";
    case Panel::ScriptDebug: return "Script Debug";
    case Panel::WorldEditor: return "World Editor";
    case Panel::TerrainCreator: return "Terrain Creator";
    case Panel::RagdollPhysics: return "Ragdoll Physics Editor";
    case Panel::AnimationRetargeting: return "Animation Retargeting";
    case Panel::AbilityEditor: return "Ability Editor";
    case Panel::RuntimePropertyInspector: return "Runtime Property Inspector";
    case Panel::AssetDependencyViewer: return "Asset Dependency Viewer";
    case Panel::WeatherEditor: return "Weather Editor";
    case Panel::ProceduralBuilding: return "Procedural Building Tool";
    case Panel::RoadGenerator: return "Road Generator";
    case Panel::LevelInstances: return "Level Instance Tool";
    case Panel::WorldPartition: return "World Partition Tool";
    case Panel::ProceduralScatterGraph: return "Procedural Scatter Graph";
    case Panel::BiomeEditor: return "Biome Editor";
    case Panel::DayNightTimeline: return "Day/Night Timeline";
    case Panel::CaveTunnel: return "Cave and Tunnel Tool";
    case Panel::FenceWallPainter: return "Fence and Wall Painter";
    case Panel::DestructionAuthoring: return "Destruction Authoring Tool";
    case Panel::InteractionAuthoring: return "Interaction Editor";
    case Panel::PortalAuthoring: return "Portal and Teleport Tool";
    case Panel::QuestEditor: return "Quest Editor";
    case Panel::DialogueEditor: return "Dialogue Editor";
    case Panel::InventoryItemEditor: return "Inventory and Item Editor";
    case Panel::CombatEditor: return "Combat Editor";
    case Panel::SpawnManager: return "Spawn Manager";
    case Panel::CheckpointSave: return "Checkpoint and Save Editor";
    case Panel::IKRigEditor: return "IK Rig Editor";
    case Panel::Count:     break;
    }
    return "Panel";
}

const char* PlainGroupName(EditorPanels::Group group)
{
    using Group = EditorPanels::Group;
    switch (group) {
    case Group::Core: return "Core";
    case Group::WorldGameplay: return "World & Gameplay";
    case Group::Content: return "Content Editors";
    case Group::Animation: return "Animation & Characters";
    case Group::EffectsAudio: return "Effects & Audio";
    case Group::AiScripting: return "AI & Scripting";
    case Group::LevelDesign: return "Level Design";
    case Group::Debug: return "Debug & Diagnostics";
    case Group::Count: break;
    }
    return "Panels";
}
} // namespace

const char* EditorPanels::Name(Panel panel)
{
    const char* plain = PlainPanelName(panel);
    if (panel == Panel::Count || !editor::icons::Available()) return plain;
    static std::array<std::string, static_cast<int>(Panel::Count)> labels;
    std::string& label = labels[static_cast<int>(panel)];
    if (label.empty())
        label = editor::icons::WindowLabel(editor::icons::ForPanel(panel), plain);
    return label.c_str();
}

const char* EditorPanels::GroupName(Group group)
{
    const char* plain = PlainGroupName(group);
    if (group == Group::Count || !editor::icons::Available()) return plain;
    static std::array<std::string, static_cast<int>(Group::Count)> labels;
    std::string& label = labels[static_cast<int>(group)];
    if (label.empty())
        label = editor::icons::Label(editor::icons::ForGroup(group), plain);
    return label.c_str();
}

EditorPanels::Group EditorPanels::GroupOf(Panel panel)
{
    switch (panel) {
    case Panel::Hierarchy:
    case Panel::Inspector:
    case Panel::Assets:
    case Panel::Console:
    case Panel::Gizmo:
    case Panel::Viewport:
        return Group::Core;

    case Panel::WorldSettings:
    case Panel::GameModeSettings:
    case Panel::CameraManager:
    case Panel::Hud:
    case Panel::WorldEditor:
    case Panel::WeatherEditor:
    case Panel::DayNightTimeline:
    case Panel::InventoryItemEditor:
    case Panel::CombatEditor:
    case Panel::SpawnManager:
    case Panel::CheckpointSave:
        return Group::WorldGameplay;

    case Panel::MaterialMaker:
    case Panel::ShaderEditor:
    case Panel::MeshEditor:
    case Panel::Prefab:
    case Panel::TerrainCreator:
        return Group::Content;

    case Panel::AnimationPreview:
    case Panel::CharacterEditor:
    case Panel::ClipEditor:
    case Panel::GraphEditor:
    case Panel::RagdollPhysics:
    case Panel::AnimationRetargeting:
    case Panel::AbilityEditor:
    case Panel::IKRigEditor:
        return Group::Animation;

    case Panel::AudioEditor:
    case Panel::AudioMixer:
    case Panel::ParticleEditor:
        return Group::EffectsAudio;

    case Panel::BehaviorGraph:
    case Panel::ScriptApi:
    case Panel::ScriptDebug:
        return Group::AiScripting;

    case Panel::ModularPlacement:
    case Panel::DecalPlacement:
    case Panel::PrefabPalette:
    case Panel::RoomBuilder:
    case Panel::ScatterPaint:
    case Panel::ArrayTool:
    case Panel::Measurement:
    case Panel::LevelValidation:
    case Panel::LevelVariants:
    case Panel::LevelLayers:
    case Panel::ViewportBookmarks:
    case Panel::Blockout:
    case Panel::Alignment:
    case Panel::SplineBuilder:
    case Panel::ProceduralBuilding:
    case Panel::RoadGenerator:
    case Panel::LevelInstances:
    case Panel::WorldPartition:
    case Panel::ProceduralScatterGraph:
    case Panel::BiomeEditor:
    case Panel::CaveTunnel:
    case Panel::FenceWallPainter:
    case Panel::DestructionAuthoring:
    case Panel::InteractionAuthoring:
    case Panel::PortalAuthoring:
    case Panel::QuestEditor:
    case Panel::DialogueEditor:
        return Group::LevelDesign;

    case Panel::PhysicsStatus:
    case Panel::GameplayDebug:
    case Panel::OptimizationAuditor:
    case Panel::LightingAnalysis:
    case Panel::RuntimePropertyInspector:
    case Panel::AssetDependencyViewer:
        return Group::Debug;

    case Panel::Count:
        break;
    }
    return Group::Core;
}
