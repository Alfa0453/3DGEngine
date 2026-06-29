#pragma once

#include <engine/core/Application.h>
#include <engine/core/Config.h>
#include <engine/ecs/Components.h>
#include <engine/graphics/Camera.h>
#include <engine/graphics/Mesh.h>
#include <engine/graphics/Renderer.h>
#include <engine/graphics/Shader.h>
#include <engine/graphics/TextRenderer.h>

#include "EditorAssets.h"
#include "EditorProject.h"
#include "EditorScene.h"

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
    void RefreshAssets();
    void UseSelectedAsset();
    std::string AssetFullPath(const EditorAssets::Asset& asset) const;
    void AddCube();
    void AddPlane();
    void CycleSelectedColor();
    void SetSelectedPrimitive(EditorScene::Primitive primitive);
    void DuplicateSelected();
    void DeleteSelected();
    void Undo();
    void Redo();
    void SaveScene();
    void LoadScene();
    void EnterPlayMode();
    void ExitPlayMode();
    bool Pressed(int key);
    bool IsTransformEditActive(const engine::Window& window) const;

    engine::Config&       m_config;
    engine::Renderer      m_renderer;
    engine::Camera        m_camera{ glm::vec3(0.0f, 3.0f, 8.0f) };
    EditorAssets m_assets;
    EditorProject m_project;
    EditorScene m_scene;

    std::optional<engine::Mesh>         m_cube;
    std::optional<engine::Mesh>         m_plane;
    std::optional<engine::Shader>       m_shader;
    std::optional<engine::TextRenderer> m_text;

    std::string m_status    = "Ready";
    EditorMode       m_mode = EditorMode::Edit;
    std::optional<EditorScene::Snapshot> m_editSnapshot;

    bool m_mouseLook = false;
    float m_fps = 60.0f;
    float m_elapsed = 0.0f;
    bool m_wasTransformEditing = false;
    std::unordered_map<int, bool> m_keyPrev;
};