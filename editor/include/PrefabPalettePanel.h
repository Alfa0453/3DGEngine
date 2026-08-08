#pragma once

#include <glm/glm.hpp>

#include <array>
#include <random>
#include <string>
#include <unordered_set>
#include <vector>

class PrefabPalettePanel {
public:
    struct Placement {
        std::string path;
        std::string name;
        float yawDegrees = 0.0f;
        float uniformScale = 1.0f;
    };
    struct Result {
        bool placeInFrontRequested = false;
        bool replaceSelectedRequested = false;
    };

    Result Draw(const std::string& assetRoot, bool* open);
    void Refresh(const std::string& assetRoot);
    bool PlacementActive() const { return m_placementActive; }
    bool SurfaceSnap() const { return m_surfaceSnap; }
    bool OffsetByBounds() const { return m_offsetByBounds; }
    float GridSize() const { return m_gridSize; }
    Placement NextPlacement();
    glm::vec3 SnapPosition(const glm::vec3& position) const;

private:
    struct PreviewLine { glm::vec2 a{0.0f}; glm::vec2 b{0.0f}; };
    struct Entry {
        std::string path;
        std::string relativePath;
        std::string name;
        std::string category;
        std::string variantGroup;
        std::vector<PreviewLine> preview;
    };

    void LoadFavorites();
    void SaveFavorites() const;
    void BuildPreview(Entry& entry, const std::string& modelPath) const;
    const Entry* Selected() const;
    std::vector<int> VisibleEntries() const;
    std::vector<int> VariantEntries(int sourceIndex) const;

    std::vector<Entry> m_entries;
    std::vector<std::string> m_categories;
    std::unordered_set<std::string> m_favorites;
    std::array<char, 128> m_filter{};
    std::string m_assetRoot;
    std::string m_category = "All";
    int m_selected = -1;
    bool m_placementActive = false;
    bool m_surfaceSnap = true;
    bool m_offsetByBounds = true;
    bool m_favoritesOnly = false;
    bool m_randomVariant = true;
    bool m_randomYaw = false;
    bool m_randomScale = false;
    float m_yaw = 0.0f;
    float m_yawStep = 15.0f;
    float m_scaleMin = 0.9f;
    float m_scaleMax = 1.1f;
    float m_gridSize = 0.5f;
    mutable std::mt19937 m_random{std::random_device{}()};
};
