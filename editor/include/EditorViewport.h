#pragma once

#include "EditorGizmo.h"
#include "EditorScene.h"

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace engine {
class Mesh;
class Model;
class Renderer;
class RuntimeAssetManager;
class Shader;
class Texture;
}

class EditorViewport {
public:
    struct PhysicsEventGuide {
        glm::vec3 a{0.0f};
        glm::vec3 b{0.0f};
        std::string objectA;
        std::string objectB;
        int phase = 0; // 0 enter, 1 stay, 2 exit
        bool trigger = false;
    };

    struct PhysicsJointGuide {
        glm::vec3 a{0.0f};
        glm::vec3 b{0.0f};
        int type = 0; // 0 distance, 1 spring
        bool rope = false;
        bool enabled = true;
    };

    bool ContainsPoint(float x, float y, int width, int height) const;

    void DrawSceneGizmo(engine::Renderer& renderer,
                        engine::Shader& shader,
                        const engine::Mesh& cube,
                        const engine::Mesh& cone,
                        const EditorScene& scene,
                        const EditorGizmo& gizmo,
                        const glm::mat4& viewProj) const;

    void DrawSelectedLightGuide(engine::Renderer& renderer,
                                engine::Shader& shader,
                                const engine::Mesh& cube,
                                const EditorScene& scene,
                                const glm::mat4& viewProj,
                                bool selectedOnly) const;

    void DrawPhysicsColliderGuides(engine::Renderer& renderer,
                                   engine::Shader& shader,
                                   const engine::Mesh& cube,
                                   const EditorScene& scene,
                                   const glm::mat4& viewProj,
                                   bool selectedOnly) const;

    void DrawPhysicsEventGuides(engine::Renderer& renderer,
                                engine::Shader& shader,
                                const engine::Mesh& cube,
                                const std::vector<PhysicsEventGuide>& guides,
                                const glm::mat4& viewProj) const;

    void DrawPhysicsJointGuides(engine::Renderer& renderer,
                            engine::Shader& shader,
                            const engine::Mesh& cube,
                            const std::vector<PhysicsJointGuide>& guides,
                            const glm::mat4& viewProj) const;

    void DrawSelectedModelOutline(engine::Renderer& renderer,
                                  engine::Shader& shader,
                                  const engine::ecs::Transform& transform,
                                  const engine::Model& model,
                                  const glm::vec3& color,
                                  float thickness) const;

    void DrawSelectedMeshOutline(engine::Renderer& renderer,
                                 engine::Shader& shader,
                                 const engine::ecs::Transform& transform,
                                 const engine::Mesh& mesh,
                                 const glm::vec3& color,
                                 float thickness) const;

    void DrawLoadedModel(engine::Shader& shader,
                         const engine::ecs::Transform& transform,
                         const engine::Model& model) const;

    void DrawSceneObject(engine::Renderer& renderer,
                         engine::Shader& shader,
                         const engine::ecs::Transform& transform,
                         const engine::ecs::MeshRenderer& meshRenderer,
                         const engine::Texture* diffuseTexture,
                         const glm::vec3& emissive) const;

    bool ProjectWorldToScreen(const glm::vec3& world,
                              const glm::mat4& viewProj,
                              int width,
                              int height,
                              glm::vec2* screen) const;

    int PickSceneObject(const EditorScene& scene,
                        const engine::RuntimeAssetManager& assets,
                        float x,
                        float y,
                        const glm::mat4& viewProj,
                        int width,
                        int height) const;

    bool PickGizmoHandle(EditorGizmo& gizmo,
                         const EditorScene& scene,
                         float x,
                         float y,
                         const glm::mat4& viewProj,
                         int width,
                         int height) const;

    glm::vec3 SceneDropPosition(float x,
                                float y,
                                const glm::mat4& viewProj,
                                int width,
                                int height) const;
};
