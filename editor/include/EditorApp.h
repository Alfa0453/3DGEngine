#pragma once

#include <chrono>

#include <engine/core/Application.h>
#include <engine/core/Config.h>
#include <engine/assets/AssetRegistry.h>
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
#include <engine/graphics/FoliageRenderer.h>
#include <engine/graphics/ParticleRenderer.h>
#include <engine/graphics/RuntimeParticleSystem.h>
#include <engine/graphics/PostProcess.h>
#include <engine/graphics/ProceduralSky.h>
#include <engine/graphics/Skybox.h>
#include <engine/graphics/Renderer.h>
#include <engine/graphics/Shader.h>
#include <engine/graphics/SkinnedRenderer.h>
#include <engine/graphics/SSAO.h>
#include <engine/graphics/SSR.h>
#include <engine/graphics/Terrain.h>
#include <engine/graphics/Water.h>
#include <engine/graphics/Framebuffer.h>
#include <engine/graphics/GpuProfiler.h>
#include <engine/graphics/TextRenderer.h>
#include <engine/graphics/LightingBuildData.h>
#include <engine/graphics/DynamicIrradiance.h>
#include <engine/graphics/SSGI.h>
#include <engine/graphics/ReflectionProbeSystem.h>
#include <engine/gameplay/PlayerController.h>
#include <engine/gameplay/CameraDirector.h>
#include <engine/physics/PhysicsComponents.h>
#include "EditorDirtyDocument.h"
#include <engine/gameplay/Script.h>
#include <engine/gameplay/ScriptModule.h>
#include <engine/physics/PhysicsWorld.h>
#include <engine/ui/ImGuiLayer.h>
#include <MaterialMaker/MaterialMakerPanel.h>
#include "BehaviorGraphPanel.h"
#include "ParticleEditorPanel.h"
#include "ShaderEditorPanel.h"
#include "HudEditorPanel.h"
#include "CharacterEditorPanel.h"
#include "ClipEditorPanel.h"
#include "MeshEditorPanel.h"
#include "DecalPlacementPanel.h"
#include "OptimizationAuditorPanel.h"
#include "LightingAnalysisPanel.h"
#include "RagdollPhysicsPanel.h"
#include "AnimationRetargetingPanel.h"
#include "AbilityEditorPanel.h"
#include "RuntimePropertyInspectorPanel.h"
#include "AssetDependencyViewerPanel.h"
#include "WeatherEditorPanel.h"
#include "ProceduralBuildingPanel.h"
#include "RoadGeneratorPanel.h"
#include "LevelInstancePanel.h"
#include "WorldPartitionPanel.h"
#include "ProceduralScatterGraphPanel.h"
#include "BiomeEditorPanel.h"
#include "DayNightTimelinePanel.h"
#include "CaveTunnelPanel.h"
#include "FenceWallPainterPanel.h"
#include "DestructionAuthoringPanel.h"
#include "TerrainCreatorPanel.h"
#include "ModularPlacementPanel.h"
#include "PrefabPalettePanel.h"
#include "RoomBuilderPanel.h"
#include "ScatterPaintPanel.h"
#include "ArrayToolPanel.h"
#include "MeasurementPanel.h"
#include "LevelValidationPanel.h"
#include "LevelVariantPanel.h"
#include "LevelLayersPanel.h"
#include "ViewportBookmarksPanel.h"
#include "BlockoutPanel.h"
#include "AlignmentPanel.h"
#include "SplineBuilderPanel.h"
#include "PrefabAsset.h"

#include <engine/scene/WorldManifest.h>
#include "AnimationGraphEditorPanel.h"

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
#include <cstdint>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <filesystem>
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
        LoadScene,
        OpenProject,
        NewProject,
        RestartScripts
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
        engine::ecs::Entity configuredTargetEntity = engine::ecs::kNull;
        std::string         name;                                // scene object name (debug label)
        int                 team = 0;                            // faction id (0 = neutral)
        bool                autoTarget = false;                  // acquire nearest hostile each tick
        float               hearingRange = 0.0f;                 // how far this agent hears noises (0 = deaf)
        bool                perceivesTarget = false;             // saw its target this frame (for squad alerts)
        glm::vec3           perceivedTargetPos{0.0f};            // where it last saw the target
        int                 flankSlot = 0;                       // assigned slot when surrounding a shared target
        int                 flankCount = 1;                      // how many teammates share that target (1 = solo)
        float               squadAlertRadius = 18.0f;            // responds to a teammate's alert within this range
        float               squadForgetTime = 6.0f;              // seconds this agent's sighting keeps its squad alerted
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
    void DrawClipEditorPanel();
    void DrawGraphEditorPanel();
    void DrawMeshEditorPanel();
    void DrawDecalPlacementPanel();
    void DrawTerrainCreatorPanel();
    void DrawModularPlacementPanel();
    void DrawPrefabPalettePanel();
    void DrawRoomBuilderPanel();
    void DrawScatterPaintPanel();
    void DrawArrayToolPanel();
    void DrawMeasurementPanel();
    void DrawLevelValidationPanel();
    void DrawOptimizationAuditorPanel();
    void DrawLightingAnalysisPanel();
    void DrawRagdollPhysicsPanel();
    void DrawAnimationRetargetingPanel();
    void DrawAbilityEditorPanel();
    void DrawRuntimePropertyInspectorPanel();
    void DrawAssetDependencyViewerPanel();
    void DrawWeatherEditorPanel();
    void DrawProceduralBuildingPanel();
    void DrawRoadGeneratorPanel();
    void DrawLevelInstancePanel();
    void DrawWorldPartitionPanel();
    void DrawProceduralScatterGraphPanel();
    void DrawBiomeEditorPanel();
    void DrawDayNightTimelinePanel();
    void DrawCaveTunnelPanel();
    void GenerateCaveTunnel();
    int DeleteGeneratedCaveTunnel(const std::string& caveName);
    void DrawFenceWallPainterPanel();
    void GenerateFenceWall();
    int DeleteGeneratedFenceWall(const std::string& name);
    void DrawDestructionAuthoringPanel();
    void GenerateDestructionPreview();
    int DeleteDestructionPreview(const std::string& name);
    bool CreatePartitionCellFromSelection(const std::string& path, int cellX, int cellZ);
    void DrawLevelVariantPanel();
    void DrawLevelLayersPanel();
    void DrawViewportBookmarksPanel();
    void DrawBlockoutPanel();
    void DrawAlignmentPanel();
    void DrawSplineBuilderPanel();
    void GenerateSplineBuild();
    int DeleteGeneratedSplineBuild(const std::string& groupName);
    void GenerateBlockout();
    int DeleteGeneratedBlockout(const std::string& groupName);
    void GenerateRoom();
    int DeleteGeneratedRoom(const std::string& roomName);
    void GenerateProceduralBuilding();
    int DeleteGeneratedProceduralBuilding(const std::string& buildingName);
    void GenerateRoad();
    int DeleteGeneratedRoad(const std::string& roadName);
    void PaintScatterStamp(const glm::vec3& center, const glm::vec3& normal,
                           bool projectToTerrain);
    int EraseScatterAt(const glm::vec3& center, float radius);
    int ClearPaintedScatter();
    void GenerateObjectArray();
    int DeleteGeneratedArray(const std::string& groupName);
    bool ComputeModularPlacement(float viewportX, float viewportY,
                                 glm::vec3* position, glm::vec3* normal);
    bool PlaceSelectedModule(const glm::vec3& position,
                             const glm::vec3& surfaceNormal);
    void ReplaceSelectionWithModule();
    bool ComputePrefabPalettePlacement(float viewportX, float viewportY,
                                       const PrefabPalettePanel::Placement& placement,
                                       glm::vec3* position, glm::vec3* normal);
    bool PlacePalettePrefab(const PrefabPalettePanel::Placement& placement,
                            const glm::vec3& position);
    void ReplaceSelectionWithPalettePrefab();
    void DrawPrefabEditorPanel();   // author a reusable object template (.3dgprefab)
    void DrawScriptDebugPanel();    // live per-entity script field inspector (Play mode)
    void HotReloadScripts();        // rebuild + reload game_scripts.dll without restart (dev)
    bool LoadProjectScriptModule(bool reportMissing = false);
    bool InstallProjectScriptCandidate(const std::filesystem::path& candidate,
                                       std::uint64_t generation,
                                       bool reportMissing = false);
    bool RecoverInterruptedScriptLoad(const std::filesystem::path& projectRoot);
    void DrainScriptBuild(bool shuttingDown);
    void AcknowledgeCreatedScriptSource();
    void UpdateScriptAutoReload(float dt);
    void UpdateMaterialForgeDeployments(float dt);
    void StartScriptAutoBuild();
    void ResetScriptAutoReloadWatcher();
    void DrawWorldEditorPanel();    // compose a streamed world (.3dgworld) from level scenes
    // Cook an authoring world (editor-scene refs) into a runnable one: export each level
    // to a runtime scene, compute bounds, and write the cooked .3dgworld.
    bool CookWorld(const engine::WorldManifest& authoring, const std::string& authoringDir,
                   const std::string& outputDir, std::string* error);
    void DrawViewportPanel();   // scene rendered into a dockable, interactive panel
    // Maps a main-window cursor position into scene render-pixel space when the Viewport
    // panel owns input; returns false (and passes the point through) otherwise.
    bool RemapViewportMouse(float winX, float winY, float& outX, float& outY);
    void DrawPlayHud();
    void SyncHudFromScene();   // load the scene's referenced .hud into m_hud
    void ScanHudImages();      // recursively list content-folder images for the picker
    unsigned int HudTextureId(const std::string& relPath);  // resolve HUD image -> GL texture id
    void DrawMaterialMakerTools(bool materialSaved);
    void DrawDirtyScenePrompt();
    std::vector<DirtyDocument> CollectDirtyDocuments();
    bool SaveAllDirtyDocuments(std::string* error);
    EditorDockspace::GameplayDebugState BuildGameplayDebugState();
    EditorDockspace::AnimationPreviewState BuildAnimationPreviewState();
    EditorDockspace::AnimationPreviewState BuildAnimationAssetPreviewState();
    void OpenAnimationAssetPreview(const std::string& path,
                                   EditorAssets::Type type);
    void RefreshAnimationAssetPreviewChoices();
    unsigned int RenderAnimationAssetPreview(
        const engine::SkinnedModel& model, int clipIndex,
        float durationSeconds);
    void DrawAssetOverlay(float x, float y, const glm::vec3& text, const glm::vec3& muted);
    void DrawLogOverlay(float x, float y, const glm::vec3& text, const glm::vec3& muted);
    void HandleGlobalShortcuts(engine::Window& window);
    void HandleAssetShortcuts(engine::Window&, bool controlDown);
    void HandleEditorCommandShortcuts(engine::Window&, bool controlDown);
    void DrawPlayScene(const glm::mat4& viewProj);
    void DrawEditScene(const glm::mat4& viewProj);
    void UpdateEnvironmentIbl(const EditorScene::Environment& environment,
                          const engine::DayNightCycle::Sample& sky);
    // (Re)load the imported equirectangular sky when skyTexturePath changes.
    void EnsureImportedSky(const EditorScene::Environment& environment);
    // Draw the active sky (procedural or imported) — used by the scene render and IBL.
    void DrawEnvironmentSky(const glm::mat4& view, const glm::mat4& projection,
                            const engine::DayNightCycle::Sample& sky, bool tonemap);
    void ConfigureEnvironmentPbrOptions(engine::ecs::Registry& registry,
                                        engine::PbrRenderer::Options& options,
                                        const EditorScene::Environment& environment,
                                        const engine::DayNightCycle::Sample& sky);
    std::uint64_t ComputeLightingStateHash() const;
    std::uint64_t ComputeDynamicGiGeometryHash() const;
    std::vector<engine::LightingTriangle> GatherLightingTriangles() const;
    void StartLightingBuild();
    void PollLightingBuild();
    bool CaptureSelectedReflectionProbe(bool clearOnly, bool buildCapture = false);
    void LoadSceneLightingAsset();
    void UpdateDynamicGi(engine::ecs::Registry& registry,
                         const EditorScene::Environment& environment,
                         const engine::DayNightCycle::Sample& sky);
    void TogglePanel(EditorPanels::Panel panel);
    void HandleMouseAssetDrag();
    void HandleMouseViewportSelection();
    void HandleMouseViewportGizmo();
    void HandleTerrainSculpt();
    void HandleFoliagePaint();
    void AddTerrainMeshes(engine::ecs::Registry& pbrRegistry);   // shared edit + play terrain draw
    // PBR surface and CPU texture data baked for a painted terrain layer.
    engine::TerrainLayerSurface TerrainLayerMaterialSurface(const std::string& materialPath);
    engine::TerrainLayerTexture TerrainLayerMaterialTexture(const std::string& materialPath);
    bool AverageImageColor(const std::string& relativePath, glm::vec3& outColor);
    float TerrainSurfaceY(float worldX, float worldZ, bool& over);  // walkable height query
    std::string TerrainNameAt(float worldX, float worldZ);          // terrain object under a point
    float WaterSurfaceY(float worldX, float worldZ, bool& over);     // wave height for buoyancy
    bool UpdateUnderwaterState(const engine::Camera& camera, float dt);
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
    void AddWater(int preset = 0, bool createRiverSpline = true); // 0 generic, 1 lake, 2 ocean, 3 river
    void DrawWaterBodies(const engine::Camera& camera, float aspect);   // transparent water pass
    void CaptureWaterSceneBuffers();
    void DrawGrass(const engine::Camera& camera, float aspect);         // instanced grass on terrain
    void DrawFoliage(const engine::Camera& camera, float aspect);       // instanced mesh foliage actors
    void AddSpline(int type = 0);                         // create an editable path/river/rail
    void DrawSplines(const glm::mat4& viewProj);          // curve + control-point handles
    void AddTriggerVolume();
    void AddNavMeshBoundsVolume();
    void AddPlayerStart();
    void AddCharacterToScene(const CharacterAsset& character, const glm::vec3& position,
                             const std::string& assetPath = std::string());   // instantiate a .3dgcharacter
    void AddPrefabToScene(const PrefabAsset& prefab, const glm::vec3& position,
                          const std::string& assetPath = std::string());   // instantiate a .3dgprefab
    void SyncPrefabInstances(const std::string& prefabPath, const PrefabAsset& prefab);   // re-apply to linked instances
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
    // Bake the current multi-selection of static objects (primitives + static
    // model assets) into one .3dgmesh and replace them with a single object
    // referencing it (implemented in EditorApp_MergeMesh.cpp).
    bool MergeSelectedToSingleMesh();
    void DeleteSelected();
    void Undo();
    void Redo();
    void SaveScene();
    void SaveSceneAs(const std::string& path);
    void PersistProject();                                       // save project settings to the right config
    void LoadProjectAssetRegistry();
    void NewProject(const std::string& location, const std::string& name);
    void OpenProjectFromPath(const std::string& projectFile);
    void RequestOpenProjectFromPath(const std::string& projectFile);
    void RequestNewProject(const std::string& location, const std::string& name);
    void RequestScriptCompileRestart();
    bool PerformScriptCompileRestart();
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
    void CookProject();
    void PackageProject();
    void LoadPackagingSettings();
    void PersistPackagingSettings();
    void UpdatePackageBuild();
    void ValidateRuntimeScene();
    void TriggerAnimationPreviewAction();
    void UpdateEditParticlePreviews(float dt);
    void DrawEditParticlePreviews();
    void ClearEditParticlePreviews();
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
    // Enable/disable grounded foot IK on every play AnimatedModel and give each a ground
    // raycast that ignores its own collider. Cheap; called each fixed step before animation.
    void ConfigurePlayFootIK();
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
    engine::AssetRegistry m_assetRegistry;
    std::string m_dependencyAssetOpenPath;
    EditorAssets::Type m_dependencyAssetOpenType = EditorAssets::Type::Other;
    EditorDockspace       m_dockspace;
    EditorDragDrop        m_dragDrop;
    EditorGizmo           m_gizmo;
    int                   m_selectedSplinePoint = -1;
    EditorLog             m_log;
    EditorMouseController m_mouse;
    EditorPanels          m_panels;
    EditorProject         m_project;
    EditorRuntimeController m_runtime;
    EditorScene           m_scene;
    EditorCameraController m_cameraController;
    EditorContentController m_content;

    // Drag-and-drop import: files dropped from the OS explorer open an Import Settings
    // popup that lists each file with its detected type before importing.
    struct PendingImportFile {
        std::string path;          // absolute source path
        std::string name;          // filename for display
        std::string extension;     // lowercase, with dot
        bool        isModel = false;
        bool        isTexture = false;
        int         modelMode = 0; // 0=Automatic 1=Static 2=Skeletal 3=Animation
        // Model source inspection (fills the default mode + a summary line).
        bool        inspected = false;
        bool        hasBones = false;
        int         meshCount = 0;
        int         animationCount = 0;
        // Texture options.
        bool        srgb = true;   // color maps sRGB; normal/mask maps should be off
        bool        smooth = true; // linear filtering
    };
    std::vector<PendingImportFile> m_pendingImports;
    bool m_importDialogRequested = false;   // open the popup this frame
    int  m_importGlobalModelMode = 0;       // combo that sets every model's mode
    std::array<char, 260> m_importFolderBuffer{};   // target folder (relative to Content)
    void BeginImportDialog(const std::vector<std::string>& paths);
    void DrawImportDialog();
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
    engine::LightingProbeGrid            m_lightingProbeGrid;
    engine::DynamicIrradianceSystem       m_dynamicGi;
    std::optional<engine::LightingBuildData> m_loadedLightingData;
    std::uint64_t                        m_dynamicGiConfigurationHash = 0;
    std::uint64_t                        m_dynamicGiGeometryHash = 0;
    std::uint32_t                        m_dynamicGiFrame = 0;
    engine::ReflectionProbeSystem        m_reflectionProbes;
    struct LightingBuildResult {
        bool success = false;
        engine::LightingBuildData data;
        std::string path;
        std::string error;
    };
    std::future<LightingBuildResult>      m_lightingBuildFuture;
    std::shared_ptr<engine::LightingBuildProgress> m_lightingBuildProgressState;
    bool                                 m_lightingBuildRunning = false;
    bool                                 m_lightingBuildDirty = false;
    int                                  m_lightingBuildQuality = 1;
    std::string                          m_lightingBuildStatus = "Missing - no lighting asset is assigned";
    bool                                 m_forceDirectionalShadowUpdate = false;
    std::chrono::steady_clock::time_point m_lightingBuildStartedAt{};
    double                               m_lastLightingBuildMs = 0.0;
    double                               m_lastReflectionCaptureMs = 0.0;
    std::uint64_t                        m_lastLightingBuildRays = 0;
    std::string                          m_loadedLightingAsset;
    std::optional<engine::FoliageRenderer> m_foliageRenderer;
    std::optional<engine::ParticleRenderer> m_particleRenderer;
    std::optional<engine::PostProcess>   m_postProcess;
    std::optional<engine::ProceduralSky> m_sky;
    std::optional<engine::Skybox>        m_importedSky;   // imported equirect sky (when skyMode=1)
    std::string                          m_importedSkyPath;
    std::string                          m_lastSkySignature;   // IBL re-bake key
    std::optional<engine::IBL>           m_ibl;
    std::optional<engine::SSAO>          m_ssao;
    std::optional<engine::SSGI>          m_ssgi;
    std::optional<engine::SSR>           m_ssr;
    std::optional<engine::Framebuffer>   m_viewportFbo;   // scene-in-a-panel display target
    std::optional<engine::Framebuffer>   m_waterSceneCopy; // opaque colour + depth sampled by water
    engine::PostProcess::UnderwaterSettings m_underwaterVisuals;
    float m_underwaterBlend = 0.0f;
    // Scene-viewport panel rect in main-window pixel space (set each frame by
    // DrawViewportPanel). When valid, mouse picking / camera / gizmo route here.
    float m_sceneViewX = 0.0f;
    float m_sceneViewY = 0.0f;
    float m_sceneViewW = 0.0f;
    float m_sceneViewH = 0.0f;
    bool  m_sceneViewValid = false;     // panel visible & docked in the main window
    bool  m_sceneViewHovered = false;   // image is the top hovered item
    // Cached generated terrain per object; regenerated when the object's params change.
    struct TerrainCache {
        engine::Terrain terrain;
        int   res = 0;
        float size = 0.0f, maxHeight = 0.0f, frequency = 0.0f;
        int   seed = 0, octaves = 0;
    };
    std::unordered_map<engine::ecs::Entity, TerrainCache> m_terrains;
    std::unordered_map<engine::ecs::Entity, engine::Water> m_waters;   // one Water per water object
    // Cache of custom water-shader GLSL source keyed by file path, with the file's last
    // write time so edits hot-reload. Returned source feeds WaterConfig.customFragmentSource.
    std::unordered_map<std::string,
        std::pair<std::filesystem::file_time_type, std::string>> m_waterShaderCache;
    std::unordered_map<engine::ecs::Entity, std::unique_ptr<engine::GrassField>> m_grass;   // one grass field per terrain
    std::unordered_map<std::string, engine::TerrainLayerSurface> m_terrainMaterialSurfaces;
    std::unordered_map<std::string, engine::TerrainLayerTexture> m_terrainMaterialTextures;
    engine::TerrainCameraConstraint m_terrainCameraConstraint;
    bool  m_terrainSculpt = false;        // sculpt mode active (paints the selected terrain)
    int   m_terrainSculptMode = 0;        // 0 raise, 1 lower, 2 smooth, 3 flatten, 4 paint
    int   m_terrainPaintLayer = 1;        // 0 auto/erase, 1 grass, 2 rock, 3 dirt, 4 snow, 5 sand
    float m_terrainBrushRadius = 5.0f;
    float m_terrainBrushStrength = 6.0f;
    glm::vec2 m_terrainBrushCenterLocal{0.0f};
    bool  m_terrainBrushHoverValid = false;
    bool  m_terrainBrushApplying = false;
    bool  m_foliagePaint = false;
    bool  m_foliageErase = false;
    float m_foliageBrushRadius = 4.0f;
    float m_foliagePaintDensity = 0.5f;
    int   m_foliageTypeIndex = 0;
    float m_foliageStrokeCooldown = 0.0f;
    glm::vec3 m_foliageBrushCenterWorld{0.0f};
    std::vector<glm::vec3> m_foliageBrushRing;
    bool m_foliageBrushHoverValid = false;
    bool m_foliageBrushApplying = false;
    std::optional<engine::TextRenderer>  m_text;
    engine::ImGuiLayer                   m_imgui;
    engine::GpuProfiler                  m_gpuProfiler;   // per-pass GPU timings
    bool                                 m_showProfiler = true;
    double                               m_cpuFrameMs = 0.0;   // CPU cost of OnRender
    double                               m_cpuSceneMs = 0.0;   // CPU cost of scene submission
    double                               m_cpuUiMs = 0.0;      // CPU cost of building the UI
    static constexpr int                 kFrameHistory = 120;
    std::array<float, kFrameHistory>     m_frameMsHistory{};   // raw frames used by statistics
    int                                  m_frameMsHead = 0;    // raw ring-buffer write cursor
    // The graph is sampled at a fixed rate and lightly filtered. Recording one point per
    // rendered frame makes an uncapped editor graph alias with OS/GPU scheduling and look
    // unstable even when frame pacing is healthy.
    std::array<float, kFrameHistory>     m_frameGraphHistory{};
    int                                  m_frameGraphHead = 0;
    float                                m_smoothedFrameMs = 0.0f;
    float                                m_frameGraphAccumulator = 0.0f;
    bool                                 m_frameGraphInitialized = false;
    int                                  m_renderW = 0;        // 3D render target width (render scale)
    int                                  m_renderH = 0;        // 3D render target height
    material_maker::MaterialMakerPanel   m_materialMaker;
    BehaviorGraphPanel                   m_behaviorGraph;
    ParticleEditorPanel                  m_particleEditor;
    ShaderEditorPanel                    m_shaderEditor;
    HudEditorPanel                       m_hudPanel;
    CharacterEditorPanel                 m_characterEditor;
    ClipEditorPanel                      m_clipEditor;
    AnimationGraphEditorPanel            m_graphEditor;
    MeshEditorPanel                      m_meshEditor;
    DecalPlacementPanel                  m_decalPlacement;
    TerrainCreatorPanel                 m_terrainCreator;
    ModularPlacementPanel                m_modularPlacement;
    PrefabPalettePanel                   m_prefabPalette;
    RoomBuilderPanel                     m_roomBuilder;
    ScatterPaintPanel                    m_scatterPaint;
    ArrayToolPanel                       m_arrayTool;
    MeasurementPanel                     m_measurementPanel;
    LevelValidationPanel                 m_levelValidation;
    OptimizationAuditorPanel             m_optimizationAuditor;
    LightingAnalysisPanel                m_lightingAnalysis;
    RagdollPhysicsPanel                   m_ragdollPhysics;
    AnimationRetargetingPanel             m_animationRetargeting;
    AbilityEditorPanel                    m_abilityEditor;
    RuntimePropertyInspectorPanel         m_runtimePropertyInspector;
    AssetDependencyViewerPanel            m_assetDependencyViewer;
    WeatherEditorPanel                    m_weatherEditor;
    ProceduralBuildingPanel               m_proceduralBuilding;
    RoadGeneratorPanel                    m_roadGenerator;
    LevelInstancePanel                    m_levelInstances;
    WorldPartitionPanel                   m_worldPartition;
    ProceduralScatterGraphPanel           m_proceduralScatterGraph;
    BiomeEditorPanel                       m_biomeEditor;
    DayNightTimelinePanel                  m_dayNightTimeline;
    CaveTunnelPanel                       m_caveTunnel;
    FenceWallPainterPanel                m_fenceWallPainter;
    DestructionAuthoringPanel            m_destructionAuthoring;
    LevelVariantPanel                    m_levelVariants;
    LevelLayersPanel                     m_levelLayers;
    ViewportBookmarksPanel               m_viewportBookmarks;
    BlockoutPanel                        m_blockoutPanel;
    AlignmentPanel                       m_alignmentPanel;
    SplineBuilderPanel                   m_splineBuilder;
    glm::vec3                            m_lastModulePaintPosition{0.0f};
    bool                                 m_hasLastModulePaintPosition = false;
    glm::vec3                            m_scatterBrushPosition{0.0f};
    glm::vec3                            m_scatterBrushNormal{0.0f, 1.0f, 0.0f};
    glm::vec3                            m_lastScatterStrokePosition{0.0f};
    bool                                 m_hasScatterBrushHit = false;
    bool                                 m_hasLastScatterStrokePosition = false;
    PrefabAsset                          m_prefabAsset;      // prefab being authored in the Prefab Editor
    engine::WorldManifest                m_worldAuthoring;   // world being composed in the World Editor
    std::string                          m_worldAuthoringPath;  // .3dgworld authoring file
    std::string                          m_worldCookOutputDir;  // where Cook writes runtime scenes + manifest
    std::string                          m_worldStatus;      // last save/load/cook message
    std::string                          m_prefabPath;       // current .3dgprefab path ("" = unsaved)
    engine::ScriptModule                 m_scriptModule;     // loaded hot-reload script DLL (dev)
    std::vector<std::string>             m_projectScriptClasses;
    std::vector<std::string>             m_projectBtScriptClasses;
    enum class ProjectScriptModuleState {
        Unloaded, Building, CandidateReady, Validating, Reloading, Ready,
        BuildFailed, LoadFailed, Quarantined, SafeMode
    };
    ProjectScriptModuleState             m_projectScriptModuleState =
        ProjectScriptModuleState::Unloaded;
    bool                                 m_projectScriptSafeMode = false;
    bool                                 m_scriptModuleInstallInProgress = false;
    bool                                 m_scriptShuttingDown = false;
    std::uint64_t                        m_scriptBuildGeneration = 0;
    std::uint64_t                        m_activeScriptBuildGeneration = 0;
    std::filesystem::path                m_activeScriptCandidate;
    struct ScriptBuildResult {
        bool success = false;
        std::string error;
        std::filesystem::path projectRoot;
        std::filesystem::path candidateDll;
        std::uint64_t generation = 0;
    };
    std::future<ScriptBuildResult>       m_scriptBuildFuture;
    std::unordered_map<std::string, std::uint64_t> m_scriptSourceSnapshot;
    bool                                 m_scriptSourceSnapshotInitialized = false;
    bool                                 m_autoCompileScripts = true;
    bool                                 m_scriptBuildRunning = false;
    bool                                 m_scriptBuildPending = false;
    float                                m_scriptWatchPoll = 0.0f;
    float                                m_scriptBuildDebounce = 0.0f;
    std::string                          m_scriptBuildStatus = "Watching Content/Scripts";
    float                                m_materialForgeDeployPoll = 0.0f;
    std::string                          m_materialForgeDeploySignalPath;
    std::string                          m_materialForgeDeployToken;
    struct PackageBuildResult {
        bool success = false;
        std::string error;
        std::filesystem::path artifact;
    };
    std::future<PackageBuildResult>       m_packageBuildFuture;
    bool                                  m_packageBuildRunning = false;
    std::string                           m_packageBuildStatus = "Ready to package";
    std::array<char, 512>                 m_packageOutputDraft{};
    int                                   m_packageConfiguration = 0;
    bool                                  m_packageStaticRuntime = true;
    bool                                  m_packageCleanOutput = true;
    bool                                  m_packageCreateZip = true;
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
    engine::ecs::Registry m_editParticlePreviewRegistry;
    std::unordered_map<engine::ecs::Entity, engine::ecs::Entity>
        m_editParticlePreviewEntities;
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
    bool m_showGameplayTraces = true;
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
    engine::ai::SoundField m_playSoundField;   // transient noises agents can hear (footsteps, etc.)
    glm::vec3 m_prevPlayerPos{0.0f};           // for detecting player movement -> footstep noise
    bool m_prevPlayerPosValid = false;
    std::unordered_map<engine::ecs::Entity, float> m_prevHp;  // last-frame HP -> emit noise on damage
    // Squad alert memory (per team): rises to 1 on a sighting, decays over time, and
    // holds the target's last-known position so a squad keeps searching after losing
    // sight, then de-escalates to patrol together.
    struct SquadAlert { float level = 0.0f; glm::vec3 poi{0.0f}; bool valid = false; float forget = 6.0f; };
    std::unordered_map<int, SquadAlert> m_squadAlerts;
    engine::ai::NavGrid m_playNavGrid;   // used by chase/search (M2); patrol needs none
    engine::ai::NavMesh m_playNavMesh;   // funnel-smoothed nav source (M6) when m_useNavMesh
    bool m_useNavMesh = false;           // route chase/search through the navmesh agent overload
    std::unordered_map<std::string, engine::ai::BehaviorGraph> m_playBtGraphCache;  // subtree assets
    engine::ai::NavMesh m_editorNavMesh;
    bool m_showNavigationPreview = false;
    bool m_showGrid = true;               // reference ground grid + world axes (edit mode)
    bool m_previewSceneAnimations = false; // advance character animations in the edit viewport (off = paused idle)
    bool m_playFootIK = false;             // experimental: grounded foot IK during Play (raycasts the scene)

    float m_fps = 60.0f;
    float m_elapsed = 0.0f;
    float m_dt = 0.016f;
    float m_autosaveTimer = 0.0f;
    float m_lastIblDay = -1.0f;
    bool m_renderingHdrPreview = false;
    PendingSceneAction m_pendingSceneAction = PendingSceneAction::None;
    std::string m_pendingScenePath;
    std::string m_pendingActionArgument;
    std::vector<DirtyDocument> m_pendingDirtyDocuments;
    std::string m_dirtyDocumentSaveError;
    bool m_dirtyScenePromptQueued = false;
    // A script's RequestSceneLoad during Play, deferred to the next frame's top.
    std::string m_playSceneLoadRequest;
    // One-time note that level streaming is a packaged-world-only feature in editor Play.
    bool m_warnedEditorLevelStreaming = false;
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
    std::string m_animationAssetPreviewPath;
    std::string m_animationAssetPreviewMeshPath;
    EditorAssets::Type m_animationAssetPreviewType = EditorAssets::Type::Other;
    std::vector<EditorDockspace::AnimationPreviewState::AssetChoice>
        m_animationAssetPreviewMeshes;
    std::unordered_map<std::string, std::string> m_animationPreferredRigBySkeleton;
    engine::RuntimeAssetManager m_animationPreviewAssets;
    std::optional<engine::Framebuffer> m_animationAssetPreviewFbo;
    std::unique_ptr<engine::SkinnedRenderer> m_animationAssetPreviewRenderer;
    std::vector<glm::mat4> m_animationAssetPreviewPose;
    float m_animationAssetPreviewTime = 0.0f;
    float m_animationAssetPreviewSpeed = 1.0f;
    float m_animationAssetPreviewYaw = 0.0f;
    float m_animationAssetPreviewPitch = 0.0f;
    float m_animationAssetPreviewZoom = 1.0f;
    bool m_animationAssetPreviewPlaying = true;
    bool m_animationAssetPreviewLoop = true;
    bool m_animationAssetPreviewStripRootMotion = false;
    bool m_animationAssetPreviewRestartRequested = false;
    bool m_animationAssetPreviewRefreshRequested = false;
    AnimationPreviewAction m_animationPreviewAction;
    int m_animationActionClip = 0;
    float m_animationActionFadeIn = 0.08f;
    float m_animationActionFadeOut = 0.15f;
    float m_animationActionSpeed = 1.0f;
    std::array<char, 128> m_animationActionMaskRoot{};
};
