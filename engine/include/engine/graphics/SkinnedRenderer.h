#pragma once

#include <glm/glm.hpp>

#include <memory>
#include <vector>

namespace engine {

class SkinnedModel;
class Shader;
class Camera;
class CascadedShadow;
class IBL;
namespace ecs { class Registry; }

// Lighting context for the PBR skinned pass -- mirrors the subset of
// PbrRenderer::Options that skinned characters use, so they match the world.
// `cascade` is required by DrawScene and comes from the PBR pass. `ibl` is
// optional so animated objects continue to render in environments with IBL off.
struct SkinnedLighting {
    glm::vec3 sunDir{0.0f, -1.0f, 0.0f};
    glm::vec3 sunColor{1.0f};
    glm::vec3 ambient{0.03f};
    const CascadedShadow* cascade = nullptr;   // sun (cascade) shadows
    const IBL*            ibl     = nullptr;    // image-based ambient
    float shadowSoftness = 2.5f;
    bool  tonemap = false;                      // false = linear HDR (for PostProcess)
    bool  fog = false;
    glm::vec3 fogColor{0.6f, 0.7f, 0.8f};
    float fogDensity = 0.02f, fogHeight = 0.0f, fogHeightFalloff = 0.12f;
};

// Draws animated SkinnedModels with GPU skinning. Two lighting paths share the
// same bone-blend vertex stage: a simple Phong Draw() (used by the standalone
// character demo) and a Cook-Torrance DrawScene() that renders AnimatedModel ECS
// entities to match the PBR world (sun + cascade shadows + ambient/IBL + fog).
class SkinnedRenderer {
public:
    static constexpr int kMaxBones = 128;   // matches SkinnedModel::kMaxBones and the shader

    SkinnedRenderer();
    ~SkinnedRenderer();
    SkinnedRenderer(const SkinnedRenderer&)            = delete;
    SkinnedRenderer& operator=(const SkinnedRenderer&) = delete;

    // Phong single-model draw (sun + ambient). Kept for the standalone demo.
    void Draw(const SkinnedModel& model,
              const std::vector<glm::mat4>& boneMatrices,
              const glm::mat4& modelMatrix,
              const Camera& camera, float aspect,
              const glm::vec3& sunDir, const glm::vec3& sunColor,
              const glm::vec3& ambient);

    
    // PBR ECS scene: draw every AnimatedModel entity (Transform + AnimatedModel)
    // with Cook-Torrance lighting matching PbrRenderer. Run AFTER PbrRenderer so
    // the cascade/IBL textures the shared uniforms need are already generated.
    void DrawScene(ecs::Registry& registry, const Camera& camera, float aspect,
                   const SkinnedLighting& lighting);
    
    // Depth of one skinned model into the bound shadow map (for CascadedShadow).
    void DrawDepth(const SkinnedModel& model,
                   const std::vector<glm::mat4>& boneMatrices,
                   const glm::mat4& modelMatrix, const glm::mat4& lightVP);
    
    // Depth of every shadow-casting AnimatedModel entity -- pass a lambda wrapping
    // this as PbrRenderer::Options.shadowCasters so characters cast sun shadows.
    void DrawSceneDepth(ecs::Registry& registry, const glm::mat4& lightVP);

private:
    std::unique_ptr<Shader> m_shader;   // Phong
    std::unique_ptr<Shader> m_pbr;      // Cook-Torrance (scene)
    std::unique_ptr<Shader> m_depth;    // depth-only (shadows)
};

} // namespace engine
