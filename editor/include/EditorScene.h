#pragma once

#include <engine/ecs/Components.h>
#include <engine/ecs/Entity.h>
#include <engine/ecs/Registry.h>

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace engine {
class Mesh;
}

class EditorScene {
public:
    enum class Primitive { Plane, Cube };

    struct Object {
        engine::ecs::Entity entity = engine::ecs::kNull;
        std::string name;
        Primitive primitive = Primitive::Cube;
        bool visible = true;
        bool locked = false;
        std::string modelAssetPath;
        std::string materialAssetPath;
        glm::vec3 linearVelocity{0.0f};
        glm::vec3 angularVelocityAxis{0.0f, 1.0f, 0.0f};
        float angularVelocityRadians = 0.0f;
    };

    struct ObjectSnapshot {
        Object object;
        engine::ecs::Transform transform;
        glm::vec3 color{1.0f};
    };

    struct Snapshot {
        std::vector<ObjectSnapshot> objects;
        int selectedIndex = -1;
        int nextCubeNumber = 1;
    };

    void BuildDefault(const engine::Mesh& Cube, const engine::Mesh& plane);
    bool Save(const std::string& path, std::string* error);
    bool Load(const std::string& path, const engine::Mesh& cube, const engine::Mesh& plane, std::string* error);

    engine::ecs::Registry& Registry() { return m_registry; }
    const std::vector<Object>& Objects() const { return m_objects; }
    bool IsDirty() const { return m_dirty; }
    void MarkClean() { m_dirty = false; }

    int SelectedIndex() const { return m_selectedIndex; }
    const Object* SelectedObject() const;
    engine::ecs::Transform* SelectedTransform();
    const engine::ecs::Transform* TryGetTransform(engine::ecs::Entity entity) const;
    const engine::ecs::MeshRenderer* TryGetMeshRenderer(engine::ecs::Entity entity) const;
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
    void ResetSelectedTransform();
    void BeginTransformEdit();
    void EndTransformEdit();
    bool Undo(const engine::Mesh& cube, const engine::Mesh& plane);
    bool Redo(const engine::Mesh& cube, const engine::Mesh& plane);
    Snapshot CreateSnapshot();
    void RestoreFromSnapshot(const Snapshot& snapshot, const engine::Mesh& cube, const engine::Mesh& plane);
    void AddCube(const engine::Mesh& cube);
    void AddPlane(const engine::Mesh& plane);
    bool AddModel(const std::string& path, const engine::Mesh& placeholderMesh, const engine::ecs::Transform& transform);
    bool CycleSelectedColor();
    bool SetSelectedPrimitive(Primitive primitive, const engine::Mesh& mesh);
    bool SetSelectedModelAsset(const std::string& path);
    bool SetSelectedMaterialAsset(const std::string& path);
    bool SetSelectedLinearVelocity(const glm::vec3& velocity);
    bool SetSelectedAngularVelocity(const glm::vec3& axis, float radiansPerSecond);
    bool ToggleSelectVisible();
    bool ToggleSelectedLocked();
    bool DuplicateSelected(const engine::Mesh& cube, const engine::Mesh& plane);
    bool DeleteSelected();

private:
    engine::ecs::Entity CreateObject(const std::string& name, Primitive primitive,
                                     const engine::Mesh& mesh, const engine::ecs::Transform& transform,
                                     const glm::vec3& color);
    Snapshot CaptureSnapshot();
    void RestoreSnapshot(const Snapshot& snapshot, const engine::Mesh& cube, const engine::Mesh& plane);
    void PushUndoSnapshot();
    void ClearHistory();
    void Clear();

    engine::ecs::Registry m_registry;
    std::vector<Object> m_objects;
    std::vector<Snapshot> m_undoStack;
    std::vector<Snapshot> m_redoStack;
    int m_selectedIndex = -1;
    int m_nextCubeNumber = 1;
    bool m_dirty = false;
    bool m_transformEditOpen = false;
};