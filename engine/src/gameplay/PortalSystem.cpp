#include "engine/gameplay/PortalSystem.h"
#include "engine/gameplay/Script.h"

#include "engine/core/Paths.h"
#include "engine/ecs/Components.h"
#include "engine/ecs/Registry.h"
#include "engine/physics/PhysicsComponents.h"

#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <filesystem>

namespace engine {
namespace {
std::string Resolve(const std::string& path) {
    const std::filesystem::path p(path);
    if (std::filesystem::exists(p)) return p.string();
    const std::filesystem::path exe(ExecutableDir());
    for (const auto& candidate : {exe / p, exe / "Content" / p, exe.parent_path() / p})
        if (std::filesystem::exists(candidate)) return candidate.string();
    return path;
}

ecs::Entity FindNamed(ecs::Registry& registry, const std::string& wanted) {
    ecs::Entity result = ecs::kNull;
    registry.view<ecs::RuntimeName>().each([&](ecs::Entity entity, ecs::RuntimeName& name) {
        if (result == ecs::kNull && name.value == wanted) result = entity;
    });
    return result;
}

void Push(PortalComponent& portal, PortalEventType type, ecs::Entity traveler,
          const std::string& level = {}, const std::string& audio = {},
          const std::string& effect = {}) {
    portal.events.push_back({type, traveler, level, audio, effect});
}
}

bool ConfigurePortal(ecs::Registry& registry, ecs::Entity entity,
                     const std::string& path, std::string* error) {
    PortalAssetData asset;
    if (!LoadPortalAsset(Resolve(path), &asset, error)) return false;
    return ConfigurePortal(registry, entity, asset, path);
}

bool ConfigurePortal(ecs::Registry& registry, ecs::Entity entity,
                     const PortalAssetData& source, const std::string& path) {
    if (!registry.Valid(entity) || !registry.TryGet<ecs::Transform>(entity)) return false;
    PortalAssetData asset = source;
    NormalizePortalAsset(asset);
    if (!ValidatePortalAsset(asset)) return false;
    PortalComponent component;
    component.assetPath = path;
    component.asset = std::move(asset);
    registry.Add<PortalComponent>(entity, std::move(component));
    return true;
}

bool PortalReady(const ecs::Registry& registry, ecs::Entity entity) {
    const auto* portal = registry.TryGet<PortalComponent>(entity);
    return portal && portal->cooldownRemaining <= 0.0f;
}

bool ActivatePortal(ecs::Registry& registry, ecs::Entity entity, ecs::Entity traveler,
                    const std::string& accessTag) {
    auto* portal = registry.TryGet<PortalComponent>(entity);
    auto* travelerTransform = registry.TryGet<ecs::Transform>(traveler);
    if (!portal || !travelerTransform || portal->cooldownRemaining > 0.0f) return false;
    if (!portal->asset.requiredAccessTag.empty() &&
        portal->asset.requiredAccessTag != accessTag) {
        Push(*portal, PortalEventType::AccessDenied, traveler, {}, portal->asset.enterAudioPath);
        return false;
    }

    if (portal->asset.mode != PortalMode::SameLevel) {
        portal->cooldownRemaining = portal->asset.cooldown;
        Push(*portal, PortalEventType::LevelTransitionRequested, traveler,
             portal->asset.destinationLevel, portal->asset.enterAudioPath,
             portal->asset.transitionEffectPath);
        return true;
    }

    const ecs::Entity destination = FindNamed(registry, portal->asset.destinationObject);
    const auto* destinationTransform = registry.TryGet<ecs::Transform>(destination);
    if (!destinationTransform || destination == entity) {
        Push(*portal, PortalEventType::DestinationMissing, traveler);
        return false;
    }

    glm::vec3 offset = portal->asset.arrivalOffset;
    if (portal->asset.safeArrival) offset.y = std::max(offset.y, 0.1f);
    travelerTransform->position = destinationTransform->position +
        destinationTransform->rotation * offset;
    if (portal->asset.alignFacing) {
        const glm::quat authored = glm::quat(glm::radians(portal->asset.arrivalRotationDegrees));
        travelerTransform->rotation = glm::normalize(destinationTransform->rotation * authored);
    }
    if (!portal->asset.preserveVelocity) {
        if (auto* body = registry.TryGet<ecs::RigidBody>(traveler)) {
            body->velocity = glm::vec3(0.0f);
            body->angularVelocity = glm::vec3(0.0f);
        }
    }
    portal->cooldownRemaining = portal->asset.cooldown;
    Push(*portal, PortalEventType::Teleported, traveler, {}, portal->asset.exitAudioPath,
         portal->asset.transitionEffectPath);
    return true;
}

void UpdatePortals(ecs::Registry& registry, float deltaSeconds) {
    const float dt = std::clamp(deltaSeconds, 0.0f, 1.0f);
    ecs::Entity traveler = ecs::kNull;
    registry.view<PortalTravelerComponent, ecs::Transform>().each(
        [&](ecs::Entity entity, PortalTravelerComponent&, ecs::Transform&) {
            if (traveler == ecs::kNull) traveler = entity;
        });
    registry.view<PortalComponent, ecs::Transform>().each(
        [&](ecs::Entity entity, PortalComponent& portal, ecs::Transform& transform) {
        portal.cooldownRemaining = std::max(0.0f, portal.cooldownRemaining - dt);
        if (!portal.asset.automatic || traveler == ecs::kNull) return;
        const auto* travelerTransform = registry.TryGet<ecs::Transform>(traveler);
        if (!travelerTransform) return;
        const bool inside = glm::distance(transform.position, travelerTransform->position) <=
            portal.asset.activationRadius;
        if (inside && !portal.automaticTravelerInside && portal.cooldownRemaining <= 0.0f) {
            if (ActivatePortal(registry, entity, traveler) &&
                portal.asset.mode != PortalMode::SameLevel)
                QueueScriptSceneLoadRequest(portal.asset.destinationLevel);
        }
        portal.automaticTravelerInside = inside;
    });
}

std::vector<PortalRuntimeEvent> ConsumePortalEvents(ecs::Registry& registry,
                                                     ecs::Entity entity) {
    auto* portal = registry.TryGet<PortalComponent>(entity);
    if (!portal) return {};
    auto events = std::move(portal->events);
    portal->events.clear();
    return events;
}

} // namespace engine
