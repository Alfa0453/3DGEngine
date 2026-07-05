#include "MarbleApp.h"

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
using engine::ecs::RigidBody;
using engine::ecs::Collider;

namespace {
engine::WindowProps MakeProps(const engine::Config& cfg) {
    engine::WindowProps p;
    p.title  = "Marble Roller";
    p.width  = cfg.GetInt("window.width", 1280);
    p.height = cfg.GetInt("window.height", 720);
    p.vsync  = cfg.GetBool("window.vsync", true);
    return p;
}
constexpr float kMoveAccel = 34.0f;   // rolling push force
constexpr float kMaxSpeed  = 9.0f;    // horizontal speed cap
constexpr float kJumpSpeed = 6.5f;
constexpr float kKillY     = -8.0f;
} // namespace

MarbleApp::MarbleApp(engine::Config& config)
    : engine::Application(MakeProps(config)), m_config(config) {}

void MarbleApp::OnInit() {
    m_renderer.Init();
    m_sphere.emplace(engine::primitives::Sphere(32));
    m_cube.emplace(engine::primitives::Cube());
    m_pbr.emplace(2048);
    m_sky.emplace();
    m_post.emplace(GetWindow().Width(), GetWindow().Height());
    m_ibl.emplace(256);
    m_text.emplace();
    m_world.gravity = glm::vec3(0.0f, -18.0f, 0.0f);   // a touch heavy: snappier marble

    BuildLevel();
    m_sample = engine::DayNightCycle::At(0.34f);
    m_ibl->Generate([&](const glm::mat4& v, const glm::mat4& p) { m_sky->Draw(v, p, m_sample, false); });
    GetWindow().SetCursorCaptured(true);
}

Entity MarbleApp::AddPlatform(const glm::vec3& pos, const glm::vec3& half,
                              const glm::vec3& color, float tiltZdeg) {
    Entity e = m_reg.Create();
    Transform t; t.position = pos; t.scale = half * 2.0f;
    if (tiltZdeg != 0.0f) t.rotation = glm::angleAxis(glm::radians(tiltZdeg), glm::vec3(0, 0, 1));
    m_reg.Add<Transform>(e, t);
    PbrMaterial m; m.albedo = color; m.roughness = 0.8f;
    m_reg.Add<MeshPBR>(e, MeshPBR{&*m_cube, m});
    Collider c = Collider::MakeBox(half); c.friction = 0.9f; c.restitution = 0.05f;
    m_reg.Add<Collider>(e, c);   // static: no RigidBody
    return e;
}

Entity MarbleApp::AddTrigger(const glm::vec3& pos, const glm::vec3& half,
                             const glm::vec3& markerColor) {
    Entity e = m_reg.Create();
    Transform t; t.position = pos; t.scale = half * 2.0f;
    m_reg.Add<Transform>(e, t);
    PbrMaterial m; m.albedo = glm::vec3(0.0f); m.emissive = markerColor * 2.0f; m.roughness = 1.0f;
    m_reg.Add<MeshPBR>(e, MeshPBR{&*m_cube, m});         // glowing marker
    Collider c = Collider::MakeBox(half); c.isTrigger = true;
    m_reg.Add<Collider>(e, c);
    return e;
}

void MarbleApp::BuildLevel() {
    const glm::vec3 pad(0.55f, 0.58f, 0.62f), path(0.45f, 0.5f, 0.55f), ramp(0.5f, 0.42f, 0.32f);

    AddPlatform({ 0.0f, 0.0f,  0.0f}, {3.0f, 0.5f, 3.0f}, pad);     // start
    AddPlatform({ 8.0f, 0.0f,  0.0f}, {3.0f, 0.5f, 2.0f}, path);    // path 1
    // gap x ~ 11 .. 15
    Entity cpA = AddPlatform({18.0f, 0.0f, 0.0f}, {3.0f, 0.5f, 2.0f}, path);  // landing (checkpoint A)
    (void)cpA;
    AddPlatform({24.0f, 1.35f, 0.0f}, {4.0f, 0.4f, 2.0f}, ramp, 18.0f);        // ramp up
    AddPlatform({30.0f, 2.5f,  0.0f}, {3.0f, 0.5f, 2.0f}, path);               // upper (checkpoint B)
    AddPlatform({30.0f, 2.5f,  6.0f}, {2.0f, 0.5f, 3.0f}, path);               // turn toward +z
    AddPlatform({30.0f, 2.5f, 12.0f}, {3.0f, 0.5f, 3.0f}, pad);               // goal pad

    // Checkpoints (trigger volumes just above the landing + upper platforms).
    m_checkpoints[AddTrigger({18.0f, 1.4f, 0.0f}, {1.0f, 1.0f, 1.8f}, {0.2f, 0.6f, 1.0f})]
        = glm::vec3(18.0f, 1.6f, 0.0f);
    m_checkpoints[AddTrigger({30.0f, 3.9f, 0.0f}, {1.0f, 1.0f, 1.8f}, {0.2f, 0.6f, 1.0f})]
        = glm::vec3(30.0f, 4.1f, 0.0f);

    // Goal (green ring at the end).
    m_goal = AddTrigger({30.0f, 3.9f, 12.0f}, {1.4f, 1.2f, 1.4f}, {0.2f, 1.0f, 0.4f});

    // The marble.
    m_marble = m_reg.Create();
    Transform t; t.position = m_spawn; t.scale = glm::vec3(m_marbleRadius * 2.0f);
    m_reg.Add<Transform>(m_marble, t);
    PbrMaterial m; m.albedo = glm::vec3(0.95f, 0.2f, 0.15f); m.metallic = 0.3f; m.roughness = 0.25f;
    m_reg.Add<MeshPBR>(m_marble, MeshPBR{&*m_sphere, m});
    RigidBody rb = RigidBody::Dynamic(1.0f); rb.allowSleep = false;   // always responsive
    m_reg.Add<RigidBody>(m_marble, rb);
    Collider c = Collider::MakeSphere(m_marbleRadius); c.friction = 0.6f; c.restitution = 0.2f;
    m_reg.Add<Collider>(m_marble, c);

    // Sun + a warm fill light.
    Entity sun = m_reg.Create();
    m_reg.Add<Transform>(sun, Transform{});
    { Light l; l.type = Light::Type::Directional; l.direction = glm::vec3(-0.4f, -1.0f, -0.35f);
      l.color = glm::vec3(1.0f, 0.97f, 0.9f); l.intensity = 2.8f; m_reg.Add<Light>(sun, l); }
    Entity pl = m_reg.Create();
    m_reg.Add<Transform>(pl, Transform{glm::vec3(20.0f, 8.0f, 4.0f), glm::vec3(1.0f), glm::quat(1,0,0,0)});
    { Light l; l.type = Light::Type::Point; l.color = glm::vec3(1.0f, 0.7f, 0.4f); l.intensity = 60.0f;
      m_reg.Add<Light>(pl, l); }
}

bool MarbleApp::GroundedRay() {
    // Cast just below the marble's surface so it never hits its own collider.
    const glm::vec3 c = m_reg.Get<Transform>(m_marble).position;
    engine::Ray ray;
    ray.origin = c - glm::vec3(0.0f, m_marbleRadius + 0.02f, 0.0f);
    ray.direction = glm::vec3(0.0f, -1.0f, 0.0f);
    engine::RaycastHit hit = m_world.Raycast(m_reg, ray, 0.2f);
    return hit.hit;
}

void MarbleApp::Respawn() {
    Transform& t = m_reg.Get<Transform>(m_marble);
    t.position = m_spawn;
    RigidBody& rb = m_reg.Get<RigidBody>(m_marble);
    rb.velocity = glm::vec3(0.0f);
    m_roll = glm::quat(1, 0, 0, 0);
}

void MarbleApp::OnUpdate(float dt) {
    engine::Window& w = GetWindow();
    m_dt = dt;
    if (dt > 0.0f) m_fps = m_fps * 0.92f + (1.0f / dt) * 0.08f;
    if (!m_won) m_time += dt;

    auto press = [&](int key) {
        const bool d = w.IsKeyPressed(key); const bool was = m_keyPrev[key]; m_keyPrev[key] = d; return d && !was;
    };
    if (press(GLFW_KEY_Q)) w.SetShouldClose(true);
    if (press(GLFW_KEY_F11)) w.ToggleFullscreen();
    if (press(GLFW_KEY_R)) { m_spawn = glm::vec3(0.0f, 1.2f, 0.0f); m_checkpointsHit = 0; m_won = false; m_time = 0.0f; Respawn(); }
    if (press(GLFW_KEY_ESCAPE)) { m_mouseCaptured = !m_mouseCaptured; w.SetCursorCaptured(m_mouseCaptured); }
    if (press(GLFW_KEY_SPACE)) m_jumpQueued = true;

    // Camera yaw/pitch from the mouse.
    if (m_mouseCaptured) {
        m_camYaw   += w.MouseDeltaX() * 0.15f;
        m_camPitch  = glm::clamp(m_camPitch - w.MouseDeltaY() * 0.12f, 5.0f, 70.0f);
    }

    // Camera-relative move direction (horizontal).
    const float yr = glm::radians(m_camYaw);
    const glm::vec3 fwd(std::cos(yr), 0.0f, std::sin(yr));
    const glm::vec3 right(-std::sin(yr), 0.0f, std::cos(yr));
    glm::vec3 dir(0.0f);
    if (w.IsKeyPressed(GLFW_KEY_W)) dir += fwd;
    if (w.IsKeyPressed(GLFW_KEY_S)) dir -= fwd;
    if (w.IsKeyPressed(GLFW_KEY_D)) dir += right;
    if (w.IsKeyPressed(GLFW_KEY_A)) dir -= right;
    m_moveDir = (glm::dot(dir, dir) > 1e-4f) ? glm::normalize(dir) : glm::vec3(0.0f);
}

void MarbleApp::OnFixedUpdate(float h) {
    RigidBody& rb = m_reg.Get<RigidBody>(m_marble);
    m_grounded = GroundedRay();

    // Roll: push in the desired direction (only bite when grounded, like real rolling).
    if (glm::dot(m_moveDir, m_moveDir) > 0.0f)
        rb.AddForce(m_moveDir * (m_grounded ? kMoveAccel : kMoveAccel * 0.35f));
    else if (m_grounded) {
        rb.velocity.x *= 0.90f; rb.velocity.z *= 0.90f;   // rolling resistance
    }

    if (m_jumpQueued && m_grounded) rb.velocity.y = kJumpSpeed;
    m_jumpQueued = false;

    m_world.Step(m_reg, h);

    // Clamp horizontal speed.
    glm::vec3& v = rb.velocity;
    glm::vec2 hv(v.x, v.z);
    const float sp = glm::length(hv);
    if (sp > kMaxSpeed) { hv *= kMaxSpeed / sp; v.x = hv.x; v.z = hv.y; }

    // Trigger events: checkpoints + goal.
    for (const auto& ev : m_world.Events()) {
        if (ev.phase != engine::CollisionEvent::Phase::Enter) continue;
        Entity other = (ev.a == m_marble) ? ev.b : (ev.b == m_marble ? ev.a : engine::ecs::kNull);
        if (other == engine::ecs::kNull) continue;
        if (other == m_goal) { m_won = true; }
        auto it = m_checkpoints.find(other);
        if (it != m_checkpoints.end() && it->second.x > m_spawn.x - 0.1f) {
            m_spawn = it->second; ++m_checkpointsHit;
        }
    }

    // Fall off -> respawn.
    if (m_reg.Get<Transform>(m_marble).position.y < kKillY) Respawn();

    // Visual roll from horizontal velocity.
    Transform& t = m_reg.Get<Transform>(m_marble);
    glm::vec3 hvel(v.x, 0.0f, v.z);
    const float s = glm::length(hvel);
    if (s > 1e-3f) {
        const glm::vec3 axis = glm::normalize(glm::cross(glm::vec3(0, 1, 0), hvel / s));
        m_roll = glm::angleAxis(s * h / m_marbleRadius, axis) * m_roll;
        t.rotation = m_roll;
    }
}

void MarbleApp::OnRender() {
    engine::Window& w = GetWindow();
    const float aspect = w.AspectRatio();
    m_post->Resize(w.Width(), w.Height());

    // Third-person follow camera behind the marble.
    const glm::vec3 target = m_reg.Get<Transform>(m_marble).position + glm::vec3(0.0f, 0.6f, 0.0f);
    const float yr = glm::radians(m_camYaw), pr = glm::radians(m_camPitch);
    const glm::vec3 back(-std::cos(yr) * std::cos(pr), std::sin(pr), -std::sin(yr) * std::cos(pr));
    const float dist = 9.0f;
    engine::Camera cam(target + back * dist);
    cam.LookAt(target);

    m_post->BeginScene();
    engine::PbrRenderer::Options opt;
    opt.ambient = m_sample.ambient;
    opt.tonemap = false;
    opt.ibl     = &*m_ibl;
    opt.fog     = true;
    opt.fogColor = m_sample.horizon;
    opt.pointShadows = false;
    m_pbr->Render(m_reg, cam, aspect, w.Width(), w.Height(), opt);
    m_sky->Draw(cam.ViewMatrix(), cam.ProjectionMatrix(aspect), m_sample, false);
    m_post->RenderToScreen(w.Width(), w.Height(), m_dt);

    // HUD.
    const int ww = w.Width(), hh = w.Height();
    m_text->Begin(ww, hh);
    const glm::vec3 white(1.0f), grey(0.7f), green(0.4f, 1.0f, 0.5f);
    char buf[96];
    std::snprintf(buf, sizeof(buf), "TIME %5.1fs    checkpoints %d/2", m_time, m_checkpointsHit);
    m_text->Text(buf, 24.0f, 22.0f, 2.0f, white);
    if (m_won) {
        m_text->Text("GOAL!  press R to play again", ww * 0.5f - 190.0f, hh * 0.5f - 12.0f, 2.4f, green);
    }
    m_text->Text("WASD roll (camera-relative)   SPACE jump   mouse look   R reset   Q quit",
                 24.0f, hh - 32.0f, 1.4f, grey);
    m_text->End();
}

void MarbleApp::OnShutdown() {
    m_config.Set("window.vsync", GetWindow().IsVSync());
    m_config.Save();
}
