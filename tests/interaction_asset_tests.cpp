#include <engine/assets/AssetRegistry.h>
#include <engine/assets/InteractionAsset.h>
#include <engine/ecs/Components.h>
#include <engine/ecs/Registry.h>
#include <engine/gameplay/InteractionSystem.h>

#include <cmath>
#include <filesystem>
#include <iostream>

namespace {
int failures = 0;
void Check(bool value, const char* message) { if (!value) { ++failures; std::cerr << "FAIL: " << message << '\n'; } }
bool Near(float a, float b, float epsilon = 0.001f) { return std::abs(a - b) <= epsilon; }
}

int main() {
    engine::InteractionAssetData asset;
    asset.header.id = engine::AssetHandle::Generate();
    asset.name = "VaultDoor";
    asset.motion = engine::InteractionMotionType::SlidingDoor;
    asset.localTranslation = {3.0f, 0.0f, 0.0f};
    asset.openDuration = 1.0f; asset.closeDuration = 0.5f;
    asset.autoClose = false; asset.locked = true; asset.accessTag = "BlueKey";
    asset.inputMode = engine::InteractionInputMode::Hold; asset.holdDuration = 0.4f;
    asset.facingAngleDegrees = 45.0f; asset.requireFacing = true; asset.requireLineOfSight = true;
    asset.requiredConditionTags = {"PowerOn", "QuestReady"};
    asset.interactorAnimationPath = "Animations/PullLever.3dgclip";
    asset.waitForAnimationEvent = true; asset.animationCommitEvent = "LeverCommit";
    asset.openAudioId = engine::AssetHandle::Generate();
    engine::NormalizeInteractionAsset(asset);
    std::string error;
    Check(engine::ValidateInteractionAsset(asset, &error), "valid interaction asset");
    Check(Near(engine::EvaluateInteractionEasing(engine::InteractionEasing::SmoothStep, 0.5f), 0.5f), "smooth easing midpoint");

    const auto root = std::filesystem::temp_directory_path() / "3dg_interaction_test";
    std::filesystem::create_directories(root);
    const auto path = root / "VaultDoor.3dginteraction";
    Check(engine::SaveInteractionAsset(path.string(), asset, &error), "save interaction asset");
    engine::InteractionAssetData loaded;
    Check(engine::LoadInteractionAsset(path.string(), &loaded, &error), "load interaction asset");
    Check(loaded.header.id == asset.header.id && loaded.header.dependencies.size() == 1, "identity and dependencies round trip");
    Check(loaded.motion == engine::InteractionMotionType::SlidingDoor && Near(loaded.localTranslation.x, 3.0f), "motion round trip");
    Check(loaded.inputMode == engine::InteractionInputMode::Hold && Near(loaded.holdDuration, 0.4f), "input contract round trip");
    Check(loaded.requiredConditionTags.size() == 2 && loaded.waitForAnimationEvent, "conditions and animation requirements round trip");
    engine::AssetRegistry assets;
    Check(assets.RebuildFromContent(root.string(), &error), "scan interaction registry");
    const auto* entry = assets.Find(asset.header.id);
    Check(entry && entry->type == engine::AssetType::Interaction, "interaction registry type");

    engine::ecs::Registry registry;
    const auto door = registry.Create();
    registry.Add<engine::ecs::Transform>(door, {});
    Check(engine::ConfigureInteractiveMotion(registry, door, loaded, path.string()), "configure runtime interaction");
    Check(!engine::OpenInteraction(registry, door), "locked interaction rejects missing access tag");
    Check(engine::OpenInteraction(registry, door, "BlueKey"), "matching access tag opens interaction");
    engine::UpdateInteractions(registry, 0.5f);
    const auto* transform = registry.TryGet<engine::ecs::Transform>(door);
    Check(transform && Near(transform->position.x, 1.5f), "opening samples eased transform");
    engine::UpdateInteractions(registry, 0.5f);
    Check(engine::GetInteractionState(registry, door) == engine::InteractionState::Open, "open state reached");
    Check(engine::CloseInteraction(registry, door), "close request accepted");
    engine::UpdateInteractions(registry, 0.5f);
    Check(engine::GetInteractionState(registry, door) == engine::InteractionState::Closed, "closed state reached");
    Check(Near(registry.Get<engine::ecs::Transform>(door).position.x, 0.0f), "closed pose restored");
    const auto events = engine::ConsumeInteractionEvents(registry, door);
    Check(events.size() >= 5 && events.front().type == engine::InteractionEventType::AccessDenied, "runtime events emitted");

    const auto lever = registry.Create();
    engine::ecs::Transform leverTransform; leverTransform.position = {0.0f, 0.0f, -1.0f};
    registry.Add<engine::ecs::Transform>(lever, leverTransform);
    Check(engine::ConfigureInteractiveMotion(registry, lever, loaded), "configure authored interaction contract");
    engine::InteractionQuery query;
    query.interactorPosition = {0.0f, 0.0f, 0.0f}; query.interactorForward = {0.0f, 0.0f, -1.0f};
    query.accessTag = "BlueKey"; query.conditionTags = {"PowerOn", "QuestReady"};
    Check(engine::QueryInteraction(registry, lever, query).available, "complete interaction query is available");
    query.hasLineOfSight = false;
    Check(!engine::QueryInteraction(registry, lever, query).available, "blocked line of sight rejects interaction");
    query.hasLineOfSight = true; query.conditionTags.pop_back();
    Check(!engine::QueryInteraction(registry, lever, query).available, "missing required condition rejects interaction");
    query.conditionTags.push_back("QuestReady");
    Check(!engine::RequestInteraction(registry, lever, query, 0.2f), "hold interaction waits for full duration");
    Check(engine::RequestInteraction(registry, lever, query, 0.2f), "hold interaction reaches animation gate");
    Check(engine::GetInteractionState(registry, lever) == engine::InteractionState::Closed, "animation gate delays motion");
    Check(!engine::SignalInteractionAnimationEvent(registry, lever, "WrongEvent"), "wrong animation event is ignored");
    Check(engine::SignalInteractionAnimationEvent(registry, lever, "LeverCommit"), "matching animation event commits interaction");
    Check(engine::GetInteractionState(registry, lever) == engine::InteractionState::Opening, "committed interaction starts motion");

    engine::InteractionAssetData hinge;
    hinge.motion = engine::InteractionMotionType::HingedDoor;
    hinge.pivotOffset = {-1.0f, 0.0f, 0.0f}; hinge.openAngleDegrees = 90.0f;
    engine::ecs::Transform closed;
    const auto openPose = engine::SampleInteractionTransform(hinge, closed, 1.0f);
    Check(Near(openPose.position.x, -1.0f) && Near(openPose.position.z, -1.0f), "hinge rotates around authored pivot");

    std::filesystem::remove_all(root);
    if (failures) return 1;
    std::cout << "Interaction asset tests passed\n";
    return 0;
}
