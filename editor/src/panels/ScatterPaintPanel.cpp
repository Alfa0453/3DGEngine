#include "ScatterPaintPanel.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>

namespace {

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

} // namespace

void ScatterPaintPanel::Refresh(const std::string& assetRoot) {
    const std::string selectedPath = SelectedAsset() ? SelectedAsset()->path : std::string();
    m_assets.clear();
    m_scannedRoot = assetRoot;
    std::error_code ec;
    const std::filesystem::path root(assetRoot);
    if (!std::filesystem::is_directory(root, ec)) return;

    for (std::filesystem::recursive_directory_iterator it(
             root, std::filesystem::directory_options::skip_permission_denied, ec), end;
         it != end; it.increment(ec)) {
        if (ec || !it->is_regular_file(ec)
            || Lower(it->path().extension().string()) != ".3dgmesh") continue;
        AssetChoice asset;
        asset.path = it->path().string();
        asset.name = it->path().stem().string();
        asset.relativePath = std::filesystem::relative(it->path(), root, ec).generic_string();
        if (ec) { ec.clear(); asset.relativePath = it->path().filename().string(); }
        m_assets.push_back(std::move(asset));
    }
    std::sort(m_assets.begin(), m_assets.end(), [](const AssetChoice& a,
                                                   const AssetChoice& b) {
        return Lower(a.relativePath) < Lower(b.relativePath);
    });
    m_selected = -1;
    for (int i = 0; i < static_cast<int>(m_assets.size()); ++i) {
        if (m_assets[static_cast<std::size_t>(i)].path == selectedPath) {
            m_selected = i;
            break;
        }
    }
}

const ScatterPaintPanel::AssetChoice* ScatterPaintPanel::SelectedAsset() const {
    if (m_selected < 0 || m_selected >= static_cast<int>(m_assets.size())) return nullptr;
    return &m_assets[static_cast<std::size_t>(m_selected)];
}

bool ScatterPaintPanel::SlopeAllowed(const glm::vec3& normal) const {
    if (glm::dot(normal, normal) < 1.0e-8f) return true;
    const float up = glm::clamp(glm::dot(glm::normalize(normal), glm::vec3(0, 1, 0)),
                                -1.0f, 1.0f);
    return glm::degrees(std::acos(up)) <= m_maxSlopeDegrees;
}

std::vector<ScatterPaintPanel::StampPoint> ScatterPaintPanel::MakeStamp(
    const glm::vec3& center, const glm::vec3& surfaceNormal) {
    std::vector<StampPoint> result;
    const glm::vec3 normal = glm::dot(surfaceNormal, surfaceNormal) > 1.0e-8f
        ? glm::normalize(surfaceNormal) : glm::vec3(0, 1, 0);
    if (!SlopeAllowed(normal)) return result;

    const float area = 3.14159265359f * m_radius * m_radius;
    const int count = std::clamp(static_cast<int>(std::round(area * m_density)), 1, 128);
    const glm::vec3 helper = std::abs(normal.y) < 0.95f
        ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    const glm::vec3 tangent = glm::normalize(glm::cross(helper, normal));
    const glm::vec3 bitangent = glm::normalize(glm::cross(normal, tangent));
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    std::uniform_real_distribution<float> yaw(0.0f, 6.28318530718f);
    std::uniform_real_distribution<float> scale(m_scaleMin, m_scaleMax);

    glm::quat align(1.0f, 0.0f, 0.0f, 0.0f);
    if (m_alignToNormal) {
        const glm::vec3 up(0, 1, 0);
        const float cosine = glm::clamp(glm::dot(up, normal), -1.0f, 1.0f);
        if (cosine < -0.9999f) align = glm::angleAxis(3.14159265359f, glm::vec3(1, 0, 0));
        else if (cosine < 0.9999f) {
            const glm::vec3 axis = glm::normalize(glm::cross(up, normal));
            align = glm::angleAxis(std::acos(cosine), axis);
        }
    }
    const int maxAttempts = count * 12;
    for (int attempt = 0; attempt < maxAttempts
         && static_cast<int>(result.size()) < count; ++attempt) {
        const float angle = yaw(m_random);
        const float distance = m_radius * std::sqrt(unit(m_random));
        StampPoint point;
        point.position = center + tangent * (std::cos(angle) * distance)
            + bitangent * (std::sin(angle) * distance) + normal * m_heightOffset;
        bool spaced = true;
        for (const StampPoint& existing : result) {
            if (glm::distance(existing.position, point.position) < m_minimumSpacing) {
                spaced = false;
                break;
            }
        }
        if (!spaced) continue;
        const float randomYaw = m_randomYaw ? yaw(m_random) : 0.0f;
        point.rotation = align * glm::angleAxis(randomYaw, glm::vec3(0, 1, 0));
        point.uniformScale = m_randomScale ? scale(m_random) : 1.0f;
        result.push_back(point);
    }
    return result;
}

ScatterPaintPanel::Result ScatterPaintPanel::Draw(const std::string& assetRoot,
                                                   bool* open) {
    Result result;
    if (m_scannedRoot != assetRoot) Refresh(assetRoot);
    if (!ImGui::Begin("Scatter & Paint", open)) { ImGui::End(); return result; }

    if (ImGui::Button("Refresh Meshes")) Refresh(assetRoot);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##ScatterSearch", "Search static meshes...",
                             m_filter.data(), m_filter.size());
    const float listHeight = std::clamp(ImGui::GetContentRegionAvail().y * 0.35f,
                                        130.0f, 300.0f);
    if (ImGui::BeginChild("##ScatterMeshes", ImVec2(0, listHeight), true)) {
        const std::string filter = Lower(m_filter.data());
        for (int i = 0; i < static_cast<int>(m_assets.size()); ++i) {
            const AssetChoice& asset = m_assets[static_cast<std::size_t>(i)];
            if (!filter.empty() && Lower(asset.relativePath).find(filter) == std::string::npos)
                continue;
            ImGui::PushID(i);
            if (ImGui::Selectable(asset.name.c_str(), i == m_selected)) m_selected = i;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", asset.relativePath.c_str());
            ImGui::PopID();
        }
        if (m_assets.empty()) ImGui::TextDisabled("No engine-owned static meshes found.");
    }
    ImGui::EndChild();

    ImGui::SeparatorText("Brush");
    if (ImGui::RadioButton("Paint", m_mode == Mode::Paint)) m_mode = Mode::Paint;
    ImGui::SameLine();
    if (ImGui::RadioButton("Erase", m_mode == Mode::Erase)) m_mode = Mode::Erase;
    if (m_mode == Mode::Paint && !SelectedAsset()) ImGui::BeginDisabled();
    ImGui::Checkbox("Active in Viewport", &m_active);
    if (m_mode == Mode::Paint && !SelectedAsset()) ImGui::EndDisabled();
    ImGui::DragFloat("Radius", &m_radius, 0.05f, 0.1f, 100.0f, "%.2f m");
    ImGui::DragFloat("Stroke Spacing", &m_strokeSpacing, 0.05f, 0.05f, 100.0f, "%.2f m");
    m_radius = std::clamp(m_radius, 0.1f, 100.0f);
    m_strokeSpacing = std::clamp(m_strokeSpacing, 0.05f, 100.0f);

    if (m_mode == Mode::Paint) {
        ImGui::DragFloat("Density", &m_density, 0.05f, 0.02f, 30.0f, "%.2f / m2");
        ImGui::DragFloat("Minimum Spacing", &m_minimumSpacing,
                         0.02f, 0.0f, 100.0f, "%.2f m");
        ImGui::DragFloat("Maximum Slope", &m_maxSlopeDegrees,
                         1.0f, 0.0f, 180.0f, "%.0f deg");
        m_density = std::clamp(m_density, 0.02f, 30.0f);
        m_minimumSpacing = std::clamp(m_minimumSpacing, 0.0f, 100.0f);
        m_maxSlopeDegrees = std::clamp(m_maxSlopeDegrees, 0.0f, 180.0f);
        ImGui::Checkbox("Align to Surface", &m_alignToNormal);
        ImGui::Checkbox("Random Yaw", &m_randomYaw);
        ImGui::Checkbox("Random Scale", &m_randomScale);
        if (m_randomScale) {
            ImGui::DragFloatRange2("Scale Range", &m_scaleMin, &m_scaleMax,
                                   0.01f, 0.05f, 10.0f, "%.2f", "%.2f");
            m_scaleMin = std::clamp(m_scaleMin, 0.05f, 10.0f);
            m_scaleMax = std::clamp(m_scaleMax, m_scaleMin, 10.0f);
        }
        ImGui::Checkbox("Keep Outside Surface", &m_keepOutsideSurface);
        ImGui::DragFloat("Height Offset", &m_heightOffset,
                         0.01f, -100.0f, 100.0f, "%.2f m");
    }

    ImGui::Separator();
    if (ImGui::Button("Clear All Painted Objects", ImVec2(-1, 0)))
        result.clearAllRequested = true;
    if (BrushActive()) {
        ImGui::TextColored(m_mode == Mode::Paint
                ? ImVec4(0.25f, 0.9f, 0.45f, 1.0f)
                : ImVec4(1.0f, 0.35f, 0.25f, 1.0f),
            m_mode == Mode::Paint
                ? "Hold left mouse in the Viewport to paint"
                : "Hold left mouse in the Viewport to erase");
    }
    ImGui::TextDisabled("Only objects created by this tool are erased.");
    ImGui::End();
    return result;
}
