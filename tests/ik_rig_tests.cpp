#include <engine/animation/Animator.h>
#include <engine/assets/AssetRegistry.h>
#include <engine/assets/IKRigAsset.h>

#include <cmath>
#include <filesystem>
#include <iostream>

namespace {
int failures = 0;
void Check(bool value, const char* message) {
    if (!value) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
bool Near(float a, float b, float epsilon = 0.002f) { return std::abs(a - b) <= epsilon; }
}

int main() {
    engine::IKRigAssetData asset;
    asset.header.id = engine::AssetHandle::Generate();
    asset.name = "WizardHumanoidIK";
    asset.skeletonId = engine::AssetHandle::Generate();
    asset.skeletonPath = "GameAssets/Skeletons/Wizard.3dgskel";
    asset.feet.enabled = true; asset.feet.pelvisBone = "Hips";
    asset.feet.leftUpperBone = "LeftUpLeg"; asset.feet.leftMidBone = "LeftLeg";
    asset.feet.leftFootBone = "LeftFoot"; asset.feet.rightUpperBone = "RightUpLeg";
    asset.feet.rightMidBone = "RightLeg"; asset.feet.rightFootBone = "RightFoot";
    engine::IKGoalDefinition hand;
    hand.name = "StaffHand"; hand.rootBone = "RightArm"; hand.midBone = "RightForeArm";
    hand.endBone = "RightHand"; hand.weight = 2.0f; hand.interpolationSpeed = -3.0f;
    asset.goals.push_back(hand);
    engine::IKGoalDefinition look;
    look.name = "LookAt"; look.type = engine::IKGoalType::LookAt; look.rootBone = "Head";
    look.maxAngleDegrees = 240.0f; asset.goals.push_back(look);
    engine::NormalizeIKRigAsset(asset);
    Check(Near(asset.goals[0].weight, 1.0f) && Near(asset.goals[0].interpolationSpeed, 0.0f),
          "goal values normalize");
    Check(Near(asset.goals[1].maxAngleDegrees, 180.0f), "look angle clamps");
    std::string error;
    Check(engine::ValidateIKRigAsset(asset, &error), "valid IK rig");

    const auto root = std::filesystem::temp_directory_path() / "3dg_ik_rig_test";
    std::filesystem::create_directories(root);
    const auto path = root / "Wizard.3dgikrig";
    Check(engine::SaveIKRigAsset(path.string(), asset, &error), "save IK rig");
    engine::IKRigAssetData loaded;
    Check(engine::LoadIKRigAsset(path.string(), &loaded, &error), "load IK rig");
    Check(loaded.header.id == asset.header.id && loaded.header.dependencies.size() == 1,
          "identity and skeleton dependency round trip");
    Check(loaded.goals.size() == 2 && loaded.goals[0].endBone == "RightHand",
          "IK goals round trip");
    engine::AssetRegistry registry;
    Check(registry.RebuildFromContent(root.string(), &error), "scan IK rig registry");
    const auto* entry = registry.Find(asset.header.id);
    Check(entry && entry->type == engine::AssetType::IKRig, "IK rig registry type");

    const glm::vec3 rootJoint(0.0f, 0.0f, 0.0f), midJoint(1.0f, 0.0f, 0.0f);
    const glm::vec3 endJoint(2.0f, 0.0f, 0.0f), target(1.0f, 1.0f, 0.0f);
    const auto solved = engine::Animator::SolveTwoBoneIK(rootJoint, midJoint, endJoint,
                                                         target, glm::vec3(0.0f, 0.0f, 1.0f));
    const glm::vec3 solvedMid = rootJoint + solved.upper * (midJoint - rootJoint);
    const glm::vec3 solvedEnd = solvedMid + solved.lower * (solved.upper * (endJoint - midJoint));
    Check(glm::distance(solvedEnd, target) < 0.02f, "two-bone solver reaches target");

    std::filesystem::remove_all(root);
    if (failures) return 1;
    std::cout << "IK rig tests passed\n";
    return 0;
}
