#include "engine/graphics/FoliageRenderer.h"
#include "engine/graphics/Frustum.h"

#include "engine/ecs/Components.h"
#include "engine/ecs/Registry.h"
#include "engine/graphics/Camera.h"
#include "engine/graphics/Model.h"
#include "engine/graphics/Shader.h"
#include "engine/graphics/Texture.h"
#include "engine/graphics/GpuProfiler.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace engine {
namespace {

glm::mat4 InstanceMatrix(const ecs::Transform& owner,
                         const ecs::FoliageInstance& instance) {
    glm::mat4 local(1.0f);
    local = glm::translate(local, instance.position);
    local = glm::rotate(local, glm::radians(instance.rotationDegrees.y), glm::vec3(0, 1, 0));
    local = glm::rotate(local, glm::radians(instance.rotationDegrees.x), glm::vec3(1, 0, 0));
    local = glm::rotate(local, glm::radians(instance.rotationDegrees.z), glm::vec3(0, 0, 1));
    local = glm::scale(local, instance.scale);
    return owner.Model() * local;
}

} // namespace

FoliageRenderer::FoliageRenderer() {
    static const char* vertex = R"glsl(
#version 330 core
layout(location=0) in vec3 aPosition;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUv;
layout(location=4) in vec4 iModel0;
layout(location=5) in vec4 iModel1;
layout(location=6) in vec4 iModel2;
layout(location=7) in vec4 iModel3;
uniform mat4 uViewProjection;
uniform float uTime;
uniform float uWindStrength;
uniform vec3 uCameraPos;
uniform float uCullStart;
uniform float uCullEnd;
out vec3 vNormal;
out vec2 vUv;
out float vFade;
void main() {
    mat4 model = mat4(iModel0, iModel1, iModel2, iModel3);
    vec4 world = model * vec4(aPosition, 1.0);
    // Wind sway: bend by a sine wave scaled by the vertex's local height so the base
    // stays planted while the canopy moves. Phase offset by world position so nearby
    // instances don't sway in lockstep.
    if (uWindStrength > 0.0) {
        float height = max(aPosition.y, 0.0);
        float phase = uTime * 1.7 + world.x * 0.35 + world.z * 0.35;
        vec2 sway = vec2(sin(phase), cos(phase * 0.8)) * uWindStrength * 0.15 * height;
        world.xz += sway;
    }
    // Distance fade band: 1 at/inside cullStart, 0 at cullEnd (dithered in the frag).
    float dist = length(world.xyz - uCameraPos);
    vFade = (uCullEnd > uCullStart)
        ? clamp((uCullEnd - dist) / max(uCullEnd - uCullStart, 0.001), 0.0, 1.0)
        : 1.0;
    vNormal = normalize(mat3(transpose(inverse(model))) * aNormal);
    vUv = aUv;
    gl_Position = uViewProjection * world;
}
)glsl";
    static const char* fragment = R"glsl(
#version 330 core
in vec3 vNormal;
in vec2 vUv;
in float vFade;
uniform vec3 uColor;
uniform vec3 uSunDirection;
uniform vec3 uSunColor;
uniform vec3 uAmbient;
uniform sampler2D uAlbedo;
uniform int uHasAlbedo;
uniform float uAlphaCutoff;
out vec4 FragColor;
// Ordered 4x4 Bayer dither so the distance fade dissolves cleanly without needing
// alpha blending or back-to-front sorting (the pass stays opaque + alpha-cutout).
float DitherThreshold(vec2 fragCoord) {
    int x = int(mod(fragCoord.x, 4.0));
    int y = int(mod(fragCoord.y, 4.0));
    int index = x + y * 4;
    float bayer[16] = float[16](
         0.0/16.0,  8.0/16.0,  2.0/16.0, 10.0/16.0,
        12.0/16.0,  4.0/16.0, 14.0/16.0,  6.0/16.0,
         3.0/16.0, 11.0/16.0,  1.0/16.0,  9.0/16.0,
        15.0/16.0,  7.0/16.0, 13.0/16.0,  5.0/16.0);
    return bayer[index];
}
void main() {
    // Fade out with distance: fewer fragments survive as vFade drops toward 0.
    if (vFade < DitherThreshold(gl_FragCoord.xy)) discard;
    vec4 sampleColor = (uHasAlbedo == 1) ? texture(uAlbedo, vUv) : vec4(1.0);
    if (sampleColor.a < uAlphaCutoff) discard;
    float ndl = max(dot(normalize(vNormal), normalize(-uSunDirection)), 0.0);
    vec3 light = uAmbient + uSunColor * (0.2 + ndl * 0.8);
    FragColor = vec4(uColor * sampleColor.rgb * light, sampleColor.a);
}
)glsl";
    m_shader = std::make_unique<Shader>(vertex, fragment);
    glGenBuffers(1, &m_instanceVbo);
}

FoliageRenderer::~FoliageRenderer() {
    if (m_instanceVbo) glDeleteBuffers(1, &m_instanceVbo);
}

void FoliageRenderer::Draw(ecs::Registry& registry, const Camera& camera, float aspect,
                           const glm::vec3& sunDirection, const glm::vec3& sunColor,
                           const glm::vec3& ambient, float time) {
    m_visibleInstances = 0;
    m_drawCalls = 0;
    if (!m_shader) return;
    auto foliageView = registry.view<ecs::Transform, ecs::FoliageComponent>();
    if (foliageView.empty()) return;

    const glm::mat4 viewProj = camera.ProjectionMatrix(aspect) * camera.ViewMatrix();
    m_shader->Bind();
    m_shader->SetMat4("uViewProjection", viewProj);
    m_shader->SetVec3("uSunDirection", sunDirection);
    m_shader->SetVec3("uSunColor", sunColor);
    m_shader->SetVec3("uAmbient", ambient);
    m_shader->SetFloat("uAlphaCutoff", 0.35f);
    m_shader->SetInt("uAlbedo", 0);
    m_shader->SetFloat("uTime", time);
    m_shader->SetVec3("uCameraPos", camera.Position());
    const Frustum viewFrustum = ExtractFrustum(viewProj);

    const GLboolean cull = glIsEnabled(GL_CULL_FACE);
    glDisable(GL_CULL_FACE); // leaves and cards are commonly two-sided
    const Texture* boundAlbedo = nullptr;

    foliageView.each(
        [&](ecs::Entity, ecs::Transform& owner, ecs::FoliageComponent& foliage) {
            if (!foliage.visible) return;
            for (std::size_t typeIndex = 0; typeIndex < foliage.types.size(); ++typeIndex) {
                const ecs::FoliageTypeRuntime& type = foliage.types[typeIndex];
                if (!type.model) continue;
                // Per-type wind + distance-fade band (authored on the foliage asset).
                m_shader->SetFloat("uWindStrength", type.windStrength);
                m_shader->SetFloat("uCullStart", type.cullStartDistance);
                m_shader->SetFloat("uCullEnd", std::max(type.cullEndDistance, type.cullStartDistance + 0.001f));
                // Keep the placement batch, but select one optional authored mesh
                // per distance tier. A missing tier always falls back to LOD0.
                const Model* lodModels[3] = {type.model, type.lod1Model, type.lod2Model};
                auto& matrices = m_lodMatrices;
                for (auto& tier : matrices) {
                    tier.clear();
                    if (tier.capacity() < foliage.instances.size()) tier.reserve(foliage.instances.size());
                }
                for (const ecs::FoliageInstance& instance : foliage.instances) {
                    if (!instance.enabled || instance.typeIndex != typeIndex) continue;
                    const glm::mat4 model = InstanceMatrix(owner, instance);
                    const glm::vec3 worldPosition = glm::vec3(model[3]);
                    const glm::vec3 cameraDelta = worldPosition - camera.Position();
                    const float distanceSq = glm::dot(cameraDelta, cameraDelta);
                    const float cullEnd = std::max(type.cullEndDistance, 0.0f);
                    if (distanceSq > cullEnd * cullEnd) continue;
                    const glm::vec3 boundsCenter = glm::vec3(
                        model * glm::vec4(type.model->Center(), 1.0f));
                    const float scaleX = glm::length(glm::vec3(model[0]));
                    const float scaleY = glm::length(glm::vec3(model[1]));
                    const float scaleZ = glm::length(glm::vec3(model[2]));
                    const float boundsRadius = type.model->BoundingRadius()
                        * std::max({scaleX, scaleY, scaleZ});
                    if (!SphereInFrustum(viewFrustum, boundsCenter, boundsRadius)) continue;
                    int lod = 0;
                    if (type.lod2Model && distanceSq >= type.lod2Distance * type.lod2Distance) lod = 2;
                    else if (type.lod1Model && distanceSq >= type.lod1Distance * type.lod1Distance) lod = 1;
                    matrices[lod].push_back(model);
                }
                for (int lod = 0; lod < 3; ++lod) {
                    if (matrices[lod].empty() || !lodModels[lod]) continue;
                    m_visibleInstances += static_cast<int>(matrices[lod].size());

                    glBindBuffer(GL_ARRAY_BUFFER, m_instanceVbo);
                    const std::size_t instanceBytes = matrices[lod].size() * sizeof(glm::mat4);
                    if (instanceBytes > m_instanceCapacity) {
                        m_instanceCapacity = std::max(instanceBytes,
                            std::max<std::size_t>(m_instanceCapacity * 2, 4096));
                        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(m_instanceCapacity),
                                     nullptr, GL_DYNAMIC_DRAW);
                    }
                    glBufferSubData(GL_ARRAY_BUFFER, 0,
                                    static_cast<GLsizeiptr>(instanceBytes), matrices[lod].data());

                    const Model* lodModel = lodModels[lod];
                    const auto& subMeshes = lodModel->SubMeshes();
                    const auto& materials = lodModel->Materials();
                    const auto& textures = lodModel->Textures();
                    for (const SubMesh& subMesh : subMeshes) {
                    const Material* sourceMaterial =
                        subMesh.material >= 0
                        && subMesh.material < static_cast<int>(materials.size())
                        ? &materials[static_cast<std::size_t>(subMesh.material)] : nullptr;
                    glm::vec3 color = type.material.albedo;
                    if (color == glm::vec3(0.8f) && sourceMaterial)
                        color = sourceMaterial->diffuse;
                    m_shader->SetVec3("uColor", color);

                    const Texture* albedo = type.material.albedoMap;
                    if (!albedo && sourceMaterial && sourceMaterial->diffuseMap >= 0
                        && sourceMaterial->diffuseMap < static_cast<int>(textures.size())) {
                        albedo = textures[static_cast<std::size_t>(sourceMaterial->diffuseMap)].get();
                    }
                    m_shader->SetInt("uHasAlbedo", albedo ? 1 : 0);
                    if (albedo && albedo != boundAlbedo) {
                        albedo->Bind(0);
                        boundAlbedo = albedo;
                    }

                    glBindVertexArray(subMesh.mesh.Vao());
                    glBindBuffer(GL_ARRAY_BUFFER, m_instanceVbo);
                    for (int column = 0; column < 4; ++column) {
                        const GLuint location = static_cast<GLuint>(4 + column);
                        glEnableVertexAttribArray(location);
                        glVertexAttribPointer(location, 4, GL_FLOAT, GL_FALSE,
                            static_cast<GLsizei>(sizeof(glm::mat4)),
                            reinterpret_cast<void*>(static_cast<std::size_t>(column)
                                                    * sizeof(glm::vec4)));
                        glVertexAttribDivisor(location, 1);
                    }
                        glDrawElementsInstanced(GL_TRIANGLES,
                            static_cast<GLsizei>(subMesh.mesh.IndexCount()), GL_UNSIGNED_INT,
                            nullptr, static_cast<GLsizei>(matrices[lod].size()));
                        GpuProfiler::RecordDrawCall();
                    for (GLuint location = 4; location <= 7; ++location) {
                        glVertexAttribDivisor(location, 0);
                        glDisableVertexAttribArray(location);
                    }
                        ++m_drawCalls;
                    }
                }
            }
        });

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    if (cull) glEnable(GL_CULL_FACE);
}

} // namespace engine
