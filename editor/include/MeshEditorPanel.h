#pragma once

#include <engine/assets/SkeletalAsset.h>
#include <engine/assets/StaticMeshAsset.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

// Edits engine-owned mesh metadata and geometry. Pivot changes are previewed
// non-destructively, then baked only after an explicit confirmation.
class MeshEditorPanel {
public:
    void QueueOpen(const std::string& path);
    void Draw(bool* open, bool* assetSaved, std::string* message);

    const std::string& Path() const { return m_path; }
    bool IsSkeletal() const { return m_kind == Kind::Skeletal; }

private:
    enum class Kind { None, Static, Skeletal };
    struct Triangle {
        glm::vec3 a{0.0f};
        glm::vec3 b{0.0f};
        glm::vec3 c{0.0f};
    };

    bool Load(const std::string& path, std::string* error);
    bool BakePivot(std::string* error);
    void RebuildPreviewGeometry();
    void DrawPreview();
    void SetPivot(const glm::vec3& pivot);

    Kind m_kind = Kind::None;
    std::string m_path;
    std::string m_pendingOpen;
    engine::StaticMeshAssetData m_staticAsset;
    engine::SkeletalMeshAssetData m_skeletalAsset;
    std::vector<Triangle> m_triangles;
    glm::vec3 m_minimum{0.0f};
    glm::vec3 m_maximum{0.0f};
    glm::vec3 m_pivot{0.0f};
    std::size_t m_vertexCount = 0;
    std::size_t m_triangleCount = 0;
    float m_previewYaw = -0.55f;
    float m_previewPitch = 0.30f;
    float m_previewZoom = 1.0f;
    bool m_dirty = false;
};
