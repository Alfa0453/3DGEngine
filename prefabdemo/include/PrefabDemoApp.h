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
#include <engine/ecs/Prefab.h>

#include <glm/glm.hpp>

#include <optional>

// Prefab / spawning demo: a few entity templates are DEFINED once in a
// PrefabLibrary, then each key SPAWNS a fully-configured instance with one call.
// C clones the last spawned entity (Registry::Clone); R clears the field.
class PrefabDemoApp : public engine::Application {
public:
    explicit PrefabDemoApp(engine::Config& config);

protected:
    void OnInit()             override;
    void OnUpdate(float dt)   override;
    void OnRender()           override;
    void OnShutdown()         override;

private:
    void DefinePrefabs();
    glm::vec3 RandomSpot();

    engine::Config&  m_config;
    engine::Renderer m_renderer;

    std::optional<engine::Mesh>          m_cube, m_sphere, m_plane;
    std::optional<engine::PbrRenderer>   m_pbr;
    std::optional<engine::ProceduralSky> m_sky;
    std::optional<engine::PostProcess>   m_post;
    std::optional<engine::TextRenderer>  m_text;
    std::optional<engine::IBL>           m_ibl;

    engine::ecs::Registry m_reg;
    engine::PrefabLibrary m_prefabs;
    engine::DayNightCycle::Sample m_sample{};

    engine::ecs::Entity m_last = engine::ecs::kNull;
    engine::ecs::Entity m_ground = engine::ecs::kNull, m_sun = engine::ecs::kNull;
    unsigned m_rng = 0x51ed270u;
    int   m_spawned = 0;
    bool  m_p1=false,m_p2=false,m_p3=false,m_p4=false,m_pc=false,m_pr=false;

    float m_camYaw = 2.3f, m_camPitch = 0.7f, m_camDist = 26.0f;
    float m_dt = 0.016f, m_fps = 60.0f;
};
