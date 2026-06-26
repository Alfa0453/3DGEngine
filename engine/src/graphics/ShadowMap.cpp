#include "engine/graphics/ShadowMap.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

#include <utility>

namespace engine {

ShadowMap::ShadowMap(int size) : m_size(size) {
    glGenFramebuffers(1, &m_fbo);

    glGenTextures(1, &m_depthTex);
    glBindTexture(GL_TEXTURE_2D, m_depthTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, m_size, m_size, 0,
                 GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    // Clamp to a white border so anything outside the light's frustum is lit.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    const float border[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);

    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_depthTex, 0);
    glDrawBuffer(GL_NONE);  // depth only — no colour buffer
    glReadBuffer(GL_NONE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

ShadowMap::~ShadowMap() { Release(); }

void ShadowMap::Release() {
    if (m_depthTex) glDeleteTextures(1, &m_depthTex);
    if (m_fbo)      glDeleteFramebuffers(1, &m_fbo);
    m_depthTex = 0;
    m_fbo = 0;
}

ShadowMap::ShadowMap(ShadowMap&& o) noexcept
    : m_fbo(o.m_fbo), m_depthTex(o.m_depthTex), m_size(o.m_size) {
        o.m_fbo = 0; o.m_depthTex = 0;
}

ShadowMap& ShadowMap::operator=(ShadowMap&& o) noexcept {
    if (this != &o) {
        Release();
        m_fbo = o.m_fbo; m_depthTex = o.m_depthTex; m_size = o.m_size;
        o.m_fbo = 0; o.m_depthTex = 0;
    }
    return *this;
}

void ShadowMap::BeginDepthPass() const {
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &m_prevFbo);
    glViewport(0, 0, m_size, m_size);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glClear(GL_DEPTH_BUFFER_BIT);
}

void ShadowMap::EndDepthPass(int screenWidth, int screenHeight) const {
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<unsigned int>(m_prevFbo));
    glViewport(0, 0, screenWidth, screenHeight);
}

void ShadowMap::BindDepthTexture(unsigned int unit) const {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, m_depthTex);
}

glm::mat4 ShadowMap::LightSpaceMatrix(const glm::vec3& lightDir, const glm::vec3& center, float radius) {
    const glm::vec3 dir = glm::normalize(lightDir);
    const glm::vec3 eye = center - dir * (radius * 2.0f);
    glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
    if (glm::abs(glm::dot(dir, up)) > 0.99f) up = glm::vec3(0.0f, 0.0f, 1.0f);
    const glm::mat4 view = glm::lookAt(eye, center, up);
    const glm::mat4 proj = glm::ortho(-radius, radius, -radius, radius, 0.1f, radius * 4.0f);

    return proj * view;
}

} // namespace engine