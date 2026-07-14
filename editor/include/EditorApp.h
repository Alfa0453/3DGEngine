#pragma once

#include <engine/core/Application.h>
#include <engine/core/Config.h>
#include <engine/assets/RuntimeAssetManager.h>
#include <engine/animation/AnimatedModel.h>
#include <engine/ecs/Components.h>
#include <engine/ecs/Registry.h>
#include <engine/graphics/Camera.h>
#include <engine/graphics/DayNightCycle.h>
#include <engine/graphics/IBL.h>
#include <engine/graphics/Mesh.h>
#include <engine/graphics/Model.h>
#include <engine/graphics/PbrRenderer.h>
#include <engine/graphics/PostProcess.h>
#include <engine/graphics/ProceduralSky.h>
#include <engine/graphics/Renderer.h>
#include <engine/graphics/Shader.h>
#include <engine/graphics/SkinnedRenderer.h>
#include <engine/graphics/SSAO.h>
#include <engine/graphics/SSR.h>
#include <engine/graphics/TextRenderer.h>
#include <engine/gameplay/PlayerController.h>
#include <engine/physics/PhysicsComponents.h>
#include <engine/gameplay/Script.h>
#include <engine/physics/PhysicsWorld.h>
#include <engine/ui/ImGuiLayer.h>
#include <MaterialMaker/MaterialMakerPanel.h>

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
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

class EditorApp final : public engine::Application {
public:
    explicit EditorApp(engine::Config& config);

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
        engine::ecs::Mover mover;
        engine::ecs::Rotator rotator;
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
    void DrawMaterialMakerTools(bool materialSaved);
    void DrawDirtyScenePrompt();
    EditorDockspace::GameplayDebugState BuildGameplayDebugState();
    EditorDockspace::AnimationPreviewState BuildAnimationPreviewState();
    void DrawAssetOverlay(float x, float y, const glm::vec3& text, const glm::vec3& muted);
    void DrawLogOverlay(float x, float y, const glm::vec3& text, const glm::vec3& muted);
    void HandleGlobalShortcuts(engine::Window& window);
    void HandleAssetShortcuts(engine::Window& window, bool controlDown);
    void HandleEditorCommandShortcuts(engine::Window& window, bool controlDown);
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
    void BeginAssetDrag();
    void DropPayloadOnScene();
    glm::vec3 SceneDropPosition();
    bool IsViewportDropPosition(float x, float y);
    float ProjectGizmoDrag(float dx, float dy, const glm::mat4& viewProj,
                           int viewportWidth, int viewportHeight) const;
    void AddCube();
    void AddPlane();
    void AddSphere();
    void AddDynamicCube();
    void AddStaticFloor();
    void AddTriggerVolume();
    void AddPlayerStart();
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
    void ApplyPlayTriggerAction(engine::ecs::Entity trigger, engine::ecs::Entity other, engine::CollisionEvent::Phase phase);
    void PushPlayTriggerActionRow(const std::string& triggerName,
                              const std::string& targetName,
                              const std::string& componentName,
                              bool enabled,
                              engine::CollisionEvent::Phase phase);
    void UpdatePlayPlayerController(float dt, bool inputEnabled);
    engine::ScriptInputState CapturePlayScriptInput(bool inputEnabled);
    void StepPlayPhysics(float dt, bool inputEnabled);
    void CapturePlayPhysicsEvents();
    bool Pressed(int key);

    engine::Config&       m_config;
    engine::Renderer      m_renderer;
    engine::Camera        m_camera{ glm::vec3(0.0f, 3.0f, 8.0f) };
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

    std::optional<engine::Mesh>          m_cube;
    std::optional<engine::Mesh>          m_cone;
    std::optional<engine::Mesh>          m_plane;
    std::optional<engine::Mesh>          m_sphere;
    std::optional<engine::Shader>        m_shader;
    std::optional<engine::Shader>        m_modelShader;
    std::optional<engine::SkinnedRenderer> m_skinnedRenderer;
    std::optional<engine::Shader>        m_outlineShader;
    std::optional<engine::Shader>        m_skinnedOutlineShader;
    std::optional<engine::PbrRenderer>   m_pbrRenderer;
    std::optional<engine::PostProcess>   m_postProcess;
    std::optional<engine::ProceduralSky> m_sky;
    std::optional<engine::IBL>           m_ibl;
    std::optional<engine::SSAO>          m_ssao;
    std::optional<engine::SSR>           m_ssr;
    std::optional<engine::TextRenderer>  m_text;
    engine::ImGuiLayer                   m_imgui;
    material_maker::MaterialMakerPanel   m_materialMaker;

    EditorMode       m_mode = EditorMode::Edit;
    std::optional<EditorScene::Snapshot> m_editSnapshot;
    engine::RuntimeAssetManager m_editAssets;
    engine::PhysicsWorld m_playPhysics;
    std::optional<engine::ecs::Registry> m_playRegistry;
    std::optional<engine::RuntimeAssetManager> m_playAssets;
    std::optional<engine::PlayerController> m_playPlayerController;
    engine::ecs::Entity m_playPlayerEntity = engine::ecs::kNull;
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
    bool m_showPhysicsEventGuides = true;
    bool m_physicsEventGuidesSelectedOnly = false;
    bool m_physicsEventGuidesTriggersOnly = false;
    bool m_physicsEventGuidesEnterExitOnly = false;
    std::unordered_map<engine::ecs::Entity, std::string> m_playEntityNames;
    std::unordered_map<engine::ecs::Entity, PlayTriggerAction> m_playTriggerActions;

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
