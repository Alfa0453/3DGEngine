#pragma once

#include "engine/assets/AssetIdentity.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace engine {

inline constexpr std::uint32_t kInteractionAssetVersion = 2;

enum class InteractionMotionType : std::uint32_t {
    HingedDoor = 0,
    SlidingDoor,
    Gate,
    Elevator,
    MovingPlatform
};

enum class InteractionEasing : std::uint32_t {
    Linear = 0,
    SmoothStep,
    EaseInOut
};

enum class InteractionInputMode : std::uint32_t {
    Press = 0,
    Hold
};

struct InteractionAssetData {
    NativeAssetHeader header;
    std::string name = "InteractiveObject";
    InteractionMotionType motion = InteractionMotionType::HingedDoor;
    InteractionEasing easing = InteractionEasing::SmoothStep;

    // Hinged motion rotates around the local pivot and axis. Other modes move
    // by localTranslation from the captured closed transform.
    glm::vec3 localTranslation{0.0f, 3.0f, 0.0f};
    glm::vec3 hingeAxis{0.0f, 1.0f, 0.0f};
    glm::vec3 pivotOffset{-0.5f, 0.0f, 0.0f};
    float openAngleDegrees = 90.0f;
    float openDuration = 0.8f;
    float closeDuration = 0.8f;
    float holdOpenTime = 1.5f;
    float interactionRange = 2.5f;
    float facingAngleDegrees = 75.0f;
    float holdDuration = 0.0f;

    bool startsOpen = false;
    bool autoClose = true;
    bool loop = false;
    bool locked = false;
    bool oneShot = false;
    std::string accessTag;
    std::string prompt = "Interact";
    std::string unavailablePrompt = "Unavailable";
    std::string inputAction = "Interact";
    InteractionInputMode inputMode = InteractionInputMode::Press;
    bool requireFacing = true;
    bool requireLineOfSight = true;
    bool lockInteractorMovement = false;
    bool waitForAnimationEvent = false;
    std::vector<std::string> requiredConditionTags;
    std::string animationCommitEvent = "InteractionCommit";
    std::string startedEvent = "InteractionStarted";
    std::string completedEvent = "InteractionCompleted";
    std::string failedEvent = "InteractionFailed";

    std::string openAudioPath;
    std::string closeAudioPath;
    std::string lockedAudioPath;
    std::string openAnimationPath;
    std::string closeAnimationPath;
    std::string interactorAnimationPath;
    AssetHandle openAudioId;
    AssetHandle closeAudioId;
    AssetHandle lockedAudioId;
    AssetHandle openAnimationId;
    AssetHandle closeAnimationId;
    AssetHandle interactorAnimationId;
};

void NormalizeInteractionAsset(InteractionAssetData& asset);
bool ValidateInteractionAsset(const InteractionAssetData& asset,
                              std::string* error = nullptr);
float EvaluateInteractionEasing(InteractionEasing easing, float alpha);
bool SaveInteractionAsset(const std::string& path, InteractionAssetData asset,
                          std::string* error = nullptr);
bool LoadInteractionAsset(const std::string& path, InteractionAssetData* asset,
                          std::string* error = nullptr);

const char* InteractionMotionTypeName(InteractionMotionType type);
const char* InteractionEasingName(InteractionEasing easing);
const char* InteractionInputModeName(InteractionInputMode mode);

} // namespace engine
