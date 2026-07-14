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
#include <engine/graphics/Terrain.h>
#include <engine/ecs/Registry.h>
#include <engine/ecs/Components.h>

#include <glm/glm.hpp>

#include <optional>

// Procedural terrain demo: an fBm heightmap generated into a lit, shadowed mesh
// with height/slope texturing. A ball snaps to the surface via HeightAt as you
// drive it (WASD); R regenerates a fresh terrain.
class TerrainDemoApp : public engine::Application {
public:
    explicit TerrainDemoApp(engine::Config& config);

protected:
    void OnInit()               override;
    void OnUpdate(float dt)     override;
    void OnRender()             override;
    void OnShutdown()           override;

private:
    void Regenerate(unsigned seed);

    engine::Config&  m_config;
    engine::Renderer m_renderer;

    std::optional<engine::Mesh>           m_sphere;
    std::optional<engine::Terrain>        m_terrain;
    std::optional<engine::PbrRenderer>    m_pbr;
    std::optional<engine::ProceduralSky>  m_sky;
    std::optional<engine::PostProcess>    m_post;
    std::optional<engine::TextRenderer>   m_text;
    std::optional<engine::IBL>            m_ibl;

    engine::ecs::Registry m_reg;
    engine::DayNightCycle::Sample m_sample{};

    engine::ecs::Entity m_terrainEnt = engine::ecs::kNull;
    engine::ecs::Entity m_ballEnt = engine::ecs::kNull;

    float m_size = 96.0f, m_maxHeight = 14.0f;
    int   m_res = 192;
    unsigned m_seed = 1;
    glm::vec3 m_ball{0.0f};

    float m_camYaw = 2.4f, m_camPitch = 0.45f, m_camDist = 60.0f;
    bool  m_prevR = false;
    float m_dt = 0.016f, m_fps = 60.0f;
};
