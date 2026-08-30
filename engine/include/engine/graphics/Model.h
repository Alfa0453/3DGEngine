#pragma once

#include "engine/graphics/Mesh.h"
#include "engine/graphics/Texture.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace engine {

class Shader;
namespace ecs { struct PbrMaterial; struct LoadedMaterialAsset; }

// A surface description. Colours feed the Phong shader; the *Map fields are
// indices into Model::Textures() (or -1 when a material has no such map). A
// single material can stack several maps at once (diffuse + normal + specular +
// emissive), which is what real models use.
struct Material {
    std::string name;
    std::string assetPath;            // resolved .3dgmat, empty for embedded legacy material
    std::string assetHandle;
    glm::vec3   diffuse{0.8f};      // base colour
    glm::vec3   specular{0.2f};     // highlight colour
    glm::vec3   emissive{0.0f};     // self-illumination
    float       shininess = 32.0f;  // Ns — specular exponent
    float       metallic = 0.0f;
    float       roughness = 0.5f;
    float       ao = 1.0f;
    float       opacity = 1.0f;
    float       alphaCutoff = 0.5f;
    int         blendMode = 0;          // ecs::PbrMaterial::BlendMode value
    glm::vec2   uvScale{1.0f};
    glm::vec2   uvOffset{0.0f};
    float       uvRotation = 0.0f;
    bool        worldSpaceUv = false;
    float       normalStrength = 1.0f;
    float       heightScale = 0.0f;
    float       clearcoat = 0.0f;
    float       clearcoatRoughness = 0.1f;
    float       transmission = 0.0f;
    float       ior = 1.5f;
    float       thickness = 0.0f;
    float       anisotropy = 0.0f;
    float       anisotropyRotation = 0.0f;
    glm::vec3   sheenColor{0.0f};
    float       sheenRoughness = 0.5f;
    float       specularLevel = 0.5f;
    float       subsurface = 0.0f;
    glm::vec3   subsurfaceColor{1.0f};

    int diffuseMap  = -1;            // indices into Model::Textures(), or -1
    int normalMap   = -1;
    int specularMap = -1;
    int emissiveMap = -1;
    int metalRoughMap = -1;
    int heightMap = -1;
    std::string diffuseMapPath;
    std::string normalMapPath;
    std::string metalRoughMapPath;
    std::string heightMapPath;
    std::string emissiveMapPath;
};

// One drawable chunk: geometry plus the index of the material it uses. A model
// has one SubMesh per (mesh, material) Assimp produces.
struct SubMesh {
    Mesh mesh;
    int  material = -1;     // index into Model::Materials(), or -1 for a default
};

// Complete object-level material replacement used when a scene object assigns
// a .3dgmat to an imported model. Unlike `tint`, this replaces the model's
// embedded import material rather than multiplying it.
struct ModelMaterialOverride {
    glm::vec3 diffuse{0.8f};
    glm::vec3 specular{0.2f};
    glm::vec3 emissive{0.0f};
    float shininess = 32.0f;
    const Texture* diffuseMap = nullptr;
    const Texture* normalMap = nullptr;
    const Texture* specularMap = nullptr;
    const Texture* emissiveMap = nullptr;
};

// A model loaded from disk via Assimp. Move-only: it owns GPU resources (the
// sub-meshes' buffers and the textures). Mesh vertex format is
// position3 / normal3 / uv2 / tangent3 (VertexLayout{3,3,2,3}).
class Model {
public:
    Model() = default;

    // Load any format Assimp understands (FBX, glTF 2.0, OBJ, COLLADA, …).
    // Embedded textures are decoded with the engine's PNG/JPEG decoders.
    // Throws std::runtime_error on failure.
    static Model FromFile(const std::string& path);

    Model(const Model&)            = delete;
    Model& operator=(const Model&) = delete;
    Model(Model&&) noexcept            = default;
    Model& operator=(Model&&) noexcept = default;

    const std::vector<SubMesh>&  SubMeshes() const { return m_subMeshes; }
    const std::vector<Material>& Materials() const { return m_materials; }
    const std::vector<std::unique_ptr<Texture>>& Textures() const { return m_textures; }

    // Axis-aligned bounds of all geometry (model space). Lets a viewer frame any
    // model without knowing its size in advance.
    const glm::vec3& Min() const { return m_min; }
    const glm::vec3& Max() const { return m_max; }
    glm::vec3 Center() const { return (m_min + m_max) * 0.5f; }
    float BoundingRadius() const { return glm::length(m_max - m_min) * 0.5f; }
    std::uint64_t Revision() const { return m_revision; }
    void SetRevision(std::uint64_t revision) { m_revision = std::max(revision, 1ull); }

    // Quick stats (handy for tests / debug overlays).
    std::size_t SubMeshCount() const { return m_subMeshes.size(); }
    std::size_t VertexCount() const {
        std::size_t count = 0;
        for (const SubMesh& subMesh : m_subMeshes)
            count += subMesh.mesh.VertexCount();
        return count;
    }
    std::size_t TriangleCount() const {
        std::size_t count = 0;
        for (const SubMesh& subMesh : m_subMeshes)
            count += subMesh.mesh.TriangleCount();
        return count;
    }

private:
    std::vector<SubMesh>                  m_subMeshes;
    std::vector<Material>                 m_materials;
    std::vector<std::unique_ptr<Texture>> m_textures;   // owned; materials index in
    glm::vec3 m_min{0.0f};
    glm::vec3 m_max{0.0f};
    std::uint64_t m_revision = 1;
};

// Draw a model: for each sub-mesh, set its material's uniforms on `shader`, bind
// its maps, and draw. The caller binds the shader and sets the camera/lighting
// uniforms first. Shader uniform convention:
//   uColor/uSpecular/uEmissive (vec3), uShininess (float)
//   uHasDiffuse/uHasNormal/uHasSpecular/uHasEmissive (int 0/1)
//   uDiffuseTex/uNormalTex/uSpecularTex/uEmissiveTex (sampler2D, units 0..3)
// `tint` multiplies each submesh's base colour; `albedoOverride`, when set, replaces
// the diffuse map on every submesh (used to apply a .3dgmat to an attachment).
void DrawModel(const Model& model, Shader& shader,
               const glm::vec3& tint = glm::vec3(1.0f),
               const Texture* albedoOverride = nullptr,
               const ModelMaterialOverride* materialOverride = nullptr);

// Converts one imported submesh material slot into the exact runtime surface
// consumed by PbrRenderer.  A scene-level .3dgmat is authoritative when supplied;
// otherwise the model's per-slot material and owned textures are retained.
ecs::PbrMaterial ResolveModelPbrMaterial(
    const Model& model, int materialSlot,
    const ecs::LoadedMaterialAsset* objectOverride = nullptr,
    bool* usedFallback = nullptr);

} // namespace engine
