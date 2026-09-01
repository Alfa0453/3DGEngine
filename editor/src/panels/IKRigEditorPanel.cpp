#include "IKRigEditorPanel.h"
#include "EditorPanels.h"

#include <engine/animation/AnimatedModel.h>
#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>

namespace {
std::string FullPath(const std::string& root, const std::string& path) {
    if (path.empty()) return {};
    const std::filesystem::path input(path);
    if (input.is_absolute()) return input.string();
    return (std::filesystem::path(root) / input).string();
}
std::string SafeName(std::string value) {
    for (char& c : value) if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') c = '_';
    return value.empty() ? "NewIKRig" : value;
}
bool StringField(const char* label, std::string& value) {
    char buffer[192]{}; std::snprintf(buffer, sizeof(buffer), "%s", value.c_str());
    if (!ImGui::InputText(label, buffer, sizeof(buffer))) return false;
    value = buffer; return true;
}
template<class Enum>
bool EnumCombo(const char* label, Enum& value, int count, const char* (*name)(Enum)) {
    bool changed = false;
    if (ImGui::BeginCombo(label, name(value))) {
        for (int i = 0; i < count; ++i) {
            const auto candidate = static_cast<Enum>(i);
            if (ImGui::Selectable(name(candidate), candidate == value)) { value = candidate; changed = true; }
        }
        ImGui::EndCombo();
    }
    return changed;
}
}

void IKRigEditorPanel::New(const std::string& root) {
    m_asset = {}; m_asset.header.id = engine::AssetHandle::Generate();
    m_path = (std::filesystem::path(root) / "GameAssets" / "IKRigs" / "NewIKRig.3dgikrig").string();
    m_skeleton = {}; m_skeletonLoaded = false; m_selectedGoal = -1; m_status.clear(); m_dirty = true;
}

bool IKRigEditorPanel::LoadSkeleton(const std::string& root, std::string* error) {
    m_skeletonLoaded = engine::LoadSkeletonAsset(FullPath(root, m_asset.skeletonPath), &m_skeleton, error);
    return m_skeletonLoaded;
}

bool IKRigEditorPanel::Load(const std::string& path, const std::string& root, std::string* error) {
    if (!engine::LoadIKRigAsset(path, &m_asset, error)) return false;
    m_path = path; m_selectedGoal = m_asset.goals.empty() ? -1 : 0; m_dirty = false;
    return LoadSkeleton(root, error);
}

bool IKRigEditorPanel::SaveForShutdown(const std::string& root, std::string* error) {
    if (m_path.empty()) New(root);
    if (!engine::SaveIKRigAsset(m_path, m_asset, error)) return false;
    m_dirty = false; return true;
}

bool IKRigEditorPanel::BoneCombo(const char* label, std::string& bone) {
    bool changed = false;
    if (ImGui::BeginCombo(label, bone.empty() ? "Choose bone..." : bone.c_str())) {
        for (std::size_t i = 0; i < m_skeleton.skeleton.bones.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            const auto& name = m_skeleton.skeleton.bones[i].name;
            if (ImGui::Selectable(name.c_str(), name == bone)) { bone = name; changed = true; }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool IKRigEditorPanel::AutoSetup() {
    if (!m_skeletonLoaded) return false;
    const auto& skeleton = m_skeleton.skeleton;
    engine::FootIK detected; engine::AutoDetectFootIKBones(skeleton, detected);
    auto boneName = [&](int index) -> std::string {
        return index >= 0 && index < static_cast<int>(skeleton.bones.size())
            ? skeleton.bones[static_cast<std::size_t>(index)].name : std::string{};
    };
    auto find = [&](std::initializer_list<const char*> words) {
        for (const char* word : words) for (std::size_t i = 0; i < skeleton.bones.size(); ++i) {
            std::string name = skeleton.bones[i].name;
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c){return static_cast<char>(std::tolower(c));});
            if (name.find(word) != std::string::npos) return static_cast<int>(i);
        }
        return -1;
    };
    auto& feet = m_asset.feet; feet.enabled = detected.left.Valid() && detected.right.Valid();
    feet.pelvisBone = boneName(detected.pelvis);
    feet.leftUpperBone = boneName(detected.left.upper); feet.leftMidBone = boneName(detected.left.mid);
    feet.leftFootBone = boneName(detected.left.foot); feet.rightUpperBone = boneName(detected.right.upper);
    feet.rightMidBone = boneName(detected.right.mid); feet.rightFootBone = boneName(detected.right.foot);
    m_asset.goals.clear();
    auto handGoal = [&](const char* name, bool left) {
        engine::IKGoalDefinition goal; goal.name = name; goal.type = engine::IKGoalType::TwoBone;
        goal.rootBone = boneName(find(left ? std::initializer_list<const char*>{"leftarm", "upperarm_l", "upperarm.l"}
                                           : std::initializer_list<const char*>{"rightarm", "upperarm_r", "upperarm.r"}));
        goal.midBone = boneName(find(left ? std::initializer_list<const char*>{"leftforearm", "lowerarm_l", "forearm_l"}
                                          : std::initializer_list<const char*>{"rightforearm", "lowerarm_r", "forearm_r"}));
        goal.endBone = boneName(find(left ? std::initializer_list<const char*>{"lefthand", "hand_l", "hand.l"}
                                          : std::initializer_list<const char*>{"righthand", "hand_r", "hand.r"}));
        goal.targetOffset = left ? glm::vec3(-0.45f, 1.2f, -0.5f) : glm::vec3(0.45f, 1.2f, -0.5f);
        goal.poleOffset = left ? glm::vec3(-0.7f, 1.0f, 0.0f) : glm::vec3(0.7f, 1.0f, 0.0f);
        if (!goal.rootBone.empty() && !goal.midBone.empty() && !goal.endBone.empty()) m_asset.goals.push_back(goal);
    };
    handGoal("LeftHand", true); handGoal("RightHand", false);
    const int head = find({"head"});
    if (head >= 0) {
        engine::IKGoalDefinition look; look.name = "LookAt"; look.type = engine::IKGoalType::LookAt;
        look.rootBone = boneName(head); look.targetOffset = {0.0f, 1.65f, -2.0f};
        look.forwardAxis = {0.0f, 0.0f, 1.0f}; look.maxAngleDegrees = 75.0f;
        m_asset.goals.push_back(look);
    }
    m_selectedGoal = m_asset.goals.empty() ? -1 : 0; m_dirty = true; return true;
}

void IKRigEditorPanel::DrawPreview() {
    const ImVec2 size(std::max(250.0f, ImGui::GetContentRegionAvail().x), 360.0f);
    ImGui::InvisibleButton("##IKRigPreview", size);
    auto* draw = ImGui::GetWindowDrawList(); const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
    draw->AddRectFilled(min, max, IM_COL32(14, 17, 23, 255));
    if (!m_skeletonLoaded || m_skeleton.skeleton.bones.empty()) {
        draw->AddText({min.x + 12, min.y + 12}, IM_COL32(190,190,195,255), "Choose a skeleton to preview the IK rig."); return;
    }
    const auto& skeleton = m_skeleton.skeleton; std::vector<glm::mat4> world(skeleton.bones.size());
    std::vector<glm::vec3> points(skeleton.bones.size()); glm::vec2 lo(1e9f), hi(-1e9f);
    for (std::size_t i=0;i<skeleton.bones.size();++i) {
        const int p=skeleton.bones[i].parent;world[i]=p>=0?world[static_cast<std::size_t>(p)]*skeleton.bones[i].localBind:skeleton.bones[i].localBind;
        points[i]=glm::vec3(world[i][3]);lo=glm::min(lo,glm::vec2(points[i].x,points[i].y));hi=glm::max(hi,glm::vec2(points[i].x,points[i].y));
    }
    const glm::vec2 span=glm::max(hi-lo,glm::vec2(0.01f));const float scale=std::min((size.x-50)/span.x,(size.y-40)/span.y);
    const auto project=[&](const glm::vec3& p){return ImVec2(min.x+25+(p.x-lo.x)*scale,max.y-20-(p.y-lo.y)*scale);};
    for(std::size_t i=0;i<skeleton.bones.size();++i){const int p=skeleton.bones[i].parent;if(p>=0)draw->AddLine(project(points[static_cast<std::size_t>(p)]),project(points[i]),IM_COL32(80,100,125,255),2);}
    for(std::size_t i=0;i<m_asset.goals.size();++i){const auto& goal=m_asset.goals[i];const int bone=skeleton.Find(goal.rootBone);if(bone<0)continue;
        const bool selected=static_cast<int>(i)==m_selectedGoal;const ImU32 color=selected?IM_COL32(255,185,45,255):IM_COL32(65,210,235,255);
        draw->AddCircleFilled(project(points[static_cast<std::size_t>(bone)]),selected?6.0f:4.0f,color);
        const ImVec2 target=project(goal.targetOffset);draw->AddLine(project(points[static_cast<std::size_t>(bone)]),target,color,1.5f);draw->AddCircle(target,7.0f,color,0,2.0f);
    }
}

IKRigEditorPanel::Result IKRigEditorPanel::Draw(EditorAssets& assets, const std::string& root, bool* open) {
    Result result; if (!m_asset.header.id.Valid()) New(root);
    if (!m_pendingOpen.empty()) { std::string error; if (!Load(m_pendingOpen, root, &error)) m_status=error; else m_status="Loaded "+m_pendingOpen; m_pendingOpen.clear(); }
    if (!ImGui::Begin(EditorPanels::Name(EditorPanels::Panel::IKRigEditor), open)) { ImGui::End(); return result; }
    if (ImGui::Button("New")) New(root); ImGui::SameLine();
    if (ImGui::Button("Save")) { std::string error;if(m_path.empty())m_path=(std::filesystem::path(root)/"GameAssets"/"IKRigs"/(SafeName(m_asset.name)+".3dgikrig")).string();
        if(engine::SaveIKRigAsset(m_path,m_asset,&error)){m_dirty=false;result.saved=true;result.message="Saved IK rig: "+m_path;}else result.message=error; }
    ImGui::SameLine(); if (ImGui::Button("Apply to Selected")) result.applySelected=true;
    ImGui::SameLine(); ImGui::TextUnformatted(m_path.c_str());
    m_dirty |= StringField("Name", m_asset.name);
    const std::string preview=m_asset.skeletonPath.empty()?"Choose skeleton...":std::filesystem::path(m_asset.skeletonPath).filename().string();
    if(ImGui::BeginCombo("Skeleton",preview.c_str())){for(const auto& candidate:assets.ContentAssetPaths(EditorAssets::Type::Skeleton)){ImGui::PushID(candidate.c_str());
        const std::string name=std::filesystem::path(candidate).filename().string();if(ImGui::Selectable(name.c_str(),candidate==m_asset.skeletonPath)){m_asset.skeletonPath=candidate;m_asset.skeletonId=assets.AssetIdForPath(candidate);std::string error;LoadSkeleton(root,&error);m_status=error;m_dirty=true;}ImGui::PopID();}ImGui::EndCombo();}
    ImGui::SameLine(); if(ImGui::Button("Auto Setup Humanoid")) { if(!AutoSetup())m_status="Choose a valid humanoid skeleton first."; }
    ImGui::Columns(2,"##IKRigColumns",true);
    ImGui::SeparatorText("Goals");
    for(std::size_t i=0;i<m_asset.goals.size();++i){ImGui::PushID(static_cast<int>(i));const bool selected=static_cast<int>(i)==m_selectedGoal;
        if(ImGui::Selectable(m_asset.goals[i].name.c_str(),selected))m_selectedGoal=static_cast<int>(i);ImGui::PopID();}
    if(ImGui::Button("Add Goal")){m_asset.goals.emplace_back();m_selectedGoal=static_cast<int>(m_asset.goals.size()-1);m_dirty=true;}
    ImGui::SameLine();if(ImGui::Button("Remove")&&m_selectedGoal>=0&&m_selectedGoal<static_cast<int>(m_asset.goals.size())){m_asset.goals.erase(m_asset.goals.begin()+m_selectedGoal);m_selectedGoal=std::min(m_selectedGoal,static_cast<int>(m_asset.goals.size())-1);m_dirty=true;}
    ImGui::SeparatorText("Foot Placement");auto& feet=m_asset.feet;m_dirty|=ImGui::Checkbox("Enabled##Feet",&feet.enabled);
    if(feet.enabled){m_dirty|=BoneCombo("Pelvis",feet.pelvisBone);m_dirty|=BoneCombo("Left Upper",feet.leftUpperBone);m_dirty|=BoneCombo("Left Mid",feet.leftMidBone);m_dirty|=BoneCombo("Left Foot",feet.leftFootBone);
        m_dirty|=BoneCombo("Right Upper",feet.rightUpperBone);m_dirty|=BoneCombo("Right Mid",feet.rightMidBone);m_dirty|=BoneCombo("Right Foot",feet.rightFootBone);
        m_dirty|=ImGui::DragFloat("Trace Up",&feet.traceUp,0.01f,0,10,"%.2f m");m_dirty|=ImGui::DragFloat("Trace Down",&feet.traceDown,0.01f,0,10,"%.2f m");m_dirty|=ImGui::DragFloat("Foot Height",&feet.footHeight,0.005f,-1,1,"%.3f m");m_dirty|=ImGui::SliderFloat("Pelvis Weight",&feet.pelvisWeight,0,1);m_dirty|=ImGui::DragFloat("Max Pelvis Drop",&feet.maxPelvisDrop,0.01f,0,5,"%.2f m");m_dirty|=ImGui::SliderFloat("Foot IK Weight",&feet.weight,0,1);}
    ImGui::NextColumn();ImGui::SeparatorText("Goal Details");
    if(m_selectedGoal>=0&&m_selectedGoal<static_cast<int>(m_asset.goals.size())){ImGui::PushID(m_selectedGoal);auto& goal=m_asset.goals[static_cast<std::size_t>(m_selectedGoal)];
        m_dirty|=StringField("Goal Name",goal.name);m_dirty|=ImGui::Checkbox("Enabled",&goal.enabled);m_dirty|=EnumCombo("Type",goal.type,4,engine::IKGoalTypeName);m_dirty|=BoneCombo("Root Bone",goal.rootBone);
        if(goal.type==engine::IKGoalType::TwoBone){m_dirty|=BoneCombo("Mid Bone",goal.midBone);m_dirty|=BoneCombo("End Bone",goal.endBone);m_dirty|=ImGui::DragFloat3("Pole Offset",&goal.poleOffset.x,0.02f,-10000,10000,"%.2f m");}
        m_dirty|=ImGui::DragFloat3("Preview Target",&goal.targetOffset.x,0.02f,-10000,10000,"%.2f m");
        if(goal.type!=engine::IKGoalType::TwoBone)m_dirty|=ImGui::DragFloat3("Forward Axis",&goal.forwardAxis.x,0.01f,-1,1,"%.2f");
        m_dirty|=ImGui::SliderFloat("Weight",&goal.weight,0,1);m_dirty|=ImGui::SliderFloat("Maximum Angle",&goal.maxAngleDegrees,0,180,"%.0f deg");m_dirty|=ImGui::DragFloat("Target Smoothing",&goal.interpolationSpeed,0.1f,0,1000,"%.1f /s");ImGui::PopID();}
    engine::NormalizeIKRigAsset(m_asset);DrawPreview();ImGui::Columns(1);
    if(m_dirty)ImGui::TextColored({1,0.7f,0.2f,1},"Unsaved changes");if(!m_status.empty())ImGui::TextWrapped("%s",m_status.c_str());
    ImGui::End();return result;
}
