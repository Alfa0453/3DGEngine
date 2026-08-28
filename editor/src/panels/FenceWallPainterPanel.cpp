#include "FenceWallPainterPanel.h"
#include "EditorPanels.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>

namespace {
const EditorScene::Object* FindSpline(const EditorScene& scene,
                                      const std::string& name) {
    for (const EditorScene::Object& object : scene.Objects())
        if (object.isSpline && object.name == name
            && object.splinePoints.size() >= 2) return &object;
    return nullptr;
}
}

void FenceWallPainterPanel::New(const std::string& root) {
    m_asset = {};
    m_asset.header.id = engine::AssetHandle::Generate();
    m_asset.name = "NewFence";
    m_path = (std::filesystem::path(root) / "GameAssets" / "Fences"
              / "NewFence.3dgfence").string();
    m_status.clear(); m_dirty = true;
}

bool FenceWallPainterPanel::SaveForShutdown(const std::string& root,
                                            std::string* error) {
    if (m_path.empty()) New(root);
    if (!engine::SaveFenceWallAsset(m_path, m_asset, error)) return false;
    m_dirty = false; return true;
}

bool FenceWallPainterPanel::AssetCombo(
    const char* label, EditorAssets::Type type, std::string& path,
    engine::AssetHandle& id, EditorAssets& assets, const char* emptyLabel) {
    bool changed = false;
    const std::string preview = path.empty() ? emptyLabel
        : std::filesystem::path(path).filename().string();
    if (ImGui::BeginCombo(label, preview.c_str())) {
        if (ImGui::Selectable(emptyLabel, path.empty())) {
            path.clear(); id = {}; changed = true;
        }
        for (const std::string& candidate : assets.ContentAssetPaths(type)) {
            const std::string name = std::filesystem::path(candidate).filename().string();
            ImGui::PushID(candidate.c_str());
            if (ImGui::Selectable(name.c_str(), candidate == path)) {
                path = candidate; id = assets.AssetIdForPath(candidate); changed = true;
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    return changed;
}

void FenceWallPainterPanel::DrawPreview() const {
    const ImVec2 size(ImGui::GetContentRegionAvail().x,
                      std::max(190.0f, ImGui::GetContentRegionAvail().y));
    ImGui::InvisibleButton("##FenceWallPreview", size);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetItemRectMin();
    draw->AddRectFilled(origin, {origin.x+size.x, origin.y+size.y},
                        IM_COL32(18,20,24,255));
    if (m_asset.points.size() < 2) return;
    glm::vec3 minimum = m_asset.points.front(), maximum = minimum;
    for (const glm::vec3& point : m_asset.points) {
        minimum = glm::min(minimum, point); maximum = glm::max(maximum, point);
    }
    const float sx = size.x / std::max(maximum.x-minimum.x, 1.0f);
    const float sz = size.y / std::max(maximum.z-minimum.z, 1.0f);
    const float scale = std::min(sx, sz) * 0.82f;
    const glm::vec3 center = (minimum+maximum)*0.5f;
    auto map = [&](glm::vec3 point) {
        return ImVec2(origin.x+size.x*.5f+(point.x-center.x)*scale,
                      origin.y+size.y*.5f+(point.z-center.z)*scale);
    };
    const ImU32 lineColor = m_asset.mode == engine::FenceWallMode::Wall
        ? IM_COL32(190,160,120,255) : IM_COL32(120,190,145,255);
    const std::size_t segments = m_asset.points.size()-(m_asset.closed?0:1);
    for (std::size_t i=0; i<segments; ++i) {
        const glm::vec3 a=m_asset.points[i], b=m_asset.points[(i+1)%m_asset.points.size()];
        draw->AddLine(map(a), map(b), lineColor,
                      std::max(2.0f, m_asset.thickness*scale));
    }
    for (const glm::vec3& point : m_asset.points)
        draw->AddCircleFilled(map(point), 4.0f, IM_COL32(240,190,80,255));
    for (const engine::FenceOpening& opening : m_asset.openings) {
        if (opening.segmentIndex < 0
            || opening.segmentIndex >= static_cast<int>(segments)) continue;
        const glm::vec3 a=m_asset.points[opening.segmentIndex];
        const glm::vec3 b=m_asset.points[(opening.segmentIndex+1)%m_asset.points.size()];
        const float length=glm::length(b-a);
        if (length < .001f) continue;
        const glm::vec3 point=a+(b-a)*(std::clamp(opening.centerDistance,0.0f,length)/length);
        draw->AddCircle(map(point), 7.0f,
            opening.gate ? IM_COL32(100,180,255,255) : IM_COL32(255,100,90,255),
            12, 2.0f);
    }
}

FenceWallPainterPanel::Result FenceWallPainterPanel::Draw(
    const EditorScene& scene, EditorAssets& assets, const std::string& root,
    bool* open) {
    Result result;
    if (!m_asset.header.id.Valid()) New(root);
    if (!m_pendingOpen.empty()) {
        std::string error;
        if (engine::LoadFenceWallAsset(m_pendingOpen, &m_asset, &error)) {
            m_path=m_pendingOpen; m_dirty=false; m_status="Loaded " + m_path;
        } else m_status=error;
        m_pendingOpen.clear();
    }
    if (!ImGui::Begin(EditorPanels::Name(EditorPanels::Panel::FenceWallPainter), open)) {
        ImGui::End(); return result;
    }
    std::vector<const EditorScene::Object*> splines;
    for (const EditorScene::Object& object : scene.Objects())
        if (object.isSpline && object.splinePoints.size() >= 2) splines.push_back(&object);
    if (!FindSpline(scene,m_splineName) && !splines.empty())
        m_splineName=splines.front()->name;

    if (ImGui::Button("New")) New(root);
    ImGui::SameLine();
    if (ImGui::Button("Create Drawing Spline")) result.createSpline=true;
    ImGui::SameLine();
    if (ImGui::Button("Capture Spline")) {
        if (const auto* spline=FindSpline(scene,m_splineName)) {
            m_asset.points=spline->splinePoints; m_asset.closed=spline->splineClosed;
            engine::NormalizeFenceWallAsset(m_asset); m_dirty=true;
            m_status="Captured " + spline->name;
        } else m_status="Choose an editable spline first.";
    }
    ImGui::SameLine();
    if (ImGui::Button("Save")) {
        std::string error;
        if (engine::SaveFenceWallAsset(m_path,m_asset,&error)) {
            m_dirty=false; result.saved=true; result.message="Saved fence/wall: "+m_path;
        } else result.message=error;
    }
    ImGui::SameLine(); if (ImGui::Button("Build / Rebuild")) result.build=true;
    ImGui::SameLine(); if (ImGui::Button("Delete Generated")) result.remove=true;

    char name[128]{}; std::snprintf(name,sizeof(name),"%s",m_asset.name.c_str());
    if (ImGui::InputText("Name",name,sizeof(name))) { m_asset.name=name; m_dirty=true; }
    int mode=static_cast<int>(m_asset.mode);
    const char* modes[]={"Fence","Wall"};
    if (ImGui::Combo("Type",&mode,modes,2)) {
        m_asset.mode=static_cast<engine::FenceWallMode>(mode); m_dirty=true;
        if (m_asset.mode==engine::FenceWallMode::Wall) {
            m_asset.height=2.5f; m_asset.thickness=.25f; m_asset.createPosts=false;
        }
    }
    if (ImGui::BeginCombo("Drawing Spline",m_splineName.empty()?"No spline":m_splineName.c_str())) {
        for (const auto* spline:splines) {
            ImGui::PushID(spline);
            if (ImGui::Selectable(spline->name.c_str(),spline->name==m_splineName))
                m_splineName=spline->name;
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::TextDisabled("Edit the chosen spline in the level viewport, then Capture Spline.");

    ImGui::SeparatorText("Geometry");
    m_dirty|=ImGui::DragFloat("Height",&m_asset.height,.05f,.1f,50,"%.2f m");
    m_dirty|=ImGui::DragFloat("Thickness",&m_asset.thickness,.01f,.02f,10,"%.2f m");
    m_dirty|=ImGui::DragFloat("Panel Length",&m_asset.panelLength,.05f,.1f,50,"%.2f m");
    m_dirty|=ImGui::Checkbox("Posts",&m_asset.createPosts);
    if (m_asset.createPosts) {
        m_dirty|=ImGui::DragFloat("Post Spacing",&m_asset.postSpacing,.05f,.1f,100,"%.2f m");
        m_dirty|=ImGui::DragFloat("Post Width",&m_asset.postWidth,.01f,.02f,10,"%.2f m");
        m_dirty|=ImGui::DragFloat("Post Height Extra",&m_asset.postHeightExtra,.01f,0,10,"%.2f m");
    }
    m_dirty|=ImGui::Checkbox("Follow Slopes",&m_asset.followSlope); ImGui::SameLine();
    m_dirty|=ImGui::Checkbox("Collision",&m_asset.createCollision);
    m_dirty|=ImGui::Checkbox("Snap Endpoints to Grid",&m_asset.snapToGrid);
    if (m_asset.snapToGrid)
        m_dirty|=ImGui::DragFloat("Grid Size",&m_asset.gridSize,.01f,.01f,100,"%.2f m");

    ImGui::SeparatorText("Panels, Posts and Gates");
    m_dirty|=AssetCombo("Panel Mesh",EditorAssets::Type::Model,
        m_asset.panelMeshPath,m_asset.panelMeshId,assets,"Generated Cube");
    m_dirty|=AssetCombo("Post Mesh",EditorAssets::Type::Model,
        m_asset.postMeshPath,m_asset.postMeshId,assets,"Generated Cube");
    m_dirty|=AssetCombo("Gate Mesh",EditorAssets::Type::Model,
        m_asset.gateMeshPath,m_asset.gateMeshId,assets,"Generated Cube");
    m_dirty|=AssetCombo("Panel Material",EditorAssets::Type::Material,
        m_asset.panelMaterialPath,m_asset.panelMaterialId,assets,"Default");
    m_dirty|=AssetCombo("Post Material",EditorAssets::Type::Material,
        m_asset.postMaterialPath,m_asset.postMaterialId,assets,"Default");
    m_dirty|=AssetCombo("Gate Material",EditorAssets::Type::Material,
        m_asset.gateMaterialPath,m_asset.gateMaterialId,assets,"Default");

    ImGui::SeparatorText("Gates and Openings");
    if (ImGui::Button("Add Gate")) {
        m_asset.openings.push_back({0,m_asset.panelLength,m_asset.panelLength,true});
        m_dirty=true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Add Opening")) {
        m_asset.openings.push_back({0,m_asset.panelLength,m_asset.panelLength,false});
        m_dirty=true;
    }
    const int maxSegment=std::max(0,static_cast<int>(m_asset.points.size())
        -(m_asset.closed?0:1)-1);
    for (int i=0;i<static_cast<int>(m_asset.openings.size());++i) {
        ImGui::PushID(i); auto& opening=m_asset.openings[static_cast<std::size_t>(i)];
        ImGui::Separator(); ImGui::Text("%s %d",opening.gate?"Gate":"Opening",i+1);
        m_dirty|=ImGui::SliderInt("Segment",&opening.segmentIndex,0,maxSegment);
        m_dirty|=ImGui::DragFloat("Distance",&opening.centerDistance,.05f,0,10000,"%.2f m");
        m_dirty|=ImGui::DragFloat("Width",&opening.width,.05f,.1f,50,"%.2f m");
        m_dirty|=ImGui::Checkbox("Create Gate",&opening.gate);
        if (opening.gate) m_dirty|=ImGui::DragFloat("Gate Height",&m_asset.gateHeight,.05f,.1f,50,"%.2f m");
        ImGui::SameLine();
        if (ImGui::Button("Remove")) {
            m_asset.openings.erase(m_asset.openings.begin()+i--); m_dirty=true;
        }
        ImGui::PopID();
    }

    engine::FenceGenerationStats stats; std::string error;
    const auto placements=engine::GenerateFenceWall(m_asset,&stats,&error);
    if (error.empty())
        ImGui::Text("%.1f m | %zu panels | %zu posts | %zu gates | %zu objects",
            stats.length,stats.panels,stats.posts,stats.gates,placements.size());
    else ImGui::TextDisabled("%s",error.c_str());
    ImGui::Separator(); DrawPreview();
    if (m_dirty) ImGui::TextColored({1,.7f,.2f,1},"Unsaved changes");
    if (!m_status.empty()) ImGui::TextWrapped("%s",m_status.c_str());
    ImGui::End(); return result;
}
