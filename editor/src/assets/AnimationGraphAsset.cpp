#include "AnimationGraphAsset.h"
#include "AnimationClipAsset.h"

#include <engine/assets/AssetReference.h>
#include <engine/assets/AssetRegistry.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <queue>
#include <unordered_set>

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

void AnimationGraphAsset::NormalizeGraphMetadata(bool generateLayout,
                                                 bool migrateLegacyMotionSources) {
    const auto uniqueId = [](const engine::AssetHandle& id,
                             const std::vector<engine::AssetHandle>& used) {
        return id.Valid() && std::find(used.begin(), used.end(), id) == used.end();
    };
    std::vector<engine::AssetHandle> stateIds;
    stateIds.reserve(states.size());
    for (auto& state : states) {
        if (!uniqueId(state.graphId, stateIds)) state.graphId = engine::AssetHandle::Generate();
        stateIds.push_back(state.graphId);
        // Version <= 6 encoded the motion source implicitly in these fields.
        if (migrateLegacyMotionSources
            && state.motionSourceType == EditorScene::AnimationStateNode::MotionSourceType::Clip) {
            if (state.blendSpace2D)
                state.motionSourceType = EditorScene::AnimationStateNode::MotionSourceType::BlendSpace2D;
            else if (!state.blendParameter.empty())
                state.motionSourceType = EditorScene::AnimationStateNode::MotionSourceType::BlendSpace1D;
        }
    }
    if (!entryStateId.Valid()
        || std::find(stateIds.begin(), stateIds.end(), entryStateId) == stateIds.end())
        entryStateId = states.empty() ? engine::AssetHandle{} : states.front().graphId;

    const auto stateByName = [&](const std::string& name) -> const EditorScene::AnimationStateNode* {
        for (const auto& state : states) if (state.name == name) return &state;
        return nullptr;
    };
    const auto stateById = [&](engine::AssetHandle id) -> const EditorScene::AnimationStateNode* {
        for (const auto& state : states) if (state.graphId == id) return &state;
        return nullptr;
    };
    std::vector<engine::AssetHandle> transitionIds;
    transitionIds.reserve(transitions.size());
    for (auto& transition : transitions) {
        if (!uniqueId(transition.graphId, transitionIds))
            transition.graphId = engine::AssetHandle::Generate();
        transitionIds.push_back(transition.graphId);
        if (!transition.fromStateId.Valid() && !transition.fromState.empty()) {
            if (const auto* state = stateByName(transition.fromState))
                transition.fromStateId = state->graphId;
        }
        if (!transition.toStateId.Valid() && !transition.toState.empty()) {
            if (const auto* state = stateByName(transition.toState))
                transition.toStateId = state->graphId;
        }
        if (transition.fromStateId.Valid()) {
            if (const auto* state = stateById(transition.fromStateId)) transition.fromState = state->name;
        } else {
            transition.fromState.clear(); // invalid source is the existing Any State representation
        }
        if (const auto* state = stateById(transition.toStateId)) transition.toState = state->name;
    }

    nodeLayouts.erase(std::remove_if(nodeLayouts.begin(), nodeLayouts.end(),
        [&](const NodeLayout& layout) { return stateById(layout.stateId) == nullptr; }), nodeLayouts.end());
    for (const auto& state : states) {
        const auto found = std::find_if(nodeLayouts.begin(), nodeLayouts.end(),
            [&](const NodeLayout& layout) { return layout.stateId == state.graphId; });
        if (found == nodeLayouts.end()) nodeLayouts.push_back({state.graphId, {}, false});
    }
    if (!generateLayout || states.empty()) return;
    bool needsLayout = true;
    for (const auto& layout : nodeLayouts) {
        if (layout.position.x != 0.0f || layout.position.y != 0.0f) { needsLayout = false; break; }
    }
    if (!needsLayout) return;

    std::vector<int> depth(states.size(), -1);
    int entryIndex = 0;
    for (std::size_t i = 0; i < states.size(); ++i)
        if (states[i].graphId == entryStateId) entryIndex = static_cast<int>(i);
    depth[static_cast<std::size_t>(entryIndex)] = 0;
    std::queue<int> pending;
    pending.push(entryIndex);
    while (!pending.empty()) {
        const int from = pending.front(); pending.pop();
        for (const auto& transition : transitions) {
            if (transition.fromStateId != states[static_cast<std::size_t>(from)].graphId) continue;
            for (std::size_t to = 0; to < states.size(); ++to) {
                if (states[to].graphId == transition.toStateId && depth[to] < 0) {
                    depth[to] = depth[static_cast<std::size_t>(from)] + 1;
                    pending.push(static_cast<int>(to));
                }
            }
        }
    }
    std::vector<int> rows(states.size() + 1, 0);
    int orphanDepth = 1;
    for (int value : depth) orphanDepth = std::max(orphanDepth, value + 1);
    for (std::size_t i = 0; i < states.size(); ++i) {
        if (depth[i] < 0) depth[i] = orphanDepth;
        const int row = rows[static_cast<std::size_t>(depth[i])]++;
        auto found = std::find_if(nodeLayouts.begin(), nodeLayouts.end(),
            [&](const NodeLayout& layout) { return layout.stateId == states[i].graphId; });
        found->position = glm::vec2(40.0f + depth[i] * 250.0f, 35.0f + row * 125.0f);
    }
}

bool AnimationGraphAsset::Save(const std::string& path, std::string* error) {
    if (!assetId.Valid()) assetId = engine::AssetHandle::Generate();
    version = 7;
    NormalizeGraphMetadata(true, false);
    const std::string contentRoot = engine::FindContentRootForAsset(path);
    engine::AssetRegistry registry;
    if (!contentRoot.empty()) {
        std::string ignored;
        registry.Load(
            engine::AssetRegistry::DefaultRegistryPath(contentRoot), &ignored);
        const engine::AssetHandle currentPreview =
            engine::MakeAssetReference(
                &registry, contentRoot, previewModel,
                engine::AssetType::SkeletalMesh).id;
        if (currentPreview.Valid()) previewModelAssetId = currentPreview;
        for (AnimationGraphClip& clip : clips) {
            const engine::AssetHandle currentClip =
                engine::MakeAssetReference(
                    &registry, contentRoot, clip.clipAsset,
                    engine::AssetType::AnimationClip).id;
            if (currentClip.Valid()) clip.clipAssetId = currentClip;
            const engine::AssetHandle currentSource =
                engine::MakeAssetReference(
                    &registry, contentRoot, clip.sourceFile).id;
            if (currentSource.Valid()) clip.sourceAssetId = currentSource;
        }
    }
    std::error_code ec;
    const std::filesystem::path p(path);
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
    std::ofstream out(path, std::ios::trunc);
    if (!out) { if (error) *error = "Could not write graph asset: " + path; return false; }

    out << "3DG_GRAPH " << version << ' ' << assetId.ToString() << '\n'
        << std::quoted(OrDash(name)) << ' ' << std::quoted(OrDash(previewModel))
        << ' ' << (previewModelAssetId.Valid()
            ? previewModelAssetId.ToString() : std::string("-")) << '\n';

    out << "CLIPS " << clips.size() << '\n';
    for (const AnimationGraphClip& c : clips) {
        out << std::quoted(OrDash(c.clipAsset)) << ' ' << std::quoted(OrDash(c.sourceFile)) << ' '
            << std::quoted(OrDash(c.sourceClipName)) << ' ' << std::quoted(OrDash(c.clipName)) << ' '
            << (c.stripRootMotion ? 1 : 0) << ' '
            << (c.clipAssetId.Valid() ? c.clipAssetId.ToString() : std::string("-")) << ' '
            << (c.sourceAssetId.Valid() ? c.sourceAssetId.ToString() : std::string("-"))
            << '\n';
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
            << transition.useConditions << ' '
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
    out << "GRAPH_EDITOR "
        << (entryStateId.Valid() ? entryStateId.ToString() : std::string("-")) << ' '
        << entryNodePosition.x << ' ' << entryNodePosition.y << ' '
        << anyStateNodePosition.x << ' ' << anyStateNodePosition.y << '\n';
    out << "STATE_METADATA " << states.size() << '\n';
    for (const auto& state : states) {
        const auto layout = std::find_if(nodeLayouts.begin(), nodeLayouts.end(),
            [&](const NodeLayout& item) { return item.stateId == state.graphId; });
        const glm::vec2 position = layout == nodeLayouts.end() ? glm::vec2(0.0f) : layout->position;
        const bool collapsed = layout != nodeLayouts.end() && layout->collapsed;
        out << state.graphId.ToString() << ' '
            << static_cast<int>(state.motionSourceType) << ' '
            << position.x << ' ' << position.y << ' ' << collapsed << '\n';
    }
    out << "TRANSITION_IDS " << transitions.size() << '\n';
    for (const auto& transition : transitions) {
        out << transition.graphId.ToString() << ' '
            << (transition.fromStateId.Valid() ? transition.fromStateId.ToString() : std::string("-")) << ' '
            << (transition.toStateId.Valid() ? transition.toStateId.ToString() : std::string("-")) << '\n';
    }
    std::vector<engine::AssetHandle> dependencies;
    const auto addDependency = [&](engine::AssetHandle id) {
        if (id.Valid()
            && std::find(dependencies.begin(), dependencies.end(), id)
                   == dependencies.end())
            dependencies.push_back(id);
    };
    addDependency(previewModelAssetId);
    for (const AnimationGraphClip& clip : clips) {
        addDependency(clip.clipAssetId);
        addDependency(clip.sourceAssetId);
    }
    out << "ASSET_DEPS " << dependencies.size();
    for (engine::AssetHandle id : dependencies) out << ' ' << id.ToString();
    out << '\n';
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
    assetId = {};
    if (loadedVersion >= 5) {
        std::string id;
        in >> id;
        if (!engine::AssetHandle::Parse(id, &assetId)) {
            if (error) *error = "Graph asset has an invalid stable ID: " + path;
            return false;
        }
    }
    clips.clear(); states.clear(); parameters.clear(); transitions.clear();
    entryStateId = {};
    entryNodePosition = {-260.0f, 20.0f};
    anyStateNodePosition = {-260.0f, 170.0f};
    nodeLayouts.clear();
    in >> std::quoted(name) >> std::quoted(previewModel);
    previewModelAssetId = {};
    if (loadedVersion >= 5) {
        std::string id;
        in >> id;
        if (id != "-" && !engine::AssetHandle::Parse(id, &previewModelAssetId)) {
            if (error) *error = "Graph preview model identity is invalid: " + path;
            return false;
        }
    }
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
            if (loadedVersion >= 5) {
                std::string clipId;
                std::string sourceId;
                in >> clipId >> sourceId;
                if ((clipId != "-"
                        && !engine::AssetHandle::Parse(
                            clipId, &c.clipAssetId))
                    || (sourceId != "-"
                        && !engine::AssetHandle::Parse(
                            sourceId, &c.sourceAssetId))) {
                    if (error) *error =
                        "Graph clip contains an invalid asset identity: " + path;
                    return false;
                }
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
            if (loadedVersion >= 6) {
                in >> transition.useConditions;
            } else {
                // Existing graphs always behave as condition-driven.
                transition.useConditions = true;
            }
            in >> transition.requireAllConditions >> conditionCount;
            for (std::size_t c = 0; c < conditionCount; ++c) {
                EditorScene::AnimationStateTransition::Condition condition;
                int conditionCompare = 0;
                in >> std::quoted(condition.parameter) >> conditionCompare >> condition.threshold;
                condition.compare = static_cast<EditorScene::AnimationStateTransition::Compare>(
                    std::clamp(conditionCompare, 0, 5));
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
    if (loadedVersion >= 7) {
        std::string entryId;
        if (!(in >> tag >> entryId >> entryNodePosition.x >> entryNodePosition.y
              >> anyStateNodePosition.x >> anyStateNodePosition.y)
            || tag != "GRAPH_EDITOR"
            || (entryId != "-" && !engine::AssetHandle::Parse(entryId, &entryStateId))) {
            if (error) *error = "Graph editor metadata is invalid: " + path;
            return false;
        }
        std::size_t metadataCount = 0;
        if (!(in >> tag >> metadataCount) || tag != "STATE_METADATA"
            || metadataCount != states.size()) {
            if (error) *error = "Graph state metadata is incomplete: " + path;
            return false;
        }
        for (std::size_t i = 0; i < states.size(); ++i) {
            std::string stateId;
            int sourceType = 0;
            NodeLayout layout;
            if (!(in >> stateId >> sourceType >> layout.position.x >> layout.position.y
                  >> layout.collapsed)
                || !engine::AssetHandle::Parse(stateId, &states[i].graphId)) {
                if (error) *error = "Graph contains invalid state metadata: " + path;
                return false;
            }
            states[i].motionSourceType =
                static_cast<EditorScene::AnimationStateNode::MotionSourceType>(
                    std::clamp(sourceType, 0, 2));
            layout.stateId = states[i].graphId;
            nodeLayouts.push_back(layout);
        }
        std::size_t transitionIdCount = 0;
        if (!(in >> tag >> transitionIdCount) || tag != "TRANSITION_IDS"
            || transitionIdCount != transitions.size()) {
            if (error) *error = "Graph transition metadata is incomplete: " + path;
            return false;
        }
        for (std::size_t i = 0; i < transitions.size(); ++i) {
            std::string transitionId, fromId, toId;
            if (!(in >> transitionId >> fromId >> toId)
                || !engine::AssetHandle::Parse(transitionId, &transitions[i].graphId)
                || (fromId != "-" && !engine::AssetHandle::Parse(fromId, &transitions[i].fromStateId))
                || (toId != "-" && !engine::AssetHandle::Parse(toId, &transitions[i].toStateId))) {
                if (error) *error = "Graph contains invalid transition metadata: " + path;
                return false;
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
    version = 7;
    NormalizeGraphMetadata(true, loadedVersion < 7);
    // Runtime controllers intentionally still begin in their first state. Keep the
    // explicit entry state first in the authoring array when handing graph data to
    // legacy scene/character paths that do not carry graph-level metadata.
    const auto entry = std::find_if(states.begin(), states.end(),
        [&](const auto& state) { return state.graphId == entryStateId; });
    if (entry != states.end() && entry != states.begin())
        std::rotate(states.begin(), entry, entry + 1);
    const std::string contentRoot = engine::FindContentRootForAsset(path);
    engine::AssetRegistry registry;
    std::string ignored;
    if (!contentRoot.empty()
        && registry.Load(
            engine::AssetRegistry::DefaultRegistryPath(contentRoot), &ignored)) {
        if (previewModelAssetId.Valid()) {
            const std::string resolved = engine::ResolveAssetReference(
                &registry, contentRoot,
                {previewModelAssetId, previewModel},
                engine::AssetType::SkeletalMesh);
            if (!resolved.empty()) previewModel = resolved;
        }
        for (AnimationGraphClip& clip : clips) {
            if (clip.clipAssetId.Valid()) {
                const std::string resolved = engine::ResolveAssetReference(
                    &registry, contentRoot,
                    {clip.clipAssetId, clip.clipAsset},
                    engine::AssetType::AnimationClip);
                if (!resolved.empty()) clip.clipAsset = resolved;
            }
            if (clip.sourceAssetId.Valid()) {
                const std::string resolved = engine::ResolveAssetReference(
                    &registry, contentRoot,
                    {clip.sourceAssetId, clip.sourceFile});
                if (!resolved.empty()) clip.sourceFile = resolved;
            }
        }
    }
    return true;   // required GRAPH block loaded; trailing blocks are best-effort
}
