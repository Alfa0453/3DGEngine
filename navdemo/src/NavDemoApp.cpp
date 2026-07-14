#include "NavDemoApp.h"

#include <engine/graphics/Primitives.h>
#include <engine/graphics/VertexLayout.h>

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstdio>
#include <cstdint>

using engine::ecs::Entity;
using engine::ecs::Transform;
using engine::ecs::MeshPBR;
using engine::ecs::PbrMaterial;
using engine::ecs::Light;

namespace {
engine::WindowProps MakeProps(const engine::Config& cfg) {
    engine::WindowProps p;
    p.title  = "Navmesh Demo — walkable polygons + funnel pathfinding";
    p.width  = cfg.GetInt("window.width", 1280);
    p.height = cfg.GetInt("window.height", 720);
    p.vsync  = cfg.GetBool("window.vsync", true);
    return p;
}
constexpr int   kGrid = 6;                    // 6x6 cells over [0,6]^2
constexpr float kHiddenY = -1000.0f;          // park unused markers off-screen
} // namespace

NavDemoApp::NavDemoApp(engine::Config& config)
    : engine::Application(MakeProps(config)), m_config(config) {}

void NavDemoApp::OnInit() {
    m_renderer.Init();
    m_cube.emplace(engine::primitives::Cube());
    m_sphere.emplace(engine::primitives::Sphere(20));
    m_pbr.emplace(2048);
    m_sky.emplace();
    m_post.emplace(GetWindow().Width(), GetWindow().Height());
    m_text.emplace();
    m_sample = engine::DayNightCycle::At(0.45f);
    m_ibl.emplace(256);
    m_ibl->Generate([&](const glm::mat4& v, const glm::mat4& p) { m_sky->Draw(v, p, m_sample, false); });

    // --- Build the navmesh: a grid of unit quads with the centre 2x2 removed. ---
    const int V = kGrid + 1;                  // 7x7 vertices
    m_nav.vertices.clear();
    for (int j = 0; j <= kGrid; ++j)
        for (int i = 0; i <= kGrid; ++i)
            m_nav.vertices.push_back(glm::vec3(static_cast<float>(i), 0.0f, static_cast<float>(j)));
    auto vid = [&](int i, int j) { return j * V + i; };
    for (int j = 0; j < kGrid; ++j) {
        for (int i = 0; i < kGrid; ++i) {
            if (i >= 2 && i < 4 && j >= 2 && j < 4) continue;   // the hole (obstacle)
            m_nav.AddPolygon({ vid(i, j), vid(i + 1, j), vid(i + 1, j + 1), vid(i, j + 1) });
        }
    }
    m_nav.Build();

    // A single Mesh visualizing every navmesh polygon (flat quads, slightly raised).
    {
        std::vector<float> vv; std::vector<std::uint32_t> ii; std::uint32_t base = 0;
        for (const auto& poly : m_nav.polys) {
            for (int idx : poly.verts) {
                const glm::vec3& p = m_nav.vertices[static_cast<std::size_t>(idx)];
                vv.insert(vv.end(), { p.x, 0.02f, p.z,  0.0f, 1.0f, 0.0f,  0.0f, 0.0f });
            }
            ii.insert(ii.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
            base += 4;
        }
        m_navVis.emplace(vv, ii, engine::VertexLayout{ {3}, {3}, {2} });
    }

    BuildScene();
    GetWindow().SetCursorCaptured(false);
}

void NavDemoApp::BuildScene() {
    // Ground (dark).
    {
        Entity e = m_reg.Create();
        Transform t; t.position = glm::vec3(3.0f, -0.02f, 3.0f); t.scale = glm::vec3(30.0f, 1.0f, 30.0f);
        m_reg.Add<Transform>(e, t);
        PbrMaterial m; m.albedo = glm::vec3(0.12f, 0.12f, 0.14f); m.roughness = 0.95f;
        m_reg.Add<MeshPBR>(e, MeshPBR{&*m_cube, m});
    }
    // Navmesh surface (teal).
    {
        Entity e = m_reg.Create();
        m_reg.Add<Transform>(e, Transform{});
        PbrMaterial m; m.albedo = glm::vec3(0.15f, 0.55f, 0.55f); m.roughness = 0.6f; m.emissive = glm::vec3(0.02f, 0.12f, 0.12f);
        m_reg.Add<MeshPBR>(e, MeshPBR{&*m_navVis, m});
    }
    // Obstacle box over the hole.
    {
        Entity e = m_reg.Create();
        Transform t; t.position = glm::vec3(3.0f, 0.6f, 3.0f); t.scale = glm::vec3(2.0f, 1.2f, 2.0f);
        m_reg.Add<Transform>(e, t);
        PbrMaterial m; m.albedo = glm::vec3(0.5f, 0.3f, 0.25f); m.roughness = 0.8f;
        m_reg.Add<MeshPBR>(e, MeshPBR{&*m_cube, m});
    }
    // Agent (green) + target (orange).
    m_agentEnt = m_reg.Create();
    { Transform t; t.position = m_agent + glm::vec3(0,0.3f,0); t.scale = glm::vec3(0.5f);
      m_reg.Add<Transform>(m_agentEnt, t);
      PbrMaterial m; m.albedo = glm::vec3(0.3f, 0.9f, 0.4f); m.metallic = 0.1f;
      m_reg.Add<MeshPBR>(m_agentEnt, MeshPBR{&*m_sphere, m}); }
    m_targetEnt = m_reg.Create();
    { Transform t; t.position = m_target + glm::vec3(0,0.3f,0); t.scale = glm::vec3(0.4f);
      m_reg.Add<Transform>(m_targetEnt, t);
      PbrMaterial m; m.albedo = glm::vec3(1.0f, 0.55f, 0.15f); m.emissive = glm::vec3(0.3f, 0.12f, 0.0f);
      m_reg.Add<MeshPBR>(m_targetEnt, MeshPBR{&*m_sphere, m}); }

    // Waypoint markers (small cubes), parked off-screen until used.
    for (int i = 0; i < 40; ++i) {
        Entity e = m_reg.Create();
        Transform t; t.position = glm::vec3(0, kHiddenY, 0); t.scale = glm::vec3(0.12f);
        m_reg.Add<Transform>(e, t);
        PbrMaterial m; m.albedo = glm::vec3(1.0f, 0.95f, 0.4f); m.emissive = glm::vec3(0.3f, 0.28f, 0.1f);
        m_reg.Add<MeshPBR>(e, MeshPBR{&*m_cube, m});
        m_markers.push_back(e);
    }

    // Sun.
    { Entity s = m_reg.Create(); m_reg.Add<Transform>(s, Transform{});
      Light l; l.type = Light::Type::Directional; l.direction = glm::vec3(-0.4f, -1.0f, -0.5f);
      l.color = glm::vec3(1.0f); l.intensity = 2.6f; m_reg.Add<Light>(s, l); }
}

void NavDemoApp::OnUpdate(float dt) {
    engine::Window& w = GetWindow();
    m_dt = dt;
    if (dt > 0.0f) m_fps = m_fps * 0.92f + (1.0f / dt) * 0.08f;
    if (w.IsKeyPressed(GLFW_KEY_ESCAPE)) w.SetShouldClose(true);

    // Move the target (WASD) on the ground plane, clamped to the mesh extent.
    glm::vec3 mv(0.0f);
    if (w.IsKeyPressed(GLFW_KEY_W)) mv.z -= 1.0f;
    if (w.IsKeyPressed(GLFW_KEY_S)) mv.z += 1.0f;
    if (w.IsKeyPressed(GLFW_KEY_A)) mv.x -= 1.0f;
    if (w.IsKeyPressed(GLFW_KEY_D)) mv.x += 1.0f;
    if (glm::dot(mv, mv) > 0.0f) m_target += glm::normalize(mv) * (5.0f * dt);
    m_target.x = glm::clamp(m_target.x, 0.15f, 5.85f);
    m_target.z = glm::clamp(m_target.z, 0.15f, 5.85f);
    m_target.y = 0.0f;

    // Pathfind across the navmesh and step the agent toward the next waypoint.
    if (m_nav.FindPath(m_agent, m_target, m_path) && m_path.size() >= 2) {
        const glm::vec3 next = m_path[1];
        const glm::vec3 d = next - m_agent;
        const float dist = glm::length(d);
        const float step = 3.0f * dt;
        m_agent = (dist > step) ? m_agent + d / dist * step : next;
        m_agent.y = 0.0f;
    }

    m_reg.Get<Transform>(m_agentEnt).position  = m_agent + glm::vec3(0, 0.3f, 0);
    m_reg.Get<Transform>(m_targetEnt).position = m_target + glm::vec3(0, 0.3f, 0);
    // Draw the path waypoints on the markers.
    for (std::size_t i = 0; i < m_markers.size(); ++i) {
        Transform& t = m_reg.Get<Transform>(m_markers[i]);
        t.position = (i < m_path.size()) ? m_path[i] + glm::vec3(0, 0.12f, 0) : glm::vec3(0, kHiddenY, 0);
    }

    // Camera orbit.
    const float rot = 1.5f * dt;
    if (w.IsKeyPressed(GLFW_KEY_LEFT))  m_camYaw   -= rot;
    if (w.IsKeyPressed(GLFW_KEY_RIGHT)) m_camYaw   += rot;
    if (w.IsKeyPressed(GLFW_KEY_UP))    m_camPitch -= rot;
    if (w.IsKeyPressed(GLFW_KEY_DOWN))  m_camPitch += rot;
    if (w.IsKeyPressed(GLFW_KEY_Z)) m_camDist -= 10.0f * dt;
    if (w.IsKeyPressed(GLFW_KEY_X)) m_camDist += 10.0f * dt;
    m_camPitch = glm::clamp(m_camPitch, 0.2f, 1.5f);
    m_camDist  = glm::clamp(m_camDist, 5.0f, 30.0f);
}

void NavDemoApp::OnRender() {
    engine::Window& w = GetWindow();
    const float aspect = w.AspectRatio();
    m_post->Resize(w.Width(), w.Height());

    const glm::vec3 dir(std::cos(m_camPitch) * std::cos(m_camYaw),
                        std::sin(m_camPitch),
                        std::cos(m_camPitch) * std::sin(m_camYaw));
    engine::Camera cam(m_camTarget + dir * m_camDist);
    cam.LookAt(m_camTarget);

    m_post->BeginScene();
    engine::PbrRenderer::Options opt;
    opt.ambient = m_sample.ambient + glm::vec3(0.05f);
    opt.tonemap = false;
    opt.ibl     = &*m_ibl;
    opt.fog     = false;
    opt.pointShadows = false;
    m_pbr->Render(m_reg, cam, aspect, w.Width(), w.Height(), opt);
    m_sky->Draw(cam.ViewMatrix(), cam.ProjectionMatrix(aspect), m_sample, false);
    m_post->RenderToScreen(w.Width(), w.Height(), m_dt);

    const int ww = w.Width(), hh = w.Height();
    m_text->Begin(ww, hh);
    char buf[128];
    std::snprintf(buf, sizeof(buf), "NAVMESH   polys %zu   path waypoints %zu   %.0f fps",
                  m_nav.PolyCount(), m_path.size(), m_fps);
    m_text->Text(buf, 24.0f, 22.0f, 2.0f, glm::vec3(1.0f));
    m_text->Text("WASD move target (orange)   agent (green) pathfinds around the block   arrows orbit  Z/X zoom  Esc",
                 24.0f, hh - 32.0f, 1.4f, glm::vec3(0.75f));
    m_text->End();
}

void NavDemoApp::OnShutdown() {
    m_config.Set("window.vsync", GetWindow().IsVSync());
    m_config.Save();
}
