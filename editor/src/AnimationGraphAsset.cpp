#include "AnimationGraphAsset.h"
#include "AnimationClipAsset.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>

namespace {
std::string OrDash(const std::string& s) { return s.empty() ? std::string("-") : s; }
void Undash(std::string& s) { if (s == "-") s.clear(); }
std::string ClipAlias(const AnimationClipAsset& clip, const std::string& path) {
    std::string alias = clip.name;
    if (alias.empty() || alias == "Clip" || alias == clip.clipName)
        alias = std::filesystem::path(path).stem().string();
    return alias.empty() ? clip.clipName : alias;
}
}  // namespace

bool AnimationGraphAsset::Save(const std::string& path, std::string* error) const {
    std::error_code ec;
    const std::filesystem::path p(path);
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
    std::ofstream out(path, std::ios::trunc);
    if (!out) { if (error) *error = "Could not write graph asset: " + path; return false; }

    out << "3DG_GRAPH " << version << '\n'
        << std::quoted(OrDash(name)) << ' ' << std::quoted(OrDash(previewModel)) << '\n';

    out << "CLIPS " << clips.size() << '\n';
    for (const AnimationGraphClip& c : clips) {
        out << std::quoted(OrDash(c.clipAsset)) << ' ' << std::quoted(OrDash(c.sourceFile)) << ' '
            << std::quoted(OrDash(c.sourceClipName)) << ' ' << std::quoted(OrDash(c.clipName)) << ' '
            << (c.stripRootMotion ? 1 : 0) << '\n';
    }

    out << "GRAPH " << states.size() << ' ' << parameters.size() << ' ' << transitions.size() << '\n';
    for (const auto& state : states) {
        out << std::quoted(state.name) << ' ' << state.clipIndex << ' ' << std::quoted(state.clipName) << ' '
            << state.loop << ' ' << state.speed << ' ' << state.blendClipIndex << ' '
            << std::quoted(state.blendClipName) << ' ' << std::quoted(state.blendParameter) << ' '
            << state.blendMin << ' ' << state.blendMax << ' ' << state.rootMotion << '\n';
    }
    for (const auto& parameter : parameters) {
        out << std::quoted(parameter.name) << ' ' << static_cast<int>(parameter.type) << ' '
            << parameter.defaultValue << '\n';
    }
    for (const auto& transition : transitions) {
        out << std::quoted(transition.fromState) << ' ' << std::quoted(transition.toState) << ' '
            << std::quoted(transition.parameter) << ' ' << static_cast<int>(transition.compare) << ' '
            << transition.threshold << ' ' << transition.fade << ' ' << transition.exitTime << ' '
            << transition.priority << ' ' << transition.canInterrupt << ' '
            << transition.requireAllConditions << ' ' << transition.additionalConditions.size();
        for (const auto& condition : transition.additionalConditions) {
            out << ' ' << std::quoted(condition.parameter) << ' '
                << static_cast<int>(condition.compare) << ' ' << condition.threshold;
        }
        out << '\n';
    }

    out << "BLEND_SPACES " << states.size() << '\n';
    for (const auto& state : states) {
        out << state.blendSpace2D << ' ' << std::quoted(state.blendParameterY) << ' '
            << state.synchronizeBlendSpace << ' ' << state.blendSamples.size();
        for (const auto& sample : state.blendSamples) {
            out << ' ' << sample.clipIndex << ' ' << std::quoted(sample.clipName) << ' '
                << sample.value << ' ' << sample.valueY;
        }
        out << '\n';
    }
    return static_cast<bool>(out);
}

bool AnimationGraphAsset::Load(const std::string& path, std::string* error) {
    std::ifstream in(path);
    std::string magic;
    int loadedVersion = 0;
    if (!(in >> magic >> loadedVersion) || magic != "3DG_GRAPH" || loadedVersion < 1) {
        if (error) *error = "Invalid graph asset: " + path;
        return false;
    }
    clips.clear(); states.clear(); parameters.clear(); transitions.clear();
    in >> std::quoted(name) >> std::quoted(previewModel);
    Undash(name); Undash(previewModel);

    std::string tag;
    std::size_t clipCount = 0;
    if ((in >> tag >> clipCount) && tag == "CLIPS") {
        for (std::size_t i = 0; i < clipCount; ++i) {
            AnimationGraphClip c;
            int strip = 0;
            in >> std::quoted(c.clipAsset) >> std::quoted(c.sourceFile);
            if (loadedVersion >= 2) {
                in >> std::quoted(c.sourceClipName) >> std::quoted(c.clipName) >> strip;
            } else {
                // Version 1 used the source take name as the graph-facing name. Recover
                // the authored .3dgclip name when possible so old graphs containing
                // several "Unreal Take" clips upgrade without ambiguous state choices.
                in >> std::quoted(c.sourceClipName) >> strip;
                AnimationClipAsset clip;
                if (!c.clipAsset.empty() && clip.Load(c.clipAsset, nullptr))
                    c.clipName = ClipAlias(clip, c.clipAsset);
                if (c.clipName.empty())
                    c.clipName = std::filesystem::path(c.clipAsset).stem().string();
                if (c.clipName.empty()) c.clipName = c.sourceClipName;
            }
            Undash(c.clipAsset); Undash(c.sourceFile); Undash(c.sourceClipName); Undash(c.clipName);
            c.stripRootMotion = strip != 0;
            clips.push_back(std::move(c));
        }
    }

    std::size_t stateCount = 0, parameterCount = 0, transitionCount = 0;
    if (!((in >> tag >> stateCount >> parameterCount >> transitionCount) && tag == "GRAPH")) {
        if (error) *error = "Graph asset is incomplete: " + path;
        return false;
    }
    for (std::size_t i = 0; i < stateCount; ++i) {
        EditorScene::AnimationStateNode state;
        in >> std::quoted(state.name) >> state.clipIndex >> std::quoted(state.clipName)
           >> state.loop >> state.speed >> state.blendClipIndex >> std::quoted(state.blendClipName)
           >> std::quoted(state.blendParameter) >> state.blendMin >> state.blendMax >> state.rootMotion;
        states.push_back(std::move(state));
    }
    for (std::size_t i = 0; i < parameterCount; ++i) {
        EditorScene::AnimationParameter parameter; int type = 0;
        in >> std::quoted(parameter.name) >> type >> parameter.defaultValue;
        parameter.type = static_cast<EditorScene::AnimationParameter::Type>(type);
        parameters.push_back(std::move(parameter));
    }
    for (std::size_t i = 0; i < transitionCount; ++i) {
        EditorScene::AnimationStateTransition transition; int compare = 0;
        in >> std::quoted(transition.fromState) >> std::quoted(transition.toState)
           >> std::quoted(transition.parameter) >> compare >> transition.threshold >> transition.fade
           >> transition.exitTime >> transition.priority >> transition.canInterrupt;
        transition.compare = static_cast<EditorScene::AnimationStateTransition::Compare>(compare);
        if (loadedVersion >= 3) {
            std::size_t conditionCount = 0;
            in >> transition.requireAllConditions >> conditionCount;
            for (std::size_t c = 0; c < conditionCount; ++c) {
                EditorScene::AnimationStateTransition::Condition condition;
                int conditionCompare = 0;
                in >> std::quoted(condition.parameter) >> conditionCompare >> condition.threshold;
                condition.compare = static_cast<EditorScene::AnimationStateTransition::Compare>(
                    std::clamp(conditionCompare, 0, 3));
                transition.additionalConditions.push_back(std::move(condition));
            }
        }
        transitions.push_back(std::move(transition));
    }

    std::size_t blendStateCount = 0;
    if ((in >> tag >> blendStateCount) && tag == "BLEND_SPACES" && blendStateCount == states.size()) {
        for (auto& state : states) {
            std::size_t sampleCount = 0;
            in >> state.blendSpace2D >> std::quoted(state.blendParameterY)
               >> state.synchronizeBlendSpace >> sampleCount;
            for (std::size_t i = 0; i < sampleCount; ++i) {
                EditorScene::AnimationStateNode::BlendSample sample;
                in >> sample.clipIndex >> std::quoted(sample.clipName) >> sample.value >> sample.valueY;
                state.blendSamples.push_back(std::move(sample));
            }
        }
    }
    if (loadedVersion < 2) {
        // Finish upgrading old graphs whose separate FBX files all exposed the same
        // generic take name (commonly "Unreal Take"). Make aliases unique and repair
        // the common one-sample-per-added-clip blend-space layout.
        std::vector<std::string> used;
        for (AnimationGraphClip& clip : clips) {
            const std::string base = clip.clipName.empty() ? "Clip" : clip.clipName;
            std::string candidate = base;
            int suffix = 2;
            while (std::find(used.begin(), used.end(), candidate) != used.end())
                candidate = base + " " + std::to_string(suffix++);
            clip.clipName = candidate;
            used.push_back(candidate);
        }
        const auto isAlias = [&](const std::string& name) {
            return std::find(used.begin(), used.end(), name) != used.end();
        };
        for (auto& state : states) {
            if (!state.clipName.empty() && !isAlias(state.clipName) && !clips.empty()) {
                const int index = std::clamp(
                    state.clipIndex, 0, static_cast<int>(clips.size()) - 1);
                state.clipName = clips[static_cast<std::size_t>(index)].clipName;
            }
            const bool sequentialRepair = state.blendSamples.size() <= clips.size()
                && std::all_of(state.blendSamples.begin(), state.blendSamples.end(),
                    [&](const auto& sample) { return !isAlias(sample.clipName); });
            for (std::size_t i = 0; i < state.blendSamples.size(); ++i) {
                auto& sample = state.blendSamples[i];
                if (sequentialRepair) {
                    sample.clipIndex = static_cast<int>(i);
                    sample.clipName = clips[i].clipName;
                } else if (!isAlias(sample.clipName) && !clips.empty()) {
                    const int index = std::clamp(
                        sample.clipIndex, 0, static_cast<int>(clips.size()) - 1);
                    sample.clipName = clips[static_cast<std::size_t>(index)].clipName;
                }
            }
        }
    }
    version = 3;
    return true;   // required GRAPH block loaded; trailing blocks are best-effort
}
