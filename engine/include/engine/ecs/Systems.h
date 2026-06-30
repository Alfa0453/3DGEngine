#pragma once

#include "engine/ecs/Registry.h"
#include "engine/ecs/Components.h"
#include "engine/graphics/Renderer.h"
#include "engine/graphics/Shader.h"

#include <glm/glm.hpp>

namespace engine {
namespace ecs {

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
inline void RenderLoadedModels(Registry& reg, Shader& shader) {
    reg.view<Transform, LoadedModelAsset>().each(
        [&](Entity, Transform& t, LoadedModelAsset& loaded) {
            if (!loaded.model) return;
            const glm::mat4 model = t.Model();
            shader.SetMat4("uModel", model);
            shader.SetMat3("uNormalMat", glm::mat3(glm::transpose(glm::inverse(model))));
        }
    );
}

} // namespace ecs
} // namespace engine