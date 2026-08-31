#pragma once

#include "engine/assets/InteractionAsset.h"
#include "engine/ecs/Entity.h"
#include "engine/ecs/Components.h"

#include <string>
#include <cstddef>
#include <vector>
#include <glm/glm.hpp>

namespace engine {
namespace ecs { class Registry; }

enum class InteractionState : std::uint8_t { Closed, Opening, Open, Closing, Disabled };
enum class InteractionEventType : std::uint8_t {
    Started, Opening, Opened, Closing, Closed, Completed, AccessDenied, ConditionFailed
};

struct InteractionRuntimeEvent {
    InteractionEventType type = InteractionEventType::Opening;
    std::string audioPath;
    std::string animationPath;
    std::string eventName;
};

struct InteractionQuery {
    glm::vec3 interactorPosition{0.0f};
    glm::vec3 interactorForward{0.0f, 0.0f, -1.0f};
    glm::vec3 targetPosition{0.0f};
    std::string accessTag;
    std::vector<std::string> conditionTags;
    bool hasLineOfSight = true;
};

struct InteractionAvailability {
    bool available = false;
    std::string prompt;
    std::string reason;
};

struct InteractiveMotionComponent {
    std::string assetPath;
    InteractionAssetData asset;
    ecs::Transform closedTransform;
    float alpha = 0.0f;
    float holdRemaining = 0.0f;
    InteractionState state = InteractionState::Closed;
    bool locked = false;
    bool activatedOnce = false;
    std::size_t processedEventCount = 0;
    std::vector<InteractionRuntimeEvent> events;
    float inputHoldProgress = 0.0f;
    std::string lastFailureReason;
    bool awaitingAnimationCommit = false;
};

InteractionAvailability EvaluateInteraction(const InteractionAssetData& asset,
                                            bool runtimeLocked,
                                            const InteractionQuery& query);
InteractionAvailability QueryInteraction(const ecs::Registry& registry, ecs::Entity entity,
                                         const InteractionQuery& query);
bool RequestInteraction(ecs::Registry& registry, ecs::Entity entity,
                        const InteractionQuery& query, float heldSeconds = 0.0f);
void CancelInteractionRequest(ecs::Registry& registry, ecs::Entity entity);
bool SignalInteractionAnimationEvent(ecs::Registry& registry, ecs::Entity entity,
                                     const std::string& eventName);

bool ConfigureInteractiveMotion(ecs::Registry& registry, ecs::Entity entity,
                                const std::string& assetPath,
                                std::string* error = nullptr);
bool ConfigureInteractiveMotion(ecs::Registry& registry, ecs::Entity entity,
                                const InteractionAssetData& asset,
                                const std::string& assetPath = {});
bool OpenInteraction(ecs::Registry& registry, ecs::Entity entity,
                     const std::string& accessTag = {});
bool CloseInteraction(ecs::Registry& registry, ecs::Entity entity);
bool ToggleInteraction(ecs::Registry& registry, ecs::Entity entity,
                       const std::string& accessTag = {});
bool SetInteractionLocked(ecs::Registry& registry, ecs::Entity entity, bool locked);
InteractionState GetInteractionState(const ecs::Registry& registry, ecs::Entity entity);
const char* InteractionStateName(InteractionState state);
std::vector<InteractionRuntimeEvent> ConsumeInteractionEvents(
    ecs::Registry& registry, ecs::Entity entity);
void UpdateInteractions(ecs::Registry& registry, float deltaSeconds);

// Calculates the authored pose without mutating a registry. Used by the editor
// preview and tests, so authoring and gameplay always agree.
ecs::Transform SampleInteractionTransform(const InteractionAssetData& asset,
                                          const ecs::Transform& closed,
                                          float alpha);

} // namespace engine
