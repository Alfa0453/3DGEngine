#include "ProceduralBuildingPanel.h"
#include "engine/physics/ColliderTransform.h"

#include <cstdlib>
#include <iostream>
#include <cmath>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}
void RequireNear(float actual, float expected, const char* message) {
    Require(std::abs(actual - expected) < 0.0001f, message);
}
}

int main() {
    ProceduralBuildingPanel panel;
    panel.SetFootprint({{-4.0f, -3.0f}, {4.0f, -3.0f},
                        {4.0f, 3.0f}, {-4.0f, 3.0f}});
    panel.SetStoreys(1);
    panel.SetOpenings({});
    auto parts = panel.GenerateParts();
    Require(parts.size() == 7, "one-storey shell should contain floor, ceiling, four walls, roof");

    ProceduralBuildingPanel::Opening door;
    door.segment = 0;
    door.storey = 0;
    door.width = 1.2f;
    door.height = 2.2f;
    panel.SetOpenings({door});
    parts = panel.GenerateParts();
    Require(parts.size() == 9, "door must split one wall into left, right, and lintel pieces");

    panel.SetOpenings({});
    panel.SetStoreys(2);
    parts = panel.GenerateParts();
    Require(parts.size() == 13, "two-storey shell should regenerate deterministic geometry");
    Require(parts.back().suffix == "Roof", "roof should remain the final generated surface");

    panel.SetFootprint({{0.0f, 0.0f}, {0.01f, 0.0f}, {0.0f, 0.01f}});
    Require(panel.GenerateParts().empty(), "degenerate footprints must not generate geometry");

    // Regression: local collider dimensions are scaled exactly once and the
    // collider offset follows the owner's full rotation, not yaw alone.
    engine::ecs::Transform owner;
    owner.position = {10.0f, 2.0f, -4.0f};
    owner.scale = {4.0f, 2.0f, 6.0f};
    owner.rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 1, 0));
    engine::ecs::Collider local;
    local.shape = engine::ecs::ColliderShape::Box;
    local.halfExtents = {0.5f, 0.5f, 0.5f};
    local.localPosition = {1.0f, 0.0f, 0.0f};
    const auto world = engine::physics::BuildWorldCollider(owner, local);
    RequireNear(world.collider.halfExtents.x, 2.0f, "box X extent must inherit scale once");
    RequireNear(world.collider.halfExtents.y, 1.0f, "box Y extent must inherit scale once");
    RequireNear(world.collider.halfExtents.z, 3.0f, "box Z extent must inherit scale once");
    RequireNear(world.transform.scale.x, 1.0f, "scaled primitive must keep a unit query transform");
    RequireNear(world.transform.position.x, 10.0f, "rotated local offset X mismatch");
    RequireNear(world.transform.position.z, -8.0f, "rotated local offset Z mismatch");

    local.shape = engine::ecs::ColliderShape::Sphere;
    local.radius = 0.5f;
    const auto sphereWorld = engine::physics::BuildWorldCollider(owner, local);
    RequireNear(sphereWorld.collider.radius, 3.0f,
                "sphere must conservatively use the largest scale axis");

    local = {};
    local.shape = engine::ecs::ColliderShape::Capsule;
    local.radius = 0.5f;
    local.halfHeight = 1.0f;
    local.localScale = {0.5f, 2.0f, 0.25f};
    const auto capsuleWorld = engine::physics::BuildWorldCollider(owner, local);
    RequireNear(capsuleWorld.collider.radius, 1.0f,
                "capsule radius must use the largest local X/Z scale");
    RequireNear(capsuleWorld.collider.halfHeight, 4.0f,
                "capsule half height must use local and owner Y scale");

    owner.scale = {-3.0f, 2.0f, -0.5f};
    local = engine::ecs::Collider::MakeBox(glm::vec3(0.5f));
    const auto mirrored = engine::physics::BuildWorldCollider(owner, local);
    RequireNear(mirrored.collider.halfExtents.x, 1.5f,
                "negative scale must produce positive X dimensions");
    RequireNear(mirrored.collider.halfExtents.z, 0.25f,
                "negative scale must produce positive Z dimensions");

    local.shape = engine::ecs::ColliderShape::TriangleMesh;
    local.collisionAssetPath = "test.3dgmesh";
    const auto meshWorld = engine::physics::BuildWorldCollider(owner, local);
    RequireNear(meshWorld.transform.scale.x, -3.0f,
                "mesh collision must preserve signed scale for mirrored transforms");
    std::cout << "Procedural building tests passed\n";
    return 0;
}
