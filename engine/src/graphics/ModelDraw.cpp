#include "engine/graphics/Model.h"
#include "engine/graphics/Shader.h"

namespace engine {

void DrawModel(const Model& model, Shader& shader) {
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

        shader.SetVec3("uColor",      m.diffuse);
        shader.SetVec3("uSpecular",   m.specular);
        shader.SetVec3("uEmissive",   m.emissive);
        shader.SetFloat("uShininess", m.shininess);

        auto bind = [&](int idx, int unit, const char* flag) {
            const bool has = idx >= 0;
            shader.SetInt(flag, has ? 1 : 0);
            if (has) model.Textures()[static_cast<std::size_t>(idx)]->Bind(static_cast<unsigned>(unit));
        };
        bind(m.diffuseMap,  0, "uHasDiffuse");
        bind(m.normalMap,   1, "uhasNormal");
        bind(m.specularMap, 2, "uHasSpecular");
        bind(m.emissiveMap, 3, "uHasEmissive");

        sm.mesh.Draw();
    }
}

}// namespace engine