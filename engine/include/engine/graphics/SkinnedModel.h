#pragma once

#include "engine/graphics/Model.h"            // SubMesh, Material, Texture
#include "engine/animation/Skeleton.h"        // Skeleton, Animation

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <vector>

namespace engine {

// A rigged, animated model loaded via Assimp. Unlike the static Model, vertices
// are kept in bind (mesh) space -- the bones move them at draw time -- and each
// vertex carries up to four bone indices + weights. The vertex format is
// position3 / normal3 / uv2 / boneIds4 (as floats) / weights4  (VertexLayout
// {3,3,2,4,4}); bone ids are packed as floats and cast back in the shader, so no
// integer vertex attributes are needed. Move-only (owns GPU resources).
class SkinnedModel {
public:
    static constexpr int kMaxBones = 128;     // GPU bone-matrix uniform array cap

    SkinnedModel() = default;

    // Load any rigged format Assimp understands (glTF 2.0, FBX, COLLADA, ...).
    // Throws std::runtime_error on failure.
    static SkinnedModel FromFile(const std::string& path);

    SkinnedModel(const SkinnedModel&)            = delete;
    SkinnedModel& operator=(const SkinnedModel&) = delete;
    SkinnedModel(SkinnedModel&&) noexcept            = default;
    SkinnedModel& operator=(SkinnedModel&&) noexcept = default;

    const std::vector<SubMesh>&  SubMeshes() const { return m_subMeshes; }
    const std::vector<Material>& Materials() const { return m_materials; }
    const std::vector<std::unique_ptr<Texture>>& Textures() const { return m_textures; }

    const Skeleton&               GetSkeleton() const { return m_skeleton; }
    const std::vector<Animation>& Animations()  const { return m_animations; }
    std::size_t AnimationCount() const { return m_animations.size(); }
    std::size_t BoneCount()      const { return m_skeleton.bones.size(); }

    const glm::vec3& Min() const { return m_min; }
    const glm::vec3& Max() const { return m_max; }
    glm::vec3 Center() const { return (m_min + m_max) * 0.5f; }
    float BoundingRadius() const { return glm::length(m_max - m_min) * 0.5f; }

private:
    std::vector<SubMesh>                   m_subMeshes;
    std::vector<Material>                  m_materials;
    std::vector<std::unique_ptr<Texture>>  m_textures;
    Skeleton                               m_skeleton;
    std::vector<Animation>                 m_animations;
    glm::vec3 m_min{0.0f};
    glm::vec3 m_max{0.0f};
};

} // namespace engine
