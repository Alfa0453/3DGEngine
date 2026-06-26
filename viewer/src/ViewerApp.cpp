#include "ViewerApp.h"

#include <engine/core/Paths.h>

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

namespace {

engine::WindowProps MakeProps(const engine::Config& cfg) {
    engine::WindowProps p;
    p.title  = "Model Viewer";
    p.width  = cfg.GetInt("window.width", 1280);
    p.height = cfg.GetInt("window.height", 720);
    p.vsync  = cfg.GetBool("window.vsync", true);
    return p;
}

} // namespace

ViewerApp::ViewerApp(engine::Config &config)
    : engine::Application(MakeProps(config)), m_config(config)
{
}

void ViewerApp::OnInit()
{
    m_renderer.Init();
    m_renderer.SetClearColor({0.10f, 0.11f, 0.13f, 1.0f});

    m_modelName = m_config.GetString("model", "models/house.obj");
    m_model.emplace(engine::Model::FromFile(Asset(m_modelName)));
    m_shader.emplace(engine::Shader::FromFiles(
        Asset("shaders/model.vert"), Asset("shaders/model.frag")
    ));
    m_text.emplace();

    // Frame the model: orbit its centre at a distance scaled to its size.
    m_target = m_model->Center();
    m_dist   = std::max(m_model->BoundingRadius() * 3.0f, 3.0f);

    if (m_config.GetBool("window.fullscreen", false))
        GetWindow().ToggleFullscreen();
}

void ViewerApp::OnShutdown()
{
    m_config.Set("window.vysync",     GetWindow().IsVSync());
    m_config.Set("window.fullscreen", GetWindow().IsFullscreen());
    m_config.Save();
}

void ViewerApp::OnUpdate(float dt)
{
    engine::Window& w = GetWindow();
    if (Pressed(GLFW_KEY_ESCAPE)) w.SetShouldClose(true);
    if (Pressed(GLFW_KEY_F11))    w.ToggleFullscreen();
    if (Pressed(GLFW_KEY_SPACE))  m_autoSpin = !m_autoSpin;
    if (Pressed(GLFW_KEY_R)) {  // reset the view
        m_yaw = 0.7f; m_pitch = 0.35f;
        m_dist = std::max(m_model->BoundingRadius() * 3.0f, 3.0f);
        m_autoSpin = true;
    }

    const float rot = 1.6f * dt, zoom = 6.0f * dt;
    bool manual = false;
    if (w.IsKeyPressed(GLFW_KEY_LEFT)  || w.IsKeyPressed(GLFW_KEY_A)) { m_yaw -= rot; manual = true; }
    if (w.IsKeyPressed(GLFW_KEY_RIGHT) || w.IsKeyPressed(GLFW_KEY_D)) { m_yaw += rot; manual = true; }
    if (w.IsKeyPressed(GLFW_KEY_UP)    || w.IsKeyPressed(GLFW_KEY_W)) m_pitch += rot;
    if (w.IsKeyPressed(GLFW_KEY_DOWN)  || w.IsKeyPressed(GLFW_KEY_S)) m_pitch -= rot;
    if (w.IsKeyPressed(GLFW_KEY_Z)) m_dist -= zoom;
    if (w.IsKeyPressed(GLFW_KEY_X)) m_dist += zoom;

    if (manual) m_autoSpin = false;
    if (m_autoSpin) m_yaw += 0.5f * dt;

    // Keep the camera sane.
    m_pitch = std::clamp(m_pitch, -1.45f, 1.45f);
    const float minD = std::max(m_model->BoundingRadius() * 1.2f, 1.0f);
    m_dist = std::clamp(m_dist, minD, m_model->BoundingRadius() * 12.0f + 20.0f);
}

void ViewerApp::OnRender()
{
    m_renderer.Clear();

    // Spherical orbit -> eye position.
    const glm::vec3 dir(std::cos(m_pitch) * std::cos(m_yaw),
                        std::sin(m_pitch), std::cos(m_pitch) * std::sin(m_yaw));
    const glm::vec3 eye  = m_target + dir * m_dist;
    const glm::mat4 view = glm::lookAt(eye, m_target, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 proj = glm::perspective(glm::radians(45.0f), 
                                            GetWindow().AspectRatio(), 0.1f, 200.0f);
    const glm::mat4 viewProj = proj * view;

    const glm::mat4 model(1.0f);    // model is already in model space
    m_shader->Bind();
    m_shader->SetMat4("uViewProj",  viewProj);
    m_shader->SetMat4("uModel",     model);
    m_shader->SetMat3("uNormalMat", glm::mat3(1.0f));
    m_shader->SetVec3("uLightPos",  m_target + glm::vec3(4.0f, 8.0f, 6.0f));
    m_shader->SetVec3("uLightColor", glm::vec3(1.0f, 0.98f, 0.95f));
    m_shader->SetVec3("uViewPos",   eye);
    m_shader->SetInt("uTexture",    0);

    engine::DrawModel(*m_model, *m_shader);

    // HUD: model stats + controls.
    engine::Window& w = GetWindow();
    const int ww = w.Width(), hh = w.Height();
    m_text->Begin(ww, hh);
    const glm::vec3 white(1.0f), grey(0.65f);
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s", m_modelName.c_str());
    m_text->Text(buf, 24.0f, 22.0f, 2.0f, white);
    std::snprintf(buf, sizeof(buf), "sub-meshes %zu   materials %zu   textures %zu",
                m_model->SubMeshCount(), m_model->Materials().size(), m_model->Textures().size());
    m_text->Text(buf, 24.0f, 52.0f, 1.4f, grey);
    m_text->Text("A/D or arrows orbit   W/S tilt   Z/X zoom   SPACE auto-spin   R reset   F11   ESC",
                24.0f, hh - 34.0f, 1.4f, grey);
    m_text->End();
}

bool ViewerApp::Pressed(int key)
{
    const bool down = GetWindow().IsKeyPressed(key);
    const bool was  = m_keyPrev[key];
    m_keyPrev[key]  = down;
    return down && !was;
}

std::string ViewerApp::Asset(const std::string &rel) const
{
    static const std::string root = [] {
        const std::string beside = engine::ExecutableDir() + "/assets";
        std::error_code ec;
        if (std::filesystem::exists(beside, ec)) return beside;
        return std::string(ASSETS_DIR);
    }();
    return root + "/" + rel;
}
