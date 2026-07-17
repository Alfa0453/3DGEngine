#include "engine/graphics/PostProcess.h"

#include "engine/graphics/VertexLayout.h"
#include "engine/graphics/Texture.h"

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <cstdint>
#include <cmath>
#include <utility>
#include <vector>
#include <algorithm>
#include <sstream>

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

// FXAA (Timothy Lottes, console fit). Edge anti-aliasing on the final LDR image,
// so the HDR/post path gets AA that MSAA on the default framebuffer can't provide.
const char* kFxaaFrag = R"GLSL(
#version 330 core
in vec2 vUV; out vec4 FragColor;
uniform sampler2D uImage;
uniform float uInvW;
uniform float uInvH;
float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }
void main() {
    vec2 inv = vec2(uInvW, uInvH);
    vec3 rgbM  = texture(uImage, vUV).rgb;
    vec3 rgbNW = texture(uImage, vUV + vec2(-1.0,-1.0) * inv).rgb;
    vec3 rgbNE = texture(uImage, vUV + vec2( 1.0,-1.0) * inv).rgb;
    vec3 rgbSW = texture(uImage, vUV + vec2(-1.0, 1.0) * inv).rgb;
    vec3 rgbSE = texture(uImage, vUV + vec2( 1.0, 1.0) * inv).rgb;
    float lM = luma(rgbM), lNW = luma(rgbNW), lNE = luma(rgbNE), lSW = luma(rgbSW), lSE = luma(rgbSE);
    float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));
    float lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));
    if (lMax - lMin < 0.05 * lMax) { FragColor = vec4(rgbM, 1.0); return; }  // no edge here
    vec2 dir;
    dir.x = -((lNW + lNE) - (lSW + lSE));
    dir.y =  ((lNW + lSW) - (lNE + lSE));
    float reduce = max((lNW + lNE + lSW + lSE) * 0.25 * 0.125, 1.0 / 128.0);
    float rcp = 1.0 / (min(abs(dir.x), abs(dir.y)) + reduce);
    dir = clamp(dir * rcp, vec2(-8.0), vec2(8.0)) * inv;
    vec3 rgbA = 0.5 * (texture(uImage, vUV + dir * (1.0/3.0 - 0.5)).rgb +
                       texture(uImage, vUV + dir * (2.0/3.0 - 0.5)).rgb);
    vec3 rgbB = rgbA * 0.5 + 0.25 * (texture(uImage, vUV + dir * -0.5).rgb +
                                     texture(uImage, vUV + dir *  0.5).rgb);
    float lB = luma(rgbB);
    FragColor = vec4((lB < lMin || lB > lMax) ? rgbA : rgbB, 1.0);
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

std::vector<float> ParameterNumbers(std::string value) {
    std::replace(value.begin(), value.end(), ',', ' ');
    std::replace(value.begin(), value.end(), '(', ' ');
    std::replace(value.begin(), value.end(), ')', ' ');
    std::istringstream input(value);
    std::vector<float> result;
    float number = 0.0f;
    while (input >> number) result.push_back(number);
    return result;
}

void UploadEffectParameters(Shader& shader, const PostProcess::Effect& effect) {
    int textureUnit = 4;
    for (const auto& entry : effect.parameters) {
        const std::string uniform = "u_" + entry.first;
        const auto type = effect.parameterTypes.find(entry.first);
        const int valueType =
            type == effect.parameterTypes.end() ? 0 : type->second;
        if (valueType == 7) {
            const auto texture = effect.textures.find(entry.first);
            if (texture != effect.textures.end() && texture->second) {
                texture->second->Bind(static_cast<unsigned int>(textureUnit));
                shader.SetInt(uniform, textureUnit++);
            }
            continue;
        }
        const std::vector<float> values = ParameterNumbers(entry.second);
        if (valueType == 1 || valueType == 2)
            shader.SetInt(uniform, entry.second == "true" ? 1
                : values.empty() ? 0 : static_cast<int>(values[0]));
        else if (valueType == 3 && values.size() >= 2)
            shader.SetVec2(uniform, glm::vec2(values[0], values[1]));
        else if (valueType == 4 && values.size() >= 3)
            shader.SetVec3(uniform, glm::vec3(values[0], values[1], values[2]));
        else if ((valueType == 5 || valueType == 6) && values.size() >= 4)
            shader.SetVec4(
                uniform, glm::vec4(values[0], values[1], values[2], values[3]));
        else
            shader.SetFloat(uniform, values.empty() ? 0.0f : values[0]);
    }
}

} // namespace

PostProcess::PostProcess(int width, int height)
    : m_width(width), m_height(height),
      m_hdr(width, height, GL_RGBA16F, true),
      m_bloomA(width / 2, height / 2, GL_RGBA16F, false),
      m_bloomB(width / 2, height / 2, GL_RGBA16F, false),
      m_effectA(width, height, GL_RGBA16F, false),
      m_effectB(width, height, GL_RGBA16F, false),
      m_ldr(width, height, GL_RGBA8, false),
      m_bright(kFullscreenVert, kBrightFrag),
      m_blur(kFullscreenVert, kBlurFrag),
      m_composite(kFullscreenVert, kCompositeFrag),
      m_luminance(kFullscreenVert, kLuminanceFrag),
      m_fxaa(kFullscreenVert, kFxaaFrag),
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

    const float normal[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    glGenTextures(1, &m_fallbackNormal);
    glBindTexture(GL_TEXTURE_2D, m_fallbackNormal);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, 1, 1, 0,
                 GL_RGBA, GL_FLOAT, normal);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    const float velocity[2] = {0.0f, 0.0f};
    glGenTextures(1, &m_fallbackVelocity);
    glBindTexture(GL_TEXTURE_2D, m_fallbackVelocity);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, 1, 1, 0,
                 GL_RG, GL_FLOAT, velocity);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

}

PostProcess::~PostProcess() {
    if (m_lumTex) glDeleteTextures(1, &m_lumTex);
    if (m_lumFbo) glDeleteFramebuffers(1, &m_lumFbo);
    if (m_fallbackNormal) glDeleteTextures(1, &m_fallbackNormal);
    if (m_fallbackVelocity) glDeleteTextures(1, &m_fallbackVelocity);
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
    m_effectA.Resize(width, height);
    m_effectB.Resize(width, height);
    m_ldr.Resize(width, height);
}

void PostProcess::RenderToScreen(int screenWidth, int screenHeight, float dt) {
    RenderComposite(screenWidth, screenHeight, dt, nullptr);
}

void PostProcess::RenderToFramebuffer(const Framebuffer& target, float dt) {
    RenderComposite(target.Width(), target.Height(), dt, &target);
}

void PostProcess::RenderComposite(int screenWidth, int screenHeight, float dt,
                                  const Framebuffer* target) {
    glDisable(GL_DEPTH_TEST);
    m_time += std::max(dt, 0.0f);

    // Graph-authored effects run in author-defined order on the linear HDR
    // scene. All effects sample the original scene depth while colour
    // ping-pongs between full-resolution buffers.
    const Framebuffer* scene = &m_hdr;
    bool writeA = true;
    for (const Effect& effect : m_effects) {
        if (!effect.enabled || !effect.shader) continue;
        Framebuffer& destination = writeA ? m_effectA : m_effectB;
        destination.Bind();
        Shader& shader = *const_cast<Shader*>(effect.shader);
        shader.Bind();
        scene->BindColorTexture(0);
        shader.SetInt("uSceneColor", 0);
        m_hdr.BindDepthTexture(1);
        shader.SetInt("uSceneDepth", 1);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D,
            m_sceneNormal ? m_sceneNormal : m_fallbackNormal);
        shader.SetInt("uSceneNormal", 2);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D,
            m_sceneVelocity ? m_sceneVelocity : m_fallbackVelocity);
        shader.SetInt("uSceneVelocity", 3);
        shader.SetVec2("uTexelSize", glm::vec2(
            1.0f / static_cast<float>(std::max(m_width, 1)),
            1.0f / static_cast<float>(std::max(m_height, 1))));
        shader.SetFloat("uExposure", m_exposure);
        shader.SetFloat("uTime", m_time);
        shader.SetFloat("uDeltaTime", std::max(dt, 0.0f));
        UploadEffectParameters(shader, effect);
        m_quad.Draw();
        scene = &destination;
        writeA = !writeA;
    }

    // --- Auto-exposure: average scene luminance, then adapt toward it. ---
    float exposure = settings.exposure;
    if (settings.autoExposure) {
        glBindFramebuffer(GL_FRAMEBUFFER, m_lumFbo);
        glViewport(0, 0, m_lumSize, m_lumSize);
        m_luminance.Bind();
        scene->BindColorTexture(0);
        m_luminance.SetInt("uScene", 0);
        m_quad.Draw();
        glBindTexture(GL_TEXTURE_2D, m_lumTex);
        glGenerateMipmap(GL_TEXTURE_2D);
        const int topMip = static_cast<int>(std::log2(static_cast<float>(m_lumSize)));
        float avgLog = 0.0f;
        glGetTexImage(GL_TEXTURE_2D, topMip, GL_RED, GL_FLOAT, &avgLog);
        const float avgLum = std::exp(avgLog);
        float targetExposure = settings.exposureKey / std::max(avgLum, 1e-4f);
        targetExposure = std::min(std::max(targetExposure, settings.minExposure), settings.maxExposure);
        if (dt > 0.0f) m_exposure += (targetExposure - m_exposure) * (1.0f - std::exp(-dt * settings.adaptationSpeed));
        else m_exposure = targetExposure;
        exposure = m_exposure;
    }

    // Bright-pass into the half-res bloom buffer.
    m_bloomA.Bind();
    m_bright.Bind();
    scene->BindColorTexture(0);
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

    // Composite (scene + bloom, exposure, ACES, gamma). With FXAA on, render into the
    // LDR buffer first; otherwise composite straight to the final target.
    if (settings.fxaa) m_ldr.Bind();
    else if (target)   target->Bind();
    else               Framebuffer::BindDefault(screenWidth, screenHeight);
    m_composite.Bind();
    scene->BindColorTexture(0); m_composite.SetInt("uScene", 0);
    src->BindColorTexture(1);  m_composite.SetInt("uBloom", 1);
    m_composite.SetFloat("uExposure", exposure);
    m_composite.SetFloat("uBloomStrength", settings.bloom ? settings.bloomStrength : 0.0f);
    m_quad.Draw();

    // FXAA the LDR result to the final target (edge AA for the offscreen path).
    if (settings.fxaa) {
        if (target) target->Bind();
        else Framebuffer::BindDefault(screenWidth, screenHeight);
        m_fxaa.Bind();
        m_ldr.BindColorTexture(0); m_fxaa.SetInt("uImage", 0);
        m_fxaa.SetFloat("uInvW", 1.0f / static_cast<float>(m_ldr.Width()));
        m_fxaa.SetFloat("uInvH", 1.0f / static_cast<float>(m_ldr.Height()));
        m_quad.Draw();
    }

    glEnable(GL_DEPTH_TEST);
}

} // namespace engine
