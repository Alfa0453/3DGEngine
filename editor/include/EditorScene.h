#pragma once

#include <engine/ecs/Components.h>
#include <engine/ecs/Entity.h>
#include <engine/ecs/Registry.h>
#include <engine/physics/PhysicsComponents.h>

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace engine {
class Mesh;
}

class EditorScene {
public:
    enum class Primitive { Plane, Cube, Sphere };

    struct Object {
        engine::ecs::Entity entity = engine::ecs::kNull;
        std::string name;
        Primitive primitive = Primitive::Cube;
        bool light = false;
        engine::ecs::Light lightData;
        bool visible = true;
        bool locked = false;
        std::string modelAssetPath;
        std::string materialAssetPath;
        bool linearVelocityEnabled = false;
        bool angularVelocityEnabled = false;
        glm::vec3 linearVelocity{0.0f};
        glm::vec3 angularVelocityAxis{0.0f, 1.0f, 0.0f};
        float angularVelocityRadians = 0.0f;
        bool rigidBodyEnabled = false;
        bool colliderEnabled = false;
        engine::ecs::RigidBody rigidBody;
        engine::ecs::Collider collider;
    };

    struct ObjectSnapshot {
        Object object;
        engine::ecs::Transform transform;
        glm::vec3 color{1.0f};
    };

    struct Environment {
        float timeOfDay = 0.46f;
        float skyLightIntensity = 1.0f;
        bool driveSunLight = true;
        float sunIntensity = 1.0f;
        bool showLightGuides = true;
        bool selectedLightGuideOnly = true;
        bool ibl = true;
        bool ssao = false;
        float ssaoRadius = 0.5f;
        float ssaoBias = 0.025f;
        bool ssr = false;
        float ssrIntensity = 0.5f;
        bool directionalShadows = true;
        bool pointShadows = true;
        bool spotShadows = true;
        float shadowSoftness = 2.5f;
        bool  fog = true;
        glm::vec3 fogColor{0.58f, 0.68f, 0.80f};
        float fogDensity = 0.008f;
        float fogHeight = -0.35f;
        float fogHeightFalloff = 0.10f;
        glm::vec3 physicsGravity{0.0f, -9.81f, 0.0f};
        int physicsSolverIterations = 4;
        bool physicsBroadPhase = true;
        float physicsCellSize = 2.0f;
        float physicsRestitutionThreshold = 0.5f;
        bool physicsAllowSleeping = true;
        float physicsSleepLinearVelocity = 0.06f;
        float physicsTimeToSleep = 0.5f;
        bool showPhysicsGuides = true;
        bool selectedPhysicsGuideOnly = false;
    };

    struct Snapshot {
        std::vector<ObjectSnapshot> objects;
        int selectedIndex = -1;
        int nextCubeNumber = 1;
    };

    void BuildDefault(const engine::Mesh& Cube, const engine::Mesh& plane, const engine::Mesh& sphere);
    bool Save(const std::string& path, std::string* error, bool markClean = true);
    bool Load(const std::string& path, const engine::Mesh& cube, const engine::Mesh& plane, const engine::Mesh& sphere, std::string* error);

    engine::ecs::Registry& Registry() { return m_registry; }
    const std::vector<Object>& Objects() const { return m_objects; }
    bool IsDirty() const { return m_dirty; }
    void MarkClean() { m_dirty = false; }
    void MarkDirty() { m_dirty = true; }

    int SelectedIndex() const { return m_selectedIndex; }
    const Object* SelectedObject() const;
    engine::ecs::Transform* SelectedTransform();
    const engine::ecs::Transform* TryGetTransform(engine::ecs::Entity entity) const;
    const engine::ecs::MeshRenderer* TryGetMeshRenderer(engine::ecs::Entity entity) const;
    const engine::ecs::Light* TryGetLight(engine::ecs::Entity entity) const;
    const Environment& GetEnvironment() const { return m_environment; }
    bool IsVisible(engine::ecs::Entity entity) const;
    bool SelectedLocked() const;

    void SelectNext();
    void SelectPrevious();
    void SelectIndex(int index);
    void Deselect();
    void MoveSelected(const glm::vec3& delta);
    void RotateSelected(const glm::vec3& axis, float degrees);
    void RotateSelectedYaw(float degrees);
    void ScaleSelectedAxis(const glm::vec3& axis, float factor);
    void ScaleSelected(float factor);
    bool SetSelectedTransform(const engine::ecs::Transform& transform);
    void ResetSelectedTransform();
    void BeginTransformEdit();
    void EndTransformEdit();
    bool Undo(const engine::Mesh& cube, const engine::Mesh& plane, const engine::Mesh& sphere);
    bool Redo(const engine::Mesh& cube, const engine::Mesh& plane, const engine::Mesh& sphere);
    Snapshot CreateSnapshot();
    void RestoreFromSnapshot(const Snapshot& snapshot, const engine::Mesh& cube, const engine::Mesh& plane, const engine::Mesh& sphere);
    void AddCube(const engine::Mesh& cube);
    void AddPlane(const engine::Mesh& plane);
    void AddSphere(const engine::Mesh& sphere);
    void AddDirectionalLight(const engine::Mesh& placeholderMesh);
    void AddPointLight(const engine::Mesh& placeholderMesh);
    void AddSpotLight(const engine::Mesh& placeholderMesh);
    void AddAreaLight(const engine::Mesh& placeholderMesh);
    bool AddModel(const std::string& path, const engine::Mesh& placeholderMesh, const engine::ecs::Transform& transform);
    bool CycleSelectedColor();
    bool SetSelectedName(const std::string& name);
    bool SetSelectedColor(const glm::vec3& color);
    bool SetSelectedPrimitive(Primitive primitive, const engine::Mesh& mesh);
    bool SetSelectedModelAsset(const std::string& path);
    bool SetSelectedMaterialAsset(const std::string& path);
    bool SetSelectedLight(const engine::ecs::Light& light);
    void SetEnvironment(const Environment& environment);
    bool SetSelectedLinearVelocityEnabled(bool enabled);
    bool SetSelectedAngularVelocityEnabled(bool enabled);
    bool SetSelectedLinearVelocity(const glm::vec3& velocity);
    bool SetSelectedAngularVelocity(const glm::vec3& axis, float radiansPerSecond);
    bool SetSelectedRigidBodyEnabled(bool enabled);
    bool SetSelectedRigidBody(const engine::ecs::RigidBody& rigidBody);
    bool SetSelectedColliderEnabled(bool enabled);
    bool SetSelectedCollider(const engine::ecs::Collider& collider);
    bool ToggleSelectVisible();
    bool ToggleSelectedLocked();
    bool DuplicateSelected(const engine::Mesh& cube, const engine::Mesh& plane, const engine::Mesh& sphere);
    bool DeleteSelected();

private:
    engine::ecs::Entity CreateObject(const std::string& name, Primitive primitive,
                                     const engine::Mesh& mesh, const engine::ecs::Transform& transform,
                                     const glm::vec3& color);
    Snapshot CaptureSnapshot();
    void RestoreSnapshot(const Snapshot& snapshot, const engine::Mesh& cube, const engine::Mesh& plane, const engine::Mesh& sphere);
    void PushUndoSnapshot();
    void ClearHistory();
    void Clear();

    engine::ecs::Registry m_registry;
    std::vector<Object> m_objects;
    std::vector<Snapshot> m_undoStack;
    std::vector<Snapshot> m_redoStack;
    Environment m_environment;
    int m_selectedIndex = -1;
    int m_nextCubeNumber = 1;
    bool m_dirty = false;
    bool m_transformEditOpen = false;
};