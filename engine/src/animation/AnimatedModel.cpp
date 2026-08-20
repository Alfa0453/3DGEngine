#include "engine/animation/AnimatedModel.h"

#include "engine/animation/Animator.h"
#include "engine/animation/Skeleton.h"
#include "engine/graphics/SkinnedModel.h"
#include "engine/graphics/Model.h"
#include "engine/graphics/Shader.h"
#include "engine/ecs/Registry.h"
#include "engine/ecs/Components.h"
#include "engine/gameplay/GameplayComponents.h"

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <initializer_list>
#include <string>
#include <utility>

namespace engine {

namespace {
// Duration of a clip in seconds (0 if empty).
float ClipSeconds(const Animation& a) {
    const float tps = (a.ticksPerSecond > 0.0f) ? a.ticksPerSecond : 25.0f;
    return (a.duration > 0.0f) ? a.duration / tps : 0.0f;
}

// Rotation of a world-space bone matrix (columns normalised to drop any scale).
glm::quat QuatFromWorld(const glm::mat4& m) {
    glm::mat3 r(m);
    if (glm::dot(r[0], r[0]) < 1.0e-12f) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    r[0] = glm::normalize(r[0]);
    r[1] = glm::normalize(r[1]);
    r[2] = glm::normalize(r[2]);
    return glm::quat_cast(r);
}

// Grounded foot placement, run on the final local pose just before Compose. Opt-in via
// am.footIK.enabled + a groundQuery; a no-op otherwise so existing characters are untouched.
void ApplyFootIK(AnimatedModel& am, const ecs::Transform& transform, const Skeleton& skel,
                 std::vector<BoneLocal>& local) {
    FootIK& ik = am.footIK;
    if (!ik.enabled || !ik.groundQuery || ik.weight <= 0.0f) return;
    const std::size_t n = skel.bones.size();
    if (n == 0 || local.size() != n) return;
    if (!ik.detected && !(ik.left.Valid() && ik.right.Valid())) AutoDetectFootIKBones(skel, ik);
    if (!ik.left.Valid() && !ik.right.Valid()) return;
    const float w = std::min(std::max(ik.weight, 0.0f), 1.0f);

    // Forward pass: local pose -> world (animation-root frame) matrices.
    std::vector<glm::mat4> world;
    const auto computeWorld = [&]() {
        world.assign(n, glm::mat4(1.0f));
        for (std::size_t i = 0; i < n; ++i) {
            const BoneLocal& bl = local[i];
            const glm::mat4 lm = glm::translate(glm::mat4(1.0f), bl.pos)
                               * glm::mat4_cast(bl.rot)
                               * glm::scale(glm::mat4(1.0f), bl.scale);
            const int p = skel.bones[i].parent;
            world[i] = (p >= 0) ? world[static_cast<std::size_t>(p)] * lm : lm;
        }
    };
    computeWorld();

    // animation-root frame <-> scene world.
    const glm::mat4 S = transform.Model() * am.renderOffset * skel.globalInverse;
    const glm::mat4 Sinv = glm::inverse(S);
    const glm::vec3 upScene(0.0f, 1.0f, 0.0f);
    const glm::vec3 downScene(0.0f, -1.0f, 0.0f);
    const glm::vec3 upAnim = glm::normalize(glm::mat3(Sinv) * upScene);

    struct Plan { bool hit = false; glm::vec3 targetAnim{0.0f}; float dropAnim = 0.0f; };
    const auto planLeg = [&](const FootIKLeg& leg) -> Plan {
        Plan pl;
        if (!leg.Valid()) return pl;
        const glm::vec3 footAnim = glm::vec3(world[static_cast<std::size_t>(leg.foot)][3]);
        const glm::vec3 footScene = glm::vec3(S * glm::vec4(footAnim, 1.0f));
        glm::vec3 hitPos, hitNormal;
        if (!ik.groundQuery(footScene + upScene * ik.traceUp, downScene,
                            ik.traceUp + ik.traceDown, hitPos, hitNormal))
            return pl;
        const glm::vec3 desiredScene = hitPos + upScene * ik.footHeight;
        pl.targetAnim = glm::vec3(Sinv * glm::vec4(desiredScene, 1.0f));
        pl.dropAnim = std::max(0.0f, glm::dot(footAnim - pl.targetAnim, upAnim));
        pl.hit = true;
        return pl;
    };
    const Plan lp = planLeg(ik.left);
    const Plan rp = planLeg(ik.right);
    if (!lp.hit && !rp.hit) return;

    // Pelvis drop: lower toward the higher ground (the smaller drop) so the shallower foot
    // still reaches, scaled by pelvisWeight, clamped, and faded by the IK weight.
    if (ik.pelvis >= 0 && ik.pelvis < static_cast<int>(n)) {
        float drop = (lp.hit && rp.hit) ? std::min(lp.dropAnim, rp.dropAnim)
                                        : (lp.hit ? lp.dropAnim : rp.dropAnim);
        drop = std::min(drop * ik.pelvisWeight, ik.maxPelvisDrop) * w;
        if (drop > 1.0e-5f) {
            const int par = skel.bones[static_cast<std::size_t>(ik.pelvis)].parent;
            const glm::quat parentRot = (par >= 0)
                ? QuatFromWorld(world[static_cast<std::size_t>(par)]) : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            local[static_cast<std::size_t>(ik.pelvis)].pos += (glm::inverse(parentRot) * (-upAnim)) * drop;
            computeWorld();   // hips moved -> refresh the leg joint positions
        }
    }

    // Two-bone solve per planted leg, converting the world-space delta rotations back into
    // the pose's local rotations and blending by the IK weight.
    const auto solveLeg = [&](const FootIKLeg& leg, const Plan& pl) {
        if (!pl.hit || !leg.Valid()) return;
        const glm::vec3 hip  = glm::vec3(world[static_cast<std::size_t>(leg.upper)][3]);
        const glm::vec3 knee = glm::vec3(world[static_cast<std::size_t>(leg.mid)][3]);
        const glm::vec3 foot = glm::vec3(world[static_cast<std::size_t>(leg.foot)][3]);
        const Animator::TwoBoneIKResult r =
            Animator::SolveTwoBoneIK(hip, knee, foot, pl.targetAnim, knee);

        const int pu = skel.bones[static_cast<std::size_t>(leg.upper)].parent;
        const glm::quat Rpu = (pu >= 0)
            ? QuatFromWorld(world[static_cast<std::size_t>(pu)]) : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        const glm::quat RupperOld = QuatFromWorld(world[static_cast<std::size_t>(leg.upper)]);
        const glm::quat RupperNew = glm::normalize(r.upper * RupperOld);
        const glm::quat upperLocal = glm::normalize(glm::inverse(Rpu) * RupperNew);
        local[static_cast<std::size_t>(leg.upper)].rot =
            glm::normalize(glm::slerp(local[static_cast<std::size_t>(leg.upper)].rot, upperLocal, w));

        // The mid bone inherits the (full) new upper world; layer the lower delta on top.
        const glm::quat RmidInherited =
            glm::normalize(RupperNew * local[static_cast<std::size_t>(leg.mid)].rot);
        const glm::quat RmidNew = glm::normalize(r.lower * RmidInherited);
        const glm::quat midLocal = glm::normalize(glm::inverse(RupperNew) * RmidNew);
        local[static_cast<std::size_t>(leg.mid)].rot =
            glm::normalize(glm::slerp(local[static_cast<std::size_t>(leg.mid)].rot, midLocal, w));
    };
    solveLeg(ik.left, lp);
    solveLeg(ik.right, rp);
}
} // namespace

bool AutoDetectFootIKBones(const Skeleton& skel, FootIK& ik) {
    ik.detected = true;
    std::vector<std::string> names(skel.bones.size());
    for (std::size_t i = 0; i < skel.bones.size(); ++i) {
        std::string n = skel.bones[i].name;
        for (char& c : n) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        names[i] = std::move(n);
    }
    const auto find = [&](std::initializer_list<const char*> keys) -> int {
        for (const char* key : keys)
            for (std::size_t i = 0; i < names.size(); ++i)
                if (names[i].find(key) != std::string::npos) return static_cast<int>(i);
        return -1;
    };
    // Order matters: more specific "upleg/thigh" before the plain "leg" knee key.
    ik.pelvis      = find({"hips", "pelvis"});
    ik.left.upper  = find({"leftupleg", "upperleg_l", "upperleg.l", "thigh_l", "thigh.l",
                           "leftthigh", "leftupperleg", "upleg_l"});
    ik.left.mid    = find({"leftleg", "lowerleg_l", "lowerleg.l", "calf_l", "calf.l", "shin_l",
                           "leftcalf", "leftlowerleg", "leftshin"});
    ik.left.foot   = find({"leftfoot", "foot_l", "foot.l", "leftankle", "ankle_l"});
    ik.right.upper = find({"rightupleg", "upperleg_r", "upperleg.r", "thigh_r", "thigh.r",
                           "rightthigh", "rightupperleg", "upleg_r"});
    ik.right.mid   = find({"rightleg", "lowerleg_r", "lowerleg.r", "calf_r", "calf.r", "shin_r",
                           "rightcalf", "rightlowerleg", "rightshin"});
    ik.right.foot  = find({"rightfoot", "foot_r", "foot.r", "rightankle", "ankle_r"});
    return ik.left.Valid() && ik.right.Valid();
}

void AnimatedModel::PlayAction(int clip, std::vector<float> mask, std::vector<AnimEvent> animationEvents,
                               float fadeIn, float fadeOut, float speed) {
    action.clip      = clip;
    action.time      = 0.0f;
    action.weight    = 0.0f;
    action.fadeIn    = fadeIn;
    action.fadeOut   = fadeOut;
    action.speed     = speed;
    action.active    = (clip >= 0);
    action.mask      = std::move(mask);
    action.events    = std::move(animationEvents);
    std::sort(action.events.begin(), action.events.end(),
        [](const AnimEvent& a, const AnimEvent& b) { return a.time < b.time; });
    action.nextEvent = 0;
}

bool AnimatedModel::SocketWorldTransform(const ecs::Transform& character,
                                         const std::string& name,
                                         glm::mat4* world) const {
    if (!world || name.empty()) return false;
    for (const NamedModelSocket& named : sockets) {
        if (named.name != name) continue;
        glm::mat4 socket = character.Model() * renderOffset;
        if (named.bone >= 0 && named.bone < static_cast<int>(pose.size())) {
            socket *= pose[static_cast<std::size_t>(named.bone)] * named.boneBind;
        }
        *world = socket * named.localOffset;
        return true;
    }
    return false;
}

void DrawAnimatedModelAttachments(const AnimatedModel& animated,
                                  const glm::mat4& characterMatrix, Shader& shader) {
    for (const ModelAttachment& att : animated.attachments) {
        if (!att.model) continue;
        // Socket the attachment to the bone's animated mesh-space transform.
        glm::mat4 socket = characterMatrix;
        if (att.bone >= 0 && att.bone < static_cast<int>(animated.pose.size())) {
            socket = characterMatrix * animated.pose[static_cast<std::size_t>(att.bone)] * att.boneBind;
        }
        const glm::mat4 world = socket * att.localOffset;
        shader.SetMat4("uModel", world);
        shader.SetMat3("uNormalMat", glm::mat3(glm::transpose(glm::inverse(world))));
        DrawModel(*att.model, shader, att.tint, att.albedoOverride);
    }
}

void UpdateAnimations(ecs::Registry& reg, float dt) {
    reg.view<ecs::Transform, AnimatedModel>().each([&](ecs::Entity entity, ecs::Transform& transform, AnimatedModel& am) {
        if (const Ragdoll* ragdoll = reg.TryGet<Ragdoll>(entity);
            ragdoll && ragdoll->active) {
            return;
        }
        if (!am.model) return;
        const Skeleton& skel = am.model->GetSkeleton();
        const auto& clips = am.model->Animations();

        if (clips.empty()) {                       // no animation -> bind pose
            if (am.pose.size() != skel.bones.size())
                Animator::ComputeBindPose(skel, am.pose);
            return;
        }

        const int previousBaseClip = am.controller.CurrentClip();
        const float previousStateTime = am.controller.CurrentTime();
        const float previousBaseTime = am.controller.CurrentSourceTime();
        const std::string previousState = am.controller.CurrentStateName();
        am.controller.Update(dt);
        const std::string& currentState = am.controller.CurrentStateName();
        if (previousState != currentState) {
            if (am.onStateChanged) am.onStateChanged(previousState, currentState);
            if (am.onEvent) {
                am.onEvent("StateExit:" + previousState);
                am.onEvent("StateEnter:" + currentState);
            }
        }

        auto clipAt = [&](int idx) -> const Animation* {
            return (idx >= 0 && idx < static_cast<int>(clips.size()))
                 ? &clips[static_cast<std::size_t>(idx)] : nullptr;
        };

        const int currentBaseClip = am.controller.CurrentClip();
        if (am.onEvent && currentBaseClip >= 0) {
            const Animation* eventClip = clipAt(currentBaseClip);
            const float clipLength = eventClip ? ClipSeconds(*eventClip) : 0.0f;
            const float currentBaseTime = am.controller.CurrentSourceTime();
            for (const AnimEvent& event : am.events) {
                if (event.name.empty() || (event.clip >= 0 && event.clip != currentBaseClip)) {
                    continue;
                }
                const bool sameClip = previousBaseClip == currentBaseClip;
                bool crossed = false;
                if (!sameClip) {
                    crossed = event.time <= currentBaseTime;
                } else if (clipLength > 0.0f) {
                    const auto firstCycle = static_cast<long long>(std::floor(previousBaseTime / clipLength));
                    const auto lastCycle = static_cast<long long>(std::floor(currentBaseTime / clipLength));
                    const float previousLocal = std::fmod(std::max(previousBaseTime, 0.0f), clipLength);
                    const float currentLocal = std::fmod(std::max(currentBaseTime, 0.0f), clipLength);
                    crossed = lastCycle > firstCycle
                        ? (event.time > previousLocal || event.time <= currentLocal
                           || lastCycle > firstCycle + 1)
                        : (event.time > previousLocal && event.time <= currentLocal);
                } else {
                    crossed = event.time > previousBaseTime && event.time <= currentBaseTime;
                }
                if (crossed) {
                    am.onEvent(event.name);
                }
            }
        }

        // --- Base locomotion pose (clip, Blend Space, and state cross-fade) -----
        const Animation* cur = clipAt(am.controller.CurrentClip());
        if (!cur) {
            if (am.pose.size() != skel.bones.size())
                Animator::ComputeBindPose(skel, am.pose);
            return;
        }

        if (am.controller.CurrentRootMotion()
            && previousBaseClip == am.controller.CurrentClip()
            && dt > 0.0f) {
            const auto rootDelta = [&](const Animation& animation, float beforeTime, float afterTime) {
                const glm::vec3 before = Animator::SampleRootTranslation(skel, animation, beforeTime);
                const glm::vec3 after = Animator::SampleRootTranslation(skel, animation, afterTime);
                glm::vec3 delta = after - before;
                const float length = ClipSeconds(animation);
                if (length > 0.0f) {
                    const int previousCycle = static_cast<int>(std::floor(beforeTime / length));
                    const int currentCycle = static_cast<int>(std::floor(afterTime / length));
                    if (currentCycle > previousCycle) {
                        const glm::vec3 start = Animator::SampleRootTranslation(skel, animation, 0.0f);
                        const glm::vec3 end = Animator::SampleRootTranslation(skel, animation, length - 0.0001f);
                        delta = (end - before) + (after - start)
                            + static_cast<float>(currentCycle - previousCycle - 1) * (end - start);
                    }
                }
                return delta;
            };
            glm::vec3 delta(0.0f);
            const auto rootSpace = am.controller.CurrentBlendSpace();
            if (rootSpace.active && !rootSpace.samples.empty()) {
                const float referenceLength = ClipSeconds(*cur);
                for (const auto& sample : rootSpace.samples) {
                    const Animation* animation = clipAt(sample.clip);
                    if (!animation || sample.weight <= 0.0f) continue;
                    float beforeTime = previousStateTime, afterTime = am.controller.CurrentTime();
                    const float sampleLength = ClipSeconds(*animation);
                    if (rootSpace.synchronized && referenceLength > 0.0001f && sampleLength > 0.0001f) {
                        beforeTime = previousStateTime * sampleLength / referenceLength;
                        afterTime = am.controller.CurrentTime() * sampleLength / referenceLength;
                    } else if (!rootSpace.synchronized) {
                        beforeTime *= std::max(sample.basePlaybackSpeed, 0.0f);
                        afterTime *= std::max(sample.basePlaybackSpeed, 0.0f);
                    }
                    delta += rootDelta(*animation, beforeTime, afterTime) * sample.weight;
                }
            } else {
                delta = rootDelta(*cur, previousBaseTime, am.controller.CurrentSourceTime());
            }
            transform.position += transform.rotation * delta;

            // Root yaw: transfer the root bone's turn to the entity so curved / turn-in-place
            // clips steer the character (the mesh root is neutralised below, so it is not
            // applied twice). Only the twist about the rig's up axis is used, and the single
            // wrap frame per loop is skipped to avoid a spurious full-cycle delta.
            const float rotLen = ClipSeconds(*cur);
            const bool rotWrapped = rotLen > 0.0f
                && std::floor(previousBaseTime / rotLen)
                       != std::floor(am.controller.CurrentSourceTime() / rotLen);
            if (!rotWrapped) {
                const glm::quat before =
                    Animator::SampleRootRotation(skel, *cur, previousBaseTime);
                const glm::quat after =
                    Animator::SampleRootRotation(skel, *cur, am.controller.CurrentSourceTime());
                glm::quat d = glm::normalize(after * glm::inverse(before));
                if (d.w < 0.0f) d = -d;                       // shortest arc
                glm::quat yaw(d.w, 0.0f, d.y, 0.0f);          // swing-twist about local Y
                const float n = std::sqrt(yaw.w * yaw.w + yaw.y * yaw.y);
                if (n > 1.0e-5f) {
                    yaw.w /= n;
                    yaw.y /= n;
                    transform.rotation = glm::normalize(transform.rotation * yaw);
                }
            }
        }

        float curTime = am.controller.CurrentSourceTime();
        if (!am.controller.CurrentLoop()) {
            const float len = ClipSeconds(*cur);
            if (len > 0.0f) curTime = std::min(curTime, len - 1e-4f);
        }

        auto sampleState = [&](int fallbackClip, float time,
                               const AnimationController::BlendSpaceResult& space,
                               std::vector<BoneLocal>& out) {
            const Animation* reference = clipAt(fallbackClip);
            auto sampleTime = [&](const Animation* sample, float basePlaybackSpeed) {
                if (!space.synchronized)
                    return time * std::max(basePlaybackSpeed, 0.0f);
                if (!reference || !sample) return time;
                const float referenceLength = ClipSeconds(*reference);
                const float sampleLength = ClipSeconds(*sample);
                if (referenceLength <= 0.0001f || sampleLength <= 0.0001f) return time;
                const float phase = std::fmod(std::max(time, 0.0f), referenceLength) / referenceLength;
                return phase * sampleLength;
            };
            if (space.active && !space.samples.empty()) {
                float accumulated = 0.0f;
                bool sampled = false;
                for (const auto& weighted : space.samples) {
                    const Animation* clip = clipAt(weighted.clip);
                    if (!clip || weighted.weight <= 0.0f) continue;
                    std::vector<BoneLocal> pose;
                    Animator::SampleLocal(skel, *clip,
                        sampleTime(clip, weighted.basePlaybackSpeed), pose);
                    if (!sampled) {
                        out = std::move(pose);
                        accumulated = weighted.weight;
                        sampled = true;
                    } else {
                        std::vector<BoneLocal> mixed;
                        const float blend = weighted.weight / (accumulated + weighted.weight);
                        Animator::BlendLocal(out, pose, blend, mixed);
                        out = std::move(mixed);
                        accumulated += weighted.weight;
                    }
                }
                return sampled;
            }
            const Animation* a = clipAt(fallbackClip);
            if (!a) return false;
            Animator::SampleLocal(skel, *a, time, out);
            return true;
        };

        std::vector<BoneLocal> currentPose;
        sampleState(am.controller.CurrentClip(), curTime,
            am.controller.CurrentBlendSpace(), currentPose);

        // Preserve the original two-clip field for older assets.
        if (!am.controller.CurrentBlendSpace().active) {
            const Animation* blendClip = clipAt(am.controller.CurrentBlendClip());
            const float blendWeight = am.controller.CurrentBlendWeight();
            if (blendClip && blendWeight > 0.0f) {
                std::vector<BoneLocal> blendPose, mixed;
                Animator::SampleLocal(skel, *blendClip, curTime, blendPose);
                Animator::BlendLocal(currentPose, blendPose, blendWeight, mixed);
                currentPose = std::move(mixed);
            }
        }

        std::vector<BoneLocal> local;
        if (am.controller.Blending()) {
            std::vector<BoneLocal> previousPose;
            if (sampleState(am.controller.PrevClip(), am.controller.PrevTime(),
                    am.controller.PreviousBlendSpace(), previousPose)) {
                Animator::BlendLocal(previousPose, currentPose, am.controller.Blend(), local);
            } else {
                local = std::move(currentPose);
            }
        } else {
            local = std::move(currentPose);
        }
        if (am.controller.CurrentRootMotion() && !local.empty() && !skel.bones.empty()) {
            local[0].pos = glm::vec3(skel.bones[0].localBind[3]);
            // Neutralise the root's animated rotation in the mesh: the yaw was transferred
            // to the entity above, so leaving it here too would rotate the character twice.
            glm::mat3 bind(skel.bones[0].localBind);
            bind[0] = glm::normalize(bind[0]);
            bind[1] = glm::normalize(bind[1]);
            bind[2] = glm::normalize(bind[2]);
            local[0].rot = glm::quat_cast(bind);
        }

        // --- Action layer (one-shot, masked, over the base) --------------------
        if (am.action.active) {
            AnimAction& act = am.action;
            const Animation* aclip = clipAt(act.clip);
            const float len = aclip ? ClipSeconds(*aclip) : 0.0f;
            if (!aclip || len <= 0.0f) {
                act.active = false;
            } else {
                act.time += dt * act.speed;

                // Fire events whose time we've now passed.
                while (act.nextEvent < act.events.size() && act.time >= act.events[act.nextEvent].time) {
                    if (am.onEvent) am.onEvent(act.events[act.nextEvent].name);
                    ++act.nextEvent;
                }

                if (act.time >= len) {
                    act.active = false;             // finished
                    act.weight = 0.0f;
                } else {
                    // Weight ramps up over fadeIn and down over fadeOut.
                    const float wIn  = (act.fadeIn  > 0.0f) ? std::min(act.time / act.fadeIn, 1.0f) : 1.0f;
                    const float wOut = (act.fadeOut > 0.0f) ? std::min((len - act.time) / act.fadeOut, 1.0f) : 1.0f;
                    act.weight = std::max(0.0f, std::min(wIn, wOut));

                    std::vector<BoneLocal> layer;
                    Animator::SampleLocal(skel, *aclip, act.time, layer);
                    Animator::LayerLocal(local, layer, act.mask, act.weight);
                }
            }
        }

        // Grounded foot placement (opt-in). Runs on the final local pose so it plants the
        // feet of whatever locomotion/action is playing, then the hierarchy is composed.
        ApplyFootIK(am, transform, skel, local);

        Animator::Compose(skel, local, am.pose);
    });
}

} // namespace engine
