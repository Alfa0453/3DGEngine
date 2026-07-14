#include "UiDemoApp.h"

#include <engine/graphics/Primitives.h>

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstdio>

using engine::ecs::Entity;
using engine::ecs::Transform;
using engine::ecs::MeshPBR;
using engine::ecs::PbrMaterial;
using engine::ecs::Light;

namespace {
engine::WindowProps MakeProps(const engine::Config& cfg) {
    engine::WindowProps p;
    p.title  = "UI Demo — immediate-mode panels / buttons / sliders / bars";
    p.width  = cfg.GetInt("window.width", 1280);
    p.height = cfg.GetInt("window.height", 720);
    p.vsync  = cfg.GetBool("window.vsync", true);
    return p;
}
} // namespace

UiDemoApp::UiDemoApp(engine::Config& config)
    : engine::Application(MakeProps(config)), m_config(config) {}

void UiDemoApp::OnInit() {
    m_renderer.Init();
    m_cube.emplace(engine::primitives::Cube());
    m_pbr.emplace(2048);
    m_sky.emplace();
    m_post.emplace(GetWindow().Width(), GetWindow().Height());
    m_ibl.emplace(256);
    m_sample = engine::DayNightCycle::At(0.42f);
    m_ibl->Generate([&](const glm::mat4& v, const glm::mat4& p) { m_sky->Draw(v, p, m_sample, false); });
    m_ui.emplace();

    { Entity e = m_reg.Create();
      Transform t; t.scale = glm::vec3(2.0f); m_reg.Add<Transform>(e, t);
      PbrMaterial m; m.albedo = m_color; m_reg.Add<MeshPBR>(e, MeshPBR{&*m_cube, m}); m_cubeEnt = e; }
    { Entity g = m_reg.Create();
      Transform t; t.position = glm::vec3(0, -2.0f, 0); t.scale = glm::vec3(20.0f, 0.2f, 20.0f);
      m_reg.Add<Transform>(g, t);
      PbrMaterial m; m.albedo = glm::vec3(0.2f); m.roughness = 0.9f; m_reg.Add<MeshPBR>(g, MeshPBR{&*m_cube, m}); }
    { Entity s = m_reg.Create(); m_reg.Add<Transform>(s, Transform{});
      Light l; l.type = Light::Type::Directional; l.direction = glm::vec3(-0.4f, -0.9f, -0.4f);
      l.color = glm::vec3(1.0f); l.intensity = 2.8f; m_reg.Add<Light>(s, l); }

    GetWindow().SetCursorCaptured(false);
}

void UiDemoApp::OnUpdate(float dt) {
    engine::Window& w = GetWindow();
    m_dt = dt;
    if (dt > 0.0f) m_fps = m_fps * 0.92f + (1.0f / dt) * 0.08f;
    if (w.IsKeyPressed(GLFW_KEY_ESCAPE)) w.SetShouldClose(true);

    m_spin += m_spinSpeed * dt;
    Transform& t = m_reg.Get<Transform>(m_cubeEnt);
    t.rotation = glm::angleAxis(m_spin, glm::normalize(glm::vec3(0.3f, 1.0f, 0.15f)));
    MeshPBR& mp = m_reg.Get<MeshPBR>(m_cubeEnt);
    mp.material.albedo = m_color; mp.material.metallic = m_metallic; mp.material.roughness = m_rough;
}

void UiDemoApp::OnRender() {
    engine::Window& w = GetWindow();
    const float aspect = w.AspectRatio();
    m_post->Resize(w.Width(), w.Height());

    engine::Camera cam(glm::vec3(0.0f, 2.5f, 8.0f));
    cam.LookAt(glm::vec3(0.0f, 0.5f, 0.0f));

    // 3D scene.
    m_post->BeginScene();
    engine::PbrRenderer::Options opt;
    opt.ambient = m_sample.ambient + glm::vec3(0.05f);
    opt.tonemap = false; opt.ibl = &*m_ibl; opt.fog = false;
    m_pbr->Render(m_reg, cam, aspect, w.Width(), w.Height(), opt);
    m_sky->Draw(cam.ViewMatrix(), cam.ProjectionMatrix(aspect), m_sample, false);
    m_post->RenderToScreen(w.Width(), w.Height(), m_dt);

    // --- UI overlay ---------------------------------------------------------
    engine::UiInput in;
    in.mouseX = w.MouseX(); in.mouseY = w.MouseY();
    in.down = w.IsMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT);
    in.pressed  = in.down && !m_prevDown;
    in.released = !in.down && m_prevDown;
    m_prevDown = in.down;

    const int W = w.Width();
    m_ui->Begin(W, w.Height(), in);

    // Settings panel (top-left).
    m_ui->Panel(20, 20, 300, 250, glm::vec3(0.06f, 0.07f, 0.10f), 0.85f);
    m_ui->Label("CUBE SETTINGS", 36, 34, 1.6f, glm::vec3(0.9f, 0.95f, 1.0f));
    m_ui->Slider(1, "Spin speed", 36, 78, 268, 22, m_spinSpeed, 0.0f, 4.0f);
    m_ui->Slider(2, "Red",        36, 118, 268, 22, m_color.r, 0.0f, 1.0f);
    m_ui->Slider(3, "Green",      36, 158, 268, 22, m_color.g, 0.0f, 1.0f);
    m_ui->Slider(4, "Blue",       36, 198, 268, 22, m_color.b, 0.0f, 1.0f);
    if (m_ui->Button(5, "Reset", 36, 226, 130, 30)) {
        m_spinSpeed = 1.0f; m_color = glm::vec3(0.3f, 0.6f, 1.0f);
    }
    if (m_ui->Button(6, "Metallic", 174, 226, 130, 30)) {
        m_metallic = (m_metallic < 0.5f) ? 0.95f : 0.05f;
        m_rough    = (m_metallic < 0.5f) ? 0.5f  : 0.2f;
    }

    // Health widget (top-right).
    const float px = W - 300.0f;
    m_ui->Panel(px, 20, 280, 110, glm::vec3(0.06f, 0.07f, 0.10f), 0.85f);
    m_ui->Label("HEALTH", px + 16, 34, 1.6f, glm::vec3(0.9f));
    m_ui->Bar(px + 16, 58, 248, 22, m_hp / 100.0f,
              m_hp > 30.0f ? glm::vec3(0.3f, 0.85f, 0.4f) : glm::vec3(0.9f, 0.3f, 0.25f));
    char hb[32]; std::snprintf(hb, sizeof(hb), "%d / 100", (int)m_hp);
    m_ui->Label(hb, px + 200, 60, 1.3f, glm::vec3(0.9f));
    if (m_ui->Button(7, "Damage", px + 16, 90, 120, 28)) m_hp = std::max(0.0f, m_hp - 20.0f);
    if (m_ui->Button(8, "Heal",   px + 144, 90, 120, 28)) m_hp = std::min(100.0f, m_hp + 20.0f);

    char fps[48]; std::snprintf(fps, sizeof(fps), "%.0f fps   (mouse-driven UI)", m_fps);
    m_ui->Label(fps, 20.0f, w.Height() - 28.0f, 1.4f, glm::vec3(0.8f));

    m_ui->End();
}

void UiDemoApp::OnShutdown() {
    m_config.Set("window.vsync", GetWindow().IsVSync());
    m_config.Save();
}
