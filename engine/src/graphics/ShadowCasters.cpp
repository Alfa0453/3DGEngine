#include "engine/graphics/ShadowCasters.h"

#include "engine/graphics/Mesh.h"
#include "engine/graphics/Model.h"
#include "engine/graphics/Shader.h"
#include "engine/ecs/Registry.h"
#include "engine/ecs/Components.h"
#include "engine/graphics/Texture.h"
#include "engine/graphics/GpuProfiler.h"

#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

using engine::ecs::Entity;
using engine::ecs::Transform;
using engine::ecs::MeshPBR;

namespace engine {
namespace {

void HashBytes(std::uint64_t& hash, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= static_cast<std::uint64_t>(bytes[i]);
        hash *= 1099511628211ull;
    }
}

template <typename T>
void HashValue(std::uint64_t& hash, const T& value) {
    HashBytes(hash, &value, sizeof(T));
}

glm::mat4 FoliageInstanceMatrix(const Transform& owner,
                                const ecs::FoliageInstance& instance) {
    glm::mat4 local(1.0f);
    local = glm::translate(local, instance.position);
    local = glm::rotate(local, glm::radians(instance.rotationDegrees.y), glm::vec3(0, 1, 0));
    local = glm::rotate(local, glm::radians(instance.rotationDegrees.x), glm::vec3(1, 0, 0));
    local = glm::rotate(local, glm::radians(instance.rotationDegrees.z), glm::vec3(0, 0, 1));
    return owner.Model() * glm::scale(local, instance.scale);
}

} // namespace

std::uint64_t ComputeShadowCasterRevision(ecs::Registry& reg) {
    std::uint64_t hash = 1469598103934665603ull;
    auto meshView = reg.view<Transform, MeshPBR>();
    if (!meshView.empty()) meshView.each([&](Entity entity, Transform& transform, MeshPBR& mesh) {
        if (!mesh.mesh || mesh.material.blendMode == ecs::PbrMaterial::BlendMode::Transparent)
            return;
        const glm::vec3& emissive = mesh.material.emissive;
        if (emissive.x > 0.0f || emissive.y > 0.0f || emissive.z > 0.0f) return;
        HashValue(hash, entity);
        const glm::mat4 model = transform.Model();
        HashBytes(hash, glm::value_ptr(model), sizeof(glm::mat4));
        const std::uintptr_t meshIdentity = reinterpret_cast<std::uintptr_t>(mesh.mesh);
        HashValue(hash, meshIdentity);
        HashValue(hash, mesh.material.blendMode);
        HashValue(hash, mesh.material.alphaCutoff);
        HashValue(hash, mesh.material.opacity);
        HashValue(hash, mesh.material.uvScale);
        HashValue(hash, mesh.material.uvOffset);
        HashValue(hash, mesh.material.uvRotation);
        const std::uintptr_t albedo = reinterpret_cast<std::uintptr_t>(mesh.material.albedoMap);
        HashValue(hash, albedo);
    });
    auto foliageView = reg.view<Transform, ecs::FoliageComponent>();
    if (!foliageView.empty()) foliageView.each(
        [&](Entity entity, Transform& owner, ecs::FoliageComponent& foliage) {
            if (!foliage.visible) return;
            HashValue(hash, entity);
            const glm::mat4 ownerModel = owner.Model();
            HashBytes(hash, glm::value_ptr(ownerModel), sizeof(glm::mat4));
            for (std::size_t typeIndex = 0; typeIndex < foliage.types.size(); ++typeIndex) {
                const auto& type = foliage.types[typeIndex];
                if (!type.castShadows || !type.model) continue;
                HashValue(hash, typeIndex);
                const std::uintptr_t modelIdentity = reinterpret_cast<std::uintptr_t>(type.model);
                HashValue(hash, modelIdentity);
            }
            for (const auto& instance : foliage.instances) {
                if (!instance.enabled || instance.typeIndex >= foliage.types.size()
                    || !foliage.types[instance.typeIndex].castShadows) continue;
                HashValue(hash, instance.typeIndex);
                HashValue(hash, instance.position);
                HashValue(hash, instance.rotationDegrees);
                HashValue(hash, instance.scale);
            }
        });
    auto modelView = reg.view<Transform, ecs::LoadedModelAsset>();
    if (!modelView.empty()) modelView.each(
        [&](Entity entity, Transform& transform, ecs::LoadedModelAsset& loaded) {
            if (!loaded.model || reg.Has<MeshPBR>(entity)) return;
            HashValue(hash, entity);
            const glm::mat4 model = transform.Model();
            HashBytes(hash, glm::value_ptr(model), sizeof(glm::mat4));
            const std::uintptr_t identity =
                reinterpret_cast<std::uintptr_t>(loaded.model);
            HashValue(hash, identity);
            HashValue(hash, loaded.model->Revision());
            const auto* overrideMaterial = reg.TryGet<ecs::LoadedMaterialAsset>(entity);
            for (const SubMesh& submesh : loaded.model->SubMeshes()) {
                const ecs::PbrMaterial material = ResolveModelPbrMaterial(
                    *loaded.model, submesh.material, overrideMaterial);
                HashValue(hash, submesh.material);
                HashValue(hash, material.blendMode);
                HashValue(hash, material.alphaCutoff);
                HashValue(hash, material.opacity);
                HashValue(hash, material.uvScale);
                HashValue(hash, material.uvOffset);
                HashValue(hash, material.uvRotation);
                const std::uintptr_t albedo =
                    reinterpret_cast<std::uintptr_t>(material.albedoMap);
                HashValue(hash, albedo);
            }
        });
    return hash;
}

ShadowCasterBatch::ShadowCasterBatch()  { glGenBuffers(1, &m_vbo); }
ShadowCasterBatch::~ShadowCasterBatch() { if (m_vbo) glDeleteBuffers(1, &m_vbo); }

void ShadowCasterBatch::Build(ecs::Registry &reg)
{
    m_data.clear();
    m_records.clear();
    m_textured.clear();

    auto& groups = m_groups;
    for (auto& group : groups) group.second.clear();
    auto meshView = reg.view<Transform, MeshPBR>();
    if (!meshView.empty()) meshView.each([&](Entity, Transform& t, MeshPBR& m) {
        if (!m.mesh) return;
        if (m.material.blendMode == ecs::PbrMaterial::BlendMode::Transparent) return;
        const glm::vec3& e = m.material.emissive;
        if (e.x > 0.0f || e.y > 0.0f || e.z > 0.0f) return;    // skip light gizmos
        const glm::mat4 model = t.Model();
        // Textured meshes carry a tangent at location 3, which would clash with the
        // instance attributes -- draw those per-object instead.
        if (m.material.albedoMap || m.material.normalMap || m.material.metalRoughMap
            || m.material.heightMap
            || m.material.blendMode == ecs::PbrMaterial::BlendMode::Masked) {
            m_textured.push_back({m.mesh, model, m.material});
            return;
        }
        std::vector<float>& v = groups[m.mesh];
        const float* mp = glm::value_ptr(model);
        v.insert(v.end(), mp, mp + 16);
    });

    // Foliage is stored as many light-weight instances under one actor. Feed its
    // enabled, shadow-casting types into the same batch as ordinary static meshes.
    auto foliageView = reg.view<Transform, ecs::FoliageComponent>();
    if (!foliageView.empty()) foliageView.each(
        [&](Entity, Transform& owner, ecs::FoliageComponent& foliage) {
            if (!foliage.visible) return;
            for (std::size_t typeIndex = 0; typeIndex < foliage.types.size(); ++typeIndex) {
                const ecs::FoliageTypeRuntime& type = foliage.types[typeIndex];
                if (!type.castShadows || !type.model) continue;
                const bool textured = type.material.albedoMap || type.material.normalMap
                    || type.material.metalRoughMap || type.material.heightMap;
                for (const ecs::FoliageInstance& instance : foliage.instances) {
                    if (!instance.enabled || instance.typeIndex != typeIndex) continue;
                    const glm::mat4 model = FoliageInstanceMatrix(owner, instance);
                    for (const SubMesh& subMesh : type.model->SubMeshes()) {
                        if (textured) {
                            m_textured.push_back({&subMesh.mesh, model, type.material});
                        } else {
                            std::vector<float>& values = groups[&subMesh.mesh];
                            const float* matrix = glm::value_ptr(model);
                            values.insert(values.end(), matrix, matrix + 16);
                        }
                    }
                }
            }
        });

    // Imported static models are native Model geometry by this point.  Feed
    // every opaque/masked submesh into the same caster submission used by
    // MeshPBR so directional, point and spot shadow generators all see it.
    auto modelView = reg.view<Transform, ecs::LoadedModelAsset>();
    if (!modelView.empty()) modelView.each(
        [&](Entity entity, Transform& transform, ecs::LoadedModelAsset& loaded) {
            if (!loaded.model || reg.Has<MeshPBR>(entity)) return;
            const glm::mat4 model = transform.Model();
            const auto* objectOverride = reg.TryGet<ecs::LoadedMaterialAsset>(entity);
            for (const SubMesh& submesh : loaded.model->SubMeshes()) {
                ecs::PbrMaterial material = ResolveModelPbrMaterial(
                    *loaded.model, submesh.material, objectOverride);
                if (material.blendMode == ecs::PbrMaterial::BlendMode::Transparent)
                    continue;
                const bool perObject = material.albedoMap
                    || material.blendMode == ecs::PbrMaterial::BlendMode::Masked;
                if (perObject) {
                    m_textured.push_back({&submesh.mesh, model, material});
                } else {
                    std::vector<float>& values = groups[&submesh.mesh];
                    const float* matrix = glm::value_ptr(model);
                    values.insert(values.end(), matrix, matrix + 16);
                }
            }
        });

    for (auto& kv : groups) {
        if (kv.second.empty()) continue;
        Record r;
        r.mesh = kv.first;
        r.offsetFloats = static_cast<int>(m_data.size());
        r.count = static_cast<int>(kv.second.size() / 16);
        m_data.insert(m_data.end(), kv.second.begin(), kv.second.end());
        m_records.push_back(r);
    }
    if (!m_data.empty()) {
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        const std::size_t bytes = m_data.size() * sizeof(float);
        if (bytes > m_vboCapacity) {
            m_vboCapacity = std::max(bytes, std::max<std::size_t>(m_vboCapacity * 2, 4096));
            glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(m_vboCapacity),
                         nullptr, GL_DYNAMIC_DRAW);
        }
        glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(bytes), m_data.data());
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }
}

void ShadowCasterBatch::Draw(Shader &sh)
{
    if (!m_records.empty()) {
        sh.SetInt("uInstanced", 1);
        sh.SetInt("uAlphaMasked", 0);
        sh.SetInt("uHasAlbedoMap", 0);
        const GLsizei stride = 16 * static_cast<GLsizei>(sizeof(float));
        for (const Record& r : m_records) {
            const int lod = r.mesh->LodForTriangleBudget(50000u);
            r.mesh->BindLod(lod);
            glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
            const std::size_t base = static_cast<std::size_t>(r.offsetFloats) * sizeof(float);
            for (int c = 0; c < 4; ++c) {
                const GLuint loc = static_cast<GLuint>(3 + c);
                glEnableVertexAttribArray(loc);
                glVertexAttribPointer(loc, 4, GL_FLOAT, GL_FALSE, stride,
                                      reinterpret_cast<void*>(base + static_cast<std::size_t>(c) * 4 * sizeof(float)));
                glVertexAttribDivisor(loc, 1);
            }
            glDrawElementsInstanced(GL_TRIANGLES,
                                    static_cast<GLsizei>(r.mesh->IndexCount(lod)),
                                    GL_UNSIGNED_INT, nullptr, r.count);
            GpuProfiler::RecordDrawCall();
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
        const Texture* boundAlbedo = nullptr;
        for (const auto& pr : m_textured) {
            sh.SetMat4("uModel", pr.model);
            const bool masked = pr.material.blendMode == ecs::PbrMaterial::BlendMode::Masked;
            sh.SetInt("uAlphaMasked", masked ? 1 : 0);
            sh.SetFloat("uAlphaCutoff", pr.material.alphaCutoff);
            sh.SetFloat("uOpacity", pr.material.opacity);
            sh.SetVec2("uUvScale", pr.material.uvScale);
            sh.SetVec2("uUvOffset", pr.material.uvOffset);
            sh.SetFloat("uUvRotation", pr.material.uvRotation);
            const bool hasMap = pr.material.albedoMap != nullptr;
            sh.SetInt("uHasAlbedoMap", hasMap ? 1 : 0); sh.SetInt("uAlbedoMap", 0);
            if (hasMap && pr.material.albedoMap != boundAlbedo) {
                pr.material.albedoMap->Bind(0);
                boundAlbedo = pr.material.albedoMap;
            }
            pr.mesh->DrawLod(pr.mesh->LodForTriangleBudget(50000u));
        }
    }
}

} // namespace engine
