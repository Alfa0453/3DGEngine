#include "EditorApp.h"

#include <engine/graphics/Primitives.h>

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cstdio>
#include <filesystem>

using engine::ecs::Entity;
using engine::ecs::MeshRenderer;
using engine::ecs::Transform;

namespace {

engine::WindowProps MakeEditorWindowProps(const engine::Config& config) {
    engine::WindowProps props;
    props.title = "3DG Editor";
    props.width = config.GetInt("window.width", 1440);
    props.height = config.GetInt("window.height", 900);
    props.vsync = config.GetBool("window.vsync", true);
    return props;
}

glm::vec3 SelectedColor(bool selected, const glm::vec3& base) {
    return selected ? glm::vec3(1.0f, 0.78f, 0.22f) : base;
}

} // namespace

EditorApp::EditorApp(engine::Config &config)
    : engine::Application(MakeEditorWindowProps(config)), m_config(config)
{
}

void EditorApp::OnInit()
{
    m_renderer.Init();
    m_renderer.SetClearColor({0.08f, 0.09f, 0.11f, 1.0f});

    m_cube.emplace(engine::primitives::Cube());
    m_plane.emplace(engine::primitives::Plane(1.0f, 8.0f));
    m_shader.emplace(
        R"glsl(
            #version 330 core
            layout(location = 0) in vec3 aPos;
            layout(location = 1) in vec3 aNormal;

            uniform mat4 uViewProj;
            uniform mat4 uModel;

            out vec3 vNormal;
            out vec3 vWorldPos;

            void main() {
                vec4 world = uModel * vec4(aPos, 1.0);
                vWorldPos = world.xyz;
                vNormal = mat3(transpose(inverse(uModel))) * aNormal;
                gl_Position = uViewProj * world;
            }
        )glsl",
        R"glsl(
            #version 330 core
            in vec3 vNormal;
            in vec3 vWorldPos;

            uniform vec3 uColor;
            uniform vec3 uLightDir;

            out vec4 FragColor;

            void main() {
                vec3 normal = normalize(vNormal);
                float diffuse = max(dot(normal, normalize(-uLightDir)), 0.0);
                vec3 ambient = uColor * 0.24;
                vec3 lit = ambient + uColor * diffuse * 0.76;
                FragColor = vec4(lit, 1.0);
            }
        )glsl"
    );
    m_text.emplace();

    m_project.Load(m_config);
    RefreshAssets();
    m_scene.BuildDefault(*m_cube, *m_plane);
}

void EditorApp::OnUpdate(float dt)
{
    engine::Window& window = GetWindow();
    m_elapsed += dt;
    if (dt > 0.0f) {
        m_fps = m_fps * 0.92f + (1.0f / dt) * 0.08f;
    }

    if (Pressed(GLFW_KEY_ESCAPE)) {
        window.SetShouldClose(true);
    }
    if (Pressed(GLFW_KEY_F11)) {
    window.ToggleFullscreen();
    }
    if (Pressed(GLFW_KEY_TAB)) {
        m_scene.SelectNext();
    }
    if (Pressed(GLFW_KEY_BACKSPACE)) {
        m_scene.SelectPrevious();
    }
    if (Pressed(GLFW_KEY_P)) {
        if (m_mode == EditorMode::Edit) {
            EnterPlayMode();
        } else {
            ExitPlayMode();
        }
    }
    if (Pressed(GLFW_KEY_M)) {
        m_mouseLook = !m_mouseLook;
        window.SetCursorCaptured(m_mouseLook);
    }
    if (Pressed(GLFW_KEY_F5)) {
        SaveScene();
    }
    if (Pressed(GLFW_KEY_F9)) {
        LoadScene();
    }
    if (Pressed(GLFW_KEY_F6)) {
        RefreshAssets();
    }
    if (Pressed(GLFW_KEY_LEFT_BRACKET)) {
    m_assets.SelectPrevious();
    }
    if (Pressed(GLFW_KEY_RIGHT_BRACKET)) {
        m_assets.SelectNext();
    }
    if (Pressed(GLFW_KEY_ENTER)) {
        UseSelectedAsset();
    }
    if ((window.IsKeyPressed(GLFW_KEY_LEFT_CONTROL) || window.IsKeyPressed(GLFW_KEY_RIGHT_CONTROL))
    && Pressed(GLFW_KEY_Z)) {
        Undo();
    }
    if ((window.IsKeyPressed(GLFW_KEY_LEFT_CONTROL) || window.IsKeyPressed(GLFW_KEY_RIGHT_CONTROL))
        && Pressed(GLFW_KEY_Y)) {
        Redo();
    }
    if ((window.IsKeyPressed(GLFW_KEY_LEFT_CONTROL) || window.IsKeyPressed(GLFW_KEY_RIGHT_CONTROL))
        && Pressed(GLFW_KEY_L)) {
        m_log.Clear();
    }
    if (m_mode == EditorMode::Edit && Pressed(GLFW_KEY_N)) {
        AddCube();
    }
    if (m_mode == EditorMode::Edit && Pressed(GLFW_KEY_B)) {
    AddPlane();
    }
    if (m_mode == EditorMode::Edit && Pressed(GLFW_KEY_C)) {
        CycleSelectedColor();
    }
    if (m_mode == EditorMode::Edit && Pressed(GLFW_KEY_1)) {
        SetSelectedPrimitive(EditorScene::Primitive::Cube);
    }
    if (m_mode == EditorMode::Edit && Pressed(GLFW_KEY_2)) {
        SetSelectedPrimitive(EditorScene::Primitive::Plane);
    }
    if (m_mode == EditorMode::Edit && !m_mouseLook && Pressed(GLFW_KEY_D)) {
        DuplicateSelected();
    }
    if (m_mode == EditorMode::Edit && Pressed(GLFW_KEY_DELETE)) {
        DeleteSelected();
    }
    if (m_mode == EditorMode::Edit && Pressed(GLFW_KEY_R)) {
        m_scene.ResetSelectedTransform();
        m_log.Info("Reset selected transform");
    }

    const float cameraSpeed = (window.IsKeyPressed(GLFW_KEY_LEFT_SHIFT) ? 12.0f : 5.0f) * dt;
    if (m_mouseLook) {
        m_camera.AddYawPitch(window.MouseDeltaX() * 0.1f, -window.MouseDeltaY() * 0.1f);
        if (window.IsKeyPressed(GLFW_KEY_W)) m_camera.MoveForward(cameraSpeed);
        if (window.IsKeyPressed(GLFW_KEY_S)) m_camera.MoveForward(-cameraSpeed);
        if (window.IsKeyPressed(GLFW_KEY_D)) m_camera.MoveRight(cameraSpeed);
        if (window.IsKeyPressed(GLFW_KEY_A)) m_camera.MoveRight(-cameraSpeed);
        if (window.IsKeyPressed(GLFW_KEY_SPACE)) m_camera.MoveUp(cameraSpeed);
        if (window.IsKeyPressed(GLFW_KEY_LEFT_CONTROL)) m_camera.MoveUp(-cameraSpeed);
    }

    const float objectSpeed = 2.0f * dt;
    if (m_mode == EditorMode::Edit) {
        const bool transformEditing = IsTransformEditActive(window);
        if (transformEditing && !m_wasTransformEditing) {
            m_scene.BeginTransformEdit();
        }
        
        if (window.IsKeyPressed(GLFW_KEY_LEFT)) m_scene.MoveSelected(glm::vec3(-objectSpeed, 0.0f, 0.0f));
        if (window.IsKeyPressed(GLFW_KEY_RIGHT)) m_scene.MoveSelected(glm::vec3(objectSpeed, 0.0f, 0.0f));
        if (window.IsKeyPressed(GLFW_KEY_UP)) m_scene.MoveSelected(glm::vec3(0.0f, 0.0f, -objectSpeed));
        if (window.IsKeyPressed(GLFW_KEY_DOWN)) m_scene.MoveSelected(glm::vec3(0.0f, 0.0f, objectSpeed));
        if (window.IsKeyPressed(GLFW_KEY_Q)) m_scene.MoveSelected(glm::vec3(0.0f, objectSpeed, 0.0f));
        if (window.IsKeyPressed(GLFW_KEY_E)) m_scene.MoveSelected(glm::vec3(0.0f, -objectSpeed, 0.0f));
        if (window.IsKeyPressed(GLFW_KEY_J)) m_scene.RotateSelectedYaw(-90.0f * dt);
        if (window.IsKeyPressed(GLFW_KEY_L)) m_scene.RotateSelectedYaw(90.0f * dt);
        if (window.IsKeyPressed(GLFW_KEY_EQUAL) || window.IsKeyPressed(GLFW_KEY_KP_ADD)) {
            m_scene.ScaleSelected(1.0f + dt);
        }
        if (window.IsKeyPressed(GLFW_KEY_MINUS) || window.IsKeyPressed(GLFW_KEY_KP_SUBTRACT)) {
            m_scene.ScaleSelected(1.0f - dt);
        }

        if (!transformEditing && m_wasTransformEditing) {
            m_scene.EndTransformEdit();
        }
        m_wasTransformEditing = transformEditing;
    } else if (m_wasTransformEditing) {
        m_scene.EndTransformEdit();
        m_wasTransformEditing = false;
    }
}

void EditorApp::OnRender()
{
    m_renderer.Clear();

    const engine::Window& window = GetWindow();
    const glm::mat4 viewProj = m_camera.ProjectionMatrix(window.AspectRatio()) * m_camera.ViewMatrix();

    m_shader->Bind();
    m_shader->SetMat4("uViewProj", viewProj);
    m_shader->SetVec3("uLightDir", glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f)));

    m_scene.Registry().view<Transform, MeshRenderer>().each(
        [&](Entity etity, Transform& transform, MeshRenderer& renderer) {
            const EditorScene::Object* selectedObject = m_scene.SelectedObject();
            const bool selected = selectedObject && selectedObject->entity == etity;
            MeshRenderer drawRenderer = renderer;
            drawRenderer.color = SelectedColor(selected, renderer.color);
            DrawSceneObject(transform, drawRenderer);
        }
    );

    DrawEditorOverlay();
}

void EditorApp::OnShutdown()
{
    m_project.Save(m_config);
    m_config.Set("window.vsync", GetWindow().IsVSync());
    m_config.Set("window.fullscreen", GetWindow().IsFullscreen());
    m_config.Save();
}

void EditorApp::DrawSceneObject(const engine::ecs::Transform &transform, const engine::ecs::MeshRenderer &renderer)
{
    if (!renderer.mesh) {
        return;
    }

    m_shader->SetMat4("uModel", transform.Model());
    m_shader->SetVec3("uColor", renderer.color);
    m_renderer.Draw(*renderer.mesh);
}

void EditorApp::DrawEditorOverlay()
{
   const engine::Window& window = GetWindow();
    const int width = window.Width();
    const int height = window.Height();

    m_text->Begin(width, height);

    const glm::vec3 text(0.92f, 0.94f, 0.96f);
    const glm::vec3 muted(0.66f, 0.70f, 0.76f);
    const glm::vec3 accent(1.0f, 0.78f, 0.22f);

    char line[160];
    std::snprintf(line, sizeof(line), "3DG EDITOR  %s%s  %.0f fps",
        m_mode == EditorMode::Edit ? "EDIT" : "PLAY",
        m_scene.IsDirty() ? " *" : "",
        m_fps);
    m_text->Text(line, 24.0f, 22.0f, 1.8f, text);
    std::snprintf(line, sizeof(line), "%s  scene: %s",
        m_project.ProjectName().c_str(), m_project.ScenePath().c_str());
    m_text->Text(line, 24.0f, 52.0f, 1.15f, muted);

    m_text->Text("Hierarchy", 24.0f, 70.0f, 1.45f, text);
    float y = 102.0f;
    const std::vector<EditorScene::Object>& objects = m_scene.Objects();
    for (int i = 0; i < static_cast<int>(objects.size()); ++i) {
        const EditorScene::Object& object = objects[static_cast<std::size_t>(i)];
        std::snprintf(line, sizeof(line), "%s %s",
            i == m_scene.SelectedIndex() ? ">" : " ", object.name.c_str());
        m_text->Text(line, 30.0f, y, 1.25f, i == m_scene.SelectedIndex() ? accent : muted);
        y += 26.0f;
    }
    DrawAssetOverlay(24.0f, y + 28.0f, text, muted);

    m_text->Text("Inspector", static_cast<float>(width) - 330.0f, 70.0f, 1.45f, text);
    if (const EditorScene::Object* selected = m_scene.SelectedObject()) {
        const Transform* transform = m_scene.Registry().TryGet<Transform>(selected->entity);
        const MeshRenderer* renderer = m_scene.Registry().TryGet<MeshRenderer>(selected->entity);
        std::snprintf(line, sizeof(line), "Name: %s", selected->name.c_str());
        m_text->Text(line, static_cast<float>(width) - 330.0f, 106.0f, 1.2f, muted);
        std::snprintf(line, sizeof(line), "Type: %s",
            selected->primitive == EditorScene::Primitive::Plane ? "Plane" : "Cube");
        m_text->Text(line, static_cast<float>(width) - 330.0f, 134.0f, 1.2f, muted);
        if (transform) {
            std::snprintf(line, sizeof(line), "Position: %.2f, %.2f, %.2f",
                transform->position.x, transform->position.y, transform->position.z);
            m_text->Text(line, static_cast<float>(width) - 330.0f, 162.0f, 1.2f, muted);
            std::snprintf(line, sizeof(line), "Scale: %.2f, %.2f, %.2f",
                transform->scale.x, transform->scale.y, transform->scale.z);
            m_text->Text(line, static_cast<float>(width) - 330.0f, 190.0f, 1.2f, muted);
            std::snprintf(line, sizeof(line), "Rotation: %.2f, %.2f, %.2f, %.2f",
                transform->rotation.w, transform->rotation.x, transform->rotation.y, transform->rotation.z);
            m_text->Text(line, static_cast<float>(width) - 330.0f, 218.0f, 1.2f, muted);
        }
        if (renderer) {
            std::snprintf(line, sizeof(line), "Color: %.2f, %.2f, %.2f",
                renderer->color.r, renderer->color.g, renderer->color.b);
            m_text->Text(line, static_cast<float>(width) - 330.0f, 246.0f, 1.2f, muted);
        }
    }

    DrawLogOverlay(static_cast<float>(width) - 330.0f, 292.0f, text, muted);

    std::snprintf(line, sizeof(line), "Status: %s", m_log.LatestMessage().c_str());
    m_text->Text(line, 24.0f, static_cast<float>(height) - 62.0f, 1.2f, accent);
    m_text->Text("P play/edit   N cube   B plane   C color   [/] assets   Enter use asset   Ctrl+Z/Y undo/redo   F5 save   F6 assets",
        24.0f, static_cast<float>(height) - 34.0f, 1.2f, muted);

    m_text->End();
}

void EditorApp::DrawAssetOverlay(float x, float y, const glm::vec3 & text, const glm::vec3 & muted)
{
    m_text->Text("Assets", x, y, 1.45f, text);

    char line [180];
    std::snprintf(line, sizeof(line), "%s   (%zu files)",
        m_assets.RootPath().c_str(), m_assets.Assets().size());
    m_text->Text(line, x + 6.0f, y + 32.0f, 1.05f, muted);

    const std::vector<EditorAssets::Asset>& assets = m_assets.Assets();
    const int maxVisible = 8;
    for (int i = 0; i < static_cast<int>(assets.size()) && i < maxVisible; ++i) {
        const EditorAssets::Asset& asset = assets[static_cast<std::size_t>(i)];
        std::snprintf(line, sizeof(line), "%s  %s",
            EditorAssets::TypeName(asset.type), asset.relativePath.c_str());
        m_text->Text(line, x + 6.0f, y + 60.0f + static_cast<float>(i) * 22.0f, 1.05f, muted);
    }

    if (const EditorAssets::Asset* selected = m_assets.SelectedAsset()) {
        std::snprintf(line, sizeof(line), "Selected: %s", selected->displayName.c_str());
        m_text->Text(line, x + 6.0f, y + 248.0f, 1.05f, text);
        std::snprintf(line, sizeof(line), "Type: %s", EditorAssets::TypeName(selected->type));
        m_text->Text(line, x + 6.0f, y + 272.0f, 1.05f, muted);
    }
}

void EditorApp::DrawLogOverlay(float x, float y, const glm::vec3 & text, const glm::vec3 & muted)
{
    m_text->Text("Console", x, y, 1.45f, text);

    const std::vector<EditorLog::Entry>& entries = m_log.Entries();
    const int maxVisible = 7;
    const int first = static_cast<int>(entries.size()) > maxVisible
        ? static_cast<int>(entries.size()) - maxVisible
        : 0;

    char line[180];
    float rowY = y + 32.0f;
    for (int i = first; i < static_cast<int>(entries.size()); ++i) {
        const EditorLog::Entry& entry = entries[static_cast<std::size_t>(i)];
        std::snprintf(line, sizeof(line), "%s  %s",
            EditorLog::LevelName(entry.level), entry.message.c_str());
        m_text->Text(line, x + 6.0f, rowY, 1.05f, muted);
        rowY += 22.0f;
    }
}

void EditorApp::RefreshAssets()
{
    std::string error;
    if (m_assets.Refresh(m_project.AssetRoot(), &error)) {
        m_log.Info("Scanned assets: " + std::to_string(m_assets.Assets().size()) + "files");
    } else {
        m_log.Error(error);
    }
}

void EditorApp::UseSelectedAsset()
{
    const EditorAssets::Asset* asset = m_assets.SelectedAsset();
    if (!asset) {
        m_log.Warning("No asset selected");
        return;
    }

    if (asset->type != EditorAssets::Type::Scene) {
        m_log.Warning("Selected asset is not a scene");
        return;
    }

    m_project.SetScenePath(AssetFullPath(*asset));
    LoadScene();
}

std::string EditorApp::AssetFullPath(const EditorAssets::Asset & asset) const
{
    return (std::filesystem::path(m_assets.RootPath()) / asset.relativePath).string();
}

void EditorApp::AddCube()
{
    if (!m_cube) {
        m_log.Error("Add failed: cube mesh is not ready");
        return;
    }

    m_scene.AddCube(*m_cube);
    m_log.Info("Added cube");
}

void EditorApp::AddPlane()
{
    if (!m_plane) {
        m_log.Error("Add failed: plane mesh is not ready");
        return;
    }

    m_scene.AddPlane(*m_plane);
    m_log.Info("Added plane");
}

void EditorApp::CycleSelectedColor()
{
    if (m_scene.CycleSelectedColor()) {
        m_log.Info("Changed selected color");
    } else {
        m_log.Warning("Color change failed: no selected object");
    }
}

void EditorApp::SetSelectedPrimitive(EditorScene::Primitive primitive)
{
    if (!m_cube || !m_plane) {
        m_log.Error("Type change failed: editor meshes are not ready");
        return;
    }

    const engine::Mesh& mesh = primitive == EditorScene::Primitive::Cube ? *m_cube : *m_plane;
    if (m_scene.SetSelectedPrimitive(primitive, mesh)) {
        m_log.Info(primitive == EditorScene::Primitive::Cube
            ? "Changed selected type to cube"
            : "Changed selected type to plane");
    } else {
        m_log.Warning("Type change skipped");
    }
}

void EditorApp::DuplicateSelected()
{
    if (!m_cube || !m_plane) {
        m_log.Error("Duplicate failed: editor meshes are not ready");
        return;
    }

    if (m_scene.DuplicateSelected(*m_cube, *m_plane)) {
        m_log.Info("Duplicated selected object");
    } else {
        m_log.Warning("Duplicate failed: no selected object");
    }
}

void EditorApp::DeleteSelected()
{
    if (m_scene.DeleteSelected()) {
        m_log.Info("Deleted selected object");
    } else {
        m_log.Warning("Delete failed: no selected object");
    }
}

void EditorApp::Undo()
{
    if (!m_cube || !m_plane) {
        m_log.Error("Undo failed: editor meshes are not ready");
        return;
    }

    if (m_scene.Undo(*m_cube, *m_plane)) {
        m_log.Info("Undo");
    } else {
        m_log.Warning("Nothing to undo");
    }
}

void EditorApp::Redo()
{
    if (!m_cube || !m_plane) {
    m_log.Error("Redo failed: editor meshes are not ready");
    return;
    }

    if (m_scene.Redo(*m_cube, *m_plane)) {
        m_log.Info("Redo");
    } else {
        m_log.Warning("Nothing to redo");
    }
}

void EditorApp::SaveScene()
{
    std::string error;
    if (m_scene.Save(m_project.ScenePath(), &error)) {
        m_log.Info("Saved " + m_project.ScenePath());
    } else {
        m_log.Error("Save failed: " + error);
    }
}

void EditorApp::LoadScene()
{
    if (!m_cube || !m_plane) {
        m_log.Error("Load failed: editor meshes are not ready");
        return;
    }

    std::string error;
    if (m_scene.Load(m_project.ScenePath(), *m_cube, *m_plane, &error)) {
        m_log.Info("Loaded " + m_project.ScenePath()); 
    } else {
        m_log.Error("Load failed: " + error);
    }
}

void EditorApp::EnterPlayMode()
{
    m_editSnapshot = m_scene.CreateSnapshot();
    m_mode = EditorMode::Play;
    m_log.Info("Play mode: edit scene snapshot captured");
}

void EditorApp::ExitPlayMode()
{
    if (!m_cube || !m_plane) {
        m_log.Error("Could not restore edit scene");
        m_mode = EditorMode::Edit;
        m_editSnapshot.reset();
        return;
    }

    if (m_editSnapshot) {
        m_scene.RestoreFromSnapshot(*m_editSnapshot, *m_cube, *m_plane);
    }
    m_editSnapshot.reset();
    m_mode = EditorMode::Edit;
    m_log.Info("Edit mode: restored scene from before Play");
}

bool EditorApp::IsTransformEditActive(const engine::Window &window) const
{
    return window.IsKeyPressed(GLFW_KEY_LEFT)
        || window.IsKeyPressed(GLFW_KEY_RIGHT)
        || window.IsKeyPressed(GLFW_KEY_UP)
        || window.IsKeyPressed(GLFW_KEY_DOWN)
        || window.IsKeyPressed(GLFW_KEY_Q)
        || window.IsKeyPressed(GLFW_KEY_E)
        || window.IsKeyPressed(GLFW_KEY_J)
        || window.IsKeyPressed(GLFW_KEY_L)
        || window.IsKeyPressed(GLFW_KEY_EQUAL)
        || window.IsKeyPressed(GLFW_KEY_KP_ADD)
        || window.IsKeyPressed(GLFW_KEY_MINUS)
        || window.IsKeyPressed(GLFW_KEY_KP_SUBTRACT);
}

bool EditorApp::Pressed(int key)
{
    const bool down = GetWindow().IsKeyPressed(key);
    const bool was = m_keyPrev[key];
    m_keyPrev[key] = down;
    return down && !was;
}
