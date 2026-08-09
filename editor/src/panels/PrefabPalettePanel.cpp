#include "PrefabPalettePanel.h"

#include "EditorPanels.h"
#include "PrefabAsset.h"

#include <engine/assets/StaticMeshAsset.h>
#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <unordered_map>

namespace {

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string VariantGroup(std::string name) {
    while (!name.empty() && std::isdigit(static_cast<unsigned char>(name.back())))
        name.pop_back();
    if (name.size() >= 2 && name[name.size() - 2] == '_'
        && std::isalpha(static_cast<unsigned char>(name.back()))) {
        name.resize(name.size() - 2);
    }
    while (!name.empty() && (name.back() == '_' || name.back() == '-' || name.back() == ' '))
        name.pop_back();
    return Lower(name);
}

std::filesystem::path ResolveModelPath(const std::string& assetRoot,
                                       const std::string& modelPath) {
    std::filesystem::path path(modelPath);
    if (path.is_absolute()) return path;
    std::error_code ec;
    std::filesystem::path candidate = std::filesystem::path(assetRoot) / path;
    if (std::filesystem::is_regular_file(candidate, ec)) return candidate;
    candidate = std::filesystem::path(assetRoot).parent_path() / path;
    return candidate;
}

} // namespace

void PrefabPalettePanel::LoadFavorites() {
    m_favorites.clear();
    std::ifstream in(std::filesystem::path(m_assetRoot).parent_path()
                     / "Saved" / "Editor" / "PrefabFavorites.txt");
    std::string line;
    while (std::getline(in, line)) if (!line.empty()) m_favorites.insert(line);
}

void PrefabPalettePanel::SaveFavorites() const {
    const std::filesystem::path path = std::filesystem::path(m_assetRoot).parent_path()
        / "Saved" / "Editor" / "PrefabFavorites.txt";
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::trunc);
    std::vector<std::string> ordered(m_favorites.begin(), m_favorites.end());
    std::sort(ordered.begin(), ordered.end());
    for (const std::string& favorite : ordered) out << favorite << '\n';
}

void PrefabPalettePanel::BuildPreview(Entry& entry, const std::string& modelPath) const {
    entry.preview.clear();
    if (modelPath.empty()) return;
    engine::StaticMeshAssetData mesh;
    if (!engine::LoadStaticMeshAsset(
            ResolveModelPath(m_assetRoot, modelPath).string(), &mesh, nullptr)) return;

    const glm::vec3 minimum(mesh.minimum[0], mesh.minimum[1], mesh.minimum[2]);
    const glm::vec3 maximum(mesh.maximum[0], mesh.maximum[1], mesh.maximum[2]);
    const glm::vec3 center = (minimum + maximum) * 0.5f;
    const glm::vec3 extent = glm::max(maximum - minimum, glm::vec3(0.001f));
    const float fit = 1.75f / std::max({extent.x, extent.y, extent.z});
    auto project = [&](const glm::vec3& p) {
        const glm::vec3 q = p - center;
        return glm::vec2((q.x - q.z * 0.45f) * fit,
                         (-q.y + q.z * 0.22f) * fit);
    };
    constexpr std::size_t maxTriangles = 900;
    std::size_t totalTriangles = 0;
    for (const auto& sub : mesh.subMeshes) totalTriangles += sub.indices.size() / 3;
    const std::size_t step = std::max<std::size_t>(1, totalTriangles / maxTriangles);
    std::size_t triangle = 0;
    for (const auto& sub : mesh.subMeshes) {
        const std::size_t vertexCount = sub.vertices.size() / engine::kStaticMeshVertexStride;
        for (std::size_t i = 0; i + 2 < sub.indices.size(); i += 3, ++triangle) {
            if (triangle % step != 0) continue;
            const std::uint32_t ia = sub.indices[i], ib = sub.indices[i + 1], ic = sub.indices[i + 2];
            if (ia >= vertexCount || ib >= vertexCount || ic >= vertexCount) continue;
            auto position = [&](std::uint32_t index) {
                const std::size_t o = static_cast<std::size_t>(index)
                    * engine::kStaticMeshVertexStride;
                return glm::vec3(sub.vertices[o], sub.vertices[o + 1], sub.vertices[o + 2]);
            };
            const glm::vec2 a = project(position(ia));
            const glm::vec2 b = project(position(ib));
            const glm::vec2 c = project(position(ic));
            entry.preview.push_back({a, b});
            entry.preview.push_back({b, c});
            entry.preview.push_back({c, a});
        }
    }
}

void PrefabPalettePanel::Refresh(const std::string& assetRoot) {
    const std::string selectedPath = Selected() ? Selected()->path : std::string();
    m_assetRoot = assetRoot;
    LoadFavorites();
    m_entries.clear();
    m_categories.clear();
    m_categories.push_back("All");
    std::error_code ec;
    const std::filesystem::path root(assetRoot);
    for (std::filesystem::recursive_directory_iterator it(
             root, std::filesystem::directory_options::skip_permission_denied, ec), end;
         it != end; it.increment(ec)) {
        if (ec || !it->is_regular_file(ec)
            || Lower(it->path().extension().string()) != ".3dgprefab") continue;
        Entry entry;
        entry.path = it->path().string();
        entry.relativePath = std::filesystem::relative(it->path(), root, ec).generic_string();
        if (ec) { ec.clear(); entry.relativePath = it->path().filename().string(); }
        entry.name = it->path().stem().string();
        const std::filesystem::path relative(entry.relativePath);
        entry.category = relative.has_parent_path()
            ? relative.parent_path().generic_string() : "Root";
        if (entry.category.empty() || entry.category == ".") entry.category = "Root";
        entry.variantGroup = VariantGroup(entry.name);
        PrefabAsset prefab;
        if (prefab.Load(entry.path, nullptr)) BuildPreview(entry, prefab.object.modelAssetPath);
        m_entries.push_back(std::move(entry));
    }
    std::sort(m_entries.begin(), m_entries.end(), [](const Entry& a, const Entry& b) {
        if (a.category != b.category) return Lower(a.category) < Lower(b.category);
        return Lower(a.name) < Lower(b.name);
    });
    for (const Entry& entry : m_entries) m_categories.push_back(entry.category);
    std::sort(m_categories.begin() + 1, m_categories.end());
    m_categories.erase(std::unique(m_categories.begin(), m_categories.end()), m_categories.end());
    m_selected = -1;
    for (int i = 0; i < static_cast<int>(m_entries.size()); ++i)
        if (m_entries[static_cast<std::size_t>(i)].path == selectedPath) m_selected = i;
    if (m_selected < 0) m_placementActive = false;
}

const PrefabPalettePanel::Entry* PrefabPalettePanel::Selected() const {
    if (m_selected < 0 || m_selected >= static_cast<int>(m_entries.size())) return nullptr;
    return &m_entries[static_cast<std::size_t>(m_selected)];
}

std::vector<int> PrefabPalettePanel::VisibleEntries() const {
    std::vector<int> result;
    const std::string filter = Lower(m_filter.data());
    for (int i = 0; i < static_cast<int>(m_entries.size()); ++i) {
        const Entry& entry = m_entries[static_cast<std::size_t>(i)];
        if (m_category != "All" && entry.category != m_category) continue;
        if (m_favoritesOnly && m_favorites.count(entry.relativePath) == 0) continue;
        if (!filter.empty() && Lower(entry.name + " " + entry.relativePath).find(filter)
            == std::string::npos) continue;
        result.push_back(i);
    }
    return result;
}

std::vector<int> PrefabPalettePanel::VariantEntries(int sourceIndex) const {
    std::vector<int> variants;
    if (sourceIndex < 0 || sourceIndex >= static_cast<int>(m_entries.size())) return variants;
    const Entry& source = m_entries[static_cast<std::size_t>(sourceIndex)];
    for (int i = 0; i < static_cast<int>(m_entries.size()); ++i) {
        const Entry& candidate = m_entries[static_cast<std::size_t>(i)];
        if (candidate.category == source.category
            && candidate.variantGroup == source.variantGroup) variants.push_back(i);
    }
    return variants;
}

PrefabPalettePanel::Placement PrefabPalettePanel::NextPlacement() {
    Placement placement;
    if (!Selected()) return placement;
    int index = m_selected;
    if (m_randomVariant) {
        const std::vector<int> variants = VariantEntries(index);
        if (!variants.empty()) {
            std::uniform_int_distribution<std::size_t> choose(0, variants.size() - 1);
            index = variants[choose(m_random)];
        }
    }
    const Entry& entry = m_entries[static_cast<std::size_t>(index)];
    placement.path = entry.path;
    placement.name = entry.name;
    placement.yawDegrees = m_yaw;
    if (m_randomYaw) {
        const int steps = std::max(1, static_cast<int>(std::round(360.0f / m_yawStep)));
        std::uniform_int_distribution<int> choose(0, steps - 1);
        placement.yawDegrees += static_cast<float>(choose(m_random)) * m_yawStep;
    }
    placement.uniformScale = 1.0f;
    if (m_randomScale) {
        std::uniform_real_distribution<float> choose(m_scaleMin, m_scaleMax);
        placement.uniformScale = choose(m_random);
    }
    return placement;
}

glm::vec3 PrefabPalettePanel::SnapPosition(const glm::vec3& position) const {
    const float step = std::max(m_gridSize, 0.01f);
    return glm::round(position / step) * step;
}

PrefabPalettePanel::Result PrefabPalettePanel::Draw(const std::string& assetRoot,
                                                     bool* open) {
    Result result;
    if (m_assetRoot != assetRoot) Refresh(assetRoot);
    if (!ImGui::Begin(EditorPanels::Name(EditorPanels::Panel::PrefabPalette), open)) { ImGui::End(); return result; }

    if (ImGui::Button("Refresh")) Refresh(assetRoot);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##PrefabSearch", "Search prefabs...",
                             m_filter.data(), m_filter.size());
    ImGui::SetNextItemWidth(180.0f);
    if (ImGui::BeginCombo("Category", m_category.c_str())) {
        for (const std::string& category : m_categories) {
            if (ImGui::Selectable(category.c_str(), category == m_category))
                m_category = category;
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Favorites only", &m_favoritesOnly);

    const std::vector<int> visible = VisibleEntries();
    const float cardWidth = 138.0f, cardHeight = 126.0f;
    const int columns = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / (cardWidth + 8.0f)));
    const float paletteHeight = std::clamp(ImGui::GetContentRegionAvail().y * 0.55f,
                                           210.0f, 500.0f);
    if (ImGui::BeginChild("##PrefabCards", ImVec2(0.0f, paletteHeight), true)) {
        for (int item = 0; item < static_cast<int>(visible.size()); ++item) {
            const int index = visible[static_cast<std::size_t>(item)];
            Entry& entry = m_entries[static_cast<std::size_t>(index)];
            ImGui::PushID(index);
            const ImVec2 top = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##Card", ImVec2(cardWidth, cardHeight));
            if (ImGui::IsItemClicked()) m_selected = index;
            if (ImGui::IsItemHovered()
                && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                m_selected = index;
                m_placementActive = true;
            }
            ImDrawList* draw = ImGui::GetWindowDrawList();
            const bool selected = index == m_selected;
            draw->AddRectFilled(top, ImVec2(top.x + cardWidth, top.y + cardHeight),
                selected ? IM_COL32(43, 76, 112, 255) : IM_COL32(28, 34, 43, 255), 4.0f);
            draw->AddRect(top, ImVec2(top.x + cardWidth, top.y + cardHeight),
                selected ? IM_COL32(80, 165, 245, 255) : IM_COL32(67, 76, 88, 255), 4.0f);
            const ImVec2 center(top.x + cardWidth * 0.5f, top.y + 48.0f);
            if (!entry.preview.empty()) {
                for (const PreviewLine& line : entry.preview) {
                    draw->AddLine(ImVec2(center.x + line.a.x * 40.0f, center.y + line.a.y * 40.0f),
                                  ImVec2(center.x + line.b.x * 40.0f, center.y + line.b.y * 40.0f),
                                  IM_COL32(139, 186, 222, 145));
                }
            } else {
                draw->AddRect(ImVec2(center.x - 26, center.y - 25),
                              ImVec2(center.x + 26, center.y + 25),
                              IM_COL32(139, 186, 222, 180), 3.0f);
                draw->AddLine(ImVec2(center.x - 26, center.y - 25),
                              ImVec2(center.x + 26, center.y + 25), IM_COL32(139, 186, 222, 120));
            }
            const std::string display = entry.name.size() > 19
                ? entry.name.substr(0, 18) + "..." : entry.name;
            draw->AddText(ImVec2(top.x + 7, top.y + 94), IM_COL32(225, 230, 237, 255),
                          display.c_str());
            const std::vector<int> variants = VariantEntries(index);
            if (variants.size() > 1) {
                const std::string count = std::to_string(variants.size()) + " variants";
                draw->AddText(ImVec2(top.x + 7, top.y + 109), IM_COL32(142, 156, 174, 255),
                              count.c_str());
            }
            const bool favorite = m_favorites.count(entry.relativePath) != 0;
            draw->AddText(ImVec2(top.x + cardWidth - 21, top.y + 6),
                          favorite ? IM_COL32(255, 205, 65, 255) : IM_COL32(105, 115, 128, 255),
                          favorite ? "*" : "+");
            if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                if (favorite) m_favorites.erase(entry.relativePath);
                else m_favorites.insert(entry.relativePath);
                SaveFavorites();
            }
            ImGui::PopID();
            if ((item + 1) % columns != 0) ImGui::SameLine();
        }
        if (visible.empty()) ImGui::TextDisabled("No prefabs match the current filters.");
    }
    ImGui::EndChild();
    ImGui::TextDisabled("Click: select | Double-click: place | Right-click: favorite");

    ImGui::SeparatorText("Placement Variations");
    ImGui::Checkbox("Random Variant", &m_randomVariant);
    ImGui::Checkbox("Random Yaw", &m_randomYaw); ImGui::SameLine();
    ImGui::SetNextItemWidth(100.0f);
    ImGui::DragFloat("Step##PrefabYaw", &m_yawStep, 1.0f, 1.0f, 180.0f, "%.0f deg");
    m_yawStep = std::clamp(m_yawStep, 1.0f, 180.0f);
    ImGui::DragFloat("Base Yaw", &m_yaw, 1.0f, -360.0f, 360.0f, "%.0f deg");
    ImGui::Checkbox("Random Scale", &m_randomScale);
    if (m_randomScale) {
        ImGui::DragFloatRange2("Scale Range", &m_scaleMin, &m_scaleMax,
                               0.01f, 0.05f, 10.0f, "%.2f", "%.2f");
        m_scaleMin = std::clamp(m_scaleMin, 0.05f, 10.0f);
        m_scaleMax = std::clamp(m_scaleMax, m_scaleMin, 10.0f);
    }
    ImGui::Checkbox("Snap to Surfaces", &m_surfaceSnap);
    ImGui::Checkbox("Keep Outside Surface", &m_offsetByBounds);
    ImGui::DragFloat("Grid Size", &m_gridSize, 0.05f, 0.01f, 100.0f, "%.2f");
    m_gridSize = std::clamp(m_gridSize, 0.01f, 100.0f);

    if (!Selected()) ImGui::BeginDisabled();
    ImGui::Checkbox("Place in Viewport", &m_placementActive);
    if (ImGui::Button("Place In Front", ImVec2(-1, 0))) result.placeInFrontRequested = true;
    if (ImGui::Button("Replace Selected", ImVec2(-1, 0))) result.replaceSelectedRequested = true;
    if (!Selected()) ImGui::EndDisabled();
    if (m_placementActive)
        ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.45f, 1.0f), "Click in the Viewport to place prefabs");

    ImGui::End();
    return result;
}
