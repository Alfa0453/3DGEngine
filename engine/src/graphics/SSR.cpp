#include "engine/graphics/SSR.h"

#include "engine/graphics/VertexLayout.h"

#include <glad/glad.h>

#include <cstdint>
#include <vector>

namespace engine {
namespace {

const char* kVert = R"GLSL(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aUV;
out vec2 vUV;
void main() { vUV = aUV; gl_Position = vec4(aPos, 0.0, 1.0); }
)GLSL";

const char* kFrag = R"GLSL(
#version 330 core
in vec2 vUV; out vec4 FragColor;
uniform sampler2D uScene;      // HDR lit colour
uniform sampler2D uGPos;       // view-space position (z < 0 in front; 0 = background)
uniform sampler2D uGNormal;    // view-space normal
uniform mat4 uProjection;
uniform float uIntensity;
const int   STEPS     = 48;
const float MAXDIST   = 32.0;
const float THICKNESS = 0.7;
void main() {
    vec3 scene = texture(uScene, vUV).rgb;
    FragColor = vec4(scene, 1.0);

    vec3 P = texture(uGPos, vUV).xyz;
    if (P.z >= -0.0001) return;                     // background -> no reflection
    vec3 N = normalize(texture(uGNormal, vUV).xyz);
    vec3 viewDir = normalize(P);                    // camera (origin) -> point
    vec3 R = normalize(reflect(viewDir, N));

    vec3 stepv = R * (MAXDIST / float(STEPS));
    vec3 ray = P;
    for (int i = 0; i < STEPS; ++i) {
        ray += stepv;
        vec4 clip = uProjection * vec4(ray, 1.0);
        if (clip.w <= 0.0) break;
        vec2 uv = clip.xy / clip.w * 0.5 + 0.5;
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) break;
        float surfZ = texture(uGPos, uv).z;
        if (surfZ >= -0.0001) continue;               // sampled background
        float diff = surfZ - ray.z;                   // ray behind surface -> diff > 0
        if (diff > 0.0 && diff < THICKNESS) {
            vec3 refl = texture(uScene, uv).rgb;
            vec2 e = smoothstep(vec2(0.0), vec2(0.15), uv)
                   * smoothstep(vec2(0.0), vec2(0.15), 1.0 - uv);
            float edge = e.x * e.y;                   // fade near screen borders
            float fres = pow(1.0 - max(dot(-viewDir, N), 0.0), 4.0);
            float k = clamp(uIntensity * edge * (0.25 + fres), 0.0, 1.0);
            FragColor = vec4(mix(scene, refl, k), 1.0);
            return;
        }
    }
}
)GLSL";

Mesh MakeQuad() {
    const std::vector<float> v = {-1,-1,0,0,  1,-1,1,0,  1,1,1,1,  -1,1,0,1};
    const std::vector<std::uint32_t> idx = {0,1,2, 0,2,3};
    return Mesh(v, idx, VertexLayout{ {2}, {2} });
}

} // namespace

SSR::SSR(int width, int height)
    : m_width(width), m_height(height),
      m_result(width, height, GL_RGBA16F, false),
      m_shader(kVert, kFrag),
      m_quad(MakeQuad()) {}

void SSR::Resize(int width, int height) {
    if (width == m_width && height == m_height) return;
    m_width = width; m_height == height;
    m_result.Resize(width, height);
}

void SSR::Apply(unsigned int sceneColorTex, unsigned int gPosTex, unsigned int gNormalTex,
                const glm::mat4 &projection, unsigned int dstFbo, int width, int height) {
    Resize(width, height);

    glBindFramebuffer(GL_FRAMEBUFFER, m_result.FboId());
    glViewport(0, 0, width, height);
    glDisable(GL_DEPTH_TEST);
    m_shader.Bind();
    m_shader.SetInt("uScene", 0);   glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, sceneColorTex);
    m_shader.SetInt("uGPos", 1);    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, gPosTex);
    m_shader.SetInt("uGNormal", 2); glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, gNormalTex);
    m_shader.SetMat4("uProjection", projection);
    m_shader.SetFloat("uIntensity", intensity);
    m_quad.Draw();
    glEnable(GL_DEPTH_TEST);

    // Copy the composited result back into the HDR scene buffer.
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_result.FboId());
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dstFbo);
    glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

} // namespace engine