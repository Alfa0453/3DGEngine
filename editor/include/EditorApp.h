#pragma once

#include <engine/core/Application.h>
#include <engine/core/Config.h>
#include <engine/assets/RuntimeAssetManager.h>
#include <engine/ecs/Components.h>
#include <engine/ecs/Registry.h>
#include <engine/graphics/Camera.h>
#include <engine/graphics/Mesh.h>
#include <engine/graphics/Model.h>
#include <engine/graphics/Mesh.h>
#include <engine/graphics/Renderer.h>
#include <engine/graphics/Shader.h>
#include <engine/graphics/TextRenderer.h>
#include <engine/graphics/Texture.h>
#include <engine/scene/RuntimeSceneLoader.h>
#include <engine/ui/ImGuiLayer.h>

#include "EditorAssets.h"
#include "EditorDockspace.h"
#include "EditorDragDrop.h"
#include "EditorGizmo.h"
#include "EditorLog.h"
#include "EditorPanels.h"
#include "EditorProject.h"
#include "EditorScene.h"
#include "RuntimeSceneExporter.h"

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
    enum class EditorMode { Edit, Play };

    void DrawSceneObject(const engine::ecs::Transform& transform, 
                         const engine::ecs::MeshRenderer& renderer,
                         const engine::Texture* diffuseTexture);
    void DrawEditModeModels(const glm::mat4& viewProj);
    void DrawSelectionOutline(const glm::mat4& viewProj);
    void DrawSelectedModelOutline(const engine::ecs::Transform& transform,
                              const engine::Model& model,
                              const glm::vec3& color,
                              float thickness);
    void DrawSelectedMeshOutline(const engine::ecs::Transform& transform,
                             const engine::Mesh& mesh,
                             const glm::vec3& color,
                             float thickness);
    void DrawSceneGizmo(const glm::mat4& viewProj);
    void DrawGizmoBox(const glm::vec3& position, const glm::vec3& scale, const glm::vec3& color);
    void DrawGizmoCone(const glm::vec3& position, EditorGizmo::Axis axis, const glm::vec3& color);
    void DrawGizmoRing(const glm::vec3& center, EditorGizmo::Axis axis, const glm::vec3& color);
    void DrawEditorOverlay();
    void DrawAssetOverlay(float x, float y, const glm::vec3& text, const glm::vec3& muted);
    void HandleGlobalShortcuts(engine::Window& window);
    void UpdateMouseCapture(engine::Window& window);
    void HandleAssetShortcuts(engine::Window& window, bool controlDown);
    void HandleEditorCommandShortcuts(engine::Window& window, bool controlDown);
    void UpdateCameraControls(engine::Window& window, float dt);
    void UpdateSelectedTransformShortcuts(engine::Window& window, float dt);
    void DrawPlayScene(const glm::mat4& viewProj);
    void DrawEditScene(const glm::mat4& viewProj);
    void DrawLogOverlay(float x, float y, const glm::vec3& text, const glm::vec3& muted);
    void ApplyGizmoNudge(float direction, float dt);
    void ApplyGizmoDrag(float pixels);
    void TogglePanel(EditorPanels::Panel panel);
    void HandleMouseAssetDrag();
    void HandleMouseViewportSelection();
    void HandleMouseViewportGizmo();
    bool ProjectWorldToScreen(const glm::vec3& world, const glm::mat4 viewProj, 
                              int width, int height, glm::vec2* screen) const;
    int PickSceneObject(float x, float y, const glm::mat4& viewProj, int width, int height) const;
    bool PickGizmoHandle(float x, float y, const glm::mat4& viewProj, int width, int height);
    void BeginAssetDrag();
    void DropPayloadOnScene();
    glm::vec3 SceneDropPosition();
    void RefreshAssets();
    void CtreateContentFolder(const std::string& name);
    void ImportContentAsset(const std::string& sourcePath);
    void CopyContentEntry();
    void PasteContentEntry();
    void DeleteContentEntry();
    void UseSelectedAsset();
    std::string AssetFullPath(const EditorAssets::Asset& asset) const;
    float AssetPanelTop() const;
    int FolderIndexAtPosition(float x, float y) const;
    int AssetIndexAtPosition(float x, float y) const;
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
    bool IsTransformEditActive(const engine::Window& window) const;

    engine::Config&       m_config;
    engine::Renderer      m_renderer;
    engine::Camera        m_camera{ glm::vec3(0.0f, 3.0f, 8.0f) };
    EditorAssets          m_assets;
    EditorDockspace       m_dockspace;
    EditorDragDrop        m_dragDrop;
    EditorGizmo           m_gizmo;
    EditorLog             m_log;
    EditorPanels          m_panels;
    EditorProject         m_project;
    EditorScene           m_scene;

    std::optional<engine::Mesh>         m_cube;
    std::optional<engine::Mesh>         m_cone;
    std::optional<engine::Mesh>         m_plane;
    std::optional<engine::Shader>       m_shader;
    std::optional<engine::Shader>       m_modelShader;
    std::optional<engine::Shader>       m_outlineShader;
    std::optional<engine::TextRenderer> m_text;
    engine::ImGuiLayer                  m_imgui;

    EditorMode       m_mode = EditorMode::Edit;
    std::optional<EditorScene::Snapshot> m_editSnapshot;
    engine::RuntimeAssetManager m_editAssets;
    std::optional<engine::ecs::Registry> m_playRegistry;
    std::optional<engine::RuntimeAssetManager> m_playAssets;

    bool m_mouseLook = false;
    bool m_mouseLookPinned = false;
    bool m_rightMouseLookActive = false;
    bool m_rightMouseLookPrev = false;
    bool m_middleMousePanActive = false;
    bool m_middleMousePanPrev = false;
    bool m_leftMousePrev = false;
    bool m_viewportLeftMousePrev = false;
    bool m_rightMousePrev = false;
    bool m_mouseGizmoActive = false;
    int m_mouseGizmoButton = -1;
    float m_mouseGizmoLastX = 0.0f;
    float m_mouseGizmoLastY = 0.0f;
    float m_fps = 60.0f;
    float m_elapsed = 0.0f;
    bool m_wasTransformEditing = false;
    std::unordered_map<int, bool> m_keyPrev;
    std::unordered_map<std::string, bool> m_editModelLoadErrors;
    std::unordered_map<std::string, bool> m_editTextureLoadErrors;
};