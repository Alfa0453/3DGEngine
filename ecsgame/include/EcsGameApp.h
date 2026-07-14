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
#include <engine/gameplay/GameplayComponents.h>
#include <engine/gameplay/GameplaySystems.h>

#include <glm/glm.hpp>

#include <optional>

// A tiny top-down shooter built the "right" way to show code SEPARATION: the player,
// enemies, projectiles and the attached wand are all just ENTITIES with COMPONENTS,
// and every behaviour is a SYSTEM (a free function over the registry). The App holds
// no per-entity state and no behaviour -- it only spawns entities and ORDERS the
// systems each frame.
class EcsGameApp : public engine::Application {
public:
    explicit EcsGameApp(engine::Config& config);

protected:
    void OnInit()             override;
    void OnUpdate(float dt)   override;
    void OnRender()           override;
    void OnShutdown()         override;

private:
    engine::Config&  m_config;
    engine::Renderer m_renderer;

    std::optional<engine::Mesh>          m_sphere, m_cube, m_plane;
    std::optional<engine::PbrRenderer>   m_pbr;
    std::optional<engine::ProceduralSky> m_sky;
    std::optional<engine::PostProcess>   m_post;
    std::optional<engine::TextRenderer>  m_text;
    std::optional<engine::IBL>           m_ibl;

    engine::ecs::Registry m_reg;
    engine::DayNightCycle::Sample m_sample{};
    engine::ecs::Entity m_player = engine::ecs::kNull;

    float m_shootCooldown = 0.0f;
    float m_camYaw = 1.7f, m_camPitch = 1.0f, m_camDist = 24.0f;
    bool  m_prevShoot = false;
    float m_dt = 0.016f, m_fps = 60.0f;
};
