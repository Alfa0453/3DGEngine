#pragma once

#include <engine/core/Application.h>
#include <engine/core/Config.h>
#include <engine/graphics/Renderer.h>
#include <engine/graphics/Mesh.h>
#include <engine/graphics/Camera.h>
#include <engine/graphics/TextRenderer.h>
#include <engine/graphics/PbrRenderer.h>
#include <engine/graphics/ProceduralSky.h>
#include <engine/graphics/PostProcess.h>
#include <engine/graphics/IBL.h>
#include <engine/graphics/DayNightCycle.h>
#include <engine/ecs/Registry.h>
#include <engine/ecs/Components.h>
#include <engine/physics/PhysicsWorld.h>
#include <engine/physics/PhysicsComponents.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <optional>
#include <unordered_map>

// A rolling-marble obstacle course. The player is a sphere rigid body pushed by
// forces relative to a third-person follow camera; the course is static box
// platforms with a jump gap and a ramp; trigger volumes are checkpoints and the
// goal; falling off respawns at the last checkpoint. Physics runs in the fixed
// step and the renderer draws the Transforms the solver writes.
class MarbleApp : public engine::Application {
public:
    explicit MarbleApp(engine::Config& config);

protected:
    void OnInit()             override;
    void OnUpdate(float dt)   override;
    void OnFixedUpdate(float h) override;
    void OnRender()           override;
    void OnShutdown()         override;

private:
    void BuildLevel();
    engine::ecs::Entity AddPlatform(const glm::vec3& pos, const glm::vec3& half,
                                    const glm::vec3& color, float tiltZdeg = 0.0f);
    engine::ecs::Entity AddTrigger(const glm::vec3& pos, const glm::vec3& half,
                                   const glm::vec3& markerColor);
    bool  GroundedRay();
    void  Respawn();

    engine::Config&  m_config;
    engine::Renderer m_renderer;

    std::optional<engine::Mesh>          m_sphere, m_cube;
    std::optional<engine::PbrRenderer>   m_pbr;
    std::optional<engine::ProceduralSky> m_sky;
    std::optional<engine::PostProcess>   m_post;
    std::optional<engine::IBL>           m_ibl;
    std::optional<engine::TextRenderer>  m_text;

    engine::ecs::Registry m_reg;
    engine::PhysicsWorld  m_world;
    engine::ecs::Entity   m_marble = engine::ecs::kNull;
    engine::ecs::Entity   m_goal   = engine::ecs::kNull;
    std::unordered_map<engine::ecs::Entity, glm::vec3> m_checkpoints;  // trigger -> respawn point

    engine::DayNightCycle::Sample m_sample{};

    float m_marbleRadius = 0.5f;
    glm::vec3 m_spawn{0.0f, 1.2f, 0.0f};
    glm::quat m_roll{1, 0, 0, 0};       // visual-only rolling orientation

    float m_camYaw   = 0.0f;            // degrees; forward = +X at yaw 0
    float m_camPitch = 22.0f;           // look-down angle
    glm::vec3 m_moveDir{0.0f};          // camera-relative accel dir this step
    bool  m_jumpQueued = false;
    bool  m_grounded   = false;

    bool  m_won  = false;
    float m_time = 0.0f;
    int   m_checkpointsHit = 0;
    float m_fps  = 60.0f;
    float m_dt   = 0.016f;
    bool  m_mouseCaptured = true;
    std::unordered_map<int, bool> m_keyPrev;
};
