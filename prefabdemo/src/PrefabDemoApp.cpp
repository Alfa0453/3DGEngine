#include "PrefabDemoApp.h"

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

namespace {
engine::WindowProps MakeProps(const engine::Config& cfg) {
    engine::WindowProps p;
    p.title  = "Prefab Demo — define once, spawn many (+ clone)";
    p.width  = cfg.GetInt("window.width", 1280);
    p.height = cfg.GetInt("window.height", 720);
    p.vsync  = cfg.GetBool("window.vsync", true);
    return p;
}
// A demo-local spin tag so some prefabs rotate (shows components come along).
struct Spin { float speed = 1.0f; glm::vec3 axis{0, 1, 0}; };
} // namespace

PrefabDemoApp::PrefabDemoApp(engine::Config& config)
    : engine::Application(MakeProps(config)), m_config(config) {}

glm::vec3 PrefabDemoApp::RandomSpot() {
    auto r = [&](){ m_rng ^= m_rng << 13; m_rng ^= m_rng >> 17; m_rng ^= m_rng << 5; return (m_rng & 0xFFFF) / 65535.0f; };
    return glm::vec3((r() - 0.5f) * 20.0f, 0.0f, (r() - 0.5f) * 20.0f);
}

void PrefabDemoApp::DefinePrefabs() {
    // Each prefab is a builder that fully configures one kind of entity.
    const engine::Mesh* cube = &*m_cube; const engine::Mesh* sphere = &*m_sphere;

    m_prefabs.Define("crate", [cube](Registry& r, Entity e) {
        r.Add<Transform>(e, [](){ Transform t; t.position.y = 0.6f; t.scale = glm::vec3(1.2f); return t; }());
        PbrMaterial m; m.albedo = glm::vec3(0.55f, 0.4f, 0.24f); m.roughness = 0.8f;
        r.Add<MeshPBR>(e, MeshPBR{cube, m});
    });
    m_prefabs.Define("orb", [sphere](Registry& r, Entity e) {
        r.Add<Transform>(e, [](){ Transform t; t.position.y = 1.0f; t.scale = glm::vec3(0.8f); return t; }());
        PbrMaterial m; m.albedo = glm::vec3(0.3f, 0.8f, 1.6f); m.emissive = glm::vec3(0.15f, 0.5f, 1.0f); m.metallic = 0.2f;
        r.Add<MeshPBR>(e, MeshPBR{sphere, m});
        r.Add<Spin>(e, Spin{ 2.0f, glm::normalize(glm::vec3(0.2f, 1.0f, 0.1f)) });
    });
    m_prefabs.Define("pillar", [cube](Registry& r, Entity e) {
        r.Add<Transform>(e, [](){ Transform t; t.position.y = 2.0f; t.scale = glm::vec3(0.8f, 4.0f, 0.8f); return t; }());
        PbrMaterial m; m.albedo = glm::vec3(0.4f, 0.42f, 0.46f); m.roughness = 0.6f; m.metallic = 0.3f;
        r.Add<MeshPBR>(e, MeshPBR{cube, m});
    });
    m_prefabs.Define("gem", [cube](Registry& r, Entity e) {
        r.Add<Transform>(e, [](){ Transform t; t.position.y = 0.8f; t.scale = glm::vec3(0.7f);
            t.rotation = glm::angleAxis(0.9f, glm::normalize(glm::vec3(1,1,0))); return t; }());
        PbrMaterial m; m.albedo = glm::vec3(1.0f, 0.2f, 0.5f); m.emissive = glm::vec3(0.4f, 0.05f, 0.2f); m.roughness = 0.2f;
        r.Add<MeshPBR>(e, MeshPBR{cube, m});
        r.Add<Spin>(e, Spin{ 3.0f, glm::vec3(0, 1, 0) });
    });
}

void PrefabDemoApp::OnInit() {
    m_renderer.Init();
    m_cube.emplace(engine::primitives::Cube());
    m_sphere.emplace(engine::primitives::Sphere(20));
    m_plane.emplace(engine::primitives::Plane(1.0f, 16.0f));
    m_pbr.emplace(2048);
    m_sky.emplace();
    m_post.emplace(GetWindow().Width(), GetWindow().Height());
    m_text.emplace();
    m_sample = engine::DayNightCycle::At(0.40f);
    m_ibl.emplace(256);
    m_ibl->Generate([&](const glm::mat4& v, const glm::mat4& p) { m_sky->Draw(v, p, m_sample, false); });

    { m_ground = m_reg.Create();
      Transform t; t.scale = glm::vec3(30.0f, 1.0f, 30.0f); m_reg.Add<Transform>(m_ground, t);
      PbrMaterial m; m.albedo = glm::vec3(0.18f, 0.20f, 0.22f); m.roughness = 0.95f;
      m_reg.Add<MeshPBR>(m_ground, MeshPBR{&*m_plane, m}); }
    { m_sun = m_reg.Create(); m_reg.Add<Transform>(m_sun, Transform{});
      Light l; l.type = Light::Type::Directional; l.direction = glm::vec3(-0.4f, -1.0f, -0.5f);
      l.color = glm::vec3(1.0f); l.intensity = 2.6f; m_reg.Add<Light>(m_sun, l); }

    DefinePrefabs();
    GetWindow().SetCursorCaptured(false);
}

void PrefabDemoApp::OnUpdate(float dt) {
    engine::Window& w = GetWindow();
    m_dt = dt;
    if (dt > 0.0f) m_fps = m_fps * 0.92f + (1.0f / dt) * 0.08f;
    if (w.IsKeyPressed(GLFW_KEY_ESCAPE)) w.SetShouldClose(true);

    // Edge-triggered spawns: ONE call per prefab makes a fully-built entity.
    auto edge=[&](int key, bool& prev){ bool d=w.IsKeyPressed(key); bool hit=d&&!prev; prev=d; return hit; };
    if (edge(GLFW_KEY_1, m_p1)) { m_last = m_prefabs.Spawn("crate",  m_reg, RandomSpot() + glm::vec3(0,0.6f,0)); ++m_spawned; }
    if (edge(GLFW_KEY_2, m_p2)) { m_last = m_prefabs.Spawn("orb",    m_reg, RandomSpot() + glm::vec3(0,1.0f,0)); ++m_spawned; }
    if (edge(GLFW_KEY_3, m_p3)) { m_last = m_prefabs.Spawn("pillar", m_reg, RandomSpot() + glm::vec3(0,2.0f,0)); ++m_spawned; }
    if (edge(GLFW_KEY_4, m_p4)) { m_last = m_prefabs.Spawn("gem",    m_reg, RandomSpot() + glm::vec3(0,0.8f,0)); ++m_spawned; }
    if (edge(GLFW_KEY_C, m_pc) && m_reg.Valid(m_last)) {           // clone the last spawned
        Entity c = m_reg.Clone(m_last);
        m_reg.Get<Transform>(c).position = RandomSpot() + glm::vec3(0, m_reg.Get<Transform>(c).position.y, 0);
        m_last = c; ++m_spawned;
    }
    if (edge(GLFW_KEY_R, m_pr)) {                                   // clear everything spawned
        std::vector<Entity> kill;
        m_reg.view<Transform, MeshPBR>().each([&](Entity e, Transform&, MeshPBR&){ if (e != m_ground) kill.push_back(e); });
        for (Entity e : kill) m_reg.Destroy(e);
        m_last = engine::ecs::kNull; m_spawned = 0;
    }

    // A tiny system: spin entities that have Spin (shows the component came along).
    m_reg.view<Transform, Spin>().each([&](Entity, Transform& t, Spin& s){
        t.rotation = glm::angleAxis(s.speed * dt, s.axis) * t.rotation;
    });

    const float rot = 1.5f * dt;
    if (w.IsKeyPressed(GLFW_KEY_LEFT))  m_camYaw   -= rot;
    if (w.IsKeyPressed(GLFW_KEY_RIGHT)) m_camYaw   += rot;
    if (w.IsKeyPressed(GLFW_KEY_UP))    m_camPitch -= rot;
    if (w.IsKeyPressed(GLFW_KEY_DOWN))  m_camPitch += rot;
    if (w.IsKeyPressed(GLFW_KEY_Z)) m_camDist -= 14.0f * dt;
    if (w.IsKeyPressed(GLFW_KEY_X)) m_camDist += 14.0f * dt;
    m_camPitch = glm::clamp(m_camPitch, 0.2f, 1.45f);
    m_camDist  = glm::clamp(m_camDist, 8.0f, 45.0f);
}

void PrefabDemoApp::OnRender() {
    engine::Window& w = GetWindow();
    const float aspect = w.AspectRatio();
    m_post->Resize(w.Width(), w.Height());

    const glm::vec3 tgt(0, 1.0f, 0);
    const glm::vec3 dir(std::cos(m_camPitch) * std::cos(m_camYaw), std::sin(m_camPitch), std::cos(m_camPitch) * std::sin(m_camYaw));
    engine::Camera cam(tgt + dir * m_camDist);
    cam.LookAt(tgt);

    m_post->BeginScene();
    engine::PbrRenderer::Options opt;
    opt.ambient = m_sample.ambient + glm::vec3(0.05f);
    opt.tonemap = false; opt.ibl = &*m_ibl; opt.fog = false;
    m_pbr->Render(m_reg, cam, aspect, w.Width(), w.Height(), opt);
    m_sky->Draw(cam.ViewMatrix(), cam.ProjectionMatrix(aspect), m_sample, false);
    m_post->RenderToScreen(w.Width(), w.Height(), m_dt);

    const int ww = w.Width(), hh = w.Height();
    m_text->Begin(ww, hh);
    char buf[96];
    std::snprintf(buf, sizeof(buf), "PREFABS   defined %zu   spawned %d   entities %zu   %.0f fps",
                  m_prefabs.Count(), m_spawned, m_reg.AliveCount(), m_fps);
    m_text->Text(buf, 24.0f, 22.0f, 2.0f, glm::vec3(1.0f));
    m_text->Text("1 crate   2 orb   3 pillar   4 gem   C clone last   R clear   arrows orbit  Z/X zoom  Esc",
                 24.0f, hh - 32.0f, 1.4f, glm::vec3(0.75f));
    m_text->End();
}

void PrefabDemoApp::OnShutdown() {
    m_config.Set("window.vsync", GetWindow().IsVSync());
    m_config.Save();
}
