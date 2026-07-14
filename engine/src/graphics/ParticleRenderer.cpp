#include "engine/graphics/ParticleRenderer.h"

#include "engine/graphics/Shader.h"
#include "engine/graphics/Camera.h"
#include "engine/graphics/ParticleSystem.h"

#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>

#include <vector>

namespace engine {
namespace {

const char* kVert = R"GLSL(
#version 330 core
layout (location = 0) in vec2 aCorner;   // unit quad corner in [-0.5,0.5]
layout (location = 1) in vec3 iCenter;   // per-particle world centre
layout (location = 2) in float iSize;
layout (location = 3) in vec4 iColor;
uniform mat4 uViewProj;
uniform vec3 uCamRight;
uniform vec3 uCamUp;
out vec2 vUV;
out vec4 vColor;
void main() {
    vec3 world = iCenter + (aCorner.x * uCamRight + aCorner.y * uCamUp) * iSize;
    gl_Position = uViewProj * vec4(world, 1.0);
    vUV = aCorner + 0.5;
    vColor = iColor;
}
)GLSL";

const char* kFrag = R"GLSL(
#version 330 core
in vec2 vUV;
in vec4 vColor;
out vec4 FragColor;
void main() {
    // Soft round sprite: radial falloff, squared for a smoother core.
    float d = length(vUV - vec2(0.5)) * 2.0;
    float a = clamp(1.0 - d, 0.0, 1.0);
    a *= a;
    FragColor = vec4(vColor.rgb, vColor.a * a);
}
)GLSL";

} // namespace

ParticleRenderer::ParticleRenderer()
    : m_shader(std::make_unique<Shader>(kVert, kFrag)) {
    const float quad[] = {
        -0.5f, -0.5f,   0.5f, -0.5f,   0.5f, 0.5f,
        -0.5f, -0.5f,   0.5f,  0.5f,  -0.5f, 0.5f,
    };
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_quadVBO);
    glGenBuffers(1, &m_instanceVBO);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glBindVertexArray(0);
}

ParticleRenderer::~ParticleRenderer() {
    if (m_instanceVBO) glDeleteBuffers(1, &m_instanceVBO);
    if (m_quadVBO)     glDeleteBuffers(1, &m_quadVBO);
    if (m_vao)         glDeleteVertexArrays(1, &m_vao);
}

void ParticleRenderer::Draw(const ParticleEmitter& emitter, const Camera& camera, float aspect) {
    const auto& ps = emitter.Particles();
    if (ps.empty()) return;

    // Pack per-instance data: vec3 center, float size, vec4 color = 8 floats.
    std::vector<float> inst;
    inst.reserve(ps.size() * 8);
    for (const Particle& p : ps) {
        inst.push_back(p.pos.x); inst.push_back(p.pos.y); inst.push_back(p.pos.z);
        inst.push_back(p.size);
        inst.push_back(p.color.r); inst.push_back(p.color.g); inst.push_back(p.color.b); inst.push_back(p.color.a);
    }

    const glm::mat4 view = camera.ViewMatrix();
    const glm::mat4 proj = camera.ProjectionMatrix(aspect);
    // Camera right/up from the view matrix (rows) for billboarding.
    const glm::vec3 camRight(view[0][0], view[1][0], view[2][0]);
    const glm::vec3 camUp   (view[0][1], view[1][1], view[2][1]);

    m_shader->Bind();
    m_shader->SetMat4("uViewProj", proj * view);
    m_shader->SetVec3("uCamRight", camRight);
    m_shader->SetVec3("uCamUp", camUp);

    // Blend + depth state: test against the scene, don't write depth.
    glEnable(GL_BLEND);
    if (emitter.cfg.blend == ParticleBlend::Additive) glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    else                                              glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glEnable(GL_DEPTH_TEST);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_instanceVBO);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(inst.size() * sizeof(float)), inst.data(), GL_DYNAMIC_DRAW);
    const GLsizei stride = 8 * sizeof(float);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(0));
    glVertexAttribDivisor(1, 1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(3 * sizeof(float)));
    glVertexAttribDivisor(2, 1);
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(4 * sizeof(float)));
    glVertexAttribDivisor(3, 1);

    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, static_cast<GLsizei>(ps.size()));

    glBindVertexArray(0);
    // Restore sane defaults for following passes.
    glDepthMask(GL_TRUE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_BLEND);
}

} // namespace engine
