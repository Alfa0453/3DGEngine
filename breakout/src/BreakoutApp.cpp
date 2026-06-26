// Breakout, implemented on the engine's ECS. Bricks/ball/paddle/walls are
// entities (Transform + MeshRenderer); movement, collision and rendering are
// systems. Completely independent of the Pong game.
#include "BreakoutApp.h"

#include <engine/core/Paths.h>
#include <engine/graphics/Primitives.h>
#include <engine/ecs/Systems.h>         // engine::ecs::RenderMeshes

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <vector>

using engine::ecs::Entity;
using engine::ecs::Transform;
using engine::ecs::MeshRenderer;

namespace {

// Field layout (world units, XZ plane; +Z is toward the player/paddle).
constexpr float kWallX      = 8.0f;     // side walls
constexpr float kTopZ       = 7.5f;     // top wall
constexpr float kPaddleZ    = 6.5f;     // paddle line
constexpr float kBottomZ    = 8.5f;     // below this the ball is lost
constexpr float kBallSpeed  = 12.0f;
constexpr float kBallHalf   = 0.30f;

struct AABB { glm::vec3 c, h; };
AABB BoxOf(const Transform& t) { return { t.position, t.scale * 0.5f }; }
bool Overlap(const AABB& a, const AABB& b) {
    return std::abs(a.c.x - b.c.x) <= a.h.x + b.h.x
        && std::abs(a.c.y - b.c.y) <= a.h.y + b.h.y
        && std::abs(a.c.z - b.c.z) <= a.h.z + b.h.z;
}

engine::WindowProps MakeProps(const engine::Config& cfg) {
    engine::WindowProps p;
    p.title  = "Breakout";
    p.width  = cfg.GetInt("window.width", 1280);
    p.height = cfg.GetInt("window.height", 720);
    p.vsync  = cfg.GetBool("window.vsync", true);
    return p;
}

} // anonymous namespace

BreakoutApp::BreakoutApp(engine::Config &config)
    : engine::Application(MakeProps(config)), m_config(config)
{
    m_bestScore = m_config.GetInt("stats.bestScore", 0);
}

void BreakoutApp::OnInit()
{
    m_renderer.Init();
    m_renderer.SetClearColor({0.03f, 0.04f, 0.06f, 1.0f});

    m_cube.emplace(engine::primitives::Cube());
    m_sphere.emplace(engine::primitives::Sphere(18));
    m_shader.emplace(engine::Shader::FromFiles(
        Asset("shaders/object.vert"), Asset("shaders/object.frag")
    ));
    m_text.emplace();
    m_particles.emplace();

    m_audio.emplace();
    m_sndBounce = Asset("sounds/bounce.wav");
    m_sndBrick  = Asset("sounds/brick.wav");
    m_sndLose   = Asset("sounds/lose.wav");
    m_sndWin    = Asset("sounds/win.wav");
    m_audio->Preload(m_sndBounce);
    m_audio->Preload(m_sndBrick);
    m_audio->Preload(m_sndLose);
    m_audio->Preload(m_sndWin);

    // Look down the field from above and behind the paddle.
    m_camera.LookAt(glm::vec3(0.0f, 0.0f, -1.0f));

    // Static walls + floor (entities, drawn by the render system; collision uses
    // the constants above, not these meshes).
    const glm::vec3 wallCol(0.35f, 0.37f, 0.45f);
    auto wall = [&](glm::vec3 pos, glm::vec3 scale)
    {
        Entity e = m_reg.Create();
        m_reg.Add<Transform>(e, Transform{pos, scale});
        m_reg.Add<MeshRenderer>(e, MeshRenderer{&*m_cube, wallCol});
    };
    wall({-kWallX - 0.3f, 0.0f, -0.5f}, {0.6f, 1.0f, 2.0f * kTopZ + 3.0f});
    wall({ kWallX + 0.3f, 0.0f, -0.5f}, {0.6f, 1.0f, 2.0f * kTopZ + 3.0f});
    wall({ 0.0f, 0.0f, -kTopZ - 0.3f}, {2.0f * kWallX + 1.2f, 1.0f, 0.6f});
    {   // floor
        Entity e = m_reg.Create();
        m_reg.Add<Transform>(e, Transform{{0.0f, -0.6f, 0.0f},
                                          {2.0f * kWallX + 1.0f, 0.2f, 2.0f * kTopZ + 4.0f}});
        m_reg.Add<MeshRenderer>(e, MeshRenderer{&*m_cube, glm::vec3(0.10f, 0.11f, 0.15f)});
    }

    // Paddle and ball.
    m_paddle = m_reg.Create();
    m_reg.Add<Transform>(m_paddle, Transform{{0.0f, 0.0f, kPaddleZ}, {2.8f, 0.6f, 0.5f}});
    m_reg.Add<MeshRenderer>(m_paddle, MeshRenderer{&*m_cube, glm::vec3(0.35f, 0.7f, 1.0f)});

    m_ball = m_reg.Create();
    m_reg.Add<Transform>(m_ball, Transform{{0.0f, 0.0f, kPaddleZ - 1.0f}, glm::vec3(2.0f * kBallHalf)});
    m_reg.Add<MeshRenderer>(m_ball, MeshRenderer{&*m_sphere, glm::vec3(1.0f, 0.95f, 0.4f)});
    m_reg.Add<bo::Velocity>(m_ball, {});

    BuildLevel();
    ResetBall();

    if (m_config.GetBool("window.fullscreen", false))
        GetWindow().ToggleFullscreen();
}

void BreakoutApp::OnShutdown()
{
    m_config.Set("window.width",      m_config.GetInt("window.width", 1280));
    m_config.Set("window.height",     m_config.GetInt("window.height", 720));
    m_config.Set("window.vsync",      GetWindow().IsVSync());
    m_config.Set("window.fullscreen", GetWindow().IsFullscreen());
    m_config.Set("stats.bestScore",   m_bestScore);
    m_config.Save();
}

void BreakoutApp::OnUpdate(float dt)
{
    engine::Window& w = GetWindow();
    const bool eSpace = Pressed(GLFW_KEY_SPACE);
    if (w.IsKeyPressed(GLFW_KEY_Q) || Pressed(GLFW_KEY_ESCAPE)) w.SetShouldClose(true);
    if (Pressed(GLFW_KEY_F11)) w.ToggleFullscreen();

    m_time += dt;
    if (eSpace) {
        if (m_state == State::Ready) m_launch = true;
        else if (m_state == State::GameOver || m_state == State::Win) {
            m_score = 0; m_lives = 3;
            BuildLevel();
            ResetBall();
        }
    }

    m_particles->Update(dt);
    m_shake = std::max(0.0f, m_shake - dt * 1.5f);
}

void BreakoutApp::OnFixedUpdate(float dt)
{
    if (m_state == State::Ready) {
        MovePaddle(dt);
        Transform& bt = m_reg.Get<Transform>(m_ball);
        Transform& pt = m_reg.Get<Transform>(m_paddle);
        bt.position = glm::vec3(pt.position.x, 0.0f, pt.position.z - 1.0f);
        m_ballPrev = bt.position;
        if (m_launch) {
            // Launch up the field with a guaranteed diagonal (never vertical).
            const float sign = (Randf(0.0f, 1.0f) < 0.5f) ? -1.0f : 1.0f;
            const float ax   = sign * Randf(0.40f, 0.65f);
            m_reg.Get<bo::Velocity>(m_ball).value =
                glm::normalize(glm::vec3(ax, 0.0f, -1.0f)) * kBallSpeed;
            m_state = State::Playing;
            m_launch = false;
        }
        return;
    }
    if (m_state == State::Playing) {
        MovePaddle(dt);
        StepBall(dt);
    }
    // GameOver / Win: simulation frozen.
}

void BreakoutApp::OnRender()
{
    m_renderer.Clear();

    glm::mat4 view = m_camera.ViewMatrix();
    if (m_shake > 0.0f) {
        const float s = m_shake * m_shake;
        const glm::vec3 off(Randf(-1.0f, 1.0f) * 0.2f * s, Randf(-1.0f, 1.0f) * 0.2f * s, 0.0f);
        const float ang = Randf(-1.0f, 1.0f) * 0.03f * s;
        view = glm::rotate(glm::translate(glm::mat4(1.0f), off), ang, glm::vec3(0, 0, 1)) * view;
    }
    m_viewProj = m_camera.ProjectionMatrix(GetWindow().AspectRatio()) * view;

    m_shader->Bind();
    m_shader->SetMat4("uViewProj",   m_viewProj);
    m_shader->SetVec3("uLightPos",   glm::vec3(0.0f, 16.0f, 6.0f));
    m_shader->SetVec3("uLightColor", glm::vec3(1.0f, 0.97f, 0.92f));
    m_shader->SetVec3("uViewPos",    m_camera.Position());

    // Interpolate the ball between fixed steps (transiently), then restore the
    // authoritative position so the next physics step is unaffected.
    Transform& bt = m_reg.Get<Transform>(m_ball);
    const glm::vec3 real = bt.position;
    bt.position = glm::mix(m_ballPrev, real, InterpolationAlpha());
    engine::ecs::RenderMeshes(m_reg, m_renderer, *m_shader);
    bt.position = real;

    const float pointScale = GetWindow().Height() * 0.5f
                            / std::tan(glm::radians(m_camera.fov) * 0.5f);
    m_particles->Render(m_viewProj, pointScale);

    DrawHud();
}

std::string BreakoutApp::Asset(const std::string &rel) const
{
    static const std::string root = []
    {
        const std::string beside = engine::ExecutableDir() + "/assets";
        std::error_code ec;
        if (std::filesystem::exists(beside, ec)) return beside;
        return std::string(ASSETS_DIR);
    }();
    return root + "/" + rel;
}

void BreakoutApp::BuildLevel()
{
    // Remove any existing bricks (for a restart).
    std::vector<Entity> old;
    m_reg.view<bo::Brick>().each([&](Entity e, bo::Brick&) { old.push_back(e); });
    for (Entity e : old) m_reg.Destroy(e);

    const int   cols = 11, rows = 5;
    const float bw = 1.3f, bd = 0.7f, gx = 0.12f, gz = 0.14f;
    const float startX = -((cols - 1) * (bw + gx)) * 0.5f;
    const float startZ = -5.2f;
    const glm::vec3 rowCol[5] = {
        {0.90f, 0.30f, 0.30f}, {0.95f, 0.60f, 0.25f}, {0.90f, 0.90f, 0.30f},
        {0.40f, 0.85f, 0.45f}, {0.45f, 0.70f, 1.00f},
    };
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c) {
            const glm::vec3 pos(startX + c * (bw + gx), 0.0f, startZ + r * (bd + gz));
            Entity e = m_reg.Create();
            m_reg.Add<Transform>(e, Transform{pos, glm::vec3(bw, 0.6f, bd)});
            m_reg.Add<MeshRenderer>(e, MeshRenderer{&*m_cube, rowCol[r]});
            m_reg.Add<bo::Brick>(e, bo::Brick{1});
        }
    m_bricksLeft = cols * rows;
}

void BreakoutApp::ResetBall()
{
    Transform& bt = m_reg.Get<Transform>(m_ball);
    Transform& pt = m_reg.Get<Transform>(m_paddle);
    bt.position = glm::vec3(pt.position.x, 0.0f, pt.position.z - 1.0f);
    m_ballPrev  = bt.position;
    m_reg.Get<bo::Velocity>(m_ball).value = glm::vec3(0.0f);
    m_state  = State::Ready;
    m_launch = false;
}

void BreakoutApp::MovePaddle(float dt)
{
    engine::Window& w = GetWindow();
    const float speed = 14.0f * dt;
    Transform& t = m_reg.Get<Transform>(m_paddle);
    if (w.IsKeyPressed(GLFW_KEY_LEFT)  || w.IsKeyPressed(GLFW_KEY_A)) t.position.x -= speed;
    if (w.IsKeyPressed(GLFW_KEY_RIGHT) || w.IsKeyPressed(GLFW_KEY_D)) t.position.x += speed;
    const float limit = kWallX - t.scale.x * 0.5f;
    t.position.x = std::clamp(t.position.x, -limit, limit);
}

void BreakoutApp::StepBall(float dt)
{
    Transform&    bt = m_reg.Get<Transform>(m_ball);
    bo::Velocity& bv = m_reg.Get<bo::Velocity>(m_ball);
    m_ballPrev = bt.position;
    bt.position += bv.value * dt;

    // Side and top walls.
    if (bt.position.x - kBallHalf < -kWallX) {
        bt.position.x = -kWallX + kBallHalf; bv.value.x = std::abs(bv.value.x);
        m_audio->Play(m_sndBounce, Randf(0.95f, 1.1f));
    } else if (bt.position.x + kBallHalf > kWallX) {
        bt.position.x = kWallX - kBallHalf; bv.value.x = -std::abs(bv.value.x);
        m_audio->Play(m_sndBounce, Randf(0.95f, 1.1f));
    }
    if (bt.position.z - kBallHalf < -kTopZ) {
        bt.position.z = -kTopZ + kBallHalf; bv.value.z = std::abs(bv.value.z);
        m_audio->Play(m_sndBounce, Randf(0.95f, 1.1f));
    }

    // Paddle: reflect upward, steered by where it struck. We always keep BOTH a
    // strong upward AND a real sideways component, so the ball can never settle
    // into a pure-vertical drill or a horizontal wall-to-wall stall.
    Transform& pt = m_reg.Get<Transform>(m_paddle);
    if (bv.value.z > 0.0f && Overlap(BoxOf(bt), BoxOf(pt))) {
        const float off  = std::clamp((bt.position.x - pt.position.x) / (pt.scale.x * 0.5f), -1.0f, 1.0f);
        const float sign = (off != 0.0f) ? (off < 0.0f ? -1.0f : 1.0f)
                                         : (Randf(0.0f, 1.0f) < 0.5f ? -1.0f : 1.0f);
        const float ax   = sign * std::clamp(std::abs(off), 0.45f, 1.0f) * 0.9f; // |x| in 0.40..0.90
        bv.value = glm::normalize(glm::vec3(ax, 0.0f, -1.0f)) * kBallSpeed;
        bt.position.z = pt.position.z - (pt.scale.z * 0.5f + kBallHalf);
        m_audio->Play(m_sndBounce, Randf(0.95f, 1.1f));
    }

    // Bricks: find the first overlapping brick, reflect off the shallow axis,
    // then resolve the hit AFTER the loop (never mutate the pool mid-iteration).
    const AABB ball = BoxOf(bt);
    Entity hit = engine::ecs::kNull;
    glm::vec3 hitPos(0.0f);
    m_reg.view<Transform, bo::Brick>().each([&](Entity e, Transform& t, bo::Brick&){
        if (hit != engine::ecs::kNull) return;
        const AABB b = BoxOf(t);
        if (!Overlap(ball, b)) return;
        hit = e; hitPos = t.position;
        const float dx = ball.c.x - b.c.x, dz = ball.c.z - b.c.z;
        const float ox = (ball.h.x + b.h.x) - std::abs(dx);
        const float oz = (ball.h.z + b.h.z) - std::abs(dz);
        if (ox < oz) bv.value.x = (dx < 0.0f ? -1.0f : 1.0f) * std::abs(bv.value.x);
        else         bv.value.z = (dz < 0.0f ? -1.0f : 1.0f) * std::abs(bv.value.z);
    });
    if (hit != engine::ecs::kNull) {
        const glm::vec3 col = m_reg.Get<MeshRenderer>(hit).color;
        bo::Brick& br = m_reg.Get<bo::Brick>(hit);
        if (--br.hp <= 0) {
            m_reg.Destroy(hit);
            --m_bricksLeft;
            m_score += 10;
            m_particles->EmitBurst(hitPos, col, 40, 5.0f, 0.6f, 0.18f);
            m_shake = std::min(1.0f, m_shake + 0.18f);
        }
        m_audio->Play(m_sndBrick, Randf(0.95f, 1.15f));
        if (m_bricksLeft <= 0) {
            m_state = State::Win;
            m_bestScore = std::max(m_bestScore, m_score);
            m_audio->Play(m_sndWin);
        }
    }

    // Past the bottom: lose a life.
    if (bt.position.z - kBallHalf > kBottomZ) {
        --m_lives;
        m_audio->Play(m_sndLose);
        m_shake = std::min(1.0f, m_shake + 0.6f);
        if (m_lives <= 0) {
            m_state = State::GameOver;
            m_bestScore = std::max(m_bestScore, m_score);
        } else {
            ResetBall();
        }
    }
}

void BreakoutApp::DrawHud()
{
    engine::Window& w = GetWindow();
    const int ww = w.Width(), hh = w.Height();
    m_text->Begin(ww, hh);

    const glm::vec3 white(1.0f), grey(0.6f), yellow(1.0f, 0.92f, 0.4f);
    auto center = [&](const std::string& s, float y, float sc, glm::vec3 c) {
        m_text->Text(s, (ww - m_text->Measure(s, sc)) * 0.5f, y, sc, c);
    };

    char buf[96];
    std::snprintf(buf, sizeof(buf), "SCORE %d", m_score);
    m_text->Text(buf, 24.0f, 24.0f, 2.0f, white);
    std::snprintf(buf, sizeof(buf), "LIVES %d", m_lives);
    m_text->Text(buf, ww -24.0f - m_text->Measure(buf, 2.0f), 24.0f, 2.0f, white);

    if (m_state == State::Ready) {
        center("PRESS SPACE TO LAUNCH", hh * 0.62f, 2.0f, yellow);
    } else if (m_state == State::GameOver) {
        center("GAME OVER", hh * 0.42f, 4.0f, glm::vec3(1.0f, 0.4f, 0.4f));
        std::snprintf(buf, sizeof(buf), "SCORE %d    BEST %d", m_score, m_bestScore);
        center("SPACE TO RESTART", hh * 0.60f, 1.6f, yellow);
    } else if (m_state == State::Win) {
        center("YOU WIN!", hh * 0.42f, 4.0f, glm::vec3(0.5f, 1.0f, 0.6f));
        std::snprintf(buf, sizeof(buf), "SCORE %d    BEST %d", m_score, m_bestScore);
        center(buf, hh * 0.52f, 1.8f, grey);
        center("SPACE TO PLAY AGAIN", hh * 0.60f, 1.6f, yellow);
    }

    center("A / D or LEFT / RIGHT to move", hh - 40.0f, 1.5f, grey);
    m_text->End();
}

bool BreakoutApp::Pressed(int key)
{
    const bool down =GetWindow().IsKeyPressed(key);
    const bool was  = m_keyPrev[key];
    m_keyPrev[key]  = down;
    return down && !was;
}

float BreakoutApp::Randf(float lo, float hi)
{
    std::uniform_real_distribution<float> d(lo, hi);
    return d(m_rng);
}
