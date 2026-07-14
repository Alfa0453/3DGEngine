#include "ParticleDemoApp.h"

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
using engine::EmitShape;
using engine::ParticleBlend;

namespace {
engine::WindowProps MakeProps(const engine::Config& cfg) {
    engine::WindowProps p;
    p.title  = "Particle Demo — magic / fire / smoke / sparks (HDR + bloom)";
    p.width  = cfg.GetInt("window.width", 1280);
    p.height = cfg.GetInt("window.height", 720);
    p.vsync  = cfg.GetBool("window.vsync", true);
    return p;
}
} // namespace

ParticleDemoApp::ParticleDemoApp(engine::Config& config)
    : engine::Application(MakeProps(config)), m_config(config) {}

void ParticleDemoApp::OnInit() {
    m_renderer.Init();
    m_plane.emplace(engine::primitives::Plane(1.0f, 16.0f));
    m_cube.emplace(engine::primitives::Cube());
    m_pbr.emplace(2048);
    m_sky.emplace();
    m_post.emplace(GetWindow().Width(), GetWindow().Height());
    m_text.emplace();
    m_particles.emplace();
    m_sample = engine::DayNightCycle::At(0.86f);         // dusk, so the glow reads
    m_ibl.emplace(256);
    m_ibl->Generate([&](const glm::mat4& v, const glm::mat4& p) { m_sky->Draw(v, p, m_sample, false); });

    BuildScene();
    GetWindow().SetCursorCaptured(false);
}

void ParticleDemoApp::BuildScene() {
    // Ground + two pedestals for the fire and magic.
    {
        Entity e = m_reg.Create();
        Transform t; t.scale = glm::vec3(30.0f, 1.0f, 30.0f);
        m_reg.Add<Transform>(e, t);
        PbrMaterial m; m.albedo = glm::vec3(0.18f, 0.19f, 0.22f); m.roughness = 0.95f;
        m_reg.Add<MeshPBR>(e, MeshPBR{&*m_plane, m});
    }
    auto pedestal = [&](glm::vec3 pos) {
        Entity e = m_reg.Create();
        Transform t; t.position = pos + glm::vec3(0, 0.4f, 0); t.scale = glm::vec3(1.2f, 0.8f, 1.2f);
        m_reg.Add<Transform>(e, t);
        PbrMaterial m; m.albedo = glm::vec3(0.25f, 0.24f, 0.26f); m.roughness = 0.8f;
        m_reg.Add<MeshPBR>(e, MeshPBR{&*m_cube, m});
    };
    pedestal(glm::vec3(-3.0f, 0, 0));
    pedestal(glm::vec3( 3.0f, 0, 0));

    // Sun (low + warm).
    { Entity s = m_reg.Create(); m_reg.Add<Transform>(s, Transform{});
      Light l; l.type = Light::Type::Directional; l.direction = glm::vec3(-0.3f, -0.8f, -0.5f);
      l.color = glm::vec3(0.7f, 0.6f, 0.8f); l.intensity = 1.2f; m_reg.Add<Light>(s, l); }

    // --- Emitters -----------------------------------------------------------
    // Magic fountain (centre): cyan -> violet, additive, tall narrow cone.
    m_magic.position = glm::vec3(0.0f, 0.2f, 0.0f);
    m_magic.cfg.rate = 220.0f; m_magic.cfg.maxParticles = 4000;
    m_magic.cfg.shape = EmitShape::Cone; m_magic.cfg.direction = glm::vec3(0, 1, 0);
    m_magic.cfg.coneAngleDeg = 16.0f; m_magic.cfg.shapeRadius = 0.15f;
    m_magic.cfg.speedMin = 2.5f; m_magic.cfg.speedMax = 4.5f;
    m_magic.cfg.lifeMin = 1.0f; m_magic.cfg.lifeMax = 1.8f;
    m_magic.cfg.gravity = glm::vec3(0, -1.2f, 0); m_magic.cfg.drag = 0.4f;
    m_magic.cfg.startColor = glm::vec4(0.3f, 1.4f, 2.2f, 1.0f);
    m_magic.cfg.endColor   = glm::vec4(1.6f, 0.3f, 2.0f, 0.0f);
    m_magic.cfg.startSize = 0.22f; m_magic.cfg.endSize = 0.02f;
    m_magic.cfg.blend = ParticleBlend::Additive;

    // Fire on the left pedestal: orange -> red, additive.
    m_fire.position = glm::vec3(-3.0f, 0.9f, 0.0f);
    m_fire.cfg.rate = 260.0f; m_fire.cfg.maxParticles = 3000;
    m_fire.cfg.shape = EmitShape::Cone; m_fire.cfg.direction = glm::vec3(0, 1, 0);
    m_fire.cfg.coneAngleDeg = 22.0f; m_fire.cfg.shapeRadius = 0.25f;
    m_fire.cfg.speedMin = 1.2f; m_fire.cfg.speedMax = 2.6f;
    m_fire.cfg.lifeMin = 0.5f; m_fire.cfg.lifeMax = 1.0f;
    m_fire.cfg.gravity = glm::vec3(0, 1.5f, 0); m_fire.cfg.drag = 1.2f;   // buoyant
    m_fire.cfg.startColor = glm::vec4(2.6f, 1.3f, 0.3f, 1.0f);
    m_fire.cfg.endColor   = glm::vec4(1.8f, 0.15f, 0.0f, 0.0f);
    m_fire.cfg.startSize = 0.35f; m_fire.cfg.endSize = 0.05f;
    m_fire.cfg.blend = ParticleBlend::Additive;

    // Smoke above the fire: grey, alpha-blended, slow rise + expand.
    m_smoke.position = glm::vec3(-3.0f, 1.6f, 0.0f);
    m_smoke.cfg.rate = 40.0f; m_smoke.cfg.maxParticles = 800;
    m_smoke.cfg.shape = EmitShape::Cone; m_smoke.cfg.direction = glm::vec3(0, 1, 0);
    m_smoke.cfg.coneAngleDeg = 25.0f; m_smoke.cfg.shapeRadius = 0.2f;
    m_smoke.cfg.speedMin = 0.6f; m_smoke.cfg.speedMax = 1.1f;
    m_smoke.cfg.lifeMin = 1.6f; m_smoke.cfg.lifeMax = 2.6f;
    m_smoke.cfg.gravity = glm::vec3(0, 0.4f, 0); m_smoke.cfg.drag = 0.8f;
    m_smoke.cfg.startColor = glm::vec4(0.10f, 0.10f, 0.11f, 0.5f);
    m_smoke.cfg.endColor   = glm::vec4(0.20f, 0.20f, 0.22f, 0.0f);
    m_smoke.cfg.startSize = 0.35f; m_smoke.cfg.endSize = 1.4f;
    m_smoke.cfg.blend = ParticleBlend::Alpha;

    // Sparks on the right pedestal: yellow, additive, wide, gravity-pulled.
    m_sparks.position = glm::vec3(3.0f, 0.9f, 0.0f);
    m_sparks.cfg.rate = 0.0f;                     // burst-only (SPACE)
    m_sparks.cfg.maxParticles = 3000;
    m_sparks.cfg.shape = EmitShape::Cone; m_sparks.cfg.direction = glm::vec3(0, 1, 0);
    m_sparks.cfg.coneAngleDeg = 55.0f; m_sparks.cfg.shapeRadius = 0.05f;
    m_sparks.cfg.speedMin = 3.0f; m_sparks.cfg.speedMax = 6.0f;
    m_sparks.cfg.lifeMin = 0.6f; m_sparks.cfg.lifeMax = 1.2f;
    m_sparks.cfg.gravity = glm::vec3(0, -9.0f, 0); m_sparks.cfg.drag = 0.1f;
    m_sparks.cfg.startColor = glm::vec4(3.0f, 2.2f, 0.5f, 1.0f);
    m_sparks.cfg.endColor   = glm::vec4(2.0f, 0.4f, 0.0f, 0.0f);
    m_sparks.cfg.startSize = 0.09f; m_sparks.cfg.endSize = 0.02f;
    m_sparks.cfg.blend = ParticleBlend::Additive;
}

void ParticleDemoApp::OnUpdate(float dt) {
    engine::Window& w = GetWindow();
    m_dt = dt;
    if (dt > 0.0f) m_fps = m_fps * 0.92f + (1.0f / dt) * 0.08f;
    if (w.IsKeyPressed(GLFW_KEY_ESCAPE)) w.SetShouldClose(true);
    if (w.IsKeyPressed(GLFW_KEY_SPACE)) m_sparks.Burst(24);   // continuous while held

    // Camera orbit.
    const float rot = 1.5f * dt;
    if (w.IsKeyPressed(GLFW_KEY_LEFT))  m_camYaw   -= rot;
    if (w.IsKeyPressed(GLFW_KEY_RIGHT)) m_camYaw   += rot;
    if (w.IsKeyPressed(GLFW_KEY_UP))    m_camPitch -= rot;
    if (w.IsKeyPressed(GLFW_KEY_DOWN))  m_camPitch += rot;
    if (w.IsKeyPressed(GLFW_KEY_Z)) m_camDist -= 10.0f * dt;
    if (w.IsKeyPressed(GLFW_KEY_X)) m_camDist += 10.0f * dt;
    m_camPitch = glm::clamp(m_camPitch, 0.05f, 1.4f);
    m_camDist  = glm::clamp(m_camDist, 4.0f, 40.0f);

    m_magic.Update(dt);
    m_fire.Update(dt);
    m_smoke.Update(dt);
    m_sparks.Update(dt);
}

void ParticleDemoApp::OnRender() {
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
    opt.ambient = m_sample.ambient;
    opt.tonemap = false;
    opt.ibl     = &*m_ibl;
    opt.fog     = false;
    opt.pointShadows = false;
    m_pbr->Render(m_reg, cam, aspect, w.Width(), w.Height(), opt);
    m_sky->Draw(cam.ViewMatrix(), cam.ProjectionMatrix(aspect), m_sample, false);

    // Particles into the HDR buffer (after opaques + sky) -> bloom picks up the glow.
    m_particles->Draw(m_smoke, cam, aspect);   // alpha first
    m_particles->Draw(m_magic, cam, aspect);
    m_particles->Draw(m_fire,  cam, aspect);
    m_particles->Draw(m_sparks, cam, aspect);

    m_post->RenderToScreen(w.Width(), w.Height(), m_dt);

    const int ww = w.Width(), hh = w.Height();
    m_text->Begin(ww, hh);
    char buf[128];
    const std::size_t total = m_magic.Alive() + m_fire.Alive() + m_smoke.Alive() + m_sparks.Alive();
    std::snprintf(buf, sizeof(buf), "PARTICLES   live %zu   %.0f fps", total, m_fps);
    m_text->Text(buf, 24.0f, 22.0f, 2.0f, glm::vec3(1.0f));
    m_text->Text("SPACE spark burst   arrows orbit   Z/X zoom   Esc quit",
                 24.0f, hh - 32.0f, 1.4f, glm::vec3(0.75f));
    m_text->End();
}

void ParticleDemoApp::OnShutdown() {
    m_config.Set("window.vsync", GetWindow().IsVSync());
    m_config.Save();
}
