#pragma once

#include <engine/core/Application.h>
#include <engine/core/Config.h>
#include <engine/graphics/Renderer.h>
#include <engine/graphics/Mesh.h>
#include <engine/graphics/Camera.h>
#include <engine/graphics/PbrRenderer.h>
#include <engine/graphics/ProceduralSky.h>
#include <engine/graphics/PostProcess.h>
#include <engine/graphics/DayNightCycle.h>
#include <engine/graphics/IBL.h>
#include <engine/ui/UI.h>
#include <engine/ecs/Registry.h>
#include <engine/ecs/Components.h>

#include <glm/glm.hpp>

#include <optional>

// Immediate-mode UI demo: a 3D scene (a spinning cube) with a mouse-driven UI
// overlay - sliders (spin speed, colour), buttons (reset / damage / heal), a
// health bar, and labels.
class UiDemoApp : public engine::Application {
public:
    explicit UiDemoApp(engine::Config& config);

protected:
    void OnInit()             override;
    void OnUpdate(float dt)   override;
    void OnRender()           override;
    void OnShutdown()         override;

private:
    engine::Config&  m_config;
    engine::Renderer m_renderer;

    std::optional<engine::Mesh>          m_cube;
    std::optional<engine::PbrRenderer>   m_pbr;
    std::optional<engine::ProceduralSky> m_sky;
    std::optional<engine::PostProcess>   m_post;
    std::optional<engine::IBL>           m_ibl;
    std::optional<engine::UI>            m_ui;

    engine::ecs::Registry m_reg;
    engine::DayNightCycle::Sample m_sample{};
    engine::ecs::Entity m_cubeEnt = engine::ecs::kNull;

    float m_spin = 0.0f, m_spinSpeed = 1.0f, m_metallic = 0.1f, m_rough = 0.4f;
    glm::vec3 m_color{0.3f, 0.6f, 1.0f};
    float m_hp = 75.0f;
    bool  m_prevDown = false;
    float m_dt = 0.016f, m_fps = 60.0f;
};
