#include "engine/graphics/Model.h"
#include "engine/graphics/Shader.h"

namespace engine {

void DrawModel(const Model& model, Shader& shader,
               const glm::vec3& tint, const Texture* albedoOverride,
               const ModelMaterialOverride* materialOverride) {
    const auto& mats = model.Materials();

    // Sampler units are fixed for the lifetime of the shader's use here.
    shader.SetInt("uDiffuseTex",  0);
    shader.SetInt("uNormalTex",   1);
    shader.SetInt("uSpecularTex", 2);
    shader.SetInt("uEmissiveTex", 3);

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

        auto bind = [&](int idx, int unit, const char* flag) {
            const bool has = idx >= 0;
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
        } else if (albedoOverride) {
            albedoOverride->Bind(0);
            shader.SetInt("uDiffuseTex", 0);
            shader.SetInt("uHasDiffuse", 1);
            bind(m.normalMap,   1, "uHasNormal");
            bind(m.specularMap, 2, "uHasSpecular");
            bind(m.emissiveMap, 3, "uHasEmissive");
        } else {
            bind(m.diffuseMap, 0, "uHasDiffuse");
            bind(m.normalMap,   1, "uHasNormal");
            bind(m.specularMap, 2, "uHasSpecular");
            bind(m.emissiveMap, 3, "uHasEmissive");
        }

        sm.mesh.Draw();
    }
}

}// namespace engine
