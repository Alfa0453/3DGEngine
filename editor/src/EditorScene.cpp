#include "EditorScene.h"

#include <engine/graphics/Mesh.h>

#include <fstream>
#include <sstream>
#include <cstddef>

using engine::ecs::Entity;
using engine::ecs::MeshRenderer;
using engine::ecs::Transform;

namespace {

const char* PrimitiveName(EditorScene::Primitive primitive) {
    switch (primitive) {
    case EditorScene::Primitive::Plane: return "Plane";
    case EditorScene::Primitive::Cube: return "Cube";
    }
    return "Cube";
}

bool ParsePrimitive(const std::string& value, EditorScene::Primitive* primitive) {
    if (value == "Plane") {
        *primitive = EditorScene::Primitive::Plane;
        return true;
    }
    if (value == "Cube") {
        *primitive = EditorScene::Primitive::Cube;
        return true;
    }
    return false;
}

const engine::Mesh& MeshFor(EditorScene::Primitive primitive, const engine::Mesh& cube,
                            const engine::Mesh& plane){
    return primitive == EditorScene::Primitive::Plane ? plane : cube;
}

const glm::vec3 kPalette[] = {
    {0.83f, 0.20f, 0.24f},
    {0.20f, 0.55f, 0.92f},
    {0.32f, 0.73f, 0.45f},
    {0.78f, 0.48f, 0.18f},
    {0.68f, 0.42f, 0.82f},
    {0.82f, 0.78f, 0.42f},
    {0.34f, 0.37f, 0.41f}
};

} // namespace

void EditorScene::BuildDefault(const engine::Mesh & cube, const engine::Mesh & plane)
{
    Clear();

    Transform ground;
    ground.position = glm::vec3(0.0f, -0.5f, 0.0f);
    ground.scale = glm::vec3(8.0f, 1.0f, 8.0f);
    CreateObject("Ground", Primitive::Plane, plane, ground, glm::vec3(0.34f, 0.37f, 0.41f));

    const glm::vec3 positions[] = {
        {-2.0f, 0.25f, 0.0f},
        {0.0f, 0.25f, 0.0f},
        {2.0f, 0.25f, 0.0f}
    };
    const glm::vec3 colors[] = {
        {0.83f, 0.20f, 0.24f},
        {0.20f, 0.55f, 0.92f},
        {0.32f, 0.73f, 0.45f}
    };

    for (int i = 0; i < 3; ++i) {
        Transform cubeTransform;
        cubeTransform.position = positions[i];
        cubeTransform.scale = glm::vec3(0.9f);
        CreateObject("Cube_" + std::to_string(i + 1), Primitive::Cube, cube, cubeTransform, colors[i]);
    }

    m_selectedIndex = 1;
    m_dirty = false;
    ClearHistory();
}

bool EditorScene::Save(const std::string & path, std::string * error)
{
    std::ofstream out(path);
    if (!out) {
        if (error) *error = "Could not open scene file for writing.";
        return false;
    }

    out << "3DGEditorScene 1\n";
    for (const Object& object : m_objects) {
        const Transform* transform = m_registry.TryGet<Transform>(object.entity);
        const MeshRenderer* renderer = m_registry.TryGet<MeshRenderer>(object.entity);
        if (!transform || !renderer) {
            continue;
        }

        out << "object "
            << PrimitiveName(object.primitive) << ' '
            << object.name << ' '
            << transform->position.x << ' ' << transform->position.y << ' ' << transform->position.z << ' '
            << transform->scale.x << ' ' << transform->scale.y << ' ' << transform->scale.z << ' '
            << renderer->color.r << ' ' << renderer->color.g << ' ' << renderer->color.b << '\n';
    }

    m_dirty = false;
    ClearHistory();
    return true;
}

bool EditorScene::Load(const std::string & path, const engine::Mesh & cube, const engine::Mesh & plane, std::string * error)
{
    std::ifstream in(path);
    if (!in) {
        if (error) *error = "Could not open scene file for reading.";
        return false;
    }

    std::string magic;
    int version = 0;
    in >> magic >> version;
    if (magic != "3DGEditorScene" || version != 1) {
        if (error) *error = "Scene file has an unknown format.";
        return false;
    }

    Clear();

    std::string recordType;
    while (in >> recordType) {
        if (recordType != "object") {
            std::string rest;
            std::getline(in, rest);
            continue;
        }

        std::string primitiveName;
        std::string name;
        Primitive primitive = Primitive::Cube;
        Transform transform;
        glm::vec3 color;

        in >> primitiveName >> name
           >> transform.position.x >> transform.position.y >> transform.position.z
           >> transform.scale.x >> transform.scale.y >> transform.scale.z
           >> color.r >> color.g >> color.b;

        if (!in || !ParsePrimitive(primitiveName, &primitive)) {
            if (error) *error = "Scene file contains an invalid object record.";
            Clear();
            return false;
        }

        CreateObject(name, primitive, MeshFor(primitive, cube, plane), transform, color);
    }

    m_selectedIndex = m_objects.empty() ? -1 : 0;
    m_dirty = false;
    ClearHistory();
    return true;
}

const EditorScene::Object * EditorScene::SelectedObject() const
{
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return nullptr;
    }
    return &m_objects[static_cast<std::size_t>(m_selectedIndex)];
}

engine::ecs::Transform * EditorScene::SelectedTransform()
{
    const Object* selected = SelectedObject();
    return selected ? m_registry.TryGet<Transform>(selected->entity) : nullptr;
}

void EditorScene::SelectNext()
{
    if (m_objects.empty()) {
        m_selectedIndex = -1;
        return;
    }
    m_selectedIndex = (m_selectedIndex + 1) % static_cast<int>(m_objects.size());
}

void EditorScene::SelectPrevious()
{
    if (m_objects.empty()) {
        m_selectedIndex = -1;
        return;
    }
    m_selectedIndex = (m_selectedIndex <= 0)
        ? static_cast<int>(m_objects.size()) - 1
        : m_selectedIndex - 1;
}

void EditorScene::MoveSelected(const glm::vec3 & delta)
{
    if (Transform* transform = SelectedTransform()) {
        transform->position += delta;
        m_dirty = true;
    }
}

void EditorScene::RotateSelectedYaw(float degrees)
{
    if (Transform* transform = SelectedTransform()) {
        const glm::quat yaw = glm::angleAxis(glm::radians(degrees), glm::vec3(0.0f, 1.0f, 0.0f));
        transform->rotation = yaw * transform->rotation;
        m_dirty = true;
    }
}

void EditorScene::ScaleSelected(float factor)
{
    if (Transform* transform = SelectedTransform()) {
        transform->scale *= factor;
        m_dirty = true;
    }
}

void EditorScene::ResetSelectedTransform()
{
    if (Transform* transform = SelectedTransform()) {
        transform->position = glm::vec3(0.0f);
        transform->scale = glm::vec3(1.0f);
        transform->rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        m_dirty = true;
    }
}

void EditorScene::BeginTransformEdit()
{
    if (!m_transformEditOpen && SelectedTransform()) {
        PushUndoSnapshot();
        m_transformEditOpen = true;
    }
}

void EditorScene::EndTransformEdit()
{
    m_transformEditOpen = false;
}

bool EditorScene::Undo(const engine::Mesh & cube, const engine::Mesh & plane)
{
    if (m_undoStack.empty()) {
        return false;
    }

    m_redoStack.push_back(CaptureSnapshot());
    RestoreSnapshot(m_undoStack.back(), cube, plane);
    m_undoStack.pop_back();
    m_dirty = true;
    m_transformEditOpen = false;
    return true;
}

bool EditorScene::Redo(const engine::Mesh & cube, const engine::Mesh & plane)
{
    if (m_redoStack.empty()) {
        return false;
    }

    m_undoStack.push_back(CaptureSnapshot());
    RestoreSnapshot(m_redoStack.back(), cube, plane);
    m_redoStack.pop_back();
    m_dirty = true;
    m_transformEditOpen = false;
    return true;
}

void EditorScene::AddCube(const engine::Mesh & cube)
{
    PushUndoSnapshot();

    Transform transform;
    transform.position = glm::vec3(0.0f, 0.25f, 0.0f);
    transform.scale = glm::vec3(0.9f);

    const glm::vec3 color(0.78f, 0.48f, 0.18f);
    CreateObject("Cube_" + std::to_string(m_nextCubeNumber++), Primitive::Cube, cube, transform, color);
    m_selectedIndex = static_cast<int>(m_objects.size()) - 1;
    m_dirty = true;
}

void EditorScene::AddPlane(const engine::Mesh & plane)
{
    PushUndoSnapshot();

    Transform transform;
    transform.position = glm::vec3(0.0f, 0.0f, 0.0f);
    transform.scale = glm::vec3(3.0f, 1.0f, 3.0f);

    const glm::vec3 color(0.34f, 0.37f, 0.41f);
    CreateObject("Plane_" + std::to_string(m_nextCubeNumber++), Primitive::Plane, plane, transform, color);
    m_selectedIndex = static_cast<int>(m_objects.size()) - 1;
    m_dirty = true;
}

bool EditorScene::CycleSelectedColor()
{
    const Object* selected = SelectedObject();
    if (!selected)
    {
        return false;
    }

    MeshRenderer* renderer = m_registry.TryGet<MeshRenderer>(selected->entity);
    if (!renderer) {
        return false;
    }

    PushUndoSnapshot();

    int next = 0;
    const int paletteCount = static_cast<int>(sizeof(kPalette) / sizeof(kPalette[0]));
    for (int i = 0; i < paletteCount; ++i) {
        const glm::vec3 delta = renderer->color - kPalette[i];
        if (glm::dot(delta, delta) < 0.0001f) {
            next = (i + 1) % paletteCount;
            break;
        }
    }

    renderer->color = kPalette[next];
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedPrimitive(Primitive primitive, const engine::Mesh & mesh)
{
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }

    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    MeshRenderer* renderer = m_registry.TryGet<MeshRenderer>(selected.entity);
    if (!renderer || selected.primitive == primitive) {
        return false;
    }

    PushUndoSnapshot();

    selected.primitive = primitive;
    renderer->mesh = &mesh;
    m_dirty = true;
    return true;
}

bool EditorScene::DuplicateSelected(const engine::Mesh & cube, const engine::Mesh & plane)
{
    const Object* selected = SelectedObject();
    if (!selected) {
        return false;
    }

    const Transform* transform = m_registry.TryGet<Transform>(selected->entity);
    const MeshRenderer* renderer = m_registry.TryGet<MeshRenderer>(selected->entity);
    if (!transform || !renderer) {
        return false;
    }

    PushUndoSnapshot();

    Transform duplicateTransform = *transform;
    duplicateTransform.position += glm::vec3(0.8f, 0.0f, 0.8f);

    const engine::Mesh& mesh = MeshFor(selected->primitive, cube, plane);
    CreateObject(selected->name + "_Copy", selected->primitive, mesh, duplicateTransform, renderer->color);
    m_selectedIndex = static_cast<int>(m_objects.size()) - 1;
    m_dirty = true;
    return true;
}

bool EditorScene::DeleteSelected()
{
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }

    PushUndoSnapshot();

    const std::size_t index = static_cast<std::size_t>(m_selectedIndex);
    m_registry.Destroy(m_objects[index].entity);
    m_objects.erase(m_objects.begin() + static_cast<std::ptrdiff_t>(index));

    if (m_objects.empty()) {
        m_selectedIndex = -1;
    } else if (m_selectedIndex >= static_cast<int>(m_objects.size())) {
        m_selectedIndex = static_cast<int>(m_objects.size()) - 1;
    }

    m_dirty = true;
    return true;
}

engine::ecs::Entity EditorScene::CreateObject(const std::string & name, Primitive primitive, const engine::Mesh & mesh, const engine::ecs::Transform & transform, const glm::vec3 & color)
{
    Entity entity = m_registry.Create();
    m_registry.Add<Transform>(entity, transform);
    m_registry.Add<MeshRenderer>(entity, MeshRenderer{&mesh, color});
    m_objects.push_back({entity, name, primitive});
    return entity;
}

EditorScene::Snapshot EditorScene::CaptureSnapshot()
{
    Snapshot snapshot;
    snapshot.selectedIndex = m_selectedIndex;
    snapshot.nextCubeNumber = m_nextCubeNumber;

    for (const Object& object : m_objects) {
        const Transform* transform = m_registry.TryGet<Transform>(object.entity);
        const MeshRenderer* renderer = m_registry.TryGet<MeshRenderer>(object.entity);
        if (!transform || !renderer) {
            continue;
        }

        ObjectSnapshot objectSnapshot;
        objectSnapshot.object = object;
        objectSnapshot.transform = *transform;
        objectSnapshot.color = renderer->color;
        snapshot.objects.push_back(objectSnapshot);
    }

    return snapshot;
}

void EditorScene::RestoreSnapshot(const Snapshot & snapshot, const engine::Mesh & cube, const engine::Mesh & plane)
{
    m_registry = engine::ecs::Registry{};
    m_objects.clear();

    for (const ObjectSnapshot& objectSnapshot : snapshot.objects) {
        Entity entity = m_registry.Create();
        const engine::Mesh& mesh = MeshFor(objectSnapshot.object.primitive, cube, plane);
        m_registry.Add<Transform>(entity, objectSnapshot.transform);
        m_registry.Add<MeshRenderer>(entity, MeshRenderer{&mesh, objectSnapshot.color});

        Object object = objectSnapshot.object;
        object.entity = entity;
        m_objects.push_back(object);
    }

    m_selectedIndex = snapshot.selectedIndex;
    if (m_selectedIndex >= static_cast<int>(m_objects.size())) {
        m_selectedIndex = m_objects.empty() ? -1 : static_cast<int>(m_objects.size()) -1;
    }
    m_nextCubeNumber = snapshot.nextCubeNumber;
}

void EditorScene::PushUndoSnapshot()
{
    m_undoStack.push_back(CaptureSnapshot());
    m_redoStack.clear();
}

void EditorScene::ClearHistory()
{
    m_undoStack.clear();
    m_redoStack.clear();
    m_transformEditOpen = false;
}

void EditorScene::Clear()
{
    m_registry = engine::ecs::Registry{};
    m_objects.clear();
    m_undoStack.clear();
    m_redoStack.clear();
    m_selectedIndex = -1;
    m_nextCubeNumber = 1;
    m_dirty = false;
    m_transformEditOpen = false;
}
