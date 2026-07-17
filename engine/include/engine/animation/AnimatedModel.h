#pragma once

#include "engine/animation/AnimationController.h"

#include <glm/glm.hpp>

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace engine {

class SkinnedModel;                 // forward declared -- only a pointer is stored
class Texture;                      // optional albedo override
namespace ecs { class Registry; }

// A timestamped notify on an animation (e.g. the frame an attack connects). Fired
// through AnimatedModel::onEvent as the action layer's time crosses it.
struct AnimEvent {
    int         clip = -1;          // -1 = action-local/any clip, >=0 = clip index
    float       time = 0.0f;        // seconds into the clip
    std::string name;
};

// A one-shot action played OVER the base locomotion on a set of bones (a per-bone
// mask). Used for attacks / casts / hit-reactions: e.g. an upper-body swing while
// the legs keep walking. Managed by UpdateAnimations; start one with PlayAction().
struct AnimAction {
    int   clip = -1;
    float time = 0.0f;
    float weight = 0.0f;            // current blend weight (managed)
    float fadeIn = 0.1f, fadeOut = 0.2f;
    float speed = 1.0f;
    bool  active = false;
    std::vector<float>     mask;    // per-bone; empty = full body
    std::vector<AnimEvent> events;  // fired as `time` passes each
    std::size_t            nextEvent = 0;
};

// ECS component: a rigged SkinnedModel plus its per-entity animation state. The
// model is shared (load once, point many entities at it); the controller and the
// computed `pose` (skinning matrices) are per-entity. Positioned by the entity's
// Transform. Add one to an entity, then call UpdateAnimations() each frame to
// advance the controller and refill `pose`; the skinned PBR renderer draws it.
struct AnimatedModel {
    const SkinnedModel*    model = nullptr;
    AnimationController    controller;
    std::vector<glm::mat4> pose;          // bone matrices, refilled by UpdateAnimations

    // One-shot action layer + its event callback.
    AnimAction                            action;
    std::vector<AnimEvent>                events;  // base controller notifies
    std::function<void(const std::string&)> onEvent;
    std::function<void(const std::string&, const std::string&)> onStateChanged;

    // PBR material overrides (the imported Material is Phong-ish). albedo tints the
    // model's diffuse; metallic/roughness set the surface response; emissive is an
    // additive glow (handy to flash on an event).
    glm::vec3 tint{1.0f};
    float     metallic  = 0.0f;
    float     roughness = 0.6f;
    glm::vec3 emissive{0.0f};
    bool      castShadow = true;

    // Optional albedo texture applied to EVERY submesh, overriding the model's own
    // material maps. Useful for packs whose textures aren't referenced by the mesh
    // file (e.g. a shared palette atlas beside the model). null = use materials.
    const Texture* albedoOverride = nullptr;

    void SetModel(const SkinnedModel* m) { model = m; }

    // Trigger a one-shot action layer. `mask` empty = full body; `events` fire via
    // onEvent as the action plays. Restarts the action if already playing.
    void PlayAction(int clip, std::vector<float> mask = {}, std::vector<AnimEvent> events = {},
                    float fadeIn = 0.1f, float fadeOut = 0.2f, float speed = 1.0f);
    bool ActionPlaying() const { return action.active; }
    // Full-body actions behave like non-layered montages: gameplay locomotion is
    // held until the clip finishes. A non-empty bone mask is layered and does not
    // block movement, allowing upper-body actions while walking or running.
    bool BlocksMovement() const { return action.active && action.mask.empty(); }
};

// Advance every AnimatedModel's controller (+ action layer) by dt and recompute its
// pose (base locomotion, cross-faded, with the action layered on top). Call once
// per frame before rendering. Entities whose model has no clips get the bind pose.
void UpdateAnimations(ecs::Registry& registry, float dt);

} // namespace engine
