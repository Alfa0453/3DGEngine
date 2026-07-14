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
#include <engine/ai/NavMeshBuilder.h>

#include <glm/glm.hpp>

#include <optional>
#include <vector>

// Auto-navmesh demo: box obstacles are placed, then NavMeshBuilder VOXELIZES the
// area and MERGES walkable cells into rectangles at runtime. Each merged polygon is
// drawn a different colour so you can see the decomposition; an agent pathfinds
// across the generated mesh to a movable target.
class GenNavDemoApp : public engine::Application {
public:
    explicit GenNavDemoApp(engine::Config& config);

protected:
    void OnInit()               override;
    void OnUpdate(float dt)     override;
    void OnRender()             override;
    void OnShutdown()           override;

private:
    void BuildScene();

    engine::Config&  m_config;
    engine::Renderer m_renderer;

    std::optional<engine::Mesh>           m_cube, m_sphere;
    std::vector<engine::Mesh>             m_polyMeshes;   // one flat quad per nav polygon
    std::optional<engine::PbrRenderer>    m_pbr;
    std::optional<engine::ProceduralSky>  m_sky;
    std::optional<engine::PostProcess>    m_post;
    std::optional<engine::TextRenderer>   m_text;
    std::optional<engine::IBL>            m_ibl;

    engine::ecs::Registry m_reg;
    engine::DayNightCycle::Sample m_sample{};
    engine::ai::NavMesh m_nav;
    std::vector<engine::ai::NavObstacle> m_obstacles;

    engine::ecs::Entity m_agentEnt = engine::ecs::kNull;
    engine::ecs::Entity m_targetEnt = engine::ecs::kNull;
    std::vector<engine::ecs::Entity> m_markers;

    glm::vec3 m_agent{0.7f, 0.0f, 0.7f};
    glm::vec3 m_target{11.0f, 0.0f, 11.0f};
    std::vector<glm::vec3> m_path;
    glm::vec3 m_boundsMin{0,0,0}, m_boundsMax{12,0,12};

    float m_camYaw = 2.3f, m_camPitch = 0.95f, m_camDist = 20.0f;
    glm::vec3 m_camTarget{6.0f, 0.0f, 6.0f};
    float m_dt = 0.016f, m_fps = 60.0f;
};
