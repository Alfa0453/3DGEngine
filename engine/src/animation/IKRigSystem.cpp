#include "engine/animation/IKRigSystem.h"

#include "engine/animation/AnimatedModel.h"
#include "engine/core/Paths.h"
#include "engine/ecs/Registry.h"
#include "engine/graphics/SkinnedModel.h"

#include <algorithm>
#include <filesystem>

namespace engine {
namespace {
std::string Resolve(const std::string& path) {
    const std::filesystem::path input(path);
    if (std::filesystem::exists(input)) return input.string();
    const std::filesystem::path executable(ExecutableDir());
    for (const auto& candidate : {executable / input, executable / "Content" / input,
                                  executable.parent_path() / input})
        if (std::filesystem::exists(candidate)) return candidate.string();
    return path;
}
}

bool ConfigureIKRig(ecs::Registry& registry, ecs::Entity entity,
                    const std::string& path, std::string* error) {
    IKRigAssetData asset;
    if (!LoadIKRigAsset(Resolve(path), &asset, error)) return false;
    return ConfigureIKRig(registry, entity, asset, path, error);
}

bool ConfigureIKRig(ecs::Registry& registry, ecs::Entity entity,
                    const IKRigAssetData& source, const std::string& path,
                    std::string* error) {
    auto* animated = registry.TryGet<AnimatedModel>(entity);
    if (!animated || !animated->model) {
        if (error) *error = "IK rig target has no loaded animated model.";
        return false;
    }
    IKRigAssetData asset = source; NormalizeIKRigAsset(asset);
    if (!ValidateIKRigAsset(asset, error)) return false;
    const Skeleton& skeleton = animated->model->GetSkeleton();
    IKRigRuntime runtime; runtime.assetPath = path; runtime.assetId = asset.header.id;
    for (const auto& definition : asset.goals) {
        IKGoalRuntime goal; goal.definition = definition;
        goal.root = skeleton.Find(definition.rootBone);
        goal.mid = definition.midBone.empty() ? -1 : skeleton.Find(definition.midBone);
        goal.end = definition.endBone.empty() ? -1 : skeleton.Find(definition.endBone);
        if (goal.root < 0 || (definition.type == IKGoalType::TwoBone
            && (goal.mid < 0 || goal.end < 0))) {
            if (error) *error = "IK goal '" + definition.name + "' references a bone missing from the model.";
            return false;
        }
        goal.runtimeWeight = definition.weight;
        runtime.goals.push_back(std::move(goal));
    }
    animated->ikRig = std::move(runtime);
    const auto& feet = asset.feet;
    animated->footIK.enabled = feet.enabled;
    animated->footIK.pelvis = skeleton.Find(feet.pelvisBone);
    animated->footIK.left = {skeleton.Find(feet.leftUpperBone), skeleton.Find(feet.leftMidBone),
                             skeleton.Find(feet.leftFootBone)};
    animated->footIK.right = {skeleton.Find(feet.rightUpperBone), skeleton.Find(feet.rightMidBone),
                              skeleton.Find(feet.rightFootBone)};
    animated->footIK.detected = animated->footIK.left.Valid() && animated->footIK.right.Valid();
    animated->footIK.traceUp = feet.traceUp; animated->footIK.traceDown = feet.traceDown;
    animated->footIK.footHeight = feet.footHeight; animated->footIK.pelvisWeight = feet.pelvisWeight;
    animated->footIK.maxPelvisDrop = feet.maxPelvisDrop; animated->footIK.weight = feet.weight;
    return true;
}

bool SetIKTarget(ecs::Registry& registry, ecs::Entity entity, const std::string& name,
                 const glm::vec3& target, float weight) {
    auto* animated = registry.TryGet<AnimatedModel>(entity); if (!animated) return false;
    for (auto& goal : animated->ikRig.goals) if (goal.definition.name == name) {
        goal.targetWorld = target; goal.targetSet = true;
        goal.runtimeWeight = std::clamp(weight, 0.0f, 1.0f); return true;
    }
    return false;
}

bool ClearIKTarget(ecs::Registry& registry, ecs::Entity entity, const std::string& name) {
    auto* animated = registry.TryGet<AnimatedModel>(entity); if (!animated) return false;
    for (auto& goal : animated->ikRig.goals) if (goal.definition.name == name) {
        goal.targetSet = false; goal.smoothingInitialized = false; return true;
    }
    return false;
}

bool SetIKGoalWeight(ecs::Registry& registry, ecs::Entity entity,
                     const std::string& name, float weight) {
    auto* animated = registry.TryGet<AnimatedModel>(entity); if (!animated) return false;
    for (auto& goal : animated->ikRig.goals) if (goal.definition.name == name) {
        goal.runtimeWeight = std::clamp(weight, 0.0f, 1.0f); return true;
    }
    return false;
}

bool HasIKGoal(const ecs::Registry& registry, ecs::Entity entity, const std::string& name) {
    const auto* animated = registry.TryGet<AnimatedModel>(entity); if (!animated) return false;
    return std::any_of(animated->ikRig.goals.begin(), animated->ikRig.goals.end(),
        [&](const IKGoalRuntime& goal) { return goal.definition.name == name; });
}

} // namespace engine
