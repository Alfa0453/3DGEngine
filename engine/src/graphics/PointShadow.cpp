#include "engine/graphics/PointShadow.h"

#include "engine/ecs/Registry.h"
#include "engine/ecs/Components.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>

using engine::ecs::Entity;
using engine::ecs::Transform;
using engine::ecs::MeshPBR;

namespace engine {
namespace {

const char* kVert = R"GLSL(
#version 330 core
layout (location = 0) in vec3 aPos;
uniform mat4 uModel;
uniform mat4 uLightVP;
out vec3 vWorld;
void main() {
    vec4 w = uModel * vec4(aPos, 1.0);
    vWorld = w.xyz;
    gl_Position = uLightVP * w;
}
)GLSL";

const char* kFrag = R"GLSL(
#version 330 core
in vec3 vWorld; out float FragColor;
uniform vec3 uLightPos;
void main() { FragColor = length(vWorld - uLightPos); }
)GLSL";

} // namespace

PointShadow::PointShadow(int faceSize) : m_faceSize(faceSize), m_shader(kVert, kFrag) {
    glGenFramebuffers(1, &m_fbo);
    glGenRenderbuffers(1, &m_depthRbo);
    glBindRenderbuffer(GL_RENDERBUFFER, m_depthRbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, m_faceSize, m_faceSize);

    glGenTextures(kMax, m_cubes);
    for (int c = 0; c < kMax; ++c) {
        glBindTexture(GL_TEXTURE_CUBE_MAP, m_cubes[c]);
        for (unsigned int f = 0; f < 6; ++f)
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, 0, GL_R32F,
                         m_faceSize, m_faceSize, 0, GL_RED, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_depthRbo);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

PointShadow::~PointShadow() {
    glDeleteTextures(kMax, m_cubes);
    glDeleteRenderbuffers(1, &m_depthRbo);
    glDeleteFramebuffers(1, &m_fbo);
}

void PointShadow::Generate(ecs::Registry &reg, const std::vector<glm::vec3> &lights, float farPlane) {
    m_far = farPlane;
    m_count = std::min(static_cast<int>(lights.size()), kMax);

    GLint prevFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);

    glViewport(0, 0, m_faceSize, m_faceSize);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    m_shader.Bind();

    const glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, m_far);
    const glm::vec3 dir[6] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    const glm::vec3 up[6]  = {{0,-1,0},{0,-1,0},{0,0,1},{0,0,-1},{0,-1,0},{0,-1,0}};
    // glClearColor clamps to [0,1] even for float attachments; use glClearBufferfv
    // so unrendered pixels hold 1e6 ("very far" = lit) in the R32F cubemap.
    const GLfloat farClear[4] = {1.0e6f, 0.0f, 0.0f, 0.0f};

    for (int i = 0; i < m_count; ++i) {
        const glm::vec3 lp = lights[static_cast<std::size_t>(i)];
        m_shader.SetVec3("uLightPos", lp);
        for (unsigned int f = 0; f < 6; ++f) {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                   GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, m_cubes[i], 0);
            glClearBufferfv(GL_COLOR, 0, farClear);
            glClear(GL_DEPTH_BUFFER_BIT);
            m_shader.SetMat4("uLightVP", proj * glm::lookAt(lp, lp + dir[f], up[f]));
            reg.view<Transform, MeshPBR>().each([&](Entity, Transform& t, MeshPBR& m) {
                if (!m.mesh) return;
                // Skip emissive geometry (the light gizmos): a marker sphere sits
                // ON the light, so casting it would block the light everywhere.
                const glm::vec3& e = m.material.emissive;
                if (e.x > 0.0f || e.y > 0.0f || e.z > 0.0f) return;
                m_shader.SetMat4("uModel", t.Model());
                m.mesh->Draw();
            });
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
}

void PointShadow::BindCubes(unsigned int startUnit) const {
    for (int c = 0; c < kMax; ++c) {
        glActiveTexture(GL_TEXTURE0 + startUnit + static_cast<unsigned int>(c));
        glBindTexture(GL_TEXTURE_CUBE_MAP, m_cubes[c]);
    }
}

} // namespace engine