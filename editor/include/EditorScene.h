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

    void SelectNext();
    void SelectPrevious();
    void MoveSelected(const glm::vec3& delta);
    void RotateSelectedYaw(float degrees);
    void ScaleSelected(float factor);
    void ResetSelectedTransform();
    void BeginTransformEdit();
    void EndTransformEdit();
    bool Undo(const engine::Mesh& cube, const engine::Mesh& plane);
    bool Redo(const engine::Mesh& cube, const engine::Mesh& plane);
    void AddCube(const engine::Mesh& cube);
    void AddPlane(const engine::Mesh& plane);
    bool CycleSelectedColor();
    bool SetSelectedPrimitive(Primitive primitive, const engine::Mesh& mesh);
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