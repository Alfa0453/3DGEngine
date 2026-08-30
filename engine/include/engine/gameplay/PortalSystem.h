#pragma once

#include "engine/assets/PortalAsset.h"
#include "engine/ecs/Entity.h"

#include <string>
#include <vector>

namespace engine {
namespace ecs { class Registry; }

enum class PortalEventType : std::uint8_t {
    Teleported,
    AccessDenied,
    LevelTransitionRequested,
    DestinationMissing
};

struct PortalRuntimeEvent {
    PortalEventType type = PortalEventType::Teleported;
    ecs::Entity traveler = ecs::kNull;
    std::string levelPath;
    std::string audioPath;
    std::string effectPath;
};

struct PortalComponent {
    std::string assetPath;
    PortalAssetData asset;
    float cooldownRemaining = 0.0f;
    bool automaticTravelerInside = false;
    std::vector<PortalRuntimeEvent> events;
};

// The runtime scene loader tags the configured player with this marker. Custom
// games may add it to another actor to make automatic portals react to that actor.
struct PortalTravelerComponent {};

bool ConfigurePortal(ecs::Registry& registry, ecs::Entity portal,
                     const std::string& assetPath, std::string* error = nullptr);
bool ConfigurePortal(ecs::Registry& registry, ecs::Entity portal,
                     const PortalAssetData& asset, const std::string& assetPath = {});
bool ActivatePortal(ecs::Registry& registry, ecs::Entity portal, ecs::Entity traveler,
                    const std::string& accessTag = {});
bool PortalReady(const ecs::Registry& registry, ecs::Entity portal);
void UpdatePortals(ecs::Registry& registry, float deltaSeconds);
std::vector<PortalRuntimeEvent> ConsumePortalEvents(ecs::Registry& registry,
                                                     ecs::Entity portal);

} // namespace engine
