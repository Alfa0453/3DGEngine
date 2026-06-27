#include "engine/graphics/IBL.h"

#include "engine/graphics/Primitives.h"
#include "engine/graphics/VertexLayout.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstdint>
#include <vector>

namespace engine {
namespace {

// Cube vertex shader for the convolution passes: hands the fragment shader the
// world-space direction (the cube's local position).
const char* kCubeVert = R"GLSL(
#version 330 core
layout (location = 0) in vec3 aPos;
uniform mat4 uProj;
uniform mat4 uView;
out vec3 vLocal;
void main() { vLocal = aPos; gl_Position = uProj * uView * vec4(aPos, 1.0); }
)GLSL";

// Diffuse irradiance: integrate incoming light over the hemisphere around N.
const char* kIrradianceFrag = R"GLSL(
#version 330 core
in vec3 vLocal; out vec4 FragColor;
uniform samplerCube uEnv;
const float PI = 3.14159265359;
void main () {
    vec3 N = normalize(vLocal);
    vec3 up = vec3(0.0, 1.0, 0.0);
    vec3 right = normalize(cross(up, N));
    up = normalize(cross(N, right));
    vec3 irradiance = vec3(0.0);
    float samples = 0.0;
    float d = 0.025;
    for (float phi = 0.0; phi < 2.0 * PI; phi += d)
        for (float theta = 0.0; theta < 0.5 * PI; theta += d) {
            vec3 t = vec3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
            vec3 sv = t.x * right + t.y * up + t.z * N;
            irradiance += texture(uEnv, sv).rgb * cos(theta) * sin(theta);
            samples += 1.0;
        }
    FragColor = vec4(PI * irradiance / samples, 1.0);
}
)GLSL";

// Specular prefilter: GGX importance-sampled blur of the environment per
// roughness level (written to a different mip each time).
const char* kPrefilterFrag = R"GLSL(
#version 330 core
in vec3 vLocal; out vec4 FragColor;
uniform samplerCube uEnv;
uniform float uRoughness;
const float PI = 3.14159265359;
float RadicalInverse(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}
vec2 Hammersley(uint i, uint n) { return vec2(float(i) / float(n), RadicalInverse(i)); }
vec3 ImportanceSampleGGX(vec2 Xi, vec3 N, float rough) {
    float a = rough * rough;
    float phi = 2.0 * PI * Xi.x;
    float ct = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
    float st = sqrt(1.0 - ct * ct);
    vec3 H = vec3(cos(phi) * st, sin(phi) * st, ct);
    vec3 up = abs(N.z) < 0.999 ? vec3(0, 0, 1) : vec3(1, 0, 0);
    vec3 tx = normalize(cross(up, N));
    vec3 ty = cross(N, tx);
    return normalize(tx * H.x + ty * H.y + N * H.z);
}
void main() {
    vec3 N = normalize(vLocal);
    vec3 V = N;
    const uint COUNT = 1024u;
    vec3 color = vec3(0.0);
    float total = 0.0;
    for (uint i = 0u; i < COUNT; ++i) {
        vec2 Xi = Hammersley(i, COUNT);
        vec3 H = ImportanceSampleGGX(Xi, N, uRoughness);
        vec3 L = normalize(2.0 * dot(V, H) * H - V);
        float NdotL = max(dot(N, L), 0.0);
        if (NdotL > 0.0) { color += texture(uEnv, L).rgb * NdotL; total += NdotL; }
    }
    FragColor = vec4(color / max(total, 0.001), 1.0);
}
)GLSL";

const char* kQuadVert = R"GLSL(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aUV;
out vec2 vUV;
void main() { vUV = aUV; gl_Position = vec4(aPos, 0.0, 1.0); }
)GLSL";

// BRDF integration LUT: the scale+bias the split-sum approximation needs.
const char* kBrdfFrag = R"GLSL(
#version 330 core
in vec2 vUV; out vec2 FragColor;
const float PI= 3.14159265359;
float RadicalInverse(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}
vec2 Hammersley(uint i, uint n) { return vec2(float(i) / float(n), RadicalInverse(i)); }
vec3 ImportanceSampleGGX(vec2 Xi, vec3 N, float rough) {
    float a = rough * rough;
    float phi = 2.0 * PI * Xi.x;
    float ct = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
    float st = sqrt(1.0 - ct * ct);
    vec3 H = vec3(cos(phi) * st, sin(phi) * st, ct);
    vec3 up = abs(N.z) < 0.999 ? vec3(0, 0, 1) : vec3(1, 0, 0);
    vec3 tx = normalize(cross(up, N));
    vec3 ty = cross(N, tx);
    return normalize(tx * H.x + ty * H.y + N * H.z);
}
float GeometrySchlickGGX(float NdotV, float rough) {
    float k = (rough * rough) / 2.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}
float GeometrySmith(vec3 N, vec3 V, vec3 L, float rough) {
    return GeometrySchlickGGX(max(dot(N, V), 0.0), rough)
         * GeometrySchlickGGX(max(dot(N, L), 0.0), rough);
}
vec2 IntegrateBRDF(float NdotV, float rough) {
    vec3 V = vec3(sqrt(1.0 - NdotV * NdotV), 0.0, NdotV);
    float A = 0.0, B = 0.0;
    vec3 N = vec3(0.0, 0.0, 1.0);
    const uint COUNT = 1024u;
    for (uint i = 0u; i < COUNT; ++i) {
        vec2 Xi = Hammersley(i, COUNT);
        vec3 H = ImportanceSampleGGX(Xi, N, rough);
        vec3 L = normalize(2.0 * dot(V, H) * H - V);
        float NdotL = max(L.z, 0.0);
        float NdotH = max(H.z, 0.0);
        float VdotH = max(dot(V, H), 0.0);
        if (NdotL > 0.0) {
            float G = GeometrySmith(N, V, L, rough);
            float Gv = (G * VdotH) / (NdotH * NdotV);
            float Fc = pow(1.0 - VdotH, 5.0);
            A += (1.0 - Fc) * Gv;
            B += Fc * Gv;
        }
    }
    return vec2(A, B) / float(COUNT);
}
void main() { FragColor = IntegrateBRDF(vUV.x, vUV.y); }
)GLSL";

unsigned int MakeCubemap(int size, bool mips) {
    unsigned int id;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_CUBE_MAP, id);
    for (unsigned int i = 0; i < 6; ++i)
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F, size, size, 0,
                     GL_RGB, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER,
                    mips ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    if (mips) glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
    return id;
}

Mesh MakeQuad() {
    const std::vector<float> v = {
        -1, -1, 0, 0,  1, -1, 1, 0,  1, 1, 1, 1,  -1, 1, 0, 1,
    };
    const std::vector<std::uint32_t> idx = {0, 1, 2, 0, 2, 3};
    return Mesh(v, idx, VertexLayout{ {2}, {2} });
}

} // namespace

IBL::IBL(int envSize)
    : m_envSize(envSize),
      m_irradianceShader(kCubeVert, kIrradianceFrag),
      m_prefilterShader(kCubeVert, kPrefilterFrag),
      m_brdfShader(kQuadVert, kBrdfFrag),
      m_cube(primitives::Cube()),
      m_quad(MakeQuad()) {
    glGenFramebuffers(1, &m_captureFbo);
    glGenRenderbuffers(1, &m_captureRbo);

    m_envCube    = MakeCubemap(m_envSize, true);
    m_irradiance = MakeCubemap(m_irrSize, false);
    m_prefilter  = MakeCubemap(m_preSize, true);

    glGenTextures(1, &m_brdfLUT);
    glBindTexture(GL_TEXTURE_2D, m_brdfLUT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, m_brdfSize, m_brdfSize, 0, GL_RG, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    RenderBrdfLUT();   // constant — compute once
}

IBL::~IBL() {
    if (m_brdfLUT)    glDeleteTextures(1, &m_brdfLUT);
    if (m_prefilter)  glDeleteTextures(1, &m_prefilter);
    if (m_irradiance) glDeleteTextures(1, &m_irradiance);
    if (m_envCube)    glDeleteTextures(1, &m_envCube);
    if (m_captureRbo) glDeleteRenderbuffers(1, &m_captureRbo);
    if (m_captureFbo) glDeleteFramebuffers(1, &m_captureFbo);
}

void IBL::RenderBrdfLUT() {
    glBindFramebuffer(GL_FRAMEBUFFER, m_captureFbo);
    glBindRenderbuffer(GL_RENDERBUFFER, m_captureRbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, m_brdfSize, m_brdfSize);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_captureRbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_brdfLUT, 0);
    glViewport(0, 0, m_brdfSize, m_brdfSize);
    glDisable(GL_DEPTH_TEST);
    m_brdfShader.Bind();
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    m_quad.Draw();
    glEnable(GL_DEPTH_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void IBL::Generate(const std::function<void(const glm::mat4&, const glm::mat4&)>& drawSky) {
    const glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
    const glm::vec3 O(0.0f);
    const glm::mat4 views[6] = {
        glm::lookAt(O, glm::vec3( 1, 0, 0), glm::vec3(0, -1,  0)),
        glm::lookAt(O, glm::vec3(-1, 0, 0), glm::vec3(0, -1,  0)),
        glm::lookAt(O, glm::vec3( 0, 1, 0), glm::vec3(0,  0,  1)),
        glm::lookAt(O, glm::vec3( 0,-1, 0), glm::vec3(0,  0, -1)),
        glm::lookAt(O, glm::vec3( 0, 0, 1), glm::vec3(0, -1,  0)),
        glm::lookAt(O, glm::vec3( 0, 0,-1), glm::vec3(0, -1,  0)),
    };

    glBindFramebuffer(GL_FRAMEBUFFER, m_captureFbo);
    glBindRenderbuffer(GL_RENDERBUFFER, m_captureRbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_captureRbo);

    // 1. Render the environment (sky) into the env cubemap.
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, m_envSize, m_envSize);
    glViewport(0, 0, m_envSize, m_envSize);
    for (unsigned int i = 0; i < 6; ++i) {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, m_envCube, 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        drawSky(views[i], proj);
    }
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_envCube);
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

    // 2. Irradiance convolution.
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, m_irrSize, m_irrSize);
    glViewport(0, 0, m_irrSize, m_irrSize);
    m_irradianceShader.Bind();
    m_irradianceShader.SetInt("uEnv", 0);
    m_irradianceShader.SetMat4("uProj", proj);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_envCube);
    for (unsigned int i = 0; i < 6; ++i) {
        m_irradianceShader.SetMat4("uView", views[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, m_irradiance, 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        m_cube.Draw();
    }

    // 3. Specular prefilter into the mip chain.
    m_prefilterShader.Bind();
    m_prefilterShader.SetInt("uEnv", 0);
    m_prefilterShader.SetMat4("uProj", proj);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_envCube);
    for (int mip = 0; mip < m_prefilterMips; ++mip) {
        const int mipSize = static_cast<int>(m_preSize * std::pow(0.5, mip));
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, mipSize, mipSize);
        glViewport(0, 0, mipSize, mipSize);
        const float roughness = static_cast<float>(mip) / static_cast<float>(m_prefilterMips - 1);
        m_prefilterShader.SetFloat("uRoughness", roughness);
        for (unsigned int i = 0; i < 6; ++i) {
            m_prefilterShader.SetMat4("uView", views[i]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                   GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, m_prefilter, mip);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            m_cube.Draw();
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void IBL::Bind(unsigned int irradianceUnit, unsigned int prefilterUnit,
               unsigned int brdfUnit) const {
    glActiveTexture(GL_TEXTURE0 + irradianceUnit);
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_irradiance);
    glActiveTexture(GL_TEXTURE0 + prefilterUnit);
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_prefilter);
    glActiveTexture(GL_TEXTURE0 + brdfUnit);
    glBindTexture(GL_TEXTURE_2D, m_brdfLUT);
}

} // namespace engine