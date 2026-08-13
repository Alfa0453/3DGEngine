#include "LightingAnalysisPanel.h"

#include "EditorPanels.h"
#include "EditorScene.h"

#include <engine/assets/RuntimeAssetManager.h>
#include <engine/assets/MaterialAssetLoader.h>
#include <engine/ecs/Components.h>
#include <imgui.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>

namespace {

editor::lighting::LightType ToAnalysisType(engine::ecs::Light::Type type) {
    using A = editor::lighting::LightType;
    switch (type) {
    case engine::ecs::Light::Type::Directional: return A::Directional;
    case engine::ecs::Light::Type::Point: return A::Point;
    case engine::ecs::Light::Type::Spot: return A::Spot;
    case engine::ecs::Light::Type::Area: return A::Area;
    }
    return A::Point;
}

const char* SeverityName(editor::lighting::Severity severity) {
    switch (severity) {
    case editor::lighting::Severity::Info: return "Info";
    case editor::lighting::Severity::Warning: return "Warning";
    case editor::lighting::Severity::Critical: return "Critical";
    }
    return "Info";
}

ImVec4 SeverityColor(editor::lighting::Severity severity) {
    switch (severity) {
    case editor::lighting::Severity::Info: return {0.35f, 0.72f, 1.0f, 1.0f};
    case editor::lighting::Severity::Warning: return {1.0f, 0.72f, 0.18f, 1.0f};
    case editor::lighting::Severity::Critical: return {1.0f, 0.28f, 0.22f, 1.0f};
    }
    return {1, 1, 1, 1};
}

bool ShadowEnabled(const EditorScene::Environment& environment,
                   engine::ecs::Light::Type type) {
    switch (type) {
    case engine::ecs::Light::Type::Directional: return environment.directionalShadows;
    case engine::ecs::Light::Type::Point: return environment.pointShadows;
    case engine::ecs::Light::Type::Spot: return environment.spotShadows;
    case engine::ecs::Light::Type::Area: return false;
    }
    return false;
}

} // namespace

float LightingAnalysisPanel::CellSize() const {
    return std::max((m_settings.boundsMax.x - m_settings.boundsMin.x)
        / std::max(1, m_settings.gridResolution), 0.05f);
}

void LightingAnalysisPanel::Scan(const EditorScene& scene,
                                 engine::RuntimeAssetManager& assets) {
    const auto& objects = scene.Objects();
    glm::vec2 boundsMin(std::numeric_limits<float>::max());
    glm::vec2 boundsMax(-std::numeric_limits<float>::max());
    float lowestY = std::numeric_limits<float>::max();
    std::vector<editor::lighting::LightInput> lights;
    const auto& environment = scene.GetEnvironment();

    for (int index = 0; index < static_cast<int>(objects.size()); ++index) {
        const EditorScene::Object& object = objects[static_cast<std::size_t>(index)];
        if (!object.visible) continue;
        const auto* transform = scene.TryGetTransform(object.entity);
        if (!transform) continue;
        const glm::vec2 xz(transform->position.x, transform->position.z);
        const float extent = std::max({std::abs(transform->scale.x),
            std::abs(transform->scale.z), 0.5f});
        boundsMin = glm::min(boundsMin, xz - glm::vec2(extent));
        boundsMax = glm::max(boundsMax, xz + glm::vec2(extent));
        lowestY = std::min(lowestY, transform->position.y);
        if (!object.light) continue;
        const engine::ecs::Light* light = scene.TryGetLight(object.entity);
        if (!light) light = &object.lightData;
        lights.push_back({object.name, index, ToAnalysisType(light->type),
            transform->position, light->direction, light->color, light->intensity,
            std::max(light->range, light->sourceRadius * 2.0f), light->outerAngle,
            ShadowEnabled(environment, light->type)});
    }
    if (boundsMin.x > boundsMax.x) {
        boundsMin = glm::vec2(-20.0f);
        boundsMax = glm::vec2(20.0f);
        lowestY = 0.0f;
    }
    const glm::vec2 padding = glm::max((boundsMax - boundsMin) * 0.05f,
                                       glm::vec2(1.0f));
    m_settings.boundsMin = boundsMin - padding;
    m_settings.boundsMax = boundsMax + padding;
    m_settings.sampleHeight = lowestY + 0.08f;
    const float ambient = std::max(0.0f, environment.skyLightIntensity)
        * std::max(environment.minimumSkylight, 0.02f);
    m_report = editor::lighting::Analyze(lights, m_settings, ambient);

    if (environment.directionalShadows && environment.shadowDistance > 250.0f) {
        m_report.findings.push_back({editor::lighting::Severity::Warning, -1,
            "World Settings", "Cascade pressure",
            "Directional shadows cover " + std::to_string(
                static_cast<int>(environment.shadowDistance))
                + " world units, spreading cascade resolution across a large range.",
            "Use the shortest shadow visibility distance that still supports gameplay, then inspect the Shadow Coverage view."});
    }
    if (environment.shadowSoftness > 5.0f && m_report.shadowLightCount > 0) {
        m_report.findings.push_back({editor::lighting::Severity::Info, -1,
            "World Settings", "Shadow filtering",
            "High shadow softness increases filtering pressure on every shadow map.",
            "Reduce softness where crisp shadows are acceptable or reserve it for cinematic presets."});
    }

    for (int index = 0; index < static_cast<int>(objects.size()); ++index) {
        const EditorScene::Object& object = objects[static_cast<std::size_t>(index)];
        if (!object.visible || object.materialAssetPath.empty()) continue;
        std::string error;
        const engine::RuntimeMaterialAsset* material =
            assets.LoadMaterial(object.materialAssetPath, &error);
        if (!material) continue;
        if (material->material.blendMode == engine::ecs::PbrMaterial::BlendMode::Transparent) {
            m_report.findings.push_back({editor::lighting::Severity::Warning, index,
                object.name, "Transparency cost",
                "Transparent material requires ordered blending and increases overdraw.",
                "Keep transparent surfaces small, reduce layers, or use Masked blending."});
        } else if (material->material.blendMode == engine::ecs::PbrMaterial::BlendMode::Masked
                   && material->material.opacity < 0.5f) {
            m_report.findings.push_back({editor::lighting::Severity::Info, index,
                object.name, "Masked overdraw",
                "A heavily cut-out masked material may waste fragment shading.",
                "Tighten mesh silhouettes and use mip-safe opacity masks."});
        }
        if (material->material.transmission > 0.25f) {
            m_report.findings.push_back({editor::lighting::Severity::Warning, index,
                object.name, "Transmission cost",
                "Material transmission adds a costly transparent surface path.",
                "Reserve transmission for important glass and avoid overlapping shells."});
        }
    }
    std::stable_sort(m_report.findings.begin(), m_report.findings.end(),
        [](const auto& a, const auto& b) {
            return static_cast<int>(a.severity) > static_cast<int>(b.severity);
        });
    m_scanned = true;
    m_selectedFinding = -1;
    BuildOverlay(scene, assets);
    std::ostringstream summary;
    summary << m_report.lightCount << " lights | " << m_report.shadowLightCount
            << " shadow lights | max overlap " << m_report.maximumOverlap
            << " | " << m_report.findings.size() << " findings";
    m_status = summary.str();
}

void LightingAnalysisPanel::BuildOverlay(const EditorScene& scene,
                                         engine::RuntimeAssetManager& assets) {
    m_overlayCells.clear();
    if (!m_scanned || m_mode == ViewMode::Disabled) return;
    if (m_mode == ViewMode::TransparencyCost) {
        for (const auto& finding : m_report.findings) {
            if (finding.category != "Transparency cost"
                && finding.category != "Transmission cost"
                && finding.category != "Masked overdraw") continue;
            if (finding.objectIndex < 0
                || finding.objectIndex >= static_cast<int>(scene.Objects().size())) continue;
            const auto* transform = scene.TryGetTransform(
                scene.Objects()[static_cast<std::size_t>(finding.objectIndex)].entity);
            if (transform) m_overlayCells.push_back({transform->position, 1.0f,
                finding.severity != editor::lighting::Severity::Info});
        }
        (void)assets;
        return;
    }
    for (const auto& cell : m_report.cells) {
        OverlayCell overlay;
        overlay.position = cell.position;
        switch (m_mode) {
        case ViewMode::LightComplexity:
            overlay.value = static_cast<float>(cell.lightOverlap)
                / std::max(1, m_settings.overlapCritical);
            overlay.warning = cell.lightOverlap >= m_settings.overlapWarning;
            break;
        case ViewMode::ShadowCoverage:
            overlay.value = static_cast<float>(cell.shadowOverlap)
                / std::max(1, m_settings.shadowWarning);
            overlay.warning = cell.shadowOverlap >= m_settings.shadowWarning;
            break;
        case ViewMode::Exposure:
            overlay.value = cell.illuminance / std::max(0.01f,
                                                       m_settings.overexposedThreshold);
            overlay.warning = cell.illuminance > m_settings.overexposedThreshold;
            break;
        case ViewMode::UnlitAreas:
            overlay.value = cell.illuminance < m_settings.unlitThreshold ? 1.0f : 0.0f;
            overlay.warning = overlay.value > 0.5f;
            break;
        default: break;
        }
        if (overlay.value > 0.001f || m_mode == ViewMode::Exposure)
            m_overlayCells.push_back(overlay);
    }
}

bool LightingAnalysisPanel::ExportReport(const std::string& assetRoot,
                                         std::string* path,
                                         std::string* error) const {
    const std::filesystem::path output = std::filesystem::path(assetRoot)
        / "Reports" / "LightingAnalysis.txt";
    std::error_code ec;
    std::filesystem::create_directories(output.parent_path(), ec);
    std::ofstream stream(output);
    if (ec || !stream) {
        if (error) *error = "Could not create the lighting report.";
        return false;
    }
    stream << "3DG Lighting Analysis\n" << m_status << "\n\n"
           << "Illuminance min/avg/max: " << std::fixed << std::setprecision(3)
           << m_report.minimumIlluminance << " / " << m_report.averageIlluminance
           << " / " << m_report.maximumIlluminance << "\n"
           << "Unlit samples: " << m_report.unlitCellCount << "\n"
           << "Overexposed samples: " << m_report.overexposedCellCount << "\n\n";
    for (const auto& finding : m_report.findings) {
        stream << '[' << SeverityName(finding.severity) << "] " << finding.category
               << " - " << finding.objectName << "\n  " << finding.message
               << "\n  Recommendation: " << finding.recommendation << "\n\n";
    }
    if (!stream) {
        if (error) *error = "Could not finish the lighting report.";
        return false;
    }
    if (path) *path = output.string();
    return true;
}

LightingAnalysisPanel::Result LightingAnalysisPanel::Draw(
    const EditorScene& scene, engine::RuntimeAssetManager& assets,
    const std::string& assetRoot, bool* open) {
    Result result;
    if (!ImGui::Begin(EditorPanels::Name(EditorPanels::Panel::LightingAnalysis), open)) {
        ImGui::End(); return result;
    }
    if (ImGui::Button("Analyze Level")) Scan(scene, assets);
    ImGui::SameLine();
    if (ImGui::Button("Export Report")) {
        std::string path, error;
        m_status = ExportReport(assetRoot, &path, &error) ? "Saved " + path : error;
    }
    ImGui::SameLine(); ImGui::Checkbox("Viewport Overlay", &m_overlayEnabled);
    const char* modes[] = {"Disabled", "Light Complexity", "Shadow Coverage",
        "Exposure", "Unlit Areas", "Transparency Cost"};
    int mode = static_cast<int>(m_mode);
    if (ImGui::Combo("View Mode", &mode, modes, IM_ARRAYSIZE(modes))) {
        m_mode = static_cast<ViewMode>(mode);
        BuildOverlay(scene, assets);
    }
    if (ImGui::CollapsingHeader("Analysis Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool changed = false;
        changed |= ImGui::SliderInt("Grid Resolution", &m_settings.gridResolution, 8, 64);
        changed |= ImGui::DragFloat("Unlit Threshold", &m_settings.unlitThreshold,
                                    0.01f, 0.0f, 4.0f, "%.2f");
        changed |= ImGui::DragFloat("Overexposed Threshold",
                                    &m_settings.overexposedThreshold,
                                    0.05f, 0.1f, 32.0f, "%.2f");
        changed |= ImGui::SliderInt("Overlap Warning", &m_settings.overlapWarning, 1, 12);
        changed |= ImGui::SliderInt("Overlap Critical", &m_settings.overlapCritical, 2, 16);
        changed |= ImGui::SliderInt("Shadow Warning", &m_settings.shadowWarning, 1, 8);
        if (changed && m_scanned) Scan(scene, assets);
    }
    ImGui::TextWrapped("%s", m_status.c_str());
    if (m_scanned) {
        const int cells = static_cast<int>(m_report.cells.size());
        ImGui::SeparatorText("Coverage");
        ImGui::Text("Unlit: %d / %d", m_report.unlitCellCount, cells);
        ImGui::SameLine(); ImGui::Text("Overexposed: %d / %d",
                                      m_report.overexposedCellCount, cells);
        ImGui::Text("Illuminance  min %.2f   avg %.2f   max %.2f",
                    m_report.minimumIlluminance, m_report.averageIlluminance,
                    m_report.maximumIlluminance);
        ImGui::Text("Maximum overlap: %d lights / %d shadow casters",
                    m_report.maximumOverlap, m_report.maximumShadowOverlap);
    }
    ImGui::SeparatorText("Findings");
    if (m_report.findings.empty()) ImGui::TextDisabled("No findings yet.");
    for (int i = 0; i < static_cast<int>(m_report.findings.size()); ++i) {
        const auto& finding = m_report.findings[static_cast<std::size_t>(i)];
        ImGui::PushID(i);
        ImGui::PushStyleColor(ImGuiCol_Text, SeverityColor(finding.severity));
        const std::string label = std::string("[") + SeverityName(finding.severity)
            + "] " + finding.category + " - " + finding.objectName;
        const bool selected = ImGui::Selectable(label.c_str(), m_selectedFinding == i);
        ImGui::PopStyleColor();
        if (selected) {
            m_selectedFinding = i;
            if (finding.objectIndex >= 0) result.frameObject = finding.objectIndex;
        }
        if (m_selectedFinding == i) {
            ImGui::Indent();
            ImGui::TextWrapped("%s", finding.message.c_str());
            ImGui::TextDisabled("Recommendation");
            ImGui::TextWrapped("%s", finding.recommendation.c_str());
            ImGui::Unindent();
        }
        ImGui::PopID();
    }
    ImGui::End();
    return result;
}
