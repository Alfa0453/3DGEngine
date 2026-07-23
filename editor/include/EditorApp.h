#pragma once

#include <engine/core/Application.h>
#include <engine/core/Config.h>
#include <engine/assets/RuntimeAssetManager.h>
#include <engine/animation/AnimatedModel.h>
#include <engine/ai/AiAgent.h>
#include <engine/ai/NavMesh.h>
#include <engine/audio/AudioEngine.h>
#include <engine/audio/RuntimeAudioSystem.h>
#include <engine/ai/BehaviorGraph.h>
#include <engine/ecs/Components.h>
#include <engine/ecs/Registry.h>
#include <engine/graphics/Camera.h>
#include <engine/graphics/CameraBlend.h>
#include <engine/graphics/CameraShake.h>
#include <engine/graphics/CameraSequence.h>
#include <engine/graphics/DayNightCycle.h>
#include <engine/graphics/IBL.h>
#include <engine/graphics/Mesh.h>
#include <engine/graphics/Model.h>
#include <engine/graphics/PbrRenderer.h>
#include <engine/graphics/ParticleRenderer.h>
#include <engine/graphics/RuntimeParticleSystem.h>
#include <engine/graphics/PostProcess.h>
#include <engine/graphics/ProceduralSky.h>
#include <engine/graphics/Renderer.h>
#include <engine/graphics/Shader.h>
#include <engine/graphics/SkinnedRenderer.h>
#include <engine/graphics/SSAO.h>
#include <engine/graphics/SSR.h>
#include <engine/graphics/Terrain.h>
#include <engine/graphics/Water.h>
#include <engine/graphics/GpuProfiler.h>
#include <engine/graphics/TextRenderer.h>
#include <engine/gameplay/PlayerController.h>
#include <engine/gameplay/CameraDirector.h>
#include <engine/physics/PhysicsComponents.h>
#include <engine/gameplay/Script.h>
#include <engine/physics/PhysicsWorld.h>
#include <engine/ui/ImGuiLayer.h>
#include <MaterialMaker/MaterialMakerPanel.h>
#include "BehaviorGraphPanel.h"
#include "ParticleEditorPanel.h"
#include "ShaderEditorPanel.h"
#include "HudEditorPanel.h"
#include "CharacterEditorPanel.h"

#include "EditorAssets.h"
#include "EditorContentController.h"
#include "EditorCameraController.h"
#include "EditorDockspace.h"
#include "EditorDragDrop.h"
#include "EditorGizmo.h"
#include "EditorLog.h"
#include "EditorMouseController.h"
#include "EditorPanels.h"
#include "EditorProject.h"
#include "EditorRuntimeController.h"
#include "EditorScene.h"
#include "EditorTransformController.h"
#include "EditorViewport.h"


#include <glm/glm.hpp>

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace engine { class GrassField; }   // header pulls in glad, so keep it out of this header

class EditorApp final : public engine::Application {
public:
    explicit EditorApp(engine::Config& config);
    ~EditorApp() override;   // defined in EditorApp.cpp where GrassField is complete

protected:
    void OnInit()           override;
    void OnUpdate(float dt) override;
    void OnRender()         override;
    void OnShutdown()       override;

private:
    enum class EditorMode { 
        Edit,
        Play 
    };

    enum class PendingSceneAction {
        None,
        CloseEditor,
        NewScene,
        LoadScene
    };

    struct PlayTriggerAction {
        engine::ecs::Entity target = engine::ecs::kNull;
        EditorScene::TriggerActionMode enterMoverAction = EditorScene::TriggerActionMode::None;
        EditorScene::TriggerActionMode enterRotatorAction = EditorScene::TriggerActionMode::None;
        EditorScene::TriggerActionMode exitMoverAction = EditorScene::TriggerActionMode::None;
        EditorScene::TriggerActionMode exitRotatorAction = EditorScene::TriggerActionMode::None;
        std::string cameraSequenceName;
        EditorScene::CameraSequenceTriggerAction enterCameraAction =
            EditorScene::CameraSequenceTriggerAction::None;
        EditorScene::CameraSequenceTriggerAction exitCameraAction =
            EditorScene::CameraSequenceTriggerAction::None;
        bool cameraLockInput = true;
        bool cameraSkippable = true;
        engine::ecs::Mover mover;
        engine::ecs::Rotator rotator;
    };

    // A play-mode AI agent: a runtime brain bound to a play-registry entity.
    struct PlayAgent {
        engine::ecs::Entity entity = engine::ecs::kNull;
        engine::ecs::Entity targetEntity = engine::ecs::kNull;   // chase target (M2)
        std::string         name;                                // scene object name (debug label)
        int                 team = 0;                            // faction id (0 = neutral)
        bool                autoTarget = false;                  // acquire nearest hostile each tick
        engine::ai::AiAgent brain;                               // built-in patrol/chase/search
        engine::ai::AiMovementComponent movement;
        // M7: optional data-driven behaviour tree. When useGraph is set, 'tree' + 'ctx'
        // drive the agent instead of 'brain'.
        bool                                              useGraph = false;
        engine::ai::AgentContext                          ctx;
        engine::ai::BehaviorTree<engine::ai::AgentContext> tree;
    };

    struct PlayAudioSource {
        engine::ecs::Entity entity = engine::ecs::kNull;
        engine::AudioEngine::SourceHandle source = engine::AudioEngine::InvalidSource;
        std::string name;
        bool spatial = true;
    };

    struct PlayCameraZone {
        std::string presetName;
        bool restoreOnExit = true;
        int priority = 0;
        float returnBlend = 0.35f;
    };

    struct AnimationPreviewAction {
        engine::ecs::Entity entity = engine::ecs::kNull;
        int clip = 0;
        float time = 0.0f;
        float fadeIn = 0.08f;
        float fadeOut = 0.15f;
        float speed = 1.0f;
        std::vector<float> mask;
        bool active = false;
    };

    void DrawEditModeModels(const glm::mat4& viewProj);
    void DrawSelectionOutline(const glm::mat4& viewProj);
    void DrawEditorOverlay();
    void DrawMaterialMakerPanel();
    void DrawBehaviorGraphPanel();
    void DrawParticleEditorPanel();
    void DrawShaderEditorPanel();
    void DrawHudEditorPanel();
    void DrawCharacterEditorPanel();
    void DrawPlayHud();
    void SyncHudFromScene();   // load the scene's referenced .hud into m_hud
    void ScanHudImages();      // recursively list content-folder images for the picker
    unsigned int HudTextureId(const std::string& relPath);  // resolve HUD image -> GL texture id
    void DrawMaterialMakerTools(bool materialSaved);
    void DrawDirtyScenePrompt();
    EditorDockspace::GameplayDebugState BuildGameplayDebugState();
    EditorDockspace::AnimationPreviewState BuildAnimationPreviewState();
    void DrawAssetOverlay(float x, float y, const glm::vec3& text, const glm::vec3& muted);
    void DrawLogOverlay(float x, float y, const glm::vec3& text, const glm::vec3& muted);
    void HandleGlobalShortcuts(engine::Window& window);
    void HandleAssetShortcuts(engine::Window&, bool controlDown);
    void HandleEditorCommandShortcuts(engine::Window&, bool controlDown);
    void DrawPlayScene(const glm::mat4& viewProj);
    void DrawEditScene(const glm::mat4& viewProj);
    void UpdateEnvironmentIbl(const EditorScene::Environment& environment,
                          const engine::DayNightCycle::Sample& sky);
    void ConfigureEnvironmentPbrOptions(engine::ecs::Registry& registry,
                                        engine::PbrRenderer::Options& options,
                                        const EditorScene::Environment& environment,
                                        const engine::DayNightCycle::Sample& sky);
    void TogglePanel(EditorPanels::Panel panel);
    void HandleMouseAssetDrag();
    void HandleMouseViewportSelection();
    void HandleMouseViewportGizmo();
    void HandleTerrainSculpt();
    void AddTerrainMeshes(engine::ecs::Registry& pbrRegistry);   // shared edit + play terrain draw
    // Representative albedo colour of a painted-layer material (base colour modulated by
    // the average of its albedo map); cached by path. Empty path -> default palette.
    glm::vec3 TerrainLayerMaterialColor(const std::string& materialPath);
    bool AverageImageColor(const std::string& relativePath, glm::vec3& outColor);
    float TerrainSurfaceY(float worldX, float worldZ, bool& over);  // walkable height query
    float WaterSurfaceY(float worldX, float worldZ, bool& over);     // wave height for buoyancy
    void ApplyWaterBuoyancy(float dt);                               // float/sink dynamic bodies in water
    void BeginAssetDrag();
    void DropPayloadOnScene();
    glm::vec3 SceneDropPosition();
    bool IsViewportDropPosition(float x, float y);
    float ProjectGizmoDrag(float dx, float dy, const glm::mat4& viewProj,
                           int viewportWidth, int viewportHeight) const;
    void AddEmpty();
    void AddCube();
    void AddPlane();
    void AddSphere();
    void AddCapsule();
    void AddConfiguredPrimitive(EditorScene::Primitive primitive,
                                const engine::ecs::Transform& transform,
                                const engine::ecs::Collider* collider,
                                const std::string& name = {});
    void AddDynamicCube();
    void AddStaticFloor();
    void AddTerrain();
    void AddWater(int preset = 0);   // 0 generic, 1 lake, 2 ocean, 3 river
    void DrawWaterBodies(const engine::Camera& camera, float aspect);   // transparent water pass
    void DrawGrass(const engine::Camera& camera, float aspect);         // instanced grass on terrain
    void AddSpline();                                     // create an editable Catmull-Rom path
    void DrawSplines(const glm::mat4& viewProj);          // curve + control-point handles
    void AddTriggerVolume();
    void AddNavMeshBoundsVolume();
    void AddPlayerStart();
    void AddCharacterToScene(const CharacterAsset& character, const glm::vec3& position);   // instantiate a .3dgcharacter
    void AddGameplayDoor();
    void AddGameplayPickup();
    void AddGameplayDamageZone();
    void AddGameplayMovingPlatform();
    void AddGameplayTriggerMoverTest();
    void CycleSelectedColor();
    void SetSelectedPrimitive(EditorScene::Primitive primitive);
    void ToggleSelectedVisible();
    void ToggleSelectedLocked();
    void FrameSelected();
    void DuplicateSelected();
    void DeleteSelected();
    void Undo();
    void Redo();
    void SaveScene();
    void SaveSceneAs(const std::string& path);
    void PersistProject();                                       // save project settings to the right config
    void NewProject(const std::string& location, const std::string& name);
    void OpenProjectFromPath(const std::string& projectFile);
    void SetScenePathDraft(const std::string& path);
    void UpdateAutosave(float dt);
    void LoadScene();
    void RequestCloseEditor();
    void RequestNewScene();
    void RequestLoadSceneFromPath(const std::string& path);
    void PerformNewScene();
    void PerformLoadSceneFromPath(const std::string& path);
    void QueueDirtySceneAction(PendingSceneAction action, const std::string& path = std::string());
    void CompletePendingSceneAction();
    void CancelPendingSceneAction();
    void LoadSceneFromPath(const std::string& path);
    void ExportRuntimeScene();
    void ValidateRuntimeScene();
    void TriggerAnimationPreviewAction();
    void EnterPlayMode();
    void ExitPlayMode();
    bool BuildPlayRuntimePreview(std::string* error);
    void ConfigurePlayPlayerController(const std::unordered_map<std::string, engine::ecs::Entity>& playEntitiesByName);
    void BuildPlayTriggerActions(const std::unordered_map<std::string, engine::ecs::Entity>& playEntitiesByName);
    void BuildPlayCameraZones(const std::unordered_map<std::string, engine::ecs::Entity>& playEntitiesByName);
    void ApplyPlayCameraZoneEvent(engine::ecs::Entity trigger, engine::ecs::Entity other,
                                  engine::CollisionEvent::Phase phase);
    void RefreshPlayCameraZone();
    void ApplyPlayTriggerAction(engine::ecs::Entity trigger, engine::ecs::Entity other, engine::CollisionEvent::Phase phase);
    void PushPlayTriggerActionRow(const std::string& triggerName,
                              const std::string& targetName,
                              const std::string& componentName,
                              bool enabled,
                              engine::CollisionEvent::Phase phase);
    void UpdatePlayPlayerController(float dt, bool inputEnabled);
    engine::ecs::Entity FindBestPlayLockTarget();
    void UpdatePlayLockOn(bool inputEnabled);
    void ApplyManagedPlayCamera();
    void BeginCameraBlend(const EditorScene::CameraPreset& preset);
    void UpdateCameraBlend(float dt);
    void RestoreCameraBeforeShake();
    void UpdateCameraShake(float dt);
    void StartCameraSequence(const EditorScene::CameraSequence& sequence,
                             bool lockInput = false, bool skippable = true);
    void UpdateCameraSequence(float dt);
    void ProcessCameraDirectorCommands();
    void SkipActiveCameraSequence();
    void ExecuteCinematicCues(float previousTime, float currentTime, bool wrapped);
    void ExecuteCinematicCue(const EditorScene::CinematicCue& cue);
    void BuildPlayAgents(const std::unordered_map<std::string, engine::ecs::Entity>& playEntitiesByName);
    void BuildPlayAudioSources();
    void UpdatePlayAudioSources();
    void BakePlayNavGrid();
    void BakePlayNavMesh();
    void BakeEditorNavMesh();
    void UpdateAI(float dt);
    engine::ScriptInputState CapturePlayScriptInput(bool inputEnabled, bool includeFrameEdges);
    void StepPlayPhysics(float dt, bool inputEnabled);
    void CapturePlayPhysicsEvents();
    bool Pressed(int key);

    engine::Config&       m_config;          // global editor.cfg (window settings + current project pointer)
    engine::Config        m_projectConfig;   // active project's Project.3dgproject (when m_hasProjectFile)
    bool                  m_hasProjectFile = false;
    engine::Renderer      m_renderer;
    engine::Camera        m_camera{ glm::vec3(0.0f, 3.0f, 8.0f) };
    engine::CameraBlend   m_cameraBlend;
    engine::CameraShake   m_cameraShake;
    engine::CameraSequencePlayer m_cameraSequence;
    engine::CameraDirector m_cameraDirector;
    std::vector<EditorScene::CinematicCue> m_activeCinematicCues;
    bool m_cameraSequencePaused = false;
    std::optional<engine::CameraPose> m_cameraBeforeShake;
    EditorAssets          m_assets;
    EditorDockspace       m_dockspace;
    EditorDragDrop        m_dragDrop;
    EditorGizmo           m_gizmo;
    EditorLog             m_log;
    EditorMouseController m_mouse;
    EditorPanels          m_panels;
    EditorProject         m_project;
    EditorRuntimeController m_runtime;
    EditorScene           m_scene;
    EditorCameraController m_cameraController;
    EditorContentController m_content;
    EditorTransformController m_transformController;
    EditorViewport        m_viewport;
    engine::AudioEngine   m_audio;
    engine::RuntimeAudioSystem m_runtimeAudio;

    std::optional<engine::Mesh>          m_cube;
    std::optional<engine::Mesh>          m_cone;
    std::optional<engine::Mesh>          m_plane;
    std::optional<engine::Mesh>          m_sphere;
    std::optional<engine::Mesh>          m_capsule;
    std::optional<engine::Mesh>          m_cylinder;
    std::optional<engine::Mesh>          m_pyramid;
    std::optional<engine::Mesh>          m_torus;
    std::optional<engine::Mesh>          m_staircase;
    std::optional<engine::Shader>        m_shader;
    std::optional<engine::Shader>        m_modelShader;
    std::optional<engine::SkinnedRenderer> m_skinnedRenderer;
    std::optional<engine::Shader>        m_outlineShader;
    std::optional<engine::Shader>        m_skinnedOutlineShader;
    std::optional<engine::PbrRenderer>   m_pbrRenderer;
    std::optional<engine::ParticleRenderer> m_particleRenderer;
    std::optional<engine::PostProcess>   m_postProcess;
    std::optional<engine::ProceduralSky> m_sky;
    std::optional<engine::IBL>           m_ibl;
    std::optional<engine::SSAO>          m_ssao;
    std::optional<engine::SSR>           m_ssr;
    // Cached generated terrain per object; regenerated when the object's params change.
    struct TerrainCache {
        engine::Terrain terrain;
        int   res = 0;
        float size = 0.0f, maxHeight = 0.0f, frequency = 0.0f;
        int   seed = 0, octaves = 0;
    };
    std::unordered_map<engine::ecs::Entity, TerrainCache> m_terrains;
    std::unordered_map<engine::ecs::Entity, engine::Water> m_waters;   // one Water per water object
    std::unordered_map<engine::ecs::Entity, std::unique_ptr<engine::GrassField>> m_grass;   // one grass field per terrain
    std::unordered_map<std::string, glm::vec3> m_terrainMaterialColors;   // paint-layer material -> colour cache
    bool  m_terrainSculpt = false;        // sculpt mode active (paints the selected terrain)
    int   m_terrainSculptMode = 0;        // 0 raise, 1 lower, 2 smooth, 3 flatten, 4 paint
    int   m_terrainPaintLayer = 1;        // 0 auto/erase, 1 grass, 2 rock, 3 dirt, 4 snow, 5 sand
    float m_terrainBrushRadius = 5.0f;
    float m_terrainBrushStrength = 6.0f;
    std::optional<engine::TextRenderer>  m_text;
    engine::ImGuiLayer                   m_imgui;
    engine::GpuProfiler                  m_gpuProfiler;   // per-pass GPU timings
    bool                                 m_showProfiler = true;
    double                               m_cpuFrameMs = 0.0;   // CPU cost of OnRender
    double                               m_cpuSceneMs = 0.0;   // CPU cost of scene submission
    double                               m_cpuUiMs = 0.0;      // CPU cost of building the UI
    int                                  m_renderW = 0;        // 3D render target width (render scale)
    int                                  m_renderH = 0;        // 3D render target height
    material_maker::MaterialMakerPanel   m_materialMaker;
    BehaviorGraphPanel                   m_behaviorGraph;
    ParticleEditorPanel                  m_particleEditor;
    ShaderEditorPanel                    m_shaderEditor;
    HudEditorPanel                       m_hudPanel;
    CharacterEditorPanel                 m_characterEditor;
    engine::HudDocument                  m_hud;              // active HUD document (in memory)
    std::string                          m_hudPath;          // last saved/loaded .hud path
    std::unordered_map<std::string, float>       m_hudFloats;   // named numeric HUD values
    std::unordered_map<std::string, std::string> m_hudStrings;  // named text HUD values
    bool                                 m_hudMousePrev = false; // left-click edge for HUD buttons
    std::vector<std::string>             m_hudImageChoices;      // content-folder images for the picker

    EditorMode       m_mode = EditorMode::Edit;
    std::optional<EditorScene::Snapshot> m_editSnapshot;
    std::optional<engine::Camera> m_editCameraBeforePlay;
    engine::RuntimeAssetManager m_editAssets;
    engine::PhysicsWorld m_playPhysics;
    std::optional<engine::ecs::Registry> m_playRegistry;
    std::optional<engine::RuntimeAssetManager> m_playAssets;
    std::optional<engine::PlayerController> m_playPlayerController;
    engine::ecs::Entity m_playPlayerEntity = engine::ecs::kNull;
    engine::ecs::Entity m_playLockTarget = engine::ecs::kNull;
    bool m_playLockTogglePrev = false;
    bool m_playMouseCaptured = false;   // Play mode: cursor locked -> mouse look w/o holding RMB
    bool m_playCursorTogglePrev = false; // edge detector for the ESC free/recapture toggle
    bool m_cinematicSkipPrev = false;
    bool m_physicsPaused = false;
    bool m_physicsStepRequested = false;
    float m_physicsFixedTimestep = 1.0f / 60.0f;
    float m_physicsAccumulator = 0.0f;
    int m_physicsStepsLastFrame = 0;
    int m_physicsEventEnterCount = 0;
    int m_physicsEventStayCount = 0;
    int m_physicsEventExitCount = 0;
    int m_physicsActionCount = 0;
    std::vector<EditorDockspace::PhysicsEventRow> m_physicsEventRows;
    std::vector<EditorViewport::PhysicsEventGuide> m_physicsEventGuides;
    std::vector<engine::ScriptAnimationEvent> m_playAnimationEvents;
    bool m_showPhysicsEventGuides = false;
    bool m_showAiDebug = true;
    bool m_showParticleDebug = true;
    bool m_showCameraRails = true;
    bool m_particleDebugSelectedOnly = true;
    bool m_particleDebugShapes = true;
    bool m_particleDebugDirections = true;
    bool m_particleDebugBounds = true;
    bool m_particleDebugCullingState = true;
    bool m_physicsEventGuidesSelectedOnly = false;
    bool m_physicsEventGuidesTriggersOnly = false;
    bool m_physicsEventGuidesEnterExitOnly = false;
    std::unordered_map<engine::ecs::Entity, std::string> m_playEntityNames;
    std::unordered_map<engine::ecs::Entity, PlayTriggerAction> m_playTriggerActions;
    std::unordered_map<engine::ecs::Entity, PlayCameraZone> m_playCameraZones;
    std::unordered_set<engine::ecs::Entity> m_playCameraZonesInside;
    engine::ecs::Entity m_activePlayCameraZone = engine::ecs::kNull;
    std::optional<EditorScene::CameraPreset> m_playCameraOverride;
    std::vector<PlayAgent> m_playAgents;
    std::vector<PlayAudioSource> m_playAudioSources;
    engine::ai::NavGrid m_playNavGrid;   // used by chase/search (M2); patrol needs none
    engine::ai::NavMesh m_playNavMesh;   // funnel-smoothed nav source (M6) when m_useNavMesh
    bool m_useNavMesh = false;           // route chase/search through the navmesh agent overload
    std::unordered_map<std::string, engine::ai::BehaviorGraph> m_playBtGraphCache;  // subtree assets
    engine::ai::NavMesh m_editorNavMesh;
    bool m_showNavigationPreview = false;
    bool m_showGrid = true;               // reference ground grid + world axes (edit mode)

    float m_fps = 60.0f;
    float m_elapsed = 0.0f;
    float m_dt = 0.016f;
    float m_autosaveTimer = 0.0f;
    float m_lastIblDay = -1.0f;
    bool m_renderingHdrPreview = false;
    PendingSceneAction m_pendingSceneAction = PendingSceneAction::None;
    std::string m_pendingScenePath;
    bool m_dirtyScenePromptQueued = false;
    std::array<char, 260> m_scenePathDraft{};
    std::array<char, 128> m_projectNameDraft{};       // New Project: name field
    std::array<char, 260> m_projectLocationDraft{};   // New Project: parent folder field
    std::array<char, 260> m_openProjectDraft{};       // Open Project: path to a .3dgproject
    std::unordered_map<int, bool> m_keyPrev;
    std::unordered_map<int, bool> m_scriptKeyPrev;
    std::unordered_map<int, bool> m_scriptMousePrev;
    std::unordered_map<std::string, bool> m_editModelLoadErrors;
    std::unordered_map<std::string, bool> m_editTextureLoadErrors;
    std::unordered_map<engine::ecs::Entity, float> m_animationPreviewTimes;
    std::unordered_map<engine::ecs::Entity, std::vector<glm::mat4>> m_editAnimationPoses;
    std::unordered_map<std::string, float> m_animationPreviewParameters;
    AnimationPreviewAction m_animationPreviewAction;
    int m_animationActionClip = 0;
    float m_animationActionFadeIn = 0.08f;
    float m_animationActionFadeOut = 0.15f;
    float m_animationActionSpeed = 1.0f;
    std::array<char, 128> m_animationActionMaskRoot{};
};
