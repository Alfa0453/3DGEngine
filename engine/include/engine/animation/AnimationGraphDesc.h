#pragma once

// -----------------------------------------------------------------------------
// A serializable, model-agnostic description of an animation state graph, and the
// single canonical routine that turns one into a runtime AnimationController.
//
// This is the engine-side source of truth for "what an animation graph is". The
// editor authors graphs (as EditorScene types) and converts them into this
// descriptor; the runtime/player can also build one directly from data. Either
// way the mapping into a controller (clamps, infinite auto-ranges, blend spaces,
// clip-name resolution) lives here once, not copy-pasted per call site.
//
// Clips are referenced by name plus a fallback index so a graph survives clip
// reordering; the two lookups are model-specific and passed in at build time:
//   resolveClip(fallbackIndex, clipName) -> clip index into the model's animations
//   clipDuration(clipIndex)              -> that clip's length in seconds
//
// Header-only (all inline) so no new translation unit / CMake reconfigure.
// -----------------------------------------------------------------------------

#include "engine/animation/AnimationController.h"

#include <algorithm>
#include <functional>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine {

struct AnimationGraphDesc {
    struct BlendSample {
        int         clipIndex = 0;
        std::string clipName;
        float       value  = 0.0f;
        float       valueY = 0.0f;
    };

    struct StateDesc {
        std::string name = "State";
        int         clipIndex = 0;
        std::string clipName;
        bool        loop  = true;
        float       speed = 1.0f;
        int         blendClipIndex = -1;
        std::string blendClipName;
        std::string blendParameter;
        float       blendMin = 0.0f;
        float       blendMax = 1.0f;
        bool        rootMotion = false;
        std::vector<BlendSample> blendSamples;
        std::string blendParameterY;
        bool        blendSpace2D = false;
        bool        synchronizeBlendSpace = true;
    };

    struct ParamDesc {
        enum class Type { Float = 0, Bool = 1, Trigger = 2 };
        std::string name = "Speed";
        Type        type = Type::Float;
        float       defaultValue = 0.0f;
    };

    struct TransitionDesc {
        enum class Compare { GreaterOrEqual = 0, Less = 1, Equal = 2, NotEqual = 3 };
        std::string fromState;            // empty = "any state"
        std::string toState;
        std::string parameter = "Speed";
        Compare     compare = Compare::GreaterOrEqual;
        float       threshold = 0.0f;
        float       fade = 0.2f;
        float       exitTime = 0.0f;
        int         priority = 0;
        bool        canInterrupt = false;
    };

    std::vector<StateDesc>      states;
    std::vector<ParamDesc>      parameters;
    std::vector<TransitionDesc> transitions;
};

// Populate `out` (expected empty) from a graph descriptor. This is the one place
// the graph->controller mapping is defined.
inline void BuildAnimationController(
    AnimationController& out,
    const AnimationGraphDesc& desc,
    const std::function<int(int, const std::string&)>& resolveClip,
    const std::function<float(int)>& clipDuration) {

    std::unordered_map<std::string, int> stateIndices;
    for (const AnimationGraphDesc::StateDesc& node : desc.states) {
        const int clip = resolveClip(node.clipIndex, node.clipName);
        const int index = out.AddState(AnimationController::State{
            node.name.empty() ? std::string("State") : node.name,
            clip,
            node.loop,
            std::max(node.speed, 0.0f),
            -std::numeric_limits<float>::infinity(),
            std::numeric_limits<float>::infinity(),
            clipDuration(clip),
            node.blendClipIndex >= 0
                ? resolveClip(node.blendClipIndex, node.blendClipName)
                : -1,
            node.blendParameter,
            node.blendMin,
            node.blendMax,
            node.rootMotion
        });
        for (const AnimationGraphDesc::BlendSample& sample : node.blendSamples) {
            out.AddBlendSample(index,
                {resolveClip(sample.clipIndex, sample.clipName), sample.value, sample.valueY});
        }
        out.ConfigureBlendSpace(index, node.blendParameterY,
            node.blendSpace2D, node.synchronizeBlendSpace);
        stateIndices[node.name] = index;
    }

    for (const AnimationGraphDesc::ParamDesc& parameter : desc.parameters) {
        out.DeclareParameter({
            parameter.name,
            static_cast<AnimationController::ParameterType>(parameter.type),
            parameter.defaultValue
        });
    }

    for (const AnimationGraphDesc::TransitionDesc& transition : desc.transitions) {
        const auto from = stateIndices.find(transition.fromState);
        const auto to   = stateIndices.find(transition.toState);
        if ((!transition.fromState.empty() && from == stateIndices.end())
            || to == stateIndices.end()) {
            continue;
        }
        out.AddTransition(AnimationController::Transition{
            transition.fromState.empty() ? -1 : from->second,
            to->second,
            transition.parameter,
            static_cast<AnimationController::Transition::Compare>(
                std::clamp(static_cast<int>(transition.compare), 0, 3)),
            transition.threshold,
            std::max(transition.fade, 0.0f),
            std::clamp(transition.exitTime, 0.0f, 1.0f),
            transition.priority,
            transition.canInterrupt
        });
    }
}

} // namespace engine
