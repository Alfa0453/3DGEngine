#include "engine/graphics/PostProcess.h"

#include "engine/graphics/VertexLayout.h"

#include <glad/glad.h>

#include <cstdint>
#include <cmath>
#include <utility>
#include <vector>

namespace engine {
namespace {

const char* kFullscreenVert = R"GLSL(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aUV;
out vec2 vUV;
void main () { vUV = aUV; gl_Position = vec4(aPos, 0.0, 1.0); }
)GLSL";

// Bright-pass: keep only pixels brighter than the threshold (the bloom source).
const char* kBrightFrag = R"GLSL(
#version 330 core
in vec2 vUV; out vec4 FragColor;
uniform sampler2D uScene;
uniform float uThreshold;
void main() {
    vec3 c = texture(uScene, vUV).rgb;
    float b = max(max(c.r, c.g), c.b);
    FragColor = vec4(b > uThreshold ? c : vec3(0.0), 1.0);
}
)GLSL";

// Separable Gaussian blur (one axis per invocation).
const char* kBlurFrag = R"GLSL(
#version 330 core
in vec2 vUV; out vec4 FragColor;
uniform sampler2D uImage;
uniform float uHorizontal;
const float w[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
void main() {
    vec2 texel = 1.0 / vec2(textureSize(uImage, 0));
    vec2 dir = uHorizontal > 0.5 ? vec2(1.0, 0.0) : vec2(0.0, 1.0);
    vec3 r = texture(uImage, vUV).rgb * w[0];
    for (int i = 1; i < 5; ++i) {
        vec2 off = dir * texel * float(i);
        r += texture(uImage, vUV + off).rgb * w[i];
        r += texture(uImage, vUV - off).rgb * w[i];
    }
    FragColor = vec4(r, 1.0);
}
)GLSL";

// Composite: scene + bloom, exposure, ACES tone map, gamma.
const char* kCompositeFrag = R"GLSL(
#version 330 core
in vec2 vUV; out vec4 FragColor;
uniform sampler2D uScene;
uniform sampler2D uBloom;
uniform float uExposure;
uniform float uBloomStrength;
vec3 ACES(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}
void main() {
    vec3 hdr = texture(uScene, vUV).rgb;
    hdr += texture(uBloom, vUV).rgb * uBloomStrength;
    vec3 col = ACES(hdr * uExposure);
    col = pow(col, vec3(1.0 / 2.2));
    FragColor = vec4(col, 1.0);
}
)GLSL";

const char* kLuminanceFrag = R"GLSL(
#version 330 core
in vec2 vUV; out float FragColor;
uniform sampler2D uScene;
void main() {
    vec3 c = texture(uScene, vUV).rgb;
    float lum = dot(c, vec3(0.2126, 0.7152, 0.0722));
    FragColor = log(max(lum, 1e-4));     // log space averages perceptually
}
)GLSL";

Mesh MakeFullscreenQuad() {
    const std::vector<float> verts = {
    // x,    y,    u,   v
    -1.0f, -1.0f, 0.0f, 0.0f,
     1.0f, -1.0f, 1.0f, 0.0f,
     1.0f,  1.0f, 1.0f, 1.0f,
    -1.0f,  1.0f, 0.0f, 1.0f,
    };
    const std::vector<std::uint32_t> idx = {0, 1, 2, 0, 2, 3};
    return Mesh(verts, idx, VertexLayout{ {2}, {2} });
}

} // namespace

PostProcess::PostProcess(int width, int height)
    : m_width(width), m_height(height),
      m_hdr(width, height, GL_RGBA16F, true),
      m_bloomA(width / 2, height / 2, GL_RGBA16F, false),
      m_bloomB(width / 2, height / 2, GL_RGBA16F, false),
      m_bright(kFullscreenVert, kBrightFrag),
      m_blur(kFullscreenVert, kBlurFrag),
      m_composite(kFullscreenVert, kCompositeFrag),
      m_luminance(kFullscreenVert, kLuminanceFrag),
      m_quad(MakeFullscreenQuad()) {
    glGenFramebuffers(1, &m_lumFbo);
    glGenTextures(1, &m_lumTex);
    glBindTexture(GL_TEXTURE_2D, m_lumTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, m_lumSize, m_lumSize, 0, GL_RED, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindFramebuffer(GL_FRAMEBUFFER, m_lumFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_lumTex, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

}

void PostProcess::BeginScene() {
    m_hdr.Bind();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void PostProcess::Resize(int width, int height) {
    if (width == m_width && height == m_height) return;
    m_width = width; m_height = height;
    m_hdr.Resize(width, height);
    m_bloomA.Resize(width / 2, height / 2);
    m_bloomB.Resize(width / 2, height / 2);
}

void PostProcess::RenderToScreen(int screenWidth, int screenHeight, float dt) {
    glDisable(GL_DEPTH_TEST);

    // --- Auto-exposure: average scene luminance, then adapt toward it. ---
    float exposure = settings.exposure;
    if (settings.autoExposure) {
        glBindFramebuffer(GL_FRAMEBUFFER, m_lumFbo);
        glViewport(0, 0, m_lumSize, m_lumSize);
        m_luminance.Bind();
        m_hdr.BindColorTexture(0);
        m_luminance.SetInt("uScene", 0);
        m_quad.Draw();
        glBindTexture(GL_TEXTURE_2D, m_lumTex);
        glGenerateMipmap(GL_TEXTURE_2D);
        const int topMip = static_cast<int>(std::log2(static_cast<float>(m_lumSize)));
        float avgLog = 0.0f;
        glGetTexImage(GL_TEXTURE_2D, topMip, GL_RED, GL_FLOAT, &avgLog);
        const float avgLum = std::exp(avgLog);
        float target = settings.exposureKey / std::max(avgLum, 1e-4f);
        target = std::min(std::max(target, settings.minExposure), settings.maxExposure);
        if (dt > 0.0f) m_exposure += (target - m_exposure) * (1.0f - std::exp(-dt * settings.adaptationSpeed));
        else m_exposure = target;
        exposure = m_exposure;
    }

    // Bright-pass into the half-res bloom buffer.
    m_bloomA.Bind();
    m_bright.Bind();
    m_hdr.BindColorTexture(0);
    m_bright.SetInt("uScene", 0);
    m_bright.SetFloat("uThreshold", settings.bloomThreshold);
    m_quad.Draw();

    // Ping-pong Gaussian blur (5 horizontal + 5 vertical).
    m_blur.Bind();
    m_blur.SetInt("uImage", 0);
    Framebuffer* src = &m_bloomA;
    Framebuffer* dst = &m_bloomB;
    bool horizontal = true;
    for (int i = 0; i < 10; i++) {
        dst->Bind();
        src->BindColorTexture(0);
        m_blur.SetFloat("uHorizontal", horizontal ? 1.0f : 0.0f);
        m_quad.Draw();
        std::swap(src, dst);
        horizontal = !horizontal;
    }

    // Composite to the screen.
    Framebuffer::BindDefault(screenWidth, screenHeight);
    m_composite.Bind();
    m_hdr.BindColorTexture(0); m_composite.SetInt("uScene", 0);
    src->BindColorTexture(1);  m_composite.SetInt("uBloom", 1);
    m_composite.SetFloat("uExposure", settings.exposure);
    m_composite.SetFloat("uBloomStrength", settings.bloom ? settings.bloomStrength : 0.0f);
    m_quad.Draw();

    glEnable(GL_DEPTH_TEST);
}

} // namespace engine