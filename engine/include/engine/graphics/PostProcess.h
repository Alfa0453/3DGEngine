#pragma once

#include "engine/graphics/Framebuffer.h"
#include "engine/graphics/Shader.h"
#include "engine/graphics/Mesh.h"
#include "engine/graphics/EnvironmentLighting.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace engine {

class Texture;
class CascadedShadow;

// HDR post-processing. Render the 3D scene into its HDR buffer (BeginScene),
// then RenderToScreen applies bloom and ACES tone mapping + gamma and presents
// the result. Drop-in:
//
//     PostProcess post(width, height);
//     ...
//     post.BeginScene();                 // scene renders into HDR
//     pbr.Render(...); skybox.Draw(...);
//     post.RenderToScreen(width, height);
//     // draw HUD on top
//
class PostProcess {
public:
    struct Effect {
        const Shader* shader = nullptr;
        bool enabled = true;
        std::unordered_map<std::string, std::string> parameters;
        std::unordered_map<std::string, int> parameterTypes;
        std::unordered_map<std::string, const Texture*> textures;
    };

    struct Settings {
        bool  fxaa           = true;  // edge anti-aliasing on the composited result
        bool  bloom          = true;
        float bloomThreshold = 1.0f;  // HDR luminance above which a pixel blooms
        float bloomStrength  = 0.6f;
        float exposure       = 1.0f;  // used when autoExposure is off
        bool  autoExposure    = true;
        float exposureKey     = 0.4f;   // target middle-grey
        float adaptationSpeed = 1.5f;   // eye-adaptation rate (per second)
        float minExposure     = 0.08f;
        float maxExposure     = 6.0f;
        float minEV = -4.0f;
        float maxEV = 4.0f;
        float exposureCompensationEV = 0.0f;
        float adaptationSpeedUp = 3.0f;
        float adaptationSpeedDown = 1.0f;
        float histogramLowPercent = 0.02f;
        float histogramHighPercent = 0.95f;
        float exposureDeadZoneEV = 0.08f;
        float bloomKnee = 0.5f;
        int bloomLevels = 5;
        float temperature = 6500.0f;
        float tint = 0.0f;
        float saturation = 1.0f;
        float contrast = 1.0f;
        glm::vec3 lift{0.0f};
        glm::vec3 gamma{1.0f};
        glm::vec3 gain{1.0f};
        float lutIntensity = 1.0f;
    };
    struct UnderwaterSettings {
        float blend = 0.0f;                 // smoothly driven by the camera/water test
        glm::vec3 tint{0.04f, 0.30f, 0.38f};
        float fogDensity = 0.16f;
        float distortion = 0.006f;
        float causticsStrength = 0.20f;
        float causticsScale = 7.0f;
    };
    struct VolumetricSettings {
        bool enabled = false;
        float density = 0.008f;
        glm::vec3 scatteringAlbedo{0.72f, 0.80f, 0.92f};
        float scattering = 1.0f;
        float extinction = 1.0f;
        float anisotropy = 0.55f;
        float baseHeight = -0.35f;
        float heightFalloff = 0.10f;
        float startDistance = 0.0f;
        float maxDistance = 180.0f;
        int depthSlices = 48;
        int xyDownsample = 4;
        float historyWeight = 0.90f;
    };
    struct VolumetricLight {
        glm::vec3 position{0.0f};
        glm::vec3 direction{0.0f,-1.0f,0.0f};
        glm::vec3 radiance{1.0f};
        float range = 10.0f;
        float outerCos = -1.0f; // < 0 means point light
    };
    struct LocalFogVolume {
        glm::vec3 position{0.0f};
        glm::vec3 boxExtents{1.0f};
        glm::vec3 albedo{0.72f, 0.80f, 0.92f};
        float radius = 1.0f;
        float blendDistance = 0.5f;
        float density = 0.02f;
        float extinction = 1.0f;
        float anisotropy = 0.2f;
        bool sphere = false;
    };
    Settings settings;
    UnderwaterSettings underwater;
    VolumetricSettings volumetrics;

    PostProcess(int width, int height);
    ~PostProcess();

    PostProcess(const PostProcess&)            = delete;
    PostProcess& operator=(const PostProcess&) = delete;

    void BeginScene();                                  // bind + clear HDR target
    void RenderToScreen(int screenWidth, int screenHeight, float dt = 0.0f);
    // Composite into another framebuffer instead of the application backbuffer.
    // Used by isolated editor previews that still need the engine's real bloom.
    void RenderToFramebuffer(const Framebuffer& target, float dt = 0.0f);
    float Exposure() const { return m_exposure; }   // current adapted exposure
    float CurrentEV() const { return m_currentEV; }
    float TargetEV() const { return m_targetEV; }
    void NotifyCameraCut() {
        m_resetExposure = true;
        m_volumeHistoryValid = false;
        m_volumetricCameraValid = false;
    }
    void SetVolumetricCamera(const glm::mat4& inverseViewProjection,
                             const glm::vec3& cameraPosition,
                             const EnvironmentLightingState& environment) {
        const glm::mat4 viewProjection = glm::inverse(inverseViewProjection);
        if (m_volumetricCameraValid) {
            m_previousViewProjection = m_currentViewProjection;
            // Reprojection handles ordinary camera motion. Large teleports must not
            // reuse unrelated screen-space fog history.
            const float teleportDistance = std::max(5.0f, volumetrics.maxDistance * 0.10f);
            if (glm::distance(cameraPosition, m_cameraPosition) > teleportDistance)
                m_volumeHistoryValid = false;
        } else {
            m_previousViewProjection = viewProjection;
            m_volumetricCameraValid = true;
        }
        m_currentViewProjection = viewProjection;
        m_inverseViewProjection = inverseViewProjection;
        m_cameraPosition = cameraPosition;
        m_environment = environment;
    }
    void SetVolumetricDirectionalShadow(const CascadedShadow* shadow,
                                        const glm::mat4& view) {
        m_directionalShadow = shadow;
        m_volumeView = view;
    }
    void SetVolumetricLights(std::vector<VolumetricLight> lights) {
        if (lights.size() > 16) lights.resize(16);
        bool changed = lights.size() != m_volumetricLights.size();
        for (std::size_t i = 0; !changed && i < lights.size(); ++i) {
            const VolumetricLight& a = lights[i];
            const VolumetricLight& b = m_volumetricLights[i];
            changed = glm::dot(a.position-b.position, a.position-b.position) > 0.0001f
                || glm::dot(a.direction-b.direction, a.direction-b.direction) > 0.0001f
                || glm::dot(a.radiance-b.radiance, a.radiance-b.radiance) > 0.0001f
                || std::abs(a.range - b.range) > 0.001f
                || std::abs(a.outerCos - b.outerCos) > 0.0001f;
        }
        if (changed) m_volumeHistoryValid = false;
        m_volumetricLights = std::move(lights);
    }
    void SetLocalFogVolumes(std::vector<LocalFogVolume> volumes) {
        if (volumes.size() > 8) volumes.resize(8);
        bool changed = volumes.size() != m_localFogVolumes.size();
        for (std::size_t i = 0; !changed && i < volumes.size(); ++i) {
            const LocalFogVolume& a = volumes[i];
            const LocalFogVolume& b = m_localFogVolumes[i];
            changed = a.sphere != b.sphere
                || glm::dot(a.position-b.position, a.position-b.position) > 0.0001f
                || glm::dot(a.boxExtents-b.boxExtents, a.boxExtents-b.boxExtents) > 0.0001f
                || glm::dot(a.albedo-b.albedo, a.albedo-b.albedo) > 0.0001f
                || std::abs(a.radius - b.radius) > 0.001f
                || std::abs(a.blendDistance - b.blendDistance) > 0.001f
                || std::abs(a.density - b.density) > 0.0001f
                || std::abs(a.extinction - b.extinction) > 0.0001f
                || std::abs(a.anisotropy - b.anisotropy) > 0.0001f;
        }
        if (changed) m_volumeHistoryValid = false;
        m_localFogVolumes = std::move(volumes);
    }
    void SetEffects(std::vector<Effect> effects) { m_effects = std::move(effects); }
    const std::vector<Effect>& Effects() const { return m_effects; }
    void ClearEffects() { m_effects.clear(); }
    void SetColorLut(const Texture* texture) { m_colorLut = texture; }
    void SetSceneTextures(unsigned int normalTexture,
                          unsigned int velocityTexture) {
        m_sceneNormal = normalTexture;
        m_sceneVelocity = velocityTexture;
    }
    void SetIndirectTexture(unsigned int texture, float strength) {
        m_indirectTexture = texture;
        m_indirectStrength = strength;
    }
    void SetIndirectDebug(bool enabled) { m_indirectDebug = enabled; }
    void SetLightingDebugPassthrough(bool enabled) { m_lightingDebugPassthrough = enabled; }
    void Resize(int width, int height);
    unsigned int HdrFbo()   const { return m_hdr.FboId(); }
    unsigned int HdrColor() const { return m_hdr.ColorTexture(); }
    std::uint64_t MemoryBytes() const;

private:
    void RenderComposite(int width, int height, float dt, const Framebuffer* target);
    int m_width, m_height;
    Framebuffer m_hdr;               // full-res HDR scene
    Framebuffer m_bloomA, m_bloomB;  // half-res bloom ping-pong
    Framebuffer m_effectA, m_effectB; // full-res graph-effect ping-pong
    Framebuffer m_ldr;               // composited LDR result (FXAA input)
    Shader m_bright, m_blur, m_composite, m_luminance, m_fxaa, m_volumetricShader;
    Framebuffer m_volumetricA, m_volumetricB;
    Mesh   m_quad;
    unsigned int m_lumFbo = 0, m_lumTex = 0;
    int   m_lumSize  = 256;
    float m_exposure = 1.0f;   // adapted over time
    float m_currentEV = 0.0f;
    float m_targetEV = 0.0f;
    bool m_resetExposure = true;
    unsigned int m_exposureFrame = 0;
    float m_time = 0.0f;
    std::vector<Effect> m_effects;
    unsigned int m_sceneNormal = 0;
    unsigned int m_sceneVelocity = 0;
    unsigned int m_fallbackNormal = 0;
    unsigned int m_fallbackVelocity = 0;
    unsigned int m_fallbackIndirect = 0;
    unsigned int m_indirectTexture = 0;
    float m_indirectStrength = 0.0f;
    bool m_indirectDebug = false;
    bool m_lightingDebugPassthrough = false;
    glm::mat4 m_inverseViewProjection{1.0f};
    glm::mat4 m_currentViewProjection{1.0f};
    glm::mat4 m_previousViewProjection{1.0f};
    glm::vec3 m_cameraPosition{0.0f};
    EnvironmentLightingState m_environment;
    bool m_volumeHistoryValid = false;
    bool m_volumetricCameraValid = false;
    int m_volumeDownsample = 4;
    const CascadedShadow* m_directionalShadow = nullptr;
    glm::mat4 m_volumeView{1.0f};
    std::vector<VolumetricLight> m_volumetricLights;
    std::vector<LocalFogVolume> m_localFogVolumes;
    const Texture* m_colorLut = nullptr;
};

} // namespace engine
