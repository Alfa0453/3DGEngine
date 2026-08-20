#pragma once

// -----------------------------------------------------------------------------
// Editor-side adapter: converts the editor's authored animation graph
// (EditorScene::AnimationStateNode / AnimationParameter / AnimationStateTransition)
// into an engine::AnimationGraphDesc and hands it to the one canonical builder in
// the engine (engine::BuildAnimationController). The graph->controller mapping
// itself lives in the engine now; this file is just a mechanical field copy so the
// editor's authoring types stay independent of the runtime descriptor.
//
// Header-only so no new translation unit / CMake reconfigure is needed. The two
// clip lookups are model-specific and passed through unchanged:
//   resolveClip(fallbackIndex, clipName) -> clip index into the model's animations
//   clipSeconds(clipIndex)               -> that clip's duration in seconds
//   clipBaseSpeed(index, alias)          -> authoritative .3dgclip base rate
// -----------------------------------------------------------------------------

#include "EditorScene.h"

#include <engine/animation/AnimationGraphDesc.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace editor {

// Build a runtime controller from authored editor graph data. Signature is kept
// stable so existing call sites don't change.
inline void BuildAnimationController(
    engine::AnimationController& out,
    const std::vector<EditorScene::AnimationStateNode>& states,
    const std::vector<EditorScene::AnimationParameter>& parameters,
    const std::vector<EditorScene::AnimationStateTransition>& transitions,
    const std::function<int(int, const std::string&)>& resolveClip,
    const std::function<float(int)>& clipSeconds,
    engine::AssetHandle entryStateId = {},
    const std::function<float(int, const std::string&)>& clipBaseSpeed = {}) {

    engine::AnimationGraphDesc desc;

    std::vector<const EditorScene::AnimationStateNode*> orderedStates;
    orderedStates.reserve(states.size());
    if (entryStateId.Valid()) {
        for (const auto& state : states)
            if (state.graphId == entryStateId) orderedStates.push_back(&state);
    }
    for (const auto& state : states)
        if (orderedStates.empty() || orderedStates.front() != &state)
            orderedStates.push_back(&state);

    desc.states.reserve(states.size());
    for (const EditorScene::AnimationStateNode* authored : orderedStates) {
        const EditorScene::AnimationStateNode& s = *authored;
        engine::AnimationGraphDesc::StateDesc d;
        d.name                  = s.name;
        d.clipIndex             = s.clipIndex;
        d.clipName              = s.clipName;
        d.loop                  = s.loop;
        d.speed                 = s.speed;
        d.clipBaseSpeed         = clipBaseSpeed
            ? clipBaseSpeed(s.clipIndex, s.clipName) : 1.0f;
        d.blendClipIndex        = s.blendClipIndex;
        d.blendClipName         = s.blendClipName;
        const bool clipSource = s.motionSourceType
            == EditorScene::AnimationStateNode::MotionSourceType::Clip;
        d.blendParameter        = clipSource ? std::string() : s.blendParameter;
        d.blendMin              = s.blendMin;
        d.blendMax              = s.blendMax;
        d.rootMotion            = s.rootMotion;
        d.blendParameterY       = s.motionSourceType
            == EditorScene::AnimationStateNode::MotionSourceType::BlendSpace2D
            ? s.blendParameterY : std::string();
        d.blendSpace2D          = s.motionSourceType
            == EditorScene::AnimationStateNode::MotionSourceType::BlendSpace2D;
        d.synchronizeBlendSpace = s.synchronizeBlendSpace;
        if (!clipSource) {
            d.blendSamples.reserve(s.blendSamples.size());
            for (const auto& bs : s.blendSamples) {
                const int resolved = resolveClip(bs.clipIndex, bs.clipName);
                d.blendSamples.push_back({bs.clipIndex, bs.clipName, bs.value, bs.valueY,
                    clipBaseSpeed ? clipBaseSpeed(bs.clipIndex, bs.clipName) : 1.0f,
                    clipSeconds(resolved)});
            }
        }
        desc.states.push_back(std::move(d));
    }

    desc.parameters.reserve(parameters.size());
    for (const EditorScene::AnimationParameter& p : parameters) {
        desc.parameters.push_back({
            p.name,
            static_cast<engine::AnimationGraphDesc::ParamDesc::Type>(p.type),
            p.defaultValue
        });
    }

    desc.transitions.reserve(transitions.size());
    for (const EditorScene::AnimationStateTransition& t : transitions) {
        engine::AnimationGraphDesc::TransitionDesc d;
        d.fromState    = t.fromState;
        d.toState      = t.toState;
        d.parameter    = t.parameter;
        d.compare      = static_cast<engine::AnimationGraphDesc::TransitionDesc::Compare>(t.compare);
        d.threshold    = t.threshold;
        d.fade         = t.fade;
        d.exitTime     = t.exitTime;
        d.priority     = t.priority;
        d.canInterrupt = t.canInterrupt;
        d.useConditions = t.useConditions;
        d.requireAllConditions = t.requireAllConditions;
        d.additionalConditions.reserve(t.additionalConditions.size());
        for (const auto& condition : t.additionalConditions) {
            d.additionalConditions.push_back({
                condition.parameter,
                static_cast<engine::AnimationGraphDesc::TransitionDesc::Compare>(
                    condition.compare),
                condition.threshold
            });
        }
        desc.transitions.push_back(std::move(d));
    }

    engine::BuildAnimationController(out, desc, resolveClip, clipSeconds);
}

} // namespace editor
