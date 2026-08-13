#include "MeshEditorPanel.h"
#include "EditorPanels.h"
#include "MeshGeometryOperations.h"

#include <imgui.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>

namespace {

glm::vec3 Vec3(const std::array<float, 3>& value) {
    return {value[0], value[1], value[2]};
}

std::array<float, 3> Array3(const glm::vec3& value) {
    return {{value.x, value.y, value.z}};
}

glm::vec3 ReadPosition(const std::vector<float>& vertices,
                       std::size_t vertex, std::size_t stride) {
    const std::size_t offset = vertex * stride;
    return {vertices[offset], vertices[offset + 1], vertices[offset + 2]};
}

} // namespace

void MeshEditorPanel::QueueOpen(const std::string& path) {
    m_pendingOpen = path;
}

bool MeshEditorPanel::Load(const std::string& path, std::string* error) {
    const std::string extension = std::filesystem::path(path).extension().string();
    if (extension == ".3dgmesh") {
        engine::StaticMeshAssetData asset;
        if (!engine::LoadStaticMeshAsset(path, &asset, error)) return false;
        m_staticAsset = std::move(asset);
        m_skeletalAsset = {};
        m_kind = Kind::Static;
        m_minimum = Vec3(m_staticAsset.minimum);
        m_maximum = Vec3(m_staticAsset.maximum);
    } else if (extension == ".3dgskmesh") {
        engine::SkeletalMeshAssetData asset;
        if (!engine::LoadSkeletalMeshAsset(path, &asset, error)) return false;
        m_skeletalAsset = std::move(asset);
        m_staticAsset = {};
        m_kind = Kind::Skeletal;
        m_minimum = Vec3(m_skeletalAsset.minimum);
        m_maximum = Vec3(m_skeletalAsset.maximum);
    } else {
        if (error) *error = "Mesh Editor supports .3dgmesh and .3dgskmesh assets";
        return false;
    }

    m_path = path;
    m_pivot = glm::vec3(0.0f);
    m_dirty = false;
    m_paintDirty = false;
    m_paintStroke = false;
    if (m_kind != Kind::Static) m_editMode = 0;
    m_undoPaint.clear();
    m_redoPaint.clear();
    m_geometryOriginal = m_staticAsset;
    m_undoGeometry.clear(); m_redoGeometry.clear(); m_selectedFaces.clear();
    m_selectedVertices.clear(); m_geometryDirty = false;
    m_previewYaw = -0.55f;
    m_previewPitch = 0.30f;
    m_previewZoom = 1.0f;
    RebuildPreviewGeometry();
    return true;
}

void MeshEditorPanel::PushGeometryUndo() {
    m_undoGeometry.push_back(m_staticAsset);
    if (m_undoGeometry.size() > 16u) m_undoGeometry.erase(m_undoGeometry.begin());
    m_redoGeometry.clear();
}

void MeshEditorPanel::RefreshGeometryState() {
    MeshGeometryOperations::RecalculateBounds(m_staticAsset);
    m_minimum=Vec3(m_staticAsset.minimum);m_maximum=Vec3(m_staticAsset.maximum);
    m_selectedFaces.clear();m_selectedVertices.clear();m_geometryDirty=true;
    RebuildPreviewGeometry();
}

bool MeshEditorPanel::SaveGeometry(std::string* error) {
    if(m_kind!=Kind::Static){if(error)*error="Geometry editing supports static meshes only";return false;}
    if(!MeshGeometryOperations::Validate(m_staticAsset,error))return false;
    if(!engine::SaveStaticMeshAsset(m_path,m_staticAsset,error))return false;
    m_geometryOriginal=m_staticAsset;m_geometryDirty=false;return true;
}

void MeshEditorPanel::SelectConnectedFaces() {
    if(m_selectedFaces.empty())return;
    std::unordered_set<std::size_t> selected=m_selectedFaces;bool changed=true;
    while(changed){
        changed=false;
        const std::vector<std::size_t> current(selected.begin(),selected.end());
        for(std::size_t i=0;i<m_triangles.size();++i){
            if(selected.count(i))continue;
            const auto& t=m_triangles[i];
            for(std::size_t j:current){
                const auto& s=m_triangles[j];
                if(t.subMesh!=s.subMesh)continue;
                const bool shares=t.ia==s.ia||t.ia==s.ib||t.ia==s.ic||
                                  t.ib==s.ia||t.ib==s.ib||t.ib==s.ic||
                                  t.ic==s.ia||t.ic==s.ib||t.ic==s.ic;
                if(shares){selected.insert(i);changed=true;break;}
            }
        }
    }
    m_selectedFaces=std::move(selected);
}

void MeshEditorPanel::RebuildPreviewGeometry() {
    m_triangles.clear();
    m_vertexCount = 0;
    m_triangleCount = 0;

    auto append = [this](const auto& subMeshes, std::size_t stride) {
        for (std::size_t subMeshIndex = 0; subMeshIndex < subMeshes.size(); ++subMeshIndex) {
            const auto& subMesh = subMeshes[subMeshIndex];
            const std::size_t vertices = subMesh.vertices.size() / stride;
            m_vertexCount += vertices;
            m_triangleCount += subMesh.indices.size() / 3;
            for (std::size_t i = 0; i + 2 < subMesh.indices.size(); i += 3) {
                const std::uint32_t ia = subMesh.indices[i];
                const std::uint32_t ib = subMesh.indices[i + 1];
                const std::uint32_t ic = subMesh.indices[i + 2];
                if (ia >= vertices || ib >= vertices || ic >= vertices) continue;
                m_triangles.push_back({
                    ReadPosition(subMesh.vertices, ia, stride),
                    ReadPosition(subMesh.vertices, ib, stride),
                    ReadPosition(subMesh.vertices, ic, stride),
                    subMeshIndex, ia, ib, ic});
            }
        }
    };

    if (m_kind == Kind::Static)
        append(m_staticAsset.subMeshes, engine::kStaticMeshVertexStride);
    else if (m_kind == Kind::Skeletal)
        append(m_skeletalAsset.subMeshes, engine::kSkeletalMeshVertexStride);
}

void MeshEditorPanel::EnsurePaintData() {
    if (m_kind != Kind::Static) return;
    for (engine::StaticMeshSubMeshData& subMesh : m_staticAsset.subMeshes) {
        const std::size_t count = subMesh.vertices.size() / engine::kStaticMeshVertexStride;
        if (subMesh.vertexColors.size() != count * 4u)
            subMesh.vertexColors.assign(count * 4u, 1.0f);
    }
}

MeshEditorPanel::PaintSnapshot MeshEditorPanel::CapturePaint() const {
    PaintSnapshot snapshot;
    snapshot.reserve(m_staticAsset.subMeshes.size());
    for (const auto& subMesh : m_staticAsset.subMeshes)
        snapshot.push_back(subMesh.vertexColors);
    return snapshot;
}

void MeshEditorPanel::RestorePaint(const PaintSnapshot& snapshot) {
    if (snapshot.size() != m_staticAsset.subMeshes.size()) return;
    for (std::size_t i = 0; i < snapshot.size(); ++i)
        m_staticAsset.subMeshes[i].vertexColors = snapshot[i];
    m_paintDirty = true;
}

void MeshEditorPanel::PushUndo() {
    EnsurePaintData();
    m_undoPaint.push_back(CapturePaint());
    if (m_undoPaint.size() > 24u) m_undoPaint.erase(m_undoPaint.begin());
    m_redoPaint.clear();
}

bool MeshEditorPanel::SavePaint(std::string* error) {
    if (m_kind != Kind::Static) {
        if (error) *error = "Vertex painting supports static meshes only";
        return false;
    }
    EnsurePaintData();
    if (!engine::SaveStaticMeshAsset(m_path, m_staticAsset, error)) return false;
    m_paintDirty = false;
    return true;
}

void MeshEditorPanel::SetPivot(const glm::vec3& pivot) {
    const glm::vec3 extent = glm::max(m_maximum - m_minimum, glm::vec3(0.001f));
    const glm::vec3 margin = extent * 4.0f;
    m_pivot = glm::clamp(pivot, m_minimum - margin, m_maximum + margin);
    m_dirty = glm::dot(m_pivot, m_pivot) > 1.0e-10f;
}

bool MeshEditorPanel::BakePivot(std::string* error) {
    if (m_kind == Kind::None) {
        if (error) *error = "No mesh is open";
        return false;
    }
    if (glm::dot(m_pivot, m_pivot) <= 1.0e-10f) return true;

    if (m_kind == Kind::Static) {
        for (engine::StaticMeshSubMeshData& subMesh : m_staticAsset.subMeshes) {
            for (std::size_t i = 0; i + 2 < subMesh.vertices.size();
                 i += engine::kStaticMeshVertexStride) {
                subMesh.vertices[i] -= m_pivot.x;
                subMesh.vertices[i + 1] -= m_pivot.y;
                subMesh.vertices[i + 2] -= m_pivot.z;
            }
        }
        m_staticAsset.minimum = Array3(m_minimum - m_pivot);
        m_staticAsset.maximum = Array3(m_maximum - m_pivot);
        if (!engine::SaveStaticMeshAsset(m_path, m_staticAsset, error)) return false;
    } else {
        // Keep weights, bind poses and animation channels untouched. Applying the
        // offset after the skeleton's inverse-root transform moves every skinned
        // vertex consistently in bind pose and in every animation.
        m_skeletalAsset.skeleton.globalInverse =
            glm::translate(glm::mat4(1.0f), -m_pivot)
            * m_skeletalAsset.skeleton.globalInverse;
        m_skeletalAsset.minimum = Array3(m_minimum - m_pivot);
        m_skeletalAsset.maximum = Array3(m_maximum - m_pivot);
        if (!engine::SaveSkeletalMeshAsset(m_path, m_skeletalAsset, error)) return false;
    }

    m_minimum -= m_pivot;
    m_maximum -= m_pivot;
    m_pivot = glm::vec3(0.0f);
    m_dirty = false;
    RebuildPreviewGeometry();
    return true;
}

void MeshEditorPanel::DrawPreview() {
    ImGui::TextUnformatted("PREVIEW");
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImVec2 size = ImGui::GetContentRegionAvail();
    size.y = std::max(280.0f, size.y);
    ImGui::InvisibleButton("##MeshPreview", size,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y),
                        IM_COL32(17, 21, 28, 255));
    draw->AddRect(origin, ImVec2(origin.x + size.x, origin.y + size.y),
                  IM_COL32(54, 67, 84, 255));

    if (ImGui::IsItemHovered()) {
        ImGuiIO& io = ImGui::GetIO();
        const ImGuiMouseButton orbitButton = m_editMode != 0
            ? ImGuiMouseButton_Right : ImGuiMouseButton_Left;
        if (ImGui::IsMouseDragging(orbitButton)) {
            m_previewYaw += io.MouseDelta.x * 0.008f;
            m_previewPitch = std::clamp(m_previewPitch + io.MouseDelta.y * 0.008f,
                                        -1.45f, 1.45f);
        }
        if (io.MouseWheel != 0.0f)
            m_previewZoom = std::clamp(m_previewZoom * std::pow(1.12f, io.MouseWheel),
                                       0.15f, 8.0f);
    }

    const glm::vec3 displayedMin = m_minimum - m_pivot;
    const glm::vec3 displayedMax = m_maximum - m_pivot;
    const glm::vec3 center = (displayedMin + displayedMax) * 0.5f;
    const float radius = std::max(0.001f, glm::length(displayedMax - displayedMin) * 0.5f);
    const float scale = 0.43f * std::min(size.x, size.y) * m_previewZoom / radius;
    glm::mat4 rotation(1.0f);
    rotation = glm::rotate(rotation, m_previewPitch, glm::vec3(1, 0, 0));
    rotation = glm::rotate(rotation, m_previewYaw, glm::vec3(0, 1, 0));

    auto project3 = [&](const glm::vec3& p) {
        const glm::vec3 q = glm::vec3(rotation * glm::vec4(p - m_pivot - center, 1.0f));
        return glm::vec3(origin.x + size.x * 0.5f + q.x * scale,
                         origin.y + size.y * 0.5f - q.y * scale, q.z);
    };
    auto project = [&](const glm::vec3& p) {
        const glm::vec3 q = project3(p);
        return ImVec2(q.x, q.y);
    };

    if (m_editMode == 2 && m_kind == Kind::Static && ImGui::IsItemHovered()
        && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const ImVec2 mouse=ImGui::GetMousePos();std::size_t bestFace=std::numeric_limits<std::size_t>::max();float bestDepth=-std::numeric_limits<float>::infinity();
        float bestVertex=12.0f;std::uint64_t bestVertexId=std::numeric_limits<std::uint64_t>::max();
        auto pointIn=[](const ImVec2&p,const ImVec2&a,const ImVec2&b,const ImVec2&c){const float area=(b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);if(std::abs(area)<.001f)return false;const float u=((p.x-a.x)*(c.y-a.y)-(p.y-a.y)*(c.x-a.x))/area,v=((b.x-a.x)*(p.y-a.y)-(b.y-a.y)*(p.x-a.x))/area;return u>=0&&v>=0&&u+v<=1;};
        auto distanceSegment=[](const ImVec2&p,const ImVec2&a,const ImVec2&b){const glm::vec2 ab(b.x-a.x,b.y-a.y),ap(p.x-a.x,p.y-a.y);const float d=glm::dot(ab,ab);const float t=d>.001f?std::clamp(glm::dot(ap,ab)/d,0.f,1.f):0.f;return glm::length(ap-ab*t);};
        float bestEdge=10.0f;
        for(std::size_t i=0;i<m_triangles.size();++i){const auto&t=m_triangles[i];const glm::vec3 pa=project3(t.a),pb=project3(t.b),pc=project3(t.c);const ImVec2 a(pa.x,pa.y),b(pb.x,pb.y),c(pc.x,pc.y);
            if(m_componentMode==2&&pointIn(mouse,a,b,c)){const float depth=(pa.z+pb.z+pc.z)/3;if(depth>bestDepth){bestDepth=depth;bestFace=i;}}
            if(m_componentMode==1){const float d=std::min({distanceSegment(mouse,a,b),distanceSegment(mouse,b,c),distanceSegment(mouse,c,a)});if(d<bestEdge){bestEdge=d;bestFace=i;}}
            if(m_componentMode==0){const std::pair<ImVec2,std::uint32_t> pts[]={{a,t.ia},{b,t.ib},{c,t.ic}};for(const auto&[p,id]:pts){const float d=std::hypot(p.x-mouse.x,p.y-mouse.y);if(d<bestVertex){bestVertex=d;bestVertexId=(static_cast<std::uint64_t>(t.subMesh)<<32)|id;}}}
        }
        const bool additive=ImGui::GetIO().KeyShift;if(!additive){m_selectedFaces.clear();m_selectedVertices.clear();}
        if(m_componentMode==0&&bestVertexId!=std::numeric_limits<std::uint64_t>::max()){auto it=std::find(m_selectedVertices.begin(),m_selectedVertices.end(),bestVertexId);if(it==m_selectedVertices.end())m_selectedVertices.push_back(bestVertexId);else if(additive)m_selectedVertices.erase(it);}
        else if(bestFace!=std::numeric_limits<std::size_t>::max()){if(!m_selectedFaces.erase(bestFace))m_selectedFaces.insert(bestFace);}
    }

    const std::size_t maxTriangles = 24000;
    const std::size_t step = std::max<std::size_t>(1, m_triangles.size() / maxTriangles);
    for (std::size_t i = 0; i < m_triangles.size(); i += step) {
        const Triangle& t = m_triangles[i];
        const ImVec2 a = project(t.a), b = project(t.b), c = project(t.c);
        if (m_editMode==2&&m_selectedFaces.count(i))draw->AddTriangleFilled(a,b,c,IM_COL32(245,145,38,190));
        else if (m_kind == Kind::Static && t.subMesh < m_staticAsset.subMeshes.size()) {
            const auto& colors = m_staticAsset.subMeshes[t.subMesh].vertexColors;
            const auto colorAt = [&](std::uint32_t vertex, int channel) {
                const std::size_t at = static_cast<std::size_t>(vertex) * 4u
                    + static_cast<std::size_t>(channel);
                return at < colors.size() ? colors[at] : 1.0f;
            };
            float red = 1.0f, green = 1.0f, blue = 1.0f;
            if (m_editMode == 1 && m_paintTarget > 0) {
                const int channel = m_paintTarget - 1;
                red = green = blue = (colorAt(t.ia, channel) + colorAt(t.ib, channel)
                    + colorAt(t.ic, channel)) / 3.0f;
            } else {
                red = (colorAt(t.ia, 0) + colorAt(t.ib, 0) + colorAt(t.ic, 0)) / 3.0f;
                green = (colorAt(t.ia, 1) + colorAt(t.ib, 1) + colorAt(t.ic, 1)) / 3.0f;
                blue = (colorAt(t.ia, 2) + colorAt(t.ib, 2) + colorAt(t.ic, 2)) / 3.0f;
            }
            draw->AddTriangleFilled(a, b, c, ImGui::ColorConvertFloat4ToU32(
                ImVec4(red * 0.78f, green * 0.78f, blue * 0.78f, 1.0f)));
        }
        draw->AddLine(a, b, IM_COL32(117, 157, 198, 115), 1.0f);
        draw->AddLine(b, c, IM_COL32(117, 157, 198, 115), 1.0f);
        draw->AddLine(c, a, IM_COL32(117, 157, 198, 115), 1.0f);
    }

    if(m_editMode==2&&m_componentMode==0)for(const std::uint64_t encoded:m_selectedVertices){const std::size_t sub=static_cast<std::size_t>(encoded>>32);const std::uint32_t vertex=static_cast<std::uint32_t>(encoded);if(sub<m_staticAsset.subMeshes.size()&&vertex<m_staticAsset.subMeshes[sub].vertices.size()/engine::kStaticMeshVertexStride)draw->AddCircleFilled(project(ReadPosition(m_staticAsset.subMeshes[sub].vertices,vertex,engine::kStaticMeshVertexStride)),5,IM_COL32(255,178,35,255));}

    if (m_editMode == 1 && m_kind == Kind::Static && ImGui::IsItemHovered()) {
        const ImVec2 mouse = ImGui::GetMousePos();
        draw->AddCircle(mouse, m_brushRadius, IM_COL32(255, 188, 52, 255), 48, 2.0f);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            PushUndo();
            m_paintStroke = true;
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) m_paintStroke = false;
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            EnsurePaintData();
            float hitDepth = -std::numeric_limits<float>::infinity();
            for (const Triangle& triangle : m_triangles) {
                const glm::vec3 pa = project3(triangle.a);
                const glm::vec3 pb = project3(triangle.b);
                const glm::vec3 pc = project3(triangle.c);
                const glm::vec2 p(mouse.x, mouse.y), a(pa.x, pa.y), b(pb.x, pb.y), c(pc.x, pc.y);
                const float area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
                if (std::abs(area) < 0.001f) continue;
                const float u = ((p.x - a.x) * (c.y - a.y) - (p.y - a.y) * (c.x - a.x)) / area;
                const float v = ((b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x)) / area;
                if (u >= 0.0f && v >= 0.0f && u + v <= 1.0f)
                    hitDepth = std::max(hitDepth, pa.z * (1.0f - u - v) + pb.z * u + pc.z * v);
            }
            const float depthTolerance = m_brushRadius / std::max(scale, 0.001f) * 1.5f;
            const float amountScale = std::clamp(m_brushStrength * ImGui::GetIO().DeltaTime * 12.0f,
                                                 0.0f, 1.0f);
            bool changed = false;
            if (m_paintThrough || std::isfinite(hitDepth)) {
              for (auto& subMesh : m_staticAsset.subMeshes) {
                const std::size_t count = subMesh.vertices.size() / engine::kStaticMeshVertexStride;
                for (std::size_t vertex = 0; vertex < count; ++vertex) {
                    const glm::vec3 p = project3(ReadPosition(
                        subMesh.vertices, vertex, engine::kStaticMeshVertexStride));
                    const float dx = p.x - mouse.x, dy = p.y - mouse.y;
                    const float distance = std::sqrt(dx * dx + dy * dy);
                    if (distance > m_brushRadius) continue;
                    if (!m_paintThrough && std::isfinite(hitDepth)
                        && std::abs(p.z - hitDepth) > depthTolerance) continue;
                    const float blend = amountScale * (1.0f - distance / m_brushRadius);
                    const std::size_t color = vertex * 4u;
                    if (m_paintTarget == 0) {
                        for (int channel = 0; channel < 4; ++channel) {
                            const float target = m_erasePaint ? 1.0f : m_brushColor[channel];
                            subMesh.vertexColors[color + channel] +=
                                (target - subMesh.vertexColors[color + channel]) * blend;
                        }
                    } else {
                        const int channel = m_paintTarget - 1;
                        const float target = m_erasePaint ? 0.0f : m_brushColor[channel];
                        subMesh.vertexColors[color + static_cast<std::size_t>(channel)] +=
                            (target - subMesh.vertexColors[color + static_cast<std::size_t>(channel)]) * blend;
                    }
                    changed = true;
                }
              }
            }
            m_paintDirty |= changed;
        }
    } else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        m_paintStroke = false;
    }

    const ImVec2 pivot = project(m_pivot);
    const float axisLength = std::max(radius * 0.20f, 0.01f);
    draw->AddLine(pivot, project(m_pivot + glm::vec3(axisLength, 0, 0)),
                  IM_COL32(235, 68, 68, 255), 2.0f);
    draw->AddLine(pivot, project(m_pivot + glm::vec3(0, axisLength, 0)),
                  IM_COL32(65, 210, 96, 255), 2.0f);
    draw->AddLine(pivot, project(m_pivot + glm::vec3(0, 0, axisLength)),
                  IM_COL32(72, 132, 245, 255), 2.0f);
    draw->AddCircleFilled(pivot, 4.0f, IM_COL32(255, 194, 41, 255));
    draw->AddText(ImVec2(origin.x + 8.0f, origin.y + 8.0f),
                  IM_COL32(190, 198, 210, 255),
                  m_editMode == 1 ? "Left-drag: paint   Right-drag: orbit   Wheel: zoom"
                  : m_editMode == 2 ? "Click: select component   Shift-click: add/remove   Right-drag: orbit"
                                  : "Left-drag: orbit   Wheel: zoom");
}

void MeshEditorPanel::Draw(bool* open, bool* assetSaved, std::string* message) {
    if (assetSaved) *assetSaved = false;
    if (message) message->clear();
    if (!m_pendingOpen.empty()) {
        std::string error;
        if (!Load(m_pendingOpen, &error) && message) *message = "Mesh Editor: " + error;
        m_pendingOpen.clear();
    }

    if (!ImGui::Begin(EditorPanels::Name(EditorPanels::Panel::MeshEditor), open)) {
        ImGui::End();
        return;
    }

    if (m_kind == Kind::None) {
        ImGui::TextDisabled("Double-click an engine-owned .3dgmesh or .3dgskmesh asset.");
        ImGui::End();
        return;
    }

    ImGui::Text("%s", std::filesystem::path(m_path).filename().string().c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%s", m_kind == Kind::Static ? "Static Mesh" : "Skeletal Mesh");
    ImGui::TextDisabled("%zu vertices | %zu triangles | %zu submeshes",
                        m_vertexCount, m_triangleCount,
                        m_kind == Kind::Static ? m_staticAsset.subMeshes.size()
                                               : m_skeletalAsset.subMeshes.size());
    ImGui::Separator();
    if (ImGui::Button(m_editMode == 0 ? "Pivot / Origin *" : "Pivot / Origin"))
        m_editMode = 0;
    ImGui::SameLine();
    if (m_kind != Kind::Static) ImGui::BeginDisabled();
    if (ImGui::Button(m_editMode == 1 ? "Mesh Paint *" : "Mesh Paint")) {
        m_editMode = 1;
        EnsurePaintData();
    }
    if (m_kind != Kind::Static) ImGui::EndDisabled();
    if (m_kind == Kind::Skeletal) {
        ImGui::SameLine();
        ImGui::TextDisabled("Vertex paint currently supports static meshes");
    }
    ImGui::SameLine();
    if (m_kind != Kind::Static) ImGui::BeginDisabled();
    if (ImGui::Button(m_editMode == 2 ? "Geometry *" : "Geometry")) m_editMode = 2;
    if (m_kind != Kind::Static) ImGui::EndDisabled();
    ImGui::Separator();

    bool requestBakeConfirmation = false;
    const float detailsWidth = std::min(360.0f, ImGui::GetContentRegionAvail().x * 0.36f);
    if (ImGui::BeginChild("##MeshDetails", ImVec2(detailsWidth, 0.0f), true)) {
        if (m_editMode == 0) {
        ImGui::TextUnformatted("PIVOT / ORIGIN");
        ImGui::TextWrapped("Choose a preset or enter a custom local-space position. "
                           "The yellow marker is the resulting mesh origin.");
        ImGui::Spacing();
        const glm::vec3 center = (m_minimum + m_maximum) * 0.5f;
        if (ImGui::Button("Original Origin", ImVec2(-1.0f, 0.0f))) SetPivot(glm::vec3(0.0f));
        if (ImGui::Button("Bounds Center", ImVec2(-1.0f, 0.0f))) SetPivot(center);
        if (ImGui::Button("Bottom Center", ImVec2(-1.0f, 0.0f)))
            SetPivot({center.x, m_minimum.y, center.z});
        if (ImGui::Button("Top Center", ImVec2(-1.0f, 0.0f)))
            SetPivot({center.x, m_maximum.y, center.z});

        float pivot[3] = {m_pivot.x, m_pivot.y, m_pivot.z};
        if (ImGui::DragFloat3("Custom", pivot, 0.01f, 0.0f, 0.0f, "%.4f"))
            SetPivot({pivot[0], pivot[1], pivot[2]});

        ImGui::Spacing();
        ImGui::Text("Bounds min: %.3f  %.3f  %.3f", m_minimum.x, m_minimum.y, m_minimum.z);
        ImGui::Text("Bounds max: %.3f  %.3f  %.3f", m_maximum.x, m_maximum.y, m_maximum.z);
        ImGui::Separator();
        if (!m_dirty) ImGui::BeginDisabled();
        if (ImGui::Button("Bake Pivot Into Asset", ImVec2(-1.0f, 0.0f)))
            requestBakeConfirmation = true;
        if (!m_dirty) ImGui::EndDisabled();
        ImGui::TextDisabled("Reimporting from the source file restores its source pivot.");
        } else if (m_editMode == 1) {
            ImGui::TextUnformatted("MESH PAINT");
            ImGui::TextWrapped("Paint persistent vertex colors or use the R, G, B and A "
                               "channels as masks in a custom material shader.");
            const char* targets[] = {"Vertex Color (RGBA)", "Red Mask", "Green Mask",
                                     "Blue Mask", "Alpha Mask"};
            ImGui::Combo("Target", &m_paintTarget, targets, IM_ARRAYSIZE(targets));
            if (m_paintTarget == 0) {
                ImGui::ColorEdit4("Paint Color", m_brushColor.data(),
                                  ImGuiColorEditFlags_AlphaBar);
            } else {
                ImGui::SliderFloat("Mask Value", &m_brushColor[
                    static_cast<std::size_t>(m_paintTarget - 1)], 0.0f, 1.0f, "%.2f");
            }
            ImGui::SliderFloat("Brush Radius", &m_brushRadius, 4.0f, 180.0f, "%.0f px");
            ImGui::SliderFloat("Strength", &m_brushStrength, 0.02f, 1.0f, "%.2f");
            ImGui::Checkbox("Erase", &m_erasePaint);
            ImGui::Checkbox("Paint Through Mesh", &m_paintThrough);
            ImGui::TextDisabled("Left-drag paints. Right-drag orbits the preview.");
            ImGui::Separator();

            const bool cannotUndo = m_undoPaint.empty();
            if (cannotUndo) ImGui::BeginDisabled();
            if (ImGui::Button("Undo")) {
                m_redoPaint.push_back(CapturePaint());
                RestorePaint(m_undoPaint.back());
                m_undoPaint.pop_back();
            }
            if (cannotUndo) ImGui::EndDisabled();
            ImGui::SameLine();
            const bool cannotRedo = m_redoPaint.empty();
            if (cannotRedo) ImGui::BeginDisabled();
            if (ImGui::Button("Redo")) {
                m_undoPaint.push_back(CapturePaint());
                RestorePaint(m_redoPaint.back());
                m_redoPaint.pop_back();
            }
            if (cannotRedo) ImGui::EndDisabled();

            if (ImGui::Button("Fill Target", ImVec2(-1.0f, 0.0f))) {
                PushUndo();
                EnsurePaintData();
                for (auto& subMesh : m_staticAsset.subMeshes) {
                    const std::size_t count = subMesh.vertexColors.size() / 4u;
                    for (std::size_t vertex = 0; vertex < count; ++vertex) {
                        if (m_paintTarget == 0) {
                            for (int channel = 0; channel < 4; ++channel)
                                subMesh.vertexColors[vertex * 4u + channel] = m_brushColor[channel];
                        } else {
                            subMesh.vertexColors[vertex * 4u
                                + static_cast<std::size_t>(m_paintTarget - 1)] =
                                    m_brushColor[static_cast<std::size_t>(m_paintTarget - 1)];
                        }
                    }
                }
                m_paintDirty = true;
            }
            if (ImGui::Button("Clear Target", ImVec2(-1.0f, 0.0f))) {
                PushUndo();
                EnsurePaintData();
                for (auto& subMesh : m_staticAsset.subMeshes) {
                    if (m_paintTarget == 0) {
                        std::fill(subMesh.vertexColors.begin(), subMesh.vertexColors.end(), 1.0f);
                    } else {
                        const std::size_t channel = static_cast<std::size_t>(m_paintTarget - 1);
                        for (std::size_t i = channel; i < subMesh.vertexColors.size(); i += 4u)
                            subMesh.vertexColors[i] = 0.0f;
                    }
                }
                m_paintDirty = true;
            }
            ImGui::Separator();
            const bool paintIsSaved = !m_paintDirty;
            if (paintIsSaved) ImGui::BeginDisabled();
            if (ImGui::Button("Save Vertex Paint", ImVec2(-1.0f, 0.0f))) {
                std::string error;
                if (SavePaint(&error)) {
                    if (assetSaved) *assetSaved = true;
                    if (message) *message = "Saved mesh paint: " + m_path;
                } else if (message) {
                    *message = "Mesh paint save failed: " + error;
                }
            }
            if (paintIsSaved) ImGui::EndDisabled();
            ImGui::TextColored(m_paintDirty ? ImVec4(1.0f, 0.72f, 0.25f, 1.0f)
                                            : ImVec4(0.35f, 0.85f, 0.45f, 1.0f),
                               m_paintDirty ? "Unsaved paint" : "Paint saved");
            ImGui::Separator();
            ImGui::TextWrapped("Shader use: add the Vertex Color node. RGB can tint the "
                               "surface; individual channels can blend dirt, snow, wetness, "
                               "damage or foliage density.");
        } else {
            ImGui::TextUnformatted("GEOMETRY EDITING");
            ImGui::TextWrapped("Edit the engine-owned static mesh. Changes stay in memory until Apply.");
            const char* modes[]={"Vertex","Edge","Face"};ImGui::Combo("Component",&m_componentMode,modes,IM_ARRAYSIZE(modes));
            if(ImGui::Button("Select All")){if(m_componentMode==2){for(std::size_t i=0;i<m_triangles.size();++i)m_selectedFaces.insert(i);}else if(m_componentMode==0){m_selectedVertices.clear();for(std::size_t s=0;s<m_staticAsset.subMeshes.size();++s)for(std::uint32_t v=0;v<m_staticAsset.subMeshes[s].vertices.size()/engine::kStaticMeshVertexStride;++v)m_selectedVertices.push_back((static_cast<std::uint64_t>(s)<<32)|v);}}
            ImGui::SameLine();if(ImGui::Button("Connected")&&m_componentMode!=0)SelectConnectedFaces();ImGui::SameLine();if(ImGui::Button("Clear")){m_selectedFaces.clear();m_selectedVertices.clear();}
            ImGui::Text("%zu faces | %zu vertices selected",m_selectedFaces.size(),m_selectedVertices.size());
            std::vector<std::size_t> faces(m_selectedFaces.begin(),m_selectedFaces.end());std::sort(faces.begin(),faces.end());
            const bool noFaces=faces.empty();if(noFaces)ImGui::BeginDisabled();
            ImGui::DragFloat("Extrude Distance",&m_extrudeDistance,.01f,-100,100,"%.3f");
            if(ImGui::Button("Extrude Faces",ImVec2(-1,0))){PushGeometryUndo();MeshGeometryOperations::ExtrudeFaces(m_staticAsset,faces,m_extrudeDistance);RefreshGeometryState();}
            ImGui::SliderFloat("Inset Amount",&m_insetAmount,.001f,.95f,"%.3f");
            if(ImGui::Button("Inset Faces",ImVec2(-1,0))){PushGeometryUndo();MeshGeometryOperations::InsetFaces(m_staticAsset,faces,m_insetAmount);RefreshGeometryState();}
            if(ImGui::Button("Bevel Faces",ImVec2(-1,0))){PushGeometryUndo();MeshGeometryOperations::InsetFaces(m_staticAsset,faces,m_insetAmount);MeshGeometryOperations::ExtrudeFaces(m_staticAsset,faces,m_extrudeDistance);RefreshGeometryState();}
            if(ImGui::Button("Subdivide / Add Edge Loops",ImVec2(-1,0))){PushGeometryUndo();MeshGeometryOperations::SubdivideFaces(m_staticAsset,faces);RefreshGeometryState();}
            if(ImGui::Button("Delete Faces",ImVec2(-1,0))){PushGeometryUndo();MeshGeometryOperations::DeleteFaces(m_staticAsset,faces);RefreshGeometryState();}
            if(noFaces)ImGui::EndDisabled();
            ImGui::SeparatorText("Vertex / Repair");ImGui::DragFloat("Weld Tolerance",&m_weldTolerance,.0001f,.000001f,1,"%.6f");
            if(ImGui::Button("Weld Coincident Vertices",ImVec2(-1,0))){PushGeometryUndo();MeshGeometryOperations::WeldVertices(m_staticAsset,m_weldTolerance);RefreshGeometryState();}
            const bool bridgeInvalid=m_selectedVertices.size()<4||m_selectedVertices.size()%2!=0;if(bridgeInvalid)ImGui::BeginDisabled();
            if(ImGui::Button("Bridge Split Vertex Selection",ImVec2(-1,0))){const std::size_t half=m_selectedVertices.size()/2,sub=static_cast<std::size_t>(m_selectedVertices[0]>>32);std::vector<std::uint32_t>a,b;bool same=true;for(std::size_t i=0;i<m_selectedVertices.size();++i){same&=static_cast<std::size_t>(m_selectedVertices[i]>>32)==sub;(i<half?a:b).push_back(static_cast<std::uint32_t>(m_selectedVertices[i]));}if(same){PushGeometryUndo();MeshGeometryOperations::BridgeLoops(m_staticAsset,sub,a,b);RefreshGeometryState();}}
            if(bridgeInvalid)ImGui::EndDisabled();
            if(ImGui::Button("Recalculate Normals + Tangents",ImVec2(-1,0))){PushGeometryUndo();MeshGeometryOperations::RecalculateNormalsAndTangents(m_staticAsset);RefreshGeometryState();}
            if(ImGui::Button("Remove Degenerate Faces",ImVec2(-1,0))){PushGeometryUndo();MeshGeometryOperations::RemoveDegenerate(m_staticAsset,1e-8f);RefreshGeometryState();}
            const auto topology=MeshGeometryOperations::AnalyzeTopology(m_staticAsset);
            ImGui::Text("Boundary edges: %zu",topology.boundaryEdges);
            ImGui::TextColored(topology.nonManifoldEdges?ImVec4(1,.35f,.25f,1):ImVec4(.45f,.8f,.5f,1),"Non-manifold edges: %zu",topology.nonManifoldEdges);
            ImGui::TextColored(topology.degenerateFaces?ImVec4(1,.35f,.25f,1):ImVec4(.45f,.8f,.5f,1),"Degenerate faces: %zu",topology.degenerateFaces);
            ImGui::SeparatorText("History");const bool noUndo=m_undoGeometry.empty();if(noUndo)ImGui::BeginDisabled();if(ImGui::Button("Undo Geometry")){m_redoGeometry.push_back(m_staticAsset);m_staticAsset=m_undoGeometry.back();m_undoGeometry.pop_back();RefreshGeometryState();}if(noUndo)ImGui::EndDisabled();ImGui::SameLine();const bool noRedo=m_redoGeometry.empty();if(noRedo)ImGui::BeginDisabled();if(ImGui::Button("Redo Geometry")){m_undoGeometry.push_back(m_staticAsset);m_staticAsset=m_redoGeometry.back();m_redoGeometry.pop_back();RefreshGeometryState();}if(noRedo)ImGui::EndDisabled();
            ImGui::Separator();if(!m_geometryDirty)ImGui::BeginDisabled();if(ImGui::Button("Apply Geometry To Asset",ImVec2(-1,0))){std::string error;if(SaveGeometry(&error)){if(assetSaved)*assetSaved=true;if(message)*message="Saved mesh geometry: "+m_path;}else if(message)*message="Geometry save failed: "+error;}if(ImGui::Button("Revert Geometry",ImVec2(-1,0))){m_staticAsset=m_geometryOriginal;m_undoGeometry.clear();m_redoGeometry.clear();RefreshGeometryState();m_geometryDirty=false;}if(!m_geometryDirty)ImGui::EndDisabled();
            ImGui::TextColored(m_geometryDirty?ImVec4(1,.72f,.25f,1):ImVec4(.35f,.85f,.45f,1),m_geometryDirty?"Unsaved geometry":"Geometry saved");
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();
    if (ImGui::BeginChild("##MeshPreviewHost", ImVec2(0.0f, 0.0f), true)) DrawPreview();
    ImGui::EndChild();

    // Open and begin the modal at the same window/ID-stack level. Opening it
    // from inside the details child would create a different popup ID.
    if (requestBakeConfirmation) ImGui::OpenPopup("Bake mesh pivot?");
    if (ImGui::BeginPopupModal("Bake mesh pivot?", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("This rewrites the engine-owned mesh asset. Scene transforms "
                           "are not changed, so existing instances will visibly move by "
                           "the pivot offset.");
        if (ImGui::Button("Bake", ImVec2(120.0f, 0.0f))) {
            std::string error;
            if (BakePivot(&error)) {
                if (assetSaved) *assetSaved = true;
                if (message) *message = "Baked mesh pivot: " + m_path;
            } else if (message) {
                *message = "Mesh pivot bake failed: " + error;
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::End();
}
