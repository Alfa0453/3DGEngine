#include "AnimationRetargetingPanel.h"

#include "EditorAssets.h"

#include <engine/assets/AssetReference.h>
#include <engine/assets/AssetRegistry.h>
#include <imgui.h>
#include <imgui_stdlib.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>

namespace {
std::string FullPath(const std::string& root, const std::string& path) {
    const std::filesystem::path p(path);
    return p.is_absolute() ? p.lexically_normal().string()
                           : (std::filesystem::path(root) / p).lexically_normal().string();
}

std::string SafeName(std::string value) {
    for (char& c : value)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-')) c = '_';
    return value.empty() ? "Retargeted" : value;
}

bool AssetCombo(const char* label, std::string& value,
                const std::vector<std::string>& paths) {
    bool changed = false;
    const std::string preview = value.empty() ? "Choose asset..."
        : std::filesystem::path(value).filename().string();
    if (ImGui::BeginCombo(label, preview.c_str())) {
        for (const std::string& path : paths) {
            const bool selected = path == value;
            const std::string display = std::filesystem::path(path).filename().string();
            if (ImGui::Selectable(display.c_str(), selected)) { value = path; changed = true; }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
}

engine::Animation ReorderChannels(const engine::NamedAnimationClipData& clip,
                                  const engine::Skeleton& skeleton) {
    engine::Animation result = clip.animation;
    result.channels.assign(skeleton.bones.size(), {});
    for (std::size_t i = 0; i < clip.animation.channels.size(); ++i) {
        int target = -1;
        if (i < clip.channelBoneNames.size()) target = skeleton.Find(clip.channelBoneNames[i]);
        else if (i < skeleton.bones.size()) target = static_cast<int>(i);
        if (target >= 0) result.channels[static_cast<std::size_t>(target)] = clip.animation.channels[i];
    }
    return result;
}

glm::vec3 SampleVec(const std::vector<engine::VecKey>& keys, float time,
                    const glm::vec3& fallback) {
    if (keys.empty()) return fallback;
    if (keys.size() == 1 || time <= keys.front().time) return keys.front().value;
    auto it = std::upper_bound(keys.begin(), keys.end(), time,
        [](float t, const engine::VecKey& key) { return t < key.time; });
    if (it == keys.end()) return keys.back().value;
    const engine::VecKey& b = *it; const engine::VecKey& a = *(it - 1);
    const float alpha = (time-a.time) / std::max(b.time-a.time, .00001f);
    return glm::mix(a.value, b.value, alpha);
}

glm::quat SampleQuat(const std::vector<engine::QuatKey>& keys, float time,
                     const glm::quat& fallback) {
    if (keys.empty()) return fallback;
    if (keys.size() == 1 || time <= keys.front().time) return keys.front().value;
    auto it = std::upper_bound(keys.begin(), keys.end(), time,
        [](float t, const engine::QuatKey& key) { return t < key.time; });
    if (it == keys.end()) return keys.back().value;
    const engine::QuatKey& b = *it; const engine::QuatKey& a = *(it - 1);
    const float alpha = (time-a.time) / std::max(b.time-a.time, .00001f);
    return glm::normalize(glm::slerp(a.value, b.value, alpha));
}

std::vector<glm::mat4> SampleWorldPose(const engine::Skeleton& skeleton,
                                       const engine::Animation* animation,
                                       float seconds) {
    std::vector<glm::mat4> world(skeleton.bones.size(), glm::mat4(1));
    float ticks = animation ? seconds * std::max(animation->ticksPerSecond, .0001f) : 0.f;
    if (animation && animation->duration > 0) ticks = std::fmod(ticks, animation->duration);
    for (std::size_t i=0;i<skeleton.bones.size();++i) {
        glm::vec3 scale(1), translation(0), skew(0); glm::quat rotation(1,0,0,0); glm::vec4 perspective;
        glm::decompose(skeleton.bones[i].localBind, scale, rotation, translation, skew, perspective);
        if (animation && i < animation->channels.size()) {
            const engine::BoneChannel& channel = animation->channels[i];
            translation = SampleVec(channel.positions, ticks, translation);
            rotation = SampleQuat(channel.rotations, ticks, rotation);
            scale = SampleVec(channel.scales, ticks, scale);
        }
        const glm::mat4 local = glm::translate(glm::mat4(1),translation)
            * glm::mat4_cast(rotation) * glm::scale(glm::mat4(1),scale);
        const int parent=skeleton.bones[i].parent;
        world[i]=parent>=0?world[static_cast<std::size_t>(parent)]*local:local;
    }
    return world;
}
}

bool AnimationRetargetingPanel::LoadInputs(const std::string& root, std::string* error) {
    if (m_sourceSkeletonPath.empty() || m_targetSkeletonPath.empty()) {
        if (error) *error = "Choose source and target skeletons.";
        return false;
    }
    if (!engine::LoadSkeletonAsset(FullPath(root, m_sourceSkeletonPath), &m_sourceSkeleton, error)
        || !engine::LoadSkeletonAsset(FullPath(root, m_targetSkeletonPath), &m_targetSkeleton, error))
        return false;
    if (!m_animationPath.empty()
        && !engine::LoadAnimationAsset(FullPath(root, m_animationPath), &m_sourceAnimation, error))
        return false;
    m_profile.sourceSkeletonPath = m_sourceSkeletonPath;
    m_profile.targetSkeletonPath = m_targetSkeletonPath;
    m_profile.sourceSkeletonId = m_sourceSkeleton.header.id;
    m_profile.targetSkeletonId = m_targetSkeleton.header.id;
    if (m_profile.sourceRootBone.empty() && !m_sourceSkeleton.skeleton.bones.empty())
        m_profile.sourceRootBone = m_sourceSkeleton.skeleton.bones.front().name;
    if (m_profile.targetRootBone.empty() && !m_targetSkeleton.skeleton.bones.empty())
        m_profile.targetRootBone = m_targetSkeleton.skeleton.bones.front().name;
    m_selectedClip = std::clamp(m_selectedClip, 0,
        std::max(0, static_cast<int>(m_sourceAnimation.clips.size()) - 1));
    m_inputsLoaded = true;
    return true;
}

bool AnimationRetargetingPanel::AutoMap(const std::string& root, std::string* error) {
    if (!LoadInputs(root, error)) return false;
    m_profile.mappings = engine::BuildAutomaticRetargetMap(
        m_sourceSkeleton.skeleton, m_targetSkeleton.skeleton);
    m_selectedMapping = m_profile.mappings.empty() ? -1 : 0;
    m_dirty = true;
    return !m_profile.mappings.empty();
}

bool AnimationRetargetingPanel::SaveProfile(const std::string& root, std::string* error) {
    if (!m_inputsLoaded && !LoadInputs(root, error)) return false;
    if (m_profile.name.empty()) m_profile.name = "RetargetProfile";
    if (m_profilePath.empty())
        m_profilePath = (std::filesystem::path(root) / "GameAssets" / "AnimationRetarget"
            / (SafeName(m_profile.name) + ".3dgretarget")).string();
    if (!engine::SaveAnimationRetargetAsset(m_profilePath, m_profile, error)) return false;
    m_dirty = false;
    return true;
}

bool AnimationRetargetingPanel::LoadProfile(const std::string& path, std::string* error) {
    engine::AnimationRetargetAssetData loaded;
    if (!engine::LoadAnimationRetargetAsset(path, &loaded, error)) return false;
    m_profile = std::move(loaded);
    m_profilePath = path;
    m_sourceSkeletonPath = m_profile.sourceSkeletonPath;
    m_targetSkeletonPath = m_profile.targetSkeletonPath;
    m_inputsLoaded = false;
    m_selectedMapping = m_profile.mappings.empty() ? -1 : 0;
    m_dirty = false;
    return true;
}

bool AnimationRetargetingPanel::RetargetSelected(const std::string& root, bool all,
                                                  std::string* error) {
    if (m_animationPath.empty()) { if (error) *error = "Choose a source animation asset."; return false; }
    if (!LoadInputs(root, error) || m_sourceAnimation.clips.empty()) {
        if (error && error->empty()) *error = "The source animation contains no clips.";
        return false;
    }
    engine::AnimationAssetData output;
    output.header.id = engine::AssetHandle::Generate();
    output.header.sourceHash = 0;
    output.skeletonId = m_targetSkeleton.header.id;
    const int first = all ? 0 : m_selectedClip;
    const int end = all ? static_cast<int>(m_sourceAnimation.clips.size()) : first + 1;
    for (int i = first; i < end; ++i) {
        engine::NamedAnimationClipData clip;
        const engine::Animation source = ReorderChannels(
            m_sourceAnimation.clips[static_cast<std::size_t>(i)], m_sourceSkeleton.skeleton);
        if (!engine::RetargetAnimationClip(m_sourceSkeleton.skeleton, source,
                m_targetSkeleton.skeleton, m_profile, &clip.animation, error)) return false;
        clip.animation.name += "_" + SafeName(m_profile.name);
        clip.channelBoneNames.reserve(m_targetSkeleton.skeleton.bones.size());
        for (const engine::Bone& bone : m_targetSkeleton.skeleton.bones)
            clip.channelBoneNames.push_back(bone.name);
        output.clips.push_back(std::move(clip));
    }
    const std::filesystem::path outputPath = std::filesystem::path(root)
        / "GameAssets" / "AnimationRetarget" / "Clips"
        / (SafeName(m_outputName) + ".3dganim");
    if (!engine::SaveAnimationAsset(outputPath.string(), output, error)) return false;
    return true;
}

void AnimationRetargetingPanel::DrawSkeletonComparison() {
    const ImVec2 size(ImGui::GetContentRegionAvail().x, 250.0f);
    ImGui::InvisibleButton("##RetargetPreview", size);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
    draw->AddRectFilled(min, max, IM_COL32(14, 18, 24, 255));
    draw->AddRect(min, max, IM_COL32(60, 70, 85, 255));
    if (!m_inputsLoaded) {
        draw->AddText(ImVec2(min.x + 12, min.y + 12), IM_COL32(185,185,185,255),
                      "Choose two skeletons and click Load / Auto Map.");
        return;
    }
    engine::Animation sourcePreview, targetPreview;
    const engine::Animation* sourceAnimation = nullptr;
    const engine::Animation* targetAnimation = nullptr;
    if (!m_sourceAnimation.clips.empty() && m_selectedClip >= 0
        && m_selectedClip < static_cast<int>(m_sourceAnimation.clips.size())) {
        sourcePreview = ReorderChannels(m_sourceAnimation.clips[static_cast<std::size_t>(m_selectedClip)],
                                        m_sourceSkeleton.skeleton);
        sourceAnimation = &sourcePreview;
        std::string ignored;
        if (engine::RetargetAnimationClip(m_sourceSkeleton.skeleton, sourcePreview,
                m_targetSkeleton.skeleton, m_profile, &targetPreview, &ignored))
            targetAnimation = &targetPreview;
    }
    const auto drawSkeleton = [&](const engine::Skeleton& skeleton,
                                  const engine::Animation* animation,
                                  float centerX, ImU32 color) {
        if (skeleton.bones.empty()) return;
        std::vector<glm::mat4> world = SampleWorldPose(skeleton, animation, m_previewTime);
        std::vector<glm::vec3> points(skeleton.bones.size());
        glm::vec2 lo(1e9f), hi(-1e9f);
        for (std::size_t i = 0; i < skeleton.bones.size(); ++i) {
            points[i] = glm::vec3(world[i][3]); lo = glm::min(lo, glm::vec2(points[i]));
            hi = glm::max(hi, glm::vec2(points[i]));
        }
        const glm::vec2 span = glm::max(hi - lo, glm::vec2(0.01f));
        const float scale = std::min(size.x * 0.38f / span.x, (size.y - 28) / span.y);
        auto project = [&](const glm::vec3& p) { return ImVec2(centerX + (p.x-(lo.x+hi.x)*.5f)*scale,
            max.y-14-(p.y-lo.y)*scale); };
        for (std::size_t i=0;i<skeleton.bones.size();++i) if (skeleton.bones[i].parent>=0)
            draw->AddLine(project(points[static_cast<std::size_t>(skeleton.bones[i].parent)]),
                          project(points[i]), color, 2.0f);
    };
    drawSkeleton(m_sourceSkeleton.skeleton, sourceAnimation,
                 min.x + size.x * .25f, IM_COL32(80,170,255,255));
    drawSkeleton(m_targetSkeleton.skeleton, targetAnimation,
                 min.x + size.x * .75f, IM_COL32(255,170,60,255));
    draw->AddText(ImVec2(min.x+10,min.y+8), IM_COL32(80,170,255,255), "SOURCE");
    draw->AddText(ImVec2(min.x+size.x*.5f+10,min.y+8), IM_COL32(255,170,60,255), "TARGET");
}

void AnimationRetargetingPanel::Draw(EditorAssets& assets, const std::string& root,
                                     bool* open, bool* changed, std::string* message) {
    if (changed) *changed = false;
    if (!m_queuedPath.empty()) {
        std::string error;
        m_status = LoadProfile(m_queuedPath, &error) ? "Loaded retarget profile." : error;
        m_queuedPath.clear();
    }
    if (!ImGui::Begin("Animation Retargeting", open)) { ImGui::End(); return; }
    const auto skeletons = assets.ContentAssetPaths(EditorAssets::Type::Skeleton);
    const auto animations = assets.ContentAssetPaths(EditorAssets::Type::Animation);
    ImGui::InputText("Profile Name", &m_profile.name);
    if (AssetCombo("Source Skeleton", m_sourceSkeletonPath, skeletons)) m_inputsLoaded = false;
    if (AssetCombo("Target Skeleton", m_targetSkeletonPath, skeletons)) m_inputsLoaded = false;
    AssetCombo("Source Animation", m_animationPath, animations);
    if (ImGui::Button("Load / Auto Map")) {
        std::string error; m_status = AutoMap(root, &error)
            ? "Mapped " + std::to_string(m_profile.mappings.size()) + " bones." : error;
    }
    ImGui::SameLine();
    if (ImGui::Button("Save Profile")) {
        std::string error;
        if (SaveProfile(root, &error)) { m_status = "Saved " + m_profilePath; if(changed)*changed=true; }
        else m_status = error;
    }
    ImGui::SameLine(); ImGui::TextUnformatted(m_dirty ? "Unsaved *" : "Saved");
    DrawSkeletonComparison();
    if (!m_sourceAnimation.clips.empty()) {
        const engine::Animation& clip = m_sourceAnimation.clips[static_cast<std::size_t>(m_selectedClip)].animation;
        const float duration = clip.ticksPerSecond > 0 ? clip.duration / clip.ticksPerSecond : 0.f;
        if (m_previewPlaying && duration > 0) {
            m_previewTime = std::fmod(m_previewTime + ImGui::GetIO().DeltaTime, duration);
        }
        if (ImGui::Button(m_previewPlaying ? "Pause Preview" : "Play Preview"))
            m_previewPlaying = !m_previewPlaying;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##PreviewTime", &m_previewTime, 0.f, std::max(duration,.001f), "%.2f s");
    }
    ImGui::SeparatorText("Profile");
    m_dirty |= ImGui::Checkbox("Transfer Root Motion", &m_profile.transferRootMotion);
    m_dirty |= ImGui::DragFloat("Global Scale", &m_profile.globalScale, .01f, .001f, 100.f, "%.3f");
    const int unmapped = m_inputsLoaded ? static_cast<int>(m_sourceSkeleton.skeleton.Count())
        - static_cast<int>(m_profile.mappings.size()) : 0;
    ImGui::Text("Mapped: %d   Unmapped source bones: %d",
                static_cast<int>(m_profile.mappings.size()), std::max(unmapped,0));
    if (ImGui::BeginTable("Mappings", 3, ImGuiTableFlags_Borders|ImGuiTableFlags_RowBg|ImGuiTableFlags_ScrollY,
                          ImVec2(0, 210))) {
        ImGui::TableSetupColumn("Source"); ImGui::TableSetupColumn("Target"); ImGui::TableSetupColumn("Translation");
        ImGui::TableHeadersRow();
        for (std::size_t i=0;i<m_profile.mappings.size();++i) {
            auto& map=m_profile.mappings[i]; ImGui::PushID(static_cast<int>(i)); ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::Selectable(map.sourceBone.c_str(), m_selectedMapping==(int)i,
                ImGuiSelectableFlags_SpanAllColumns); if(ImGui::IsItemClicked())m_selectedMapping=(int)i;
            ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(map.targetBone.c_str());
            ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(map.transferTranslation?"Yes":"No"); ImGui::PopID();
        } ImGui::EndTable();
    }
    if (m_selectedMapping>=0 && m_selectedMapping<(int)m_profile.mappings.size()) {
        auto& map=m_profile.mappings[static_cast<std::size_t>(m_selectedMapping)];
        ImGui::SeparatorText("Selected Bone Correction");
        if (m_inputsLoaded) {
            if (ImGui::BeginCombo("Source Bone", map.sourceBone.c_str())) {
                for (const engine::Bone& bone : m_sourceSkeleton.skeleton.bones)
                    if (ImGui::Selectable(bone.name.c_str(), bone.name==map.sourceBone)) {
                        map.sourceBone=bone.name; m_dirty=true;
                    }
                ImGui::EndCombo();
            }
            if (ImGui::BeginCombo("Target Bone", map.targetBone.c_str())) {
                for (const engine::Bone& bone : m_targetSkeleton.skeleton.bones)
                    if (ImGui::Selectable(bone.name.c_str(), bone.name==map.targetBone)) {
                        map.targetBone=bone.name; m_dirty=true;
                    }
                ImGui::EndCombo();
            }
        }
        m_dirty |= ImGui::DragFloat3("Rotation Offset", &map.rotationOffsetDegrees.x, .25f, -180, 180, "%.2f deg");
        m_dirty |= ImGui::DragFloat("Translation Scale", &map.translationScale, .01f, .001f, 100, "%.3f");
        m_dirty |= ImGui::Checkbox("Transfer Translation", &map.transferTranslation);
    }
    if (m_inputsLoaded && ImGui::Button("Add Manual Mapping")) {
        engine::RetargetBoneMapping mapping;
        if (!m_sourceSkeleton.skeleton.bones.empty()) mapping.sourceBone=m_sourceSkeleton.skeleton.bones.front().name;
        if (!m_targetSkeleton.skeleton.bones.empty()) mapping.targetBone=m_targetSkeleton.skeleton.bones.front().name;
        m_profile.mappings.push_back(std::move(mapping));
        m_selectedMapping=static_cast<int>(m_profile.mappings.size())-1; m_dirty=true;
    }
    ImGui::SameLine();
    if (m_selectedMapping>=0 && m_selectedMapping<(int)m_profile.mappings.size()
        && ImGui::Button("Remove Mapping")) {
        m_profile.mappings.erase(m_profile.mappings.begin()+m_selectedMapping);
        m_selectedMapping=std::min(m_selectedMapping,(int)m_profile.mappings.size()-1); m_dirty=true;
    }
    ImGui::SeparatorText("Output");
    if (!m_sourceAnimation.clips.empty()) {
        const char* preview=m_sourceAnimation.clips[static_cast<std::size_t>(m_selectedClip)].animation.name.c_str();
        if(ImGui::BeginCombo("Clip",preview)){for(int i=0;i<(int)m_sourceAnimation.clips.size();++i)
            if(ImGui::Selectable(m_sourceAnimation.clips[static_cast<std::size_t>(i)].animation.name.c_str(),i==m_selectedClip))m_selectedClip=i; ImGui::EndCombo();}
    }
    ImGui::InputText("Output Name", &m_outputName);
    const bool selectedClicked = ImGui::Button("Retarget Selected Clip");
    ImGui::SameLine();
    const bool allClicked = ImGui::Button("Retarget All Clips");
    if (selectedClicked || allClicked) {
        std::string error; if(RetargetSelected(root,allClicked,&error)){
            m_status="Created native retargeted animation."; if(changed)*changed=true;
        } else m_status=error;
    }
    if (!m_status.empty()) ImGui::TextWrapped("%s", m_status.c_str());
    if (message && changed && *changed) *message = m_status;
    ImGui::End();
}
