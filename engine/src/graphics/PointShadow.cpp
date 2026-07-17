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
layout (location = 2) in vec2 aUV;
layout (location = 3) in vec4 aIModel0;
layout (location = 4) in vec4 aIModel1;
layout (location = 5) in vec4 aIModel2;
layout (location = 6) in vec4 aIModel3;
uniform int  uInstanced;
uniform mat4 uModel;
uniform mat4 uLightVP;
out vec3 vWorld;
out vec2 vUV;
void main() {
    mat4 model = (uInstanced == 1) ? mat4(aIModel0, aIModel1, aIModel2, aIModel3) : uModel;
    vec4 w = model * vec4(aPos, 1.0);
    vWorld = w.xyz;
    vUV = aUV;
    gl_Position = uLightVP * w;
}
)GLSL";

const char* kFrag = R"GLSL(
#version 330 core
in vec3 vWorld; in vec2 vUV; out float FragColor;
uniform vec3 uLightPos;
uniform int uAlphaMasked; uniform int uHasAlbedoMap; uniform float uAlphaCutoff; uniform float uOpacity;
uniform vec2 uUvScale, uUvOffset; uniform float uUvRotation; uniform sampler2D uAlbedoMap;
void main() { if (uAlphaMasked == 1) { float a=radians(uUvRotation); vec2 p=vUV*uUvScale-0.5;
vec2 uv=mat2(cos(a),-sin(a),sin(a),cos(a))*p+0.5+uUvOffset;
float alpha=uOpacity*((uHasAlbedoMap==1)?texture(uAlbedoMap,uv).a:1.0); if(alpha<uAlphaCutoff) discard; }
FragColor = length(vWorld - uLightPos); }
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
    GLfloat prevClear[4];
    glGetFloatv(GL_COLOR_CLEAR_VALUE, prevClear);

    glViewport(0, 0, m_faceSize, m_faceSize);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    m_shader.Bind();
    glClearColor(1.0e6f, 1.0e6f, 1.0e6f, 1.0e6f);  // unrendered = "very far" = lit

    const glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, m_far);
    const glm::vec3 dir[6] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    const glm::vec3 up[6]  = {{0,-1,0},{0,-1,0},{0,0,1},{0,0,-1},{0,-1,0},{0,-1,0}};
   
    m_batch.Build(reg);
    for (int i = 0; i < m_count; ++i) {
        const glm::vec3 lp = lights[static_cast<std::size_t>(i)];
        m_shader.SetVec3("uLightPos", lp);
        for (unsigned int f = 0; f < 6; ++f) {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                   GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, m_cubes[i], 0);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            m_shader.SetMat4("uLightVP", proj * glm::lookAt(lp, lp + dir[f], up[f]));
            m_batch.Draw(m_shader);
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    glClearColor(prevClear[0], prevClear[1], prevClear[2], prevClear[3]);
}

void PointShadow::BindCubes(unsigned int startUnit) const {
    for (int c = 0; c < kMax; ++c) {
        glActiveTexture(GL_TEXTURE0 + startUnit + static_cast<unsigned int>(c));
        glBindTexture(GL_TEXTURE_CUBE_MAP, m_cubes[c]);
    }
}

} // namespace engine
