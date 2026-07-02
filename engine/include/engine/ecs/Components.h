#pragma once

#include "engine/graphics/Mesh.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <string>

namespace engine {

class Model;     // non-owning loaded model pointers
class Texture;   // non-owning material map pointers

namespace ecs{

// Position / rotation / scale, with the model matrix derived on demand. The
// engine's common spatial component — most renderable or moving entities have one.
struct Transform {
    glm::vec3 position{0.0f};
    glm::vec3 scale{1.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};  // identity (w, x, y, z)

    glm::mat4 Model() const {
        return glm::translate(glm::mat4(1.0f), position)
             * glm::mat4_cast(rotation)
             * glm::scale(glm::mat4(1.0f), scale);
    }
};

struct LinearVelocity {
    glm::vec3 velocity{0.0f};
};

struct AngularVelocity {
    glm::vec3 axis{0.0f, 1.0f, 0.0f};
    float radiansPerSecond = 0.0f;
};

// Marks an entity as drawable: which mesh to draw and in what colour. The mesh is
// referenced, not owned — it lives in the game (e.g. a shared cube) so many
// entities can point at the same geometry.
struct MeshRenderer {
    const Mesh* mesh = nullptr;
    glm::vec3   color{1.0f};
};

// A physically-based surface description (metallic / roughness workflow). These
// are the parameters a Cook-Torrance shader needs; the scene demo sets them
// directly (a sphere grid sweeping metallic x roughness), and they could equally
// be filled from a loaded model's maps.
struct PbrMaterial {
    glm::vec3 albedo{0.8f, 0.8f, 0.8f}; // base colour
    float     metallic  = 0.0f;         // 0 = dielectric, 1 = metal
    float     roughness = 0.5f;         // 0 = mirror, 1 = fully rough
    float     ao        = 1.0f;         // ambient-occlusion factor
    glm::vec3 emissive{0.0f};           // self-illumination

    // Optional, non-owning texture maps. When set they modulate the values above.
    const Texture* albedoMap     = nullptr;   // RGB base colour
    const Texture* normalMap     = nullptr;   // tangent-space normals
    const Texture* metalRoughMap = nullptr;   // glTF ORM: G = roughness, B = metallic
};

// Drawable entity rendered through the PBR pipeline: geometry (referenced, not
// owned) plus its material.
struct MeshPBR {
    const Mesh* mesh = nullptr;
    PbrMaterial material;
};

// Runtime asset references imported from editor-authored scene files. These are
// path handles, not loaded GPU resources; a runtime asset system can resolve
// them into Model/Texture objects later.
struct ModelAsset {
    std::string path;
};

struct MaterialAsset {
    std::string albedoPath;
};

struct LoadedModelAsset {
    const Model* model = nullptr;
};

struct LoadedMaterialAsset {
    const Texture* albedoMap = nullptr;
};

// A light source. Point lights take their position from the entity's Transform;
// directional lights use `direction`. `intensity` scales `color`.
struct Light {
    enum class Type { Directional, Point, Spot, Area };
    Type      type     = Type::Point;
    glm::vec3 color{1.0f, 1.0f, 1.0f};
    float     intensity = 1.0f;
    glm::vec3 direction{0.0f, -1.0f, 0.0f} ;   // Directional + Spot
    float     innerAngle = 20.0f;             // Spot: degrees, full intensity inside
    float     outerAngle = 30.0f;             // Spot: degrees, fades to zero at the edge
    float     range      = 40.0f;             // Spot: shadow far plane
    float     sourceRadius = 1.0f;            // Area: physical sphere radius
};

} // namespace ecs
} // namespace engine