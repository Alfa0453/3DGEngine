#include "TerrainDemoApp.h"

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
    p.title  = "Terrain Demo — procedural heightmap (PBR + shadows)";
    p.width  = cfg.GetInt("window.width", 1280);
    p.height = cfg.GetInt("window.height", 720);
    p.vsync  = cfg.GetBool("window.vsync", true);
    return p;
}
} // namespace

TerrainDemoApp::TerrainDemoApp(engine::Config& config)
    : engine::Application(MakeProps(config)), m_config(config) {}

void TerrainDemoApp::OnInit() {
    m_renderer.Init();
    m_sphere.emplace(engine::primitives::Sphere(24));
    m_terrain.emplace();
    m_pbr.emplace(4096);
    m_sky.emplace();
    m_post.emplace(GetWindow().Width(), GetWindow().Height());
    m_text.emplace();
    m_sample = engine::DayNightCycle::At(0.30f);   // morning: long shadows over the hills
    m_ibl.emplace(256);
    m_ibl->Generate([&](const glm::mat4& v, const glm::mat4& p) { m_sky->Draw(v, p, m_sample, false); });

    // Terrain entity (albedo texture from the generator; big shadow frustum).
    m_terrainEnt = m_reg.Create();
    m_reg.Add<Transform>(m_terrainEnt, Transform{});
    // Ball that walks the surface.
    m_ballEnt = m_reg.Create();
    { Transform t; t.scale = glm::vec3(2.0f); m_reg.Add<Transform>(m_ballEnt, t);
      PbrMaterial m; m.albedo = glm::vec3(0.9f, 0.25f, 0.2f); m.metallic = 0.2f; m.roughness = 0.35f;
      m_reg.Add<MeshPBR>(m_ballEnt, MeshPBR{&*m_sphere, m}); }
    // Sun.
    { Entity s = m_reg.Create(); m_reg.Add<Transform>(s, Transform{});
      Light l; l.type = Light::Type::Directional; l.direction = glm::vec3(-0.5f, -0.8f, -0.35f);
      l.color = glm::vec3(1.0f, 0.95f, 0.85f); l.intensity = 3.0f; m_reg.Add<Light>(s, l); }

    Regenerate(m_seed);
    GetWindow().SetCursorCaptured(false);
}

void TerrainDemoApp::Regenerate(unsigned seed) {
    m_seed = seed;
    const glm::vec3 origin(-m_size * 0.5f, 0.0f, -m_size * 0.5f);
    m_terrain->Generate(m_res, m_size, origin, m_maxHeight, seed);

    PbrMaterial m; m.albedo = glm::vec3(1.0f); m.roughness = 0.95f;
    m.albedoMap = &m_terrain->Albedo();
    m_reg.Add<MeshPBR>(m_terrainEnt, MeshPBR{ &m_terrain->GetMesh(), m });

    m_ball = glm::vec3(0.0f, m_terrain->HeightAt(0.0f, 0.0f) + 2.0f, 0.0f);
}

void TerrainDemoApp::OnUpdate(float dt) {
    engine::Window& w = GetWindow();
    m_dt = dt;
    if (dt > 0.0f) m_fps = m_fps * 0.92f + (1.0f / dt) * 0.08f;
    if (w.IsKeyPressed(GLFW_KEY_ESCAPE)) w.SetShouldClose(true);

    const bool r = w.IsKeyPressed(GLFW_KEY_R);
    if (r && !m_prevR) Regenerate(m_seed + 1);
    m_prevR = r;

    // Drive the ball across the terrain; snap it to the surface via HeightAt.
    glm::vec3 fwd(std::cos(m_camYaw), 0.0f, std::sin(m_camYaw));
    const glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0, 1, 0)));
    glm::vec3 mv(0.0f);
    if (w.IsKeyPressed(GLFW_KEY_W)) mv += fwd;
    if (w.IsKeyPressed(GLFW_KEY_S)) mv -= fwd;
    if (w.IsKeyPressed(GLFW_KEY_D)) mv += right;
    if (w.IsKeyPressed(GLFW_KEY_A)) mv -= right;
    if (glm::dot(mv, mv) > 0.0f) m_ball += glm::normalize(mv) * (18.0f * dt);
    const float lim = m_size * 0.5f - 1.0f;
    m_ball.x = glm::clamp(m_ball.x, -lim, lim);
    m_ball.z = glm::clamp(m_ball.z, -lim, lim);
    m_ball.y = m_terrain->HeightAt(m_ball.x, m_ball.z) + 2.0f;   // sit on the surface
    m_reg.Get<Transform>(m_ballEnt).position = m_ball;

    // Camera orbit around the ball.
    const float rot = 1.4f * dt;
    if (w.IsKeyPressed(GLFW_KEY_LEFT))  m_camYaw   -= rot;
    if (w.IsKeyPressed(GLFW_KEY_RIGHT)) m_camYaw   += rot;
    if (w.IsKeyPressed(GLFW_KEY_UP))    m_camPitch -= rot;
    if (w.IsKeyPressed(GLFW_KEY_DOWN))  m_camPitch += rot;
    if (w.IsKeyPressed(GLFW_KEY_Z)) m_camDist -= 30.0f * dt;
    if (w.IsKeyPressed(GLFW_KEY_X)) m_camDist += 30.0f * dt;
    m_camPitch = glm::clamp(m_camPitch, 0.1f, 1.35f);
    m_camDist  = glm::clamp(m_camDist, 8.0f, 120.0f);
}

void TerrainDemoApp::OnRender() {
    engine::Window& w = GetWindow();
    const float aspect = w.AspectRatio();
    m_post->Resize(w.Width(), w.Height());

    const glm::vec3 target = m_ball + glm::vec3(0.0f, 3.0f, 0.0f);
    const glm::vec3 dir(std::cos(m_camPitch) * std::cos(m_camYaw),
                        std::sin(m_camPitch),
                        std::cos(m_camPitch) * std::sin(m_camYaw));
    engine::Camera cam(target + dir * m_camDist);
    cam.farPlane = 400.0f;
    cam.LookAt(target);

    m_post->BeginScene();
    engine::PbrRenderer::Options opt;
    opt.ambient = m_sample.ambient + glm::vec3(0.04f);
    opt.tonemap = false;
    opt.ibl     = &*m_ibl;
    opt.fog     = true;
    opt.fogColor = m_sample.horizon;
    opt.fogDensity = 0.006f;
    opt.pointShadows = false;
    opt.shadowRadius = m_size * 0.6f;     // cover the terrain in the sun shadow map
    m_pbr->Render(m_reg, cam, aspect, w.Width(), w.Height(), opt);
    m_sky->Draw(cam.ViewMatrix(), cam.ProjectionMatrix(aspect), m_sample, false);
    m_post->RenderToScreen(w.Width(), w.Height(), m_dt);

    const int ww = w.Width(), hh = w.Height();
    m_text->Begin(ww, hh);
    char buf[128];
    std::snprintf(buf, sizeof(buf), "TERRAIN   %dx%d verts   seed %u   ball y %.1f   %.0f fps",
                  m_res, m_res, m_seed, m_ball.y, m_fps);
    m_text->Text(buf, 24.0f, 22.0f, 2.0f, glm::vec3(1.0f));
    m_text->Text("WASD drive ball (snaps to surface)   R regenerate   arrows orbit   Z/X zoom   Esc",
                 24.0f, hh - 32.0f, 1.4f, glm::vec3(0.8f));
    m_text->End();
}

void TerrainDemoApp::OnShutdown() {
    m_config.Set("window.vsync", GetWindow().IsVSync());
    m_config.Save();
}
