#pragma once

#include "EditorGizmo.h"
#include "EditorScene.h"

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace engine {
class Camera;
class Mesh;
class Model;
class Renderer;
class RuntimeAssetManager;
class Shader;
class SkinnedModel;
class Texture;
namespace ai { struct NavGrid; class NavMesh; }
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

    struct AiAgentGuide {
        glm::vec3 position{0.0f};
        glm::vec3 facing{0.0f, 0.0f, 1.0f};
        std::vector<glm::vec3> path;   // current chase/search corridor
        int  state = 0;                // 0 patrol, 1 chase, 2 search
        bool seesTarget = false;
        bool hasTarget = false;
    };

    bool ContainsPoint(float x, float y, int width, int height) const;

    void DrawSceneGizmo(engine::Renderer& renderer,
                        engine::Shader& shader,
                        const engine::Mesh& cube,
                        const engine::Mesh& cone,
                                    const EditorScene& scene,
                                    const EditorGizmo& gizmo,
                                    const glm::mat4& viewProj,
                                    const engine::Camera& camera,
                                    int viewportHeight) const;

    void DrawSelectedLightGuide(engine::Renderer& renderer,
                                engine::Shader& shader,
                                const engine::Mesh& cube,
                                const EditorScene& scene,
                                const glm::mat4& viewProj,
                                bool selectedOnly) const;

    // Reference ground grid on the XZ plane (minor + brighter major lines) with
    // coloured world axes through the origin: X red, Z blue, Y green.
    void DrawWorldGrid(engine::Renderer& renderer,
                       engine::Shader& shader,
                       const engine::Mesh& cube,
                       const glm::mat4& viewProj) const;

    void DrawPhysicsColliderGuides(engine::Renderer& renderer,
                                   engine::Shader& shader,
                                   const engine::Mesh& cube,
                                   const EditorScene& scene,
                                   const glm::mat4& viewProj,
                                   bool selectedOnly) const;

    // Unreal-style forward arrow for skeletal characters: points along the object's
    // local -Z (the gameplay "front" the player controller faces). Align the character
    // mesh to it (via the Model Rot offset) so it faces forward when placed.
    void DrawCharacterFacingArrows(engine::Renderer& renderer,
                                   engine::Shader& shader,
                                   const engine::Mesh& cube,
                                   const EditorScene& scene,
                                   const glm::mat4& viewProj) const;

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

    // Draws the selected AI agent's patrol path (markers + looped segments) and,
    // when it has a chase target, its vision cone boundary rays.
    void DrawNavAgentGuides(engine::Renderer& renderer,
                            engine::Shader& shader,
                            const engine::Mesh& cube,
                            const EditorScene& scene,
                            const glm::mat4& viewProj) const;

    void DrawCameraSequenceGuides(engine::Renderer& renderer,
                                  engine::Shader& shader,
                                  const engine::Mesh& cube,
                                  const EditorScene& scene,
                                  const glm::mat4& viewProj) const;

    // Draws every spline object's smooth curve plus its control-point handles
    // (the selected spline is highlighted).
    void DrawSplineGuides(engine::Renderer& renderer,
                          engine::Shader& shader,
                          const engine::Mesh& cube,
                          const EditorScene& scene,
                          const glm::mat4& viewProj) const;

    void DrawNavMeshBoundsGuides(engine::Renderer& renderer,
                                 engine::Shader& shader,
                                 const engine::Mesh& cube,
                                 const EditorScene& scene,
                                 const glm::mat4& viewProj) const;

    void DrawAudioSourceGuides(engine::Renderer& renderer,
                               engine::Shader& shader,
                               const engine::Mesh& cube,
                               const EditorScene& scene,
                               const glm::mat4& viewProj) const;

    void DrawParticleSystemGuides(engine::Renderer& renderer,
                                  engine::Shader& shader,
                                  const engine::Mesh& cube,
                                  const EditorScene& scene,
                                  const glm::mat4& viewProj,
                                  bool selectedOnly,
                                  bool showShapes,
                                  bool showDirections,
                                  bool showBounds,
                                  bool showCullingState) const;

    // Play-mode overlay: per agent, a state-coloured marker (green patrol / red
    // chase / amber search, brighter when it sees its target) and its live path.
    void DrawAiAgentDebugGuides(engine::Renderer& renderer,
                                engine::Shader& shader,
                                const engine::Mesh& cube,
                                const std::vector<AiAgentGuide>& guides,
                                const glm::mat4& viewProj) const;

    // Draws the blocked cells of the nav grid the agents actually path on (flat
    // markers), so you can see what pathfinding avoids. Capped for large grids.
    void DrawNavGridOverlay(engine::Renderer& renderer,
                            engine::Shader& shader,
                            const engine::Mesh& cube,
                            const engine::ai::NavGrid& grid,
                            const glm::mat4& viewProj) const;

    // Draws the walkable polygons of a baked navmesh (outlines), so you can see the
    // funnel-smoothed surface the navmesh agent paths on. Capped for large meshes.
    void DrawNavMeshOverlay(engine::Renderer& renderer,
                            engine::Shader& shader,
                            const engine::Mesh& cube,
                            const engine::ai::NavMesh& mesh,
                            const glm::mat4& viewProj) const;

    void DrawEditorNavMeshOverlay(engine::Renderer& renderer,
                                  engine::Shader& shader,
                                  const engine::Mesh& cube,
                                  const engine::ai::NavMesh& mesh,
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

    void DrawSelectedSkinnedModelOutline(engine::Renderer& renderer,
                                         engine::Shader& shader,
                                         const engine::ecs::Transform& transform,
                                         const engine::SkinnedModel& model,
                                         const std::vector<glm::mat4>& pose,
                                         const glm::vec3& color,
                                         float thickness,
                                         const glm::mat4& modelOffset = glm::mat4(1.0f)) const;

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
                         int height,
                         const engine::Camera& camera) const;

    glm::vec3 SceneDropPosition(float x,
                                float y,
                                const glm::mat4& viewProj,
                                int width,
                                int height) const;
};
