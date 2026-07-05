#pragma once

#include <engine/core/Application.h>
#include <engine/core/Config.h>
#include <engine/assets/RuntimeAssetManager.h>
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
#include <engine/graphics/SSAO.h>
#include <engine/graphics/SSR.h>
#include <engine/graphics/TextRenderer.h>
#include <engine/physics/PhysicsComponents.h>
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

#include <optional>
#include <string>
#include <unordered_map>

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

    void DrawEditModeModels(const glm::mat4& viewProj);
    void DrawSelectionOutline(const glm::mat4& viewProj);
    void DrawEditorOverlay();
    void DrawMaterialMakerPanel();
    void DrawMaterialMakerTools(bool materialSaved);
    void DrawDirtyScenePrompt();
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
    void AddCube();
    void AddPlane();
    void AddSphere();
    void AddDynamicCube();
    void AddStaticFloor();
    void AddTriggerVolume();
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
    void EnterPlayMode();
    void ExitPlayMode();
    bool BuildPlayRuntimePreview(std::string* error);
    void StepPlayPhysics(float dt);
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
    std::optional<engine::Shader>        m_outlineShader;
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
    bool m_physicsPaused = false;
    bool m_physicsStepRequested = false;
    float m_physicsFixedTimestep = 1.0f / 60.0f;
    float m_physicsAccumulator = 0.0f;
    int m_physicsStepsLastFrame = 0;
    int m_physicsEventEnterCount = 0;
    int m_physicsEventStayCount = 0;
    int m_physicsEventExitCount = 0;
    std::vector<EditorDockspace::PhysicsEventRow> m_physicsEventRows;
    std::vector<EditorViewport::PhysicsEventGuide> m_physicsEventGuides;
    bool m_showPhysicsEventGuides = true;
    bool m_physicsEventGuidesSelectedOnly = false;
    bool m_physicsEventGuidesTriggersOnly = false;
    bool m_physicsEventGuidesEnterExitOnly = false;
    std::unordered_map<engine::ecs::Entity, std::string> m_playEntityNames;

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
    std::unordered_map<std::string, bool> m_editModelLoadErrors;
    std::unordered_map<std::string, bool> m_editTextureLoadErrors;
    std::unordered_map<std::string, bool> m_editMaterialLoadErrors;
};