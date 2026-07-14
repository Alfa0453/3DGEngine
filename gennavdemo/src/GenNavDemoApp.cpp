#include "GenNavDemoApp.h"

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
using engine::ai::NavObstacle;
using engine::ai::NavBuildConfig;
using engine::ai::NavMeshBuilder;

namespace {
engine::WindowProps MakeProps(const engine::Config& cfg) {
    engine::WindowProps p;
    p.title  = "Auto-Navmesh — voxelize + region-merge from box obstacles";
    p.width  = cfg.GetInt("window.width", 1280);
    p.height = cfg.GetInt("window.height", 720);
    p.vsync  = cfg.GetBool("window.vsync", true);
    return p;
}
constexpr float kHiddenY = -1000.0f;
// A palette so adjacent merged rectangles read as distinct.
const glm::vec3 kPalette[] = {
    {0.20f,0.55f,0.85f},{0.85f,0.45f,0.25f},{0.35f,0.75f,0.45f},{0.75f,0.65f,0.25f},
    {0.60f,0.40f,0.80f},{0.30f,0.75f,0.75f},{0.85f,0.40f,0.55f},{0.55f,0.70f,0.30f},
};
} // namespace

GenNavDemoApp::GenNavDemoApp(engine::Config& config)
    : engine::Application(MakeProps(config)), m_config(config) {}

void GenNavDemoApp::OnInit() {
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

    // Obstacles (their XZ footprints carve the walkable area).
    m_obstacles = {
        { {4.0f, 0.9f, 4.0f},  {1.4f, 0.9f, 1.4f} },
        { {8.5f, 0.9f, 7.0f},  {1.0f, 0.9f, 2.2f} },
        { {5.5f, 0.9f, 10.0f}, {2.2f, 0.9f, 0.8f} },
    };

    // Auto-generate the navmesh.
    NavBuildConfig cfg;
    cfg.boundsMin = m_boundsMin; cfg.boundsMax = m_boundsMax;
    cfg.cellSize = 0.5f; cfg.agentRadius = 0.35f;
    m_nav = NavMeshBuilder::Build(cfg, m_obstacles);

    // Build one flat quad Mesh per generated polygon (for per-poly colouring).
    m_polyMeshes.reserve(m_nav.polys.size());
    for (const auto& poly : m_nav.polys) {
        std::vector<float> vv; std::vector<std::uint32_t> ii;
        for (int idx : poly.verts) {
            const glm::vec3& p = m_nav.vertices[static_cast<std::size_t>(idx)];
            vv.insert(vv.end(), { p.x, 0.02f, p.z, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f });
        }
        ii = { 0, 1, 2, 0, 2, 3 };
        m_polyMeshes.emplace_back(vv, ii, engine::VertexLayout{ {3}, {3}, {2} });
    }

    BuildScene();
    GetWindow().SetCursorCaptured(false);
}

void GenNavDemoApp::BuildScene() {
    // Dark ground under everything.
    {
        Entity e = m_reg.Create();
        Transform t; t.position = glm::vec3(6.0f, -0.05f, 6.0f); t.scale = glm::vec3(40.0f, 0.1f, 40.0f);
        m_reg.Add<Transform>(e, t);
        PbrMaterial m; m.albedo = glm::vec3(0.10f, 0.10f, 0.12f); m.roughness = 0.95f;
        m_reg.Add<MeshPBR>(e, MeshPBR{&*m_cube, m});
    }
    // Navmesh polygons, one entity each, coloured from the palette.
    for (std::size_t i = 0; i < m_polyMeshes.size(); ++i) {
        Entity e = m_reg.Create();
        m_reg.Add<Transform>(e, Transform{});
        const glm::vec3 c = kPalette[i % (sizeof(kPalette) / sizeof(kPalette[0]))];
        PbrMaterial m; m.albedo = c; m.roughness = 0.65f; m.emissive = c * 0.06f;
        m_reg.Add<MeshPBR>(e, MeshPBR{&m_polyMeshes[i], m});
    }
    // Obstacle boxes.
    for (const NavObstacle& o : m_obstacles) {
        Entity e = m_reg.Create();
        Transform t; t.position = o.center; t.scale = o.halfExtents * 2.0f;
        m_reg.Add<Transform>(e, t);
        PbrMaterial m; m.albedo = glm::vec3(0.45f, 0.28f, 0.24f); m.roughness = 0.85f;
        m_reg.Add<MeshPBR>(e, MeshPBR{&*m_cube, m});
    }
    // Agent + target.
    m_agentEnt = m_reg.Create();
    { Transform t; t.position = m_agent + glm::vec3(0,0.3f,0); t.scale = glm::vec3(0.5f);
      m_reg.Add<Transform>(m_agentEnt, t);
      PbrMaterial m; m.albedo = glm::vec3(0.3f, 0.95f, 0.4f); m.emissive = glm::vec3(0.05f,0.25f,0.08f);
      m_reg.Add<MeshPBR>(m_agentEnt, MeshPBR{&*m_sphere, m}); }
    m_targetEnt = m_reg.Create();
    { Transform t; t.position = m_target + glm::vec3(0,0.3f,0); t.scale = glm::vec3(0.4f);
      m_reg.Add<Transform>(m_targetEnt, t);
      PbrMaterial m; m.albedo = glm::vec3(1.0f, 0.5f, 0.12f); m.emissive = glm::vec3(0.3f,0.12f,0.0f);
      m_reg.Add<MeshPBR>(m_targetEnt, MeshPBR{&*m_sphere, m}); }
    // Path markers.
    for (int i = 0; i < 48; ++i) {
        Entity e = m_reg.Create();
        Transform t; t.position = glm::vec3(0, kHiddenY, 0); t.scale = glm::vec3(0.12f);
        m_reg.Add<Transform>(e, t);
        PbrMaterial m; m.albedo = glm::vec3(1.0f, 0.98f, 0.5f); m.emissive = glm::vec3(0.3f,0.28f,0.12f);
        m_reg.Add<MeshPBR>(e, MeshPBR{&*m_cube, m});
        m_markers.push_back(e);
    }
    // Sun.
    { Entity s = m_reg.Create(); m_reg.Add<Transform>(s, Transform{});
      Light l; l.type = Light::Type::Directional; l.direction = glm::vec3(-0.4f, -1.0f, -0.5f);
      l.color = glm::vec3(1.0f); l.intensity = 2.6f; m_reg.Add<Light>(s, l); }
}

void GenNavDemoApp::OnUpdate(float dt) {
    engine::Window& w = GetWindow();
    m_dt = dt;
    if (dt > 0.0f) m_fps = m_fps * 0.92f + (1.0f / dt) * 0.08f;
    if (w.IsKeyPressed(GLFW_KEY_ESCAPE)) w.SetShouldClose(true);

    glm::vec3 mv(0.0f);
    if (w.IsKeyPressed(GLFW_KEY_W)) mv.z -= 1.0f;
    if (w.IsKeyPressed(GLFW_KEY_S)) mv.z += 1.0f;
    if (w.IsKeyPressed(GLFW_KEY_A)) mv.x -= 1.0f;
    if (w.IsKeyPressed(GLFW_KEY_D)) mv.x += 1.0f;
    if (glm::dot(mv, mv) > 0.0f) m_target += glm::normalize(mv) * (6.0f * dt);
    m_target.x = glm::clamp(m_target.x, m_boundsMin.x + 0.3f, m_boundsMax.x - 0.3f);
    m_target.z = glm::clamp(m_target.z, m_boundsMin.z + 0.3f, m_boundsMax.z - 0.3f);
    m_target.y = 0.0f;

    if (m_nav.FindPath(m_agent, m_target, m_path) && m_path.size() >= 2) {
        const glm::vec3 d = m_path[1] - m_agent;
        const float dist = glm::length(d);
        const float step = 3.5f * dt;
        m_agent = (dist > step) ? m_agent + d / dist * step : m_path[1];
        m_agent.y = 0.0f;
    }
    m_reg.Get<Transform>(m_agentEnt).position  = m_agent + glm::vec3(0, 0.3f, 0);
    m_reg.Get<Transform>(m_targetEnt).position = m_target + glm::vec3(0, 0.3f, 0);
    for (std::size_t i = 0; i < m_markers.size(); ++i) {
        Transform& t = m_reg.Get<Transform>(m_markers[i]);
        t.position = (i < m_path.size()) ? m_path[i] + glm::vec3(0, 0.12f, 0) : glm::vec3(0, kHiddenY, 0);
    }

    const float rot = 1.5f * dt;
    if (w.IsKeyPressed(GLFW_KEY_LEFT))  m_camYaw   -= rot;
    if (w.IsKeyPressed(GLFW_KEY_RIGHT)) m_camYaw   += rot;
    if (w.IsKeyPressed(GLFW_KEY_UP))    m_camPitch -= rot;
    if (w.IsKeyPressed(GLFW_KEY_DOWN))  m_camPitch += rot;
    if (w.IsKeyPressed(GLFW_KEY_Z)) m_camDist -= 12.0f * dt;
    if (w.IsKeyPressed(GLFW_KEY_X)) m_camDist += 12.0f * dt;
    m_camPitch = glm::clamp(m_camPitch, 0.2f, 1.5f);
    m_camDist  = glm::clamp(m_camDist, 6.0f, 40.0f);
}

void GenNavDemoApp::OnRender() {
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
    opt.ambient = m_sample.ambient + glm::vec3(0.06f);
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
    std::snprintf(buf, sizeof(buf), "AUTO-NAVMESH   generated polys %zu   path %zu   %.0f fps",
                  m_nav.PolyCount(), m_path.size(), m_fps);
    m_text->Text(buf, 24.0f, 22.0f, 2.0f, glm::vec3(1.0f));
    m_text->Text("coloured tiles = merged rectangles from voxelize+merge   WASD move target   arrows orbit  Z/X zoom",
                 24.0f, hh - 32.0f, 1.4f, glm::vec3(0.75f));
    m_text->End();
}

void GenNavDemoApp::OnShutdown() {
    m_config.Set("window.vsync", GetWindow().IsVSync());
    m_config.Save();
}
