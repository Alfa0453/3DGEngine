#pragma once

#include "engine/assets/InteractionAsset.h"
#include "engine/ecs/Entity.h"
#include "engine/ecs/Components.h"

#include <string>
#include <cstddef>
#include <vector>

namespace engine {
namespace ecs { class Registry; }

enum class InteractionState : std::uint8_t { Closed, Opening, Open, Closing, Disabled };
enum class InteractionEventType : std::uint8_t { Opening, Opened, Closing, Closed, AccessDenied };

struct InteractionRuntimeEvent {
    InteractionEventType type = InteractionEventType::Opening;
    std::string audioPath;
    std::string animationPath;
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
};

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
