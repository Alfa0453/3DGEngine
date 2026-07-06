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
#include <engine/ai/NavGrid.h>
#include <engine/ai/AStar.h>
#include <engine/ai/Steering.h>
#include <engine/ai/AiAgent.h>
#include <engine/ai/Perception.h>

#include <glm/glm.hpp>

#include <optional>
#include <unordered_map>
#include <vector>

// A top-down arena where an enemy patrols, spots the WASD-controlled player by
// line of sight, then pathfinds (A*) and pursues (steering) -- state chosen by an
// FSM. The whole AI stack in one demo.
class AiDemoApp : public engine::Application {
public:
    explicit AiDemoApp(engine::Config& config);

protected:
    void OnInit()             override;
    void OnUpdate(float dt)   override;
    void OnFixedUpdate(float h) override;
    void OnRender()           override;
    void OnShutdown()         override;

private:
    void BuildArena();

    engine::Config&  m_config;
    engine::Renderer m_renderer;

    std::optional<engine::Mesh>          m_cube, m_sphere, m_plane;
    std::optional<engine::PbrRenderer>   m_pbr;
    std::optional<engine::ProceduralSky> m_sky;
    std::optional<engine::PostProcess>   m_post;
    std::optional<engine::TextRenderer>  m_text;
    std::optional<engine::IBL>           m_ibl;

    engine::ecs::Registry m_reg;
    engine::PhysicsWorld  m_world;
    engine::DayNightCycle::Sample m_sample{};

    engine::ai::NavGrid   m_grid;
    engine::ai::AiAgent   m_ai;

    engine::ecs::Entity m_player = engine::ecs::kNull;
    engine::ecs::Entity m_enemy  = engine::ecs::kNull;
    engine::ai::VisionCone m_cone;

    // Orbit camera (mouse-drag to rotate, arrows to rotate, Z/X to zoom).
    glm::vec3 m_camTarget{0.0f, 0.0f, 0.0f};
    float m_camYaw = 1.571f, m_camPitch = 0.876f, m_camDist = 31.0f;

    float m_dt = 0.016f, m_fps = 60.0f;
    std::unordered_map<int, bool> m_keyPrev;
};
