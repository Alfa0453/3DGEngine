#pragma once

#include <engine/core/Application.h>
#include <engine/core/Config.h>
#include <engine/graphics/Renderer.h>
#include <engine/graphics/Shader.h>
#include <engine/graphics/Model.h>
#include <engine/graphics/TextRenderer.h>

#include <glm/glm.hpp>

#include <optional>
#include <string>
#include <unordered_map>

// A small tool that loads a model and orbits a camera around it. It exists to
// prove the OBJ loader end-to-end on the GPU and to inspect any model the loader
// can read. Built on the engine like the games, but renders a Model via
// engine::DrawModel instead of running gameplay.
class ViewerApp : public engine::Application {
public:
    explicit ViewerApp(engine::Config& config);

protected:
    void OnInit()       override;
    void OnShutdown()   override;
    void OnUpdate(float dt) override;
    void OnRender()     override;

private:
    bool        Pressed(int key);
    std::string Asset(const std::string& rel) const;

    engine::Config& m_config;
    engine::Renderer m_renderer;
    std::optional<engine::Shader>       m_shader;
    std::optional<engine::Model>        m_model;
    std::optional<engine::TextRenderer> m_text;
    std::string m_modelName;

    // Orbit-camera state (spherical coordinates around m_target).
    float     m_yaw      = 0.7f;// radians, around world Y
    float     m_pitch    = 0.35f;   // radians, elevation
    float     m_dist     = 6.0f;    // distance from target
    glm::vec3 m_target{0.0f};
    bool      m_autoSpin = true;

    std::unordered_map<int, bool> m_keyPrev;
};