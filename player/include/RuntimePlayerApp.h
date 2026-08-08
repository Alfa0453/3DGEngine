#pragma once

#include <engine/core/Application.h>
#include <engine/core/Config.h>
#include <engine/graphics/Renderer.h>
#include <engine/graphics/Mesh.h>
#include <engine/graphics/Shader.h>
#include <engine/graphics/Camera.h>
#include <engine/graphics/TextRenderer.h>
#include <engine/graphics/PbrRenderer.h>
#include <engine/graphics/FoliageRenderer.h>
#include <engine/graphics/SkinnedRenderer.h>
#include <engine/graphics/ProceduralSky.h>
#include <engine/graphics/Skybox.h>
#include <engine/graphics/PostProcess.h>
#include <engine/graphics/DayNightCycle.h>
#include <engine/graphics/IBL.h>
#include <engine/graphics/ParticleRenderer.h>
#include <engine/graphics/RuntimeParticleSystem.h>
#include <engine/graphics/CameraSequence.h>
#include <engine/graphics/CameraShake.h>
#include <engine/graphics/CameraBlend.h>
#include <engine/graphics/Terrain.h>
#include <engine/graphics/Water.h>
#include <engine/graphics/Framebuffer.h>
#include <engine/scene/RuntimeSceneLoader.h>
#include <engine/scene/LevelStreamingManager.h>
#include <engine/physics/PhysicsWorld.h>
#include <engine/gameplay/PlayerController.h>
#include <engine/gameplay/CameraDirector.h>
#include <engine/gameplay/Script.h>
#include <engine/gameplay/SaveGame.h>
#include <engine/assets/RuntimeAssetManager.h>
#include <engine/audio/AudioEngine.h>
#include <engine/audio/RuntimeAudioSystem.h>
#include <engine/ui/Hud.h>
#include <engine/ecs/Registry.h>
#include <engine/ecs/Components.h>
#include <engine/ai/AiAgent.h>
#include <engine/ai/BehaviorGraph.h>
#include <engine/ai/NavMesh.h>

#include <glm/glm.hpp>

#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// RM1 of the standalone runtime player (see RUNTIME_ROADMAP.md). Loads an
// editor-exported runtime scene and renders it with no editor/ImGui, using a
// free-fly dev camera. Simulation and HUD arrive in later milestones.
class RuntimePlayerApp : public engine::Application {
public:
    RuntimePlayerApp(engine::Config& config, std::string scenePath);

protected:
    void OnInit()                  override;
    void OnUpdate(float dt)        override;
    void OnFixedUpdate(float h)    override;
    void OnRender()                override;
    void OnShutdown()              override;

private:
    void LoadScene();
    void ConfigurePhysics();
    void LoadHud();
    void DrawHudOverlay();
    void RestartScene();
    unsigned int HudTextureId(const std::string& relPath);
    void SetupPlayer();          // find PlayerStart entity, attach a controller
    // Per-entity setup (animation-event hookup + MeshPBR conversion), scoped to a list
    // so both boot and streamed-level activation reuse it.
    void PostProcessLoadedEntities(const std::vector<engine::ecs::Entity>& entities);
    void ActivateStreamedLevel(
        std::size_t levelIndex,
        const engine::RuntimeSceneLoader::Scene& scene,
        const std::vector<engine::ecs::Entity>& entities);
    void PrepareStreamedLevelUnload(
        std::size_t levelIndex,
        const engine::RuntimeSceneLoader::Scene& scene,
        const std::vector<engine::ecs::Entity>& entities);
    void FinishStreamedLevelUnload(std::size_t levelIndex);
    void RebuildResidentScene();
    void RebuildResidentSystems();
    void BuildAI();
    void BakeNavigation();
    void UpdateAI(float dt);
    void BuildRuntimeLevelFeatures();
    void ProcessLevelPhysicsEvents();
    void RefreshCameraZone();
    void UpdateLockOn(bool inputEnabled);
    void BuildTerrains();
    void BuildWaters();
    void DrawWaters(const engine::Camera& camera, float aspect);
    void DrawEnvironmentSky(const glm::mat4& view, const glm::mat4& projection, bool tonemap);
    void CaptureWaterSceneBuffers();
    float WaterSurfaceY(float x, float z, bool& over) const;
    void ApplyWaterBuoyancy(float dt);
    void UpdateUnderwaterState(const engine::Camera& camera, float dt);
    float TerrainSurfaceY(float x, float z, bool& over) const;
    void ValidateRuntimeScene();
    void GatherPlayerInput();    // fill m_playerInput from the keyboard/mouse
    void SetPlayCursor(bool captured);
    void UpdateFreeCamera(float dt);
    engine::ScriptInputState CaptureScriptInput(bool enabled, bool includeFrameEdges);
    engine::Camera BuildCamera() const;
    void ProcessCameraCommands();
    void UpdateCameraSequence(float dt);
    void ExecuteCinematicCues(float previousTime, float currentTime, bool wrapped);
    void ExecuteCinematicCue(const engine::RuntimeSceneLoader::Scene::CinematicCue& cue);
    engine::ecs::Entity FindNamedEntity(const std::string& name) const;
    bool HasPlayer() const { return m_playerController.has_value(); }

    engine::Config&  m_config;
    engine::Renderer m_renderer;
    std::string      m_scenePath;

    std::optional<engine::Mesh> m_cube, m_plane, m_sphere, m_capsule,
                                m_cylinder, m_cone, m_pyramid, m_torus, m_staircase;
    std::optional<engine::PbrRenderer>   m_pbr;
    std::optional<engine::FoliageRenderer> m_foliageRenderer;
    std::optional<engine::Shader>        m_modelShader;   // static-model (LoadedModelAsset) pass
    std::optional<engine::SkinnedRenderer> m_skinnedRenderer;  // animated-character pass
    std::optional<engine::ProceduralSky> m_sky;
    std::optional<engine::Skybox>        m_importedSky;   // marketplace equirect sky (skyMode=1)
    std::optional<engine::PostProcess>   m_post;
    std::optional<engine::TextRenderer>  m_text;
    std::optional<engine::IBL>           m_ibl;
    std::optional<engine::ParticleRenderer> m_particleRenderer;

    engine::AudioEngine        m_audio;
    engine::RuntimeAudioSystem m_runtimeAudio;
    engine::CameraShake        m_cameraShake;
    engine::CameraShakeSample  m_cameraShakeSample;
    engine::CameraSequencePlayer m_cameraSequence;
    engine::CameraDirector     m_cameraDirector;
    engine::CameraBlend        m_zoneCameraBlend;
    std::optional<engine::CameraPose> m_zoneCameraPose;
    std::vector<engine::RuntimeSceneLoader::Scene::CinematicCue> m_activeCinematicCues;
    std::vector<engine::ScriptAnimationEvent> m_animationEvents;
    bool m_cinematicSkipPrev = false;

    engine::ecs::Registry              m_registry;
    engine::RuntimeSceneLoader::Scene  m_scene;
    engine::RuntimeSceneLoader::Scene  m_persistentScene;
    engine::PhysicsWorld               m_physics;
    engine::RuntimeAssetManager        m_assets;   // resolves HUD image textures
    engine::DayNightCycle::Sample      m_sample{};

    // HUD (the scene's referenced .hud, drawn during play).
    engine::HudDocument m_hud;
    bool                m_hudLoaded = false;
    engine::ecs::Entity m_hudHealthEntity = engine::ecs::kNull;   // bound health source
    std::string         m_sceneDir;   // directory of the scene file (relative asset base)
    bool                m_hudMousePrev = false;

    bool m_simReady  = false;   // a scene loaded successfully -> ok to simulate
    bool m_paused    = false;   // P toggles: freeze the simulation
    bool m_pausePrev = false;   // edge detector for the pause key

    // In-game save/load menu. F5 quicksaves to slot 0; F9 opens the load menu (Up/Down to
    // pick, Enter to load, Delete to erase, F9/Esc to close). The menu pauses the game.
    static constexpr int kSaveSlotCount = 6;
    bool m_loadMenuOpen = false;
    int  m_loadMenuSelection = 0;
    bool m_pausedBeforeMenu = false;
    std::vector<engine::SaveSlotInfo> m_loadMenuSlots;
    bool m_quickSavePrev = false, m_loadMenuKeyPrev = false, m_escPrev = false;
    bool m_menuUpPrev = false, m_menuDownPrev = false;
    bool m_menuEnterPrev = false, m_menuDeletePrev = false;
    float m_saveToastTime = 0.0f;      // seconds remaining for the "Saved" toast
    std::string m_saveToastText;
    void SaveToSlot(int slot, const std::string& name = {});
    bool LoadFromSlot(int slot);       // reloads the saved scene + restores state
    void CloseLoadMenu();              // restore pause/cursor when leaving the menu
    bool UpdateSaveLoadMenu();         // input; returns true if a load reloaded the world
    void DrawSaveLoadMenu(int screenW, int screenH);

    // Player controller (present when the scene has a "PlayerStart" entity); when
    // absent, the free-fly dev camera is used instead.
    std::optional<engine::PlayerController> m_playerController;
    engine::ecs::Entity                     m_playerEntity = engine::ecs::kNull;
    engine::ecs::Entity                     m_lockTarget = engine::ecs::kNull;
    engine::PlayerInput                     m_playerInput{};
    bool                                    m_lookPending = false;
    bool                                    m_lockTogglePrev = false;

    // Free-fly camera state.
    glm::vec3 m_camPos{0.0f, 3.0f, 10.0f};
    float     m_camYaw   = -1.5708f;   // looking toward -Z
    float     m_camPitch = -0.2f;
    double    m_lastMouseX = 0.0, m_lastMouseY = 0.0;
    bool      m_looking = false;
    std::unordered_map<int, bool> m_scriptKeyPrev;
    std::unordered_map<int, bool> m_scriptMousePrev;

    struct RuntimeAgent {
        engine::ecs::Entity entity = engine::ecs::kNull;
        engine::ecs::Entity targetEntity = engine::ecs::kNull;
        std::string name;
        int team = 0;
        bool autoTarget = false;
        bool useGraph = false;
        float hearingRange = 0.0f;
        engine::ai::AiAgent brain;
        engine::ai::AiMovementComponent movement;
        engine::ai::AgentContext context;
        engine::ai::BehaviorTree<engine::ai::AgentContext> tree;
    };
    std::vector<RuntimeAgent> m_agents;
    engine::ai::NavMesh m_navMesh;
    engine::ai::SoundField m_soundField;                   // transient noises agents can hear
    glm::vec3 m_prevPlayerPos{0.0f};
    bool m_prevPlayerPosValid = false;
    std::unordered_map<engine::ecs::Entity, float> m_prevHp;  // HP tracking -> combat noise
    std::unordered_map<std::string, engine::ai::BehaviorGraph> m_behaviorGraphCache;

    // Level-as-asset streaming: when booting a .3dgworld, the persistent level loads as
    // the main scene and the manager streams the rest around the viewer.
    engine::WorldManifest m_worldManifest;
    engine::LevelStreamingManager m_streaming;
    engine::RuntimeSceneLoader::PrimitiveMeshes m_primitiveMeshes;   // stashed for streamed loads
    std::string m_worldDir;
    std::string m_lastStreamingError;
    bool m_streamingEnabled = false;
    std::unordered_map<std::size_t, engine::RuntimeSceneLoader::Scene> m_streamedScenes;

    struct RuntimeTriggerAction {
        engine::ecs::Entity target = engine::ecs::kNull;
        int enterMover = 0, enterRotator = 0, exitMover = 0, exitRotator = 0;
        std::string cameraSequence;
        int enterCamera = 0, exitCamera = 0;
        bool cameraLockInput = true, cameraSkippable = true;
        engine::ecs::Mover mover;
        engine::ecs::Rotator rotator;
    };
    struct RuntimeCameraZone {
        std::string presetName;
        bool restoreOnExit = true;
        int priority = 0;
        float returnBlend = 0.35f;
    };
    struct RuntimeTerrain {
        engine::ecs::Entity entity = engine::ecs::kNull;
        engine::Terrain terrain;
    };
    struct RuntimeWater {
        engine::RuntimeSceneLoader::Scene::WaterDesc desc;
        engine::Water water;
        RuntimeWater(engine::RuntimeSceneLoader::Scene::WaterDesc authored,
                     const engine::WaterConfig& config)
            : desc(std::move(authored)), water(config) {}
    };
    std::unordered_map<engine::ecs::Entity, RuntimeTriggerAction> m_triggerActions;
    std::unordered_map<engine::ecs::Entity, RuntimeCameraZone> m_cameraZones;
    std::unordered_set<engine::ecs::Entity> m_cameraZonesInside;
    engine::ecs::Entity m_activeCameraZone = engine::ecs::kNull;
    std::vector<RuntimeTerrain> m_terrains;
    std::vector<RuntimeWater> m_waters;
    std::optional<engine::Framebuffer> m_waterSceneCopy;
    engine::PostProcess::UnderwaterSettings m_underwaterVisuals;
    float m_underwaterBlend = 0.0f;
    bool m_underwaterAudio = false;
    engine::TerrainCameraConstraint m_terrainCameraConstraint;
    std::vector<std::string> m_runtimeWarnings;

    std::string m_loadError;
    std::size_t m_entityCount = 0;
    int         m_assetErrors = 0;   // asset paths that failed to resolve
    float       m_dt = 0.016f, m_fps = 60.0f;
};
