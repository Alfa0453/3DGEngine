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
#include <engine/graphics/ParticleSystem.h>
#include <engine/graphics/ParticleRenderer.h>
#include <engine/graphics/GpuParticleSystem.h>
#include <engine/ecs/Registry.h>
#include <engine/ecs/Components.h>

#include <glm/glm.hpp>

#include <optional>
#include <memory>
#include <vector>

// Showcases the 3D particle system: several emitters (magic fountain, fire, smoke,
// spark bursts) in a PBR scene with bloom, drawn as camera-facing billboards. The
// bright HDR particles glow through the post-process bloom pass.
class ParticleDemoApp : public engine::Application {
public:
    explicit ParticleDemoApp(engine::Config& config);

protected:
    void OnInit()               override;
    void OnUpdate(float dt)     override;
    void OnRender()             override;
    void OnShutdown()           override;

private:
    void BuildScene();

    engine::Config&  m_config;
    engine::Renderer m_renderer;

    std::optional<engine::Mesh>            m_plane, m_cube;
    std::optional<engine::PbrRenderer>     m_pbr;
    std::optional<engine::ProceduralSky>   m_sky;
    std::optional<engine::PostProcess>     m_post;
    std::optional<engine::TextRenderer>    m_text;
    std::optional<engine::IBL>             m_ibl;
    std::optional<engine::ParticleRenderer> m_particles;

    engine::ecs::Registry m_reg;
    engine::DayNightCycle::Sample m_sample{};

    engine::ParticleEmitter m_magic, m_fire, m_smoke, m_sparks;
    std::unique_ptr<engine::GpuParticleEmitter> m_gpuMagic;
    float m_gpuMagicSpawn = 0.0f;

    float m_camYaw = 2.4f, m_camPitch = 0.35f, m_camDist = 12.0f;
    glm::vec3 m_camTarget{0.0f, 1.5f, 0.0f};
    float m_dt = 0.016f, m_fps = 60.0f;
};
