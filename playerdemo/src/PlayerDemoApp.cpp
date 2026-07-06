#include "PlayerDemoApp.h"

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
using engine::ecs::RigidBody;

namespace {
engine::WindowProps MakeProps(const engine::Config& cfg) {
    engine::WindowProps p;
    p.title  = "Player Demo — capsule controller (1st / 3rd person)";
    p.width  = cfg.GetInt("window.width", 1280);
    p.height = cfg.GetInt("window.height", 720);
    p.vsync  = cfg.GetBool("window.vsync", true);
    return p;
}
constexpr float kArena     = 40.0f;   // ground plane size
constexpr float kPlayerR   = 0.4f;
constexpr float kPlayerH   = 1.8f;
constexpr float kPinR      = 0.25f;
constexpr float kPinH      = 1.4f;
} // namespace

PlayerDemoApp::PlayerDemoApp(engine::Config& config)
    : engine::Application(MakeProps(config)), m_config(config) {}

void PlayerDemoApp::OnInit() {
    m_renderer.Init();
    m_cube.emplace(engine::primitives::Cube());
    m_plane.emplace(engine::primitives::Plane(1.0f, 24.0f));
    m_playerCap.emplace(engine::primitives::Capsule(kPlayerR, kPlayerH, 20));
    m_pinCap.emplace(engine::primitives::Capsule(kPinR, kPinH, 16));
    m_pbr.emplace(2048);
    m_sky.emplace();
    m_post.emplace(GetWindow().Width(), GetWindow().Height());
    m_text.emplace();
    m_sample = engine::DayNightCycle::At(0.40f);
    m_ibl.emplace(256);
    m_ibl->Generate([&](const glm::mat4& v, const glm::mat4& p) { m_sky->Draw(v, p, m_sample, false); });

    BuildArena();
    GetWindow().SetCursorCaptured(true);   // capture for mouse-look
}

static Entity AddBox(engine::ecs::Registry& reg, const engine::Mesh* mesh,
                     const glm::vec3& pos, const glm::vec3& half, const glm::vec3& color,
                     const glm::quat& rot = glm::quat(1, 0, 0, 0)) {
    Entity e = reg.Create();
    Transform t; t.position = pos; t.scale = half * 2.0f; t.rotation = rot;
    reg.Add<Transform>(e, t);
    PbrMaterial m; m.albedo = color; m.roughness = 0.9f;
    reg.Add<MeshPBR>(e, MeshPBR{mesh, m});
    reg.Add<Collider>(e, Collider::MakeBox(half));
    return e;
}

void PlayerDemoApp::BuildArena() {
    // Ground (visual plane + physics plane collider).
    {
        Entity e = m_reg.Create();
        Transform t; t.scale = glm::vec3(kArena, 1.0f, kArena);
        m_reg.Add<Transform>(e, t);
        PbrMaterial m; m.albedo = glm::vec3(0.30f, 0.32f, 0.34f); m.roughness = 0.97f;
        m_reg.Add<MeshPBR>(e, MeshPBR{&*m_plane, m});
        Entity g = m_reg.Create();
        m_reg.Add<Transform>(g, Transform{});
        m_reg.Add<Collider>(g, Collider::MakePlane(glm::vec3(0, 1, 0), 0.0f));
    }

    const glm::vec3 wallCol(0.55f, 0.52f, 0.48f);
    // Perimeter-ish walls to bump into.
    AddBox(m_reg, &*m_cube, {  0.0f, 1.0f, -10.0f}, {6.0f, 1.0f, 0.4f}, wallCol);
    AddBox(m_reg, &*m_cube, {-10.0f, 1.0f,  0.0f}, {0.4f, 1.0f, 6.0f}, wallCol);

    // A low step (0.3 high) the controller should walk straight up.
    AddBox(m_reg, &*m_cube, { 5.0f, 0.15f, 0.0f}, {2.0f, 0.15f, 3.0f}, glm::vec3(0.5f, 0.45f, 0.4f));

    // A ramp (a box tilted 20 deg) to walk up.
    {
        const glm::quat rot = glm::angleAxis(glm::radians(-20.0f), glm::vec3(0, 0, 1));
        AddBox(m_reg, &*m_cube, {-5.0f, 0.9f, 4.0f}, {3.0f, 0.2f, 2.5f}, glm::vec3(0.45f, 0.5f, 0.45f), rot);
    }

    // Player capsule (kinematic controller; entity carries a Capsule collider so the
    // dynamic pins collide with it, but no RigidBody -- it never falls under the solver).
    m_player.SetCapsule(kPlayerR, kPlayerH);
    m_player.SetPosition(glm::vec3(0.0f, kPlayerH * 0.5f + 0.1f, 4.0f));
    m_player.view = engine::PlayerController::View::ThirdPerson;
    m_playerEnt = m_reg.Create();
    { Transform t; t.position = m_player.CapsulePosition();
      m_reg.Add<Transform>(m_playerEnt, t);
      PbrMaterial m; m.albedo = glm::vec3(0.25f, 0.55f, 1.0f); m.roughness = 0.35f; m.metallic = 0.05f;
      m_reg.Add<MeshPBR>(m_playerEnt, MeshPBR{&*m_playerCap, m});
      m_reg.Add<Collider>(m_playerEnt, Collider::MakeCapsuleFromHeight(kPlayerR, kPlayerH)); }

    // A cluster of dynamic capsule "pins" that topple + settle (full dynamic capsules).
    const glm::vec3 pinCols[] = {
        {0.95f, 0.35f, 0.30f}, {0.35f, 0.85f, 0.45f}, {0.95f, 0.80f, 0.30f},
        {0.60f, 0.45f, 0.90f}, {0.30f, 0.80f, 0.90f},
    };
    int i = 0;
    for (int gx = -1; gx <= 1; ++gx) {
        Entity e = m_reg.Create();
        Transform t;
        t.position = glm::vec3(-2.0f + gx * 0.8f, 2.5f + i * 0.2f, -3.0f);
        // tilt each a little so they fall and settle, proving dynamic capsule contacts
        t.rotation = glm::angleAxis(glm::radians(15.0f * gx), glm::vec3(0, 0, 1));
        m_reg.Add<Transform>(e, t);
        auto rb = RigidBody::Dynamic(1.0f);
        m_reg.Add<RigidBody>(e, rb);
        m_reg.Add<Collider>(e, Collider::MakeCapsuleFromHeight(kPinR, kPinH));
        PbrMaterial m; m.albedo = pinCols[i % 5]; m.roughness = 0.5f; m.metallic = 0.05f;
        m_reg.Add<MeshPBR>(e, MeshPBR{&*m_pinCap, m});
        m_pins.push_back(e);
        ++i;
    }

    // Sun.
    { Entity s = m_reg.Create(); m_reg.Add<Transform>(s, Transform{});
      Light l; l.type = Light::Type::Directional; l.direction = glm::vec3(-0.4f, -1.0f, -0.35f);
      l.color = glm::vec3(1.0f); l.intensity = 2.6f; m_reg.Add<Light>(s, l); }
}

void PlayerDemoApp::OnUpdate(float dt) {
    engine::Window& w = GetWindow();
    m_dt = dt;
    if (dt > 0.0f) m_fps = m_fps * 0.92f + (1.0f / dt) * 0.08f;
    if (w.IsKeyPressed(GLFW_KEY_ESCAPE)) w.SetShouldClose(true);

    // Accumulate mouse-look deltas (consumed in the fixed step).
    if (m_mouseLook) { m_lookDX += w.MouseDeltaX(); m_lookDY += w.MouseDeltaY(); }
}

void PlayerDemoApp::OnFixedUpdate(float h) {
    engine::Window& w = GetWindow();

    engine::PlayerInput in;
    if (w.IsKeyPressed(GLFW_KEY_W)) in.moveForward += 1.0f;
    if (w.IsKeyPressed(GLFW_KEY_S)) in.moveForward -= 1.0f;
    if (w.IsKeyPressed(GLFW_KEY_D)) in.moveRight   += 1.0f;
    if (w.IsKeyPressed(GLFW_KEY_A)) in.moveRight   -= 1.0f;
    in.jump       = w.IsKeyPressed(GLFW_KEY_SPACE);
    in.sprint     = w.IsKeyPressed(GLFW_KEY_LEFT_SHIFT) || w.IsKeyPressed(GLFW_KEY_RIGHT_SHIFT);
    in.toggleView = w.IsKeyPressed(GLFW_KEY_V);
    in.lookYaw    = m_lookDX;
    in.lookPitch  = m_lookDY;
    m_lookDX = m_lookDY = 0.0f;

    // 1) Advance the kinematic player capsule (collide-and-slide vs the scene).
    m_player.Update(m_reg, in, h);

    // 2) Push the result onto the entity so the renderer + the dynamic solver see it.
    Transform& pt = m_reg.Get<Transform>(m_playerEnt);
    pt.position = m_player.CapsulePosition();
    pt.rotation = m_player.CapsuleRotation();

    // 3) Step the dynamic bodies (the pins); the player capsule acts as a static
    //    obstacle for them since it has a Collider but no RigidBody.
    m_world.Step(m_reg, h);
}

void PlayerDemoApp::OnRender() {
    engine::Window& w = GetWindow();
    const float aspect = w.AspectRatio();
    m_post->Resize(w.Width(), w.Height());

    engine::Camera cam(m_player.CameraPosition());
    cam.LookAt(m_player.CameraTarget());

    m_post->BeginScene();
    engine::PbrRenderer::Options opt;
    opt.ambient = m_sample.ambient;
    opt.tonemap = false;
    opt.ibl     = &*m_ibl;            // image-based ambient (as in the marble demo)
    opt.fog     = true;
    opt.fogColor = m_sample.horizon;
    opt.pointShadows = false;
    m_pbr->Render(m_reg, cam, aspect, w.Width(), w.Height(), opt);
    m_sky->Draw(cam.ViewMatrix(), cam.ProjectionMatrix(aspect), m_sample, false);
    m_post->RenderToScreen(w.Width(), w.Height(), m_dt);

    const int ww = w.Width(), hh = w.Height();
    m_text->Begin(ww, hh);
    const bool fp = (m_player.view == engine::PlayerController::View::FirstPerson);
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s   %s   %.0f fps",
                  fp ? "FIRST PERSON" : "THIRD PERSON",
                  m_player.Grounded() ? "grounded" : "airborne", m_fps);
    m_text->Text(buf, 24.0f, 22.0f, 2.0f, glm::vec3(1.0f));
    m_text->Text("WASD move   mouse look   Shift sprint   Space jump   V toggle view   Esc quit",
                 24.0f, hh - 32.0f, 1.4f, glm::vec3(0.75f));
    m_text->End();
}

void PlayerDemoApp::OnShutdown() {
    m_config.Set("window.vsync", GetWindow().IsVSync());
    m_config.Save();
}
