#include "PongApp.h"

#include <engine/core/Paths.h>

#include <filesystem>

#include <engine/graphics/Primitives.h>

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

// Field layout (world units).
constexpr float kPaddleX   = 7.0f;   // |x| of each paddle plane
constexpr float kWallZ     = 5.0f;   // |z| of each side wall (ball bounces here)
constexpr float kPaddleBaseHalfZ = 1.4f;
constexpr float kBallBaseHalf    = 0.35f;

// Difficulty presets: AI tracking speed, wall-aim chance, and ball speed.
struct Diff { const char* name; float aiSpeed; float aiWall; float ballSpeed; };
const Diff kDiffs[3] = {
    { "EASY",   5.0f, 0.15f,  8.0f },
    { "NORMAL", 6.5f, 0.40f,  9.0f },
    { "HARD",   8.5f, 0.65f, 10.5f },
};

enum PuType { PU_Grow, PU_BigBall, PU_Slow, PU_COUNT };

// Build the window settings from config (with sensible fallbacks).
engine::WindowProps MakeProps(const engine::Config& cfg) {
    engine::WindowProps p;
    p.title  = "3D Pong";
    p.width  = cfg.GetInt("window.width", 1280);
    p.height = cfg.GetInt("window.height", 720);
    p.vsync  = cfg.GetBool("window.vsync", true);
    return p;
}

// Resolve an asset path. A packaged build finds "assets/" next to the
// executable; during development we fall back to ASSETS_DIR (the absolute source
// path baked in by CMake), so running from the build tree also works.
const std::string& AssetsRoot() {
    static const std::string root = [] {
        const std::string beside = engine::ExecutableDir() + "/assets";
        std::error_code ec;
        if (std::filesystem::exists(beside, ec)) return beside;
        return std::string(ASSETS_DIR);
    }();
    return root;
}
std::string Asset(const std::string& rel) { return AssetsRoot() + "/" + rel; }

} // anonymous namespace

PongApp::PongApp(engine::Config& config)
    : engine::Application(MakeProps(config)),
      m_config(config) {
    // Apply settings that affect the engine before the loop starts.
    m_fixedRate = m_config.GetInt("physics.rate", 120);
    m_volume    = m_config.GetFloat("audio.volume", 1.0f);
    m_maxScore  = m_config.GetInt("game.maxScore", 5);
    m_difficulty = std::clamp(m_config.GetInt("game.difficulty", 1), 0, 2);
    m_bestStreak = m_config.GetInt("stats.bestStreak", 0);
    m_musicVolume = m_config.GetFloat("audio.musicVolume", 0.5f);
    ApplyDifficulty();    // sets aiSpeed / aiWallChance / ballSpeed from the preset
    SetFixedTimeStep(1.0f / static_cast<float>(m_fixedRate));
}

void PongApp::OnInit()
{
    m_renderer.Init();
    m_renderer.SetClearColor({0.03f, 0.03f, 0.05f, 1.0f});

    m_cube.emplace(engine::primitives::Cube());
    m_ballMesh.emplace(engine::primitives::Sphere(20));
    
    m_shader.emplace(engine::Shader::FromFiles(
        Asset("/shaders/lit.vert"),
        Asset("/shaders/phong_color.frag")));
    m_ballShader.emplace(engine::Shader::FromFiles(
        Asset("/shaders/lit.vert"),
        Asset("/shaders/ball.frag")));

    // A fixed, angled camera looking down at the centre of the field.
    m_camera.LookAt(glm::vec3(0.0f, 0.0f, 0.0f));

    // Static objects.
    m_floor   = { { 0.0f, -0.7f, 0.0f}, {kPaddleX + 1.0f, 0.1f, kWallZ + 1.0f}, {0.13f, 0.15f, 0.20f} };
    m_wallTop = { { 0.0f,  0.0f, -kWallZ - 0.3f}, {kPaddleX + 0.6f, 0.5f, 0.3f}, {0.55f, 0.57f, 0.65f} };
    m_wallBot = { { 0.0f,  0.0f,  kWallZ + 0.3f}, {kPaddleX + 0.6f, 0.5f, 0.3f}, {0.55f, 0.57f, 0.65f} };
    m_player  = { {-kPaddleX, 0.0f, 0.0f}, {0.3f, 0.5f, 1.4f}, {0.30f, 0.70f, 1.00f} };
    m_ai      = { { kPaddleX, 0.0f, 0.0f}, {0.3f, 0.5f, 1.4f}, {1.00f, 0.50f, 0.30f} };
    m_ball.half  = glm::vec3(0.35f);
    m_ball.color = glm::vec3(1.0f, 0.95f, 0.4f);

    ResetBall(+1);
    m_text.emplace();
    m_particles.emplace();

    m_audio.emplace();
    m_sndBounce = Asset("/sounds/bounce.wav");
    m_sndWall   = Asset("/sounds/wall.wav");
    m_sndScore  = Asset("/sounds/score.wav");
    m_sndPower  = Asset("/sounds/powerup.wav");
    m_music     = Asset("/sounds/music.wav");
    m_audio->Preload(m_sndBounce);
    m_audio->Preload(m_sndWall);
    m_audio->Preload(m_sndScore);
    m_audio->SetMasterVolume(m_volume);
    if (m_config.GetBool("audio.music", true))
        m_audio->PlayMusic(m_music, m_musicVolume);

    if (m_config.GetBool("window.fullscreen", false))
        GetWindow().ToggleFullscreen();

    m_state = State::Menu;
}

void PongApp::OnShutdown()
{
    // Persist runtime choices so they survive a restart.
    m_config.Set("window.width",      m_config.GetInt("window.width", 1280));
    m_config.Set("window.height",     m_config.GetInt("window.height", 720));
    m_config.Set("window.vsync",      GetWindow().IsVSync());
    m_config.Set("window.fullscreen", GetWindow().IsFullscreen());
    m_config.Set("physics.rate",      m_fixedRate);
    m_config.Set("audio.volume",      m_volume);
    m_config.Set("game.maxScore",     m_maxScore);
    m_config.Set("game.difficulty",   m_difficulty);
    m_config.Set("stats.bestStreak",  m_bestStreak);
    m_config.Set("audio.musicVolume", m_musicVolume);
    m_config.Save();
}

void PongApp::OnUpdate(float dt)
{
    // Per-frame, variable-rate: just input. Physics lives in OnFixedUpdate.
    engine::Window& window = GetWindow();

    // Compute key rising-edges once per frame, so edge state stays consistent
    // no matter which game state consumes them.
    const bool eEsc   = Pressed(GLFW_KEY_ESCAPE);
    const bool eSpace = Pressed(GLFW_KEY_SPACE);
    const bool eUp    = Pressed(GLFW_KEY_UP);
    const bool eDown  = Pressed(GLFW_KEY_DOWN);
    const bool eEnter = Pressed(GLFW_KEY_ENTER);
    const bool e1     = Pressed(GLFW_KEY_1);
    const bool e2     = Pressed(GLFW_KEY_2);
    const bool eM     = Pressed(GLFW_KEY_M);
    const bool eLeft  = Pressed(GLFW_KEY_LEFT);
    const bool eRight = Pressed(GLFW_KEY_RIGHT);
    
    if (window.IsKeyPressed(GLFW_KEY_Q)) window.SetShouldClose(true);
    if (Pressed(GLFW_KEY_F11)) window.ToggleFullscreen();
    if (Pressed(GLFW_KEY_V))   window.ToggleVSync();
    if (Pressed(GLFW_KEY_F3))  m_showDebug = !m_showDebug;

    // Smoothed perf numbers + per-frame fixed-step counter reset.
    const float instFps = (dt > 0.0f) ? 1.0f / dt : 0.0f;
    m_fps     += (instFps          - m_fps)     * 0.1f;   // exponential moving average
    m_frameMs += (dt * 1000.0f     - m_frameMs) * 0.1f;
    m_fixedSteps = 0;   // reset; this frame's fixed steps are counted next, before OnRender

    if (m_state == State::Menu) {
        // Choose game mode. Up/Down highlight; Enter/Space confirm; 1/2 jump.
        if (eUp)    m_menuIndex = 0;
        if (eDown)  m_menuIndex = 1;
        if (eLeft)  m_difficulty = (m_difficulty + 2) % 3;
        if (eRight) m_difficulty = (m_difficulty + 1) % 3;
        if (e1)             { m_menuIndex = 0; StartMatch(); }
        else if (e2)        { m_menuIndex = 1; StartMatch(); }
        else if (eEnter || eSpace) StartMatch();
        m_particles->Update(dt);    // let any leftover sparks fade
        return;
    }

    // Esc pauses during play; M returns to the menu from the game-over screen.
    if ((m_state == State::Serving || m_state == State::Playing) && eEsc)
        m_paused = !m_paused;
    if (m_state == State::GameOver && eM)
        m_state = State::Menu;

    if (eSpace) m_spaceRequested = true;   // serve / play-again intent

    if (!m_paused) {
        m_time += dt;
        m_particles->Update(dt);
        m_shake = std::max(0.0f, m_shake - dt * 1.6f);  // screen-shake decays away
    }
}

void PongApp::StartMatch()
{
    m_mode = (m_menuIndex == 0) ? Mode::VsAI : Mode::VsPlayer;
    ApplyDifficulty();
    m_playerScore = m_aiScore = 0;
    // Clear any power-up effects from a previous match.
    m_puActive = false;
    m_growTimerL = m_growTimerR = m_bigBallTimer = m_slowTimer = 0.0f;
    m_player.half.z = kPaddleBaseHalfZ;
    m_ai.half.z     = kPaddleBaseHalfZ;
    m_ball.half     = glm::vec3(kBallBaseHalf);
    m_puSpawnTimer  = Randf(4.0f, 7.0f);
    ResetBall(+1);  // sets state = Serving
}

bool PongApp::Pressed(int key) 
{
    const bool down = GetWindow().IsKeyPressed(key);
    const bool was  = m_keyPrev[key];
    m_keyPrev[key]  = down;
    return down && !was;
}

void PongApp::OnFixedUpdate(float dt)
{
    if (m_paused || m_state == State::Menu) return;     // frozen in menu/pause
    ++m_fixedSteps;     // how many sim steps this rendered frame (for the overlay)
    UpdatePowerUps(dt);
    // Fixed-rate simulation: deterministic, independent of frame rate.
    switch (m_state)
    {
    case State::Serving:
        if (m_spaceRequested) {
            m_ballVel = glm::vec3(m_serveDir * m_ballSpeed, 0.0f, Randf(-2.0f, 2.0f));
            m_state = State::Playing;
            UpdateTitle();
        }
        MoveLeftPaddle(dt);
        MoveRightPaddle(dt);
        m_ballPrev = m_ball.center;  // ball is static -> no interpolation
        break;
    case State::Playing:
        m_ballPrev = m_ball.center;  // remember where it was...
        MoveLeftPaddle(dt);
        MoveRightPaddle(dt);
        StepBall(dt);                // ...then advance exactly one step
        // Leave a glowing trail behind the ball.
        m_particles->Emit(m_ball.center,
                          glm::vec3(Randf(-0.6f, 0.6f), Randf(-0.6f, 0.6f), Randf(-0.6f, 0.6f)),
                          glm::vec4(m_ball.color, 1.0f), 0.45f, 0.18f);
        break;
    case State::Menu:
        break;
    case State::GameOver:
        if (m_spaceRequested) {
            m_playerScore = m_aiScore = 0;
            ResetBall(+1);
        }
        m_ballPrev = m_ball.center;
        break;
    }
    m_spaceRequested = false;   // intent handled (or discarded) this step
}

void PongApp::OnRender()
{
    m_renderer.Clear();
                                      
    glm::mat4 view     = m_camera.ViewMatrix();
    if (m_shake > 0.0f) {
        // Square the trauma for a snappier feel, then jolt the view by a random
        // translation + roll. The HUD uses its own projection, so it stays still.
        const float s = m_shake * m_shake;
        const glm::vec3 off(Randf(-1.0f, 1.0f) * 0.25f * s,
                            Randf(-1.0f, 1.0f) * 0.25f * s, 0.0f);
        const float ang = Randf(-1.0f, 1.0f) * 0.04f * s;
        view = glm::rotate(glm::translate(glm::mat4(1.0f), off), ang,
                           glm::vec3(0.0f, 0.0f, 1.0f)) * view;
    }
    const glm::mat4 proj     = m_camera.ProjectionMatrix(GetWindow().AspectRatio());
    m_viewProj = proj * view;

    const glm::vec3 lightPos(3.0f, 12.0f, 4.0f);
    const glm::vec3 lightColor(1.0f, 0.97f, 0.92f);
    const glm::vec3 viewPos = m_camera.Position();

    m_shader->Bind();
    m_shader->SetMat4("uViewProj", m_viewProj);
    m_shader->SetVec3("uLightPos",  glm::vec3(3.0f, 12.0f, 4.0f));
    m_shader->SetVec3("uLightColor", glm::vec3(1.0f, 0.97f, 0.92f));
    m_shader->SetVec3("uViewPos",  m_camera.Position());

    DrawBox(m_floor);
    DrawBox(m_wallTop);
    DrawBox(m_wallBot);
    DrawBox(m_player);
    DrawBox(m_ai);
    if (m_puActive) {
        Box pu = m_pu;
        pu.half *= 1.0f + 0.18f * std::sin(m_time * 6.0f);  // gentle pulse
        DrawBox(pu);
    }

    // The ball is a rolling, lit sphere. Interpolate its position between fixed
    // steps, then apply the accumulated roll rotation.
    m_ballShader->Bind();
    m_ballShader->SetMat4("uViewProj", m_viewProj);
    m_ballShader->SetVec3("uLightPos", lightPos);
    m_ballShader->SetVec3("uLightColor", lightColor);
    m_ballShader->SetVec3("uViewPos", viewPos);
    const glm::vec3 bc = glm::mix(m_ballPrev, m_ball.center, InterpolationAlpha());
    const glm::mat4 ballModel = glm::translate(glm::mat4(1.0f), bc) * m_ballRot
                              * glm::scale(glm::mat4(1.0f), m_ball.half * 2.0f);
    m_ballShader->SetMat4("uModel", ballModel);
    m_ballShader->SetMat3("uNormalMat", glm::mat3(glm::transpose(glm::inverse(ballModel))));
    m_ballShader->SetVec3("uColor", m_ball.color);
    m_renderer.Draw(*m_ballMesh);

    // Glowing particles (trail + score bursts), drawn over the opaque scene.
    const float pointScale = GetWindow().Height() * 0.5f
                            / std::tan(glm::radians(m_camera.fov) * 0.5f);
    m_particles->Render(m_viewProj, pointScale);

    DrawHud();
}

void PongApp::DrawBox(const Box &b)
{
    const glm::mat4 model =
        glm::scale(glm::translate(glm::mat4(1.0f), b.center), b.half * 2.0f);
    const glm::mat3 normalMat = glm::mat3(glm::transpose(glm::inverse(model)));
    m_shader->SetMat4("uModel", model);
    m_shader->SetMat3("uNormalMat", normalMat);
    m_shader->SetVec3("uColor", b.color);
    m_renderer.Draw(*m_cube);
}

void PongApp::DrawHud()
{
    engine::Window& win = GetWindow();
    const int w = win.Width();
    const int h = win.Height();

    const glm::vec3 white(1.0f);
    const glm::vec3 blue(0.40f, 0.75f, 1.0f);
    const glm::vec3 orange(1.0f, 0.60f, 0.35f);
    const glm::vec3 yellow(1.0f, 0.92f, 0.40f);
    const glm::vec3 grey(0.6f);

    m_text->Begin(w, h);

    auto center = [&](const std::string& str, float yy, float sc, const glm::vec3& col) {
        m_text->Text(str, (w - m_text->Measure(str, sc)) * 0.5f, yy, sc, col);
    };

    if (m_state == State::Menu) {
        m_text->FillRect(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h),
                         glm::vec3(0.04f, 0.05f, 0.08f), 0.88f);
        center("3D PONG", h * 0.20f, 6.0f, white);

        const std::string o0 = "PLAYER  VS  AI";
        const std::string o1 = "PLAYER  VS  PLAYER";
        center((m_menuIndex == 0 ? "> " + o0 + " <" : " " + o0 + " "),
               h * 0.38f, 2.5f, m_menuIndex == 0 ? yellow : grey);
        center((m_menuIndex == 1 ? "> " + o1 + " <" : " " + o1 + " "),
               h * 0.47f, 2.5f, m_menuIndex == 1 ? yellow : grey);

        center(std::string("DIFFICULTY: < ") + kDiffs[m_difficulty].name + " >",
               h * 0.60f, 1.8f, white);
        center("BEST STREAK: " + std::to_string(m_bestStreak), h * 0.67f, 1.5f, grey);
            
        center("UP / DOWN SELECT    ENTER START     (OR 1 / 2)", h * 0.74f, 1.5f, grey);
    } else {
        const bool pvp = (m_mode == Mode::VsPlayer);

        const std::string score = std::to_string(m_playerScore) + " : " + std::to_string(m_aiScore);
        center(score, 26.0f, 5.0f, white);

        const char* leftLabel = pvp ? "P1" : "YOU";
        const char* rightLabel = pvp ? "P2" : "AI";
        m_text->Text(leftLabel, 24.0f, 26.0f, 2.0f, blue);
        m_text->Text(rightLabel, w - 24.0f - m_text->Measure(rightLabel, 2.0f), 26.0f, 2.0f, orange);

        std::string fx;
        if (m_growTimerL > 0.0f || m_growTimerR > 0.0f) fx += "GROW  ";
        if (m_bigBallTimer > 0.0f) fx += "BIG BALL  ";
        if (m_slowTimer > 0.0f) fx += "SLOW  ";
        if (!fx.empty()) center(fx, 84.0f, 1.5f, glm::vec3(0.6f, 1.0f, 0.7f));

        if (m_state == State::Serving) {
            center("PRESS SPACE TO SERVE", h * 0.63f, 2.5f, yellow);
        } else if (m_state == State::GameOver) {
            const bool leftWon = m_playerScore > m_aiScore;
            const std::string winner = pvp ? (leftWon ? "P1 WINS!" : "P2 WINS!")
                                           : (leftWon ? "YOU WIN!" : "AI WINS");
            center(winner, h * 0.42f, 4.0f, leftWon ? blue : orange);
            if (!pvp)
                center("STREAK " + std::to_string(m_winStreak) + "    BEST " + std::to_string(m_bestStreak),
                       h * 0.50f, 1.6f, grey);
            center("SPACE PLAY AGAIN      M MENU", h * 0.55f, 1.8f, yellow);
        }

        center(pvp ? "P1: W / S         P2: UP / DOWN" : "W / S TO MOVE",
               h - 40.0f, 1.5f, grey);
    }

    if (m_showDebug) {
        const glm::vec3 green(0.5f, 1.0f, 0.6f);
        char line[96];
        float y = 70.0f;
        auto dbg = [&](const char* s) { m_text->Text(s, 24.0f, y, 1.5f, green); y += 18.0f; };

        std::snprintf(line, sizeof(line), "FPS %.0f  (&.2f MS)", m_fps, m_frameMs);
        dbg(line);
        std::snprintf(line, sizeof(line), "PHYS %d STEPS/FRAME @ %.0f HZ",
                      m_fixedSteps, 1.0f / FixedTimeStep());
        dbg(line);
        std::snprintf(line, sizeof(line), "PARTICLES %zu", m_particles->AliveCount());
        dbg(line);
        std::snprintf(line, sizeof(line), "BALL SPD %.1f", glm::length(m_ballVel));
        dbg(line);
        const char* st = (m_state == State::Serving) ? "SERVING"
                       : (m_state == State::Playing) ? "PLAYING" : "GAMEOVER";
        std::snprintf(line, sizeof(line), "STATE %s", st);
        dbg(line);
        dbg("F3 TO HIDE");
    } else {
        m_text->Text("F3 DEBUG", w - 24.0f - m_text->Measure("F3 DEBUG", 1.5f), h - 40.0f, 1.5f, grey);
    }

    if (m_paused) {
        m_text->FillRect(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h),
                         glm::vec3(0.0f), 0.55f);  // dim the whole screen
        center("PAUSED", h * 0.42f, 6.0f, white);
        center("ESC RESUME      Q QUIT", h * 0.56f, 2.0f, yellow);
    }

    m_text->End();
}

void PongApp::MoveLeftPaddle(float dt)
{
    // Player 1, always W/S.
    engine::Window& w = GetWindow();
    const float speed = 9.0f * dt;
    if (w.IsKeyPressed(GLFW_KEY_W)) m_player.center.z -= speed;
    if (w.IsKeyPressed(GLFW_KEY_S)) m_player.center.z += speed;
    ClampPaddle(m_player);
}

void PongApp::MoveRightPaddle(float dt)
{
    if (m_mode == Mode::VsPlayer) {
        // Player 2, Up/Down arrows.
        engine::Window& w = GetWindow();
        const float speed = 9.0f * dt;
        if (w.IsKeyPressed(GLFW_KEY_UP)) m_ai.center.z -= speed;
        if (w.IsKeyPressed(GLFW_KEY_DOWN)) m_ai.center.z += speed;
        ClampPaddle(m_ai);
    } else {
        StepAI(dt);   // computer opponent
    }
}

void PongApp::StepAI(float dt)
{
    float targetZ;
    if (m_ballVel.x > 0.0f) {
        // Ball is heading toward the AI. On the first frame of the approach,
        // pick WHERE on the paddle to meet it: usually near the centre, but with
        // probability m_aiWallChance, deliberately off-centre so the bounce
        // angles the ball toward a wall (a much less robotic opponent).
        if (!m_aiCommitted) {
            m_aiCommitted = true;
            if (Randf(0.0f, 1.0f) < m_aiWallChance)
                m_aiAim = (Randf(0.0f, 1.0f) < 0.5f ? -1.0f : 1.0f) * Randf(0.45f, 0.8f);
            else
                m_aiAim = Randf(-0.15f, 0.15f);
        }
        // Position so the ball lands at the chosen offset on the paddle. Hitting
        // with an edge imparts sideways "english" in BouncePaddle. The capped
        // speed means a bold aim is sometimes missed -> natural imperfection.
        targetZ = m_ball.center.z - m_aiAim * m_ai.half.z;
    } else {
        // Ball going away: recentre and re-arm for the next approach.
        m_aiCommitted = false;
        targetZ = 0.0f;
    }

    const float speed = m_aiSpeed * dt;
    const float diff  = targetZ - m_ai.center.z;
    m_ai.center.z += std::clamp(diff, -speed, speed);
    ClampPaddle(m_ai);
}

void PongApp::ApplyDifficulty()
{
    const Diff& d = kDiffs[m_difficulty];
    m_aiSpeed      = d.aiSpeed;
    m_aiWallChance = d.aiWall;
    m_ballSpeed    = d.ballSpeed;
}

void PongApp::SpawnPowerUp() 
{
    m_puType = static_cast<int>(Randf(0.0f, static_cast<float>(PU_COUNT)));
    if (m_puType >= PU_COUNT) m_puType = PU_COUNT - 1;
    m_pu.center = glm::vec3(Randf(-3.5f, 3.5f), 0.0f, Randf(-3.0f, 3.0f));
    m_pu.half   = glm::vec3(0.45f);
    const glm::vec3 colors[PU_COUNT] = {
        glm::vec3(0.40f, 1.00f, 0.50f),   // grow    - green
        glm::vec3(0.40f, 0.90f, 1.00f),   // big ball - cyan
        glm::vec3(1.00f, 0.50f, 1.00f),   // slow    - magenta
    };
    m_pu.color = colors[m_puType];
    m_puActive = true;
}

void PongApp::ApplyPowerUp(int type)
{
    switch (type)
    {
    case PU_Grow:
        (m_lastHitSide == 0 ? m_growTimerL : m_growTimerR) = 6.0f;
        break;
    case PU_BigBall: m_bigBallTimer = 6.0f; break;
    case PU_Slow:    m_slowTimer    = 4.0f; break;
    default:
        break;
    }
    m_audio->Play(m_sndPower);
    m_particles->EmitBurst(m_pu.center, m_pu.color, 50, 5.0f, 0.7f, 0.2f);
}

void PongApp::UpdatePowerUps(float dt)
{
    // Active effects tick down; sizes are recomputed from the timers so they
    // auto-revert when an effect expires (no manual restore needed).
    m_growTimerL   = std::max(0.0f, m_growTimerL   - dt);
    m_growTimerR   = std::max(0.0f, m_growTimerR   - dt);
    m_bigBallTimer = std::max(0.0f, m_bigBallTimer - dt);
    m_slowTimer    = std::max(0.0f, m_slowTimer    - dt);

    m_player.half.z = (m_growTimerL  > 0.0f) ? kPaddleBaseHalfZ * 1.6f : kPaddleBaseHalfZ;
    m_ai.half.z     = (m_growTimerR  > 0.0f) ? kPaddleBaseHalfZ * 1.6f : kPaddleBaseHalfZ;
    m_ball.half     = glm::vec3((m_bigBallTimer > 0.0f) ? kBallBaseHalf * 1.7f : kBallBaseHalf);

    // Periodically drop a new pickup during play.
    if (m_state == State::Playing) {
        m_puSpawnTimer -= dt;
        if (!m_puActive && m_puSpawnTimer <= 0.0f) {
            SpawnPowerUp();
            m_puSpawnTimer = Randf(6.0f, 10.0f);
        }
    }
}

void PongApp::StepBall(float dt)
{
    m_ball.center += m_ballVel * dt;

    // Roll: spin about the horizontal axis perpendicular to the motion, by the
    // distance travelled divided by the radius (rolling without slipping).
    const float spd = glm::length(m_ballVel);
    if (spd > 1e-3f) {
        const glm::vec3 dir = m_ballVel / spd;
        const glm::vec3 axis = glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), dir));
        const float angle = spd * dt / m_ball.half.x;
        m_ballRot = glm::rotate(glm::mat4(1.0f), angle, axis) * m_ballRot;
    }

    // Pick up a power-up if the ball overlaps it.
    if (m_puActive && Overlap(m_ball, m_pu)) {
        ApplyPowerUp(m_puType);
        m_puActive = false;
    } 

    // Bounce off the two side walls (z).
    if (m_ball.center.z - m_ball.half.z < -kWallZ) {
        m_ball.center.z = -kWallZ + m_ball.half.z;
        m_ballVel.z = std::abs(m_ballVel.z);
        m_audio->Play(m_sndWall, Randf(0.9f, 1.1f));
    } else if (m_ball.center.z + m_ball.half.z > kWallZ) {
        m_ball.center.z = kWallZ - m_ball.half.z;
        m_ballVel.z = -std::abs(m_ballVel.z);
        m_audio->Play(m_sndWall, Randf(0.9f, 1.1f));
    }

    // Bounce off a paddle only when moving toward it.
    if (m_ballVel.x < 0.0f && Overlap(m_ball, m_player)) BouncePaddle(m_player, +1.0f);
    if (m_ballVel.x > 0.0f && Overlap(m_ball, m_ai))     BouncePaddle(m_ai,     -1.0f);

    // Score when the ball passes a paddle.
    if (m_ball.center.x < -kPaddleX - 1.5f) {
        m_particles->EmitBurst(m_ball.center, glm::vec3(1.0f, 0.6f, 0.35f), 60, 6.0f, 0.8f, 0.22f);
        m_shake = std::min(1.0f, m_shake + 0.7f);
        m_audio->Play(m_sndScore);
        ++m_aiScore;     AfterScore(false);
    } else if (m_ball.center.x > kPaddleX + 1.5f) {
        m_particles->EmitBurst(m_ball.center, glm::vec3(0.4f, 0.75f, 1.0f), 60, 6.0f, 0.8f, 0.22f);
        m_shake = std::min(1.0f, m_shake + 0.7f);
        m_audio->Play(m_sndScore);
        ++m_playerScore; AfterScore(true);
    }
}

void PongApp::BouncePaddle(const Box &paddle, float dirSign)
{
    m_audio->Play(m_sndBounce, Randf(0.92f, 1.12f));
    m_lastHitSide = (dirSign > 0.0f) ? 0 : 1;   // +1 = left paddle, -1 = right
    m_ballVel.x = dirSign * std::abs(m_ballVel.x);
    // Where the ball hit the paddle (-1 = far edge .. +1 = near edge) adds
    // "english", so the player can aim by hitting with the paddle's ends.
    const float offset = (m_ball.center.z - paddle.center.z) / paddle.half.z;
    m_ballVel.z += offset * 3.0f;
    m_ballVel *= 1.04f;                 // speed up a touch each rally hit
    // Push the ball back outside the paddle so it cannot re-collide.
    m_ball.center.x = paddle.center.x + dirSign * (paddle.half.x + m_ball.half.x);
    // Cap the total speed so rallies stay controllable.
    const float maxSpeed = 22.0f;
    if (glm::length(m_ballVel) > maxSpeed)
        m_ballVel = glm::normalize(m_ballVel) * maxSpeed;
}

void PongApp::AfterScore(bool playerScored)
{
    if (m_playerScore >= m_maxScore || m_aiScore >= m_maxScore) {
        m_state = State::GameOver;
        if (m_mode == Mode::VsAI) {   // streaks only count vs the computer
            const bool youWon = m_playerScore > m_aiScore;
            m_winStreak = youWon ? m_winStreak + 1 : 0;
            if (m_winStreak > m_bestStreak) m_bestStreak = m_winStreak;
        }
        UpdateTitle();
    } else {
        // Serve toward whoever was just scored on.
        ResetBall(playerScored ? +1 : -1);
    }
}

void PongApp::ResetBall(int serveDir)
{
    m_ball.center = glm::vec3(0.0f);
    m_ballPrev    = glm::vec3(0.0f);
    m_ballRot     = glm::mat4(1.0f);
    m_ballVel     = glm::vec3(0.0f);
    m_serveDir    = serveDir;
    m_state       = State::Serving;
    m_spaceRequested = false;
    UpdateTitle();
}

void PongApp::ClampPaddle(Box &p)
{
    const float limit = kWallZ - p.half.z;
    p.center.z = std::clamp(p.center.z, -limit, limit);
}

float PongApp::Randf(float lo, float hi)
{
    std::uniform_real_distribution<float> d(lo, hi);
    return d(m_rng);
}

void PongApp::UpdateTitle()
{
    char buf[160];
    const char* hint =
        (m_state == State::Serving)  ? "SPACE to serve" :
        (m_state == State::GameOver) ? ((m_playerScore > m_aiScore)
                                            ? "YOU WIN!   SPACE to restart"
                                            : "AI WINS.   SPACE to restart")
                                        : "W/S or Up/Down to move";
    std::snprintf(buf, sizeof(buf), "3D Pong    You %d : %d AI  [ %s ]",
                    m_playerScore, m_aiScore, hint);
    GetWindow().SetTitle(buf);
}
