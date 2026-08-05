#include "AnimationGraphEditorPanel.h"

#include "AnimationClipAsset.h"
#include "AnimationGraphBuilder.h"

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
    const float tps = a.ticksPerSecond > 0.0f ? a.ticksPerSecond : 25.0f;
    return a.duration > 0.0f ? a.duration / tps : 0.0f;
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

    // States: clip resolution + blend-space parameters.
    for (const EditorScene::AnimationStateNode& s : asset.states) {
        const std::string sn = stateLabel(s.name);
        if (s.blendSamples.empty()) {
            if (!s.clipName.empty() && !isClip(s.clipName))
                add(0, "State '" + sn + "': clip '" + s.clipName + "' is not in the clip list.");
            else if (s.clipName.empty())
                add(1, "State '" + sn + "' has no clip assigned.");
        } else {
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
            if (s.blendSpace2D && !s.blendParameterY.empty() && !paramType(s.blendParameterY, t))
                add(1, "State '" + sn + "': Y parameter '" + s.blendParameterY
                       + "' is not declared.");
        }
    }

    // Transitions: valid endpoints, declared parameters, and the always-true footgun.
    bool hasAnyState = false;
    for (const auto& tr : asset.transitions) if (tr.fromState.empty()) hasAnyState = true;
    for (const auto& tr : asset.transitions) {
        const std::string label = "Transition "
            + (tr.fromState.empty() ? std::string("Any") : tr.fromState) + " -> "
            + (tr.toState.empty() ? std::string("(none)") : tr.toState);
        if (!tr.fromState.empty() && !isState(tr.fromState))
            add(0, label + ": 'from' state does not exist.");
        if (tr.toState.empty() || !isState(tr.toState))
            add(0, label + ": 'to' state does not exist.");

        const auto checkParam = [&](const std::string& p) {
            PType t;
            if (!p.empty() && !paramType(p, t))
                add(1, label + " uses undeclared parameter '" + p + "'.");
        };
        checkParam(tr.parameter);
        for (const auto& c : tr.additionalConditions) checkParam(c.parameter);

        // Only flag "always true" when nothing else gates the transition.
        if (tr.additionalConditions.empty()) {
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

    // Reachability + dead ends (only precise without an Any-state transition).
    if (!hasAnyState) {
        const std::string entry = asset.states.front().name;
        for (const EditorScene::AnimationStateNode& s : asset.states) {
            int incoming = 0, outgoing = 0;
            for (const auto& tr : asset.transitions) {
                if (tr.toState == s.name) ++incoming;
                if (tr.fromState == s.name) ++outgoing;
            }
            if (s.name != entry && incoming == 0)
                add(2, "State '" + stateLabel(s.name) + "' is unreachable (nothing transitions to it).");
            if (outgoing == 0)
                add(2, "State '" + stateLabel(s.name) + "' has no outgoing transition (dead end).");
        }
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
    for (const AnimationGraphClip& c : m_asset.clips)
        signature += '|' + c.sourceFile + '#' + c.sourceClipName + "->" + c.clipName
            + (c.stripRootMotion ? "1" : "0");
    if (signature != m_loadedSignature) {
        ResetPreview();
        m_loadedSignature = signature;
        m_error.clear();
        if (!m_asset.previewModel.empty()) {
            std::vector<engine::RuntimeAssetManager::SkinnedAnimationSource> sources;
            for (const AnimationGraphClip& c : m_asset.clips)
                if (!c.sourceFile.empty())
                    sources.push_back({c.sourceFile, c.clipName, c.stripRootMotion, c.sourceClipName});
            m_model = sources.empty()
                ? m_assets.LoadSkinnedModel(m_asset.previewModel, &m_error)
                : m_assets.LoadSkinnedModel(m_asset.previewModel, sources, &m_error);
            m_controllerDirty = true;
        }
    }

    if (m_model && m_controllerDirty) {
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
        editor::BuildAnimationController(m_controller, m_asset.states, m_asset.parameters,
                                        m_asset.transitions, resolveClip, clipSeconds);
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
        if (m_asset.Load(m_pendingOpen, &error)) { m_path = m_pendingOpen; SyncBuffers(); ResetPreview(); }
        else if (message) *message = error;
        m_pendingOpen.clear();
    }
    if (m_path.empty()) {
        m_path = (std::filesystem::path(assetRoot) / "Assets" / "Animations" / "Graph.3dggraph").string();
        SyncBuffers();
    }

    if (!ImGui::Begin("Graph Editor", open, ImGuiWindowFlags_MenuBar)) { ImGui::End(); return; }
    if (ImGui::BeginMenuBar()) {
        if (ImGui::MenuItem("New")) { m_asset = {}; m_path.clear(); SyncBuffers(); ResetPreview(); }
        if (ImGui::MenuItem("Save")) {
            m_asset.name = m_nameBuffer.data();
            std::string error;
            if (m_asset.Save(m_path, &error)) { if (assetSaved) *assetSaved = true; if (message) *message = "Saved graph: " + m_path; }
            else if (message) *message = error;
        }
        ImGui::EndMenuBar();
    }

    std::array<char, 260> pathBuf{}; Copy(pathBuf, m_path);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputText("##GraphPath", pathBuf.data(), pathBuf.size())) m_path = pathBuf.data();
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
            m_asset.previewModel))
        m_asset.previewModelAssetId = {};
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
    if (ImGui::InputText("Name", m_nameBuffer.data(), m_nameBuffer.size())) m_asset.name = m_nameBuffer.data();

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
            const bool open = ImGui::CollapsingHeader(
                header, errors ? ImGuiTreeNodeFlags_DefaultOpen : 0);
            ImGui::PopStyleColor();
            if (open) {
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

    ImGui::SeparatorText("Parameters");
    int removeParam = -1;
    for (std::size_t i = 0; i < m_asset.parameters.size(); ++i) {
        auto& p = m_asset.parameters[i];
        ImGui::PushID(5000 + static_cast<int>(i));
        std::array<char, 64> nb{}; Copy(nb, p.name);
        ImGui::SetNextItemWidth(120);
        if (ImGui::InputText("##pn", nb.data(), nb.size())) { p.name = nb.data(); m_controllerDirty = true; }
        ImGui::SameLine(); ImGui::SetNextItemWidth(90);
        int type = static_cast<int>(p.type);
        const char* types[] = { "Float", "Bool", "Trigger" };
        if (ImGui::Combo("##pt", &type, types, 3)) { p.type = static_cast<EditorScene::AnimationParameter::Type>(type); m_controllerDirty = true; }
        ImGui::SameLine();
        if (ImGui::SmallButton("X##RemoveParameter")) removeParam = static_cast<int>(i);
        ImGui::PopID();
    }
    if (removeParam >= 0) { m_asset.parameters.erase(m_asset.parameters.begin() + removeParam); m_controllerDirty = true; }
    if (ImGui::Button("Add Parameter")) { m_asset.parameters.push_back({ "Speed", EditorScene::AnimationParameter::Type::Float, 0.0f }); m_controllerDirty = true; }

    ImGui::SeparatorText("States");
    int removeState = -1;
    for (std::size_t i = 0; i < m_asset.states.size(); ++i) {
        auto& s = m_asset.states[i];
        ImGui::PushID(6000 + static_cast<int>(i));
        std::array<char, 64> nb{}; Copy(nb, s.name);
        if (ImGui::InputText("State Name", nb.data(), nb.size())) { s.name = nb.data(); m_controllerDirty = true; }
        clipNameCombo("Clip", s.clipName);
        if (ImGui::Checkbox("Loop", &s.loop)) m_controllerDirty = true;
        ImGui::SameLine(); ImGui::SetNextItemWidth(90);
        if (ImGui::DragFloat("Speed", &s.speed, .02f, 0.0f, 8.0f)) m_controllerDirty = true;
        int blendMode = s.blendParameter.empty() ? 0 : (s.blendSpace2D ? 2 : 1);
        const char* blendModes[] = { "None", "1D Blend Space", "2D Blend Space" };
        if (ImGui::Combo("Blend Space", &blendMode, blendModes, 3)) {
            if (blendMode == 0) {
                s.blendParameter.clear();
                s.blendParameterY.clear();
                s.blendSpace2D = false;
            } else {
                if (s.blendParameter.empty()) s.blendParameter = "Speed";
                s.blendSpace2D = blendMode == 2;
                if (s.blendSpace2D && s.blendParameterY.empty()) s.blendParameterY = "Direction";
                if (!s.blendSpace2D) s.blendParameterY.clear();
            }
            m_controllerDirty = true;
        }
        if (blendMode != 0) {
            std::array<char, 48> bp{}; Copy(bp, s.blendParameter);
            ImGui::SetNextItemWidth(120);
            if (ImGui::InputText("X Parameter", bp.data(), bp.size())) { s.blendParameter = bp.data(); m_controllerDirty = true; }
            if (s.blendSpace2D) {
                std::array<char, 48> bpy{}; Copy(bpy, s.blendParameterY);
                ImGui::SetNextItemWidth(120);
                if (ImGui::InputText("Y Parameter", bpy.data(), bpy.size())) {
                    s.blendParameterY = bpy.data();
                    m_controllerDirty = true;
                }
            }
            if (ImGui::Checkbox("Synchronize Samples", &s.synchronizeBlendSpace))
                m_controllerDirty = true;
            int removeSample = -1;
            for (std::size_t j = 0; j < s.blendSamples.size(); ++j) {
                auto& sample = s.blendSamples[j];
                ImGui::PushID(static_cast<int>(j));
                clipNameCombo("##sc", sample.clipName);
                ImGui::SameLine(); ImGui::SetNextItemWidth(90);
                if (ImGui::DragFloat("X", &sample.value, .05f)) m_controllerDirty = true;
                if (s.blendSpace2D) {
                    ImGui::SameLine(); ImGui::SetNextItemWidth(90);
                    if (ImGui::DragFloat("Y", &sample.valueY, .05f)) m_controllerDirty = true;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("X##RemoveSample")) removeSample = static_cast<int>(j);
                ImGui::PopID();
            }
            if (removeSample >= 0) { s.blendSamples.erase(s.blendSamples.begin() + removeSample); m_controllerDirty = true; }
            if (ImGui::SmallButton("Add Sample")) { s.blendSamples.push_back({}); m_controllerDirty = true; }
        }
        if (ImGui::SmallButton("Remove State")) removeState = static_cast<int>(i);
        ImGui::Separator();
        ImGui::PopID();
    }
    if (removeState >= 0) { m_asset.states.erase(m_asset.states.begin() + removeState); m_controllerDirty = true; }
    if (ImGui::Button("Add State")) { EditorScene::AnimationStateNode s; s.name = "State" + std::to_string(m_asset.states.size() + 1); m_asset.states.push_back(std::move(s)); m_controllerDirty = true; }
    ImGui::SameLine();
    if (ImGui::Button("Create 1D Locomotion")) {
        // Build a single locomotion blend-space state from clips named Idle/Walk/Run.
        EditorScene::AnimationStateNode loco;
        loco.name = "Locomotion"; loco.blendParameter = "Speed"; loco.synchronizeBlendSpace = true;
        auto has = [&](const char* n) { for (const auto& c : m_asset.clips) if (c.clipName == n) return true; return false; };
        if (has("Idle")) loco.blendSamples.push_back({0, "Idle", 0.0f, 0.0f});
        if (has("Walk")) loco.blendSamples.push_back({0, "Walk", 1.5f, 0.0f});
        if (has("Run"))  loco.blendSamples.push_back({0, "Run", 4.0f, 0.0f});
        if (has("Idle")) loco.clipName = "Idle";
        m_asset.states.push_back(std::move(loco));
        bool hasSpeed = false; for (const auto& p : m_asset.parameters) if (p.name == "Speed") hasSpeed = true;
        if (!hasSpeed) m_asset.parameters.push_back({ "Speed", EditorScene::AnimationParameter::Type::Float, 0.0f });
        m_controllerDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Create 2D Locomotion")) {
        EditorScene::AnimationStateNode loco;
        loco.name = "Locomotion";
        loco.blendParameter = "Speed";
        loco.blendParameterY = "Direction";
        loco.blendSpace2D = true;
        loco.synchronizeBlendSpace = true;
        const auto find = [&](const std::initializer_list<const char*> words) -> std::string {
            for (const AnimationGraphClip& c : m_asset.clips) {
                const std::string name = Lower(c.clipName);
                for (const char* word : words)
                    if (name.find(Lower(word)) != std::string::npos) return c.clipName;
            }
            return {};
        };
        const std::string idle = find({"idle"});
        const std::string forward = find({"forward", "walk"});
        const std::string backward = find({"backward", "back"});
        const std::string left = find({"left", "strafe left"});
        const std::string right = find({"right", "strafe right"});
        if (!idle.empty()) loco.blendSamples.push_back({0, idle, 0.0f, 0.0f});
        if (!forward.empty()) loco.blendSamples.push_back({0, forward, 1.0f, 0.0f});
        if (!backward.empty()) loco.blendSamples.push_back({0, backward, 1.0f, -180.0f});
        if (!left.empty()) loco.blendSamples.push_back({0, left, 1.0f, -90.0f});
        if (!right.empty()) loco.blendSamples.push_back({0, right, 1.0f, 90.0f});
        loco.clipName = !idle.empty() ? idle : (!forward.empty() ? forward : std::string());
        m_asset.states.push_back(std::move(loco));
        const auto ensureFloat = [&](const char* name) {
            const bool found = std::any_of(m_asset.parameters.begin(), m_asset.parameters.end(),
                [&](const auto& p) { return p.name == name; });
            if (!found) m_asset.parameters.push_back({
                name, EditorScene::AnimationParameter::Type::Float, 0.0f});
        };
        ensureFloat("Speed");
        ensureFloat("Direction");
        m_controllerDirty = true;
    }

    ImGui::SeparatorText("Transitions");
    const char* compares[] = { ">=", "<", "==", "!=", "<=", ">" };
    int removeTransition = -1;
    for (std::size_t i = 0; i < m_asset.transitions.size(); ++i) {
        auto& t = m_asset.transitions[i];
        ImGui::PushID(7000 + static_cast<int>(i));
        const auto stateCombo = [&](const char* label, std::string& value, bool any) {
            if (ImGui::BeginCombo(label, value.empty() ? (any ? "Any" : "None") : value.c_str())) {
                if (any && ImGui::Selectable("Any", value.empty())) { value.clear(); m_controllerDirty = true; }
                for (std::size_t stateIndex = 0; stateIndex < m_asset.states.size(); ++stateIndex) {
                    const auto& state = m_asset.states[stateIndex];
                    ImGui::PushID(static_cast<int>(stateIndex));
                    if (ImGui::Selectable(state.name.c_str(), value == state.name)) {
                        value = state.name;
                        m_controllerDirty = true;
                    }
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
        };
        stateCombo("From", t.fromState, true); stateCombo("To", t.toState, false);
        ImGui::SeparatorText("Conditions");
        int conditionMode = t.requireAllConditions ? 0 : 1;
        const char* conditionModes[] = { "All conditions (AND)", "Any condition (OR)" };
        if (ImGui::Combo("Match", &conditionMode, conditionModes, 2)) {
            t.requireAllConditions = conditionMode == 0;
            m_controllerDirty = true;
        }
        ImGui::TextDisabled("Condition 1");
        if (ImGui::BeginCombo("Parameter", t.parameter.empty() ? "None" : t.parameter.c_str())) {
            for (std::size_t parameterIndex = 0; parameterIndex < m_asset.parameters.size(); ++parameterIndex) {
                const auto& parameter = m_asset.parameters[parameterIndex];
                ImGui::PushID(static_cast<int>(parameterIndex));
                if (ImGui::Selectable(parameter.name.c_str(), t.parameter == parameter.name)) {
                    t.parameter = parameter.name;
                    m_controllerDirty = true;
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        EditorScene::AnimationParameter::Type paramType = EditorScene::AnimationParameter::Type::Float;
        for (const auto& p : m_asset.parameters) if (p.name == t.parameter) { paramType = p.type; break; }
        using Comp = EditorScene::AnimationStateTransition::Compare;
        if (paramType == EditorScene::AnimationParameter::Type::Bool || paramType == EditorScene::AnimationParameter::Type::Trigger) {
            const char* conds[] = { "is false", "is true (or triggered)" };
            int sel = (t.threshold >= 0.5f) ? 1 : 0;
            ImGui::Combo("Condition", &sel, conds, 2);
            const float want = sel ? 1.0f : 0.0f;
            if (t.compare != Comp::Equal || t.threshold != want) { t.compare = Comp::Equal; t.threshold = want; m_controllerDirty = true; }
        } else {
            int compare = std::clamp(static_cast<int>(t.compare), 0, 5);
            if (ImGui::Combo("Compare", &compare, compares, 6)) { t.compare = static_cast<Comp>(compare); m_controllerDirty = true; }
            if (ImGui::DragFloat("Threshold", &t.threshold, .05f)) m_controllerDirty = true;
        }
        int removeCondition = -1;
        for (std::size_t conditionIndex = 0;
             conditionIndex < t.additionalConditions.size(); ++conditionIndex) {
            auto& condition = t.additionalConditions[conditionIndex];
            ImGui::PushID(8000 + static_cast<int>(conditionIndex));
            ImGui::Separator();
            ImGui::TextDisabled("Condition %d", static_cast<int>(conditionIndex) + 2);
            if (ImGui::BeginCombo("Parameter",
                    condition.parameter.empty() ? "None" : condition.parameter.c_str())) {
                for (std::size_t parameterIndex = 0;
                     parameterIndex < m_asset.parameters.size(); ++parameterIndex) {
                    const auto& parameter = m_asset.parameters[parameterIndex];
                    ImGui::PushID(static_cast<int>(parameterIndex));
                    if (ImGui::Selectable(parameter.name.c_str(),
                            condition.parameter == parameter.name)) {
                        condition.parameter = parameter.name;
                        m_controllerDirty = true;
                    }
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            EditorScene::AnimationParameter::Type conditionType =
                EditorScene::AnimationParameter::Type::Float;
            for (const auto& parameter : m_asset.parameters) {
                if (parameter.name == condition.parameter) {
                    conditionType = parameter.type;
                    break;
                }
            }
            if (conditionType == EditorScene::AnimationParameter::Type::Bool
                || conditionType == EditorScene::AnimationParameter::Type::Trigger) {
                const char* conditions[] = { "is false", "is true (or triggered)" };
                int selected = condition.threshold >= 0.5f ? 1 : 0;
                if (ImGui::Combo("Condition", &selected, conditions, 2)) {
                    condition.compare =
                        EditorScene::AnimationStateTransition::Compare::Equal;
                    condition.threshold = selected ? 1.0f : 0.0f;
                    m_controllerDirty = true;
                }
            } else {
                int compare = std::clamp(static_cast<int>(condition.compare), 0, 5);
                if (ImGui::Combo("Compare", &compare, compares, 6)) {
                    condition.compare =
                        static_cast<EditorScene::AnimationStateTransition::Compare>(compare);
                    m_controllerDirty = true;
                }
                if (ImGui::DragFloat("Threshold", &condition.threshold, .05f))
                    m_controllerDirty = true;
            }
            if (ImGui::SmallButton("Remove Condition"))
                removeCondition = static_cast<int>(conditionIndex);
            ImGui::PopID();
        }
        if (removeCondition >= 0) {
            t.additionalConditions.erase(
                t.additionalConditions.begin() + removeCondition);
            m_controllerDirty = true;
        }
        if (ImGui::SmallButton("Add Condition")) {
            EditorScene::AnimationStateTransition::Condition condition;
            if (!m_asset.parameters.empty()) {
                const auto& param = m_asset.parameters.front();
                condition.parameter = param.name;
                using PT = EditorScene::AnimationParameter::Type;
                if (param.type == PT::Bool || param.type == PT::Trigger) {
                    condition.compare = Comp::Equal; condition.threshold = 1.0f;
                } else {
                    condition.compare = Comp::Greater; condition.threshold = 0.1f;
                }
            }
            t.additionalConditions.push_back(std::move(condition));
            m_controllerDirty = true;
        }
        ImGui::Separator();
        if (ImGui::DragFloat("Fade", &t.fade, .01f, 0.0f, 5.0f)) m_controllerDirty = true;
        if (ImGui::SliderFloat("Exit Time", &t.exitTime, 0.0f, 1.0f)) m_controllerDirty = true;
        if (ImGui::Checkbox("Interrupt", &t.canInterrupt)) m_controllerDirty = true;
        if (ImGui::SmallButton("Remove Transition")) removeTransition = static_cast<int>(i);
        ImGui::Separator();
        ImGui::PopID();
    }
    if (removeTransition >= 0) { m_asset.transitions.erase(m_asset.transitions.begin() + removeTransition); m_controllerDirty = true; }
    if (ImGui::Button("Add Transition") && !m_asset.states.empty()) {
        EditorScene::AnimationStateTransition t;
        t.fromState = m_asset.states.front().name;
        t.toState = m_asset.states.size() > 1 ? m_asset.states[1].name : m_asset.states.front().name;
        if (!m_asset.parameters.empty()) {
            const auto& param = m_asset.parameters.front();
            t.parameter = param.name;
            // Avoid an always-true default: "Speed >= 0" fires on frame 1 and skips the
            // state. Bools/triggers compare == true; numeric params fire only above a small
            // threshold so a resting character stays put.
            using PT = EditorScene::AnimationParameter::Type;
            using Comp = EditorScene::AnimationStateTransition::Compare;
            if (param.type == PT::Bool || param.type == PT::Trigger) {
                t.compare = Comp::Equal; t.threshold = 1.0f;
            } else {
                t.compare = Comp::Greater; t.threshold = 0.1f;
            }
        }
        m_asset.transitions.push_back(std::move(t)); m_controllerDirty = true;
    }

    ImGui::EndChild();
    ImGui::End();
}
