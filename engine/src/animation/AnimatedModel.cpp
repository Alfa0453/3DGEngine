#include "engine/animation/AnimatedModel.h"

#include "engine/animation/Animator.h"
#include "engine/graphics/SkinnedModel.h"
#include "engine/ecs/Registry.h"
#include "engine/ecs/Components.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace engine {

namespace {
// Duration of a clip in seconds (0 if empty).
float ClipSeconds(const Animation& a) {
    const float tps = (a.ticksPerSecond > 0.0f) ? a.ticksPerSecond : 25.0f;
    return (a.duration > 0.0f) ? a.duration / tps : 0.0f;
}
} // namespace

void AnimatedModel::PlayAction(int clip, std::vector<float> mask, std::vector<AnimEvent> events,
                               float fadeIn, float fadeOut, float speed) {
    action.clip      = clip;
    action.time      = 0.0f;
    action.weight    = 0.0f;
    action.fadeIn    = fadeIn;
    action.fadeOut   = fadeOut;
    action.speed     = speed;
    action.active    = (clip >= 0);
    action.mask      = std::move(mask);
    action.events    = std::move(events);
    action.nextEvent = 0;
}

void UpdateAnimations(ecs::Registry& reg, float dt) {
    reg.view<ecs::Transform, AnimatedModel>().each([&](ecs::Entity, ecs::Transform& transform, AnimatedModel& am) {
        if (!am.model) return;
        const Skeleton& skel = am.model->GetSkeleton();
        const auto& clips = am.model->Animations();

        if (clips.empty()) {                       // no animation -> bind pose
            Animator::ComputeBindPose(skel, am.pose);
            return;
        }

        const int previousBaseClip = am.controller.CurrentClip();
        const float previousBaseTime = am.controller.CurrentTime();
        const std::string previousState = am.controller.CurrentStateName();
        am.controller.Update(dt);
        const std::string currentState = am.controller.CurrentStateName();
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
            const float currentBaseTime = am.controller.CurrentTime();
            for (const AnimEvent& event : am.events) {
                if (event.name.empty() || (event.clip >= 0 && event.clip != currentBaseClip)) {
                    continue;
                }
                const bool sameClip = previousBaseClip == currentBaseClip;
                bool crossed = false;
                if (!sameClip) {
                    crossed = event.time <= currentBaseTime;
                } else if (clipLength > 0.0f && currentBaseTime < previousBaseTime) {
                    crossed = event.time > previousBaseTime || event.time <= currentBaseTime;
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
        if (!cur) { Animator::ComputeBindPose(skel, am.pose); return; }

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
                    float beforeTime = previousBaseTime, afterTime = am.controller.CurrentTime();
                    const float sampleLength = ClipSeconds(*animation);
                    if (rootSpace.synchronized && referenceLength > 0.0001f && sampleLength > 0.0001f) {
                        beforeTime = previousBaseTime * sampleLength / referenceLength;
                        afterTime = am.controller.CurrentTime() * sampleLength / referenceLength;
                    }
                    delta += rootDelta(*animation, beforeTime, afterTime) * sample.weight;
                }
            } else {
                delta = rootDelta(*cur, previousBaseTime, am.controller.CurrentTime());
            }
            transform.position += transform.rotation * delta;
        }

        float curTime = am.controller.CurrentTime();
        if (!am.controller.CurrentLoop()) {
            const float len = ClipSeconds(*cur);
            if (len > 0.0f) curTime = std::min(curTime, len - 1e-4f);
        }

        auto sampleState = [&](int fallbackClip, float time,
                               const AnimationController::BlendSpaceResult& space,
                               std::vector<BoneLocal>& out) {
            const Animation* reference = clipAt(fallbackClip);
            auto synchronizedTime = [&](const Animation* sample) {
                if (!space.synchronized || !reference || !sample) return time;
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
                    Animator::SampleLocal(skel, *clip, synchronizedTime(clip), pose);
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

        Animator::Compose(skel, local, am.pose);
    });
}

} // namespace engine
