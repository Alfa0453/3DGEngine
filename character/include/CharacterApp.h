#pragma once

#include <engine/core/Application.h>
#include <engine/core/Config.h>
#include <engine/graphics/Renderer.h>
#include <engine/graphics/Camera.h>
#include <engine/graphics/TextRenderer.h>
#include <engine/graphics/ProceduralSky.h>
#include <engine/graphics/SkinnedModel.h>
#include <engine/graphics/SkinnedRenderer.h>
#include <engine/graphics/DayNightCycle.h>

#include <glm/glm.hpp>

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// Loads a rigged model, plays one of its clips through the Animator every frame,
// and draws it with SkinnedRenderer (GPU skinning). Orbit camera; a procedural
// sky as backdrop.
class CharacterApp : public engine::Application {
public:
    explicit CharacterApp(engine::Config& config);

protected:
    void OnInit()           override;
    void OnUpdate(float dt) override;
    void OnRender()         override;
    void OnShutdown()       override;

private:
    std::string Asset(const std::string& rel) const;

    engine::Config&  m_config;
    engine::Renderer m_renderer;

    std::optional<engine::SkinnedModel>   m_model;
    std::optional<engine::SkinnedRenderer> m_skinned;
    std::optional<engine::ProceduralSky>  m_sky;
    std::optional<engine::TextRenderer>   m_text;

    engine::DayNightCycle::Sample m_sample{};
    std::vector<glm::mat4> m_bones;

    std::string m_modelName = "models/character.glb";
    int   m_animIndex = 0;
    int   m_prevIndex = 0;      // clip we are fading FROM
    float m_blend     = 1.0f;   // 0 = prev clip, 1 = current clip
    bool  m_playing   = true;
    float m_time      = 0.0f;

    // Orbit camera.
    glm::vec3 m_target{0.0f, 2.0f, 0.0f};
    float m_yaw = 0.8f, m_pitch = 0.25f, m_dist = 10.0f;
    bool  m_autoSpin = true;

    float m_fps = 60.0f, m_dt = 0.016f;
    std::unordered_map<int, bool> m_keyPrev;
};
