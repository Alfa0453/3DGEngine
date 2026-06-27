#include "engine/graphics/SpotShadow.h"

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
void main() { gl_Position = uLightVP * uModel * vec4(aPos, 1.0); }
)GLSL";
const char* kFrag = R"GLSL(
#version 330 core
void main() { }
)GLSL";

} // namespace

SpotShadow::SpotShadow(int size) : m_size(size), m_shader(kVert, kFrag) {
    glGenFramebuffers(1, &m_fbo);
    glGenTextures(kMax, m_maps);
    for (int i = 0; i < kMax; ++i) {
        glBindTexture(GL_TEXTURE_2D, m_maps[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, m_size, m_size, 0,
                     GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        const float border[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
        m_vp[i] = glm::mat4(1.0f);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

SpotShadow::~SpotShadow() {
    glDeleteTextures(kMax, m_maps);
    glDeleteFramebuffers(1, &m_fbo);
}

void SpotShadow::Generate(ecs::Registry& reg, const std::vector<Spot>& spots) {
    m_count = std::min(static_cast<int>(spots.size()), kMax);

    GLint prevFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    glViewport(0, 0, m_size, m_size);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    m_shader.Bind();

    for (int i = 0; i < m_count; ++i) {
        const Spot& sp = spots[static_cast<std::size_t>(i)];
        const glm::vec3 dir = glm::normalize(sp.direction);
        glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
        if (glm::abs(glm::dot(dir, up)) > 0.99f) up = glm::vec3(0.0f, 0.0f, 1.0f);
        const float fov = glm::clamp(sp.outerAngle * 2.0f + 4.0f, 10.0f, 170.0f);
        const glm::mat4 proj = glm::perspective(glm::radians(fov), 1.0f, 0.1f, sp.range);
        const glm::mat4 view = glm::lookAt(sp.position, sp.position + dir, up);
        m_vp[i] = proj * view;

        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_maps[i], 0);
        glClear(GL_DEPTH_BUFFER_BIT);
        m_shader.SetMat4("uLightVP", m_vp[i]);
        reg.view<Transform, MeshPBR>().each([&](Entity, Transform& t, MeshPBR& m) {
            if (!m.mesh) return;
            const glm::vec3& e = m.material.emissive;
            if (e.x > 0.0f || e.y > 0.0f || e.z > 0.0f) return;   // skip light gizmos
            m_shader.SetMat4("uModel", t.Model());
            m.mesh->Draw();
        });
    }

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
}

void SpotShadow::BindMaps(unsigned int startUnit) const {
    for (int i = 0; i < kMax; ++i) {
        glActiveTexture(GL_TEXTURE0 + startUnit + static_cast<unsigned int>(i));
        glBindTexture(GL_TEXTURE_2D, m_maps[i]);
    }
}

} // namespace engine
