#pragma once

#include "engine/graphics/Framebuffer.h"
#include "engine/graphics/Shader.h"
#include "engine/graphics/Mesh.h"

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace engine {

class Texture;

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
    };
    Settings settings;

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
    void SetEffects(std::vector<Effect> effects) { m_effects = std::move(effects); }
    const std::vector<Effect>& Effects() const { return m_effects; }
    void ClearEffects() { m_effects.clear(); }
    void SetSceneTextures(unsigned int normalTexture,
                          unsigned int velocityTexture) {
        m_sceneNormal = normalTexture;
        m_sceneVelocity = velocityTexture;
    }
    void Resize(int width, int height);
    unsigned int HdrFbo()   const { return m_hdr.FboId(); }
    unsigned int HdrColor() const { return m_hdr.ColorTexture(); }

private:
    void RenderComposite(int width, int height, float dt, const Framebuffer* target);
    int m_width, m_height;
    Framebuffer m_hdr;               // full-res HDR scene
    Framebuffer m_bloomA, m_bloomB;  // half-res bloom ping-pong
    Framebuffer m_effectA, m_effectB; // full-res graph-effect ping-pong
    Framebuffer m_ldr;               // composited LDR result (FXAA input)
    Shader m_bright, m_blur, m_composite, m_luminance, m_fxaa;
    Mesh   m_quad;
    unsigned int m_lumFbo = 0, m_lumTex = 0;
    int   m_lumSize  = 256;
    float m_exposure = 1.0f;   // adapted over time
    float m_time = 0.0f;
    std::vector<Effect> m_effects;
    unsigned int m_sceneNormal = 0;
    unsigned int m_sceneVelocity = 0;
    unsigned int m_fallbackNormal = 0;
    unsigned int m_fallbackVelocity = 0;
};

} // namespace engine
