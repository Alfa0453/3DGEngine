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
#include <engine/ecs/Registry.h>
#include <engine/ecs/Components.h>

#include <glm/glm.hpp>

#include <optional>
#include <unordered_map>

// A lighting/scene showcase with a day/night cycle. The sky and sun are driven
// by a DayNightCycle; the app just changes the time of day.
class SceneApp : public engine::Application {
public:
    explicit SceneApp(engine::Config& config);

protected:
    void OnInit()      override;
    void OnShutdown()  override;
    void OnUpdate(float dt) override;
    void OnRender()        override;

private:
    void BuildScene();
    void DrawHud();
    bool Pressed(int key);

    engine::Config& m_config;

    engine::Renderer m_renderer;
    engine::Camera   m_camera{glm::vec3(0.0f, 4.0f, 18.0f)};
    std::optional<engine::Mesh>          m_sphere, m_plane;
    std::optional<engine::PbrRenderer>   m_pbr;
    std::optional<engine::ProceduralSky> m_sky;
    std::optional<engine::PostProcess>   m_post;
    std::optional<engine::TextRenderer>  m_text;

    engine::ecs::Registry m_reg;
    engine::ecs::Entity   m_sun = engine::ecs::kNull;

    engine::DayNightCycle::Sample m_sample{};
    float m_timeOfDay  = 0.5f;      // 0 midnight .. 0.5 noon
    float m_targetTime = 0.5f;      // eased toward, unless auto-cycling
    bool  m_autoCycle  = false;

    bool      m_mouseCaptured = true;
    float     m_time = 0.0f;
    bool      m_animateLights = true;
    std::unordered_map<int, bool> m_keyPrev;
};