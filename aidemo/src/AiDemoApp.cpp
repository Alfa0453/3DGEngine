#include "AiDemoApp.h"

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
using engine::ecs::Collider;
namespace ai = engine::ai;

namespace {
engine::WindowProps MakeProps(const engine::Config& cfg) {
    engine::WindowProps p;
    p.title  = "AI Demo — patrol / chase";
    p.width  = cfg.GetInt("window.width", 1280);
    p.height = cfg.GetInt("window.height", 720);
    p.vsync  = cfg.GetBool("window.vsync", true);
    return p;
}
constexpr float kArena = 20.0f;   // arena spans [-10,10] on X and Z
constexpr float kCell  = 1.0f;
} // namespace

AiDemoApp::AiDemoApp(engine::Config& config)
    : engine::Application(MakeProps(config)), m_config(config) {}

void AiDemoApp::OnInit() {
    m_renderer.Init();
    m_cube.emplace(engine::primitives::Cube());
    m_sphere.emplace(engine::primitives::Sphere(24));
    m_plane.emplace(engine::primitives::Plane(1.0f, 20.0f));
    m_pbr.emplace(2048);
    m_sky.emplace();
    m_post.emplace(GetWindow().Width(), GetWindow().Height());
    m_text.emplace();
    m_sample = engine::DayNightCycle::At(0.42f);
    m_ibl.emplace(256);
    m_ibl->Generate([&](const glm::mat4& v, const glm::mat4& p) { m_sky->Draw(v, p, m_sample, false); });

    BuildArena();
    GetWindow().SetCursorCaptured(false);
}

static Entity AddBox(engine::ecs::Registry& reg, const engine::Mesh* mesh,
                     const glm::vec3& pos, const glm::vec3& half, const glm::vec3& color, bool collider) {
    Entity e = reg.Create();
    Transform t; t.position = pos; t.scale = half * 2.0f;
    reg.Add<Transform>(e, t);
    PbrMaterial m; m.albedo = color; m.roughness = 0.85f;
    reg.Add<MeshPBR>(e, MeshPBR{mesh, m});
    if (collider) reg.Add<Collider>(e, Collider::MakeBox(half));
    return e;
}

void AiDemoApp::BuildArena() {
    // Ground.
    {
        Entity e = m_reg.Create();
        Transform t; t.scale = glm::vec3(kArena, 1.0f, kArena);
        m_reg.Add<Transform>(e, t);
        PbrMaterial m; m.albedo = glm::vec3(0.28f, 0.30f, 0.33f); m.roughness = 0.95f;
        m_reg.Add<MeshPBR>(e, MeshPBR{&*m_plane, m});
    }

    // Nav grid over the arena; obstacles are marked below from the walls.
    const int cells = static_cast<int>(kArena / kCell);
    m_grid = ai::NavGrid(cells, cells, kCell, glm::vec3(-kArena * 0.5f, 0.0f, -kArena * 0.5f));

    // A few walls (block movement, pathing and line of sight).
    const glm::vec3 wallCol(0.55f, 0.52f, 0.48f);
    struct Wall { glm::vec3 pos; glm::vec3 half; };
    const Wall walls[] = {
        {{ 0.0f, 1.0f,  0.0f}, {4.0f, 1.0f, 0.4f}},   // central wall
        {{-4.0f, 1.0f,  4.0f}, {0.4f, 1.0f, 3.0f}},
        {{ 5.0f, 1.0f, -3.0f}, {0.4f, 1.0f, 3.5f}},
    };
    for (const Wall& w : walls) {
        AddBox(m_reg, &*m_cube, w.pos, w.half, wallCol, true);
        // mark covered cells (plus a one-cell margin) as obstacles
        const glm::ivec2 mn = m_grid.WorldToCell(w.pos - w.half - glm::vec3(kCell));
        const glm::ivec2 mx = m_grid.WorldToCell(w.pos + w.half + glm::vec3(kCell));
        for (int y = mn.y; y <= mx.y; ++y)
            for (int x = mn.x; x <= mx.x; ++x) m_grid.SetObstacle(x, y);
    }

    // Player (WASD): blue sphere with a collider so line-of-sight rays can hit it.
    m_player = m_reg.Create();
    { Transform t; t.position = {-8.0f, 0.5f, -8.0f}; m_reg.Add<Transform>(m_player, t);
      PbrMaterial m; m.albedo = glm::vec3(0.25f, 0.5f, 1.0f); m.roughness = 0.4f; m.metallic = 0.1f;
      m_reg.Add<MeshPBR>(m_player, MeshPBR{&*m_sphere, m});
      m_reg.Add<Collider>(m_player, Collider::MakeSphere(0.5f)); }

    // Enemy: sphere coloured by its AI state (no collider -- it's the observer).
    m_enemy = m_reg.Create();
    { Transform t; t.position = {7.0f, 0.5f, 7.0f}; m_reg.Add<Transform>(m_enemy, t);
      PbrMaterial m; m.albedo = glm::vec3(0.3f, 0.9f, 0.4f); m.roughness = 0.4f;
      m_reg.Add<MeshPBR>(m_enemy, MeshPBR{&*m_sphere, m}); }

    // Sun.
    { Entity s = m_reg.Create(); m_reg.Add<Transform>(s, Transform{});
      Light l; l.type = Light::Type::Directional; l.direction = glm::vec3(-0.4f, -1.0f, -0.35f);
      l.color = glm::vec3(1.0f); l.intensity = 2.6f; m_reg.Add<Light>(s, l); }

    // Configure the AI controller.
    m_ai.SetPosition(m_reg.Get<Transform>(m_enemy).position);
    m_ai.agent.maxSpeed = 4.0f;
    m_ai.agent.maxForce = 16.0f;
    m_ai.patrol = { {7,0.5f,7}, {7,0.5f,-7}, {-7,0.5f,-7}, {-7,0.5f,7} };
    m_cone.range = 12.0f; m_cone.halfAngleDegrees = 40.0f;
}

void AiDemoApp::OnUpdate(float dt) {
    engine::Window& w = GetWindow();
    m_dt = dt;
    if (dt > 0.0f) m_fps = m_fps * 0.92f + (1.0f / dt) * 0.08f;
    if (w.IsKeyPressed(GLFW_KEY_Q) || w.IsKeyPressed(GLFW_KEY_ESCAPE)) w.SetShouldClose(true);

    // Move the player on the ground plane (world axes).
    Transform& pt = m_reg.Get<Transform>(m_player);
    glm::vec3 mv(0.0f);
    if (w.IsKeyPressed(GLFW_KEY_W)) mv.z -= 1.0f;
    if (w.IsKeyPressed(GLFW_KEY_S)) mv.z += 1.0f;
    if (w.IsKeyPressed(GLFW_KEY_A)) mv.x -= 1.0f;
    if (w.IsKeyPressed(GLFW_KEY_D)) mv.x += 1.0f;
    if (glm::dot(mv, mv) > 0.0f) pt.position += glm::normalize(mv) * (7.0f * dt);
    pt.position.x = glm::clamp(pt.position.x, -9.5f, 9.5f);
    pt.position.z = glm::clamp(pt.position.z, -9.5f, 9.5f);
    pt.position.y = 0.5f;

    // Orbit camera: drag with the left mouse button, or the arrow keys; Z/X zoom.
    if (w.IsMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT)) {
        m_camYaw   += w.MouseDeltaX() * 0.006f;
        m_camPitch += w.MouseDeltaY() * 0.006f;
    }
    const float rot = 1.5f * dt;
    if (w.IsKeyPressed(GLFW_KEY_LEFT))  m_camYaw   -= rot;
    if (w.IsKeyPressed(GLFW_KEY_RIGHT)) m_camYaw   += rot;
    if (w.IsKeyPressed(GLFW_KEY_UP))    m_camPitch -= rot;
    if (w.IsKeyPressed(GLFW_KEY_DOWN))  m_camPitch += rot;
    if (w.IsKeyPressed(GLFW_KEY_Z)) m_camDist -= 20.0f * dt;
    if (w.IsKeyPressed(GLFW_KEY_X)) m_camDist += 20.0f * dt;
    m_camPitch = glm::clamp(m_camPitch, 0.2f, 1.50f);
    m_camDist  = glm::clamp(m_camDist, 8.0f, 60.0f);
}

void AiDemoApp::OnFixedUpdate(float h) {
    // Perception: can the enemy see the player right now?
    const glm::vec3 playerPos = m_reg.Get<Transform>(m_player).position;
    const glm::vec3 eye = m_ai.Position() + glm::vec3(0.0f, 0.5f, 0.0f);
    const bool sees = ai::CanSee(eye, m_ai.Facing(), m_cone, playerPos + glm::vec3(0, 0.5f, 0),
                                 m_player, m_world, m_reg);

    // The AI controller does the rest (patrol / chase / search + pathfind + steer).
    m_ai.Update(h, playerPos, sees, m_grid);

    // Sync the enemy entity to the controller + colour it by state.
    Transform& et = m_reg.Get<Transform>(m_enemy);
    et.position = m_ai.Position(); et.position.y = 0.5f;
    MeshPBR& em = m_reg.Get<MeshPBR>(m_enemy);
    using St = ai::AiAgent::State;
    const St st = m_ai.GetState();
    em.material.albedo = (st == St::Chase)  ? glm::vec3(0.95f, 0.25f, 0.2f)
                       : (st == St::Search) ? glm::vec3(0.95f, 0.8f, 0.2f)
                                            : glm::vec3(0.3f, 0.9f, 0.4f);
    em.material.emissive = em.material.albedo * 0.25f;
}

void AiDemoApp::OnRender() {
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
    opt.ibl     = &*m_ibl;    // image-based ambient (as in the marble demo)
    opt.fog     = false;
    m_pbr->Render(m_reg, cam, aspect, w.Width(), w.Height(), opt);
    m_sky->Draw(cam.ViewMatrix(), cam.ProjectionMatrix(aspect), m_sample, false);
    m_post->RenderToScreen(w.Width(), w.Height(), m_dt);

    const int ww = w.Width(), hh = w.Height();
    m_text->Begin(ww, hh);
    using St = ai::AiAgent::State;
    const char* stName = (m_ai.GetState() == St::Chase) ? "Chase"
                       : (m_ai.GetState() == St::Search) ? "Search" : "Patrol";
    char buf[96];
    std::snprintf(buf, sizeof(buf), "ENEMY STATE: %s   sees player: %s   %.0f fps",
                  stName, m_ai.SeesTarget() ? "YES" : "no", m_fps);
    m_text->Text(buf, 24.0f, 22.0f, 2.0f, glm::vec3(1.0f));
    m_text->Text("WASD move player   drag/arrows orbit   Z/X zoom   green=patrol red=chase yellow=search   Q quit",
                 24.0f, hh - 32.0f, 1.4f, glm::vec3(0.7f));
    m_text->End();
}

void AiDemoApp::OnShutdown() {
    m_config.Set("window.vsync", GetWindow().IsVSync());
    m_config.Save();
}
