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
        float exposure       = 1.0f; 
    };
    Settings settings;

    PostProcess(int width, int height);

    PostProcess(const PostProcess&)            = delete;
    PostProcess& operator=(const PostProcess&) = delete;

    void BeginScene();                                  // bind + clear HDR target
    void RenderToScreen(int screenWidth, int screenHeight);
    void Resize(int width, int height);

private:
    int m_width, m_height;
    Framebuffer m_hdr;               // full-res HDR scene
    Framebuffer m_bloomA, m_bloomB;  // half-res bloom ping-pong
    Shader m_bright, m_blur, m_composite;
    Mesh   m_quad;
};

} // namespace engine