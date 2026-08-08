#pragma once

#include <array>
#include <glm/glm.hpp>
#include <string>
#include <vector>

class ModularPlacementPanel {
public:
    enum class AssetKind { Model, Prefab };
    struct AssetChoice {
        AssetKind kind = AssetKind::Model;
        std::string path;
        std::string name;
        std::string relativePath;
    };
    struct Result {
        bool refreshRequested = false;
        bool placeInFrontRequested = false;
        bool replaceSelectedRequested = false;
    };

    Result Draw(const std::string& assetRoot, bool* open);
    void Refresh(const std::string& assetRoot);

    const AssetChoice* SelectedAsset() const;
    bool PlacementActive() const { return m_placementActive; }
    bool PaintMode() const { return m_paintMode; }
    bool SurfaceSnap() const { return m_surfaceSnap; }
    bool OffsetByBounds() const { return m_offsetByBounds; }
    float GridSize() const { return m_gridSize; }
    float RotationDegrees() const { return m_rotationDegrees; }
    float PaintSpacing() const { return m_paintSpacing; }
    glm::vec3 SnapPosition(const glm::vec3& position) const;

private:
    std::vector<AssetChoice> m_assets;
    std::array<char, 128> m_filter{};
    std::string m_scannedRoot;
    int m_selected = -1;
    bool m_placementActive = false;
    bool m_paintMode = false;
    bool m_surfaceSnap = true;
    bool m_offsetByBounds = true;
    bool m_snapX = true;
    bool m_snapY = true;
    bool m_snapZ = true;
    float m_gridSize = 0.5f;
    float m_rotationDegrees = 0.0f;
    float m_rotationStep = 15.0f;
    float m_paintSpacing = 1.0f;
};
