#pragma once

#include "OptimizationAuditReport.h"

#include <cstddef>
#include <string>
#include <vector>

class EditorScene;
namespace engine { class RuntimeAssetManager; }

class OptimizationAuditorPanel {
public:
    void Draw(EditorScene& scene, engine::RuntimeAssetManager& assets,
              const std::string& assetRoot, bool* open);
    void Scan(const EditorScene& scene, engine::RuntimeAssetManager& assets,
              const std::string& assetRoot);
    int ConsumeFrameRequest();

private:
    enum QuickFix { None = 0, CapParticles = 1 };
    bool ApplyQuickFix(EditorScene& scene,
                       const editor::optimization::Finding& finding);
    bool Export(const std::string& assetRoot, bool json);

    std::vector<editor::optimization::Finding> m_findings;
    std::string m_summary = "Press Scan Level to create a report.";
    std::string m_status;
    int m_selected = -1;
    int m_frameRequest = -1;
    bool m_scanned = false;
    bool m_showInfo = true;
    bool m_showWarnings = true;
    bool m_showCritical = true;
};
