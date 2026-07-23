#pragma once

#include "engine/ecs/Registry.h"
#include "engine/ecs/Components.h"
#include "engine/graphics/Renderer.h"
#include "engine/graphics/Shader.h"
#include "engine/graphics/Model.h"
#include "engine/graphics/Texture.h"

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

namespace engine {
namespace ecs {

inline std::vector<float> ParseShaderParameterNumbers(std::string value) {
    std::replace(value.begin(), value.end(), ',', ' ');
    std::replace(value.begin(), value.end(), '(', ' ');
    std::replace(value.begin(), value.end(), ')', ' ');
    std::istringstream input(value);
    std::vector<float> values;
    float number = 0.0f;
    while (input >> number) values.push_back(number);
    return values;
}

inline void UploadLoadedMaterialShaderParameters(
    Shader& shader, const LoadedMaterialAsset& material) {
    int textureUnit = 18;
    for (const auto& entry : material.shaderParameters) {
        const std::string uniform = "u_" + entry.first;
        const auto type = material.shaderParameterTypes.find(entry.first);
        const int valueType =
            type == material.shaderParameterTypes.end() ? 0 : type->second;
        if (valueType == 7) {
            const auto texture = material.shaderTextures.find(entry.first);
            if (texture != material.shaderTextures.end() && texture->second) {
                texture->second->Bind(static_cast<unsigned int>(textureUnit));
                shader.SetInt(uniform, textureUnit++);
            }
            continue;
        }
        const std::vector<float> values =
            ParseShaderParameterNumbers(entry.second);
        if (valueType == 1 || valueType == 2)
            shader.SetInt(uniform, entry.second == "true" ? 1
                : values.empty() ? 0 : static_cast<int>(values[0]));
        else if (valueType == 3 && values.size() >= 2)
            shader.SetVec2(uniform, glm::vec2(values[0], values[1]));
        else if (valueType == 4 && values.size() >= 3)
            shader.SetVec3(uniform, glm::vec3(values[0], values[1], values[2]));
        else if ((valueType == 5 || valueType == 6) && values.size() >= 4)
            shader.SetVec4(
                uniform, glm::vec4(values[0], values[1], values[2], values[3]));
        else
            shader.SetFloat(uniform, values.empty() ? 0.0f : values[0]);
    }
}

// Draws every entity that has a Transform + MeshRenderer.
//
// Convention: `shader` must already be Bound, with its view/projection and any
// lighting uniforms set by the caller. This system sets the per-object uniforms
// `uModel` (mat4), `uNormalMat` (mat3) and `uColor` (vec3), then issues the draw.
// A game whose shader uses different uniform names can write its own one-liner.
inline void RenderMeshes(Registry& reg, Renderer& renderer, Shader& shader) {
    reg.view<Transform, MeshRenderer>().each(
        [&](Entity entity, Transform& t, MeshRenderer& mr) {
            if (reg.Has<LoadedModelAsset>(entity)) return;
            if (!mr.mesh) return;
            const glm::mat4 model = t.Model();
            shader.SetMat4("uModel", model);
            shader.SetMat3("uNormalMat", model);
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
    reg.view<Transform, LoadedModelAsset>().each(
        [&](Entity entity, Transform& t, LoadedModelAsset& loaded) {
            if (!loaded.model) return;
            const glm::mat4 model = t.Model();
            Shader* shader = &defaultShader;
            const LoadedMaterialAsset* material =
                reg.TryGet<LoadedMaterialAsset>(entity);
            if (material && material->shader) {
                shader = const_cast<Shader*>(material->shader);
                shader->Bind();
                shader->SetMat4("uViewProjection", viewProjection);
                shader->SetVec3("uLightDirection", lightDirection);
                shader->SetFloat("uLightIntensity", lightIntensity);
                UploadLoadedMaterialShaderParameters(*shader, *material);
            } else {
                defaultShader.Bind();
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
            DrawModel(*loaded.model, *shader);
        }
    );
    // Restore GL state after rendering loaded models
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
}

} // namespace ecs
} // namespace engine
