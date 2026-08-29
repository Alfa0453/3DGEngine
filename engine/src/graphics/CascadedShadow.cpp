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

// Light-space matrix that bounds one camera sub-frustum. Uses a rotation-invariant
// bounding sphere for a constant ortho extent and snaps the centre to whole shadow texels,
// so shadow edges do not shimmer / crawl as the camera moves (stabilised CSM). mapSize is
// the shadow map resolution (for the texel grid).
struct CascadeFit {
    glm::mat4 viewProjection{1.0f};
    float worldTexelSize = 1.0f;
    float depthRange = 1.0f;
};

CascadeFit FitCascade(const glm::mat4& camView, const glm::mat4& subProj,
                      const glm::vec3& lightDir, int mapSize) {
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

    // Bounding sphere: its radius does not change as the camera rotates, so the ortho box
    // keeps a constant size (no size shimmer). Quantise the radius to stop sub-frame wobble.
    float radius = 0.0f;
    for (const auto& c : corners) radius = std::max(radius, glm::length(c - center));
    radius = std::ceil(radius * 16.0f) / 16.0f;

    const glm::vec3 dir = glm::normalize(lightDir);
    const glm::vec3 up = (std::abs(glm::dot(dir, glm::vec3(0, 1, 0))) > 0.99f)
                       ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);

    // Texel snapping: quantise the centre on the light's X/Y axes to whole shadow texels, so
    // the shadow grid is fixed in world space and edges don't crawl as the camera translates.
    const float worldPerTexel = (radius * 2.0f) / static_cast<float>(std::max(mapSize, 1));
    const glm::mat3 lightBasis = glm::mat3(glm::lookAt(glm::vec3(0.0f), -dir, up));   // world -> light rotation
    glm::vec3 cLight = lightBasis * center;
    cLight.x = std::floor(cLight.x / worldPerTexel) * worldPerTexel;
    cLight.y = std::floor(cLight.y / worldPerTexel) * worldPerTexel;
    center = glm::transpose(lightBasis) * cLight;   // orthonormal basis -> inverse is transpose

    const glm::mat4 lightView = glm::lookAt(center - dir * radius, center, up);
    // Pull the near/far planes out so occluders behind the slice still cast.
    const float zMult = 10.0f;
    const glm::mat4 lightProj = glm::ortho(-radius, radius, -radius, radius,
                                           -radius * zMult, radius * zMult);
    CascadeFit fit;
    fit.viewProjection = lightProj * lightView;
    fit.worldTexelSize = worldPerTexel;
    fit.depthRange = 2.0f * radius * zMult;
    return fit;
}

} // namespace

CascadedShadow::CascadedShadow(int size) : m_size(size), m_shader(kVert, kFrag) {
    for (glm::mat4& matrix : m_vp) matrix = glm::mat4(1.0f);
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
                              const glm::vec3& lightDir, float shadowFar,
                              const std::function<void(const glm::mat4&)>& drawExtraCasters) {
    ++m_frameIndex;
    m_renderedLastFrame = 0;
    m_reusedLastFrame = 0;
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

    std::array<CascadeFit, kCascades> fits;
    for (int i = 0; i < kCascades; ++i) {
        const float cn = (i == 0) ? near : splitFar[i - 1];
        const glm::mat4 subProj = glm::perspective(glm::radians(camera.fov), aspect, cn, splitFar[i]);
        fits[static_cast<std::size_t>(i)] = FitCascade(camView, subProj, lightDir, m_size);
    }
    const std::uint64_t casterRevision = ComputeShadowCasterRevision(reg);
    const glm::vec3 normalizedLight = glm::normalize(lightDir);
    const bool globalDirty = m_forceUpdateEveryFrame || !m_cacheValid || casterRevision != m_casterRevision
        || glm::distance(normalizedLight, m_lastLightDirection) > 0.0005f
        || std::abs(shadowFar - m_lastShadowFar) > 0.01f;
    std::array<bool, kCascades> dirty{};
    bool anyDirty = false;
    for (int i = 0; i < kCascades; ++i) {
        float matrixDelta = 0.0f;
        if (m_cacheValid) {
            for (int column = 0; column < 4; ++column)
                for (int row = 0; row < 4; ++row)
                    matrixDelta = std::max(matrixDelta, std::abs(
                        fits[static_cast<std::size_t>(i)].viewProjection[column][row] - m_vp[i][column][row]));
        }
        const std::uint64_t age = m_frameIndex - m_lastUpdateFrame[static_cast<std::size_t>(i)];
        const bool scheduled = age >= m_updateIntervals[static_cast<std::size_t>(i)];
        const bool largeCameraChange = matrixDelta > (0.035f + 0.015f * static_cast<float>(i));
        const bool dynamicCasterDue = static_cast<bool>(drawExtraCasters)
            && (i == 0 || scheduled);
        dirty[static_cast<std::size_t>(i)] = globalDirty || largeCameraChange
            || (matrixDelta > 1.0e-5f && (i == 0 || scheduled)) || dynamicCasterDue;
        if (dirty[static_cast<std::size_t>(i)]) { anyDirty = true; ++m_renderedLastFrame; }
        else ++m_reusedLastFrame;
    }
    m_casterRevision = casterRevision;
    m_lastLightDirection = normalizedLight;
    m_lastShadowFar = shadowFar;
    m_cacheValid = true;
    if (!anyDirty) return;

    GLint prevFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    const GLboolean polygonOffsetWasEnabled = glIsEnabled(GL_POLYGON_OFFSET_FILL);
    const GLboolean cullFaceWasEnabled = glIsEnabled(GL_CULL_FACE);
    GLfloat previousPolygonOffsetFactor = 0.0f;
    GLfloat previousPolygonOffsetUnits = 0.0f;
    glGetFloatv(GL_POLYGON_OFFSET_FACTOR, &previousPolygonOffsetFactor);
    glGetFloatv(GL_POLYGON_OFFSET_UNITS, &previousPolygonOffsetUnits);

    glViewport(0, 0, m_size, m_size);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    // Push caster depth away from the light while building the shadow maps.
    // Without this, large coplanar receivers repeatedly shadow themselves and
    // expose the individual PCF levels as broad bands (shadow acne).
    glEnable(GL_POLYGON_OFFSET_FILL);
    // Receiver bias is cascade/world-texel aware; keep caster offset modest so
    // contact shadows are not detached from walls and floors.
    glPolygonOffset(1.1f, 1.5f);
    // Depth-only shadow casting is deliberately two-sided. Thin authored walls
    // and ceilings must block the sun regardless of winding; normal material
    // rendering keeps its existing back-face culling policy.
    glDisable(GL_CULL_FACE);
    m_shader.Bind();
    m_batch.Build(reg);

    for (int i = 0; i < kCascades; ++i) {
        if (!dirty[static_cast<std::size_t>(i)]) continue;
        const CascadeFit& fit = fits[static_cast<std::size_t>(i)];
        m_vp[i] = fit.viewProjection;
        m_worldTexelSize[i] = fit.worldTexelSize;
        m_depthRange[i] = fit.depthRange;

        glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, m_texArray, 0, i);
        glClear(GL_DEPTH_BUFFER_BIT);
        m_shader.SetMat4("uLightVP", m_vp[i]);
        m_batch.Draw(m_shader);
        if (drawExtraCasters) drawExtraCasters(m_vp[i]);   // skinned / non-ECS casters
        m_lastUpdateFrame[static_cast<std::size_t>(i)] = m_frameIndex;
    }

    glPolygonOffset(previousPolygonOffsetFactor, previousPolygonOffsetUnits);
    if (!polygonOffsetWasEnabled) glDisable(GL_POLYGON_OFFSET_FILL);
    if (cullFaceWasEnabled) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
}

void CascadedShadow::SetUpdateIntervals(
    const std::array<std::uint32_t, kCascades>& intervals) {
    for (int i = 0; i < kCascades; ++i)
        m_updateIntervals[static_cast<std::size_t>(i)] =
            std::clamp(intervals[static_cast<std::size_t>(i)], 1u, 60u);
}

std::uint64_t CascadedShadow::MemoryBytes() const {
    return static_cast<std::uint64_t>(m_size) * m_size * kCascades * sizeof(float);
}

void CascadedShadow::BindArray(unsigned int unit) const {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_texArray);
}

} // namespace engine
