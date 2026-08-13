#include <engine/assets/RagdollAsset.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
int failures = 0;
void Check(bool value, const char* message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
bool Near(float a, float b) { return std::fabs(a - b) < 0.0001f; }
}

int main() {
    const std::filesystem::path root = std::filesystem::temp_directory_path()
        / "3dg_ragdoll_asset_tests";
    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    const std::filesystem::path path = root / "Wizard.3dgragdoll";

    engine::RagdollAssetData source;
    source.name = "Wizard Ragdoll";
    source.skeletonPath = "GameAssets/Meshes/Wizard.3dgskmesh";
    source.totalMass = 72.0f;
    source.blendInDuration = 0.22f;
    source.blendOutDuration = 0.4f;
    engine::RagdollBodyDefinition body;
    body.boneName = "spine_01";
    body.shape = engine::RagdollBodyShape::Box;
    body.localPosition = {0.1f, 0.2f, 0.3f};
    body.halfExtents = {0.2f, 0.3f, 0.1f};
    body.massWeight = 2.0f;
    source.bodies.push_back(body);
    body.boneName = "head";
    body.shape = engine::RagdollBodyShape::Sphere;
    body.radius = 0.16f;
    source.bodies.push_back(body);
    engine::RagdollConstraintDefinition joint;
    joint.parentBoneName = "spine_01";
    joint.childBoneName = "head";
    joint.type = engine::RagdollJointType::Hinge;
    joint.twistMinDegrees = -20.0f;
    joint.twistMaxDegrees = 35.0f;
    source.constraints.push_back(joint);

    std::string error;
    Check(engine::SaveRagdollAsset(path.string(), source, &error),
          "ragdoll asset saves");
    engine::RagdollAssetData loaded;
    Check(engine::LoadRagdollAsset(path.string(), &loaded, &error),
          "ragdoll asset reloads");
    Check(loaded.name == source.name && loaded.skeletonPath == source.skeletonPath,
          "ragdoll identity round-trips");
    Check(loaded.bodies.size() == 2 && loaded.constraints.size() == 1,
          "body and constraint counts round-trip");
    Check(loaded.bodies[0].shape == engine::RagdollBodyShape::Box
          && Near(loaded.bodies[0].halfExtents.y, 0.3f),
          "body shape data round-trips");
    Check(loaded.constraints[0].type == engine::RagdollJointType::Hinge
          && Near(loaded.constraints[0].twistMaxDegrees, 35.0f),
          "joint limit data round-trips");

    engine::Ragdoll component;
    engine::ApplyRagdollAsset(loaded, &component);
    Check(component.bodies.size() == 2 && component.maxBodies == 2,
          "asset applies to runtime component");
    Check(Near(component.totalMass, 72.0f), "asset applies global physics settings");
    Check(Near(component.blendOutDuration, 0.4f), "blend settings reach runtime component");

    const std::filesystem::path invalidPath = root / "Invalid.3dgragdoll";
    { std::ofstream invalid(invalidPath); invalid << "not a ragdoll\n"; }
    Check(!engine::LoadRagdollAsset(invalidPath.string(), &loaded, &error),
          "invalid ragdoll asset is rejected");

    std::filesystem::remove_all(root, ec);
    if (failures == 0) std::cout << "ragdoll asset tests passed\n";
    return failures == 0 ? 0 : 1;
}
