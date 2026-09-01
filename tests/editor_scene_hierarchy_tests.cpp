#include "EditorScene.h"
#include "SceneApplyTarget.h"
#include <engine/graphics/Mesh.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAILED: " << message << '\n'; std::exit(1); }
}
}

int main() {
    engine::Mesh mesh;
    EditorScene scene;
    scene.BuildDefault(mesh, mesh, mesh, mesh, mesh, mesh, mesh, mesh, mesh);
    scene.AddCube(mesh);
    scene.AddCube(mesh);
    Require(scene.Objects().size() == 3, "objects should be created");
    Require(scene.Objects()[1].name != scene.Objects()[2].name,
            "created object names must be globally unique");

    const auto environment = scene.CreateGroup("Environment");
    const auto environment2 = scene.CreateGroup("Environment");
    Require(environment != environment2, "group IDs must be stable and unique");
    Require(scene.Groups()[0].name == "Environment", "first group keeps requested name");
    Require(scene.Groups()[1].name == "Environment_1", "duplicate group is auto-suffixed");
    Require(!scene.RenameGroup(environment2, "Environment"),
            "manual rename collision must be rejected");

    const auto building = scene.CreateGroup("Building", environment);
    Require(!scene.MoveGroupToGroup(environment, building), "group cycles must be rejected");
    Require(scene.MoveObjectToGroup(1, building), "object should move into nested group");
    Require(scene.GroupObjectCount(environment, true) == 1,
            "recursive group count should include nested objects");

    Require(scene.DeleteGroup(building), "group deletion should succeed");
    Require(scene.Objects()[1].editorGroupId == environment,
            "deleting a group must reparent objects instead of deleting them");

    scene.SelectIndex(2);
    SceneApplyTarget applyTarget;
    applyTarget.Set(scene.SelectedObject()->entity);
    Require(scene.SetSelectedName("RenamedCube"), "target object should rename");
    Require(applyTarget.Resolve(scene) != nullptr
            && applyTarget.Resolve(scene)->name == "RenamedCube",
            "apply targets must survive object renames");
    Require(scene.DeleteSelected(), "target object should delete");
    Require(applyTarget.Resolve(scene) == nullptr,
            "deleted apply targets must resolve as unavailable");

    scene.SelectIndex(1);
    engine::ecs::Collider torso = engine::ecs::Collider::MakeBox(
        glm::vec3(0.35f, 0.6f, 0.2f));
    torso.localPosition = glm::vec3(0.0f, 0.65f, 0.0f);
    engine::ecs::Collider head = engine::ecs::Collider::MakeSphere(0.24f);
    head.localPosition = glm::vec3(0.0f, 1.45f, 0.0f);
    Require(scene.SetSelectedColliders({torso, head}),
            "scene object should accept an authored collider set");
    const engine::AssetHandle ikRigId = engine::AssetHandle::Generate();
    Require(scene.SetSelectedIKRig("GameAssets/Animation/IKRigs/Wizard.3dgikrig", ikRigId),
            "scene object should accept an authored IK rig reference");

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "3dg_hierarchy_groups.scene";
    std::string error;
    Require(scene.Save(path.string(), &error), "grouped scene should save");
    EditorScene loaded;
    Require(loaded.Load(path.string(), mesh, mesh, mesh, mesh, mesh, mesh, mesh, mesh, mesh, &error),
            "grouped scene should reload");
    Require(loaded.Groups().size() == scene.Groups().size(), "groups must persist");
    Require(loaded.Objects()[1].editorGroupId == environment,
            "object group membership must persist");
    Require(loaded.Objects()[1].colliderEnabled
            && loaded.Objects()[1].collider.shape == engine::ecs::ColliderShape::Box
            && loaded.Objects()[1].additionalColliders.size() == 1
            && loaded.Objects()[1].additionalColliders[0].shape
                == engine::ecs::ColliderShape::Sphere
            && loaded.Objects()[1].additionalColliders[0].localPosition
                == head.localPosition,
            "compound colliders must persist with the scene object");
    Require(loaded.Objects()[1].ikRigPath
                == "GameAssets/Animation/IKRigs/Wizard.3dgikrig"
            && loaded.Objects()[1].ikRigAssetId == ikRigId,
            "IK rig path and stable identity must persist with the scene object");
    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::cout << "Editor scene hierarchy tests passed\n";
    return 0;
}
