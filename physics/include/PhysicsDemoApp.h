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

#include <optional>
#include <unordered_map>
#include <vector>

// An interactive physics playground: drop and stack boxes and spheres, fire fast
// CCD projectiles, watch a joint chain swing -- all drawn with the PBR pipeline.
// Physics runs in the fixed-timestep OnFixedUpdate; rendering reads the same ECS
// Transforms the solver writes, so the picture always shows the simulation.
class PhysicsDemoApp : public engine::Application {
public:
    explicit PhysicsDemoApp(engine::Config& config);

protected:
    void OnInit()             override;
    void OnUpdate(float dt)   override;
    void OnFixedUpdate(float h) override;
    void OnRender()           override;
    void OnShutdown()         override;

private:
    void BuildScene();
    void ResetDynamics();
    engine::ecs::Entity SpawnBox(const glm::vec3& pos, const glm::vec3& half,
                                 const glm::vec3& color, float mass = 1.0f);
    engine::ecs::Entity SpawnSphere(const glm::vec3& pos, float radius,
                                    const glm::vec3& color, float mass = 1.0f);
    void Shoot();
    void DrawHud();
    bool Pressed(int key);

    engine::Config&  m_config;
    engine::Renderer m_renderer;
    engine::Camera   m_camera{glm::vec3(0.0f, 6.0f, 20.0f)};

    std::optional<engine::Mesh>          m_cube, m_sphere, m_plane;
    std::optional<engine::PbrRenderer>   m_pbr;
    std::optional<engine::ProceduralSky> m_sky;
    std::optional<engine::PostProcess>   m_post;
    std::optional<engine::IBL>           m_ibl;
    std::optional<engine::TextRenderer>  m_text;

    engine::ecs::Registry m_reg;
    engine::PhysicsWorld  m_world;
    std::vector<engine::ecs::Entity> m_dynamic;   // bodies cleared on reset
    engine::ecs::Entity   m_sun = engine::ecs::kNull;

    engine::DayNightCycle::Sample m_sample{};
    float m_timeOfDay = 0.32f;
    int   m_shots  = 0;
    bool  m_mouseCaptured = true;
    float m_time = 0.0f;
    float m_fps  = 60.0f;
    float m_dt   = 0.016f;
    std::unordered_map<int, bool> m_keyPrev;
};
