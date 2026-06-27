#include "SceneApp.h"

#include <engine/graphics/Primitives.h>

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

using engine::ecs::Entity;
using engine::ecs::Transform;
using engine::ecs::MeshPBR;
using engine::ecs::PbrMaterial;
using engine::ecs::Light;

namespace {
engine::WindowProps MakeProps(const engine::Config& cfg) {
    engine::WindowProps p;
    p.title  = "Lighting & Scene (PBR, day/night)";
    p.width  = cfg.GetInt("window.width", 1280);
    p.height = cfg.GetInt("window.height", 720);
    p.vsync  = cfg.GetBool("window.vsync", true);
    return p;
}
} // namespace

SceneApp::SceneApp(engine::Config& config)
    : engine::Application(MakeProps(config)), m_config(config) {}

void SceneApp::OnInit() {
    m_renderer.Init();
    m_sphere.emplace(engine::primitives::Sphere(48));
    m_plane.emplace(engine::primitives::Plane(1.0f, 14.0f));   // tiled UVs for the floor textures
    m_pbr.emplace(2048);
    m_sky.emplace();
    m_post.emplace(GetWindow().Width(), GetWindow().Height());
    m_ibl.emplace(256);
    m_ssao.emplace(GetWindow().Width(), GetWindow().Height());
    m_ssr.emplace(GetWindow().Width(), GetWindow().Height());
    m_text.emplace();

    {   // Procedural floor textures: a subtle tile albedo + a bumpy normal map.
        const int TS = 256;
        std::vector<unsigned char> alb(static_cast<std::size_t>(TS) * TS * 4);
        std::vector<unsigned char> nrm(static_cast<std::size_t>(TS) * TS * 4);
        const float TWO_PI = 6.2831853f;
        for (int y = 0; y < TS; ++y)
            for (int x = 0; x < TS; ++x) {
                const float u = (x + 0.5f) / TS, v = (y + 0.5f) / TS;
                const std::size_t o = (static_cast<std::size_t>(y) * TS + x) * 4;
                const bool c = (((x / 64) + (y / 64)) & 1) != 0;
                const float base = c ? 0.32f : 0.22f;
                alb[o + 0] = static_cast<unsigned char>(base * 255.0f);
                alb[o + 1] = static_cast<unsigned char>(base * 1.03f * 255.0f);
                alb[o + 2] = static_cast<unsigned char>(base * 1.12f * 255.0f);
                alb[o + 3] = 255;
                const float dhdu = std::cos(u * TWO_PI) * std::sin(v * TWO_PI);
                const float dhdv = std::sin(u * TWO_PI) * std::cos(v * TWO_PI);
                glm::vec3 n = glm::normalize(glm::vec3(-dhdu * 0.6f, -dhdv * 0.6f, 1.0f)) * 0.5f + 0.5f;
                nrm[o + 0] = static_cast<unsigned char>(n.x * 255.0f);
                nrm[o + 1] = static_cast<unsigned char>(n.y * 255.0f);
                nrm[o + 2] = static_cast<unsigned char>(n.z * 255.0f);
                nrm[o + 3] = 255;
            }
        m_groundAlbedo.emplace(alb.data(), TS, TS);
        m_groundNormal.emplace(nrm.data(), TS, TS);
    }

    BuildScene();
    m_sample = engine::DayNightCycle::At(m_timeOfDay);
    GetWindow().SetCursorCaptured(true);
}

void SceneApp::BuildScene() {
    // Ground.
    {
        Entity e = m_reg.Create();
        Transform t; t.scale = glm::vec3(40.0f, 1.0f, 40.0f);
        m_reg.Add<Transform>(e, t);
        PbrMaterial m;
        m.albedo = glm::vec3(1.0f); m.roughness = 0.75f;
        m.albedoMap = &*m_groundAlbedo;
        m.normalMap = &*m_groundNormal;
        m_reg.Add<MeshPBR>(e, MeshPBR{&*m_plane, m});
    }

    // Metallic x roughness sphere grid.
    const int rows = 5, cols = 7;
    const float spacing = 2.6f, r = 0.95f;
    const glm::vec3 albedo(0.95f, 0.64f, 0.54f);
    const float startX = -((cols - 1) * spacing) * 0.5f;
    const float startZ = -((rows - 1) * spacing) * 0.5f;
    for (int row = 0; row < rows; ++row)
        for (int col = 0; col < cols; ++col) {
            Entity e = m_reg.Create();
            Transform t;
            t.position = glm::vec3(startX + col * spacing, r + 0.6f, startZ + row * spacing);
            t.scale    = glm::vec3(r * 2.0f);
            m_reg.Add<Transform>(e, t);
            PbrMaterial m;
            m.albedo    = albedo;
            m.metallic  = static_cast<float>(row) / static_cast<float>(rows - 1);
            m.roughness = glm::clamp(static_cast<float>(col) / static_cast<float>(cols - 1), 0.05f, 1.0f);
            m_reg.Add<MeshPBR>(e, MeshPBR{&*m_sphere, m});
        }

    // The sun/moon key light — updated every frame from the day/night cycle.
    m_sun = m_reg.Create();
    m_reg.Add<Transform>(m_sun, Transform{});
    {
        Light l;
        l.type = Light::Type::Directional;
        l.direction = glm::vec3(-0.5f, -1.0f, -0.35f);
        l.color = glm::vec3(1.0f);
        l.intensity = 3.0f;
        m_reg.Add<Light>(m_sun, l);
    }

    // Coloured point lights + emissive gizmos (these carry the night).
    const glm::vec3 pcolors[3] = {{1.0f, 0.3f, 0.15f}, {0.3f, 1.0f, 0.5f}, {0.4f, 0.55f, 1.0f}};
    const glm::vec3 ppos[3]    = {{-7.0f, 4.0f, 6.0f}, {0.0f, 5.0f, -7.0f}, {7.0f, 4.0f, 6.0f}};
    for (int i = 0; i < 3; ++i) {
        Entity e = m_reg.Create();
        Transform t; t.position = ppos[i];
        Light l; l.type = Light::Type::Point; l.color = pcolors[i]; l.intensity = 18.0f;
        m_reg.Add<Transform>(e, t);
        m_reg.Add<Light>(e, l);

        Entity g = m_reg.Create();
        Transform gt; gt.position = ppos[i]; gt.scale = glm::vec3(0.35f);
        PbrMaterial gm; gm.albedo = glm::vec3(0.0f); gm.emissive = pcolors[i] * 3.0f; gm.roughness = 1.0f;
        m_reg.Add<Transform>(g, gt);
        m_reg.Add<MeshPBR>(g, MeshPBR{&*m_sphere, gm});
        m_mainLight[i] = e;
        m_mainGizmo[i] = g;
    }

    // Many small static point lights — exercises the clustered (tiled) culling.
    {
        std::mt19937 rng(99u);
        std::uniform_real_distribution<float> ux(-9.0f, 9.0f), uz(-7.0f, 7.0f),
                                              uy(0.8f, 3.2f), uc(0.25f, 1.0f);
        for (int i = 0; i < 40; ++i) {
            Entity e = m_reg.Create();
            Transform t; t.position = glm::vec3(ux(rng), uy(rng), uz(rng));
            Light l; l.type = Light::Type::Point;
            l.color = glm::vec3(uc(rng), uc(rng), uc(rng));
            l.intensity = 3.0f;
            m_reg.Add<Transform>(e, t);
            m_reg.Add<Light>(e, l);
        }
    }

    // Two static spotlights aimed at the grid (cones + crisp shadows). No
    // emissive gizmos here, so the point-light gizmo animation stays simple.
    const glm::vec3 spos[2] = {{-6.0f, 9.0f, 6.0f}, {6.0f, 9.0f, -4.0f}};
    const glm::vec3 scol[2] = {{1.0f, 0.85f, 0.6f}, {0.55f, 0.7f, 1.0f}};
    const glm::vec3 target(0.0f, 1.0f, 0.0f);
    for (int i = 0; i < 2; ++i) {
        Entity e = m_reg.Create();
        Transform t; t.position = spos[i];
        Light l;
        l.type = Light::Type::Spot;
        l.direction = glm::normalize(target - spos[i]);
        l.color = scol[i];
        l.intensity = 110.0f;
        l.innerAngle = 13.0f;
        l.outerAngle = 22.0f;
        l.range = 40.0f;
        m_reg.Add<Transform>(e, t);
        m_reg.Add<Light>(e, l);
    }

    // Two sphere AREA lights: soft, stretched specular highlights. The glowing
    // sphere gizmo is sized to the light's physical radius.
    const glm::vec3 apos[2] = {{-5.0f, 5.0f, 1.0f}, {5.0f, 5.0f, 1.0f}};
    const glm::vec3 acol[2] = {{1.0f, 0.7f, 0.4f}, {0.4f, 0.6f, 1.0f}};
    const float arad = 1.6f;
    for (int i = 0; i < 2; ++i) {
        Entity e = m_reg.Create();
        Transform t; t.position = apos[i];
        Light l; l.type = Light::Type::Area; l.color = acol[i]; l.intensity = 45.0f; l.sourceRadius = arad;
        m_reg.Add<Transform>(e, t);
        m_reg.Add<Light>(e, l);
        Entity g = m_reg.Create();
        Transform gt; gt.position = apos[i]; gt.scale = glm::vec3(arad * 2.0f);
        PbrMaterial gm; gm.albedo = glm::vec3(0.0f); gm.emissive = acol[i] * 2.5f; gm.roughness = 1.0f;
        m_reg.Add<Transform>(g, gt);
        m_reg.Add<MeshPBR>(g, MeshPBR{&*m_sphere, gm});
    }
}

void SceneApp::OnUpdate(float dt) {
    engine::Window& w = GetWindow();
    m_time += dt;
    if (dt > 0.0f) m_fps = m_fps * 0.92f + (1.0f / dt) * 0.08f;
    m_dt = dt;

    if (Pressed(GLFW_KEY_Q)) w.SetShouldClose(true);
    if (Pressed(GLFW_KEY_F11)) w.ToggleFullscreen();
    if (Pressed(GLFW_KEY_L)) m_animateLights = !m_animateLights;
    if (Pressed(GLFW_KEY_B)) m_post->settings.bloom = !m_post->settings.bloom;
    if (Pressed(GLFW_KEY_E)) m_post->settings.autoExposure = !m_post->settings.autoExposure;
    if (Pressed(GLFW_KEY_I)) m_useIbl = !m_useIbl;
    if (Pressed(GLFW_KEY_G)) m_fog = !m_fog;
    if (Pressed(GLFW_KEY_O)) m_ssaoOn = !m_ssaoOn;
    if (Pressed(GLFW_KEY_P)) m_pointShadows = !m_pointShadows;
    if (Pressed(GLFW_KEY_K)) m_ssrOn = !m_ssrOn;
    if (Pressed(GLFW_KEY_ESCAPE)) {
        m_mouseCaptured = !m_mouseCaptured;
        w.SetCursorCaptured(m_mouseCaptured);
    }

    // --- Time of day ---
    if (Pressed(GLFW_KEY_T)) m_targetTime = (m_targetTime < 0.25f) ? 0.5f : 0.0f;   // toggle day/night
    if (Pressed(GLFW_KEY_C)) m_autoCycle = !m_autoCycle;
    if (Pressed(GLFW_KEY_1)) m_targetTime = 0.5f;    // noon
    if (Pressed(GLFW_KEY_2)) m_targetTime = 0.0f;    // midnight
    if (Pressed(GLFW_KEY_3)) m_targetTime = 0.75f;   // sunset
    if (w.IsKeyPressed(GLFW_KEY_LEFT_BRACKET))  m_targetTime -= dt * 0.15f;
    if (w.IsKeyPressed(GLFW_KEY_RIGHT_BRACKET)) m_targetTime += dt * 0.15f;
    m_targetTime -= std::floor(m_targetTime);

    if (m_autoCycle) m_timeOfDay += dt * 0.05f;       // ~20s per day
    else             m_timeOfDay += (m_targetTime - m_timeOfDay) * glm::clamp(dt * 2.0f, 0.0f, 1.0f);
    m_timeOfDay -= std::floor(m_timeOfDay);

    m_sample = engine::DayNightCycle::At(m_timeOfDay);
    if (m_reg.Valid(m_sun)) {
        Light& l = m_reg.Get<Light>(m_sun);
        l.direction = m_sample.keyLightDirection;
        l.color     = m_sample.keyLightColor;
        l.intensity = 1.0f;
    }

    // --- Camera ---
    if (m_mouseCaptured)
        m_camera.AddYawPitch(w.MouseDeltaX() * 0.1f, -w.MouseDeltaY() * 0.1f);
    float v = 6.0f * dt;
    if (w.IsKeyPressed(GLFW_KEY_LEFT_SHIFT)) v *= 3.0f;
    if (w.IsKeyPressed(GLFW_KEY_W)) m_camera.MoveForward(v);
    if (w.IsKeyPressed(GLFW_KEY_S)) m_camera.MoveForward(-v);
    if (w.IsKeyPressed(GLFW_KEY_D)) m_camera.MoveRight(v);
    if (w.IsKeyPressed(GLFW_KEY_A)) m_camera.MoveRight(-v);
    if (w.IsKeyPressed(GLFW_KEY_SPACE)) m_camera.MoveUp(v);
    if (w.IsKeyPressed(GLFW_KEY_LEFT_CONTROL)) m_camera.MoveUp(-v);

    // --- Orbiting point lights ---
    if (m_animateLights) {
        for (int i = 0; i < 3; ++i) {
            if (!m_reg.Valid(m_mainLight[i])) continue;
            const float a = m_time * 0.5f + static_cast<float>(i) * 2.094f;
            Transform& lt = m_reg.Get<Transform>(m_mainLight[i]);
            lt.position.x = std::cos(a) * 7.0f;
            lt.position.z = std::sin(a) * 7.0f;
            if (m_reg.Valid(m_mainGizmo[i]))
                m_reg.Get<Transform>(m_mainGizmo[i]).position = lt.position;
        }
    }
}

void SceneApp::OnRender() {
    engine::Window& w = GetWindow();
    const float aspect = w.AspectRatio();
    m_post->Resize(w.Width(), w.Height());

    // Regenerate image-based lighting from the sky when the time of day shifts.
    if (std::abs(m_sample.dayFactor - m_lastIblDay) > 0.04f) {
        m_ibl->Generate([&](const glm::mat4& v, const glm::mat4& p) {
            m_sky->Draw(v, p, m_sample, false);
        });
        m_lastIblDay = m_sample.dayFactor;
    }

    if (m_ssaoOn || m_ssrOn) m_ssao->Generate(m_reg, m_camera, aspect, w.Width(), w.Height());

    m_post->BeginScene();
    engine::PbrRenderer::Options opt;
    opt.ambient = m_sample.ambient;
    opt.tonemap = false;
    opt.ibl      = m_useIbl ? &*m_ibl : nullptr;
    opt.fog      = m_fog;
    opt.ssao     = m_ssaoOn ? &*m_ssao : nullptr;
    opt.pointShadows = m_pointShadows;
    opt.fogColor = m_sample.horizon;   // fog matches the sky at the horizon
    m_pbr->Render(m_reg, m_camera, aspect, w.Width(), w.Height(), opt);
    m_sky->Draw(m_camera.ViewMatrix(), m_camera.ProjectionMatrix(aspect), m_sample, false);
    if (m_ssrOn)
        m_ssr->Apply(m_post->HdrColor(), m_ssao->PositionTexture(), m_ssao->NormalTexture(),
                     m_camera.ProjectionMatrix(aspect), m_post->HdrFbo(), w.Width(), w.Height());
    m_post->RenderToScreen(w.Width(), w.Height(), m_dt);

    DrawHud();
}

void SceneApp::DrawHud() {
    engine::Window& w = GetWindow();
    const int ww = w.Width(), hh = w.Height();
    m_text->Begin(ww, hh);
    const glm::vec3 white(1.0f), grey(0.65f);

    const int totalMin = static_cast<int>(m_timeOfDay * 24.0f * 60.0f) % (24 * 60);
    char buf[96];
    std::snprintf(buf, sizeof(buf), "PBR SCENE   %02d:%02d   day %.2f   %.0f fps%s",
                  totalMin / 60, totalMin % 60, m_sample.dayFactor, m_fps,
                  m_autoCycle ? "   [auto]" : "");
    m_text->Text(buf, 24.0f, 22.0f, 2.0f, white);
    m_text->Text("rows = metallic   cols = roughness", 24.0f, 50.0f, 1.3f, grey);
    m_text->Text("WASD fly   SHIFT fast   T day/night   C cycle   [ ] scrub",
                 24.0f, hh - 60.0f, 1.4f, grey);
    m_text->Text("L lights   B bloom   E expo   I ibl   O ssao   P pshadow   K ssr   G fog   Q quit",
                 24.0f, hh - 32.0f, 1.4f, grey);
    m_text->End();
}

void SceneApp::OnShutdown() {
    m_config.Set("window.vsync", GetWindow().IsVSync());
    m_config.Set("window.fullscreen", GetWindow().IsFullscreen());
    m_config.Save();
}

bool SceneApp::Pressed(int key) {
    const bool down = GetWindow().IsKeyPressed(key);
    const bool was = m_keyPrev[key];
    m_keyPrev[key] = down;
    return down && !was;
}
