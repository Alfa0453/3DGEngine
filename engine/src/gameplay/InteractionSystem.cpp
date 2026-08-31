#include "engine/gameplay/InteractionSystem.h"

#include "engine/core/Paths.h"
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

void Push(InteractiveMotionComponent& component, InteractionEventType type,
          const std::string& audio = {}, const std::string& animation = {},
          const std::string& eventName = {}) {
    component.events.push_back({type, audio, animation, eventName});
}

bool HasTag(const std::vector<std::string>& tags, const std::string& wanted) {
    return std::find(tags.begin(), tags.end(), wanted) != tags.end();
}
}

InteractionAvailability EvaluateInteraction(const InteractionAssetData& source,
                                            bool runtimeLocked,
                                            const InteractionQuery& query) {
    InteractionAssetData asset = source;
    NormalizeInteractionAsset(asset);
    InteractionAvailability result;
    result.prompt = asset.prompt;
    const auto deny = [&](const std::string& reason) {
        result.available = false; result.reason = reason;
        result.prompt = asset.unavailablePrompt;
    };
    if (runtimeLocked && (asset.accessTag.empty() || query.accessTag != asset.accessTag)) {
        deny("Required access tag is missing."); return result;
    }
    for (const auto& tag : asset.requiredConditionTags) {
        if (!HasTag(query.conditionTags, tag)) {
            deny("Missing condition: " + tag); return result;
        }
    }
    const glm::vec3 toTarget = query.targetPosition - query.interactorPosition;
    const float distance = glm::length(toTarget);
    if (distance > asset.interactionRange) {
        deny("Out of range."); return result;
    }
    if (asset.requireFacing && distance > 0.0001f) {
        const glm::vec3 forward = glm::dot(query.interactorForward, query.interactorForward) > 0.000001f
            ? glm::normalize(query.interactorForward) : glm::vec3(0.0f, 0.0f, -1.0f);
        const float cosine = glm::dot(forward, toTarget / distance);
        if (cosine < std::cos(glm::radians(asset.facingAngleDegrees))) {
            deny("Interactor is not facing the target."); return result;
        }
    }
    if (asset.requireLineOfSight && !query.hasLineOfSight) {
        deny("Line of sight is blocked."); return result;
    }
    result.available = true;
    return result;
}

InteractionAvailability QueryInteraction(const ecs::Registry& registry, ecs::Entity entity,
                                         const InteractionQuery& source) {
    const auto* c = registry.TryGet<InteractiveMotionComponent>(entity);
    if (!c) return {false, "Unavailable", "Target has no interaction component."};
    InteractionQuery query = source;
    if (const auto* transform = registry.TryGet<ecs::Transform>(entity))
        query.targetPosition = transform->position;
    if (c->state == InteractionState::Disabled)
        return {false, c->asset.unavailablePrompt, "Interaction is disabled."};
    if (c->asset.oneShot && c->activatedOnce)
        return {false, c->asset.unavailablePrompt, "Interaction was already used."};
    return EvaluateInteraction(c->asset, c->locked, query);
}

bool RequestInteraction(ecs::Registry& registry, ecs::Entity entity,
                        const InteractionQuery& query, float heldSeconds) {
    auto* c = registry.TryGet<InteractiveMotionComponent>(entity);
    if (!c) return false;
    const InteractionAvailability availability = QueryInteraction(registry, entity, query);
    if (!availability.available) {
        c->inputHoldProgress = 0.0f;
        c->lastFailureReason = availability.reason;
        const bool accessDenied = availability.reason == "Required access tag is missing.";
        Push(*c, accessDenied ? InteractionEventType::AccessDenied : InteractionEventType::ConditionFailed,
             accessDenied ? c->asset.lockedAudioPath : std::string{}, {}, c->asset.failedEvent);
        return false;
    }
    c->lastFailureReason.clear();
    if (c->inputHoldProgress <= 0.0f)
        Push(*c, InteractionEventType::Started, {}, c->asset.interactorAnimationPath,
             c->asset.startedEvent);
    if (c->asset.inputMode == InteractionInputMode::Hold) {
        c->inputHoldProgress += std::max(0.0f, heldSeconds);
        if (c->inputHoldProgress + 0.0001f < c->asset.holdDuration) return false;
    }
    c->inputHoldProgress = 0.0f;
    if (c->asset.waitForAnimationEvent) {
        c->awaitingAnimationCommit = true;
        return true;
    }
    return OpenInteraction(registry, entity, query.accessTag);
}

void CancelInteractionRequest(ecs::Registry& registry, ecs::Entity entity) {
    if (auto* c = registry.TryGet<InteractiveMotionComponent>(entity)) {
        c->inputHoldProgress = 0.0f;
        c->awaitingAnimationCommit = false;
    }
}

bool SignalInteractionAnimationEvent(ecs::Registry& registry, ecs::Entity entity,
                                     const std::string& eventName) {
    auto* c = registry.TryGet<InteractiveMotionComponent>(entity);
    if (!c || !c->awaitingAnimationCommit || eventName != c->asset.animationCommitEvent)
        return false;
    c->awaitingAnimationCommit = false;
    return OpenInteraction(registry, entity, c->asset.accessTag);
}

const char* InteractionStateName(InteractionState state) {
    switch (state) {
    case InteractionState::Closed: return "Closed";
    case InteractionState::Opening: return "Opening";
    case InteractionState::Open: return "Open";
    case InteractionState::Closing: return "Closing";
    case InteractionState::Disabled: return "Disabled";
    }
    return "Disabled";
}

ecs::Transform SampleInteractionTransform(const InteractionAssetData& source,
                                          const ecs::Transform& closed,
                                          float alpha) {
    InteractionAssetData asset = source;
    NormalizeInteractionAsset(asset);
    const float t = EvaluateInteractionEasing(asset.easing, alpha);
    ecs::Transform result = closed;
    if (asset.motion == InteractionMotionType::HingedDoor) {
        const glm::quat localTurn = glm::angleAxis(glm::radians(asset.openAngleDegrees * t), asset.hingeAxis);
        result.rotation = glm::normalize(closed.rotation * localTurn);
        const glm::vec3 worldPivot = closed.position + closed.rotation * (closed.scale * asset.pivotOffset);
        result.position = worldPivot - result.rotation * (closed.scale * asset.pivotOffset);
    } else {
        result.position = closed.position + closed.rotation * (asset.localTranslation * t);
    }
    return result;
}

bool ConfigureInteractiveMotion(ecs::Registry& registry, ecs::Entity entity,
                                const std::string& path, std::string* error) {
    InteractionAssetData asset;
    if (!LoadInteractionAsset(Resolve(path), &asset, error)) return false;
    return ConfigureInteractiveMotion(registry, entity, asset, path);
}

bool ConfigureInteractiveMotion(ecs::Registry& registry, ecs::Entity entity,
                                const InteractionAssetData& source,
                                const std::string& path) {
    auto* transform = registry.TryGet<ecs::Transform>(entity);
    if (!registry.Valid(entity) || !transform) return false;
    InteractionAssetData asset = source;
    NormalizeInteractionAsset(asset);
    if (!ValidateInteractionAsset(asset)) return false;
    InteractiveMotionComponent component;
    component.assetPath = path;
    component.asset = std::move(asset);
    component.closedTransform = *transform;
    component.alpha = component.asset.startsOpen ? 1.0f : 0.0f;
    component.state = component.asset.startsOpen ? InteractionState::Open : InteractionState::Closed;
    component.locked = component.asset.locked;
    *transform = SampleInteractionTransform(component.asset, component.closedTransform, component.alpha);
    registry.Add<InteractiveMotionComponent>(entity, std::move(component));
    if (auto* body = registry.TryGet<ecs::RigidBody>(entity)) {
        body->kinematic = true; body->useGravity = false; body->invMass = 0.0f;
    }
    return true;
}

bool OpenInteraction(ecs::Registry& registry, ecs::Entity entity, const std::string& accessTag) {
    auto* c = registry.TryGet<InteractiveMotionComponent>(entity);
    if (!c || c->state == InteractionState::Disabled) return false;
    if (c->locked && (c->asset.accessTag.empty() || accessTag != c->asset.accessTag)) {
        Push(*c, InteractionEventType::AccessDenied, c->asset.lockedAudioPath, {},
             c->asset.failedEvent); return false;
    }
    if (c->asset.oneShot && c->activatedOnce) return false;
    if (c->state == InteractionState::Open || c->state == InteractionState::Opening) return true;
    c->state = InteractionState::Opening;
    c->activatedOnce = true;
    Push(*c, InteractionEventType::Opening, c->asset.openAudioPath, c->asset.openAnimationPath);
    return true;
}

bool CloseInteraction(ecs::Registry& registry, ecs::Entity entity) {
    auto* c = registry.TryGet<InteractiveMotionComponent>(entity);
    if (!c || c->state == InteractionState::Disabled || c->state == InteractionState::Closed ||
        c->state == InteractionState::Closing) return c != nullptr;
    c->state = InteractionState::Closing;
    Push(*c, InteractionEventType::Closing, c->asset.closeAudioPath, c->asset.closeAnimationPath);
    return true;
}

bool ToggleInteraction(ecs::Registry& registry, ecs::Entity entity, const std::string& accessTag) {
    const auto* c = registry.TryGet<InteractiveMotionComponent>(entity);
    if (!c) return false;
    return (c->state == InteractionState::Closed || c->state == InteractionState::Closing)
        ? OpenInteraction(registry, entity, accessTag) : CloseInteraction(registry, entity);
}

bool SetInteractionLocked(ecs::Registry& registry, ecs::Entity entity, bool locked) {
    auto* c = registry.TryGet<InteractiveMotionComponent>(entity);
    if (!c) return false;
    c->locked = locked;
    return true;
}

InteractionState GetInteractionState(const ecs::Registry& registry, ecs::Entity entity) {
    const auto* c = registry.TryGet<InteractiveMotionComponent>(entity);
    return c ? c->state : InteractionState::Disabled;
}

std::vector<InteractionRuntimeEvent> ConsumeInteractionEvents(ecs::Registry& registry,
                                                               ecs::Entity entity) {
    auto* c = registry.TryGet<InteractiveMotionComponent>(entity);
    if (!c) return {};
    auto events = std::move(c->events);
    c->events.clear(); c->processedEventCount = 0;
    return events;
}

void UpdateInteractions(ecs::Registry& registry, float deltaSeconds) {
    // Runtime normally supplies a fixed step. Keep large hitches bounded, but
    // allow authoring/tests to advance a full second deterministically.
    const float dt = std::clamp(deltaSeconds, 0.0f, 1.0f);
    registry.view<InteractiveMotionComponent, ecs::Transform>().each(
        [&](ecs::Entity entity, InteractiveMotionComponent& c, ecs::Transform& transform) {
            const glm::vec3 oldPosition = transform.position;
            const std::size_t firstNewEvent = std::min(c.processedEventCount, c.events.size());
            if (c.asset.loop && (c.state == InteractionState::Closed || c.state == InteractionState::Open))
                c.state = c.state == InteractionState::Closed ? InteractionState::Opening : InteractionState::Closing;
            if (c.state == InteractionState::Opening) {
                c.alpha = std::min(1.0f, c.alpha + dt / c.asset.openDuration);
                if (c.alpha >= 1.0f) {
                    c.state = InteractionState::Open; c.holdRemaining = c.asset.holdOpenTime;
                    Push(c, InteractionEventType::Opened);
                    Push(c, InteractionEventType::Completed, {}, {}, c.asset.completedEvent);
                }
            } else if (c.state == InteractionState::Open && c.asset.autoClose && !c.asset.oneShot) {
                c.holdRemaining -= dt;
                if (c.holdRemaining <= 0.0f) CloseInteraction(registry, entity);
            } else if (c.state == InteractionState::Closing) {
                c.alpha = std::max(0.0f, c.alpha - dt / c.asset.closeDuration);
                if (c.alpha <= 0.0f) { c.state = InteractionState::Closed; Push(c, InteractionEventType::Closed); }
            }
            transform = SampleInteractionTransform(c.asset, c.closedTransform, c.alpha);
            if (auto* body = registry.TryGet<ecs::RigidBody>(entity)) {
                body->kinematic = true; body->useGravity = false; body->invMass = 0.0f;
                body->velocity = dt > 0.0f ? (transform.position - oldPosition) / dt : glm::vec3(0.0f);
            }
            for (std::size_t i = firstNewEvent; i < c.events.size(); ++i) {
                if (c.events[i].audioPath.empty()) continue;
                ecs::AudioSource audio;
                audio.path = c.events[i].audioPath;
                audio.autoplay = true; audio.spatial = true; audio.loop = false;
                registry.Add<ecs::AudioSource>(entity, std::move(audio));
            }
            c.processedEventCount = c.events.size();
        });
}

} // namespace engine
