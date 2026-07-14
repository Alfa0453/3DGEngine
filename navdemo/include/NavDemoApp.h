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
#include <engine/graphics/DayNightCycle.h>
#include <engine/graphics/IBL.h>
#include <engine/ecs/Registry.h>
#include <engine/ecs/Components.h>
#include <engine/ai/NavMesh.h>

#include <glm/glm.hpp>

#include <optional>
#include <vector>

// Navmesh navigation demo: a walkable grid with a hole (obstacle). An agent
// pathfinds to a movable target across the navmesh and follows the funnel path --
// smooth straight lines that only turn at corners, routed around the hole.
class NavDemoApp : public engine::Application {
public:
    explicit NavDemoApp(engine::Config& config);

protected:
    void OnInit()               override;
    void OnUpdate(float dt)     override;
    void OnRender()             override;
    void OnShutdown()           override;

private:
    void BuildScene();

    engine::Config&  m_config;
    engine::Renderer m_renderer;

    std::optional<engine::Mesh>           m_cube, m_sphere, m_navVis;
    std::optional<engine::PbrRenderer>    m_pbr;
    std::optional<engine::ProceduralSky>  m_sky;
    std::optional<engine::PostProcess>    m_post;
    std::optional<engine::TextRenderer>   m_text;
    std::optional<engine::IBL>            m_ibl;

    engine::ecs::Registry m_reg;
    engine::DayNightCycle::Sample m_sample{};
    engine::ai::NavMesh m_nav;

    engine::ecs::Entity m_agentEnt = engine::ecs::kNull;
    engine::ecs::Entity m_targetEnt = engine::ecs::kNull;
    std::vector<engine::ecs::Entity> m_markers;

    glm::vec3 m_agent{0.5f, 0.0f, 0.5f};
    glm::vec3 m_target{5.5f, 0.0f, 5.5f};
    std::vector<glm::vec3> m_path;

    float m_camYaw = 2.3f, m_camPitch = 0.9f, m_camDist = 12.0f;
    glm::vec3 m_camTarget{3.0f, 0.0f, 3.0f};
    float m_dt = 0.016f, m_fps = 60.0f;
};
