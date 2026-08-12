#pragma once

#include "EditorIcons.h"
#include "EditorPanels.h"

namespace editor::icons {

inline const char* ForPanel(EditorPanels::Panel panel) {
    using P = EditorPanels::Panel;
    switch (panel) {
    case P::Hierarchy: return Layers;
    case P::Inspector: return Settings;
    case P::WorldSettings: return World;
    case P::GameModeSettings: return Settings;
    case P::Assets: return Folder;
    case P::Console: return Code;
    case P::MaterialMaker: return Palette;
    case P::PhysicsStatus: return Settings;
    case P::GameplayDebug: return Settings;
    case P::AnimationPreview: return Play;
    case P::Gizmo: return Edit;
    case P::CameraManager: return Screen;
    case P::BehaviorGraph: return Link;
    case P::AudioEditor:
    case P::AudioMixer: return Music;
    case P::ParticleEditor: return Star;
    case P::ShaderEditor: return Code;
    case P::Hud: return Screen;
    case P::CharacterEditor: return Contact;
    case P::ClipEditor: return Video;
    case P::GraphEditor: return Layers;
    case P::MeshEditor: return Layers;
    case P::ModularPlacement: return Layers;
    case P::PrefabPalette: return Archive;
    case P::RoomBuilder: return World;
    case P::ScatterPaint: return Leaf;
    case P::ArrayTool: return Copy;
    case P::Measurement: return Edit;
    case P::LevelValidation: return Settings;
    case P::LevelVariants: return Copy;
    case P::LevelLayers: return Layers;
    case P::ViewportBookmarks: return Star;
    case P::Blockout: return Layers;
    case P::Alignment: return Edit;
    case P::SplineBuilder: return Link;
    case P::Viewport: return Screen;
    case P::Prefab: return Archive;
    case P::ScriptApi:
    case P::ScriptDebug: return Code;
    case P::WorldEditor: return World;
    case P::TerrainCreator: return World;
    case P::Count: break;
    }
    return Document;
}

inline const char* ForGroup(EditorPanels::Group group) {
    using G = EditorPanels::Group;
    switch (group) {
    case G::Core: return Screen;
    case G::WorldGameplay: return World;
    case G::Content: return Archive;
    case G::Animation: return Play;
    case G::EffectsAudio: return Star;
    case G::AiScripting: return Code;
    case G::LevelDesign: return Layers;
    case G::Debug: return Settings;
    case G::Count: break;
    }
    return Document;
}

} // namespace editor::icons
