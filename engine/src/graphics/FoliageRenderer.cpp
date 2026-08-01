#include "engine/graphics/FoliageRenderer.h"

#include "engine/ecs/Components.h"
#include "engine/ecs/Registry.h"
#include "engine/graphics/Camera.h"
#include "engine/graphics/Model.h"
#include "engine/graphics/Shader.h"
#include "engine/graphics/Texture.h"

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
out vec3 vNormal;
out vec2 vUv;
void main() {
    mat4 model = mat4(iModel0, iModel1, iModel2, iModel3);
    vec4 world = model * vec4(aPosition, 1.0);
    vNormal = normalize(mat3(transpose(inverse(model))) * aNormal);
    vUv = aUv;
    gl_Position = uViewProjection * world;
}
)glsl";
    static const char* fragment = R"glsl(
#version 330 core
in vec3 vNormal;
in vec2 vUv;
uniform vec3 uColor;
uniform vec3 uSunDirection;
uniform vec3 uSunColor;
uniform vec3 uAmbient;
uniform sampler2D uAlbedo;
uniform int uHasAlbedo;
uniform float uAlphaCutoff;
out vec4 FragColor;
void main() {
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
                           const glm::vec3& ambient) {
    m_visibleInstances = 0;
    m_drawCalls = 0;
    if (!m_shader) return;

    m_shader->Bind();
    m_shader->SetMat4("uViewProjection", camera.ProjectionMatrix(aspect) * camera.ViewMatrix());
    m_shader->SetVec3("uSunDirection", sunDirection);
    m_shader->SetVec3("uSunColor", sunColor);
    m_shader->SetVec3("uAmbient", ambient);
    m_shader->SetFloat("uAlphaCutoff", 0.35f);
    m_shader->SetInt("uAlbedo", 0);

    const GLboolean cull = glIsEnabled(GL_CULL_FACE);
    glDisable(GL_CULL_FACE); // leaves and cards are commonly two-sided

    registry.view<ecs::Transform, ecs::FoliageComponent>().each(
        [&](ecs::Entity, ecs::Transform& owner, ecs::FoliageComponent& foliage) {
            if (!foliage.visible) return;
            for (std::size_t typeIndex = 0; typeIndex < foliage.types.size(); ++typeIndex) {
                const ecs::FoliageTypeRuntime& type = foliage.types[typeIndex];
                if (!type.model) continue;

                std::vector<glm::mat4> matrices;
                matrices.reserve(foliage.instances.size());
                for (const ecs::FoliageInstance& instance : foliage.instances) {
                    if (!instance.enabled || instance.typeIndex != typeIndex) continue;
                    const glm::mat4 model = InstanceMatrix(owner, instance);
                    const glm::vec3 worldPosition = glm::vec3(model[3]);
                    const float distance = glm::length(worldPosition - camera.Position());
                    if (distance > std::max(type.cullEndDistance, 0.0f)) continue;
                    matrices.push_back(model);
                }
                if (matrices.empty()) continue;
                m_visibleInstances += static_cast<int>(matrices.size());

                glBindBuffer(GL_ARRAY_BUFFER, m_instanceVbo);
                glBufferData(GL_ARRAY_BUFFER,
                    static_cast<GLsizeiptr>(matrices.size() * sizeof(glm::mat4)),
                    matrices.data(), GL_DYNAMIC_DRAW);

                const auto& subMeshes = type.model->SubMeshes();
                const auto& materials = type.model->Materials();
                const auto& textures = type.model->Textures();
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
                    if (albedo) albedo->Bind(0);

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
                        nullptr, static_cast<GLsizei>(matrices.size()));
                    for (GLuint location = 4; location <= 7; ++location) {
                        glVertexAttribDivisor(location, 0);
                        glDisableVertexAttribArray(location);
                    }
                    ++m_drawCalls;
                }
            }
        });

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    if (cull) glEnable(GL_CULL_FACE);
}

} // namespace engine
