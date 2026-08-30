#include "engine/graphics/Model.h"
#include "engine/graphics/Shader.h"
#include "engine/ecs/Components.h"

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <algorithm>

namespace engine {

ecs::PbrMaterial ResolveModelPbrMaterial(
    const Model& model, int materialSlot,
    const ecs::LoadedMaterialAsset* objectOverride, bool* usedFallback) {
    if (usedFallback) *usedFallback = false;
    if (objectOverride) return objectOverride->material;

    ecs::PbrMaterial result;
    if (materialSlot < 0
        || materialSlot >= static_cast<int>(model.Materials().size())) {
        if (usedFallback) *usedFallback = true;
        return result;
    }
    const Material& source = model.Materials()[static_cast<std::size_t>(materialSlot)];
    result.albedo = source.diffuse;
    result.metallic = source.metallic;
    result.roughness = source.roughness;
    result.ao = source.ao;
    result.emissive = source.emissive;
    result.opacity = source.opacity;
    result.alphaCutoff = source.alphaCutoff;
    result.blendMode = static_cast<ecs::PbrMaterial::BlendMode>(
        std::clamp(source.blendMode, 0, 2));
    result.uvScale = source.uvScale;
    result.uvOffset = source.uvOffset;
    result.uvRotation = source.uvRotation;
    result.worldSpaceUv = source.worldSpaceUv;
    result.normalStrength = source.normalStrength;
    result.heightScale = source.heightScale;
    result.clearcoat = source.clearcoat;
    result.clearcoatRoughness = source.clearcoatRoughness;
    result.transmission = source.transmission;
    result.ior = source.ior;
    result.thickness = source.thickness;
    result.anisotropy = source.anisotropy;
    result.anisotropyRotation = source.anisotropyRotation;
    result.sheenColor = source.sheenColor;
    result.sheenRoughness = source.sheenRoughness;
    result.specularLevel = source.specularLevel;
    result.subsurface = source.subsurface;
    result.subsurfaceColor = source.subsurfaceColor;
    auto texture = [&](int index) -> const Texture* {
        return index >= 0 && index < static_cast<int>(model.Textures().size())
            ? model.Textures()[static_cast<std::size_t>(index)].get() : nullptr;
    };
    result.albedoMap = texture(source.diffuseMap);
    result.normalMap = texture(source.normalMap);
    result.metalRoughMap = texture(source.metalRoughMap);
    result.heightMap = texture(source.heightMap);
    result.emissiveMap = texture(source.emissiveMap);
    return result;
}

void DrawModel(const Model& model, Shader& shader,
               const glm::vec3& tint, const Texture* albedoOverride,
               const ModelMaterialOverride* materialOverride) {
    const auto& mats = model.Materials();

    // Backface culling: imported models are closed solids, so never rasterize their
    // inside faces (front faces are CCW). Restored to the default (off) at the end so
    // callers/other passes keep their expected state.
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    // Sampler units are fixed for the lifetime of the shader's use here.
    shader.SetInt("uDiffuseTex",  0);
    shader.SetInt("uNormalTex",   1);
    shader.SetInt("uSpecularTex", 2);
    shader.SetInt("uEmissiveTex", 3);
    shader.SetInt("uMetalRoughMap", 4);
    shader.SetInt("uHeightMap", 5);

    // DrawModel has no object transform argument, so its model-space winding is
    // the imported convention. Callers with mirrored transforms use the PBR path.
    for (const SubMesh& sm : model.SubMeshes()) {
        const bool valid = sm.material >= 0 && sm.material < static_cast<int>(mats.size());
        const Material def;
        const Material& m = valid ? mats[static_cast<std::size_t>(sm.material)] : def;

        // A scene material is authoritative for the complete imported model.
        // Legacy attachment calls can still use the tint/albedo-only path.
        shader.SetVec3("uColor", materialOverride
            ? materialOverride->diffuse
            : albedoOverride ? tint : (m.diffuse * tint));
        shader.SetVec3("uSpecular", materialOverride
            ? materialOverride->specular : m.specular);
        shader.SetVec3("uEmissive", materialOverride
            ? materialOverride->emissive : m.emissive);
        shader.SetFloat("uShininess", materialOverride
            ? materialOverride->shininess : m.shininess);
        shader.SetFloat("uMetallic", materialOverride ? 0.0f : m.metallic);
        shader.SetFloat("uRoughness", materialOverride ? 0.5f : m.roughness);
        shader.SetFloat("uAO", materialOverride ? 1.0f : m.ao);
        shader.SetFloat("uOpacity", materialOverride ? 1.0f : m.opacity);

        auto bind = [&](int idx, int unit, const char* flag) {
            const bool has = idx >= 0
                && idx < static_cast<int>(model.Textures().size())
                && model.Textures()[static_cast<std::size_t>(idx)];
            shader.SetInt(flag, has ? 1 : 0);
            if (has) model.Textures()[static_cast<std::size_t>(idx)]->Bind(static_cast<unsigned>(unit));
        };
        auto bindOverride = [&](const Texture* texture, int unit,
                                const char* sampler, const char* flag) {
            shader.SetInt(flag, texture ? 1 : 0);
            if (texture) {
                texture->Bind(static_cast<unsigned>(unit));
                shader.SetInt(sampler, unit);
            }
        };
        if (materialOverride) {
            bindOverride(materialOverride->diffuseMap, 0,
                         "uDiffuseTex", "uHasDiffuse");
            bindOverride(materialOverride->normalMap, 1,
                         "uNormalTex", "uHasNormal");
            bindOverride(materialOverride->specularMap, 2,
                         "uSpecularTex", "uHasSpecular");
            bindOverride(materialOverride->emissiveMap, 3,
                         "uEmissiveTex", "uHasEmissive");
            shader.SetInt("uHasMetalRoughMap", 0);
            shader.SetInt("uHasHeightMap", 0);
        } else if (albedoOverride) {
            albedoOverride->Bind(0);
            shader.SetInt("uDiffuseTex", 0);
            shader.SetInt("uHasDiffuse", 1);
            bind(m.normalMap,   1, "uHasNormal");
            bind(m.specularMap, 2, "uHasSpecular");
            bind(m.emissiveMap, 3, "uHasEmissive");
            bind(m.metalRoughMap, 4, "uHasMetalRoughMap");
            bind(m.heightMap, 5, "uHasHeightMap");
        } else {
            bind(m.diffuseMap, 0, "uHasDiffuse");
            bind(m.normalMap,   1, "uHasNormal");
            bind(m.specularMap, 2, "uHasSpecular");
            bind(m.emissiveMap, 3, "uHasEmissive");
            bind(m.metalRoughMap, 4, "uHasMetalRoughMap");
            bind(m.heightMap, 5, "uHasHeightMap");
        }

        sm.mesh.Draw();
    }

    glDisable(GL_CULL_FACE);   // restore the default (off) for subsequent draws
    glFrontFace(GL_CCW);
}

}// namespace engine
