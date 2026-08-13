#pragma once

#include <engine/assets/SkeletalAsset.h>
#include <engine/assets/StaticMeshAsset.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_set>

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
        std::size_t subMesh = 0;
        std::uint32_t ia = 0;
        std::uint32_t ib = 0;
        std::uint32_t ic = 0;
    };
    using PaintSnapshot = std::vector<std::vector<float>>;

    bool Load(const std::string& path, std::string* error);
    bool BakePivot(std::string* error);
    bool SavePaint(std::string* error);
    void RebuildPreviewGeometry();
    void DrawPreview();
    void SetPivot(const glm::vec3& pivot);
    void EnsurePaintData();
    PaintSnapshot CapturePaint() const;
    void RestorePaint(const PaintSnapshot& snapshot);
    void PushUndo();
    void PushGeometryUndo();
    bool SaveGeometry(std::string* error);
    void RefreshGeometryState();
    void SelectConnectedFaces();

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
    bool m_paintDirty = false;
    bool m_paintStroke = false;
    int m_editMode = 0;       // 0 pivot, 1 vertex paint, 2 geometry
    int m_paintTarget = 0;    // 0 RGBA, 1..4 individual mask channels
    float m_brushRadius = 42.0f;
    float m_brushStrength = 0.55f;
    std::array<float, 4> m_brushColor{{0.8f, 0.18f, 0.08f, 1.0f}};
    bool m_erasePaint = false;
    bool m_paintThrough = false;
    std::vector<PaintSnapshot> m_undoPaint;
    std::vector<PaintSnapshot> m_redoPaint;
    engine::StaticMeshAssetData m_geometryOriginal;
    std::vector<engine::StaticMeshAssetData> m_undoGeometry;
    std::vector<engine::StaticMeshAssetData> m_redoGeometry;
    std::unordered_set<std::size_t> m_selectedFaces;
    std::vector<std::uint64_t> m_selectedVertices; // high 32: submesh, low 32: vertex
    int m_componentMode = 2; // 0 vertex, 1 edge, 2 face
    float m_extrudeDistance = 0.25f;
    float m_insetAmount = 0.2f;
    float m_weldTolerance = 0.001f;
    bool m_geometryDirty = false;
};
