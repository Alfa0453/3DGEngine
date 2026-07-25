#pragma once

#include "engine/animation/AnimationController.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace engine {

namespace ecs { struct Transform; }

// Build a render-only model offset from an Euler rotation (degrees): it rotates the
// mesh AND re-centres the model's bounding-box centre on the object origin, so a
// stood-up off-axis rig lines up with an origin-centred capsule. Identity when the
// Euler is ~zero (no offset). The object transform is never touched, so the collider
// and controller stay upright.
inline glm::mat4 MakeModelRenderOffset(const glm::vec3& eulerDegrees, const glm::vec3& modelCenter) {
    if (glm::dot(eulerDegrees, eulerDegrees) < 1e-4f) return glm::mat4(1.0f);
    return glm::mat4_cast(glm::quat(glm::radians(eulerDegrees)))
         * glm::translate(glm::mat4(1.0f), -modelCenter);
}

// Full render-only model offset: position + Euler rotation (degrees) + non-uniform
// scale, all applied about the model's bounding-box centre. A point of the model maps
// as  T(position) * R(euler) * S(scale) * T(-modelCenter). So rotation and scale pivot
// on the model centre, and position then shifts it in the object's local space. Returns
// identity when position~0, euler~0 and scale~1 (a plain model is left untouched, never
// re-centred). The object Transform is never modified, so the collider stays put.
inline glm::mat4 MakeModelRenderOffset(const glm::vec3& position,
                                       const glm::vec3& eulerDegrees,
                                       const glm::vec3& scale,
                                       const glm::vec3& modelCenter) {
    const glm::vec3 scaleDelta = scale - glm::vec3(1.0f);
    const bool isIdentity = glm::dot(position, position) < 1e-8f
                         && glm::dot(eulerDegrees, eulerDegrees) < 1e-4f
                         && glm::dot(scaleDelta, scaleDelta) < 1e-8f;
    if (isIdentity) return glm::mat4(1.0f);
    glm::mat4 m(1.0f);
    m = glm::translate(m, position);
    m *= glm::mat4_cast(glm::quat(glm::radians(eulerDegrees)));
    m = glm::scale(m, glm::vec3(scale.x != 0.0f ? scale.x : 1e-4f,
                                scale.y != 0.0f ? scale.y : 1e-4f,
                                scale.z != 0.0f ? scale.z : 1e-4f));
    m = glm::translate(m, -modelCenter);
    return m;
}

class SkinnedModel;                 // forward declared -- only a pointer is stored
class Texture;                      // optional albedo override
class Model;                        // static model attached to a bone (weapon, shield...)
class Shader;
namespace ecs { class Registry; }

// Build an attachment's local socket offset (position + Euler degrees + scale).
inline glm::mat4 MakeAttachmentOffset(const glm::vec3& position,
                                      const glm::vec3& eulerDegrees,
                                      const glm::vec3& scale) {
    glm::mat4 m(1.0f);
    m = glm::translate(m, position);
    m *= glm::mat4_cast(glm::quat(glm::radians(eulerDegrees)));
    m = glm::scale(m, glm::vec3(scale.x != 0.0f ? scale.x : 1e-4f,
                                scale.y != 0.0f ? scale.y : 1e-4f,
                                scale.z != 0.0f ? scale.z : 1e-4f));
    return m;
}

// A static model socketed to a skeleton bone (a weapon in the hand, a shield on the
// back, ...). Resolved from paths/names at load time. Its world matrix each frame is
//   characterMatrix * pose[bone] * boneBind * localOffset
// where boneBind = inverse(bone.offset) turns the skinning matrix back into the bone's
// mesh-space transform, so the attachment rides the animated bone.
struct ModelAttachment {
    const Model* model = nullptr;
    int          bone  = -1;            // skeleton bone index (-1 = model origin)
    glm::mat4    boneBind{1.0f};        // inverse(bone.offset), precomputed
    glm::mat4    localOffset{1.0f};     // authored position/rotation/scale at the socket
    glm::vec3    tint{1.0f};            // material base colour (multiplies the model)
    const Texture* albedoOverride = nullptr;  // material albedo map (optional)
};

struct NamedModelSocket {
    std::string name;
    int         bone = -1;
    glm::mat4   boneBind{1.0f};
    glm::mat4   localOffset{1.0f};
};

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

    // Render-only orientation offset applied to the model's local space (model matrix
    // becomes Transform * renderOffset). Used to stand up an off-axis rig (e.g. a
    // Z-up import) WITHOUT rotating the entity Transform -- so the collider and
    // controller, which read the Transform, stay upright. Identity = no offset.
    glm::mat4 renderOffset{1.0f};

    // Static models socketed to bones (weapons, shields). Drawn after the skinned mesh.
    std::vector<ModelAttachment> attachments;
    // Named animated points authored in the Character Editor. They may exist without
    // a visible attachment and are intended for gameplay spawns and traces.
    std::vector<NamedModelSocket> sockets;

    bool SocketWorldTransform(const ecs::Transform& character,
                              const std::string& name,
                              glm::mat4* world) const;

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

// Draw an AnimatedModel's socketed attachments (weapons, shields...). The caller must
// have bound `shader` and set its view-projection + lighting uniforms; this sets uModel
// / uNormalMat per attachment and draws each static model at its animated bone socket.
// `characterMatrix` is Transform.Model() * renderOffset (the skinned mesh's world space).
void DrawAnimatedModelAttachments(const AnimatedModel& animated,
                                  const glm::mat4& characterMatrix, Shader& shader);

} // namespace engine
