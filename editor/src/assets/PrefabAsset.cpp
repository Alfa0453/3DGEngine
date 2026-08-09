#include "PrefabAsset.h"

#include <fstream>
#include <iomanip>
#include <sstream>

// A prefab is stored as its own compact text asset (independent of the scene format):
//   3DG_PREFAB <version>
//   <name>
//   <curated component configuration>
// It persists the reusable, authorable component fields. Runtime-only fields (solver
// state, phases, origins) are intentionally not written; they reset on apply.

namespace {
std::string OrDash(const std::string& value) {
    return value.empty() ? std::string("-") : value;
}
void Undash(std::string& value) {
    if (value == "-") value.clear();
}
std::ostream& WriteVec3(std::ostream& out, const glm::vec3& v) {
    out << v.x << ' ' << v.y << ' ' << v.z;
    return out;
}
std::istream& ReadVec3(std::istream& in, glm::vec3& v) {
    in >> v.x >> v.y >> v.z;
    return in;
}
}  // namespace

bool PrefabAsset::Apply(EditorScene& scene) const {
    const EditorScene::Object* selected = scene.SelectedObject();
    if (!selected || scene.SelectedLocked()) return false;
    const EditorScene::Object& o = object;

    // Component config only, via setters so the ECS mirrors update. The world Transform
    // (position/rotation/scale) is intentionally never touched, so instances keep it.
    scene.SetSelectedModelAsset(o.modelAssetPath, o.modelAssetId);
    scene.SetSelectedMaterialAsset(o.materialAssetPath, o.materialAssetId);
    scene.SetSelectedModelOffset(o.modelOffsetPosition, o.modelOrientationEuler,
                                 o.modelOffsetScale);

    scene.SetSelectedColliderEnabled(o.colliderEnabled);
    if (o.colliderEnabled) scene.SetSelectedCollider(o.collider);

    scene.SetSelectedRigidBodyEnabled(o.rigidBodyEnabled);
    if (o.rigidBodyEnabled) scene.SetSelectedRigidBody(o.rigidBody);

    scene.SetSelectedRotatorEnabled(o.rotatorEnabled);
    if (o.rotatorEnabled) scene.SetSelectedRotator(o.rotator);

    scene.SetSelectedMoverEnabled(o.moverEnabled);
    if (o.moverEnabled) scene.SetSelectedMover(o.mover);

    scene.SetSelectedLinearVelocityEnabled(o.linearVelocityEnabled);
    if (o.linearVelocityEnabled) scene.SetSelectedLinearVelocity(o.linearVelocity);
    scene.SetSelectedAngularVelocityEnabled(o.angularVelocityEnabled);
    if (o.angularVelocityEnabled) {
        scene.SetSelectedAngularVelocity(o.angularVelocityAxis, o.angularVelocityRadians);
    }

    scene.SetSelectedHealthEnabled(o.healthEnabled);
    if (o.healthEnabled) scene.SetSelectedHealth(o.health);

    scene.SetSelectedScript(o.scriptClassName, o.scriptPath, o.scriptEnabled);
    scene.SetSelectedScriptScheduling(o.scriptExecutionOrder, o.scriptDependencies);
    scene.SetSelectedAdditionalScripts(o.additionalScripts);
    return true;
}

bool PrefabAsset::Save(const std::string& path, std::string* error) const {
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        if (error) *error = "Could not open prefab for writing: " + path;
        return false;
    }

    const EditorScene::Object& o = object;
    out << "3DG_PREFAB " << version << '\n';
    out << std::quoted(name) << '\n';
    out << static_cast<int>(o.primitive) << '\n';
    out << std::quoted(OrDash(o.modelAssetPath)) << ' '
        << std::quoted(OrDash(o.materialAssetPath)) << '\n';

    WriteVec3(out, o.modelOffsetPosition) << ' ';
    WriteVec3(out, o.modelOrientationEuler) << ' ';
    WriteVec3(out, o.modelOffsetScale) << '\n';

    out << o.linearVelocityEnabled << ' ';
    WriteVec3(out, o.linearVelocity) << ' ';
    out << o.angularVelocityEnabled << ' ';
    WriteVec3(out, o.angularVelocityAxis) << ' ' << o.angularVelocityRadians << '\n';

    out << o.rotatorEnabled << ' ';
    WriteVec3(out, o.rotator.axis) << ' ' << o.rotator.radiansPerSecond << '\n';

    out << o.moverEnabled << ' ';
    WriteVec3(out, o.mover.axis) << ' ' << o.mover.distance << ' ' << o.mover.speed << '\n';

    out << o.colliderEnabled << ' ' << static_cast<int>(o.collider.shape) << ' '
        << o.collider.radius << ' ';
    WriteVec3(out, o.collider.halfExtents) << ' ';
    WriteVec3(out, o.collider.planeNormal) << ' ';
    out << o.collider.planeOffset << ' ' << o.collider.halfHeight << ' '
        << o.collider.majorRadius << ' ' << o.collider.minorRadius << ' '
        << o.collider.steps << ' ' << o.collider.restitution << ' '
        << o.collider.friction << ' ' << o.collider.isTrigger << ' '
        << o.collider.layer << ' ' << o.collider.mask << '\n';

    out << o.rigidBodyEnabled << ' ' << o.rigidBody.invMass << ' '
        << o.rigidBody.useGravity << ' ' << o.rigidBody.kinematic << ' '
        << o.rigidBody.allowSleep << ' ' << o.rigidBody.linearDamping << ' '
        << o.rigidBody.angularDamping << ' ' << o.rigidBody.ccd << ' ';
    WriteVec3(out, o.rigidBody.velocity) << '\n';

    out << o.healthEnabled << ' ' << o.health.hp << ' ' << o.health.maxHp << ' '
        << o.health.alive << '\n';

    out << o.scriptEnabled << ' ' << std::quoted(OrDash(o.scriptClassName)) << ' '
        << std::quoted(OrDash(o.scriptPath)) << ' '
        << o.scriptExecutionOrder << ' ' << o.scriptDependencies.size();
    for (const std::string& dependency : o.scriptDependencies)
        out << ' ' << std::quoted(OrDash(dependency));
    out << '\n';

    return static_cast<bool>(out);
}

bool PrefabAsset::Load(const std::string& path, std::string* error) {
    std::ifstream in(path);
    if (!in) {
        if (error) *error = "Could not open prefab: " + path;
        return false;
    }

    std::string magic;
    int loadedVersion = 0;
    in >> magic >> loadedVersion;
    if (magic != "3DG_PREFAB") {
        if (error) *error = "Not a prefab file: " + path;
        return false;
    }
    if (loadedVersion < 1 || loadedVersion > version) {
        if (error) *error = "Unsupported prefab version (expected 1.." +
            std::to_string(version) + "): " + path;
        return false;
    }

    *this = PrefabAsset{};   // reset to defaults, then overlay persisted fields
    version = loadedVersion;

    EditorScene::Object& o = object;
    in >> std::quoted(name);

    int primitive = 0;
    in >> primitive;
    o.primitive = static_cast<EditorScene::Primitive>(primitive);

    in >> std::quoted(o.modelAssetPath) >> std::quoted(o.materialAssetPath);
    Undash(o.modelAssetPath);
    Undash(o.materialAssetPath);

    ReadVec3(in, o.modelOffsetPosition);
    ReadVec3(in, o.modelOrientationEuler);
    ReadVec3(in, o.modelOffsetScale);

    in >> o.linearVelocityEnabled;
    ReadVec3(in, o.linearVelocity);
    in >> o.angularVelocityEnabled;
    ReadVec3(in, o.angularVelocityAxis);
    in >> o.angularVelocityRadians;

    in >> o.rotatorEnabled;
    ReadVec3(in, o.rotator.axis);
    in >> o.rotator.radiansPerSecond;

    in >> o.moverEnabled;
    ReadVec3(in, o.mover.axis);
    in >> o.mover.distance >> o.mover.speed;

    int shape = 0;
    in >> o.colliderEnabled >> shape >> o.collider.radius;
    o.collider.shape = static_cast<engine::ecs::ColliderShape>(shape);
    ReadVec3(in, o.collider.halfExtents);
    ReadVec3(in, o.collider.planeNormal);
    in >> o.collider.planeOffset >> o.collider.halfHeight >> o.collider.majorRadius
       >> o.collider.minorRadius >> o.collider.steps >> o.collider.restitution
       >> o.collider.friction >> o.collider.isTrigger >> o.collider.layer
       >> o.collider.mask;

    in >> o.rigidBodyEnabled >> o.rigidBody.invMass >> o.rigidBody.useGravity
       >> o.rigidBody.kinematic >> o.rigidBody.allowSleep >> o.rigidBody.linearDamping
       >> o.rigidBody.angularDamping >> o.rigidBody.ccd;
    ReadVec3(in, o.rigidBody.velocity);

    in >> o.healthEnabled >> o.health.hp >> o.health.maxHp >> o.health.alive;

    in >> o.scriptEnabled >> std::quoted(o.scriptClassName) >> std::quoted(o.scriptPath);
    Undash(o.scriptClassName);
    Undash(o.scriptPath);
    if (loadedVersion >= 2) {
        std::size_t dependencyCount = 0;
        in >> o.scriptExecutionOrder >> dependencyCount;
        for (std::size_t i = 0; i < dependencyCount; ++i) {
            std::string dependency;
            in >> std::quoted(dependency);
            Undash(dependency);
            if (!dependency.empty()) o.scriptDependencies.push_back(std::move(dependency));
        }
    }

    if (in.fail()) {
        if (error) *error = "Prefab file is malformed: " + path;
        return false;
    }
    return true;
}
