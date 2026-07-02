#include "PhysicsDemoApp.h"

#include <engine/graphics/Primitives.h>

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstdio>
#include <random>

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
    p.title  = "Physics Playground";
    p.width  = cfg.GetInt("window.width", 1280);
    p.height = cfg.GetInt("window.height", 720);
    p.vsync  = cfg.GetBool("window.vsync", true);
    return p;
}
std::mt19937 g_rng(1234u);
float Rand(float a, float b) { return a + (b - a) * (g_rng() / float(g_rng.max())); }
} // namespace

PhysicsDemoApp::PhysicsDemoApp(engine::Config& config)
    : engine::Application(MakeProps(config)), m_config(config) {}

bool PhysicsDemoApp::Pressed(int key) {
    const bool down = GetWindow().IsKeyPressed(key);
    const bool was  = m_keyPrev[key];
    m_keyPrev[key] = down;
    return down && !was;
}

void PhysicsDemoApp::OnInit() {
    m_renderer.Init();
    m_cube.emplace(engine::primitives::Cube());
    m_sphere.emplace(engine::primitives::Sphere(32));
    m_plane.emplace(engine::primitives::Plane(1.0f, 20.0f));
    m_pbr.emplace(2048);
    m_sky.emplace();
    m_post.emplace(GetWindow().Width(), GetWindow().Height());
    m_ibl.emplace(256);
    m_text.emplace();

    m_world.gravity = glm::vec3(0.0f, -9.81f, 0.0f);

    BuildScene();
    m_sample = engine::DayNightCycle::At(m_timeOfDay);
    m_ibl->Generate([&](const glm::mat4& v, const glm::mat4& p) { m_sky->Draw(v, p, m_sample, false); });
    GetWindow().SetCursorCaptured(true);
}

Entity PhysicsDemoApp::SpawnBox(const glm::vec3& pos, const glm::vec3& half,
                                const glm::vec3& color, float mass) {
    Entity e = m_reg.Create();
    Transform t; t.position = pos; t.scale = half * 2.0f;   // unit cube -> box size
    m_reg.Add<Transform>(e, t);
    PbrMaterial m; m.albedo = color; m.roughness = 0.6f; m.metallic = 0.0f;
    m_reg.Add<MeshPBR>(e, MeshPBR{&*m_cube, m});
    m_reg.Add<RigidBody>(e, RigidBody::Dynamic(mass));
    Collider c = Collider::MakeBox(half); c.restitution = 0.1f; c.friction = 0.7f;
    m_reg.Add<Collider>(e, c);
    m_dynamic.push_back(e);
    return e;
}

Entity PhysicsDemoApp::SpawnSphere(const glm::vec3& pos, float radius,
                                   const glm::vec3& color, float mass) {
    Entity e = m_reg.Create();
    Transform t; t.position = pos; t.scale = glm::vec3(radius * 2.0f);
    m_reg.Add<Transform>(e, t);
    PbrMaterial m; m.albedo = color; m.roughness = 0.35f; m.metallic = 0.1f;
    m_reg.Add<MeshPBR>(e, MeshPBR{&*m_sphere, m});
    m_reg.Add<RigidBody>(e, RigidBody::Dynamic(mass));
    Collider c = Collider::MakeSphere(radius); c.restitution = 0.35f; c.friction = 0.5f;
    m_reg.Add<Collider>(e, c);
    m_dynamic.push_back(e);
    return e;
}

void PhysicsDemoApp::BuildScene() {
    // Ground: a static plane collider + a big visual quad.
    {
        Entity e = m_reg.Create();
        Transform t; t.scale = glm::vec3(60.0f, 1.0f, 60.0f);
        m_reg.Add<Transform>(e, t);
        PbrMaterial m; m.albedo = glm::vec3(0.30f, 0.32f, 0.35f); m.roughness = 0.9f;
        m_reg.Add<MeshPBR>(e, MeshPBR{&*m_plane, m});
        Collider c = Collider::MakePlane(glm::vec3(0, 1, 0), 0.0f); c.restitution = 0.2f; c.friction = 0.8f;
        m_reg.Add<Collider>(e, c);   // static: no RigidBody
    }

    // A static ramp (tilted box) to slide things down.
    {
        Entity e = m_reg.Create();
        Transform t; t.position = glm::vec3(-9.0f, 1.6f, 0.0f);
        t.rotation = glm::angleAxis(glm::radians(28.0f), glm::vec3(0, 0, 1));
        t.scale = glm::vec3(8.0f, 0.6f, 6.0f);
        m_reg.Add<Transform>(e, t);
        PbrMaterial m; m.albedo = glm::vec3(0.5f, 0.42f, 0.32f); m.roughness = 0.8f;
        m_reg.Add<MeshPBR>(e, MeshPBR{&*m_cube, m});
        Collider c = Collider::MakeBox(glm::vec3(4.0f, 0.3f, 3.0f)); c.friction = 0.4f;
        m_reg.Add<Collider>(e, c);
    }

    // Sun.
    m_sun = m_reg.Create();
    m_reg.Add<Transform>(m_sun, Transform{});
    { Light l; l.type = Light::Type::Directional; l.direction = glm::vec3(-0.4f, -1.0f, -0.3f);
      l.color = glm::vec3(1.0f); l.intensity = 2.6f; m_reg.Add<Light>(m_sun, l); }

    // A couple of warm point lights.
    const glm::vec3 pcol[2] = {{1.0f, 0.6f, 0.35f}, {0.5f, 0.7f, 1.0f}};
    const glm::vec3 ppos[2] = {{6.0f, 7.0f, 6.0f}, {-4.0f, 8.0f, -6.0f}};
    for (int i = 0; i < 2; ++i) {
        Entity e = m_reg.Create();
        Transform t; t.position = ppos[i];
        Light l; l.type = Light::Type::Point; l.color = pcol[i]; l.intensity = 40.0f;
        m_reg.Add<Transform>(e, t); m_reg.Add<Light>(e, l);
    }

    ResetDynamics();
}

void PhysicsDemoApp::ResetDynamics() {
    for (Entity e : m_dynamic) if (m_reg.Valid(e)) m_reg.Destroy(e);
    m_dynamic.clear();
    m_world.ClearJoints();
    g_rng.seed(1234u);

    // A pyramid of boxes.
    const glm::vec3 boxCol(0.80f, 0.78f, 0.72f);
    const float hs = 0.5f;   // box half-size
    for (int layer = 0; layer < 5; ++layer) {
        const int n = 5 - layer;
        const float y = hs + layer * (hs * 2.0f);
        const float x0 = -(n - 1) * hs;
        for (int i = 0; i < n; ++i)
            SpawnBox(glm::vec3(x0 + i * hs * 2.0f, y, 2.0f), glm::vec3(hs), boxCol);
    }

    // A little heap of spheres.
    for (int i = 0; i < 8; ++i)
        SpawnSphere(glm::vec3(Rand(-1.5f, 1.5f), 6.0f + i * 1.1f, -3.0f),
                    0.5f, glm::vec3(0.9f, 0.5f, 0.35f));

    // A swinging joint chain, pinned to a world anchor and started horizontal.
    {
        const glm::vec3 anchor(6.0f, 9.0f, -2.0f);
        const float link = 0.9f, rad = 0.3f;
        Entity prev = engine::ecs::kNull;
        for (int i = 0; i < 6; ++i) {
            Entity s = SpawnSphere(anchor + glm::vec3((i + 1) * link, 0.0f, 0.0f),
                                   rad, glm::vec3(0.4f, 0.8f, 0.9f), 0.6f);
            if (i == 0) m_world.AddDistanceJointToWorld(s, anchor, link);
            else        m_world.AddDistanceJoint(prev, s, link);
            prev = s;
        }
    }
}

void PhysicsDemoApp::Shoot() {
    const glm::vec3 dir = m_camera.Front();
    Entity e = SpawnSphere(m_camera.Position() + dir * 1.0f, 0.35f, glm::vec3(1.0f, 0.85f, 0.1f), 3.0f);
    RigidBody& rb = m_reg.Get<RigidBody>(e);
    rb.velocity = dir * 40.0f;     // fast
    rb.ccd = true;                 // don't tunnel through the stack
    ++m_shots;
}

void PhysicsDemoApp::OnUpdate(float dt) {
    engine::Window& w = GetWindow();
    m_time += dt; m_dt = dt;
    if (dt > 0.0f) m_fps = m_fps * 0.92f + (1.0f / dt) * 0.08f;

    if (Pressed(GLFW_KEY_Q)) w.SetShouldClose(true);
    if (Pressed(GLFW_KEY_F11)) w.ToggleFullscreen();
    if (Pressed(GLFW_KEY_R)) ResetDynamics();
    if (Pressed(GLFW_KEY_B)) m_post->settings.bloom = !m_post->settings.bloom;
    if (Pressed(GLFW_KEY_F)) Shoot();
    if (Pressed(GLFW_KEY_SPACE)) {              // drop a random body above the middle
        const glm::vec3 p(Rand(-3.0f, 3.0f), 12.0f, Rand(-3.0f, 3.0f));
        if (Rand(0.0f, 1.0f) < 0.5f) SpawnBox(p, glm::vec3(Rand(0.4f, 0.7f)), glm::vec3(Rand(0.4f,0.9f), Rand(0.4f,0.9f), Rand(0.4f,0.9f)));
        else                          SpawnSphere(p, Rand(0.35f, 0.6f), glm::vec3(Rand(0.4f,0.9f), Rand(0.4f,0.9f), Rand(0.4f,0.9f)));
    }
    if (Pressed(GLFW_KEY_ESCAPE)) { m_mouseCaptured = !m_mouseCaptured; w.SetCursorCaptured(m_mouseCaptured); }

    if (m_mouseCaptured)
        m_camera.AddYawPitch(w.MouseDeltaX() * 0.1f, -w.MouseDeltaY() * 0.1f);
    float v = 8.0f * dt;
    if (w.IsKeyPressed(GLFW_KEY_LEFT_SHIFT)) v *= 3.0f;
    if (w.IsKeyPressed(GLFW_KEY_W)) m_camera.MoveForward(v);
    if (w.IsKeyPressed(GLFW_KEY_S)) m_camera.MoveForward(-v);
    if (w.IsKeyPressed(GLFW_KEY_D)) m_camera.MoveRight(v);
    if (w.IsKeyPressed(GLFW_KEY_A)) m_camera.MoveRight(-v);
    if (w.IsKeyPressed(GLFW_KEY_LEFT_CONTROL)) m_camera.MoveUp(-v);
}

void PhysicsDemoApp::OnFixedUpdate(float h) {
    m_world.Step(m_reg, h);
}

void PhysicsDemoApp::OnRender() {
    engine::Window& w = GetWindow();
    const float aspect = w.AspectRatio();
    m_post->Resize(w.Width(), w.Height());

    m_post->BeginScene();
    engine::PbrRenderer::Options opt;
    opt.ambient = m_sample.ambient;
    opt.tonemap = false;
    opt.ibl     = &*m_ibl;
    opt.fog     = true;
    opt.fogColor = m_sample.horizon;
    opt.pointShadows = true;
    m_pbr->Render(m_reg, m_camera, aspect, w.Width(), w.Height(), opt);
    m_sky->Draw(m_camera.ViewMatrix(), m_camera.ProjectionMatrix(aspect), m_sample, false);
    m_post->RenderToScreen(w.Width(), w.Height(), m_dt);

    DrawHud();
}

void PhysicsDemoApp::DrawHud() {
    engine::Window& w = GetWindow();
    const int ww = w.Width(), hh = w.Height();
    m_text->Begin(ww, hh);
    const glm::vec3 white(1.0f), grey(0.65f);
    char buf[128];
    std::snprintf(buf, sizeof(buf), "PHYSICS PLAYGROUND   bodies %zu   shots %d   %.0f fps",
                  m_dynamic.size(), m_shots, m_fps);
    m_text->Text(buf, 24.0f, 22.0f, 2.0f, white);
    m_text->Text("boxes stack - spheres roll - chain swings on joints", 24.0f, 50.0f, 1.3f, grey);
    m_text->Text("WASD fly   SHIFT fast   SPACE drop body   F shoot (CCD)   R reset",
                 24.0f, hh - 60.0f, 1.4f, grey);
    m_text->Text("B bloom   ESC mouse   Q quit", 24.0f, hh - 32.0f, 1.4f, grey);
    m_text->End();
}

void PhysicsDemoApp::OnShutdown() {
    m_config.Set("window.vsync", GetWindow().IsVSync());
    m_config.Save();
}
