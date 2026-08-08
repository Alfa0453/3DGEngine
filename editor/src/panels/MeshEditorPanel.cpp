#include "MeshEditorPanel.h"

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
    m_previewYaw = -0.55f;
    m_previewPitch = 0.30f;
    m_previewZoom = 1.0f;
    RebuildPreviewGeometry();
    return true;
}

void MeshEditorPanel::RebuildPreviewGeometry() {
    m_triangles.clear();
    m_vertexCount = 0;
    m_triangleCount = 0;

    auto append = [this](const auto& subMeshes, std::size_t stride) {
        for (const auto& subMesh : subMeshes) {
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
                    ReadPosition(subMesh.vertices, ic, stride)});
            }
        }
    };

    if (m_kind == Kind::Static)
        append(m_staticAsset.subMeshes, engine::kStaticMeshVertexStride);
    else if (m_kind == Kind::Skeletal)
        append(m_skeletalAsset.subMeshes, engine::kSkeletalMeshVertexStride);
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
                           ImGuiButtonFlags_MouseButtonLeft);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y),
                        IM_COL32(17, 21, 28, 255));
    draw->AddRect(origin, ImVec2(origin.x + size.x, origin.y + size.y),
                  IM_COL32(54, 67, 84, 255));

    if (ImGui::IsItemHovered()) {
        ImGuiIO& io = ImGui::GetIO();
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
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

    auto project = [&](const glm::vec3& p) {
        const glm::vec3 q = glm::vec3(rotation * glm::vec4(p - m_pivot - center, 1.0f));
        return ImVec2(origin.x + size.x * 0.5f + q.x * scale,
                      origin.y + size.y * 0.5f - q.y * scale);
    };

    const std::size_t maxTriangles = 24000;
    const std::size_t step = std::max<std::size_t>(1, m_triangles.size() / maxTriangles);
    for (std::size_t i = 0; i < m_triangles.size(); i += step) {
        const Triangle& t = m_triangles[i];
        const ImVec2 a = project(t.a), b = project(t.b), c = project(t.c);
        draw->AddLine(a, b, IM_COL32(117, 157, 198, 115), 1.0f);
        draw->AddLine(b, c, IM_COL32(117, 157, 198, 115), 1.0f);
        draw->AddLine(c, a, IM_COL32(117, 157, 198, 115), 1.0f);
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
                  "Left-drag: orbit   Wheel: zoom");
}

void MeshEditorPanel::Draw(bool* open, bool* assetSaved, std::string* message) {
    if (assetSaved) *assetSaved = false;
    if (message) message->clear();
    if (!m_pendingOpen.empty()) {
        std::string error;
        if (!Load(m_pendingOpen, &error) && message) *message = "Mesh Editor: " + error;
        m_pendingOpen.clear();
    }

    if (!ImGui::Begin("Mesh Editor", open)) {
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

    bool requestBakeConfirmation = false;
    const float detailsWidth = std::min(360.0f, ImGui::GetContentRegionAvail().x * 0.36f);
    if (ImGui::BeginChild("##MeshDetails", ImVec2(detailsWidth, 0.0f), true)) {
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
