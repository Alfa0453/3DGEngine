#include "engine/graphics/ParticleSystem.h"
#include "engine/graphics/Shader.h"

#include <glad/glad.h>

#include <algorithm>

namespace engine {
namespace {

// Points in world space. gl_PointSize shrinks with distance (clip.w), so a
// particle keeps a consistent world size; clamped so it never gets silly.
const char* kVert = R"(#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec4 aColor;
layout (location = 2) in float aSize;
uniform mat4 uViewProj;
uniform float uScale;
out vec4 vColor;
void main() {
    vColor = aColor;
    vec4 clip = uViewProj * vec4(aPos, 1.0);
    gl_Position = clip;
    gl_PointSize = clamp(aSize * uScale / clip.w, 1.0, 64.0);
}
)";

// Soft round sprite from the point's built-in [0,1] coordinates.
const char* kFrag = R"(#version 330 core
in vec4 vColor;
out vec4 FragColor;
void main() {
    vec2 d = gl_PointCoord - vec2(0.5);
    float r2 = dot(d, d);
    if (r2 > 0.25) discard;                 // clip to a circle
    float soft = 1.0 - r2 * 4.0;            // fade toward the edge
    FragColor = vec4(vColor.rgb, vColor.a * soft);
}
)";

} // anonymous namespace

ParticleSystem::ParticleSystem(std::size_t capacity)
    : m_capacity(capacity), m_rng(std::random_device{}()) {
    m_particles.reserve(capacity);

    m_shader = std::make_unique<Shader>(kVert, kFrag);

    glGenVertexArrays(1, &m_vao);
    glBindVertexArray(m_vao);
    glGenBuffers(1, &m_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    // Vertex = pos(3) + color(4) + size(1) = 8 floats.
    const GLsizei stride = 8 * sizeof(float);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, 
                          reinterpret_cast<const void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<const void*>(7 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glBindVertexArray(0);
}

ParticleSystem::~ParticleSystem() {
    if (m_vbo) glDeleteBuffers(1, &m_vbo);
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
}

void ParticleSystem::Emit(const glm::vec3& pos, const glm::vec3& vel,
                          const glm::vec4& color, float life, float size) {
    if (m_particles.size() >= m_capacity) return;   // pool full: drop
    m_particles.push_back({pos, vel, color, life, life, size});
}

void ParticleSystem::EmitBurst(const glm::vec3& pos, const glm::vec3& color,
                              int count, float speed, float life, float size){
    std::uniform_real_distribution<float> d(-1.0f, 1.0f);
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    for (int i = 0; i < count; ++i) {
        glm::vec3 dir(d(m_rng), d(m_rng), d(m_rng));
        const float len = glm::length(dir);
        dir = (len > 1e-4f) ? dir / len : glm::vec3(0.0f, 1.0f, 0.0f);
        const glm::vec3 v = dir * speed * (0.4f + 0.6f * u(m_rng));
        Emit(pos, v, glm::vec4(color, 1.0f), life * (0.6f + 0.4f * u(m_rng)), size);
    }
}

void ParticleSystem::Update(float dt) {
    // Age each particle; recycle dead ones by swapping with the last (O(1)).
    for (std::size_t i = 0; i < m_particles.size();) {
        Particle& p = m_particles[i];
        p.life -= dt;
        if (p.life <= 0.0f) {
            p = m_particles.back();
            m_particles.pop_back();
            continue;            // re-check the swapped-in particle at this index
        }
        p.vel *= std::max(0.0f, 1.0f - dt * 1.5f);   // gentle drag
        p.pos += p.vel * dt;
        ++i;
    }
}

void ParticleSystem::Render(const glm::mat4& viewProj, float pointScale) {
    if (m_particles.empty()) return;

    std::vector<float> data;
    data.reserve(m_particles.size() * 8);
    for (const Particle& p : m_particles) {
        const float a = p.color.a * (p.life / p.maxLife);  // fade out with age
        data.push_back(p.pos.x); data.push_back(p.pos.y); data.push_back(p.pos.z);
        data.push_back(p.color.r); data.push_back(p.color.g); data.push_back(p.color.b);
        data.push_back(a);
        data.push_back(p.size);
    }

    m_shader->Bind();
    m_shader->SetMat4("uViewProj", viewProj);
    m_shader->SetFloat("uScale", pointScale);

    // Additive blending makes overlapping particles glow; we read depth (so the
    // floor can hide them) but do not write it (so they never occlude anything).
    GLboolean depthMask = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
    glEnable(GL_PROGRAM_POINT_SIZE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glDepthMask(GL_FALSE);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(data.size() * sizeof(float)),
                 data.data(), GL_DYNAMIC_DRAW);
    glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(m_particles.size()));

    glDepthMask(depthMask);
    glDisable(GL_BLEND);
    glDisable(GL_PROGRAM_POINT_SIZE);
}

} // namespace engine