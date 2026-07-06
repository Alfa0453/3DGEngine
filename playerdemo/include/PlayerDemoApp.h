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
#include <engine/gameplay/PlayerController.h>

#include <glm/glm.hpp>

#include <optional>
#include <vector>

// A playground for the capsule player: walk a capsule around an arena with ramps,
// a low step and walls, and toggle between first- and third-person cameras. A
// handful of dynamic capsule rigid bodies stand as "pins" that topple and settle
// under the physics solver, showing the capsule collider working both ways
// (kinematic player + dynamic bodies).
class PlayerDemoApp : public engine::Application {
public:
    explicit PlayerDemoApp(engine::Config& config);

protected:
    void OnInit()               override;
    void OnUpdate(float dt)     override;
    void OnFixedUpdate(float h) override;
    void OnRender()             override;
    void OnShutdown()           override;

private:
    void BuildArena();

    engine::Config&  m_config;
    engine::Renderer m_renderer;

    std::optional<engine::Mesh>          m_cube, m_plane, m_playerCap, m_pinCap;
    std::optional<engine::PbrRenderer>   m_pbr;
    std::optional<engine::ProceduralSky> m_sky;
    std::optional<engine::PostProcess>   m_post;
    std::optional<engine::IBL>           m_ibl;
    std::optional<engine::TextRenderer>  m_text;

    engine::ecs::Registry m_reg;
    engine::PhysicsWorld  m_world;
    engine::DayNightCycle::Sample m_sample{};

    engine::PlayerController m_player;
    engine::ecs::Entity      m_playerEnt = engine::ecs::kNull;
    std::vector<engine::ecs::Entity> m_pins;

    // Accumulated look + view-toggle input (drained each fixed step).
    float m_lookDX = 0.0f, m_lookDY = 0.0f;
    bool  m_mouseLook = true;

    float m_dt = 0.016f, m_fps = 60.0f;
};
