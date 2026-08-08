#pragma once

#include "engine/ecs/Registry.h"
#include "engine/ecs/Components.h"
#include "engine/graphics/Renderer.h"
#include "engine/graphics/Shader.h"
#include "engine/graphics/Model.h"
#include "engine/graphics/Texture.h"
#include "engine/graphics/ShaderParameterBinding.h"

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <algorithm>
#include <string>
#include <vector>

namespace engine {
namespace ecs {

inline ModelMaterialOverride LoadedModelMaterialOverride(
    const LoadedMaterialAsset& loaded) {
    const PbrMaterial& source = loaded.material;
    ModelMaterialOverride result;
    result.diffuse = source.albedo;
    const glm::vec3 dielectric(
        std::clamp(source.specularLevel, 0.0f, 1.0f) * 0.08f);
    result.specular = glm::mix(
        dielectric, source.albedo,
        std::clamp(source.metallic, 0.0f, 1.0f));
    result.emissive = source.emissive;
    const float roughness = std::clamp(source.roughness, 0.02f, 1.0f);
    result.shininess = std::clamp(
        2.0f / (roughness * roughness) - 2.0f, 1.0f, 2048.0f);
    result.diffuseMap = loaded.albedoMap;
    result.normalMap = loaded.normalMap;
    return result;
}

inline void UploadLoadedMaterialShaderParameters(
    Shader& shader, const LoadedMaterialAsset& material) {
    int textureUnit = 18;
    for (const auto& entry : material.shaderParameters) {
        const auto type = material.shaderParameterTypes.find(entry.first);
        const int valueType =
            type == material.shaderParameterTypes.end() ? 0 : type->second;
        const auto texture = material.shaderTextures.find(entry.first);
        textureUnit = UploadShaderParameter(
            shader, entry.first, valueType, entry.second,
            texture == material.shaderTextures.end() ? nullptr : texture->second,
            textureUnit);
    }
}

// Draws every entity that has a Transform + MeshRenderer.
//
// Convention: `shader` must already be Bound, with its view/projection and any
// lighting uniforms set by the caller. This system sets the per-object uniforms
// `uModel` (mat4), `uNormalMat` (mat3) and `uColor` (vec3), then issues the draw.
// A game whose shader uses different uniform names can write its own one-liner.
inline void RenderMeshes(Registry& reg, Renderer& renderer, Shader& shader) {
    auto meshView = reg.view<Transform, MeshRenderer>();
    if (meshView.empty()) return;
    meshView.each(
        [&](Entity entity, Transform& t, MeshRenderer& mr) {
            if (reg.Has<LoadedModelAsset>(entity)) return;
            if (!mr.mesh) return;
            const glm::mat4 model = t.Model();
            shader.SetMat4("uModel", model);
            shader.SetMat3("uNormalMat", glm::mat3(glm::transpose(glm::inverse(model))));
            shader.SetVec3("uColor", mr.color);
            renderer.Draw(*mr.mesh);
        }
    );
}

// Draws every entity that has a Transform + LoadedModelAsset.
//
// Convention: `shader` must already be Bound, with `uViewProj` and lighting
// uniforms set by the caller. This system sets `uModel` and `uNormalMat`, then
// lets engine::DrawModel bind material maps and draw each sub-mesh.
inline void RenderLoadedModels(
    Registry& reg, Shader& defaultShader, const glm::mat4& viewProjection,
    const glm::vec3& lightDirection = glm::vec3(0.0f, -1.0f, 0.0f),
    float lightIntensity = 1.0f) {
    Shader* boundShader = nullptr;
    auto modelView = reg.view<Transform, LoadedModelAsset>();
    if (modelView.empty()) return;
    modelView.each(
        [&](Entity entity, Transform& t, LoadedModelAsset& loaded) {
            if (!loaded.model) return;
            const glm::mat4 model = t.Model();
            Shader* shader = &defaultShader;
            const LoadedMaterialAsset* material =
                reg.TryGet<LoadedMaterialAsset>(entity);
            if (material && material->shader) {
                shader = const_cast<Shader*>(material->shader);
                if (shader != boundShader) {
                    shader->Bind();
                    boundShader = shader;
                }
                shader->SetMat4("uViewProjection", viewProjection);
                shader->SetVec3("uLightDirection", lightDirection);
                shader->SetFloat("uLightIntensity", lightIntensity);
                shader->SetVec3("uLightColor", glm::vec3(lightIntensity));  // custom lighting
                shader->SetVec3("uAmbient", glm::vec3(0.05f));
                UploadLoadedMaterialShaderParameters(*shader, *material);
            } else {
                if (shader != boundShader) {
                    shader->Bind();
                    boundShader = shader;
                }
            }
            shader->SetMat4("uModel", model);
            shader->SetMat3(
                "uNormalMat", glm::mat3(glm::transpose(glm::inverse(model))));
            if (material
                && material->material.blendMode
                    == PbrMaterial::BlendMode::Transparent) {
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glDepthMask(GL_FALSE);
            } else {
                glDisable(GL_BLEND);
                glDepthMask(GL_TRUE);
            }
            ModelMaterialOverride modelMaterial;
            const ModelMaterialOverride* modelMaterialPtr = nullptr;
            if (material) {
                modelMaterial = LoadedModelMaterialOverride(*material);
                modelMaterialPtr = &modelMaterial;
            }
            DrawModel(*loaded.model, *shader, glm::vec3(1.0f), nullptr,
                      modelMaterialPtr);
        }
    );
    // Restore GL state after rendering loaded models
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
}

} // namespace ecs
} // namespace engine
