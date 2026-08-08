#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>
#include <random>
#include <string>
#include <vector>

class ScatterPaintPanel {
public:
    enum class Mode { Paint, Erase };

    struct AssetChoice {
        std::string path;
        std::string name;
        std::string relativePath;
    };

    struct StampPoint {
        glm::vec3 position{0.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        float uniformScale = 1.0f;
    };

    struct Result {
        bool clearAllRequested = false;
    };

    Result Draw(const std::string& assetRoot, bool* open);
    void Refresh(const std::string& assetRoot);

    const AssetChoice* SelectedAsset() const;
    bool BrushActive() const { return m_active && (m_mode == Mode::Erase || SelectedAsset()); }
    bool Painting() const { return m_mode == Mode::Paint; }
    Mode CurrentMode() const { return m_mode; }
    float Radius() const { return m_radius; }
    float StrokeSpacing() const { return m_strokeSpacing; }
    float MinimumSpacing() const { return m_minimumSpacing; }
    float HeightOffset() const { return m_heightOffset; }
    bool KeepOutsideSurface() const { return m_keepOutsideSurface; }
    bool SlopeAllowed(const glm::vec3& normal) const;
    std::vector<StampPoint> MakeStamp(const glm::vec3& center,
                                      const glm::vec3& normal);

private:
    std::vector<AssetChoice> m_assets;
    std::array<char, 128> m_filter{};
    std::string m_scannedRoot;
    int m_selected = -1;
    bool m_active = false;
    Mode m_mode = Mode::Paint;
    float m_radius = 2.0f;
    float m_density = 0.7f;
    float m_strokeSpacing = 0.75f;
    float m_minimumSpacing = 0.45f;
    float m_maxSlopeDegrees = 50.0f;
    bool m_alignToNormal = true;
    bool m_randomYaw = true;
    bool m_randomScale = true;
    bool m_keepOutsideSurface = true;
    float m_scaleMin = 0.8f;
    float m_scaleMax = 1.2f;
    float m_heightOffset = 0.0f;
    std::mt19937 m_random{std::random_device{}()};
};
