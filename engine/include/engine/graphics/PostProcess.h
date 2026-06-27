#pragma once

#include "engine/graphics/Framebuffer.h"
#include "engine/graphics/Shader.h"
#include "engine/graphics/Mesh.h"

namespace engine {

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
    struct Settings {
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

    PostProcess(const PostProcess&)            = delete;
    PostProcess& operator=(const PostProcess&) = delete;

    void BeginScene();                                  // bind + clear HDR target
    void RenderToScreen(int screenWidth, int screenHeight, float dt = 0.0f);
    float Exposure() const { return m_exposure; }   // current adapted exposure
    void Resize(int width, int height);
    unsigned int HdrFbo()   const { return m_hdr.FboId(); }
    unsigned int HdrColor() const { return m_hdr.ColorTexture(); }

private:
    int m_width, m_height;
    Framebuffer m_hdr;               // full-res HDR scene
    Framebuffer m_bloomA, m_bloomB;  // half-res bloom ping-pong
    Shader m_bright, m_blur, m_composite, m_luminance;
    Mesh   m_quad;
    unsigned int m_lumFbo = 0, m_lumTex = 0;
    int   m_lumSize  = 256;
    float m_exposure = 1.0f;   // adapted over time
};

} // namespace engine