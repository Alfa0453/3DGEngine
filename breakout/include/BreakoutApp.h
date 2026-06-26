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

#include <engine/ecs/Registry.h>
#include <engine/ecs/Components.h>    // engine::ecs::Transform, MeshRenderer

#include <glm/glm.hpp>

#include <optional>
#include <random>
#include <string>
#include <unordered_map>

// --- Game-specific components ----------------------------------------------
// These live in the game, not the engine: the engine ships generic pieces
// (Transform, MeshRenderer); the game adds whatever its rules need.
namespace bo {
struct Velocity { glm::vec3 value{0.0f}; };  // world units / second
struct Brick    { int hp = 1; };             // marks a destructible brick
}

// Breakout, built on the engine's ECS. Bricks, the ball, the paddle and the
// walls are all entities; movement, collision and rendering are systems.
class BreakoutApp : public engine::Application {
public:
    explicit BreakoutApp(engine::Config& config);

protected:
    void OnInit()                override;
    void OnShutdown()            override;
    void OnUpdate(float dt)      override;
    void OnFixedUpdate(float dt) override;
    void OnRender()              override;

private:
    enum class State { Ready, Playing, GameOver, Win };

    void BuildLevel();
    void ResetBall();       // park the ball on the paddle (-> Ready)
    void MovePaddle(float dt);
    void StepBall(float dt);
    void DrawHud();
    bool Pressed(int key);
    float Randf(float lo, float hi);
    std::string Asset(const std::string& rel) const;

    engine::Config& m_config;

    engine::Renderer m_renderer;
    engine::Camera   m_camera{glm::vec3(0.0f, 16.0f, 17.0f)};
    std::optional<engine::Shader>        m_shader;
    std::optional<engine::Mesh>          m_cube;
    std::optional<engine::Mesh>          m_sphere;
    std::optional<engine::TextRenderer>  m_text;
    std::optional<engine::ParticleSystem> m_particles;
    std::optional<engine::AudioEngine>   m_audio;
    std::string m_sndBounce, m_sndBrick, m_sndLose, m_sndWin;

    engine::ecs::Registry m_reg;
    engine::ecs::Entity   m_paddle = engine::ecs::kNull;
    engine::ecs::Entity   m_ball   = engine::ecs::kNull;
    glm::vec3 m_ballPrev{0.0f};     // for render interpolation

    State m_state       = State::Ready;
    int   m_score       = 0;
    int   m_lives       = 3;
    int   m_bestScore   = 0;
    int   m_bricksLeft  = 0;
    bool  m_launch      = false;    // SPACE pressed to launch this step
    float m_shake       = 0.0f;
    float m_time        = 0.0f;
    glm::mat4 m_viewProj{1.0f};

    std::unordered_map<int, bool> m_keyPrev;
    std::mt19937 m_rng{std::random_device{}()};
};