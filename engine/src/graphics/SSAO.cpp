#include "engine/graphics/SSAO.h"

#include "engine/graphics/VertexLayout.h"
#include "engine/graphics/Camera.h"
#include "engine/ecs/Registry.h"
#include "engine/ecs/Components.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

using engine::ecs::Entity;
using engine::ecs::Transform;
using engine::ecs::MeshPBR;

namespace engine {
namespace {

// Geometry prepass: write view-space position + normal.
const char* kGeomVert = R"GLSL(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
uniform mat4 uModel, uView, uProj;
out vec3 vViewPos;
out vec3 vViewNormal;
void main() {
    mat4 mv = uView * uModel;
    vec4 vp = mv * vec4(aPos, 1.0);
    vViewPos = vp.xyz;
    vViewNormal = mat3(transpose(inverse(mv))) * aNormal;
    gl_Position = uProj * vp;
}
)GLSL";
const char* kGeomFrag = R"GLSL(
#version 330 core
layout (location = 0) out vec3 gPosition;
layout (location = 1) out vec3 gNormal;
in vec3 vViewPos;
in vec3 vViewNormal;
void main() { gPosition = vViewPos; gNormal = normalize(vViewNormal); }
)GLSL";

const char* kQuadVert = R"GLSL(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aUV;
out vec2 vUV;
void main() { vUV = aUV; gl_Position = vec4(aPos, 0.0, 1.0); }
)GLSL";

const char* kSsaoFrag = R"GLSL(
#version 330 core
in vec2 vUV; out float FragColor;
uniform sampler2D gPosition;
uniform sampler2D gNormal;
uniform sampler2D texNoise;
uniform vec3  uSamples[64];
uniform mat4  uProjection;
uniform vec2  uNoiseScale;
uniform float uRadius;
uniform float uBias;
void main() {
    vec3 fragPos = texture(gPosition, vUV).xyz;
    vec3 normal  = normalize(texture(gNormal, vUV).xyz);
    vec3 randomVec = normalize(texture(texNoise, vUV * uNoiseScale).xyz);
    vec3 tangent   = normalize(randomVec - normal * dot(randomVec, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3 TBN = mat3(tangent, bitangent, normal);
    float occlusion = 0.0;
    for (int i = 0; i < 64; ++i) {
        vec3 samplePos = fragPos + (TBN * uSamples[i]) * uRadius;
        vec4 offset = uProjection * vec4(samplePos, 1.0);
        offset.xyz /= offset.w;
        offset.xyz = offset.xyz * 0.5 + 0.5;
        float sampleDepth = texture(gPosition, offset.xy).z;
        float rangeCheck = smoothstep(0.0, 1.0, uRadius / abs(fragPos.z - sampleDepth));
        occlusion += (sampleDepth >= samplePos.z + uBias ? 1.0 : 0.0) * rangeCheck;
    }
    FragColor = 1.0 - occlusion / 64.0;
}
)GLSL";

const char* kBlurFrag = R"GLSL(
#version 330 core
in vec2 vUV; out float FragColor;
uniform sampler2D uSsao;
void main() {
    vec2 texel = 1.0 / vec2(textureSize(uSsao, 0));
    float result = 0.0;
    for (int x = -2; x < 2; ++x)
        for (int y = -2; y < 2; ++y)
            result += texture(uSsao, vUV + vec2(x, y) * texel).r;
    FragColor = result / 16.0;
}
)GLSL";

Mesh MakeQuad() {
    const std::vector<float> v = {-1,-1,0,0,  1,-1,1,0,  1,1,1,1,  -1,1,0,1};
    const std::vector<std::uint32_t> idx = {0,1,2, 0,2,3};
    return Mesh(v, idx, VertexLayout{ {2}, {2} });
}

unsigned int MakeColorTex(int w, int h, int internal, unsigned int fmt) {
    unsigned int id;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D, 0, internal, w, h, 0, fmt, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return id;
}

} // namespace

SSAO::SSAO(int width, int height)
    : m_width(width), m_height(height),
      m_geom(kGeomVert, kGeomFrag),
      m_ssao(kQuadVert, kSsaoFrag),
      m_blur(kQuadVert, kBlurFrag),
      m_quad(MakeQuad()) {
    // Hemisphere sample kernel, clustered toward the origin.
    std::mt19937 rng(1337);
    std::uniform_real_distribution<float> u01(0.0f, 1.0f), u11(-1.0f, 1.0f);
    for (int i = 0; i < 64; ++i) {
        glm::vec3 s(u11(rng), u11(rng), u01(rng));
        s = glm::normalize(s) * u01(rng);
        float scale = static_cast<float>(i) / 64.0f;
        scale = 0.1f + (scale * scale) * 0.9f;      // bias toward the centre
        m_kernel.push_back(s * scale);    
    }
    // 4x4 rotation-noise texture (tiled across the screen).
    std::vector<glm::vec3> noise;
    for (int i = 0; i < 16; ++i) noise .emplace_back(u11(rng), u11(rng), 0.0f);
    glGenTextures(1, &m_noiseTex);
    glBindTexture(GL_TEXTURE_2D, m_noiseTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, 4, 4, 0, GL_RGB, GL_FLOAT, noise.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    CreateTargets();
}

void SSAO::CreateTargets() {
    // G-buffer (position + normal) with a depth renderbuffer.
    glGenFramebuffers(1, &m_gFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_gFbo);
    m_gPos    = MakeColorTex(m_width, m_height, GL_RGBA16F, GL_RGBA);
    m_gNormal = MakeColorTex(m_width, m_height, GL_RGBA16F, GL_RGBA);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_gPos, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, m_gNormal, 0);
    const unsigned int bufs[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    glDrawBuffers(2, bufs);
    glGenRenderbuffers(1, &m_gDepth);
    glBindRenderbuffer(GL_RENDERBUFFER, m_gDepth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, m_width, m_height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_gDepth);

    // AO + blur targets (single channel).
    glGenFramebuffers(1, &m_ssaoFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_ssaoFbo);
    m_ssaoTex = MakeColorTex(m_width, m_height, GL_R16F, GL_RED);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_ssaoTex, 0);

    glGenFramebuffers(1, &m_blurFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_blurFbo);
    m_blurTex = MakeColorTex(m_width, m_height, GL_R16F, GL_RED);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_blurTex, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void SSAO::ReleaseTargets() {
    unsigned int texs[] = {m_gPos, m_gNormal, m_ssaoTex, m_blurTex};
    glDeleteTextures(4, texs);
    glDeleteRenderbuffers(1, &m_gDepth);
    unsigned int fbos[] = {m_gFbo, m_ssaoFbo, m_blurFbo};
    glDeleteFramebuffers(3, fbos);
}

SSAO::~SSAO() {
    ReleaseTargets();
    glDeleteTextures(1, &m_noiseTex);
}

void SSAO::Resize(int width, int height) {
    if (width == m_width && height == m_height) return;
    m_width = width; m_height = height;
    ReleaseTargets();
    CreateTargets();
}

void SSAO::Generate(ecs::Registry& reg, const Camera& camera, float aspect, int width, int height) {
    Resize(width, height);
    const glm::mat4 view = camera.ViewMatrix();
    const glm::mat4 proj = camera.ProjectionMatrix(aspect);

    GLint prevFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    glViewport(0, 0, m_width, m_height);

    // 1. Geometry prepass -> view-space position + normal.
    glBindFramebuffer(GL_FRAMEBUFFER, m_gFbo);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    m_geom.Bind();
    m_geom.SetMat4("uView", view);
    m_geom.SetMat4("uProj", proj);
    reg.view<Transform, MeshPBR>().each([&](Entity, Transform& t, MeshPBR& m) {
        if (!m.mesh) return;
        m_geom.SetMat4("uModel", t.Model());
        m.mesh->Draw();
    });

    // 2. SSAO pass.
    glBindFramebuffer(GL_FRAMEBUFFER, m_ssaoFbo);
    glClear(GL_COLOR_BUFFER_BIT);
    m_ssao.Bind();
    for (int i = 0; i < 64; ++i)
        m_ssao.SetVec3("uSamples[" + std::to_string(i) + "]", m_kernel[static_cast<std::size_t>(i)]);
    m_ssao.SetMat4("uProjection", proj);
    m_ssao.SetVec2("uNoiseScale", glm::vec2(m_width / 4.0f, m_height / 4.0f));
    m_ssao.SetFloat("uRadius", radius);
    m_ssao.SetFloat("uBias", bias);
    m_ssao.SetInt("gPosition", 0);
    m_ssao.SetInt("gNormal", 1);
    m_ssao.SetInt("texNoise", 2);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, m_gPos);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, m_gNormal);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, m_noiseTex);
    glDisable(GL_DEPTH_TEST);
    m_quad.Draw();

    // 3. Blur.
    glBindFramebuffer(GL_FRAMEBUFFER, m_blurFbo);
    glClear(GL_COLOR_BUFFER_BIT);
    m_blur.Bind();
    m_blur.SetInt("uSsao", 0);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, m_ssaoTex);
    m_quad.Draw();
    glEnable(GL_DEPTH_TEST);

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
}

void SSAO::BindAO(unsigned int unit) const {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, m_blurTex);
}

} // namespace engine