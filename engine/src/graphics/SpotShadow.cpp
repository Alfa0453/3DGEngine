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
layout (location = 2) in vec2 aUV;
layout (location = 3) in vec4 aIModel0;
layout (location = 4) in vec4 aIModel1;
layout (location = 5) in vec4 aIModel2;
layout (location = 6) in vec4 aIModel3;
uniform int  uInstanced;
uniform mat4 uModel;
uniform mat4 uLightVP;
out vec2 vUV;
void main() {
    mat4 model = (uInstanced == 1) ? mat4(aIModel0, aIModel1, aIModel2, aIModel3) : uModel;
    gl_Position = uLightVP * model * vec4(aPos, 1.0);
    vUV = aUV;
}
)GLSL";
const char* kFrag = R"GLSL(
#version 330 core
in vec2 vUV; uniform int uAlphaMasked; uniform int uHasAlbedoMap;
uniform float uAlphaCutoff; uniform float uOpacity; uniform vec2 uUvScale, uUvOffset;
uniform float uUvRotation; uniform sampler2D uAlbedoMap;
void main() { if (uAlphaMasked == 1) { float a=radians(uUvRotation); vec2 p=vUV*uUvScale-0.5;
vec2 uv=mat2(cos(a),-sin(a),sin(a),cos(a))*p+0.5+uUvOffset;
float alpha=uOpacity*((uHasAlbedoMap==1)?texture(uAlbedoMap,uv).a:1.0); if(alpha<uAlphaCutoff) discard; } }
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
        m_batch.Draw(m_shader);
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
