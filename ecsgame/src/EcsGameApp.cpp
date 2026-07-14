#include "EcsGameApp.h"

#include <engine/graphics/Primitives.h>

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstdio>

using engine::ecs::Entity;
using engine::ecs::Registry;
using engine::ecs::Transform;
using engine::ecs::MeshPBR;
using engine::ecs::PbrMaterial;
using engine::ecs::Light;
using engine::Health;
using engine::Projectile;
using engine::Attachment;

// ============================================================================
//  GAME-SPECIFIC COMPONENTS  (data only)
// ============================================================================
namespace {
struct Player {};                              // tag
struct Enemy  { float speed = 3.2f; glm::vec3 home{0.0f}; };

// The meshes are shared; the systems below reach them through this tiny context.
struct Meshes { const engine::Mesh* sphere; const engine::Mesh* cube; };

// A simple RNG for respawns.
float Rand01() { static unsigned s = 0x1234567u; s ^= s<<13; s ^= s>>17; s ^= s<<5; return (s & 0xFFFFFF) / float(0x1000000); }

// ============================================================================
//  GAME-SPECIFIC SYSTEMS  (free functions over the registry; no state)
// ============================================================================

// Move the player from WASD (camera-relative), on the ground plane.
void PlayerControlSystem(Registry& reg, Entity player, const glm::vec3& camFwd, float dt,
                         bool w, bool s, bool a, bool d) {
    const glm::vec3 fwd = glm::normalize(glm::vec3(camFwd.x, 0.0f, camFwd.z));
    const glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0, 1, 0)));
    glm::vec3 mv(0.0f);
    if (w) mv += fwd;
    if (s) mv -= fwd;
    if (d) mv += right;
    if (a) mv -= right;
    Transform& t = reg.Get<Transform>(player);
    if (glm::dot(mv, mv) > 0.0f) t.position += glm::normalize(mv) * (8.0f * dt);
    t.position.x = glm::clamp(t.position.x, -18.0f, 18.0f);
    t.position.z = glm::clamp(t.position.z, -18.0f, 18.0f);
}

// Every Enemy seeks the player and stops at melee range.
void EnemyChaseSystem(Registry& reg, const glm::vec3& playerPos, float dt) {
    reg.view<Transform, Enemy, Health>().each([&](Entity, Transform& t, Enemy& e, Health& h) {
        if (!h.alive) return;
        glm::vec3 to = playerPos - t.position; to.y = 0.0f;
        const float d = glm::length(to);
        if (d > 1.6f) t.position += to / d * (e.speed * dt);
    });
}

// Respawn enemies the frame they die (Health.justDied), at a random ring position.
void EnemyRespawnSystem(Registry& reg) {
    reg.view<Transform, Enemy, Health>().each([&](Entity, Transform& t, Enemy&, Health& h) {
        if (h.justDied) {
            const float ang = Rand01() * 6.28318f;
            t.position = glm::vec3(std::cos(ang) * 16.0f, 0.4f, std::sin(ang) * 16.0f);
            h.Reset(60.0f);
        }
    });
}
} // namespace

// ============================================================================
//  APP  (thin: spawn entities, then order the systems)
// ============================================================================
namespace {
engine::WindowProps MakeProps(const engine::Config& cfg) {
    engine::WindowProps p;
    p.title  = "ECS Game — components + systems (player / enemy / projectile / attachment)";
    p.width  = cfg.GetInt("window.width", 1280);
    p.height = cfg.GetInt("window.height", 720);
    p.vsync  = cfg.GetBool("window.vsync", true);
    return p;
}
} // namespace

EcsGameApp::EcsGameApp(engine::Config& config)
    : engine::Application(MakeProps(config)), m_config(config) {}

void EcsGameApp::OnInit() {
    m_renderer.Init();
    m_sphere.emplace(engine::primitives::Sphere(20));
    m_cube.emplace(engine::primitives::Cube());
    m_plane.emplace(engine::primitives::Plane(1.0f, 12.0f));
    m_pbr.emplace(2048);
    m_sky.emplace();
    m_post.emplace(GetWindow().Width(), GetWindow().Height());
    m_text.emplace();
    m_sample = engine::DayNightCycle::At(0.42f);
    m_ibl.emplace(256);
    m_ibl->Generate([&](const glm::mat4& v, const glm::mat4& p) { m_sky->Draw(v, p, m_sample, false); });

    // Ground.
    { Entity e = m_reg.Create();
      Transform t; t.scale = glm::vec3(40.0f, 1.0f, 40.0f); m_reg.Add<Transform>(e, t);
      PbrMaterial m; m.albedo = glm::vec3(0.20f, 0.22f, 0.25f); m.roughness = 0.95f;
      m_reg.Add<MeshPBR>(e, MeshPBR{&*m_plane, m}); }

    // Player = Transform + MeshPBR + Health + Player tag.
    m_player = m_reg.Create();
    { Transform t; t.position = glm::vec3(0, 0.6f, 0); t.scale = glm::vec3(1.2f); m_reg.Add<Transform>(m_player, t);
      PbrMaterial m; m.albedo = glm::vec3(0.25f, 0.6f, 1.0f); m.metallic = 0.1f; m.roughness = 0.35f;
      m_reg.Add<MeshPBR>(m_player, MeshPBR{&*m_sphere, m});
      Health h; h.hp = h.maxHp = 100.0f; m_reg.Add<Health>(m_player, h);
      m_reg.Add<Player>(m_player, Player{}); }

    // A "wand" cube ATTACHED to the player (follows it every frame via the system).
    { Entity e = m_reg.Create();
      Transform t; t.scale = glm::vec3(0.25f, 0.25f, 1.4f); m_reg.Add<Transform>(e, t);
      PbrMaterial m; m.albedo = glm::vec3(0.7f, 0.55f, 0.3f); m.roughness = 0.5f;
      m_reg.Add<MeshPBR>(e, MeshPBR{&*m_cube, m});
      Attachment a; a.parent = m_player; a.offset = glm::translate(glm::mat4(1.0f), glm::vec3(0.9f, 0.2f, 0.6f));
      m_reg.Add<Attachment>(e, a); }

    // Enemies = Transform + MeshPBR + Health + Enemy.
    for (int i = 0; i < 6; ++i) {
        const float ang = i / 6.0f * 6.28318f;
        Entity e = m_reg.Create();
        Transform t; t.position = glm::vec3(std::cos(ang) * 15.0f, 0.4f, std::sin(ang) * 15.0f); t.scale = glm::vec3(0.9f);
        m_reg.Add<Transform>(e, t);
        PbrMaterial m; m.albedo = glm::vec3(0.9f, 0.3f, 0.25f); m.roughness = 0.5f;
        m_reg.Add<MeshPBR>(e, MeshPBR{&*m_sphere, m});
        Health h; h.hp = h.maxHp = 60.0f; m_reg.Add<Health>(e, h);
        m_reg.Add<Enemy>(e, Enemy{});
    }

    // Sun.
    { Entity s = m_reg.Create(); m_reg.Add<Transform>(s, Transform{});
      Light l; l.type = Light::Type::Directional; l.direction = glm::vec3(-0.4f, -1.0f, -0.4f);
      l.color = glm::vec3(1.0f); l.intensity = 2.6f; m_reg.Add<Light>(s, l); }

    GetWindow().SetCursorCaptured(false);
}

void EcsGameApp::OnUpdate(float dt) {
    engine::Window& w = GetWindow();
    m_dt = dt;
    if (dt > 0.0f) m_fps = m_fps * 0.92f + (1.0f / dt) * 0.08f;
    if (w.IsKeyPressed(GLFW_KEY_ESCAPE)) w.SetShouldClose(true);

    // Camera orbit input (its forward feeds player movement + aim).
    const float rot = 1.5f * dt;
    if (w.IsKeyPressed(GLFW_KEY_LEFT))  m_camYaw   -= rot;
    if (w.IsKeyPressed(GLFW_KEY_RIGHT)) m_camYaw   += rot;
    if (w.IsKeyPressed(GLFW_KEY_UP))    m_camPitch -= rot;
    if (w.IsKeyPressed(GLFW_KEY_DOWN))  m_camPitch += rot;
    if (w.IsKeyPressed(GLFW_KEY_Z)) m_camDist -= 14.0f * dt;
    if (w.IsKeyPressed(GLFW_KEY_X)) m_camDist += 14.0f * dt;
    m_camPitch = glm::clamp(m_camPitch, 0.3f, 1.45f);
    m_camDist  = glm::clamp(m_camDist, 8.0f, 45.0f);
    const glm::vec3 camFwd(-std::cos(m_camPitch) * std::cos(m_camYaw), 0.0f, -std::cos(m_camPitch) * std::sin(m_camYaw));

    const glm::vec3 playerPos = m_reg.Get<Transform>(m_player).position;

    // ---- Order the systems (this is the whole game loop) -------------------
    PlayerControlSystem(m_reg, m_player, camFwd, dt,
                        w.IsKeyPressed(GLFW_KEY_W), w.IsKeyPressed(GLFW_KEY_S),
                        w.IsKeyPressed(GLFW_KEY_A), w.IsKeyPressed(GLFW_KEY_D));
    EnemyChaseSystem(m_reg, playerPos, dt);

    // Shoot: spawn a projectile ENTITY aimed at the nearest enemy (auto-aim).
    m_shootCooldown -= dt;
    const bool shoot = w.IsMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT) || w.IsKeyPressed(GLFW_KEY_SPACE);
    if (shoot && !m_prevShoot && m_shootCooldown <= 0.0f) {
        Entity nearest = engine::ecs::kNull; float best = 1e9f;
        m_reg.view<Transform, Enemy, Health>().each([&](Entity te, Transform& tt, Enemy&, Health& h) {
            if (!h.alive) return;
            const float d = glm::length(tt.position - playerPos);
            if (d < best) { best = d; nearest = te; }
        });
        if (nearest != engine::ecs::kNull) {
            const glm::vec3 dir = glm::normalize(m_reg.Get<Transform>(nearest).position - playerPos);
            Entity p = m_reg.Create();
            Transform t; t.position = playerPos + dir * 1.2f + glm::vec3(0, 0.2f, 0); t.scale = glm::vec3(0.3f);
            m_reg.Add<Transform>(p, t);
            PbrMaterial m; m.albedo = glm::vec3(2.5f, 1.6f, 0.5f); m.emissive = glm::vec3(2.0f, 1.2f, 0.3f);
            m_reg.Add<MeshPBR>(p, MeshPBR{&*m_sphere, m});
            Projectile pr; pr.dir = dir; pr.speed = 22.0f; pr.range = 45.0f; pr.damage = 30.0f; pr.radius = 1.1f; pr.owner = m_player;
            m_reg.Add<Projectile>(p, pr);
            m_shootCooldown = 0.18f;
        }
    }
    m_prevShoot = shoot;

    // Engine systems: move + damage, health bookkeeping, respawn, attachments.
    engine::UpdateProjectiles(m_reg, dt);
    engine::UpdateHealth(m_reg);
    EnemyRespawnSystem(m_reg);
    engine::UpdateAttachments(m_reg);
}

void EcsGameApp::OnRender() {
    engine::Window& w = GetWindow();
    const float aspect = w.AspectRatio();
    m_post->Resize(w.Width(), w.Height());

    const glm::vec3 target = m_reg.Get<Transform>(m_player).position;
    const glm::vec3 dir(std::cos(m_camPitch) * std::cos(m_camYaw),
                        std::sin(m_camPitch),
                        std::cos(m_camPitch) * std::sin(m_camYaw));
    engine::Camera cam(target + dir * m_camDist);
    cam.LookAt(target);

    m_post->BeginScene();
    engine::PbrRenderer::Options opt;
    opt.ambient = m_sample.ambient + glm::vec3(0.05f);
    opt.tonemap = false; opt.ibl = &*m_ibl; opt.fog = false; opt.pointShadows = false;
    m_pbr->Render(m_reg, cam, aspect, w.Width(), w.Height(), opt);
    m_sky->Draw(cam.ViewMatrix(), cam.ProjectionMatrix(aspect), m_sample, false);
    m_post->RenderToScreen(w.Width(), w.Height(), m_dt);

    const int ww = w.Width(), hh = w.Height();
    m_text->Begin(ww, hh);
    int enemies = 0; m_reg.view<Enemy, Health>().each([&](Entity, Enemy&, Health& h){ if(h.alive) ++enemies; });
    char buf[96];
    std::snprintf(buf, sizeof(buf), "ECS GAME   entities %zu   enemies %d   %.0f fps",
                  m_reg.AliveCount(), enemies, m_fps);
    m_text->Text(buf, 24.0f, 22.0f, 2.0f, glm::vec3(1.0f));
    m_text->Text("WASD move   LMB/Space auto-fire   arrows orbit   Z/X zoom   Esc  (all behaviour = systems)",
                 24.0f, hh - 32.0f, 1.4f, glm::vec3(0.75f));
    m_text->End();
}

void EcsGameApp::OnShutdown() {
    m_config.Set("window.vsync", GetWindow().IsVSync());
    m_config.Save();
}
