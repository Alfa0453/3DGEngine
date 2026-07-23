#pragma once

#include <engine/graphics/Camera.h>
#include <engine/graphics/Framebuffer.h>
#include <engine/graphics/ParticleSystem.h>
#include <engine/graphics/PostProcess.h>
#include <engine/assets/RuntimeAssetManager.h>

#include <memory>
#include <optional>
#include <array>
#include <string>

namespace engine {
class ParticleRenderer;
}

class EditorScene;
class EditorAssets;

// Combined particle authoring and isolated live-preview panel. GL resources are
// created lazily because the editor constructor runs before a context exists.
class ParticleEditorPanel {
public:
    void Draw(EditorScene& scene, EditorAssets& assets, bool* open, float dt);
    void RequestOpen(const std::string& path);

private:
    void SyncSelection(EditorScene& scene);
    void RestartPreview();
    void RestartEffectPreview();
    void UpdatePreview(float dt);
    void UpdateEffectPreview(float dt);
    void SimulateTo(float seconds);
    unsigned int RenderPreview(int width, int height, float dt);
    bool DrawModuleStack(engine::ParticleSystemComponent& settings);
    bool DrawSettings(engine::ParticleSystemComponent& settings);

    engine::ParticleSystemComponent m_settings;
    engine::ParticleEmitter m_emitter;
    std::vector<engine::ParticleEffectLayer> m_effectLayers;
    engine::Camera m_camera;
    std::optional<engine::Framebuffer> m_framebuffer;
    std::optional<engine::Framebuffer> m_bloomOutput;
    std::optional<engine::PostProcess> m_postProcess;
    std::unique_ptr<engine::ParticleRenderer> m_renderer;
    int m_selectedIndex = -2;
    engine::ParticleModuleType m_selectedModule = engine::ParticleModuleType::Spawn;
    std::uint32_t m_selectedModuleId = 0;
    std::array<char, 64> m_moduleSearch{};
    bool m_hasSystem = false;
    bool m_playing = true;
    float m_elapsed = 0.0f;
    float m_yaw = 35.0f;
    float m_pitch = 18.0f;
    float m_distance = 6.0f;
    glm::vec3 m_background{0.025f, 0.03f, 0.045f};
    bool m_showGrid = true;
    bool m_showGround = false;
    bool m_bloom = true;
    float m_bloomStrength = 0.65f;
    float m_previewFps = 60.0f;
    float m_overdraw = 0.0f;
    std::string m_assetPath;
    std::string m_assetName{"NewParticle"};
    std::string m_effectAssetPath;
    std::string m_effectAssetName{"NewParticleEffect"};
    bool m_assetDirty = false;
    bool m_pendingNew = false;
    std::string m_pendingOpenPath;
    std::string m_externalOpenPath;
    std::string m_error;
    EditorAssets* m_assetsContext = nullptr;
    engine::RuntimeAssetManager m_shaderAssets;
};
