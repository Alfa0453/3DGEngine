#pragma once

#include <engine/core/Application.h>
#include <engine/core/Config.h>
#include <engine/ecs/Components.h>
#include <engine/graphics/Camera.h>
#include <engine/graphics/Mesh.h>
#include <engine/graphics/Renderer.h>
#include <engine/graphics/Shader.h>
#include <engine/graphics/TextRenderer.h>
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
#include <engine/scene/RuntimeSceneLoader.h>

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
                         const engine::ecs::MeshRenderer& renderer);
    void DrawEditorOverlay();
    void DrawAssetOverlay(float x, float y, const glm::vec3& text, const glm::vec3& muted);
    void DrawLogOverlay(float x, float y, const glm::vec3& text, const glm::vec3& muted);
    void ApplyGizmoNudge(float direction, float dt);
    void ApplyGizmoDrag(float pixels);
    void TogglePanel(EditorPanels::Panel panel);
    void HandleMouseAssetDrag();
    void HandleMouseVIewportGizmo();
    void BeginAssetDrag();
    void DropPayloadOnScene();
    void RefreshAssets();
    void UseSelectedAsset();
    std::string AssetFullPath(const EditorAssets::Asset& asset) const;
    float AssetPanelTop() const;
    int AssetIndexAtPosition(float x, float y) const;
    bool IsViewpoertDropPosition(float x, float y);
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
    std::optional<engine::Mesh>         m_plane;
    std::optional<engine::Shader>       m_shader;
    std::optional<engine::TextRenderer> m_text;
    engine::ImGuiLayer                  m_imgui;

    EditorMode       m_mode = EditorMode::Edit;
    std::optional<EditorScene::Snapshot> m_editSnapshot;

    bool m_mouseLook = false;
    bool m_leftMousePrev = false;
    bool m_rightMousePrev = false;
    bool m_mouseGizmoActive = false;
    float m_mouseGizmoLastX = 0.0f;
    float m_mouseGizmoLastY = 0.0f;
    float m_fps = 60.0f;
    float m_elapsed = 0.0f;
    bool m_wasTransformEditing = false;
    std::unordered_map<int, bool> m_keyPrev;
};