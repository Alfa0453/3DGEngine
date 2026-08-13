#pragma once

#include "LightingAnalysis.h"

#include <glm/glm.hpp>

#include <string>
#include <vector>

class EditorScene;
namespace engine { class RuntimeAssetManager; }

class LightingAnalysisPanel {
public:
    enum class ViewMode {
        Disabled = 0,
        LightComplexity,
        ShadowCoverage,
        Exposure,
        UnlitAreas,
        TransparencyCost
    };

    struct OverlayCell {
        glm::vec3 position{0.0f};
        float value = 0.0f;
        bool warning = false;
    };

    struct Result { int frameObject = -1; };

    Result Draw(const EditorScene& scene, engine::RuntimeAssetManager& assets,
                const std::string& assetRoot, bool* open);
    void Scan(const EditorScene& scene, engine::RuntimeAssetManager& assets);
    bool OverlayEnabled() const { return m_overlayEnabled && m_mode != ViewMode::Disabled; }
    ViewMode Mode() const { return m_mode; }
    const std::vector<OverlayCell>& OverlayCells() const { return m_overlayCells; }
    float CellSize() const;

private:
    void BuildOverlay(const EditorScene& scene, engine::RuntimeAssetManager& assets);
    bool ExportReport(const std::string& assetRoot, std::string* path,
                      std::string* error) const;

    editor::lighting::Settings m_settings;
    editor::lighting::Report m_report;
    std::vector<OverlayCell> m_overlayCells;
    ViewMode m_mode = ViewMode::LightComplexity;
    bool m_overlayEnabled = true;
    bool m_scanned = false;
    int m_selectedFinding = -1;
    std::string m_status = "Press Analyze Level to sample the authored lighting.";
};
