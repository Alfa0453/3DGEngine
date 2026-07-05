#include "engine/graphics/ShadowCasters.h"

#include "engine/graphics/Mesh.h"
#include "engine/graphics/Shader.h"
#include "engine/ecs/Registry.h"
#include "engine/ecs/Components.h"

#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>

#include <cstddef>

using engine::ecs::Entity;
using engine::ecs::Transform;
using engine::ecs::MeshPBR;

namespace engine {

ShadowCasterBatch::ShadowCasterBatch()  { glGenBuffers(1, &m_vbo); }
ShadowCasterBatch::~ShadowCasterBatch() { if (m_vbo) glDeleteBuffers(1, &m_vbo); }

void ShadowCasterBatch::Build(ecs::Registry &reg)
{
    m_data.clear();
    m_records.clear();
    m_textured.clear();

    std::unordered_map<const Mesh*, std::vector<float>> groups;
    reg.view<Transform, MeshPBR>().each([&](Entity, Transform& t, MeshPBR& m) {
        if (!m.mesh) return;
        const glm::vec3& e = m.material.emissive;
        if (e.x > 0.0f || e.y > 0.0f || e.z > 0.0f) return;    // skip light gizmos
        const glm::mat4 model = t.Model();
        // Textured meshes carry a tangent at location 3, which would clash with the
        // instance attributes -- draw those per-object instead.
        if (m.material.albedoMap || m.material.normalMap || m.material.metalRoughMap) {
            m_textured.emplace_back(m.mesh, model);
            return;
        }
        std::vector<float>& v = groups[m.mesh];
        const float* mp = glm::value_ptr(model);
        v.insert(v.end(), mp, mp + 16);
    });

    for (auto& kv : groups) {
        Record r;
        r.mesh = kv.first;
        r.offsetFloats = static_cast<int>(m_data.size());
        r.count = static_cast<int>(kv.second.size() / 16);
        m_data.insert(m_data.end(), kv.second.begin(), kv.second.end());
        m_records.push_back(r);
    }
    if (!m_data.empty()) {
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(m_data.size() * sizeof(float)),
                     m_data.data(), GL_DYNAMIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }
}

void ShadowCasterBatch::Draw(Shader &sh)
{
    if (!m_records.empty()) {
        sh.SetInt("uInstanced", 1);
        const GLsizei stride = 16 * static_cast<GLsizei>(sizeof(float));
        for (const Record& r : m_records) {
            glBindVertexArray(r.mesh->Vao());
            glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
            const std::size_t base = static_cast<std::size_t>(r.offsetFloats) * sizeof(float);
            for (int c = 0; c < 4; ++c) {
                const GLuint loc = static_cast<GLuint>(3 + c);
                glEnableVertexAttribArray(loc);
                glVertexAttribPointer(loc, 4, GL_FLOAT, GL_FALSE, stride,
                                      reinterpret_cast<void*>(base + static_cast<std::size_t>(c) * 4 * sizeof(float)));
                glVertexAttribDivisor(loc, 1);
            }
            glDrawElementsInstanced(GL_TRIANGLES, static_cast<GLsizei>(r.mesh->IndexCount()),
                                    GL_UNSIGNED_INT, nullptr, r.count);
            for (GLuint loc = 3; loc <= 6; ++loc) {
                glVertexAttribDivisor(loc, 0);
                glDisableVertexAttribArray(loc);
            }
        }
        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    if (!m_textured.empty()) {
        sh.SetInt("uInstanced", 0);
        for (const auto& pr : m_textured) {
            sh.SetMat4("uModel", pr.second);
            pr.first->Draw();
        }
    }
}

} // namespace engine
