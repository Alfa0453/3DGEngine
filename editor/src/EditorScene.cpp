#include "EditorScene.h"

#include <engine/graphics/Mesh.h>

#include <fstream>
#include <sstream>
#include <cstddef>
#include <filesystem>
#include <glm/gtc/quaternion.hpp>

using engine::ecs::Entity;
using engine::ecs::Light;
using engine::ecs::MeshRenderer;
using engine::ecs::RigidBody;
using engine::ecs::Collider;
using engine::ecs::Transform;

namespace {

const char* PrimitiveName(EditorScene::Primitive primitive) {
    switch (primitive) {
    case EditorScene::Primitive::Plane: return "Plane";
    case EditorScene::Primitive::Cube: return "Cube";
    case EditorScene::Primitive::Sphere: return "Sphere";
    }
    return "Cube";
}

const char* LightTypeName(Light::Type type) {
    switch (type) {
    case Light::Type::Directional:  return "Directional";
    case Light::Type::Point:        return "Point";
    case Light::Type::Spot:         return "Spot";
    case Light::Type::Area:         return "Area";
    }
    return "Point";
}

bool ParseLightType(const std::string& value, Light::Type* type) {
    if (value == "Directional") {
        *type = Light::Type::Directional;
        return true;
    }
    if (value == "Point") {
        *type = Light::Type::Point;
        return true;
    }
    if (value == "Spot") {
        *type = Light::Type::Spot;
        return true;
    }
    if (value == "Area") {
        *type = Light::Type::Area;
        return true;
    }
    return false;
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
    if (value == "Sphere") {
        *primitive = EditorScene::Primitive::Sphere;
    }
    return false;
}

const engine::Mesh& MeshFor(EditorScene::Primitive primitive, const engine::Mesh& cube,
                            const engine::Mesh& plane, const engine::Mesh& sphere){
    if (primitive == EditorScene::Primitive::Sphere)
    {
        return sphere;
    }
    if (primitive == EditorScene::Primitive::Plane)
    {
        return plane;
    }

    return cube;
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

const char* StoredPath(const std::string& path) {
    return path.empty() ? "-" : path.c_str();
}

} // namespace

void EditorScene::BuildDefault(const engine::Mesh & cube, const engine::Mesh & plane, const engine::Mesh & sphere)
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

    AddDirectionalLight(sphere);
    AddPointLight(sphere);

    m_selectedIndex = 1;
    m_dirty = false;
    ClearHistory();
}

bool EditorScene::Save(const std::string & path, std::string * error, bool markClean)
{
    std::ofstream out(path);
    if (!out) {
        if (error) *error = "Could not open scene file for writing.";
        return false;
    }

    out << "3DGEditorScene 16\n";
    out << "environment "
        << m_environment.timeOfDay << ' '
        << m_environment.skyLightIntensity << ' '
        << (m_environment.driveSunLight ? 1 : 0) << ' '
        << m_environment.sunIntensity << ' '
        << (m_environment.showLightGuides ? 1 : 0) << ' '
        << (m_environment.selectedLightGuideOnly ? 1 : 0) << ' '
        << (m_environment.ibl ? 1 : 0) << ' '
        << (m_environment.ssao ? 1 : 0) << ' '
        << m_environment.ssaoRadius << ' '
        << m_environment.ssaoBias << ' '
        << (m_environment.ssr ? 1 : 0) << ' '
        << m_environment.ssrIntensity << ' '
        << (m_environment.directionalShadows ? 1 : 0) << ' '
        << (m_environment.pointShadows ? 1 : 0) << ' '
        << (m_environment.spotShadows ? 1 : 0) << ' '
        << m_environment.shadowSoftness << ' '
        << (m_environment.fog ? 1 : 0) << ' '
        << m_environment.fogColor.r << ' '
        << m_environment.fogColor.g << ' '
        << m_environment.fogColor.b << ' '
        << m_environment.fogDensity << ' '
        << m_environment.fogHeight << ' '
        << m_environment.fogHeightFalloff << ' '
        << m_environment.physicsGravity.x << ' '
        << m_environment.physicsGravity.y << ' '
        << m_environment.physicsGravity.z << ' '
        << m_environment.physicsSolverIterations << ' '
        << (m_environment.physicsBroadPhase ? 1 : 0) << ' '
        << m_environment.physicsCellSize << ' '
        << m_environment.physicsRestitutionThreshold << ' '
        << (m_environment.physicsAllowSleeping ? 1 : 0) << ' '
        << m_environment.physicsSleepLinearVelocity << ' '
        << m_environment.physicsTimeToSleep << ' '
        << (m_environment.showPhysicsGuides ? 1 : 0) << ' '
        << (m_environment.selectedPhysicsGuideOnly ? 1 : 0) << '\n';
    for (const Object& object : m_objects) {
        const Transform* transform = m_registry.TryGet<Transform>(object.entity);
        const MeshRenderer* renderer = m_registry.TryGet<MeshRenderer>(object.entity);
        if (!transform || !renderer) {
            continue;
        }

        if (object.light) {
            const Light* light = m_registry.TryGet<Light>(object.entity);
            const Light& data = light ? *light : object.lightData;
            out << "light "
                << object.name << ' '
                << LightTypeName(data.type) << ' '
                << transform->position.x << ' ' << transform->position.y << ' ' << transform->position.z << ' '
                << data.color.r << ' ' << data.color.g << ' ' << data.color.b << ' '
                << data.intensity << ' '
                << data.direction.x << ' ' << data.direction.y << ' ' << data.direction.z << ' '
                << data.innerAngle << ' ' << data.outerAngle << ' ' << data.range << ' ' << data.sourceRadius << ' '
                << (object.visible ? 1 : 0) << ' '
                << (object.locked ? 1 : 0) << '\n';
            continue;
        }

        out << "object "
            << PrimitiveName(object.primitive) << ' '
            << object.name << ' '
            << transform->position.x << ' ' << transform->position.y << ' ' << transform->position.z << ' '
            << transform->scale.x << ' ' << transform->scale.y << ' ' << transform->scale.z << ' '
            << transform->rotation.w << ' ' << transform->rotation.x << ' '
            << transform->rotation.y << ' ' << transform->rotation.z << ' '
            << renderer->color.r << ' ' << renderer->color.g << ' ' << renderer->color.b << ' '
            << (object.visible ? 1 : 0) << ' '
            << (object.locked ? 1 : 0) << ' '
            << StoredPath(object.modelAssetPath) << ' '
            << StoredPath(object.materialAssetPath) << ' '
            << object.linearVelocity.x << ' ' << object.linearVelocity.y << ' ' << object.linearVelocity.z << ' '
            << object.angularVelocityAxis.x << ' ' << object.angularVelocityAxis.y << ' ' << object.angularVelocityAxis.z << ' '
            << object.angularVelocityRadians << ' '
            << (object.linearVelocityEnabled ? 1 : 0) << ' '
            << (object.angularVelocityEnabled ? 1 : 0) << ' '
            << (object.rigidBodyEnabled ? 1 : 0) << ' '
            << object.rigidBody.velocity.x << ' ' << object.rigidBody.velocity.y << ' ' << object.rigidBody.velocity.z << ' '
            << object.rigidBody.invMass << ' '
            << (object.rigidBody.useGravity ? 1 : 0) << ' '
            << (object.rigidBody.allowSleep ? 1 : 0) << ' '
            << (object.rigidBody.ccd ? 1 : 0) << ' '
            << (object.colliderEnabled ? 1 : 0) << ' '
            << static_cast<int>(object.collider.shape) << ' '
            << object.collider.radius << ' '
            << object.collider.halfExtents.x << ' ' << object.collider.halfExtents.y << ' ' << object.collider.halfExtents.z << ' '
            << object.collider.planeNormal.x << ' ' << object.collider.planeNormal.y << ' ' << object.collider.planeNormal.z << ' '
            << object.collider.planeOffset << ' '
            << object.collider.restitution << ' '
            << object.collider.friction << ' '
            << (object.collider.isTrigger ? 1 : 0) << '\n';
    }

    if (markClean) {
        m_dirty = false;
        ClearHistory();
    }

    return true;
}

bool EditorScene::Load(const std::string & path, const engine::Mesh & cube, const engine::Mesh & plane, const engine::Mesh & sphere, std::string * error)
{
    std::ifstream in(path);
    if (!in) {
        if (error) *error = "Could not open scene file for reading.";
        return false;
    }

    std::string magic;
    int version = 0;
    in >> magic >> version;
    if (magic != "3DGEditorScene" ||(version < 1 || version > 14)) {
        if (error) *error = "Scene file has an unknown format.";
        return false;
    }

    Clear();

    std::string recordType;
    while (in >> recordType) {
        if (recordType == "environment") {
            if (version < 8) {
                std::string rest;
                std::getline(in, rest);
                continue;
            }

            int fog = 1;
            int driveSun = 1;
            int showGuides = 1;
            int selectedGuidesOnly = 1;
            int ibl = 1;
            int ssao = 0;
            int ssr = 0;
            int directionalShadows = 1;
            int pointShadows = 1;
            int spotShadows = 1;
            int physicsBroadPhase = m_environment.physicsBroadPhase ? 1 : 0;
            int physicsAllowSleeping = m_environment.physicsAllowSleeping ? 1 : 0;
            int showPhysicsGuides = m_environment.showPhysicsGuides ? 1 : 0;
            int selectedPhysicsGuideOnly = m_environment.selectedPhysicsGuideOnly ? 1 : 0;
            in >> m_environment.timeOfDay
               >> m_environment.skyLightIntensity;
            if (version >= 9) {
                in >> driveSun
                   >> m_environment.sunIntensity;
            }
            if (version >= 10) {
                in >> showGuides
                   >> selectedGuidesOnly;
            }
            if (version >= 11) {
                in >> ibl
                   >> ssao
                   >> m_environment.ssaoRadius
                   >> m_environment.ssaoBias
                   >> ssr
                   >> m_environment.ssrIntensity
                   >> directionalShadows
                   >> pointShadows
                   >> spotShadows
                   >> m_environment.shadowSoftness;
            }
            in >> fog;
            if (version >= 12) {
                in >> m_environment.fogColor.r
                >> m_environment.fogColor.g
                >> m_environment.fogColor.b;
            }
            in >> m_environment.fogDensity
               >> m_environment.fogHeight
               >> m_environment.fogHeightFalloff;
            if (version >= 15) {
                in >> m_environment.physicsGravity.x
                   >> m_environment.physicsGravity.y
                   >> m_environment.physicsGravity.z
                   >> m_environment.physicsSolverIterations
                   >> physicsBroadPhase
                   >> m_environment.physicsCellSize
                   >> m_environment.physicsRestitutionThreshold
                   >> physicsAllowSleeping
                   >> m_environment.physicsSleepLinearVelocity
                   >> m_environment.physicsTimeToSleep;
            }
            if (version >= 16) {
                in >> showPhysicsGuides
                   >> selectedPhysicsGuideOnly;
            }
            if (!in) {
                if (error) *error = "Scene file contains an invalid environment record.";
                Clear();
                return false;
            }
            m_environment.driveSunLight = driveSun != 0;
            m_environment.showLightGuides = showGuides != 0;
            m_environment.selectedLightGuideOnly = selectedGuidesOnly != 0;
            m_environment.ibl = ibl != 0;
            m_environment.ssao = ssao != 0;
            m_environment.ssr = ssr != 0;
            m_environment.directionalShadows = directionalShadows != 0;
            m_environment.pointShadows = pointShadows != 0;
            m_environment.spotShadows = spotShadows != 0;
            m_environment.fog = fog != 0;
            m_environment.physicsBroadPhase = physicsBroadPhase != 0;
            m_environment.physicsAllowSleeping = physicsAllowSleeping != 0;
            m_environment.showPhysicsGuides = showPhysicsGuides != 0;
            m_environment.selectedPhysicsGuideOnly = selectedPhysicsGuideOnly != 0;
            continue;
        }

        if (recordType == "light") {
            if (version < 7) {
                std::string rest;
                std::getline(in, rest);
                continue;
            }

            std::string name;
            std::string lightTypeName;
            Transform transform;
            Light light;
            int visible = 1;
            int locked = 0;
            in >> name >> lightTypeName
               >> transform.position.x >> transform.position.y >> transform.position.z
               >> light.color.r >> light.color.g >> light.color.b
               >> light.intensity
               >> light.direction.x >> light.direction.y >> light.direction.z
               >> light.innerAngle >> light.outerAngle >> light.range >> light.sourceRadius
               >> visible >> locked;

            if (!in || !ParseLightType(lightTypeName, &light.type)) {
                if (error) *error = "Scene file contains an invalid light record.";
                Clear();
                return false;
            }

            transform.scale = glm::vec3(0.22f);
            const glm::vec3 color = light.color * light.intensity;
            CreateObject(name, Primitive::Cube, cube, transform, color);
            Object& object = m_objects.back();
            object.light = true;
            object.lightData = light;
            object.visible = visible != 0;
            object.locked = locked != 0;
            m_registry.Add<Light>(object.entity, light);
            continue;
        }

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
        int visible = 1;
        int locked = 0;
        std::string modelAssetPath;
        std::string materialAssetPath;
        glm::vec3 linearVelocity{0.0f};
        glm::vec3 angularVelocityAxis{0.0f, 1.0f, 0.0f};
        float angularVelocityRadians = 0.0f;
        int linearVelocityEnabled = 0;
        int angularVelocityEnabled = 0;
        int rigidBodyEnabled = 0;
        RigidBody rigidBody;
        int colliderEnabled = 0;
        Collider collider;
        int colliderShape = static_cast<int>(engine::ecs::ColliderShape::Sphere);
        int rigidBodyUseGravity = rigidBody.useGravity ? 1 : 0;
        int rigidBodyAllowSleep = rigidBody.allowSleep ? 1 : 0;
        int rigidBodyCcd = rigidBody.ccd ? 1 : 0;
        int colliderTrigger = collider.isTrigger ? 1 : 0;

        in >> primitiveName >> name
           >> transform.position.x >> transform.position.y >> transform.position.z
           >> transform.scale.x >> transform.scale.y >> transform.scale.z;

        if (version >= 2) {
            in >> transform.rotation.w >> transform.rotation.x
               >> transform.rotation.y >> transform.rotation.z;
        }

        in >> color.r >> color.g >> color.b;

        if (version >= 3) {
            in >> visible;
        }
        if (version >= 4) {
            in >> locked;
        }
        if (version >= 5) {
            in >> modelAssetPath >> materialAssetPath;
            if (modelAssetPath == "-") {
                modelAssetPath.clear();
            }
            if (materialAssetPath == "-") {
                materialAssetPath.clear();
            }
        }
        if (version >= 6) {
            in >> linearVelocity.x >> linearVelocity.y >> linearVelocity.z
            >> angularVelocityAxis.x >> angularVelocityAxis.y >> angularVelocityAxis.z
            >> angularVelocityRadians;
        }
        if (version >= 13) {
            in >> linearVelocityEnabled
               >> angularVelocityEnabled;
        } else {
            linearVelocityEnabled = glm::dot(linearVelocity, linearVelocity) > 0.0f ? 1 : 0;
            angularVelocityEnabled = angularVelocityRadians != 0.0f
                && glm::dot(angularVelocityAxis, angularVelocityAxis) > 0.0f ? 1 : 0;
        }
        if (version >= 14) {
            in >> rigidBodyEnabled
               >> rigidBody.velocity.x >> rigidBody.velocity.y >> rigidBody.velocity.z
               >> rigidBody.invMass
               >> rigidBodyUseGravity
               >> rigidBodyAllowSleep
               >> rigidBodyCcd
               >> colliderEnabled
               >> colliderShape
               >> collider.radius
               >> collider.halfExtents.x >> collider.halfExtents.y >> collider.halfExtents.z
               >> collider.planeNormal.x >> collider.planeNormal.y >> collider.planeNormal.z
               >> collider.planeOffset
               >> collider.restitution
               >> collider.friction
               >> colliderTrigger;
            rigidBody.useGravity = rigidBodyUseGravity != 0;
            rigidBody.allowSleep = rigidBodyAllowSleep != 0;
            rigidBody.ccd = rigidBodyCcd != 0;
            collider.isTrigger = colliderTrigger != 0;
            if (colliderShape == static_cast<int>(engine::ecs::ColliderShape::Plane)) {
                collider.shape = engine::ecs::ColliderShape::Plane;
            } else if (colliderShape == static_cast<int>(engine::ecs::ColliderShape::Box)) {
                collider.shape = engine::ecs::ColliderShape::Box;
            } else {
                collider.shape = engine::ecs::ColliderShape::Sphere;
            }
        }

        if (!in || !ParsePrimitive(primitiveName, &primitive)) {
            if (error) *error = "Scene file contains an invalid object record.";
            Clear();
            return false;
        }

        CreateObject(name, primitive, MeshFor(primitive, cube, plane, sphere), transform, color);
        m_objects.back().visible = visible != 0;
        m_objects.back().locked = locked != 0;
        m_objects.back().modelAssetPath = modelAssetPath;
        m_objects.back().materialAssetPath = materialAssetPath;
        m_objects.back().linearVelocity = linearVelocity;
        m_objects.back().angularVelocityAxis = angularVelocityAxis;
        m_objects.back().angularVelocityRadians = angularVelocityRadians;
        m_objects.back().rigidBodyEnabled = rigidBodyEnabled != 0;
        m_objects.back().rigidBody = rigidBody;
        m_objects.back().colliderEnabled = colliderEnabled != 0;
        m_objects.back().collider = collider;
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

const engine::ecs::Transform *EditorScene::TryGetTransform(engine::ecs::Entity entity) const
{
    return const_cast<engine::ecs::Registry&>(m_registry).TryGet<Transform>(entity);
}

const engine::ecs::MeshRenderer *EditorScene::TryGetMeshRenderer(engine::ecs::Entity entity) const
{
    return const_cast<engine::ecs::Registry&>(m_registry).TryGet<MeshRenderer>(entity);
}

const engine::ecs::Light* EditorScene::TryGetLight(engine::ecs::Entity entity) const
{
    return const_cast<engine::ecs::Registry&>(m_registry).TryGet<Light>(entity);
}

bool EditorScene::IsVisible(engine::ecs::Entity entity) const
{
    for (const Object& object : m_objects) {
        if (object.entity == entity) {
            return object.visible;
        }
    }
    return true;
}

bool EditorScene::SelectedLocked() const
{
    const Object* selected = SelectedObject();
    return selected ? selected->locked : false;
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

void EditorScene::SelectIndex(int index)
{
    if (index < 0 || index >= static_cast<int>(m_objects.size())) {
        return;
    }
    m_selectedIndex = index;
}

void EditorScene::Deselect()
{
    m_selectedIndex = -1;
}

void EditorScene::MoveSelected(const glm::vec3 & delta)
{
    if (SelectedLocked()) {
        return;
    }

    if (Transform* transform = SelectedTransform()) {
        transform->position += delta;
        m_dirty = true;
    }
}

void EditorScene::RotateSelected(const glm::vec3 &axis, float degrees)
{
    if (SelectedLocked()) {
        return;
    }
    if (Transform* transform = SelectedTransform()) {
        if (glm::dot(axis, axis) <= 0.0001f) {
            return;
        }
        const glm::quat rotation = glm::angleAxis(glm::radians(degrees), glm::normalize(axis));
        transform->rotation = rotation * transform->rotation;
        m_dirty = true;
    }
}

void EditorScene::RotateSelectedYaw(float degrees)
{
    RotateSelected(glm::vec3(0.0f, 1.0f, 0.0f), degrees);
}

void EditorScene::ScaleSelectedAxis(const glm::vec3 &axis, float factor)
{
    if (SelectedLocked()) {
        return;
    }
    if (Transform* transform = SelectedTransform()) {
        if (axis.x != 0.0f) {
            transform->scale.x *= factor;
        }
        if (axis.y != 0.0f) {
            transform->scale.y *= factor;
        }
        if (axis.z != 0.0f) {
            transform->scale.z *= factor;
        }
        m_dirty = true;
    }
}

void EditorScene::ScaleSelected(float factor)
{
    if (SelectedLocked()) {
        return;
    }

    if (Transform* transform = SelectedTransform()) {
        transform->scale *= factor;
        m_dirty = true;
    }
}

bool EditorScene::SetSelectedTransform(const Transform& value) {
    if (SelectedLocked()) {
        return false;
    }

    Transform* transform = SelectedTransform();
    if (!transform) {
        return false;
    }

    if (transform->position == value.position
        && transform->scale == value.scale
        && transform->rotation == value.rotation) {
        return false;
    }

    if (!m_transformEditOpen) {
        PushUndoSnapshot();
    }
    *transform = value;
    m_dirty = true;
    return true;
}

void EditorScene::ResetSelectedTransform()
{
    if (SelectedLocked()) {
        return;
    }

    if (Transform* transform = SelectedTransform()) {
        PushUndoSnapshot();
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

bool EditorScene::Undo(const engine::Mesh & cube, const engine::Mesh & plane, const engine::Mesh & sphere)
{
    if (m_undoStack.empty()) {
        return false;
    }

    m_redoStack.push_back(CaptureSnapshot());
    RestoreSnapshot(m_undoStack.back(), cube, plane, sphere);
    m_undoStack.pop_back();
    m_dirty = true;
    m_transformEditOpen = false;
    return true;
}

bool EditorScene::Redo(const engine::Mesh & cube, const engine::Mesh & plane, const engine::Mesh & sphere)
{
    if (m_redoStack.empty()) {
        return false;
    }

    m_undoStack.push_back(CaptureSnapshot());
    RestoreSnapshot(m_redoStack.back(), cube, plane, sphere);
    m_redoStack.pop_back();
    m_dirty = true;
    m_transformEditOpen = false;
    return true;
}

EditorScene::Snapshot EditorScene::CreateSnapshot()
{
    return CaptureSnapshot();
}

void EditorScene::RestoreFromSnapshot(const Snapshot &snapshot, const engine::Mesh &cube, const engine::Mesh &plane, const engine::Mesh &sphere)
{
    RestoreSnapshot(snapshot, cube, plane, sphere);
    ClearHistory();
    m_dirty = false;
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

void EditorScene::AddSphere(const engine::Mesh & sphere)
{
    PushUndoSnapshot();

    Transform transform;
    transform.position = glm::vec3(0.0f, 0.0f, 0.0f);
    transform.scale = glm::vec3(0.9f);

    const glm::vec3 color(0.68f, 0.27f, 0.31f);
    CreateObject("Plane_" + std::to_string(m_nextCubeNumber++), Primitive::Sphere, sphere, transform, color);
    m_selectedIndex = static_cast<int>(m_objects.size()) - 1;
    m_dirty = true;
}

void EditorScene::AddDirectionalLight(const engine::Mesh& placeholderMesh) {
    PushUndoSnapshot();

    Transform transform;
    transform.position = glm::vec3(-2.5f, 3.0f, 1.5f);
    transform.scale = glm::vec3(0.22f);

    Light light;
    light.type = Light::Type::Directional;
    light.color = glm::vec3(1.0f, 0.92f, 0.82f);
    light.intensity = 4.0f;
    light.direction = glm::normalize(glm::vec3(-0.35f, -1.0f, -0.25f));

    CreateObject("DirectionalLight", Primitive::Cube, placeholderMesh, transform, light.color);
    Object& object = m_objects.back();
    object.light = true;
    object.lightData = light;
    m_registry.Add<Light>(object.entity, light);
    m_selectedIndex = static_cast<int>(m_objects.size()) - 1;
    m_dirty = true;
}

void EditorScene::AddPointLight(const engine::Mesh& placeholderMesh) {
    PushUndoSnapshot();

    Transform transform;
    transform.position = glm::vec3(1.8f, 1.6f, 1.4f);
    transform.scale = glm::vec3(0.18f);

    Light light;
    light.type = Light::Type::Point;
    light.color = glm::vec3(0.45f, 0.68f, 1.0f);
    light.intensity = 45.0f;
    light.range = 12.0f;

    CreateObject("PointLight", Primitive::Cube, placeholderMesh, transform, light.color);
    Object& object = m_objects.back();
    object.light = true;
    object.lightData = light;
    m_registry.Add<Light>(object.entity, light);
    m_selectedIndex = static_cast<int>(m_objects.size()) - 1;
    m_dirty = true;
}

void EditorScene::AddSpotLight(const engine::Mesh& placeholderMesh) {
    PushUndoSnapshot();

    Transform transform;
    transform.position = glm::vec3(0.0f, 3.0f, 2.5f);
    transform.scale = glm::vec3(0.2f);

    Light light;
    light.type = Light::Type::Spot;
    light.color = glm::vec3(1.0f, 0.86f, 0.58f);
    light.intensity = 70.0f;
    light.direction = glm::normalize(glm::vec3(0.0f, -1.0f, -0.35f));
    light.innerAngle = 18.0f;
    light.outerAngle = 32.0f;
    light.range = 18.0f;

    CreateObject("SpotLight", Primitive::Cube, placeholderMesh, transform, light.color);
    Object& object = m_objects.back();
    object.light = true;
    object.lightData = light;
    m_registry.Add<Light>(object.entity, light);
    m_selectedIndex = static_cast<int>(m_objects.size()) - 1;
    m_dirty = true;
}

void EditorScene::AddAreaLight(const engine::Mesh& placeholderMesh) {
    PushUndoSnapshot();

    Transform transform;
    transform.position = glm::vec3(-1.6f, 1.8f, 1.2f);
    transform.scale = glm::vec3(0.28f);

    Light light;
    light.type = Light::Type::Area;
    light.color = glm::vec3(1.0f, 0.72f, 0.42f);
    light.intensity = 80.0f;
    light.sourceRadius = 1.2f;

    CreateObject("AreaLight", Primitive::Cube, placeholderMesh, transform, light.color);
    Object& object = m_objects.back();
    object.light = true;
    object.lightData = light;
    m_registry.Add<Light>(object.entity, light);
    m_selectedIndex = static_cast<int>(m_objects.size()) - 1;
    m_dirty = true;
}

bool EditorScene::AddModel(const std::string &path, const engine::Mesh &placeholderMesh, const engine::ecs::Transform &transform)
{
    if (path.empty()) {
        return false;
    }

    PushUndoSnapshot();

    const std::filesystem::path filePath(path);
    std::string name = filePath.stem().string();
    if (name.empty()) {
        name = "Model";
    }

    CreateObject(name, Primitive::Cube, placeholderMesh, transform, glm::vec3(0.78f, 0.78f, 0.82f));
    m_objects.back().modelAssetPath = path;
    m_selectedIndex = static_cast<int>(m_objects.size()) - 1;
    m_dirty = true;
    return true;
}

bool EditorScene::CycleSelectedColor()
{
    const Object* selected = SelectedObject();
    if (!selected)
    {
        return false;
    }
    if (selected->locked) {
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

bool EditorScene::SetSelectedName(const std::string& name) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }

    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked || name.empty() || selected.name == name) {
        return false;
    }

    PushUndoSnapshot();
    selected.name = name;
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedColor(const glm::vec3& color) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }

    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    MeshRenderer* renderer = m_registry.TryGet<MeshRenderer>(selected.entity);
    if (!renderer || selected.locked || renderer->color == color) {
        return false;
    }

    PushUndoSnapshot();
    renderer->color = color;
    if (selected.light) {
        selected.lightData.color = color;
        if (Light* light = m_registry.TryGet<Light>(selected.entity)) {
            light->color = color;
        }
    }
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

bool EditorScene::SetSelectedModelAsset(const std::string &path)
{
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }

    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked) {
        return false;
    }

    PushUndoSnapshot();
    selected.modelAssetPath = path;
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedMaterialAsset(const std::string &path)
{
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }

    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked) {
        return false;
    }

    PushUndoSnapshot();
    selected.materialAssetPath = path;
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedLight(const Light& light) {
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }

    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked || !selected.light) {
        return false;
    }

    PushUndoSnapshot();
    selected.lightData = light;
    if (Light* component = m_registry.TryGet<Light>(selected.entity)) {
        *component = light;
    } else {
        m_registry.Add<Light>(selected.entity, light);
    }
    if (MeshRenderer* renderer = m_registry.TryGet<MeshRenderer>(selected.entity)) {
        renderer->color = light.color;
    }
    m_dirty = true;
    return true;
}

void EditorScene::SetEnvironment(const Environment& environment) {
    PushUndoSnapshot();
    m_environment = environment;
    m_dirty = true;
}

bool EditorScene::SetSelectedLinearVelocityEnabled(bool enabled)
{
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }

    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked || selected.linearVelocityEnabled == enabled) {
        return false;
    }

    PushUndoSnapshot();
    selected.linearVelocityEnabled = enabled;
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedAngularVelocityEnabled(bool enabled)
{ 
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }

    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked || selected.angularVelocityEnabled == enabled) {
        return false;
    }

    PushUndoSnapshot();
    selected.angularVelocityEnabled = enabled;
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedLinearVelocity(const glm::vec3 &velocity)
{
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }

    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked) {
        return false;
    }

    PushUndoSnapshot();
    selected.linearVelocity = velocity;
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedAngularVelocity(const glm::vec3 &axis, float radiansPerSecond)
{
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }

    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked) {
        return false;
    }

    PushUndoSnapshot();
    selected.angularVelocityAxis = axis;
    selected.angularVelocityRadians = radiansPerSecond;
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedRigidBodyEnabled(bool enabled)
{
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }

    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked || selected.rigidBodyEnabled == enabled) {
        return false;
    }

    PushUndoSnapshot();
    selected.rigidBodyEnabled = enabled;
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedRigidBody(const engine::ecs::RigidBody &rigidBody)
{
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }

    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked) {
        return false;
    }

    PushUndoSnapshot();
    selected.rigidBodyEnabled = true;
    selected.rigidBody = rigidBody;
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedColliderEnabled(bool enabled)
{
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }

    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked || selected.colliderEnabled == enabled) {
        return false;
    }

    PushUndoSnapshot();
    selected.colliderEnabled = enabled;
    if (enabled) {
        const Transform* transform = m_registry.TryGet<Transform>(selected.entity);
        if (selected.primitive == Primitive::Plane && selected.modelAssetPath.empty()) {
            selected.collider = Collider::MakePlane(glm::vec3(0.0f, 1.0f, 0.0f),
                transform ? transform->position.y : 0.0f);
        } else if (transform) {
            selected.collider = Collider::MakeBox(glm::vec3(
                std::max(transform->scale.x * 0.5f, 0.001f),
                std::max(transform->scale.y * 0.5f, 0.001f),
                std::max(transform->scale.z * 0.5f, 0.001f)));
        }
    }
    m_dirty = true;
    return true;
}

bool EditorScene::SetSelectedCollider(const engine::ecs::Collider &collider)
{
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }

    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    if (selected.locked) {
        return false;
    }

    PushUndoSnapshot();
    selected.colliderEnabled = true;
    selected.collider = collider;
    m_dirty = true;
    return true;
}

bool EditorScene::ToggleSelectVisible()
{
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }

    PushUndoSnapshot();
    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    selected.visible = !selected.visible;
    m_dirty = true;
    return true;
}

bool EditorScene::ToggleSelectedLocked()
{
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }

    PushUndoSnapshot();
    Object& selected = m_objects[static_cast<std::size_t>(m_selectedIndex)];
    selected.locked = !selected.locked;
    m_dirty = true;
    return true;
}

bool EditorScene::DuplicateSelected(const engine::Mesh & cube, const engine::Mesh & plane, const engine::Mesh& sphere)
{
    const Object* selected = SelectedObject();
    if (!selected) {
        return false;
    }

    const Object selectedCopy = *selected;
    const Transform* transform = m_registry.TryGet<Transform>(selected->entity);
    const MeshRenderer* renderer = m_registry.TryGet<MeshRenderer>(selected->entity);
    if (!transform || !renderer || selectedCopy.locked) {
        return false;
    }

    Transform duplicateTransform = *transform;
    const glm::vec3 duplicateColor = renderer->color;

    PushUndoSnapshot();

    duplicateTransform.position += glm::vec3(0.8f, 0.0f, 0.8f);

    const engine::Mesh& mesh = MeshFor(selectedCopy.primitive, cube, plane, sphere);
    CreateObject(selectedCopy.name + "_Copy", selectedCopy.primitive, mesh, duplicateTransform, duplicateColor);
    m_objects.back().modelAssetPath = selectedCopy.modelAssetPath;
    m_objects.back().materialAssetPath = selectedCopy.materialAssetPath;
    m_objects.back().light = selectedCopy.light;
    m_objects.back().lightData = selectedCopy.lightData;
    if (selectedCopy.light) {
        m_registry.Add<Light>(m_objects.back().entity, selectedCopy.lightData);
    }
    m_objects.back().linearVelocity = selectedCopy.linearVelocity;
    m_objects.back().angularVelocityAxis = selectedCopy.angularVelocityAxis;
    m_objects.back().angularVelocityRadians = selectedCopy.angularVelocityRadians;
    m_selectedIndex = static_cast<int>(m_objects.size()) - 1;
    m_dirty = true;
    return true;
}

bool EditorScene::DeleteSelected()
{
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_objects.size())) {
        return false;
    }
    if (m_objects[static_cast<std::size_t>(m_selectedIndex)].locked) {
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

void EditorScene::RestoreSnapshot(const Snapshot & snapshot, const engine::Mesh & cube, const engine::Mesh & plane, const engine::Mesh& sphere)
{
    m_registry = engine::ecs::Registry{};
    m_objects.clear();

    for (const ObjectSnapshot& objectSnapshot : snapshot.objects) {
        Entity entity = m_registry.Create();
        const engine::Mesh& mesh = MeshFor(objectSnapshot.object.primitive, cube, plane, sphere);
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
