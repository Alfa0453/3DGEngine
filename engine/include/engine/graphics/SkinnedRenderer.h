#pragma once

#include <glm/glm.hpp>

#include <memory>
#include <vector>

namespace engine {

class SkinnedModel;
class Shader;
class Camera;

// Draws an animated SkinnedModel with GPU skinning. Its own shader (bone-matrix
// blend in the vertex stage, Phong lighting in the fragment stage) -- separate
// from PbrRenderer because skinned vertices use attribute locations 3..4 for bone
// IDs/weights, which would clash with the instanced PBR path. Give it the pose
// (bone matrices from Animator::ComputePose) and a sun + ambient each frame.
class SkinnedRenderer {
public:
    static constexpr int kMaxBones = 128;   // matches SkinnedModel::kMaxBones and the shader

    SkinnedRenderer();
    ~SkinnedRenderer();
    SkinnedRenderer(const SkinnedRenderer&)            = delete;
    SkinnedRenderer& operator=(const SkinnedRenderer&) = delete;

    void Draw(const SkinnedModel& model,
              const std::vector<glm::mat4>& boneMatrices,
              const glm::mat4& modelMatrix,
              const Camera& camera, float aspect,
              const glm::vec3& sunDir, const glm::vec3& sunColor,
              const glm::vec3& ambient);
    
    // Render the skinned model's depth into the currently-bound shadow map, using
    // lightVP as the light's view-projection -- lets skinned characters cast
    // shadows (pass this to CascadedShadow::Generate's caster callback).
    void DrawDepth(const SkinnedModel& model,
                   const std::vector<glm::mat4>& boneMatrices,
                   const glm::mat4& modelMatrix, const glm::mat4& lightVP);

private:
    std::unique_ptr<Shader> m_shader;
    std::unique_ptr<Shader> m_depth;
};

} // namespace engine
