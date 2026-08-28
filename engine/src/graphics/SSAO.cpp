#include "engine/graphics/SSAO.h"

#include "engine/graphics/VertexLayout.h"
#include "engine/graphics/Camera.h"
#include "engine/graphics/Frustum.h"
#include "engine/ecs/Registry.h"
#include "engine/ecs/Components.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cmath>
#include <cstdint>
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
uniform mat4 uPreviousModel, uPreviousViewProjection;
out vec3 vViewPos;
out vec3 vViewNormal;
out vec4 vCurrentClip;
out vec4 vPreviousClip;
void main() {
    mat4 mv = uView * uModel;
    vec4 vp = mv * vec4(aPos, 1.0);
    vViewPos = vp.xyz;
    vViewNormal = mat3(transpose(inverse(mv))) * aNormal;
    vCurrentClip = uProj * vp;
    vPreviousClip = uPreviousViewProjection * uPreviousModel * vec4(aPos, 1.0);
    gl_Position = vCurrentClip;
}
)GLSL";
const char* kGeomFrag = R"GLSL(
#version 330 core
layout (location = 0) out vec3 gPosition;
layout (location = 1) out vec3 gNormal;
layout (location = 2) out vec2 gVelocity;
in vec3 vViewPos;
in vec3 vViewNormal;
in vec4 vCurrentClip;
in vec4 vPreviousClip;
void main() {
    gPosition = vViewPos;
    gNormal = normalize(vViewNormal);
    vec2 current = vCurrentClip.xy / max(abs(vCurrentClip.w), 0.00001);
    vec2 previous = vPreviousClip.xy / max(abs(vPreviousClip.w), 0.00001);
    gVelocity = (current - previous) * 0.5;
}
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
in vec2 vUV;
layout(location=0) out float FragAO;
layout(location=1) out vec3 FragBentNormal;
uniform sampler2D gPosition;
uniform sampler2D gNormal;
uniform mat4  uProjection;
uniform float uRadius;
uniform float uBias;
void main() {
    vec3 fragPos = texture(gPosition, vUV).xyz;
    vec3 normal  = normalize(texture(gNormal, vUV).xyz);
    if (dot(normal, normal) < 0.25 || fragPos.z >= -0.0001) {
        FragAO = 1.0; FragBentNormal = vec3(0.0, 0.0, 1.0); return;
    }
    vec2 pixel = 1.0 / vec2(textureSize(gPosition, 0));
    float projectedRadius = max(1.0, abs(uProjection[1][1]) * uRadius /
                                max(abs(fragPos.z), 0.05) * 0.5 * float(textureSize(gPosition,0).y));
    float obscurance = 0.0;
    vec3 openDirection = normal * 0.001;
    const int directions = 8;
    const int steps = 6;
    for (int d=0; d<directions; ++d) {
        float angle = (float(d) + 0.5 * mod(float(int(gl_FragCoord.x)+int(gl_FragCoord.y)),2.0))
                    * 6.28318530718 / float(directions);
        vec2 axis = vec2(cos(angle), sin(angle));
        float horizon = -1.0;
        vec3 bestOpen = normal;
        for (int s=1; s<=steps; ++s) {
            float fraction = (float(s)-0.35) / float(steps);
            vec2 uv = vUV + axis * pixel * projectedRadius * fraction;
            if (any(lessThan(uv,vec2(0.0))) || any(greaterThan(uv,vec2(1.0)))) continue;
            vec3 samplePos = texture(gPosition, uv).xyz;
            vec3 delta = samplePos - fragPos;
            float distanceToSample = length(delta);
            if (distanceToSample < uBias || distanceToSample > uRadius) continue;
            vec3 sampleDir = delta / max(distanceToSample, 1e-5);
            float elevation = dot(sampleDir, normal);
            float falloff = 1.0 - distanceToSample / uRadius;
            horizon = max(horizon, elevation * falloff);
            if (elevation < horizon) bestOpen += sampleDir * (1.0-elevation) * falloff;
        }
        float directionOcclusion = clamp((horizon + 0.08) / 1.08, 0.0, 1.0);
        obscurance += directionOcclusion;
        openDirection += normalize(bestOpen) * (1.0-directionOcclusion);
    }
    FragAO = clamp(1.0 - obscurance / float(directions), 0.0, 1.0);
    FragBentNormal = normalize(openDirection);
}
)GLSL";

const char* kBlurFrag = R"GLSL(
#version 330 core
in vec2 vUV;
layout(location=0) out float FragAO;
layout(location=1) out vec3 FragBentNormal;
uniform sampler2D uSsao;
uniform sampler2D uBentNormal;
uniform sampler2D gPosition;
uniform sampler2D gNormal;
void main() {
    vec2 texel = 1.0 / vec2(textureSize(uSsao, 0));
    vec3 centerPosition=texture(gPosition,vUV).xyz;
    vec3 centerNormal=normalize(texture(gNormal,vUV).xyz);
    float result=0.0, weightSum=0.0; vec3 bent=vec3(0.0);
    for(int x=-2;x<=2;++x) for(int y=-2;y<=2;++y){
        vec2 uv=vUV+vec2(x,y)*texel;
        vec3 p=texture(gPosition,uv).xyz;
        vec3 n=normalize(texture(gNormal,uv).xyz);
        float depthWeight=exp(-abs(p.z-centerPosition.z)*32.0/max(abs(centerPosition.z),0.25));
        float normalWeight=pow(max(dot(n,centerNormal),0.0),16.0);
        float spatialWeight=exp(-0.35*float(x*x+y*y));
        float w=depthWeight*normalWeight*spatialWeight;
        result+=texture(uSsao,uv).r*w;
        bent+=texture(uBentNormal,uv).xyz*w;
        weightSum+=w;
    }
    FragAO=result/max(weightSum,1e-5);
    FragBentNormal=normalize(bent/max(weightSum,1e-5));
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
    glGenQueries(6, &m_timestampQueries[0][0]);
    CreateTargets();
}

void SSAO::CreateTargets() {
    // G-buffer (position + normal) with a depth renderbuffer.
    glGenFramebuffers(1, &m_gFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_gFbo);
    m_gPos    = MakeColorTex(m_width, m_height, GL_RGBA16F, GL_RGBA);
    m_gNormal = MakeColorTex(m_width, m_height, GL_RGBA16F, GL_RGBA);
    m_gVelocity = MakeColorTex(m_width, m_height, GL_RG16F, GL_RG);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_gPos, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, m_gNormal, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, m_gVelocity, 0);
    const unsigned int bufs[3] = {
        GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2
    };
    glDrawBuffers(3, bufs);
    glGenRenderbuffers(1, &m_gDepth);
    glBindRenderbuffer(GL_RENDERBUFFER, m_gDepth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, m_width, m_height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_gDepth);

    // AO + blur targets (single channel).
    glGenFramebuffers(1, &m_ssaoFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_ssaoFbo);
    m_ssaoTex = MakeColorTex(m_width, m_height, GL_R16F, GL_RED);
    m_bentNormalTex = MakeColorTex(m_width, m_height, GL_RGB16F, GL_RGB);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_ssaoTex, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, m_bentNormalTex, 0);
    const unsigned int aoBuffers[2]={GL_COLOR_ATTACHMENT0,GL_COLOR_ATTACHMENT1};
    glDrawBuffers(2,aoBuffers);

    glGenFramebuffers(1, &m_blurFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_blurFbo);
    m_blurTex = MakeColorTex(m_width, m_height, GL_R16F, GL_RED);
    m_filteredBentNormalTex = MakeColorTex(m_width, m_height, GL_RGB16F, GL_RGB);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_blurTex, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, m_filteredBentNormalTex, 0);
    glDrawBuffers(2,aoBuffers);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void SSAO::ReleaseTargets() {
    unsigned int texs[] = {
        m_gPos, m_gNormal, m_gVelocity, m_ssaoTex, m_bentNormalTex,
        m_blurTex, m_filteredBentNormalTex
    };
    glDeleteTextures(7, texs);
    glDeleteRenderbuffers(1, &m_gDepth);
    unsigned int fbos[] = {m_gFbo, m_ssaoFbo, m_blurFbo};
    glDeleteFramebuffers(3, fbos);
}

SSAO::~SSAO() {
    ReleaseTargets();
    glDeleteQueries(6, &m_timestampQueries[0][0]);
}

void SSAO::Resize(int width, int height) {
    if (width == m_width && height == m_height) return;
    m_width = width; m_height = height;
    ReleaseTargets();
    CreateTargets();
}

void SSAO::Generate(ecs::Registry& reg, const Camera& camera, float aspect, int width, int height) {
    // Timestamp queries can be nested inside the editor's whole-scene elapsed
    // query, unlike another GL_TIME_ELAPSED scope. Read a three-frame-old result
    // without stalling, then reuse that slot for this GTAO pass.
    if (m_timestampSubmitted[m_timestampIndex]) {
        GLint ready = GL_FALSE;
        glGetQueryObjectiv(m_timestampQueries[m_timestampIndex][1],
                           GL_QUERY_RESULT_AVAILABLE, &ready);
        if (ready == GL_TRUE) {
            GLuint64 begin = 0, end = 0;
            glGetQueryObjectui64v(m_timestampQueries[m_timestampIndex][0],
                                  GL_QUERY_RESULT, &begin);
            glGetQueryObjectui64v(m_timestampQueries[m_timestampIndex][1],
                                  GL_QUERY_RESULT, &end);
            if (end >= begin)
                m_lastGpuMilliseconds = static_cast<double>(end - begin) / 1.0e6;
        }
    }
    glQueryCounter(m_timestampQueries[m_timestampIndex][0], GL_TIMESTAMP);
    Resize(width, height);
    const glm::mat4 view = camera.ViewMatrix();
    const glm::mat4 proj = camera.ProjectionMatrix(aspect);
    const glm::mat4 viewProjection = proj * view;
    const Frustum frustum = ExtractFrustum(viewProjection);

    GLint prevFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    glViewport(0, 0, m_width, m_height);

    // 1. Geometry prepass -> view-space position + normal.
    glBindFramebuffer(GL_FRAMEBUFFER, m_gFbo);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    m_currentModels.clear();
    if (m_currentModels.bucket_count() < m_previousModels.bucket_count())
        m_currentModels.reserve(m_previousModels.size());
    auto meshView = reg.view<Transform, MeshPBR>();
    if (!meshView.empty()) {
        m_geom.Bind();
        m_geom.SetMat4("uView", view);
        m_geom.SetMat4("uProj", proj);
        m_geom.SetMat4("uPreviousViewProjection",
            m_hasPreviousFrame ? m_previousViewProjection : viewProjection);
        meshView.each([&](Entity entity, Transform& t, MeshPBR& m) {
            if (!m.mesh) return;
            const glm::vec3 scale = glm::abs(t.scale);
            const float maxScale = std::max({scale.x, scale.y, scale.z});
            const glm::vec3 center = t.position + t.rotation * (m.mesh->BoundsCenter() * t.scale);
            if (!SphereInFrustum(frustum, center,
                                 std::max(m.mesh->BoundsRadius() * maxScale, 0.01f))) return;
            const glm::mat4 model = t.Model();
            const auto previous = m_previousModels.find(
                static_cast<std::uint32_t>(entity));
            m_geom.SetMat4("uModel", model);
            m_geom.SetMat4("uPreviousModel",
                m_hasPreviousFrame && previous != m_previousModels.end()
                    ? previous->second : model);
            m.mesh->Draw();
            m_currentModels[static_cast<std::uint32_t>(entity)] = model;
        });
    }
    m_previousModels.swap(m_currentModels);
    m_previousViewProjection = viewProjection;
    m_hasPreviousFrame = true;

    // 2. SSAO pass.
    glBindFramebuffer(GL_FRAMEBUFFER, m_ssaoFbo);
    glClear(GL_COLOR_BUFFER_BIT);
    m_ssao.Bind();
    m_ssao.SetMat4("uProjection", proj);
    m_ssao.SetFloat("uRadius", radius);
    m_ssao.SetFloat("uBias", bias);
    m_ssao.SetInt("gPosition", 0);
    m_ssao.SetInt("gNormal", 1);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, m_gPos);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, m_gNormal);
    glDisable(GL_DEPTH_TEST);
    m_quad.Draw();

    // 3. Blur.
    glBindFramebuffer(GL_FRAMEBUFFER, m_blurFbo);
    glClear(GL_COLOR_BUFFER_BIT);
    m_blur.Bind();
    m_blur.SetInt("uSsao", 0);
    m_blur.SetInt("uBentNormal", 1);
    m_blur.SetInt("gPosition", 2);
    m_blur.SetInt("gNormal", 3);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, m_ssaoTex);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, m_bentNormalTex);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, m_gPos);
    glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, m_gNormal);
    m_quad.Draw();
    glEnable(GL_DEPTH_TEST);

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    glQueryCounter(m_timestampQueries[m_timestampIndex][1], GL_TIMESTAMP);
    m_timestampSubmitted[m_timestampIndex] = true;
    m_timestampIndex = (m_timestampIndex + 1) % 3;
}

void SSAO::BindBentNormal(unsigned int unit) const {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, m_filteredBentNormalTex);
}

void SSAO::BindRawAO(unsigned int unit) const {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, m_ssaoTex);
}

void SSAO::BindAO(unsigned int unit) const {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, m_blurTex);
}

} // namespace engine
