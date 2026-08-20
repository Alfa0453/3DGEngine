#pragma once

#include <array>

class EditorPanels {
public:
    enum class Group {
        Core,
        WorldGameplay,
        Content,
        Animation,
        EffectsAudio,
        AiScripting,
        LevelDesign,
        Debug,
        Count
    };

    enum class Panel {
        Hierarchy,
        Inspector,
        WorldSettings,
        GameModeSettings,
        Assets,
        Console,
        MaterialMaker,
        PhysicsStatus,
        GameplayDebug,
        OptimizationAuditor,
        LightingAnalysis,
        AnimationPreview,
        Gizmo,
        CameraManager,
        BehaviorGraph,
        AudioEditor,
        AudioMixer,
        ParticleEditor,
        ShaderEditor,
        Hud,
        CharacterEditor,
        ClipEditor,
        GraphEditor,
        MeshEditor,
        DecalPlacement,
        ModularPlacement,
        PrefabPalette,
        RoomBuilder,
        ScatterPaint,
        ArrayTool,
        Measurement,
        LevelValidation,
        LevelVariants,
        LevelLayers,
        ViewportBookmarks,
        Blockout,
        Alignment,
        SplineBuilder,
        Viewport,
        Prefab,
        ScriptApi,
        ScriptDebug,
        WorldEditor,
        TerrainCreator,
        RagdollPhysics,
        AnimationRetargeting,
        AbilityEditor,
        RuntimePropertyInspector,
        AssetDependencyViewer,
        WeatherEditor,
        ProceduralBuilding,
        RoadGenerator,
        LevelInstances,
        WorldPartition,
        ProceduralScatterGraph,
        BiomeEditor,
        DayNightTimeline,
        CaveTunnel,
        FenceWallPainter,
        Count
    };

    bool IsOpen(Panel panel) const;
    void SetOpen(Panel panel, bool open);
    void Toggle(Panel panel);
    void ShowAll();
    void HideAll();
    void ResetDefaults();

    static const char* Name(Panel panel);
    static const char* GroupName(Group group);
    static Group GroupOf(Panel panel);

private:
    static constexpr int kPanelCount = static_cast<int>(Panel::Count);
    static constexpr std::array<bool, kPanelCount> kDefaultOpen{{
        true, true, false, false, true, true, false, false, false, false, false, false, true, true, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, true, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false
    }};
    std::array<bool, kPanelCount> m_open{kDefaultOpen};      
};
