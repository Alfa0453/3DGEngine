#include "RagdollPhysicsPanel.h"

#include "EditorScene.h"

#include <engine/animation/Skeleton.h>
#include <engine/assets/RuntimeAssetManager.h>
#include <engine/graphics/SkinnedModel.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <unordered_map>

namespace {
bool ImportantBone(std::string name) {
    std::transform(name.begin(), name.end(), name.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    constexpr const char* words[] = {
        "pelvis", "hip", "spine", "chest", "neck", "head", "upperarm",
        "lowerarm", "forearm", "hand", "thigh", "calf", "shin", "leg", "foot"
    };
    for (const char* word : words)
        if (name.find(word) != std::string::npos) return true;
    return false;
}

std::string SafeName(std::string name) {
    for (char& c : name)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-')) c = '_';
    return name.empty() ? "Ragdoll" : name;
}
} // namespace

bool RagdollPhysicsPanel::AutoGenerate(const engine::Skeleton& skeleton) {
    if (skeleton.bones.size() < 2) return false;
    std::vector<glm::mat4> world(skeleton.bones.size(), glm::mat4(1.0f));
    for (std::size_t i = 0; i < skeleton.bones.size(); ++i) {
        const int parent = skeleton.bones[i].parent;
        world[i] = parent >= 0 ? world[static_cast<std::size_t>(parent)]
            * skeleton.bones[i].localBind : skeleton.bones[i].localBind;
    }
    struct Candidate { int bone; float length; bool important; };
    std::vector<Candidate> candidates;
    for (std::size_t i = 0; i < skeleton.bones.size(); ++i) {
        const int parent = skeleton.bones[i].parent;
        if (parent < 0) continue;
        const float length = glm::distance(glm::vec3(world[i][3]),
            glm::vec3(world[static_cast<std::size_t>(parent)][3]));
        if (length > 0.01f)
            candidates.push_back({static_cast<int>(i), length,
                                  ImportantBone(skeleton.bones[i].name)});
    }
    std::stable_sort(candidates.begin(), candidates.end(),
        [](const Candidate& a, const Candidate& b) {
            if (a.important != b.important) return a.important > b.important;
            return a.length > b.length;
        });
    if (candidates.size() > 24) candidates.resize(24);
    std::sort(candidates.begin(), candidates.end(),
        [](const Candidate& a, const Candidate& b) { return a.bone < b.bone; });

    m_asset.bodies.clear();
    m_asset.constraints.clear();
    std::unordered_map<int, std::string> included;
    for (const Candidate& candidate : candidates) {
        engine::RagdollBodyDefinition body;
        body.boneName = skeleton.bones[static_cast<std::size_t>(candidate.bone)].name;
        body.shape = engine::RagdollBodyShape::Capsule;
        body.radius = std::clamp(candidate.length * 0.18f, 0.025f, 0.22f);
        body.halfHeight = std::max(candidate.length * 0.5f - body.radius, 0.01f);
        body.halfExtents = glm::vec3(body.radius, body.halfHeight + body.radius,
                                     body.radius);
        body.massWeight = std::max(candidate.length, 0.05f);
        m_asset.bodies.push_back(body);
        included[candidate.bone] = body.boneName;
    }
    for (const Candidate& candidate : candidates) {
        int parent = skeleton.bones[static_cast<std::size_t>(candidate.bone)].parent;
        while (parent >= 0 && included.find(parent) == included.end())
            parent = skeleton.bones[static_cast<std::size_t>(parent)].parent;
        if (parent < 0) continue;
        engine::RagdollConstraintDefinition joint;
        joint.parentBoneName = included[parent];
        joint.childBoneName = included[candidate.bone];
        const std::string child = joint.childBoneName;
        if (child.find("calf") != std::string::npos
            || child.find("shin") != std::string::npos
            || child.find("forearm") != std::string::npos
            || child.find("lowerarm") != std::string::npos) {
            joint.type = engine::RagdollJointType::Hinge;
            joint.swingLimitDegrees = 10.0f;
            joint.twistMinDegrees = 0.0f;
            joint.twistMaxDegrees = 120.0f;
        }
        m_asset.constraints.push_back(joint);
    }
    m_selectedBody = m_asset.bodies.empty() ? -1 : 0;
    m_selectedConstraint = -1;
    m_dirty = true;
    return !m_asset.bodies.empty();
}

bool RagdollPhysicsPanel::Save(const std::string& assetRoot, std::string* error) {
    if (m_path.empty()) {
        m_path = (std::filesystem::path(assetRoot) / "GameAssets" / "Ragdolls"
            / (SafeName(m_asset.name) + ".3dgragdoll")).string();
    }
    if (!engine::SaveRagdollAsset(m_path, m_asset, error)) return false;
    m_dirty = false;
    return true;
}

bool RagdollPhysicsPanel::Load(const std::string& path, std::string* error) {
    engine::RagdollAssetData loaded;
    if (!engine::LoadRagdollAsset(path, &loaded, error)) return false;
    m_asset = std::move(loaded);
    m_path = path;
    m_selectedBody = m_asset.bodies.empty() ? -1 : 0;
    m_selectedConstraint = -1;
    m_dirty = false;
    return true;
}

void RagdollPhysicsPanel::DrawSkeletonPreview(const engine::Skeleton* skeleton) {
    const ImVec2 size(ImGui::GetContentRegionAvail().x, 260.0f);
    ImGui::InvisibleButton("##RagdollPreview", size);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    draw->AddRectFilled(min, max, IM_COL32(15, 18, 24, 255));
    draw->AddRect(min, max, IM_COL32(60, 70, 85, 255));
    if (!skeleton || skeleton->bones.empty()) {
        draw->AddText(ImVec2(min.x + 12.0f, min.y + 12.0f), IM_COL32(180,180,180,255),
                      "Select a skeletal character, then Generate Bodies.");
        return;
    }
    std::vector<glm::mat4> world(skeleton->bones.size(), glm::mat4(1.0f));
    std::vector<glm::vec3> points(skeleton->bones.size());
    glm::vec2 lo(1.0e9f), hi(-1.0e9f);
    for (std::size_t i = 0; i < skeleton->bones.size(); ++i) {
        const int p = skeleton->bones[i].parent;
        world[i] = p >= 0 ? world[static_cast<std::size_t>(p)] * skeleton->bones[i].localBind
                          : skeleton->bones[i].localBind;
        points[i] = glm::vec3(world[i][3]);
        const glm::vec2 projected(points[i].x, points[i].y);
        lo = glm::min(lo, projected); hi = glm::max(hi, projected);
    }
    const glm::vec2 span = glm::max(hi - lo, glm::vec2(0.01f));
    const float scale = std::min((size.x - 36.0f) / span.x,
                                 (size.y - 30.0f) / span.y);
    auto project = [&](const glm::vec3& point) {
        return ImVec2(min.x + 18.0f + (point.x - lo.x) * scale,
                      max.y - 15.0f - (point.y - lo.y) * scale);
    };
    for (std::size_t i = 0; i < skeleton->bones.size(); ++i) {
        const int p = skeleton->bones[i].parent;
        if (p >= 0) draw->AddLine(project(points[static_cast<std::size_t>(p)]),
                                  project(points[i]), IM_COL32(80,100,125,255), 1.0f);
    }
    for (std::size_t i = 0; i < m_asset.bodies.size(); ++i) {
        const int bone = skeleton->Find(m_asset.bodies[i].boneName);
        if (bone < 0 || !m_asset.bodies[i].enabled) continue;
        const bool selected = static_cast<int>(i) == m_selectedBody;
        draw->AddCircleFilled(project(points[static_cast<std::size_t>(bone)]),
            selected ? 6.0f : 4.0f,
            selected ? IM_COL32(255,180,35,255) : IM_COL32(55,220,125,255));
    }
}

void RagdollPhysicsPanel::Draw(EditorScene& scene,
                               engine::RuntimeAssetManager& assets,
                               const std::string& assetRoot, bool* open,
                               bool* assetSaved, std::string* message) {
    if (assetSaved) *assetSaved = false;
    if (!m_queuedPath.empty()) {
        std::string error;
        if (!Load(m_queuedPath, &error)) m_status = error;
        else m_status = "Loaded " + m_queuedPath;
        m_queuedPath.clear();
    }
    if (!ImGui::Begin("Ragdoll Physics Editor", open)) { ImGui::End(); return; }

    std::array<char, 128> name{};
    std::snprintf(name.data(), name.size(), "%s", m_asset.name.c_str());
    if (ImGui::InputText("Asset Name", name.data(), name.size())) {
        m_asset.name = name.data(); m_dirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("New")) { m_asset = {}; m_path.clear(); m_dirty = true; }
    ImGui::SameLine();
    if (ImGui::Button("Save")) {
        std::string error;
        if (Save(assetRoot, &error)) {
            m_status = "Saved " + m_path;
            if (assetSaved) *assetSaved = true;
        } else m_status = error;
    }

    const EditorScene::Object* selected = scene.SelectedObject();
    const EditorScene::Object* target = scene.FindObject(m_applyTarget);
    const engine::SkinnedModel* model = nullptr;
    std::string loadError;
    if (selected && selected->skeletalModel && !selected->modelAssetPath.empty())
        model = assets.LoadSkinnedModel(selected->modelAssetPath, &loadError);
    const engine::Skeleton* skeleton = model ? &model->GetSkeleton() : nullptr;
    if (selected) ImGui::Text("Scene Character: %s", selected->name.c_str());
    if (ImGui::Button("Generate Bodies from Selected Character")) {
        if (!skeleton) m_status = loadError.empty() ? "Select a skeletal character in the level." : loadError;
        else {
            m_applyTarget = selected->entity;
            m_asset.skeletonPath = selected->modelAssetPath;
            if (m_asset.name == "Ragdoll") m_asset.name = selected->name + "_Ragdoll";
            m_status = AutoGenerate(*skeleton) ? "Generated editable bodies and joints." : "No usable bones found.";
        }
    }
    ImGui::SameLine();
    const bool validTarget = target && target->skeletalModel && !target->locked;
    ImGui::BeginDisabled(!validTarget);
    const std::string applyLabel = validTarget
        ? "Apply to \"" + target->name + "\"" : "Apply (No Character Target)";
    if (ImGui::Button(applyLabel.c_str())) {
        if (!target || !target->skeletalModel) m_status = "Choose a skeletal character target first.";
        else {
            const int oldIndex = scene.SelectedIndex();
            const auto oldSelection = scene.HierarchySelection();
            const auto oldGroup = scene.SelectedGroupId();
            scene.SelectEntity(target->entity);
            engine::Ragdoll ragdoll = target->ragdoll;
            engine::ApplyRagdollAsset(m_asset, &ragdoll);
            std::filesystem::path relative = m_path;
            std::error_code ec;
            if (!m_path.empty()) {
                const std::filesystem::path candidate = std::filesystem::relative(m_path, assetRoot, ec);
                if (!ec) relative = candidate;
            }
            ragdoll.assetPath = m_path.empty() ? std::string{} : relative.generic_string();
            if (scene.SetSelectedRagdoll(ragdoll))
                m_status = "Ragdoll asset applied to " + target->name + ".";
            else m_status = "Could not apply: the selected object may be locked.";
            if (oldSelection == EditorScene::HierarchySelectionType::Group) scene.SelectGroup(oldGroup);
            else if (oldIndex >= 0) scene.SelectIndex(oldIndex); else scene.Deselect();
        }
    }
    ImGui::EndDisabled();
    if (selected && selected->skeletalModel && selected->entity != m_applyTarget) {
        if (ImGui::Button(("Use \"" + selected->name + "\" As Apply Target").c_str()))
            m_applyTarget = selected->entity;
    }
    if (target && selected && selected->entity != target->entity)
        ImGui::TextColored(ImVec4(1, .7f, .2f, 1), "Current selection differs from apply target.");

    ImGui::DragFloat("Total Mass", &m_asset.totalMass, 0.5f, 1.0f, 500.0f, "%.1f kg");
    ImGui::DragFloat("Linear Damping", &m_asset.linearDamping, 0.01f, 0.0f, 10.0f);
    ImGui::DragFloat("Angular Damping", &m_asset.angularDamping, 0.01f, 0.0f, 20.0f);
    ImGui::DragFloat("Death Impulse", &m_asset.deathImpulse, 0.05f, 0.0f, 50.0f);
    if (ImGui::IsItemEdited()) m_dirty = true;
    if (ImGui::DragFloat("Blend Into Ragdoll", &m_asset.blendInDuration,
                         0.01f, 0.0f, 2.0f, "%.2f s")) m_dirty = true;
    if (ImGui::DragFloat("Recover to Animation", &m_asset.blendOutDuration,
                         0.01f, 0.0f, 3.0f, "%.2f s")) m_dirty = true;
    if (ImGui::Checkbox("Recover When Health Is Restored",
                        &m_asset.recoverWhenRevived)) m_dirty = true;
    DrawSkeletonPreview(skeleton);

    if (ImGui::BeginTable("RagdollColumns", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableNextColumn();
        ImGui::Text("Bodies (%d)", static_cast<int>(m_asset.bodies.size()));
        for (int i = 0; i < static_cast<int>(m_asset.bodies.size()); ++i) {
            ImGui::PushID(i);
            if (ImGui::Selectable(m_asset.bodies[static_cast<std::size_t>(i)].boneName.c_str(),
                                  m_selectedBody == i)) m_selectedBody = i;
            ImGui::PopID();
        }
        ImGui::TableNextColumn();
        if (m_selectedBody >= 0 && m_selectedBody < static_cast<int>(m_asset.bodies.size())) {
            auto& body = m_asset.bodies[static_cast<std::size_t>(m_selectedBody)];
            ImGui::Text("Body: %s", body.boneName.c_str());
            if (ImGui::Checkbox("Enabled", &body.enabled)) m_dirty = true;
            int shape = static_cast<int>(body.shape);
            const char* shapes[] = {"Sphere", "Box", "Capsule"};
            if (ImGui::Combo("Shape", &shape, shapes, 3)) { body.shape = static_cast<engine::RagdollBodyShape>(shape); m_dirty = true; }
            if (ImGui::DragFloat3("Local Position", &body.localPosition.x, 0.01f)) m_dirty = true;
            if (ImGui::DragFloat3("Local Rotation", &body.localRotationDegrees.x, 1.0f, -180.0f, 180.0f)) m_dirty = true;
            if (body.shape == engine::RagdollBodyShape::Box) {
                if (ImGui::DragFloat3("Half Extents", &body.halfExtents.x, 0.01f, 0.01f, 10.0f)) m_dirty = true;
            } else {
                if (ImGui::DragFloat("Radius", &body.radius, 0.005f, 0.01f, 5.0f)) m_dirty = true;
                if (body.shape == engine::RagdollBodyShape::Capsule
                    && ImGui::DragFloat("Half Height", &body.halfHeight, 0.005f, 0.0f, 10.0f)) m_dirty = true;
            }
            if (ImGui::DragFloat("Mass Weight", &body.massWeight, 0.05f, 0.01f, 20.0f)) m_dirty = true;
        }
        ImGui::EndTable();
    }
    if (ImGui::CollapsingHeader("Constraints", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (int i = 0; i < static_cast<int>(m_asset.constraints.size()); ++i) {
            auto& joint = m_asset.constraints[static_cast<std::size_t>(i)];
            ImGui::PushID(1000 + i);
            const std::string label = joint.parentBoneName + " -> " + joint.childBoneName;
            if (ImGui::Selectable(label.c_str(), m_selectedConstraint == i)) m_selectedConstraint = i;
            ImGui::PopID();
        }
        if (m_selectedConstraint >= 0 && m_selectedConstraint < static_cast<int>(m_asset.constraints.size())) {
            auto& joint = m_asset.constraints[static_cast<std::size_t>(m_selectedConstraint)];
            int type = static_cast<int>(joint.type);
            const char* types[] = {"Ball", "Hinge"};
            if (ImGui::Combo("Joint Type", &type, types, 2)) { joint.type = static_cast<engine::RagdollJointType>(type); m_dirty = true; }
            if (ImGui::DragFloat3("Joint Axis", &joint.axis.x, 0.01f, -1.0f, 1.0f)) m_dirty = true;
            if (ImGui::SliderFloat("Swing Limit", &joint.swingLimitDegrees, 0.0f, 180.0f, "%.0f deg")) m_dirty = true;
            if (ImGui::SliderFloat("Twist Min", &joint.twistMinDegrees, -180.0f, 0.0f, "%.0f deg")) m_dirty = true;
            if (ImGui::SliderFloat("Twist Max", &joint.twistMaxDegrees, 0.0f, 180.0f, "%.0f deg")) m_dirty = true;
            if (ImGui::Checkbox("Collide Connected", &joint.collideConnected)) m_dirty = true;
        }
    }
    if (!m_status.empty()) ImGui::TextWrapped("%s", m_status.c_str());
    if (m_dirty) ImGui::TextColored(ImVec4(1.0f,0.72f,0.2f,1.0f), "Unsaved changes");
    ImGui::End();
    if (message && !m_status.empty()) { *message = m_status; m_status.clear(); }
}
