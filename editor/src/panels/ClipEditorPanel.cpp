#include "ClipEditorPanel.h"
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
#include <cstdint>
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
}  // namespace

ClipEditorPanel::~ClipEditorPanel() = default;

void ClipEditorPanel::QueueOpen(const std::string& path) { m_pendingOpen = path; }

void ClipEditorPanel::QueueSource(const std::string& animationPath,
                                  const std::string& previewMeshPath) {
    m_pendingSource = animationPath;
    m_pendingPreviewMesh = previewMeshPath;
}

void ClipEditorPanel::SyncBuffers() { Copy(m_nameBuffer, m_asset.name); }

void ClipEditorPanel::ResetPreview() {
    m_sourceModel = nullptr;
    m_loadedSource.clear();
    m_pose.clear();
    m_time = 0.0f;
}

void ClipEditorPanel::RefreshChoices(const std::string& assetRoot) {
    m_modelChoices.clear();
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
            // Native engine-imported assets only: a .3dgskmesh (skeleton + mesh +
            // embedded clips) or a .3dganim (individual imported clip).
            if (ext == ".3dgskmesh" || ext == ".3dganim") {
                m_modelChoices.push_back({it->path().generic_string(), it->path().filename().string()});
            }
        }
        it.increment(ec);
    }
    std::sort(m_modelChoices.begin(), m_modelChoices.end(),
              [](const AssetChoice& a, const AssetChoice& b) { return Lower(a.displayName) < Lower(b.displayName); });
}

unsigned int ClipEditorPanel::RenderPreview(int width, int height, float deltaTime) {
    width = std::max(width, 32);
    height = std::max(height, 32);
    if (!m_fbo) m_fbo.emplace(width, height, GL_RGBA8, true);
    else m_fbo->Resize(width, height);
    if (!m_renderer) m_renderer = std::make_unique<engine::SkinnedRenderer>();

    // (Re)load the source for clip enumeration. A .3dgskmesh is self-contained
    // (skeleton + mesh + embedded clips); a .3dganim carries clips only, so it is
    // merged onto the chosen Preview Mesh's skeleton to enumerate and render it.
    const std::string sourceExt = Lower(std::filesystem::path(m_asset.sourceFile).extension().string());
    const bool animOnly = sourceExt == ".3dganim";
    const std::string loadKey = m_asset.sourceFile + '|' + m_previewMeshPath + '|'
        + (m_asset.stripRootMotion ? "1" : "0") + '|' + m_asset.clipName;
    if (m_loadedKey != loadKey) {
        ResetPreview();
        m_loadedKey = loadKey;
        m_loadedSource = m_asset.sourceFile;
        m_sourceIsAnimOnly = animOnly;
        m_error.clear();
        if (m_asset.sourceFile.empty()) {
            m_sourceModel = nullptr;
        } else if (animOnly) {
            if (m_previewMeshPath.empty()) {
                m_sourceModel = nullptr;   // needs a Preview Mesh to supply the skeleton
                m_error = "Pick a Preview Mesh (.3dgskmesh) to view a .3dganim clip.";
            } else {
                std::vector<engine::RuntimeAssetManager::SkinnedAnimationSource> src;
                const std::string alias = m_asset.name.empty() ? m_asset.clipName : m_asset.name;
                src.push_back({m_asset.sourceFile, alias, m_asset.stripRootMotion, m_asset.clipName});
                m_sourceModel = m_assets.LoadSkinnedModel(m_previewMeshPath, src, &m_error);
            }
        } else {
            m_sourceModel = m_assets.LoadSkinnedModel(m_asset.sourceFile, &m_error);
        }
        m_clipIndex = 0;
        if (m_sourceModel && !m_asset.clipName.empty()) {
            const auto& clips = m_sourceModel->Animations();
            for (std::size_t i = 0; i < clips.size(); ++i) {
                if (clips[i].name == m_asset.clipName) {
                    m_clipIndex = static_cast<int>(i);
                    break;
                }
            }
        }
    }

    // Pick the model to actually draw + which clip to play. A meshless source (e.g. a
    // Mixamo "without skin" clip) is previewed by merging the clip onto a preview rig.
    const engine::SkinnedModel* renderModel = m_sourceModel;
    int playClip = m_clipIndex;
    const bool sourceHasMesh = m_sourceModel && !m_sourceModel->SubMeshes().empty();
    const std::string previewBase = sourceHasMesh ? m_asset.sourceFile : m_previewMeshPath;
    if (m_sourceIsAnimOnly && m_sourceModel) {
        // Already merged onto the preview mesh in the load step; play the merged clip
        // (appended last, or matched by name).
        const auto& anims = m_sourceModel->Animations();
        playClip = anims.empty() ? -1 : static_cast<int>(anims.size()) - 1;
        for (std::size_t i = 0; i < anims.size(); ++i)
            if (!m_asset.clipName.empty() && anims[i].name == m_asset.clipName) {
                playClip = static_cast<int>(i);
                break;
            }
    } else if (m_sourceModel && !previewBase.empty()
        && (!sourceHasMesh || m_asset.stripRootMotion)) {
        std::vector<engine::RuntimeAssetManager::SkinnedAnimationSource> src;
        const std::string alias = m_asset.name.empty() ? m_asset.clipName : m_asset.name;
        src.push_back({
            m_asset.sourceFile, alias, m_asset.stripRootMotion, m_asset.clipName});
        std::string mergeError;
        if (const engine::SkinnedModel* merged =
                m_assets.LoadSkinnedModel(previewBase, src, &mergeError)) {
            renderModel = merged;
            const auto& anims = merged->Animations();
            playClip = anims.empty() ? -1 : static_cast<int>(anims.size()) - 1;   // merged clip is last
            for (std::size_t i = 0; i < anims.size(); ++i) {
                if (!m_asset.clipName.empty() && anims[i].name == m_asset.clipName) {
                    playClip = static_cast<int>(i);
                    break;
                }
            }
        }
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

    if (renderModel && !renderModel->SubMeshes().empty()) {
        const auto& anims = renderModel->Animations();
        if (playClip >= 0 && playClip < static_cast<int>(anims.size())) {
            const engine::Animation& clip = anims[static_cast<std::size_t>(playClip)];
            const float tps = clip.ticksPerSecond > 0.0f ? clip.ticksPerSecond : 25.0f;
            const float durationSeconds = clip.duration > 0.0f ? clip.duration / tps : 0.0f;
            if (m_playing) m_time += deltaTime * std::max(m_asset.speed, 0.0f);
            if (!m_asset.loop && durationSeconds > 0.0f && m_time > durationSeconds) {
                m_time = durationSeconds;   // hold on the last frame when not looping
            }
            engine::Animator::ComputePose(renderModel->GetSkeleton(), clip, m_time, m_pose);
        } else {
            engine::Animator::ComputeBindPose(renderModel->GetSkeleton(), m_pose);
        }

        const float radius = std::max(renderModel->BoundingRadius(), 0.001f);
        glm::mat4 model(1.0f);
        model = glm::rotate(model, glm::radians(m_yaw), glm::vec3(0, 1, 0));
        model = glm::rotate(model, glm::radians(m_pitch), glm::vec3(1, 0, 0));
        const glm::vec3 size = renderModel->Max() - renderModel->Min();
        if (size.z > size.y * 1.25f) model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(1, 0, 0));
        model = glm::scale(model, glm::vec3((0.9f * m_zoom) / radius));
        model = glm::translate(model, -renderModel->Center());

        engine::Camera camera(glm::vec3(0.0f, 0.0f, 2.5f));
        camera.LookAt(glm::vec3(0.0f));
        m_renderer->Draw(*renderModel, m_pose, model, camera,
            static_cast<float>(width) / static_cast<float>(height),
            glm::normalize(glm::vec3(0.45f, -1.0f, -0.35f)),
            glm::vec3(1.0f, 0.96f, 0.90f), glm::vec3(0.16f));
    }

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(oldFbo));
    glViewport(oldViewport[0], oldViewport[1], oldViewport[2], oldViewport[3]);
    if (depthWas) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (cullWas) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    return m_fbo->ColorTexture();
}

void ClipEditorPanel::Draw(const std::string& assetRoot, bool* open, bool* assetSaved,
                           std::string* message, float deltaTime) {
    if (m_scannedRoot != assetRoot) RefreshChoices(assetRoot);
    if (!m_pendingSource.empty()) {
        m_asset = {};
        m_asset.sourceFile = m_pendingSource;
        m_previewMeshPath = m_pendingPreviewMesh;
        m_path.clear();
        SyncBuffers();
        ResetPreview();
        m_pendingSource.clear();
        m_pendingPreviewMesh.clear();
    }
    if (!m_pendingOpen.empty()) {
        std::string error;
        if (m_asset.Load(m_pendingOpen, &error)) { m_path = m_pendingOpen; SyncBuffers(); ResetPreview(); }
        else if (message) *message = error;
        m_pendingOpen.clear();
    }
    if (m_path.empty()) {
        m_path = (std::filesystem::path(assetRoot) / "Assets" / "Animations" / "Clip.3dgclip").string();
        SyncBuffers();
    }

    if (!ImGui::Begin(EditorPanels::Name(EditorPanels::Panel::ClipEditor), open, ImGuiWindowFlags_MenuBar)) { ImGui::End(); return; }
    if (ImGui::BeginMenuBar()) {
        if (ImGui::MenuItem("New")) { m_asset = {}; m_path.clear(); SyncBuffers(); ResetPreview(); }
        if (ImGui::MenuItem("Save")) {
            m_asset.name = m_nameBuffer.data();
            // Imported files frequently expose a generic internal take such as
            // "Unreal Take". Use the .3dgclip filename as the authored name unless
            // the user supplied a meaningful custom name.
            if (m_asset.name.empty() || m_asset.name == "Clip"
                || m_asset.name == m_asset.clipName) {
                const std::string stem = std::filesystem::path(m_path).stem().string();
                if (!stem.empty()) {
                    m_asset.name = stem;
                    SyncBuffers();
                }
            }
            std::string error;
            if (m_asset.Save(m_path, &error)) { if (assetSaved) *assetSaved = true; if (message) *message = "Saved clip: " + m_path; }
            else if (message) *message = error;
        }
        ImGui::EndMenuBar();
    }

    ImGui::SetNextItemWidth(-1.0f);
    std::array<char, 260> pathBuf{}; Copy(pathBuf, m_path);
    if (ImGui::InputText("##ClipPath", pathBuf.data(), pathBuf.size())) m_path = pathBuf.data();
    ImGui::Separator();

    // Searchable asset picker.
    const auto drawPicker = [&](const char* label, std::array<char, 128>& search,
                                std::string& value) {
        bool picked = false;
        const std::string preview = value.empty() ? std::string("None")
            : std::filesystem::path(value).filename().string();
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo(label, preview.c_str())) {
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint("##search", "Search...", search.data(), search.size());
            ImGui::Separator();
            if (ImGui::Selectable("None", value.empty())) {
                value.clear(); picked = true; ImGui::CloseCurrentPopup();
            }
            const std::string filter = Lower(search.data());
            for (const AssetChoice& choice : m_modelChoices) {
                if (!filter.empty() && Lower(choice.displayName).find(filter) == std::string::npos) continue;
                if (ImGui::Selectable(choice.displayName.c_str(), value == choice.path)) {
                    value = choice.path; picked = true; ImGui::CloseCurrentPopup();
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", choice.path.c_str());
            }
            ImGui::EndCombo();
        }
        return picked;
    };

    // Live preview on the left.
    ImGui::BeginChild("ClipPreview", ImVec2(-320.0f, 0), true);
    ImGui::TextDisabled("PREVIEW");
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float side = std::max(160.0f, std::min(avail.x, avail.y - 60.0f));
    const unsigned int texture = RenderPreview(static_cast<int>(side), static_cast<int>(side), deltaTime);
    ImGui::Image((ImTextureID)(std::intptr_t)texture, ImVec2(side, side), ImVec2(0, 1), ImVec2(1, 0));
    if (ImGui::IsItemHovered()) {
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            const ImVec2 d = ImGui::GetIO().MouseDelta;
            m_yaw += d.x * 0.45f;
            m_pitch = std::clamp(m_pitch - d.y * 0.35f, -85.0f, 85.0f);
        }
        if (ImGui::GetIO().MouseWheel != 0.0f)
            m_zoom = std::clamp(m_zoom + ImGui::GetIO().MouseWheel * 0.1f, 0.4f, 3.0f);
    }
    if (!m_error.empty()) {
        ImGui::TextColored(ImVec4(1.0f, .4f, .3f, 1.0f), "Load failed: %s", m_error.c_str());
    } else if (m_sourceModel && m_sourceModel->SubMeshes().empty() && m_previewMeshPath.empty()) {
        ImGui::TextColored(ImVec4(1.0f, .72f, .25f, 1.0f), "Source has no mesh - set a Preview Mesh to view it.");
    }
    if (ImGui::Button(m_playing ? "Pause" : "Play")) m_playing = !m_playing;
    ImGui::SameLine();
    if (ImGui::Button("Restart")) m_time = 0.0f;
    ImGui::SameLine();
    if (ImGui::Button("Reset View")) { m_yaw = 0.0f; m_pitch = 0.0f; m_zoom = 1.0f; }
    ImGui::EndChild();
    ImGui::SameLine();

    // Settings on the right.
    ImGui::BeginChild("ClipSettings", ImVec2(0, 0), true);
    ImGui::TextDisabled("CLIP");
    if (ImGui::InputText("Name", m_nameBuffer.data(), m_nameBuffer.size())) m_asset.name = m_nameBuffer.data();

    ImGui::SeparatorText("Source");
    if (drawPicker("Source File", m_sourceSearch, m_asset.sourceFile)) {
        m_asset.sourceAssetId = {};
        if (Lower(std::filesystem::path(m_asset.sourceFile).extension().string())
                == ".3dganim") {
            engine::AnimationAssetData animation;
            std::string ignored;
            if (engine::LoadAnimationAsset(
                    m_asset.sourceFile, &animation, &ignored)) {
                bool currentCompatible = false;
                if (!m_previewMeshPath.empty()) {
                    engine::SkeletalMeshAssetData current;
                    currentCompatible = engine::LoadSkeletalMeshAsset(
                        m_previewMeshPath, &current, &ignored)
                        && current.skeletonId == animation.skeletonId;
                }
                if (!currentCompatible) {
                    m_previewMeshPath.clear();
                    for (const AssetChoice& choice : m_modelChoices) {
                        if (Lower(std::filesystem::path(choice.path)
                                .extension().string()) != ".3dgskmesh")
                            continue;
                        engine::SkeletalMeshAssetData mesh;
                        if (engine::LoadSkeletalMeshAsset(
                                choice.path, &mesh, &ignored)
                            && mesh.skeletonId == animation.skeletonId) {
                            m_previewMeshPath = choice.path;
                            break;
                        }
                    }
                }
            }
        }
        ResetPreview();
    }
    if (ImGui::Button("Refresh Files")) RefreshChoices(assetRoot);

    // Clip selection from the source's embedded clips.
    const char* clipPreview = m_asset.clipName.empty() ? "(first clip)" : m_asset.clipName.c_str();
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("Clip", clipPreview)) {
        if (m_sourceModel && m_sourceModel->AnimationCount() > 0) {
            const auto& anims = m_sourceModel->Animations();
            for (std::size_t i = 0; i < anims.size(); ++i) {
                ImGui::PushID(static_cast<int>(i));
                const std::string label = anims[i].name.empty() ? ("Clip " + std::to_string(i)) : anims[i].name;
                if (ImGui::Selectable(label.c_str(), m_clipIndex == static_cast<int>(i))) {
                    m_clipIndex = static_cast<int>(i);
                    m_asset.clipName = anims[i].name;
                    m_time = 0.0f;
                }
                ImGui::PopID();
            }
        } else {
            ImGui::TextDisabled("Pick a source file with animation clips.");
        }
        ImGui::EndCombo();
    }

    ImGui::SeparatorText("Settings");
    ImGui::Checkbox("Strip Root Motion", &m_asset.stripRootMotion);
    if (ImGui::Checkbox("Action Clip", &m_asset.action) && m_asset.action) {
        m_asset.loop = false;
    }
    if (m_asset.action) {
        ImGui::TextDisabled("One-shot clip callable from scripts; it is not a graph state.");
        m_asset.loop = false;
        ImGui::BeginDisabled();
        ImGui::Checkbox("Loop", &m_asset.loop);
        ImGui::EndDisabled();
        const char* maskPreview =
            m_asset.maskRootBone.empty() ? "Full Body (locks movement)"
                                         : m_asset.maskRootBone.c_str();
        if (ImGui::BeginCombo("Body Mask", maskPreview)) {
            if (ImGui::Selectable("Full Body (locks movement)",
                                  m_asset.maskRootBone.empty())) {
                m_asset.maskRootBone.clear();
            }
            if (m_sourceModel) {
                const auto& bones = m_sourceModel->GetSkeleton().bones;
                for (const auto& bone : bones) {
                    if (bone.name.empty()) continue;
                    if (ImGui::Selectable(
                            bone.name.c_str(), m_asset.maskRootBone == bone.name)) {
                        m_asset.maskRootBone = bone.name;
                    }
                }
            }
            ImGui::EndCombo();
        }
        std::array<char, 128> maskBuffer{};
        Copy(maskBuffer, m_asset.maskRootBone);
        if (ImGui::InputText("Mask Root Bone", maskBuffer.data(), maskBuffer.size())) {
            m_asset.maskRootBone = maskBuffer.data();
        }
        ImGui::DragFloat("Fade In", &m_asset.fadeIn, 0.01f, 0.0f, 5.0f, "%.2f s");
        ImGui::DragFloat("Fade Out", &m_asset.fadeOut, 0.01f, 0.0f, 5.0f, "%.2f s");
    } else {
        ImGui::Checkbox("Loop", &m_asset.loop);
    }
    ImGui::DragFloat("Base Playback Speed", &m_asset.speed, 0.02f, 0.0f, 8.0f);

    ImGui::SeparatorText("Events / Notifies");
    ImGui::TextDisabled(
        "Events fire once when playback crosses their time. Scripts can read them "
        "with WasAnimationEvent().");
    float clipDuration = 0.0f;
    if (m_sourceModel && m_clipIndex >= 0
        && m_clipIndex < static_cast<int>(m_sourceModel->Animations().size())) {
        const engine::Animation& clip =
            m_sourceModel->Animations()[static_cast<std::size_t>(m_clipIndex)];
        const float ticksPerSecond =
            clip.ticksPerSecond > 0.0f ? clip.ticksPerSecond : 25.0f;
        clipDuration = clip.duration > 0.0f ? clip.duration / ticksPerSecond : 0.0f;
    }
    bool removeEvent = false;
    std::size_t removeEventIndex = 0;
    for (std::size_t i = 0; i < m_asset.events.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        AnimationClipAsset::Event& event = m_asset.events[i];
        ImGui::SetNextItemWidth(115.0f);
        ImGui::DragFloat("Time", &event.time, 0.01f, 0.0f,
            clipDuration > 0.0f ? clipDuration : 3600.0f, "%.3f s");
        event.time = std::max(event.time, 0.0f);
        if (clipDuration > 0.0f) event.time = std::min(event.time, clipDuration);
        ImGui::SameLine();
        std::array<char, 128> eventName{};
        Copy(eventName, event.name);
        ImGui::SetNextItemWidth(-105.0f);
        if (ImGui::InputTextWithHint("##EventName", "Event name",
                                     eventName.data(), eventName.size())) {
            event.name = eventName.data();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Set Here")) {
            event.time = clipDuration > 0.0f
                ? std::clamp(m_time, 0.0f, clipDuration)
                : std::max(m_time, 0.0f);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("X")) {
            removeEvent = true;
            removeEventIndex = i;
        }
        ImGui::PopID();
    }
    if (removeEvent) {
        m_asset.events.erase(
            m_asset.events.begin() + static_cast<std::ptrdiff_t>(removeEventIndex));
    }
    if (ImGui::Button("Add Event")) {
        m_asset.events.push_back(AnimationClipAsset::Event{
            clipDuration > 0.0f ? std::clamp(m_time, 0.0f, clipDuration)
                                : std::max(m_time, 0.0f),
            "Event"});
    }
    ImGui::SameLine();
    if (clipDuration > 0.0f) {
        ImGui::TextDisabled("Preview: %.3f / %.3f s", m_time, clipDuration);
    } else {
        ImGui::TextDisabled("Preview: %.3f s", m_time);
    }

    ImGui::SeparatorText("Preview (not saved)");
    ImGui::TextDisabled("Rig to show the animation on when the source has no mesh.");
    drawPicker("Preview Mesh", m_meshSearch, m_previewMeshPath);

    ImGui::EndChild();
    ImGui::End();
}
