#include "engine/graphics/Framebuffer.h"

#include <glad/glad.h>

#include <stdexcept>

namespace engine {

Framebuffer::Framebuffer(int width, int height, unsigned int internalFormat, bool depth)
    : m_width(width), m_height(height), m_format(internalFormat), m_hasDepth(depth) {
    Create();
}

void Framebuffer::Create() {
    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

    glGenTextures(1, &m_colorTex);
    glBindTexture(GL_TEXTURE_2D, m_colorTex);
    // The data type only has to be compatible; GL_FLOAT works for 8-bit and FP.
    glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(m_format), m_width, m_height, 0,
                 GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_colorTex, 0);

    if (m_hasDepth) {
        glGenTextures(1, &m_depthTex);
        glBindTexture(GL_TEXTURE_2D, m_depthTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, m_width, m_height,
                     0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                               GL_TEXTURE_2D, m_depthTex, 0);
    }

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        throw std::runtime_error("Framebuffer: incomplete attachment");

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

Framebuffer::~Framebuffer() { Release(); }

void Framebuffer::Release() {
    if (m_colorTex) glDeleteTextures(1, &m_colorTex);
    if (m_depthTex) glDeleteTextures(1, &m_depthTex);
    if (m_fbo)      glDeleteFramebuffers(1, &m_fbo);
    m_colorTex = 0; m_depthTex = 0; m_fbo = 0;
}

Framebuffer::Framebuffer(Framebuffer&& o) noexcept
    : m_fbo(o.m_fbo), m_colorTex(o.m_colorTex), m_depthTex(o.m_depthTex),
      m_width(o.m_width), m_height(o.m_height), m_format(o.m_format), m_hasDepth(o.m_hasDepth) {
    o.m_fbo = 0; o.m_colorTex = 0; o.m_depthTex = 0;
}

Framebuffer& Framebuffer::operator=(Framebuffer&& o) noexcept {
    if (this != &o) {
        Release();
        m_fbo = o.m_fbo; m_colorTex = o.m_colorTex; m_depthTex = o.m_depthTex;
        m_width = o.m_width; m_height = o.m_height; m_format = o.m_format; m_hasDepth = o.m_hasDepth;
        o.m_fbo = 0; o.m_colorTex = 0; o.m_depthTex = 0;
    }
    return *this;
}

void Framebuffer::Bind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, m_width, m_height);
}

void Framebuffer::BindDefault(int screenWidth, int screenHeight) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, screenWidth, screenHeight);
}

void Framebuffer::BindColorTexture(unsigned int unit) const {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, m_colorTex);
}

void Framebuffer::BindDepthTexture(unsigned int unit) const {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, m_depthTex);
}

void Framebuffer::Resize(int width, int height) {
    if (width == m_width && height == m_height) return;
    m_width = width; m_height = height;
    Release();
    Create();
}

} // namespace engine
