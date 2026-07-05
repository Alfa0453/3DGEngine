#include "engine/graphics/CascadedShadow.h"

#include "engine/graphics/Camera.h"
#include "engine/ecs/Registry.h"
#include "engine/ecs/Components.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cmath>
#include <limits>

using engine::ecs::Entity;
using engine::ecs::Transform;
using engine::ecs::MeshPBR;

namespace engine {
namespace {

const char* kVert = R"GLSL(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 3) in vec4 aIModel0;
layout (location = 4) in vec4 aIModel1;
layout (location = 5) in vec4 aIModel2;
layout (location = 6) in vec4 aIModel3;
uniform int  uInstanced;
uniform mat4 uModel;
uniform mat4 uLightVP;
void main() {
    mat4 model = (uInstanced == 1) ? mat4(aIModel0, aIModel1, aIModel2, aIModel3) : uModel;
    gl_Position = uLightVP * model * vec4(aPos, 1.0);
}
)GLSL";
const char* kFrag = R"GLSL(
#version 330 core
void main() { }
)GLSL";

// Light-space matrix that tightly bounds one camera sub-frustum.
glm::mat4 FitCascade(const glm::mat4& camView, const glm::mat4& subProj,
                     const glm::vec3& lightDir) {
    const glm::mat4 inv = glm::inverse(subProj * camView);
    std::array<glm::vec3, 8> corners;
    int n = 0;
    for (int x = 0; x < 2; ++x)
        for (int y = 0; y < 2; ++y)
            for (int z = 0; z < 2; ++z) {
                const glm::vec4 pt = inv * glm::vec4(2.0f * x - 1.0f, 2.0f * y - 1.0f, 2.0f * z - 1.0f, 1.0f);
                corners[static_cast<std::size_t>(n++)] = glm::vec3(pt) / pt.w;
            }

    glm::vec3 center(0.0f);
    for (const auto& c : corners) center += c;
    center /= 8.0f;

    glm::vec3 up = (std::abs(glm::dot(lightDir, glm::vec3(0, 1, 0))) > 0.99f)
                 ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
    const glm::mat4 lightView = glm::lookAt(center - glm::normalize(lightDir), center, up);

    float minX = 1e30f, minY = 1e30f, minZ = 1e30f;
    float maxX = -1e30f, maxY = -1e30f, maxZ = -1e30f;
    for (const auto& c : corners) {
        const glm::vec3 lc = glm::vec3(lightView * glm::vec4(c, 1.0f));
        minX = std::min(minX, lc.x); maxX = std::max(maxX, lc.x);
        minY = std::min(minY, lc.y); maxY = std::max(maxY, lc.y);
        minZ = std::min(minZ, lc.z); maxZ = std::max(maxZ, lc.z);
    }
    // Pull the near/far planes out so occluders behind the slice still cast.
    const float zMult = 10.0f;
    minZ = (minZ < 0.0f) ? minZ * zMult : minZ / zMult;
    maxZ = (maxZ < 0.0f) ? maxZ / zMult : maxZ * zMult;

    const glm::mat4 lightProj = glm::ortho(minX, maxX, minY, maxY, minZ, maxZ);
    return lightProj * lightView;
}

} // namespace

CascadedShadow::CascadedShadow(int size) : m_size(size), m_shader(kVert, kFrag) {
    glGenFramebuffers(1, &m_fbo);
    glGenTextures(1, &m_texArray);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_texArray);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT32F, m_size, m_size, kCascades,
                 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    const float border[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    glTexParameterfv(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BORDER_COLOR, border);

    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

CascadedShadow::~CascadedShadow() {
    glDeleteTextures(1, &m_texArray);
    glDeleteFramebuffers(1, &m_fbo);
}

void CascadedShadow::Generate(ecs::Registry& reg, const Camera& camera, float aspect,
                              const glm::vec3& lightDir, float shadowFar) {
    const glm::mat4 camView = camera.ViewMatrix();
    const float near = camera.nearPlane;

    // Practical split scheme: blend of logarithmic and uniform.
    float splitFar[kCascades];
    for (int i = 0; i < kCascades; ++i) {
        const float si = static_cast<float>(i + 1) / static_cast<float>(kCascades);
        const float logd = near * std::pow(shadowFar / near, si);
        const float lind = near + (shadowFar - near) * si;
        splitFar[i] = 0.5f * logd + 0.5f * lind;
        m_splits[i] = splitFar[i];
    }

    GLint prevFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    glViewport(0, 0, m_size, m_size);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    m_shader.Bind();
    m_batch.Build(reg);

    for (int i = 0; i < kCascades; ++i) {
        const float cn = (i == 0) ? near : splitFar[i - 1];
        const glm::mat4 subProj = glm::perspective(glm::radians(camera.fov), aspect, cn, splitFar[i]);
        m_vp[i] = FitCascade(camView, subProj, lightDir);

        glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, m_texArray, 0, i);
        glClear(GL_DEPTH_BUFFER_BIT);
        m_shader.SetMat4("uLightVP", m_vp[i]);
        m_batch.Draw(m_shader);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
}

void CascadedShadow::BindArray(unsigned int unit) const {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_texArray);
}

} // namespace engine
