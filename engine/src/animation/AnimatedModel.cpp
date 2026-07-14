#include "engine/animation/AnimatedModel.h"

#include "engine/animation/Animator.h"
#include "engine/graphics/SkinnedModel.h"
#include "engine/ecs/Registry.h"
#include "engine/ecs/Components.h"

#include <algorithm>
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
    reg.view<ecs::Transform, AnimatedModel>().each([&](ecs::Entity, ecs::Transform&, AnimatedModel& am) {
        if (!am.model) return;
        const Skeleton& skel = am.model->GetSkeleton();
        const auto& clips = am.model->Animations();

        if (clips.empty()) {                       // no animation -> bind pose
            Animator::ComputeBindPose(skel, am.pose);
            return;
        }

        const int previousBaseClip = am.controller.CurrentClip();
        const float previousBaseTime = am.controller.CurrentTime();
        am.controller.Update(dt);

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

        // --- Base locomotion pose (single clip, or a cross-fade of two) ---------
        const Animation* cur = clipAt(am.controller.CurrentClip());
        if (!cur) { Animator::ComputeBindPose(skel, am.pose); return; }

        float curTime = am.controller.CurrentTime();
        if (!am.controller.CurrentLoop()) {
            const float len = ClipSeconds(*cur);
            if (len > 0.0f) curTime = std::min(curTime, len - 1e-4f);
        }

        std::vector<BoneLocal> local;
        const Animation* prev = am.controller.Blending() ? clipAt(am.controller.PrevClip()) : nullptr;
        if (prev) {
            std::vector<BoneLocal> a, b;
            Animator::SampleLocal(skel, *prev, am.controller.PrevTime(), a);
            Animator::SampleLocal(skel, *cur, curTime, b);
            Animator::BlendLocal(a, b, am.controller.Blend(), local);
        } else {
            Animator::SampleLocal(skel, *cur, curTime, local);
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
