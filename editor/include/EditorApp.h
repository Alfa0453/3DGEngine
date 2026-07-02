#pragma once

#include <engine/core/Application.h>
#include <engine/core/Config.h>
#include <engine/assets/RuntimeAssetManager.h>
#include <engine/ecs/Components.h>
#include <engine/ecs/Registry.h>
#include <engine/graphics/Camera.h>
#include <engine/graphics/Mesh.h>
#include <engine/graphics/Model.h>
#include <engine/graphics/Renderer.h>
#include <engine/graphics/Shader.h>
#include <engine/graphics/TextRenderer.h>
#include <engine/scene/RuntimeSceneLoader.h>
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

    void DrawEditModeModels(const glm::mat4& viewProj);
    void DrawSelectionOutline(const glm::mat4& viewProj);
    void DrawEditorOverlay();
    void DrawMaterialMakerPanel();
    void DrawMaterialMakerTools(bool materialSaved);
    void DrawAssetOverlay(float x, float y, const glm::vec3& text, const glm::vec3& muted);
    void DrawLogOverlay(float x, float y, const glm::vec3& text, const glm::vec3& muted);
    void HandleGlobalShortcuts(engine::Window& window);
    void HandleAssetShortcuts(engine::Window& window, bool controlDown);
    void HandleEditorCommandShortcuts(engine::Window& window, bool controlDown);
    void DrawPlayScene(const glm::mat4& viewProj);
    void DrawEditScene(const glm::mat4& viewProj);
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
    void CycleSelectedColor();
    void SetSelectedPrimitive(EditorScene::Primitive primitive);
    void ToggleSelectedVisible();
    void ToggleSelectedLocked();
    void DuplicateSelected();
    void DeleteSelected();
    void Undo();
    void Redo();
    void SaveScene();
    void LoadScene();
    void ExportRuntimeScene();
    void ValidateRuntimeScene();
    void EnterPlayMode();
    void ExitPlayMode();
    bool BuildPlayRuntimePreview(std::string* error);
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

    std::optional<engine::Mesh>         m_cube;
    std::optional<engine::Mesh>         m_cone;
    std::optional<engine::Mesh>         m_plane;
    std::optional<engine::Shader>       m_shader;
    std::optional<engine::Shader>       m_modelShader;
    std::optional<engine::Shader>       m_outlineShader;
    std::optional<engine::TextRenderer> m_text;
    engine::ImGuiLayer                  m_imgui;
    material_maker::MaterialMakerPanel  m_materialMaker;

    EditorMode       m_mode = EditorMode::Edit;
    std::optional<EditorScene::Snapshot> m_editSnapshot;
    engine::RuntimeAssetManager m_editAssets;
    std::optional<engine::ecs::Registry> m_playRegistry;
    std::optional<engine::RuntimeAssetManager> m_playAssets;

    float m_fps = 60.0f;
    float m_elapsed = 0.0f;
    std::unordered_map<int, bool> m_keyPrev;
    std::unordered_map<std::string, bool> m_editModelLoadErrors;
    std::unordered_map<std::string, bool> m_editTextureLoadErrors;
    std::unordered_map<std::string, bool> m_editMaterialLoadErrors;
};