#include "CharacterApp.h"

#include <engine/core/Paths.h>
#include <engine/animation/Animator.h>

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

namespace {
engine::WindowProps MakeProps(const engine::Config& cfg) {
    engine::WindowProps p;
    p.title  = "Skeletal Animation";
    p.width  = cfg.GetInt("window.width", 1280);
    p.height = cfg.GetInt("window.height", 720);
    p.vsync  = cfg.GetBool("window.vsync", true);
    return p;
}
} // namespace

CharacterApp::CharacterApp(engine::Config& config)
    : engine::Application(MakeProps(config)), m_config(config) {}

std::string CharacterApp::Asset(const std::string& rel) const {
    const std::string beside = engine::ExecutableDir() + "/assets";
    std::error_code ec;
    const std::string root = std::filesystem::exists(beside, ec) ? beside : std::string(ASSETS_DIR);
    return root + "/" + rel;
}

void CharacterApp::OnInit() {
    m_renderer.Init();
    m_model.emplace(engine::SkinnedModel::FromFile(Asset(m_modelName)));
    m_skinned.emplace();
    m_sky.emplace();
    m_text.emplace();
    m_sample = engine::DayNightCycle::At(0.35f);

    m_target = m_model->Center();
    m_dist   = std::max(m_model->BoundingRadius() * 3.0f, 6.0f);
}

void CharacterApp::OnUpdate(float dt) {
    engine::Window& w = GetWindow();
    m_dt = dt;
    if (dt > 0.0f) m_fps = m_fps * 0.92f + (1.0f / dt) * 0.08f;
    if (m_playing) m_time += dt;

    auto press = [&](int key) {
        const bool d = w.IsKeyPressed(key); const bool was = m_keyPrev[key]; m_keyPrev[key] = d; return d && !was;
    };
    if (press(GLFW_KEY_Q) || press(GLFW_KEY_ESCAPE)) w.SetShouldClose(true);
    if (press(GLFW_KEY_F11)) w.ToggleFullscreen();
    if (press(GLFW_KEY_SPACE)) m_playing = !m_playing;
    if (press(GLFW_KEY_C)) m_autoSpin = !m_autoSpin;
    if (press(GLFW_KEY_R)) { m_time = 0.0f; m_yaw = 0.8f; m_pitch = 0.25f; m_dist = std::max(m_model->BoundingRadius()*3.0f, 6.0f); }
    if (press(GLFW_KEY_N) && m_model->AnimationCount() > 1) {   // start a cross-fade to the next clip
        m_prevIndex = m_animIndex;
        m_animIndex = (m_animIndex + 1) % static_cast<int>(m_model->AnimationCount());
        m_blend = 0.0f;
    }
    m_blend = std::min(1.0f, m_blend + dt / 0.4f);              // 0.4 s cross-fade

    const float rot = 1.5f * dt, zoom = 8.0f * dt;
    bool manual = false;
    if (w.IsKeyPressed(GLFW_KEY_LEFT)  || w.IsKeyPressed(GLFW_KEY_A)) { m_yaw -= rot; manual = true; }
    if (w.IsKeyPressed(GLFW_KEY_RIGHT) || w.IsKeyPressed(GLFW_KEY_D)) { m_yaw += rot; manual = true; }
    if (w.IsKeyPressed(GLFW_KEY_UP)    || w.IsKeyPressed(GLFW_KEY_W)) m_pitch += rot;
    if (w.IsKeyPressed(GLFW_KEY_DOWN)  || w.IsKeyPressed(GLFW_KEY_S)) m_pitch -= rot;
    if (w.IsKeyPressed(GLFW_KEY_Z)) m_dist -= zoom;
    if (w.IsKeyPressed(GLFW_KEY_X)) m_dist += zoom;
    if (m_autoSpin && !manual) m_yaw += 0.4f * dt;
    m_pitch = std::clamp(m_pitch, -1.45f, 1.45f);
    m_dist  = std::clamp(m_dist, 2.0f, 60.0f);
}

void CharacterApp::OnRender() {
    engine::Window& w = GetWindow();
    const float aspect = w.AspectRatio();
    m_renderer.SetClearColor(glm::vec4(0.05f, 0.06f, 0.09f, 1.0f));
    m_renderer.Clear();

    const glm::vec3 dir(std::cos(m_pitch) * std::cos(m_yaw), std::sin(m_pitch), std::cos(m_pitch) * std::sin(m_yaw));
    engine::Camera cam(m_target + dir * m_dist);
    cam.LookAt(m_target);
    const glm::mat4 view = cam.ViewMatrix();
    const glm::mat4 proj = cam.ProjectionMatrix(aspect);

    m_sky->Draw(view, proj, m_sample, true);

    // Pose the skeleton from the current clip (or the bind pose if there are none).
    if (m_model->AnimationCount() == 0) {
        engine::Animator::ComputeBindPose(m_model->GetSkeleton(), m_bones);
    } else if (m_blend < 1.0f && m_prevIndex != m_animIndex) {   // cross-fading between two clips
        engine::Animator::ComputeBlendedPose(
            m_model->GetSkeleton(),
            m_model->Animations()[static_cast<std::size_t>(m_prevIndex)], m_time,
            m_model->Animations()[static_cast<std::size_t>(m_animIndex)], m_time,
            m_blend, m_bones);
    } else {
        engine::Animator::ComputePose(m_model->GetSkeleton(),
                                      m_model->Animations()[static_cast<std::size_t>(m_animIndex)],
                                      m_time, m_bones);
    }

    m_skinned->Draw(*m_model, m_bones, glm::mat4(1.0f), cam, aspect,
                    m_sample.keyLightDirection, m_sample.keyLightColor, m_sample.ambient + glm::vec3(0.15f));

    // HUD.
    const int ww = w.Width(), hh = w.Height();
    m_text->Begin(ww, hh);
    const glm::vec3 white(1.0f), grey(0.7f);
    char buf[128];
    const char* animName = (m_model->AnimationCount() > 0)
        ? m_model->Animations()[static_cast<std::size_t>(m_animIndex)].name.c_str() : "(bind pose)";
    std::snprintf(buf, sizeof(buf), "SKELETAL ANIMATION   bones %zu   clip '%s'   %.1fs   %.0f fps",
                  m_model->BoneCount(), animName, m_time, m_fps);
    m_text->Text(buf, 24.0f, 22.0f, 2.0f, white);
    m_text->Text("A/D orbit  W/S tilt  Z/X zoom  SPACE play/pause  N next clip  C spin  R reset  Q quit",
                 24.0f, hh - 32.0f, 1.4f, grey);
    m_text->End();
}

void CharacterApp::OnShutdown() {
    m_config.Set("window.vsync", GetWindow().IsVSync());
    m_config.Save();
}
