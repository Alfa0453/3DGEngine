#pragma once

#include <engine/assets/ScatterGraphAsset.h>

#include <array>
#include <string>
#include <vector>

class ProceduralScatterGraphPanel {
public:
    enum class BakeTarget { EditableObjects = 0, FoliageInstances = 1 };
    struct Result {
        bool saveRequested = false;
        bool refreshAssets = false;
        bool bakeRequested = false;
        BakeTarget bakeTarget = BakeTarget::EditableObjects;
        std::vector<engine::ScatterPlacement> placements;
    };

    void QueueOpen(const std::string& path) { m_pendingOpen = path; }
    Result Draw(const std::string& contentRoot, bool* open);
    const engine::ScatterGraphAssetData& Graph() const { return m_graph; }
    const std::string& Path() const { return m_path; }

private:
    struct MeshChoice { std::string name, path, relative; engine::AssetHandle id; };
    void NewGraph(const std::string& contentRoot);
    bool Load(const std::string& path, std::string* error);
    bool Save(std::string* error);
    void RefreshMeshes(const std::string& contentRoot);
    void RefreshPreview();
    void AddNode(engine::ScatterNodeType type);
    engine::ScatterGraphNode* SelectedNode();
    void DrawCanvas();
    void DrawDetails();
    void DrawPreview();

    engine::ScatterGraphAssetData m_graph;
    std::string m_path;
    std::string m_pendingOpen;
    std::string m_scannedRoot;
    std::vector<MeshChoice> m_meshes;
    std::vector<engine::ScatterPlacement> m_preview;
    std::array<char, 128> m_name{};
    std::array<char, 320> m_pathBuffer{};
    std::uint32_t m_nextNodeId = 1;
    std::uint32_t m_selectedNode = 0;
    int m_addType = 1;
    int m_bakeTarget = 0;
    bool m_dirty = false;
    bool m_autoPreview = true;
    float m_canvasZoom = 1.0f;
    glm::vec2 m_canvasPan{20.0f};
};
