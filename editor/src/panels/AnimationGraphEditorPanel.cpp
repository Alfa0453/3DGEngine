#include "AnimationGraphEditorPanel.h"

#include "AnimationClipAsset.h"
#include "AnimationGraphBuilder.h"
#include "EditorPanels.h"

#include <engine/animation/Animator.h>
#include <engine/assets/SkeletalAsset.h>
#include <engine/graphics/Camera.h>
#include <engine/graphics/SkinnedModel.h>
#include <engine/graphics/SkinnedRenderer.h>

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <cmath>

namespace {
std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}
template <std::size_t N> void Copy(std::array<char, N>& dst, const std::string& value) {
    std::fill(dst.begin(), dst.end(), '\0');
    std::memcpy(dst.data(), value.data(), std::min(value.size(), N - 1));
}
float ClipSeconds(const engine::Animation& a) {
    return engine::AnimationPlaybackSeconds(a);
}
std::string UniqueClipAlias(const std::vector<AnimationGraphClip>& clips,
                            std::string desired) {
    if (desired.empty()) desired = "Clip";
    const std::string base = desired;
    int suffix = 2;
    const auto exists = [&](const std::string& candidate) {
        const std::string lowered = Lower(candidate);
        return std::any_of(clips.begin(), clips.end(), [&](const AnimationGraphClip& c) {
            return Lower(c.clipName) == lowered;
        });
    };
    while (exists(desired)) desired = base + " " + std::to_string(suffix++);
    return desired;
}
// Like UniqueClipAlias, but ignore the clip at `skipIndex` (used when renaming a clip
// in place so it doesn't collide with its own current name).
std::string UniqueClipAliasExcept(const std::vector<AnimationGraphClip>& clips,
                                  std::size_t skipIndex, std::string desired) {
    if (desired.empty()) desired = "Clip";
    const std::string base = desired;
    int suffix = 2;
    const auto exists = [&](const std::string& candidate) {
        const std::string lowered = Lower(candidate);
        for (std::size_t i = 0; i < clips.size(); ++i) {
            if (i == skipIndex) continue;
            if (Lower(clips[i].clipName) == lowered) return true;
        }
        return false;
    };
    while (exists(desired)) desired = base + " " + std::to_string(suffix++);
    return desired;
}
const char* MotionSourceName(EditorScene::AnimationStateNode::MotionSourceType type) {
    using Type = EditorScene::AnimationStateNode::MotionSourceType;
    switch (type) {
    case Type::Clip: return "Animation Clip";
    case Type::BlendSpace1D: return "1D Blend Space";
    case Type::BlendSpace2D: return "2D Blend Space";
    }
    return "Animation Clip";
}
float DistanceToSegment(ImVec2 p, ImVec2 a, ImVec2 b) {
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float length2 = dx * dx + dy * dy;
    if (length2 <= 0.0001f) return std::hypot(p.x - a.x, p.y - a.y);
    const float t = std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / length2, 0.0f, 1.0f);
    return std::hypot(p.x - (a.x + dx * t), p.y - (a.y + dy * t));
}

// A single authoring problem found in the graph. severity: 0=error, 1=warning, 2=info.
struct GraphIssue { int severity = 2; std::string text; };

// Static analysis of the authored graph so the editor can surface the traps that made
// locomotion misbehave: unresolved clip aliases, always-true transitions, unknown states
// / parameters, and unreachable / dead-end states.
std::vector<GraphIssue> ValidateGraph(const AnimationGraphAsset& asset) {
    std::vector<GraphIssue> issues;
    const auto add = [&](int severity, const std::string& text) {
        issues.push_back(GraphIssue{severity, text});
    };
    if (asset.states.empty()) {
        add(2, "No states yet - add a state, or use Create 1D / 2D Locomotion.");
        return issues;
    }
    using PType = EditorScene::AnimationParameter::Type;
    using Comp = EditorScene::AnimationStateTransition::Compare;
    const auto isClip = [&](const std::string& n) {
        return !n.empty() && std::any_of(asset.clips.begin(), asset.clips.end(),
            [&](const AnimationGraphClip& c) { return c.clipName == n; });
    };
    const auto isState = [&](const std::string& n) {
        return std::any_of(asset.states.begin(), asset.states.end(),
            [&](const EditorScene::AnimationStateNode& s) { return s.name == n; });
    };
    const auto paramType = [&](const std::string& n, PType& out) {
        for (const EditorScene::AnimationParameter& p : asset.parameters)
            if (p.name == n) { out = p.type; return true; }
        return false;
    };
    const auto stateLabel = [](const std::string& n) {
        return n.empty() ? std::string("(unnamed)") : n;
    };

    std::vector<engine::AssetHandle> ids;
    std::vector<std::string> names;
    for (const auto& state : asset.states) {
        if (!state.graphId.Valid()) add(0, "State '" + stateLabel(state.name) + "' has no stable ID.");
        else if (std::find(ids.begin(), ids.end(), state.graphId) != ids.end())
            add(0, "Duplicate stable state ID on '" + stateLabel(state.name) + "'.");
        ids.push_back(state.graphId);
        if (state.name.empty()) add(0, "State names cannot be empty.");
        else if (std::find(names.begin(), names.end(), state.name) != names.end())
            add(0, "Duplicate state name '" + state.name + "'.");
        names.push_back(state.name);
    }
    if (!asset.entryStateId.Valid()) add(0, "The graph has no Entry state.");
    else if (std::find(ids.begin(), ids.end(), asset.entryStateId) == ids.end())
        add(0, "The Entry connection references a missing state.");

    std::vector<std::string> parameterNames;
    for (const auto& parameter : asset.parameters) {
        if (parameter.name.empty()) add(0, "Parameter names cannot be empty.");
        else if (std::find(parameterNames.begin(), parameterNames.end(), parameter.name)
                 != parameterNames.end())
            add(0, "Duplicate parameter name '" + parameter.name + "'.");
        parameterNames.push_back(parameter.name);
    }

    // States: clip resolution + blend-space parameters.
    for (const EditorScene::AnimationStateNode& s : asset.states) {
        const std::string sn = stateLabel(s.name);
        if (s.motionSourceType == EditorScene::AnimationStateNode::MotionSourceType::Clip) {
            if (!s.clipName.empty() && !isClip(s.clipName))
                add(0, "State '" + sn + "': clip '" + s.clipName + "' is not in the clip list.");
            else if (s.clipName.empty())
                add(1, "State '" + sn + "' has no clip assigned.");
        } else {
            if (s.blendSamples.empty()) add(1, "State '" + sn + "' has a Blend Space with no samples.");
            for (const auto& sample : s.blendSamples)
                if (!sample.clipName.empty() && !isClip(sample.clipName))
                    add(0, "State '" + sn + "': blend clip '" + sample.clipName
                           + "' is not in the clip list.");
            PType t;
            if (s.blendParameter.empty())
                add(1, "State '" + sn + "' is a blend space but has no X parameter.");
            else if (!paramType(s.blendParameter, t))
                add(1, "State '" + sn + "': blend parameter '" + s.blendParameter
                       + "' is not declared.");
            if (s.motionSourceType == EditorScene::AnimationStateNode::MotionSourceType::BlendSpace2D) {
                if (s.blendParameterY.empty()) add(1, "State '" + sn + "' has no Y parameter.");
                else if (!paramType(s.blendParameterY, t))
                    add(1, "State '" + sn + "': Y parameter '" + s.blendParameterY + "' is not declared.");
            }
        }
    }

    // Transitions: valid endpoints, declared parameters, and the always-true footgun.
    std::vector<engine::AssetHandle> transitionIds;
    for (const auto& tr : asset.transitions) {
        const std::string label = "Transition "
            + (tr.fromState.empty() ? std::string("Any") : tr.fromState) + " -> "
            + (tr.toState.empty() ? std::string("(none)") : tr.toState);
        if (!tr.graphId.Valid()) add(0, label + " has no stable ID.");
        else if (std::find(transitionIds.begin(), transitionIds.end(), tr.graphId)
                 != transitionIds.end())
            add(0, label + " has a duplicate stable transition ID.");
        transitionIds.push_back(tr.graphId);
        if (tr.fromStateId.Valid()
            && std::find(ids.begin(), ids.end(), tr.fromStateId) == ids.end())
            add(0, label + ": source ID references a missing state.");
        if (!tr.toStateId.Valid()
            || std::find(ids.begin(), ids.end(), tr.toStateId) == ids.end())
            add(0, label + ": target ID references a missing state.");
        if (!tr.fromState.empty() && !isState(tr.fromState))
            add(0, label + ": 'from' state does not exist.");
        if (tr.toState.empty() || !isState(tr.toState))
            add(0, label + ": 'to' state does not exist.");

        const auto checkParam = [&](const std::string& p) {
            PType t;
            if (!p.empty() && !paramType(p, t))
                add(1, label + " uses undeclared parameter '" + p + "'.");
        };
        if (tr.useConditions) {
            checkParam(tr.parameter);
            for (const auto& c : tr.additionalConditions) checkParam(c.parameter);
        }

        // Only flag "always true" when nothing else gates the transition.
        if (tr.useConditions && tr.additionalConditions.empty()) {
            if (tr.parameter.empty()) {
                add(1, label + " has no condition - it fires immediately.");
            } else {
                PType t;
                if (paramType(tr.parameter, t) && t == PType::Float
                    && static_cast<int>(tr.compare) == static_cast<int>(Comp::GreaterOrEqual)
                    && tr.threshold <= 0.0f)
                    add(1, label + " is always true (" + tr.parameter
                           + " >= 0). Give it a threshold or use '>'.");
            }
        }
    }

    // Reachability follows actual states only; Blend Space samples are pose inputs,
    // never state-machine nodes. Any State edges make their targets reachable.
    std::vector<engine::AssetHandle> reachable;
    if (asset.entryStateId.Valid()) reachable.push_back(asset.entryStateId);
    for (const auto& transition : asset.transitions)
        if (!transition.fromStateId.Valid() && transition.toStateId.Valid())
            reachable.push_back(transition.toStateId);
    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto& transition : asset.transitions) {
            if (std::find(reachable.begin(), reachable.end(), transition.fromStateId) == reachable.end()) continue;
            if (std::find(reachable.begin(), reachable.end(), transition.toStateId) == reachable.end()) {
                reachable.push_back(transition.toStateId); changed = true;
            }
        }
    }
    for (const auto& state : asset.states) {
        if (std::find(reachable.begin(), reachable.end(), state.graphId) == reachable.end())
            add(2, "State '" + stateLabel(state.name) + "' is unreachable from Entry.");
        const bool outgoing = std::any_of(asset.transitions.begin(), asset.transitions.end(),
            [&](const auto& transition) { return transition.fromStateId == state.graphId; });
        if (!outgoing) add(2, "State '" + stateLabel(state.name) + "' has no outgoing transition (dead end).");
    }
    return issues;
}

// Propagate a clip rename to everything that references the clip by name, so existing
// states / blend samples / actions keep pointing at the same animation.
void RenameClipReferences(AnimationGraphAsset& asset,
                          const std::string& oldName, const std::string& newName) {
    if (oldName.empty() || oldName == newName) return;
    for (EditorScene::AnimationStateNode& s : asset.states) {
        if (s.clipName == oldName) s.clipName = newName;
        if (s.blendClipName == oldName) s.blendClipName = newName;
        for (EditorScene::AnimationStateNode::BlendSample& bs : s.blendSamples)
            if (bs.clipName == oldName) bs.clipName = newName;
    }
    for (EditorScene::AnimationActionProfile& a : asset.actions)
        if (a.clipName == oldName) a.clipName = newName;
}
}  // namespace

AnimationGraphEditorPanel::~AnimationGraphEditorPanel() = default;

void AnimationGraphEditorPanel::QueueOpen(const std::string& path) { m_pendingOpen = path; }
void AnimationGraphEditorPanel::SyncBuffers() { Copy(m_nameBuffer, m_asset.name); }

bool AnimationGraphEditorPanel::SaveForShutdown(std::string* error) {
    m_asset.name = m_nameBuffer.data();
    if (!m_asset.Save(m_path, error)) return false;
    m_dirty = false;
    return true;
}

void AnimationGraphEditorPanel::ResetPreview() {
    m_model = nullptr;
    m_loadedSignature.clear();
    m_pose.clear();
    m_controllerDirty = true;
}

void AnimationGraphEditorPanel::RefreshChoices(const std::string& assetRoot) {
    m_modelChoices.clear();
    m_clipChoices.clear();
    m_scannedRoot = assetRoot;
    std::error_code ec;
    const std::filesystem::path root(assetRoot);
    if (!std::filesystem::exists(root, ec)) return;
    std::filesystem::recursive_directory_iterator it(
        root, std::filesystem::directory_options::skip_permission_denied, ec), end;
    while (!ec && it != end) {
        std::error_code fileEc;
        if (it->is_regular_file(fileEc)) {
            const std::string ext = Lower(it->path().extension().string());
            AssetChoice choice{it->path().generic_string(), it->path().filename().string()};
            // Native engine-imported sources (.3dgskmesh / .3dganim) instead of raw models.
            if (ext == ".3dgskmesh" || ext == ".3dganim")
                m_modelChoices.push_back(std::move(choice));
            else if (ext == ".3dgclip")
                m_clipChoices.push_back(std::move(choice));
        }
        it.increment(ec);
    }
    const auto byName = [](const AssetChoice& a, const AssetChoice& b) { return Lower(a.displayName) < Lower(b.displayName); };
    std::sort(m_modelChoices.begin(), m_modelChoices.end(), byName);
    std::sort(m_clipChoices.begin(), m_clipChoices.end(), byName);
}

unsigned int AnimationGraphEditorPanel::RenderPreview(int width, int height, float deltaTime) {
    width = std::max(width, 32);
    height = std::max(height, 32);
    if (!m_fbo) m_fbo.emplace(width, height, GL_RGBA8, true);
    else m_fbo->Resize(width, height);
    if (!m_renderer) m_renderer = std::make_unique<engine::SkinnedRenderer>();

    // Reload the preview rig with the graph's clips merged when either changes.
    std::string signature = m_asset.previewModel;
    std::string metadataSignature;
    for (const AnimationGraphClip& c : m_asset.clips) {
        signature += '|' + c.sourceFile + '#' + c.sourceClipName + "->" + c.clipName
            + (c.stripRootMotion ? "1" : "0");
        // Dependency timestamp invalidates controller timing without making the
        // graph itself dirty or forcing clips to be re-added.
        std::error_code stampError;
        const auto stamp = std::filesystem::last_write_time(c.clipAsset, stampError);
        if (!stampError) metadataSignature += c.clipAsset + '@'
            + std::to_string(stamp.time_since_epoch().count());
    }
    if (metadataSignature != m_clipMetadataSignature) {
        m_clipMetadataSignature = std::move(metadataSignature);
        m_controllerDirty = true;
    }
    signature += '|' + m_clipMetadataSignature;
    if (signature != m_loadedSignature) {
        ResetPreview();
        m_loadedSignature = signature;
        m_error.clear();
        if (!m_asset.previewModel.empty()) {
            std::vector<engine::RuntimeAssetManager::SkinnedAnimationSource> sources;
            for (const AnimationGraphClip& c : m_asset.clips) {
                if (c.sourceFile.empty()) continue;
                AnimationClipAsset clip;
                const bool loadedClip = clip.Load(c.clipAsset, nullptr);
                std::vector<engine::AnimationCurve> curves = c.curves;
                if (loadedClip) {
                    curves.clear();
                    for (const AnimationClipAsset::Curve& curve : clip.curves) {
                        engine::AnimationCurve runtimeCurve;
                        runtimeCurve.name = curve.name;
                        for (const AnimationClipAsset::CurveKey& key : curve.keys)
                            runtimeCurve.keys.push_back({key.time, key.value});
                        curves.push_back(std::move(runtimeCurve));
                    }
                }
                sources.push_back({
                    c.sourceFile, c.clipName, c.stripRootMotion, c.sourceClipName,
                    loadedClip ? std::max(clip.speed, 0.0f) : 1.0f,
                    loadedClip ? clip.playbackStart : c.playbackStart,
                    loadedClip ? clip.playbackEnd : c.playbackEnd,
                    loadedClip ? clip.additive : c.additive,
                    loadedClip ? clip.additiveReferenceTime : c.additiveReferenceTime,
                    std::move(curves)});
            }
            m_model = sources.empty()
                ? m_assets.LoadSkinnedModel(m_asset.previewModel, &m_error)
                : m_assets.LoadSkinnedModel(m_asset.previewModel, sources, &m_error);
            m_controllerDirty = true;
        }
    }

    if (m_model && m_controllerDirty) {
        m_controller = {};
        const auto& anims = m_model->Animations();
        auto resolveClip = [&](int fallback, const std::string& name) {
            int clip = fallback;
            if (!name.empty())
                for (std::size_t i = 0; i < anims.size(); ++i)
                    if (anims[i].name == name) { clip = static_cast<int>(i); break; }
            return std::clamp(clip, 0, std::max(0, static_cast<int>(anims.size()) - 1));
        };
        auto clipSeconds = [&](int clip) {
            return (clip >= 0 && clip < static_cast<int>(anims.size())) ? ClipSeconds(anims[static_cast<std::size_t>(clip)]) : 0.0f;
        };
        auto clipBaseSpeed = [&](int fallback, const std::string& alias) {
            const AnimationGraphClip* found = nullptr;
            for (const AnimationGraphClip& candidate : m_asset.clips) {
                if ((!alias.empty() && candidate.clipName == alias)
                    || (alias.empty() && !found)) { found = &candidate; if (!alias.empty()) break; }
            }
            if (!found && fallback >= 0 && fallback < static_cast<int>(m_asset.clips.size()))
                found = &m_asset.clips[static_cast<std::size_t>(fallback)];
            AnimationClipAsset clip;
            return found && clip.Load(found->clipAsset, nullptr)
                ? std::max(clip.speed, 0.0f) : 1.0f;
        };
        editor::BuildAnimationController(m_controller, m_asset.states, m_asset.parameters,
                                        m_asset.transitions, resolveClip, clipSeconds,
                                        m_asset.entryStateId, clipBaseSpeed);
        m_controllerDirty = false;
    }

    GLint oldFbo = 0, oldViewport[4]{};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oldFbo);
    glGetIntegerv(GL_VIEWPORT, oldViewport);
    const GLboolean depthWas = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean cullWas = glIsEnabled(GL_CULL_FACE);
    m_fbo->Bind();
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glClearColor(0.055f, 0.070f, 0.095f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (m_model && !m_model->SubMeshes().empty()) {
        const auto& clips = m_model->Animations();
        if (!clips.empty() && !m_asset.states.empty()) {
            for (const auto& p : m_asset.parameters) {
                const float value = m_params[p.name];
                if (p.type == EditorScene::AnimationParameter::Type::Bool)
                    m_controller.SetBoolParameter(p.name, value != 0.0f);
                else if (p.type == EditorScene::AnimationParameter::Type::Float)
                    m_controller.SetParameter(p.name, value);
            }
            if (m_playing) m_controller.Update(std::max(deltaTime, 0.0f));
            const int current = m_controller.CurrentClip();
            const int previous = m_controller.PrevClip();
            const int blendClip = m_controller.CurrentBlendClip();
            if (current >= 0 && current < static_cast<int>(clips.size())) {
                auto sampleState = [&](int fallback, float time,
                                       const engine::AnimationController::BlendSpaceResult& space,
                                       std::vector<engine::BoneLocal>& output) {
                    if (space.active && !space.samples.empty()) {
                        const float refLen = fallback >= 0 && fallback < static_cast<int>(clips.size())
                            ? ClipSeconds(clips[static_cast<std::size_t>(fallback)]) : 0.0f;
                        float accum = 0.0f; bool sampled = false;
                        for (const auto& w : space.samples) {
                            if (w.clip < 0 || w.clip >= static_cast<int>(clips.size()) || w.weight <= 0.0f) continue;
                            const auto& a = clips[static_cast<std::size_t>(w.clip)];
                            float t = time;
                            const float len = ClipSeconds(a);
                            if (space.synchronized && refLen > 0.0001f && len > 0.0001f)
                                t = (std::fmod(std::max(time, 0.0f), refLen) / refLen) * len;
                            else if (!space.synchronized)
                                t *= std::max(w.basePlaybackSpeed, 0.0f);
                            std::vector<engine::BoneLocal> pose;
                            engine::Animator::SampleLocal(m_model->GetSkeleton(), a, t, pose);
                            if (!sampled) { output = std::move(pose); accum = w.weight; sampled = true; }
                            else { std::vector<engine::BoneLocal> mixed;
                                engine::Animator::BlendLocal(output, pose, w.weight / (accum + w.weight), mixed);
                                output = std::move(mixed); accum += w.weight; }
                        }
                        return sampled;
                    }
                    const int aIndex = space.active ? space.clipA : fallback;
                    if (aIndex < 0 || aIndex >= static_cast<int>(clips.size())) return false;
                    engine::Animator::SampleLocal(m_model->GetSkeleton(), clips[static_cast<std::size_t>(aIndex)], time, output);
                    if (space.active && space.clipB != aIndex && space.clipB >= 0 && space.clipB < static_cast<int>(clips.size())) {
                        std::vector<engine::BoneLocal> b, mixed;
                        engine::Animator::SampleLocal(m_model->GetSkeleton(), clips[static_cast<std::size_t>(space.clipB)], time, b);
                        engine::Animator::BlendLocal(output, b, space.alpha, mixed);
                        output = std::move(mixed);
                    }
                    return true;
                };
                const auto currentSpace = m_controller.CurrentBlendSpace();
                std::vector<engine::BoneLocal> pose;
                sampleState(current, m_controller.CurrentTime(), currentSpace, pose);
                if (!currentSpace.active && blendClip >= 0 && blendClip < static_cast<int>(clips.size())) {
                    std::vector<engine::BoneLocal> b, mixed;
                    engine::Animator::SampleLocal(m_model->GetSkeleton(), clips[static_cast<std::size_t>(blendClip)], m_controller.CurrentTime(), b);
                    engine::Animator::BlendLocal(pose, b, m_controller.CurrentBlendWeight(), mixed);
                    pose = std::move(mixed);
                }
                if (m_controller.Blending() && previous >= 0 && previous < static_cast<int>(clips.size())) {
                    std::vector<engine::BoneLocal> prevPose, mixed;
                    sampleState(previous, m_controller.PrevTime(), m_controller.PreviousBlendSpace(), prevPose);
                    engine::Animator::BlendLocal(prevPose, pose, m_controller.Blend(), mixed);
                    pose = std::move(mixed);
                }
                engine::Animator::Compose(m_model->GetSkeleton(), pose, m_pose);
            } else {
                engine::Animator::ComputeBindPose(m_model->GetSkeleton(), m_pose);
            }
        } else {
            engine::Animator::ComputeBindPose(m_model->GetSkeleton(), m_pose);
        }

        const float radius = std::max(m_model->BoundingRadius(), 0.001f);
        glm::mat4 model(1.0f);
        model = glm::rotate(model, glm::radians(m_yaw), glm::vec3(0, 1, 0));
        model = glm::rotate(model, glm::radians(m_pitch), glm::vec3(1, 0, 0));
        const glm::vec3 size = m_model->Max() - m_model->Min();
        if (size.z > size.y * 1.25f) model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(1, 0, 0));
        model = glm::scale(model, glm::vec3((0.9f * m_zoom) / radius));
        model = glm::translate(model, -m_model->Center());
        engine::Camera camera(glm::vec3(0.0f, 0.0f, 2.5f));
        camera.LookAt(glm::vec3(0.0f));
        m_renderer->Draw(*m_model, m_pose, model, camera,
            static_cast<float>(width) / static_cast<float>(height),
            glm::normalize(glm::vec3(0.45f, -1.0f, -0.35f)), glm::vec3(1.0f, 0.96f, 0.90f), glm::vec3(0.16f));
    }

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(oldFbo));
    glViewport(oldViewport[0], oldViewport[1], oldViewport[2], oldViewport[3]);
    if (depthWas) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (cullWas) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    return m_fbo->ColorTexture();
}

void AnimationGraphEditorPanel::Draw(const std::string& assetRoot, bool* open, bool* assetSaved,
                                     std::string* message, float deltaTime) {
    if (m_scannedRoot != assetRoot) RefreshChoices(assetRoot);
    if (!m_pendingOpen.empty()) {
        std::string error;
        if (m_asset.Load(m_pendingOpen, &error)) {
            m_path = m_pendingOpen; SyncBuffers(); ResetPreview();
            m_selectionType = SelectionType::None; m_selectedId = {}; m_linking = false;
            m_dirty = false;
        }
        else if (message) *message = error;
        m_pendingOpen.clear();
    }
    if (m_path.empty()) {
        m_path = (std::filesystem::path(assetRoot) / "Assets" / "Animations" / "Graph.3dggraph").string();
        SyncBuffers();
    }

    if (!ImGui::Begin(EditorPanels::Name(EditorPanels::Panel::GraphEditor), open, ImGuiWindowFlags_MenuBar)) { ImGui::End(); return; }
    if (ImGui::BeginMenuBar()) {
        if (ImGui::MenuItem("New")) {
            m_asset = {}; m_path.clear(); SyncBuffers(); ResetPreview();
            m_selectionType = SelectionType::None; m_selectedId = {}; m_linking = false;
            m_dirty = true;
        }
        if (ImGui::MenuItem("Save")) {
            m_asset.name = m_nameBuffer.data();
            std::string error;
            if (m_asset.Save(m_path, &error)) { m_dirty = false; if (assetSaved) *assetSaved = true; if (message) *message = "Saved graph: " + m_path; }
            else if (message) *message = error;
        }
        ImGui::EndMenuBar();
    }

    std::array<char, 260> pathBuf{}; Copy(pathBuf, m_path);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputText("##GraphPath", pathBuf.data(), pathBuf.size())) { m_path = pathBuf.data(); m_dirty = true; }
    ImGui::Separator();

    const auto assetPicker = [&](const char* label, std::array<char, 128>& search,
                                 const std::vector<AssetChoice>& choices, std::string& value) {
        bool picked = false;
        const std::string preview = value.empty() ? std::string("None") : std::filesystem::path(value).filename().string();
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo(label, preview.c_str())) {
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint("##s", "Search...", search.data(), search.size());
            ImGui::Separator();
            if (ImGui::Selectable("None", value.empty())) { value.clear(); picked = true; ImGui::CloseCurrentPopup(); }
            const std::string filter = Lower(search.data());
            for (const AssetChoice& c : choices) {
                if (!filter.empty() && Lower(c.displayName).find(filter) == std::string::npos) continue;
                ImGui::PushID(c.path.c_str());
                if (ImGui::Selectable(c.displayName.c_str(), value == c.path)) { value = c.path; picked = true; ImGui::CloseCurrentPopup(); }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", c.path.c_str());
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        return picked;
    };

    // Preview (left).
    ImGui::BeginChild("GraphPreview", ImVec2(-360.0f, 0), true);
    ImGui::TextDisabled("PREVIEW");
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float pside = std::max(160.0f, std::min(avail.x, avail.y - 120.0f));
    const unsigned int texture = RenderPreview(static_cast<int>(pside), static_cast<int>(pside), deltaTime);
    const bool controllerDirtyBeforeAuthoring = m_controllerDirty;
    ImGui::Image((ImTextureID)(std::intptr_t)texture, ImVec2(pside, pside), ImVec2(0, 1), ImVec2(1, 0));
    if (ImGui::IsItemHovered()) {
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            const ImVec2 d = ImGui::GetIO().MouseDelta;
            m_yaw += d.x * 0.45f; m_pitch = std::clamp(m_pitch - d.y * 0.35f, -85.0f, 85.0f);
        }
        if (ImGui::GetIO().MouseWheel != 0.0f) m_zoom = std::clamp(m_zoom + ImGui::GetIO().MouseWheel * 0.1f, 0.4f, 3.0f);
    }
    if (!m_error.empty()) ImGui::TextColored(ImVec4(1, .4f, .3f, 1), "Preview: %s", m_error.c_str());
    else if (m_asset.previewModel.empty()) ImGui::TextDisabled("Pick a Preview Rig below to see the graph run.");
    if (ImGui::Button(m_playing ? "Pause" : "Play")) m_playing = !m_playing;
    ImGui::SameLine();
    if (m_asset.states.empty()) ImGui::TextDisabled("Add states to see playback.");
    else ImGui::TextDisabled("State: %s", m_controller.CurrentStateName().c_str());
    if (assetPicker(
            "Preview Rig", m_modelSearch, m_modelChoices,
            m_asset.previewModel)) {
        m_asset.previewModelAssetId = {};
        m_dirty = true;
    }
    ImGui::TextDisabled("Drive parameters:");
    for (std::size_t parameterIndex = 0; parameterIndex < m_asset.parameters.size(); ++parameterIndex) {
        const auto& p = m_asset.parameters[parameterIndex];
        ImGui::PushID(static_cast<int>(parameterIndex));
        float& value = m_params[p.name];
        if (p.type == EditorScene::AnimationParameter::Type::Float) ImGui::DragFloat(p.name.c_str(), &value, .05f);
        else if (p.type == EditorScene::AnimationParameter::Type::Bool) { bool v = value != 0; if (ImGui::Checkbox(p.name.c_str(), &v)) value = v ? 1.0f : 0.0f; }
        else if (ImGui::Button(("Trigger " + p.name).c_str())) m_controller.SetTriggerParameter(p.name);
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::SameLine();

    // Authoring (right).
    ImGui::BeginChild("GraphAuthor", ImVec2(0, 0), true);
    ImGui::TextDisabled("GRAPH");
    if (ImGui::InputText("Name", m_nameBuffer.data(), m_nameBuffer.size())) {
        m_asset.name = m_nameBuffer.data();
        m_dirty = true;
    }

    // Validation strip: surface authoring traps (unresolved clips, always-true transitions,
    // unreachable / dead-end states) so a broken graph explains itself.
    {
        const std::vector<GraphIssue> issues = ValidateGraph(m_asset);
        if (!issues.empty()) {
            int errors = 0, warnings = 0;
            for (const GraphIssue& issue : issues) {
                if (issue.severity == 0) ++errors;
                else if (issue.severity == 1) ++warnings;
            }
            const ImVec4 headColor = errors ? ImVec4(1.0f, 0.45f, 0.4f, 1.0f)
                                   : warnings ? ImVec4(1.0f, 0.8f, 0.35f, 1.0f)
                                              : ImVec4(0.6f, 0.8f, 0.6f, 1.0f);
            char header[80];
            std::snprintf(header, sizeof(header),
                          "Validation: %d error(s), %d warning(s)###graphValidation",
                          errors, warnings);
            ImGui::PushStyleColor(ImGuiCol_Text, headColor);
            const bool validationOpen = ImGui::CollapsingHeader(
                header, errors ? ImGuiTreeNodeFlags_DefaultOpen : 0);
            ImGui::PopStyleColor();
            if (validationOpen) {
                for (const GraphIssue& issue : issues) {
                    const ImVec4 color = issue.severity == 0 ? ImVec4(1.0f, 0.5f, 0.45f, 1.0f)
                                       : issue.severity == 1 ? ImVec4(1.0f, 0.82f, 0.4f, 1.0f)
                                                             : ImVec4(0.72f, 0.72f, 0.72f, 1.0f);
                    const char* tag = issue.severity == 0 ? "[error]"
                                    : issue.severity == 1 ? "[warn] " : "[info] ";
                    ImGui::TextColored(color, "%s %s", tag, issue.text.c_str());
                }
            }
        }
    }

    ImGui::SeparatorText("Clips (from .3dgclip)");
    ImGui::TextDisabled("Rename clips (e.g. Idle / Walk / Run) — states, samples and the "
                        "locomotion presets reference these names.");
    int removeClip = -1;
    for (std::size_t i = 0; i < m_asset.clips.size(); ++i) {
        auto& c = m_asset.clips[i];
        ImGui::PushID(4000 + static_cast<int>(i));
        std::array<char, 64> cn{}; Copy(cn, c.clipName);
        ImGui::SetNextItemWidth(170);
        ImGui::InputText("##clipname", cn.data(), cn.size());
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            // Commit on focus-out/Enter: keep the alias unique and carry existing
            // references along with the rename.
            const std::string unique = UniqueClipAliasExcept(m_asset.clips, i, cn.data());
            if (unique != c.clipName) {
                RenameClipReferences(m_asset, c.clipName, unique);
                c.clipName = unique;
                m_controllerDirty = true;
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled("[%s / %s]",
            std::filesystem::path(c.sourceFile).filename().string().c_str(),
            c.sourceClipName.empty() ? "first take" : c.sourceClipName.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove")) removeClip = static_cast<int>(i);
        ImGui::PopID();
    }
    if (removeClip >= 0) { m_asset.clips.erase(m_asset.clips.begin() + removeClip); ResetPreview(); }
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##AddClip", "Add Clip...")) {
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##clipsearch", "Search clips...", m_clipSearch.data(), m_clipSearch.size());
        ImGui::Separator();
        const std::string filter = Lower(m_clipSearch.data());
        for (const AssetChoice& choice : m_clipChoices) {
            if (!filter.empty() && Lower(choice.displayName).find(filter) == std::string::npos) continue;
            ImGui::PushID(choice.path.c_str());
            if (ImGui::Selectable(choice.displayName.c_str())) {
                AnimationClipAsset clip;
                std::string err;
                if (clip.Load(choice.path, &err)) {
                    AnimationGraphClip c;
                    c.clipAsset = choice.path;
                    c.clipAssetId = clip.assetId;
                    c.sourceFile = clip.sourceFile;
                    c.sourceAssetId = clip.sourceAssetId;
                    c.sourceClipName = clip.clipName;
                    std::string alias = clip.name;
                    if (alias.empty() || alias == "Clip" || alias == clip.clipName)
                        alias = std::filesystem::path(choice.path).stem().string();
                    c.clipName = UniqueClipAlias(m_asset.clips, std::move(alias));
                    c.stripRootMotion = clip.stripRootMotion;
                    c.playbackStart = clip.playbackStart;
                    c.playbackEnd = clip.playbackEnd;
                    c.additive = clip.additive;
                    c.additiveReferenceTime = clip.additiveReferenceTime;
                    for (const AnimationClipAsset::Curve& sourceCurve : clip.curves) {
                        engine::AnimationCurve curve;
                        curve.name = sourceCurve.name;
                        for (const AnimationClipAsset::CurveKey& key : sourceCurve.keys)
                            curve.keys.push_back({key.time, key.value});
                        c.curves.push_back(std::move(curve));
                    }
                    m_asset.clips.push_back(std::move(c));
                    if (m_asset.previewModel.empty()
                        && Lower(std::filesystem::path(clip.sourceFile)
                            .extension().string()) == ".3dganim") {
                        engine::AnimationAssetData animation;
                        std::string ignored;
                        if (engine::LoadAnimationAsset(
                                clip.sourceFile, &animation, &ignored)) {
                            if (!m_preferredPreviewMesh.empty()) {
                                engine::SkeletalMeshAssetData preferred;
                                if (engine::LoadSkeletalMeshAsset(
                                        m_preferredPreviewMesh,
                                        &preferred, &ignored)
                                    && preferred.skeletonId
                                        == animation.skeletonId) {
                                    m_asset.previewModel =
                                        m_preferredPreviewMesh;
                                    m_asset.previewModelAssetId =
                                        preferred.header.id;
                                }
                            }
                            for (const AssetChoice& modelChoice : m_modelChoices) {
                                if (!m_asset.previewModel.empty()) break;
                                engine::SkeletalMeshAssetData mesh;
                                if (engine::LoadSkeletalMeshAsset(
                                        modelChoice.path, &mesh, &ignored)
                                    && mesh.skeletonId == animation.skeletonId) {
                                    m_asset.previewModel = modelChoice.path;
                                    m_asset.previewModelAssetId = mesh.header.id;
                                    break;
                                }
                            }
                        }
                    }
                    ResetPreview();
                } else if (message) {
                    *message = err;
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
        }
        if (m_clipChoices.empty()) ImGui::TextDisabled("No .3dgclip assets - make them in the Clip Editor.");
        ImGui::EndCombo();
    }

    // Helper: a clip-name dropdown (from the graph's clips).
    const auto clipNameCombo = [&](const char* label, std::string& value) {
        if (ImGui::BeginCombo(label, value.empty() ? "(none)" : value.c_str())) {
            for (std::size_t clipIndex = 0; clipIndex < m_asset.clips.size(); ++clipIndex) {
                const AnimationGraphClip& c = m_asset.clips[clipIndex];
                if (c.clipName.empty()) continue;
                ImGui::PushID(static_cast<int>(clipIndex));
                if (ImGui::Selectable(c.clipName.c_str(), value == c.clipName)) {
                    value = c.clipName;
                    m_controllerDirty = true;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Source: %s\nTake: %s",
                        c.sourceFile.c_str(),
                        c.sourceClipName.empty() ? "(first)" : c.sourceClipName.c_str());
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
    };

    m_asset.NormalizeGraphMetadata(false, false);
    const auto stateById = [&](engine::AssetHandle id) -> EditorScene::AnimationStateNode* {
        for (auto& state : m_asset.states) if (state.graphId == id) return &state;
        return nullptr;
    };
    const auto layoutById = [&](engine::AssetHandle id) -> AnimationGraphAsset::NodeLayout* {
        for (auto& layout : m_asset.nodeLayouts) if (layout.stateId == id) return &layout;
        return nullptr;
    };
    const auto transitionById = [&](engine::AssetHandle id) -> EditorScene::AnimationStateTransition* {
        for (auto& transition : m_asset.transitions) if (transition.graphId == id) return &transition;
        return nullptr;
    };
    const auto addDefaultTransition = [&](engine::AssetHandle from, engine::AssetHandle to) {
        if (!to.Valid() || from == to) return;
        EditorScene::AnimationStateTransition transition;
        transition.graphId = engine::AssetHandle::Generate();
        transition.fromStateId = from;
        transition.toStateId = to;
        if (const auto* state = stateById(from)) transition.fromState = state->name;
        if (const auto* state = stateById(to)) transition.toState = state->name;
        if (!m_asset.parameters.empty()) {
            const auto& parameter = m_asset.parameters.front();
            transition.parameter = parameter.name;
            using P = EditorScene::AnimationParameter::Type;
            using C = EditorScene::AnimationStateTransition::Compare;
            if (parameter.type == P::Bool || parameter.type == P::Trigger) {
                transition.compare = C::Equal; transition.threshold = 1.0f;
            } else {
                transition.compare = C::Greater; transition.threshold = 0.1f;
            }
        }
        m_asset.transitions.push_back(std::move(transition));
        m_selectionType = SelectionType::Transition;
        m_selectedId = m_asset.transitions.back().graphId;
        m_controllerDirty = true;
    };

    ImGui::SeparatorText("State Machine");
    bool deleteSelection = false;
    if (ImGui::Button("Add State")) {
        EditorScene::AnimationStateNode state;
        state.graphId = engine::AssetHandle::Generate();
        state.name = "State " + std::to_string(m_asset.states.size() + 1);
        m_asset.states.push_back(state);
        m_asset.nodeLayouts.push_back({state.graphId, {80.0f, 80.0f}, false});
        if (!m_asset.entryStateId.Valid()) m_asset.entryStateId = state.graphId;
        m_selectionType = SelectionType::State; m_selectedId = state.graphId;
        m_controllerDirty = true;
    }
    ImGui::SameLine();
    const bool frameAllRequested = ImGui::Button("Frame All");
    ImGui::SameLine();
    ImGui::TextDisabled("Right-click empty space: Add State | drag output to input: transition");

    const float detailsWidth = std::clamp(ImGui::GetContentRegionAvail().x * 0.27f, 260.0f, 390.0f);
    const float parametersWidth = std::clamp(ImGui::GetContentRegionAvail().x * 0.18f, 170.0f, 250.0f);
    ImGui::BeginChild("GraphParameters", ImVec2(parametersWidth, 0.0f), true);
    ImGui::TextDisabled("PARAMETERS");
    int removeParameter = -1;
    for (std::size_t i = 0; i < m_asset.parameters.size(); ++i) {
        auto& parameter = m_asset.parameters[i];
        ImGui::PushID(5000 + static_cast<int>(i));
        const std::string oldName = parameter.name;
        std::array<char, 64> buffer{}; Copy(buffer, parameter.name);
        ImGui::SetNextItemWidth(-52.0f);
        if (ImGui::InputText("##Name", buffer.data(), buffer.size())) {
            parameter.name = buffer.data();
            if (!parameter.name.empty() && parameter.name != oldName) {
                for (auto& state : m_asset.states) {
                    if (state.blendParameter == oldName) state.blendParameter = parameter.name;
                    if (state.blendParameterY == oldName) state.blendParameterY = parameter.name;
                }
                for (auto& transition : m_asset.transitions) {
                    if (transition.parameter == oldName) transition.parameter = parameter.name;
                    for (auto& condition : transition.additionalConditions)
                        if (condition.parameter == oldName) condition.parameter = parameter.name;
                }
                const auto value = m_params.find(oldName);
                if (value != m_params.end()) { m_params[parameter.name] = value->second; m_params.erase(value); }
                m_controllerDirty = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("X")) removeParameter = static_cast<int>(i);
        int type = static_cast<int>(parameter.type);
        const char* types[] = {"Float", "Bool", "Trigger"};
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo("##Type", &type, types, 3)) {
            parameter.type = static_cast<EditorScene::AnimationParameter::Type>(type);
            m_controllerDirty = true;
        }
        if (parameter.type != EditorScene::AnimationParameter::Type::Trigger)
            if (ImGui::DragFloat("Default", &parameter.defaultValue, 0.02f)) m_controllerDirty = true;
        ImGui::Separator();
        ImGui::PopID();
    }
    if (removeParameter >= 0) {
        const std::string removed = m_asset.parameters[static_cast<std::size_t>(removeParameter)].name;
        m_asset.parameters.erase(m_asset.parameters.begin() + removeParameter);
        for (auto& transition : m_asset.transitions) {
            if (transition.parameter == removed) transition.parameter.clear();
            transition.additionalConditions.erase(std::remove_if(transition.additionalConditions.begin(),
                transition.additionalConditions.end(), [&](const auto& c) { return c.parameter == removed; }),
                transition.additionalConditions.end());
        }
        m_controllerDirty = true;
    }
    if (ImGui::Button("+ Parameter", ImVec2(-1.0f, 0.0f))) {
        std::string name = "Parameter"; int suffix = 2;
        const auto exists = [&](const std::string& candidate) { return std::any_of(m_asset.parameters.begin(),
            m_asset.parameters.end(), [&](const auto& p) { return p.name == candidate; }); };
        while (exists(name)) name = "Parameter " + std::to_string(suffix++);
        m_asset.parameters.push_back({name, EditorScene::AnimationParameter::Type::Float, 0.0f});
        m_controllerDirty = true;
    }
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginChild("GraphCanvas", ImVec2(-detailsWidth - ImGui::GetStyle().ItemSpacing.x, 0.0f), true,
                      ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar);
    const ImVec2 canvasOrigin = ImGui::GetCursorScreenPos();
    const ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    bool graphContextRequested = false;
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("CanvasBackground", canvasSize,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle | ImGuiButtonFlags_MouseButtonRight);
    const bool canvasHovered = ImGui::IsItemHovered();
    if (frameAllRequested) {
        glm::vec2 minimum = glm::min(m_asset.entryNodePosition, m_asset.anyStateNodePosition);
        glm::vec2 maximum = glm::max(m_asset.entryNodePosition, m_asset.anyStateNodePosition)
            + glm::vec2(190.0f, 70.0f);
        for (const auto& layout : m_asset.nodeLayouts) {
            minimum = glm::min(minimum, layout.position);
            maximum = glm::max(maximum, layout.position + glm::vec2(190.0f, 70.0f));
        }
        const glm::vec2 extent = glm::max(maximum - minimum, glm::vec2(1.0f));
        const float availableX = std::max(canvasSize.x - 48.0f, 1.0f);
        const float availableY = std::max(canvasSize.y - 48.0f, 1.0f);
        m_canvasZoom = std::clamp(std::min(availableX / extent.x, availableY / extent.y),
                                  0.35f, 1.5f);
        m_canvasPan = glm::vec2(24.0f) - minimum * m_canvasZoom
            + glm::max((glm::vec2(canvasSize.x, canvasSize.y) - extent * m_canvasZoom
                        - glm::vec2(48.0f)) * 0.5f,
                       glm::vec2(0.0f));
    }
    if (canvasHovered && ImGui::GetIO().MouseWheel != 0.0f) {
        const float oldZoom = m_canvasZoom;
        m_canvasZoom = std::clamp(m_canvasZoom * std::pow(1.12f, ImGui::GetIO().MouseWheel), 0.35f, 2.2f);
        const ImVec2 mouse = ImGui::GetMousePos();
        m_canvasPan.x = mouse.x - canvasOrigin.x - (mouse.x - canvasOrigin.x - m_canvasPan.x) * (m_canvasZoom / oldZoom);
        m_canvasPan.y = mouse.y - canvasOrigin.y - (mouse.y - canvasOrigin.y - m_canvasPan.y) * (m_canvasZoom / oldZoom);
    }
    if (canvasHovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        m_canvasPan.x += ImGui::GetIO().MouseDelta.x;
        m_canvasPan.y += ImGui::GetIO().MouseDelta.y;
    }
    if (canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        const ImVec2 click = ImGui::GetMousePos();
        m_contextGraphPosition = {
            (click.x - canvasOrigin.x - m_canvasPan.x) / m_canvasZoom,
            (click.y - canvasOrigin.y - m_canvasPan.y) / m_canvasZoom};
        graphContextRequested = true;
    }
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(canvasOrigin, {canvasOrigin.x + canvasSize.x, canvasOrigin.y + canvasSize.y}, IM_COL32(22, 25, 31, 255));
    const float grid = 32.0f * m_canvasZoom;
    if (grid >= 10.0f) {
        for (float x = std::fmod(m_canvasPan.x, grid); x < canvasSize.x; x += grid)
            draw->AddLine({canvasOrigin.x + x, canvasOrigin.y}, {canvasOrigin.x + x, canvasOrigin.y + canvasSize.y}, IM_COL32(48, 53, 63, 120));
        for (float y = std::fmod(m_canvasPan.y, grid); y < canvasSize.y; y += grid)
            draw->AddLine({canvasOrigin.x, canvasOrigin.y + y}, {canvasOrigin.x + canvasSize.x, canvasOrigin.y + y}, IM_COL32(48, 53, 63, 120));
    }
    const auto screen = [&](glm::vec2 p) { return ImVec2(canvasOrigin.x + m_canvasPan.x + p.x * m_canvasZoom,
                                                           canvasOrigin.y + m_canvasPan.y + p.y * m_canvasZoom); };
    const ImVec2 nodeSize(190.0f * m_canvasZoom, 70.0f * m_canvasZoom);
    const auto nodeInput = [&](engine::AssetHandle id) { auto* l = layoutById(id); const ImVec2 p = screen(l ? l->position : glm::vec2(0)); return ImVec2(p.x, p.y + nodeSize.y * 0.5f); };
    const auto nodeOutput = [&](engine::AssetHandle id) { auto* l = layoutById(id); const ImVec2 p = screen(l ? l->position : glm::vec2(0)); return ImVec2(p.x + nodeSize.x, p.y + nodeSize.y * 0.5f); };

    // Links are editor projections of the existing transition array.
    const ImVec2 mouse = ImGui::GetMousePos();
    for (auto& transition : m_asset.transitions) {
        if (!transition.toStateId.Valid() || !stateById(transition.toStateId)
            || (transition.fromStateId.Valid() && !stateById(transition.fromStateId)))
            continue;
        const ImVec2 from = transition.fromStateId.Valid() ? nodeOutput(transition.fromStateId)
            : ImVec2(screen(m_asset.anyStateNodePosition).x + nodeSize.x, screen(m_asset.anyStateNodePosition).y + nodeSize.y * 0.5f);
        const ImVec2 to = nodeInput(transition.toStateId);
        const bool selected = m_selectionType == SelectionType::Transition && m_selectedId == transition.graphId;
        const bool hovered = DistanceToSegment(mouse, from, to) < 8.0f;
        draw->AddBezierCubic(from, {from.x + 65.0f * m_canvasZoom, from.y},
            {to.x - 65.0f * m_canvasZoom, to.y}, to,
            selected ? IM_COL32(255, 186, 55, 255) : hovered ? IM_COL32(190, 215, 245, 255) : IM_COL32(135, 155, 182, 230),
            selected ? 3.0f : 2.0f);
        const ImVec2 direction(to.x - from.x, to.y - from.y);
        const float len = std::max(std::hypot(direction.x, direction.y), 1.0f);
        const ImVec2 unit(direction.x / len, direction.y / len);
        draw->AddTriangleFilled(to, {to.x - unit.x * 12.0f - unit.y * 6.0f, to.y - unit.y * 12.0f + unit.x * 6.0f},
            {to.x - unit.x * 12.0f + unit.y * 6.0f, to.y - unit.y * 12.0f - unit.x * 6.0f}, IM_COL32(160, 180, 205, 255));
        if (canvasHovered && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            m_selectionType = SelectionType::Transition; m_selectedId = transition.graphId;
        }
        if (canvasHovered && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            graphContextRequested = false;
            m_selectionType = SelectionType::Transition; m_selectedId = transition.graphId;
            ImGui::OpenPopup("TransitionContext");
        }
    }
    if (ImGui::BeginPopup("TransitionContext")) {
        if (ImGui::MenuItem("Delete Transition")) {
            const engine::AssetHandle deleted = m_selectedId;
            m_asset.transitions.erase(std::remove_if(
                m_asset.transitions.begin(), m_asset.transitions.end(),
                [&](const auto& transition) { return transition.graphId == deleted; }),
                m_asset.transitions.end());
            m_selectionType = SelectionType::None;
            m_selectedId = {};
            m_controllerDirty = true;
        }
        ImGui::EndPopup();
    }
    if (m_asset.entryStateId.Valid()) {
        const ImVec2 from(screen(m_asset.entryNodePosition).x + nodeSize.x,
                          screen(m_asset.entryNodePosition).y + nodeSize.y * 0.5f);
        const ImVec2 to = nodeInput(m_asset.entryStateId);
        draw->AddBezierCubic(from, {from.x + 55.0f * m_canvasZoom, from.y},
            {to.x - 55.0f * m_canvasZoom, to.y}, to, IM_COL32(120, 220, 140, 255), 2.5f);
    }

    const auto drawSpecialNode = [&](const char* label, glm::vec2& position, bool anyState) {
        const ImVec2 p = screen(position);
        ImGui::SetCursorScreenPos(p); ImGui::PushID(label);
        ImGui::SetNextItemAllowOverlap();
        ImGui::InvisibleButton("Body", nodeSize);
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) graphContextRequested = false;
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            position += glm::vec2(ImGui::GetIO().MouseDelta.x, ImGui::GetIO().MouseDelta.y) / m_canvasZoom;
        }
        draw->AddRectFilled(p, {p.x + nodeSize.x, p.y + nodeSize.y}, anyState ? IM_COL32(86, 65, 112, 255) : IM_COL32(55, 100, 68, 255), 7.0f);
        draw->AddRect(p, {p.x + nodeSize.x, p.y + nodeSize.y}, IM_COL32(180, 195, 215, 255), 7.0f, 0, 1.5f);
        draw->AddText({p.x + 13.0f, p.y + 24.0f * m_canvasZoom}, IM_COL32_WHITE, label);
        const ImVec2 out(p.x + nodeSize.x, p.y + nodeSize.y * 0.5f);
        draw->AddCircleFilled(out, 7.0f, IM_COL32(225, 225, 235, 255));
        ImGui::SetCursorScreenPos({out.x - 10.0f, out.y - 10.0f});
        ImGui::InvisibleButton("Output", {20.0f, 20.0f});
        if (ImGui::IsItemClicked()) { m_linking = true; m_linkingFromAny = anyState; m_linkSourceId = {}; }
        ImGui::PopID();
    };
    drawSpecialNode("Entry", m_asset.entryNodePosition, false);
    drawSpecialNode("Any State", m_asset.anyStateNodePosition, true);

    for (auto& state : m_asset.states) {
        auto* layout = layoutById(state.graphId); if (!layout) continue;
        const ImVec2 p = screen(layout->position);
        ImGui::SetCursorScreenPos(p); ImGui::PushID(state.graphId.ToString().c_str());
        ImGui::SetNextItemAllowOverlap();
        ImGui::InvisibleButton("Body", nodeSize);
        if (ImGui::IsItemClicked()) { m_selectionType = SelectionType::State; m_selectedId = state.graphId; }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            graphContextRequested = false;
            m_selectionType = SelectionType::State;
            m_selectedId = state.graphId;
            ImGui::OpenPopup("StateContext");
        }
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            layout->position += glm::vec2(ImGui::GetIO().MouseDelta.x, ImGui::GetIO().MouseDelta.y) / m_canvasZoom;
            m_dirty = true;
        }
        const bool selected = m_selectionType == SelectionType::State && m_selectedId == state.graphId;
        const bool active = m_controller.CurrentStateName() == state.name;
        draw->AddRectFilled(p, {p.x + nodeSize.x, p.y + nodeSize.y}, IM_COL32(48, 60, 78, 255), 7.0f);
        draw->AddRectFilled(p, {p.x + nodeSize.x, p.y + 25.0f * m_canvasZoom}, active ? IM_COL32(55, 135, 82, 255) : IM_COL32(58, 93, 132, 255), 7.0f, ImDrawFlags_RoundCornersTop);
        draw->AddRect(p, {p.x + nodeSize.x, p.y + nodeSize.y}, selected ? IM_COL32(255, 184, 45, 255) : IM_COL32(105, 125, 150, 255), 7.0f, 0, selected ? 3.0f : 1.0f);
        draw->AddText({p.x + 10.0f, p.y + 6.0f}, IM_COL32_WHITE, state.name.c_str());
        draw->AddText({p.x + 10.0f, p.y + 37.0f * m_canvasZoom}, IM_COL32(190, 205, 222, 255), MotionSourceName(state.motionSourceType));
        const ImVec2 input(p.x, p.y + nodeSize.y * 0.5f), output(p.x + nodeSize.x, p.y + nodeSize.y * 0.5f);
        draw->AddCircleFilled(input, 7.0f, IM_COL32(220, 225, 235, 255));
        draw->AddCircleFilled(output, 7.0f, IM_COL32(220, 225, 235, 255));
        ImGui::SetCursorScreenPos({input.x - 10.0f, input.y - 10.0f}); ImGui::InvisibleButton("Input", {20.0f, 20.0f});
        if (ImGui::IsItemClicked() && m_linking) {
            if (m_linkingFromAny) addDefaultTransition({}, state.graphId);
            else if (!m_linkSourceId.Valid()) { m_asset.entryStateId = state.graphId; m_controllerDirty = true; }
            else addDefaultTransition(m_linkSourceId, state.graphId);
            m_linking = false;
        }
        ImGui::SetCursorScreenPos({output.x - 10.0f, output.y - 10.0f}); ImGui::InvisibleButton("Output", {20.0f, 20.0f});
        if (ImGui::IsItemClicked()) { m_linking = true; m_linkingFromAny = false; m_linkSourceId = state.graphId; }
        if (ImGui::BeginPopup("StateContext")) {
            if (ImGui::MenuItem("Set as Entry", nullptr,
                                m_asset.entryStateId == state.graphId)) {
                m_asset.entryStateId = state.graphId;
                m_controllerDirty = true;
            }
            if (ImGui::MenuItem("Delete State")) {
                m_selectionType = SelectionType::State;
                m_selectedId = state.graphId;
                deleteSelection = true;
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    if (graphContextRequested) ImGui::OpenPopup("GraphContext");
    if (ImGui::BeginPopup("GraphContext")) {
        if (ImGui::MenuItem("Add State")) {
            EditorScene::AnimationStateNode state;
            state.graphId = engine::AssetHandle::Generate();
            state.name = "State " + std::to_string(m_asset.states.size() + 1);
            m_asset.states.push_back(state);
            m_asset.nodeLayouts.push_back(
                {state.graphId, m_contextGraphPosition, false});
            if (!m_asset.entryStateId.Valid()) m_asset.entryStateId = state.graphId;
            m_selectionType = SelectionType::State;
            m_selectedId = state.graphId;
            m_controllerDirty = true;
        }
        ImGui::EndPopup();
    }
    if (m_linking) {
        ImVec2 from;
        if (m_linkingFromAny) { const ImVec2 p = screen(m_asset.anyStateNodePosition); from = {p.x + nodeSize.x, p.y + nodeSize.y * 0.5f}; }
        else if (m_linkSourceId.Valid()) from = nodeOutput(m_linkSourceId);
        else { const ImVec2 p = screen(m_asset.entryNodePosition); from = {p.x + nodeSize.x, p.y + nodeSize.y * 0.5f}; }
        draw->AddBezierCubic(from, {from.x + 60.0f, from.y}, {mouse.x - 60.0f, mouse.y}, mouse, IM_COL32(255, 190, 60, 255), 2.5f);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) m_linking = false;
    }
    ImGui::SetCursorScreenPos({canvasOrigin.x + 8.0f, canvasOrigin.y + canvasSize.y - 22.0f});
    ImGui::TextDisabled("%.0f%%", m_canvasZoom * 100.0f);
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginChild("GraphDetails", ImVec2(0.0f, 0.0f), true);
    ImGui::TextDisabled("DETAILS");
    if (m_selectionType == SelectionType::State) {
        if (auto* state = stateById(m_selectedId)) {
            std::array<char, 96> name{}; Copy(name, state->name);
            const std::string oldName = state->name;
            if (ImGui::InputText("State Name", name.data(), name.size())) {
                std::string candidate = name.data();
                const bool duplicate = candidate.empty() || std::any_of(m_asset.states.begin(), m_asset.states.end(),
                    [&](const auto& other) { return other.graphId != state->graphId && other.name == candidate; });
                if (!duplicate) { state->name = candidate; m_asset.NormalizeGraphMetadata(false, false); m_controllerDirty = true; }
            }
            int source = static_cast<int>(state->motionSourceType);
            const char* sources[] = {"Animation Clip", "Blend Space 1D", "Blend Space 2D"};
            if (ImGui::Combo("Motion Source", &source, sources, 3)) {
                state->motionSourceType = static_cast<EditorScene::AnimationStateNode::MotionSourceType>(source);
                state->blendSpace2D = source == 2;
                if (source != 0 && state->blendParameter.empty()) state->blendParameter = "Speed";
                if (source == 2 && state->blendParameterY.empty()) state->blendParameterY = "Direction";
                m_controllerDirty = true;
            }
            if (state->motionSourceType == EditorScene::AnimationStateNode::MotionSourceType::Clip) {
                clipNameCombo("Clip", state->clipName);
            } else {
                const auto parameterCombo = [&](const char* label, std::string& value) {
                    if (ImGui::BeginCombo(label, value.empty() ? "None" : value.c_str())) {
                        for (std::size_t parameterIndex = 0;
                             parameterIndex < m_asset.parameters.size(); ++parameterIndex) {
                            const auto& parameter = m_asset.parameters[parameterIndex];
                            if (parameter.type != EditorScene::AnimationParameter::Type::Float) continue;
                            ImGui::PushID(static_cast<int>(parameterIndex));
                            if (ImGui::Selectable(parameter.name.c_str(), value == parameter.name)) { value = parameter.name; m_controllerDirty = true; }
                            ImGui::PopID();
                        }
                        ImGui::EndCombo();
                    }
                };
                parameterCombo("X Parameter", state->blendParameter);
                if (state->motionSourceType == EditorScene::AnimationStateNode::MotionSourceType::BlendSpace2D)
                    parameterCombo("Y Parameter", state->blendParameterY);
                if (ImGui::Checkbox("Synchronize Samples", &state->synchronizeBlendSpace)) m_controllerDirty = true;
                ImGui::SeparatorText("Samples");
                int removeSample = -1;
                for (std::size_t i = 0; i < state->blendSamples.size(); ++i) {
                    auto& sample = state->blendSamples[i]; ImGui::PushID(static_cast<int>(i));
                    clipNameCombo("Clip", sample.clipName);
                    if (ImGui::DragFloat("X", &sample.value, 0.05f)) m_controllerDirty = true;
                    if (state->motionSourceType == EditorScene::AnimationStateNode::MotionSourceType::BlendSpace2D)
                        if (ImGui::DragFloat("Y", &sample.valueY, 0.05f)) m_controllerDirty = true;
                    if (ImGui::SmallButton("Remove Sample")) removeSample = static_cast<int>(i);
                    ImGui::Separator(); ImGui::PopID();
                }
                if (removeSample >= 0) { state->blendSamples.erase(state->blendSamples.begin() + removeSample); m_controllerDirty = true; }
                if (ImGui::Button("Add Sample")) { state->blendSamples.push_back({}); m_controllerDirty = true; }
            }
            if (ImGui::Checkbox("Loop", &state->loop)) m_controllerDirty = true;
            const auto baseSpeedFor = [&](int fallback, const std::string& alias) {
                const AnimationGraphClip* found = nullptr;
                for (const auto& candidate : m_asset.clips)
                    if (candidate.clipName == alias) { found = &candidate; break; }
                if (!found && fallback >= 0 && fallback < static_cast<int>(m_asset.clips.size()))
                    found = &m_asset.clips[static_cast<std::size_t>(fallback)];
                AnimationClipAsset clip;
                return found && clip.Load(found->clipAsset, nullptr)
                    ? std::max(clip.speed, 0.0f) : 1.0f;
            };
            if (state->motionSourceType == EditorScene::AnimationStateNode::MotionSourceType::Clip) {
                const float base = baseSpeedFor(state->clipIndex, state->clipName);
                ImGui::TextDisabled("Clip Base Speed: %.2fx", base);
                ImGui::TextDisabled("Effective Speed: %.2fx", base * std::max(state->speed, 0.0f));
            } else {
                for (const auto& sample : state->blendSamples)
                    ImGui::BulletText("%s  Base %.2fx", sample.clipName.c_str(),
                        baseSpeedFor(sample.clipIndex, sample.clipName));
            }
            if (ImGui::DragFloat("Speed Multiplier", &state->speed, 0.02f, 0.0f, 8.0f)) m_controllerDirty = true;
            if (ImGui::Checkbox("Root Motion", &state->rootMotion)) m_controllerDirty = true;
            const bool isEntry = m_asset.entryStateId == state->graphId;
            if (isEntry) ImGui::TextColored(ImVec4(0.45f, 0.9f, 0.55f, 1.0f), "Entry State");
            else if (ImGui::Button("Set as Entry")) { m_asset.entryStateId = state->graphId; m_controllerDirty = true; }
            ImGui::Separator();
            if (ImGui::Button("Delete State")) deleteSelection = true;
        }
    } else if (m_selectionType == SelectionType::Transition) {
        if (auto* transition = transitionById(m_selectedId)) {
            ImGui::Text("%s -> %s", transition->fromState.empty() ? "Any State" : transition->fromState.c_str(), transition->toState.c_str());
            if (ImGui::Checkbox("Use Conditions", &transition->useConditions)) m_controllerDirty = true;
            const char* compares[] = {">=", "<", "==", "!=", "<=", ">"};
            const auto drawCondition = [&](const char* id, std::string& parameterName,
                                           EditorScene::AnimationStateTransition::Compare& compareValue,
                                           float& threshold) {
                ImGui::PushID(id);
                if (ImGui::BeginCombo("Parameter", parameterName.empty() ? "None" : parameterName.c_str())) {
                    for (std::size_t parameterIndex = 0;
                         parameterIndex < m_asset.parameters.size(); ++parameterIndex) {
                        const auto& parameter = m_asset.parameters[parameterIndex];
                        ImGui::PushID(static_cast<int>(parameterIndex));
                        if (ImGui::Selectable(parameter.name.c_str(), parameter.name == parameterName)) { parameterName = parameter.name; m_controllerDirty = true; }
                        ImGui::PopID();
                    }
                    ImGui::EndCombo();
                }
                auto type = EditorScene::AnimationParameter::Type::Float;
                for (const auto& parameter : m_asset.parameters) if (parameter.name == parameterName) type = parameter.type;
                if (type == EditorScene::AnimationParameter::Type::Bool || type == EditorScene::AnimationParameter::Type::Trigger) {
                    bool expected = threshold >= 0.5f;
                    if (ImGui::Checkbox("Expected", &expected)) { compareValue = EditorScene::AnimationStateTransition::Compare::Equal; threshold = expected ? 1.0f : 0.0f; m_controllerDirty = true; }
                } else {
                    int compare = std::clamp(static_cast<int>(compareValue), 0, 5);
                    if (ImGui::Combo("Compare", &compare, compares, 6)) { compareValue = static_cast<EditorScene::AnimationStateTransition::Compare>(compare); m_controllerDirty = true; }
                    if (ImGui::DragFloat("Threshold", &threshold, 0.05f)) m_controllerDirty = true;
                }
                ImGui::PopID();
            };
            if (transition->useConditions) {
                int mode = transition->requireAllConditions ? 0 : 1;
                const char* modes[] = {"All (AND)", "Any (OR)"};
                if (ImGui::Combo("Match", &mode, modes, 2)) { transition->requireAllConditions = mode == 0; m_controllerDirty = true; }
                ImGui::SeparatorText("Condition 1");
                drawCondition("Primary", transition->parameter, transition->compare, transition->threshold);
                int removeCondition = -1;
                for (std::size_t i = 0; i < transition->additionalConditions.size(); ++i) {
                    ImGui::SeparatorText(("Condition " + std::to_string(i + 2)).c_str());
                    auto& condition = transition->additionalConditions[i];
                    drawCondition(("Extra" + std::to_string(i)).c_str(), condition.parameter, condition.compare, condition.threshold);
                    ImGui::PushID(9000 + static_cast<int>(i));
                    if (ImGui::SmallButton("Remove")) removeCondition = static_cast<int>(i);
                    ImGui::PopID();
                }
                if (removeCondition >= 0) { transition->additionalConditions.erase(transition->additionalConditions.begin() + removeCondition); m_controllerDirty = true; }
                if (ImGui::Button("Add Condition")) { transition->additionalConditions.push_back({}); m_controllerDirty = true; }
            } else ImGui::TextDisabled("Exit Time only");
            if (ImGui::DragFloat("Fade", &transition->fade, 0.01f, 0.0f, 5.0f)) m_controllerDirty = true;
            if (ImGui::SliderFloat("Exit Time", &transition->exitTime, 0.0f, 1.0f)) m_controllerDirty = true;
            if (ImGui::DragInt("Priority", &transition->priority, 1.0f)) m_controllerDirty = true;
            if (ImGui::Checkbox("Can Interrupt", &transition->canInterrupt)) m_controllerDirty = true;
            if (!transition->useConditions && transition->exitTime <= 0.0f)
                ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.2f, 1.0f), "Exit Time 0 triggers immediately.");
            ImGui::Separator();
            if (ImGui::Button("Delete Transition")) deleteSelection = true;
        }
    } else {
        ImGui::TextWrapped("Select a state node to edit its Motion Source, or select a transition link to edit conditions and blending.");
    }
    if (!ImGui::GetIO().WantTextInput && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)
        && ImGui::IsKeyPressed(ImGuiKey_Delete)) deleteSelection = true;
    if (deleteSelection && m_selectionType == SelectionType::Transition) {
        m_asset.transitions.erase(std::remove_if(m_asset.transitions.begin(), m_asset.transitions.end(),
            [&](const auto& transition) { return transition.graphId == m_selectedId; }), m_asset.transitions.end());
        m_selectionType = SelectionType::None; m_selectedId = {}; m_controllerDirty = true;
    } else if (deleteSelection && m_selectionType == SelectionType::State) {
        const engine::AssetHandle deleted = m_selectedId;
        m_asset.states.erase(std::remove_if(m_asset.states.begin(), m_asset.states.end(),
            [&](const auto& state) { return state.graphId == deleted; }), m_asset.states.end());
        m_asset.transitions.erase(std::remove_if(m_asset.transitions.begin(), m_asset.transitions.end(),
            [&](const auto& transition) { return transition.fromStateId == deleted || transition.toStateId == deleted; }), m_asset.transitions.end());
        m_asset.nodeLayouts.erase(std::remove_if(m_asset.nodeLayouts.begin(), m_asset.nodeLayouts.end(),
            [&](const auto& layout) { return layout.stateId == deleted; }), m_asset.nodeLayouts.end());
        if (m_asset.entryStateId == deleted) m_asset.entryStateId = m_asset.states.empty() ? engine::AssetHandle{} : m_asset.states.front().graphId;
        m_selectionType = SelectionType::None; m_selectedId = {}; m_controllerDirty = true;
    }
    ImGui::EndChild();

    if (m_controllerDirty && !controllerDirtyBeforeAuthoring) m_dirty = true;
    ImGui::EndChild();
    ImGui::End();
}
