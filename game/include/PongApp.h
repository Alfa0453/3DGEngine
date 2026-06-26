#pragma once

#include <engine/core/Application.h>
#include <engine/core/Config.h>
#include <engine/graphics/Camera.h>
#include <engine/graphics/Renderer.h>
#include <engine/graphics/Shader.h>
#include <engine/graphics/Mesh.h>
#include <engine/graphics/TextRenderer.h>
#include <engine/graphics/ParticleSystem.h>
#include <engine/audio/AudioEngine.h>

#include <glm/glm.hpp>

#include <optional>
#include <random>

#include "Box.h"

// The 3D Pong game. All gameplay lives here; main.cpp only constructs and runs
// it. PongApp is an engine::Application, so the engine owns the main loop and
// calls the OnInit / OnUpdate / OnRender hooks below.
class PongApp : public engine::Application {
public:
    explicit PongApp(engine::Config& config);

protected:
    void OnInit()                override;
    void OnShutdown()            override;
    void OnUpdate(float dt)      override;
    void OnFixedUpdate(float dt) override;
    void OnRender()              override;

private:
    enum class State { Menu, Serving, Playing, GameOver };
    enum class Mode  { VsAI, VsPlayer };

    // Rendering.
    void DrawBox(const Box& b);
    void DrawHud();

    // Gameplay.
    void  MoveLeftPaddle(float dt);
    void  MoveRightPaddle(float dt);
    void  StepAI(float dt);
    void  StartMatch();
    bool  Pressed(int key);     // rising-edge key test
    void  ApplyDifficulty();
    void  SpawnPowerUp();
    void  ApplyPowerUp(int type);
    void  UpdatePowerUps(float dt);
    void  StepBall(float dt);
    void  BouncePaddle(const Box& paddle, float dirSign);
    void  AfterScore(bool playerScored);
    void  ResetBall(int serveDir);
    void  ClampPaddle(Box& p);
    float Randf(float lo, float hi);
    void  UpdateTitle();

    engine::Config& m_config;           // settings (from pong.cfg)
    int   m_fixedRate = 120;
    float m_volume    = 1.0f;
    int   m_maxScore  = 5;
    float m_ballSpeed = 9.0f;
    float m_aiSpeed      = 6.5f;    // AI paddle tracking speed (units/s)
    float m_aiWallChance = 0.4f;    // chance the AI angles a shot toward a wall
    float m_aiAim        = 0.0f;    // chosen hit-offset on the paddle [-1..1]
    bool  m_aiCommitted  = false;   // has it picked an aim for this approach?
    int   m_difficulty   = 1;       // 0=Easy 1=Normal 2=Hard
    float m_musicVolume  = 0.5f;
    int   m_winStreak    = 0;       // consecutive match wins vs AI
    int   m_bestStreak   = 0;       // persisted high score

    // Power-up state.
    bool  m_puActive     = false;   // a pickup is currently in the arena
    int   m_puType       = 0;
    Box   m_pu;
    float m_puSpawnTimer = 0.0f;
    int   m_lastHitSide  = 0;       // 0 = left paddle, 1 = right paddle
    float m_growTimerL   = 0.0f, m_growTimerR = 0.0f;
    float m_bigBallTimer = 0.0f;
    float m_slowTimer    = 0.0f;
    float m_time         = 0.0f;    // wall-clock accumulator (for pulsing)


    engine::Renderer m_renderer;
    engine::Camera   m_camera{glm::vec3(0.0f, 15.0f, 13.0f)};
    std::optional<engine::Shader>       m_shader;
    std::optional<engine::Mesh>         m_cube;
    std::optional<engine::Mesh>         m_ballMesh;   // sphere
    std::optional<engine::Shader>       m_ballShader; // patterned, shows the roll
    std::optional<engine::TextRenderer> m_text;
    std::optional<engine::ParticleSystem> m_particles;
    std::optional<engine::AudioEngine>    m_audio;
    std::string m_sndBounce, m_sndWall, m_sndScore, m_sndPower, m_music;

    Box       m_floor, m_wallTop, m_wallBot, m_player, m_ai, m_ball;
    glm::vec3 m_ballVel{0.0f};
    glm::vec3 m_ballPrev{0.0f};     // ball position last fixed step (for interpolation)
    glm::mat4 m_ballRot{1.0f};      // accumulated rolling rotation
    glm::mat4 m_viewProj{1.0f};

    int   m_playerScore = 0;
    int   m_aiScore     = 0;
    int   m_serveDir    = 1;
    State m_state       = State::Menu;
    Mode  m_mode        = Mode::VsAI;
    int   m_menuIndex   = 0;            // which menu option is highlighted
    bool  m_spaceRequested = false;     // SPACE intent, consumed by the fixed sim
    std::unordered_map<int, bool> m_keyPrev;    // for rising-edge detection

    // Debug overlay (toggled with F3), and the live stats it shows.
    bool  m_showDebug  = false;
    int   m_fixedSteps = 0;     // OnFixedUpdate calls during the current frame
    float m_fps        = 0.0f;  // smoothed frames-per-second
    float m_frameMs    = 0.0f;  // smoothed frame time in milliseconds

    // Game-feel polish.
    bool  m_paused  = false;
    float m_shake   = 0.0f;     // screen-shake "trauma", 0..1, decays over time
    std::mt19937 m_rng{std::random_device{}()};
};