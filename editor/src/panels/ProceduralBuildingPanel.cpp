#include "ProceduralBuildingPanel.h"
#include "EditorPanels.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>

namespace {
constexpr float kPi = 3.14159265358979323846f;

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

const char* OpeningName(ProceduralBuildingPanel::OpeningType type) {
    switch (type) {
    case ProceduralBuildingPanel::OpeningType::Door: return "Door";
    case ProceduralBuildingPanel::OpeningType::Window: return "Window";
    case ProceduralBuildingPanel::OpeningType::Arch: return "Arch";
    case ProceduralBuildingPanel::OpeningType::Stairwell: return "Stairwell";
    }
    return "Opening";
}
}

void ProceduralBuildingPanel::NewAsset() {
    m_path.clear();
    m_assetId = engine::AssetHandle::Generate();
    m_footprint = {{-4.0f, -3.0f}, {4.0f, -3.0f}, {4.0f, 3.0f}, {-4.0f, 3.0f}};
    m_openings = {{OpeningType::Door, 0, 0, 0.5f, 1.2f, 2.2f, 0.0f}};
    m_storeys = 1;
    m_baseHeight = 0.0f;
    m_storeyHeight = 3.0f;
    m_wallThickness = 0.25f;
    m_floorThickness = 0.2f;
    m_roofThickness = 0.25f;
    m_createFloors = m_createCeilings = m_createRoof = true;
    m_createColumns = false;
    m_dirty = true;
    m_status = "New editable building";
}

void ProceduralBuildingPanel::ApplyPreset(int preset) {
    NewAsset();
    if (preset == 1) {
        std::snprintf(m_name.data(), m_name.size(), "%s", "Cottage");
        m_footprint = {{-4.5f,-3.5f},{4.5f,-3.5f},{4.5f,3.5f},{-4.5f,3.5f}};
        m_storeyHeight = 2.8f;
        m_openings.push_back({OpeningType::Window, 1, 0, .35f, 1.4f, 1.2f, .9f});
        m_openings.push_back({OpeningType::Window, 3, 0, .65f, 1.4f, 1.2f, .9f});
    } else if (preset == 2) {
        std::snprintf(m_name.data(), m_name.size(), "%s", "TownHouse");
        m_footprint = {{-3.5f,-5.0f},{3.5f,-5.0f},{3.5f,5.0f},{-3.5f,5.0f}};
        m_storeys = 2;
        m_createColumns = true;
        m_openings.push_back({OpeningType::Window, 0, 1, .5f, 1.6f, 1.3f, 1.0f});
    } else if (preset == 3) {
        std::snprintf(m_name.data(), m_name.size(), "%s", "Tower");
        m_footprint = {{-3,-3},{3,-3},{3,3},{-3,3}};
        m_storeys = 4;
        m_storeyHeight = 3.2f;
        m_createColumns = true;
    }
}

void ProceduralBuildingPanel::RefreshMaterials(const std::string& assetRoot) {
    m_materialRoot = assetRoot;
    m_materials.clear();
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(
             assetRoot, std::filesystem::directory_options::skip_permission_denied, ec), end;
         it != end; it.increment(ec)) {
        if (ec || !it->is_regular_file(ec)
            || Lower(it->path().extension().string()) != ".3dgmat") continue;
        m_materials.push_back({it->path().string(), it->path().stem().string()});
    }
    std::sort(m_materials.begin(), m_materials.end(), [](const auto& a, const auto& b) {
        return Lower(a.name) < Lower(b.name);
    });
}

bool ProceduralBuildingPanel::ValidFootprint(std::string* error) const {
    if (m_footprint.size() < 3) {
        if (error) *error = "A building needs at least three footprint points.";
        return false;
    }
    float area = 0.0f;
    for (std::size_t i = 0; i < m_footprint.size(); ++i) {
        const glm::vec2& a = m_footprint[i];
        const glm::vec2& b = m_footprint[(i + 1) % m_footprint.size()];
        area += a.x * b.y - b.x * a.y;
        if (glm::length(b - a) < 0.1f) {
            if (error) *error = "Two neighbouring footprint points are too close.";
            return false;
        }
    }
    if (std::abs(area) < 0.1f) {
        if (error) *error = "The footprint has no usable area.";
        return false;
    }
    return true;
}

bool ProceduralBuildingPanel::Save(const std::string& assetRoot, std::string* error) {
    if (!ValidFootprint(error)) return false;
    std::filesystem::path path = m_path;
    if (path.empty()) path = std::filesystem::path(assetRoot) / "GameAssets" / "Buildings"
        / (std::string(m_name.data()) + ".3dgbuilding");
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::trunc);
    if (!out) { if (error) *error = "Could not save building asset: " + path.string(); return false; }
    if (!m_assetId.Valid()) m_assetId = engine::AssetHandle::Generate();
    out << "3DG_BUILDING 1 " << m_assetId.ToString() << "\n";
    out << "name " << std::quoted(std::string(m_name.data())) << "\n";
    out << "settings " << m_storeys << ' ' << m_baseHeight << ' ' << m_storeyHeight << ' '
        << m_wallThickness << ' ' << m_floorThickness << ' ' << m_roofThickness << '\n';
    out << "flags " << m_createFloors << ' ' << m_createCeilings << ' ' << m_createRoof << ' '
        << m_createColumns << ' ' << m_colliders << '\n';
    out << "materials " << std::quoted(m_wallMaterial) << ' ' << std::quoted(m_floorMaterial)
        << ' ' << std::quoted(m_roofMaterial) << '\n';
    out << "points " << m_footprint.size() << '\n';
    for (const glm::vec2& point : m_footprint) out << point.x << ' ' << point.y << '\n';
    out << "openings " << m_openings.size() << '\n';
    for (const Opening& opening : m_openings)
        out << static_cast<int>(opening.type) << ' ' << opening.segment << ' ' << opening.storey
            << ' ' << opening.offset << ' ' << opening.width << ' ' << opening.height << ' '
            << opening.sill << '\n';
    if (!out.good()) { if (error) *error = "Failed while writing building asset."; return false; }
    m_path = path.string();
    m_dirty = false;
    return true;
}

bool ProceduralBuildingPanel::Load(const std::string& path, std::string* error) {
    std::ifstream in(path);
    std::string magic;
    int version = 0;
    std::string idText;
    if (!(in >> magic >> version >> idText) || magic != "3DG_BUILDING" || version != 1
        || !engine::AssetHandle::Parse(idText, &m_assetId)) {
        if (error) *error = "Not a supported 3DG building asset.";
        return false;
    }
    std::string key, name;
    in >> key >> std::quoted(name);
    if (key != "name") return false;
    std::snprintf(m_name.data(), m_name.size(), "%s", name.c_str());
    in >> key >> m_storeys >> m_baseHeight >> m_storeyHeight >> m_wallThickness
       >> m_floorThickness >> m_roofThickness;
    in >> key >> m_createFloors >> m_createCeilings >> m_createRoof >> m_createColumns >> m_colliders;
    in >> key >> std::quoted(m_wallMaterial) >> std::quoted(m_floorMaterial) >> std::quoted(m_roofMaterial);
    std::size_t count = 0;
    in >> key >> count;
    m_footprint.assign(count, {});
    for (glm::vec2& point : m_footprint) in >> point.x >> point.y;
    in >> key >> count;
    m_openings.assign(count, {});
    for (Opening& opening : m_openings) {
        int type = 0;
        in >> type >> opening.segment >> opening.storey >> opening.offset >> opening.width
           >> opening.height >> opening.sill;
        opening.type = static_cast<OpeningType>(std::clamp(type, 0, 3));
    }
    if (!in || !ValidFootprint(error)) return false;
    m_path = path;
    m_dirty = false;
    return true;
}

void ProceduralBuildingPanel::DrawMaterialCombo(const char* label, std::string& value) {
    const std::string current = value.empty() ? "Default" : std::filesystem::path(value).stem().string();
    if (!ImGui::BeginCombo(label, current.c_str())) return;
    if (ImGui::Selectable("Default", value.empty())) { value.clear(); m_dirty = true; }
    for (const MaterialChoice& material : m_materials) {
        if (ImGui::Selectable(material.name.c_str(), material.path == value)) {
            value = material.path;
            m_dirty = true;
        }
    }
    ImGui::EndCombo();
}

void ProceduralBuildingPanel::DrawFootprintPreview() {
    const ImVec2 size(std::max(240.0f, ImGui::GetContentRegionAvail().x), 280.0f);
    ImGui::InvisibleButton("##BuildingFootprint", size, ImGuiButtonFlags_MouseButtonLeft);
    const ImVec2 origin = ImGui::GetItemRectMin();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, {origin.x + size.x, origin.y + size.y}, IM_COL32(22, 27, 34, 255));
    draw->AddRect(origin, {origin.x + size.x, origin.y + size.y}, IM_COL32(70, 85, 105, 255));
    if (m_footprint.empty()) return;
    glm::vec2 minimum(std::numeric_limits<float>::max()), maximum(-std::numeric_limits<float>::max());
    for (const glm::vec2& point : m_footprint) { minimum = glm::min(minimum, point); maximum = glm::max(maximum, point); }
    const glm::vec2 extent = glm::max(maximum - minimum, glm::vec2(1.0f));
    const float scale = std::min((size.x - 50.0f) / extent.x, (size.y - 50.0f) / extent.y);
    const glm::vec2 center = (minimum + maximum) * 0.5f;
    auto screen = [&](const glm::vec2& p) {
        return ImVec2(origin.x + size.x * .5f + (p.x - center.x) * scale,
                      origin.y + size.y * .5f - (p.y - center.y) * scale);
    };
    std::vector<ImVec2> points;
    for (const glm::vec2& point : m_footprint) points.push_back(screen(point));
    draw->AddPolyline(points.data(), static_cast<int>(points.size()), IM_COL32(90, 190, 255, 255),
                      ImDrawFlags_Closed, 3.0f);
    if (points.size() >= 3) draw->AddConvexPolyFilled(points.data(), static_cast<int>(points.size()), IM_COL32(50,110,155,45));

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        m_selectedPoint = -1;
        float best = 13.0f;
        for (int i = 0; i < static_cast<int>(points.size()); ++i) {
            const float dx = points[i].x - mouse.x, dy = points[i].y - mouse.y;
            const float distance = std::sqrt(dx * dx + dy * dy);
            if (distance < best) { best = distance; m_selectedPoint = i; }
        }
    }
    if (m_selectedPoint >= 0 && ImGui::IsMouseDown(ImGuiMouseButton_Left)
        && ImGui::IsItemHovered()) {
        glm::vec2& point = m_footprint[static_cast<std::size_t>(m_selectedPoint)];
        point.x = center.x + (mouse.x - origin.x - size.x * .5f) / scale;
        point.y = center.y - (mouse.y - origin.y - size.y * .5f) / scale;
        point = glm::round(point * 10.0f) / 10.0f;
        m_dirty = true;
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) m_selectedPoint = -1;
    for (int i = 0; i < static_cast<int>(points.size()); ++i) {
        draw->AddCircleFilled(points[i], 6.0f, i == m_selectedPoint
            ? IM_COL32(255,190,50,255) : IM_COL32(100,220,255,255));
        draw->AddText({points[i].x + 8, points[i].y - 9}, IM_COL32_WHITE, std::to_string(i + 1).c_str());
    }
    draw->AddText({origin.x + 8, origin.y + 8}, IM_COL32(190,205,220,255),
                  "Drag numbered corners | footprint uses world X/Z");
}

std::vector<ProceduralBuildingPanel::Part> ProceduralBuildingPanel::GenerateParts() const {
    std::vector<Part> parts;
    if (!ValidFootprint()) return parts;
    glm::vec2 minimum(std::numeric_limits<float>::max()), maximum(-std::numeric_limits<float>::max());
    for (const glm::vec2& point : m_footprint) { minimum = glm::min(minimum, point); maximum = glm::max(maximum, point); }
    const glm::vec2 center = (minimum + maximum) * .5f;
    const glm::vec2 size = glm::max(maximum - minimum, glm::vec2(.1f));
    auto add = [&](std::string suffix, glm::vec3 position, glm::vec3 scale,
                   float yaw, const std::string& material) {
        if (scale.x > .01f && scale.y > .01f && scale.z > .01f)
            parts.push_back({std::move(suffix), position, scale, yaw, material});
    };
    for (int storey = 0; storey < m_storeys; ++storey) {
        const float base = m_baseHeight + storey * m_storeyHeight;
        const Opening* stairwell = nullptr;
        for (const Opening& opening : m_openings)
            if (opening.type == OpeningType::Stairwell && opening.storey == storey) {
                stairwell = &opening; break;
            }
        if (m_createFloors && stairwell) {
            const int segment = std::clamp(stairwell->segment, 0,
                static_cast<int>(m_footprint.size()) - 1);
            const glm::vec2 a = m_footprint[static_cast<std::size_t>(segment)];
            const glm::vec2 b = m_footprint[(static_cast<std::size_t>(segment) + 1) % m_footprint.size()];
            const glm::vec2 edge = glm::normalize(b - a);
            const glm::vec2 holeCenter = a + edge * (glm::length(b - a)
                * std::clamp(stairwell->offset, 0.0f, 1.0f));
            const glm::vec2 holeHalf(std::clamp(stairwell->width * .5f, .2f, size.x * .4f),
                                     std::clamp(stairwell->sill * .5f, .2f, size.y * .4f));
            const float left = std::clamp(holeCenter.x - holeHalf.x, minimum.x, maximum.x);
            const float right = std::clamp(holeCenter.x + holeHalf.x, minimum.x, maximum.x);
            const float nearZ = std::clamp(holeCenter.y - holeHalf.y, minimum.y, maximum.y);
            const float farZ = std::clamp(holeCenter.y + holeHalf.y, minimum.y, maximum.y);
            add("S" + std::to_string(storey + 1) + "_FloorLeft",
                {(minimum.x + left) * .5f, base - m_floorThickness * .5f, center.y},
                {left - minimum.x, m_floorThickness, size.y}, 0, m_floorMaterial);
            add("S" + std::to_string(storey + 1) + "_FloorRight",
                {(right + maximum.x) * .5f, base - m_floorThickness * .5f, center.y},
                {maximum.x - right, m_floorThickness, size.y}, 0, m_floorMaterial);
            add("S" + std::to_string(storey + 1) + "_FloorNear",
                {(left + right) * .5f, base - m_floorThickness * .5f, (minimum.y + nearZ) * .5f},
                {right - left, m_floorThickness, nearZ - minimum.y}, 0, m_floorMaterial);
            add("S" + std::to_string(storey + 1) + "_FloorFar",
                {(left + right) * .5f, base - m_floorThickness * .5f, (farZ + maximum.y) * .5f},
                {right - left, m_floorThickness, maximum.y - farZ}, 0, m_floorMaterial);
        } else if (m_createFloors) add("S" + std::to_string(storey + 1) + "_Floor",
            {center.x, base - m_floorThickness * .5f, center.y},
            {size.x, m_floorThickness, size.y}, 0.0f, m_floorMaterial);
        if (m_createCeilings) add("S" + std::to_string(storey + 1) + "_Ceiling",
            {center.x, base + m_storeyHeight + m_floorThickness * .5f, center.y},
            {size.x, m_floorThickness, size.y}, 0.0f, m_floorMaterial);

        for (int segment = 0; segment < static_cast<int>(m_footprint.size()); ++segment) {
            const glm::vec2 a = m_footprint[static_cast<std::size_t>(segment)];
            const glm::vec2 b = m_footprint[(static_cast<std::size_t>(segment) + 1) % m_footprint.size()];
            const glm::vec2 delta = b - a;
            const float length = glm::length(delta);
            const glm::vec2 direction = delta / length;
            const float yaw = std::atan2(direction.y, direction.x);
            std::vector<const Opening*> openings;
            for (const Opening& opening : m_openings)
                if (opening.type != OpeningType::Stairwell && opening.segment == segment
                    && opening.storey == storey) openings.push_back(&opening);
            std::sort(openings.begin(), openings.end(), [](const Opening* lhs, const Opening* rhs) {
                return lhs->offset < rhs->offset;
            });
            float cursor = 0.0f;
            int piece = 0;
            auto wallPiece = [&](float start, float span, float bottom, float height, const char* label) {
                if (span <= .02f || height <= .02f) return;
                const glm::vec2 p = a + direction * (start + span * .5f);
                add("S" + std::to_string(storey + 1) + "_Wall" + std::to_string(segment + 1)
                        + "_" + label + std::to_string(piece++),
                    {p.x, base + bottom + height * .5f, p.y},
                    {span, height, m_wallThickness}, -yaw, m_wallMaterial);
            };
            for (const Opening* opening : openings) {
                const float width = std::clamp(opening->width, .2f, std::max(.2f, length - .2f));
                const float centerAt = std::clamp(opening->offset, 0.0f, 1.0f) * length;
                const float start = std::clamp(centerAt - width * .5f, cursor, length);
                const float end = std::clamp(start + width, start, length);
                wallPiece(cursor, start - cursor, 0.0f, m_storeyHeight, "Full");
                const float sill = opening->type == OpeningType::Window
                    ? std::clamp(opening->sill, 0.0f, m_storeyHeight - .1f) : 0.0f;
                const float openingHeight = std::clamp(opening->height, .2f, m_storeyHeight - sill);
                if (sill > .01f) wallPiece(start, end - start, 0.0f, sill, "Sill");
                const float top = sill + openingHeight;
                if (top < m_storeyHeight - .01f)
                    wallPiece(start, end - start, top, m_storeyHeight - top, "Lintel");
                cursor = std::max(cursor, end);
            }
            wallPiece(cursor, length - cursor, 0.0f, m_storeyHeight, "Full");
        }
        if (m_createColumns) {
            for (std::size_t i = 0; i < m_footprint.size(); ++i)
                add("S" + std::to_string(storey + 1) + "_Column" + std::to_string(i + 1),
                    {m_footprint[i].x, base + m_storeyHeight * .5f, m_footprint[i].y},
                    {m_wallThickness * 1.5f, m_storeyHeight, m_wallThickness * 1.5f},
                    0.0f, m_wallMaterial);
        }
    }
    if (m_createRoof) add("Roof",
        {center.x, m_baseHeight + m_storeys * m_storeyHeight + m_roofThickness * .5f, center.y},
        {size.x + m_wallThickness, m_roofThickness, size.y + m_wallThickness}, 0.0f, m_roofMaterial);
    return parts;
}

ProceduralBuildingPanel::Result ProceduralBuildingPanel::Draw(
    const std::string& assetRoot, bool* open) {
    Result result;
    if (m_footprint.empty()) NewAsset();
    if (m_materialRoot != assetRoot) RefreshMaterials(assetRoot);
    if (!m_pendingOpen.empty()) {
        std::string error;
        if (Load(m_pendingOpen, &error)) m_status = "Loaded " + m_pendingOpen;
        else m_status = error;
        m_pendingOpen.clear();
    }
    if (!ImGui::Begin(EditorPanels::Name(EditorPanels::Panel::ProceduralBuilding), open)) {
        ImGui::End(); return result;
    }
    if (ImGui::Button("New")) NewAsset();
    ImGui::SameLine();
    if (ImGui::Button("Save")) {
        std::string error;
        if (Save(assetRoot, &error)) { m_status = "Saved " + m_path; result.assetListChanged = true; }
        else m_status = error;
    }
    ImGui::SameLine();
    if (ImGui::Button("Generate / Rebuild in Level")) result.generateRequested = true;
    ImGui::SameLine();
    if (ImGui::Button("Delete Generated")) result.deleteExistingRequested = true;
    ImGui::TextUnformatted(m_path.empty() ? "Unsaved .3dgbuilding asset" : m_path.c_str());
    if (m_dirty) { ImGui::SameLine(); ImGui::TextColored({1,.7f,.2f,1}, "Unsaved *"); }

    if (ImGui::BeginTable("##BuildingLayout", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("Properties", ImGuiTableColumnFlags_WidthFixed, 390.0f);
        ImGui::TableSetupColumn("Footprint Preview", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextColumn();
        if (ImGui::InputText("Name", m_name.data(), m_name.size())) m_dirty = true;
        ImGui::TextUnformatted("Presets:"); ImGui::SameLine();
        if (ImGui::SmallButton("Basic")) ApplyPreset(0); ImGui::SameLine();
        if (ImGui::SmallButton("Cottage")) ApplyPreset(1); ImGui::SameLine();
        if (ImGui::SmallButton("Town House")) ApplyPreset(2); ImGui::SameLine();
        if (ImGui::SmallButton("Tower")) ApplyPreset(3);
        ImGui::SeparatorText("Structure");
        m_dirty |= ImGui::DragInt("Storeys", &m_storeys, 1, 1, 32);
        m_dirty |= ImGui::DragFloat("Base Height", &m_baseHeight, .05f, -100.f, 100.f, "%.2f m");
        m_dirty |= ImGui::DragFloat("Storey Height", &m_storeyHeight, .05f, .5f, 20.f, "%.2f m");
        m_dirty |= ImGui::DragFloat("Wall Thickness", &m_wallThickness, .01f, .05f, 3.f, "%.2f m");
        m_dirty |= ImGui::DragFloat("Floor Thickness", &m_floorThickness, .01f, .05f, 2.f, "%.2f m");
        m_dirty |= ImGui::DragFloat("Roof Thickness", &m_roofThickness, .01f, .05f, 2.f, "%.2f m");
        m_dirty |= ImGui::Checkbox("Floors", &m_createFloors); ImGui::SameLine();
        m_dirty |= ImGui::Checkbox("Ceilings", &m_createCeilings); ImGui::SameLine();
        m_dirty |= ImGui::Checkbox("Flat Roof", &m_createRoof); ImGui::SameLine();
        m_dirty |= ImGui::Checkbox("Columns", &m_createColumns);
        m_dirty |= ImGui::Checkbox("Colliders", &m_colliders); ImGui::SameLine();
        ImGui::Checkbox("Replace Existing", &m_replaceExisting);

        ImGui::SeparatorText("Footprint Points");
        for (int i = 0; i < static_cast<int>(m_footprint.size()); ++i) {
            ImGui::PushID(i);
            float point[2] = {m_footprint[static_cast<std::size_t>(i)].x, m_footprint[static_cast<std::size_t>(i)].y};
            ImGui::SetNextItemWidth(-34.0f);
            if (ImGui::DragFloat2("##Point", point, .05f, -10000.f, 10000.f, "%.2f")) {
                m_footprint[static_cast<std::size_t>(i)] = {point[0], point[1]}; m_dirty = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("X") && m_footprint.size() > 3) {
                m_footprint.erase(m_footprint.begin() + i); m_dirty = true; ImGui::PopID(); break;
            }
            ImGui::PopID();
        }
        if (ImGui::Button("Add Corner")) {
            const glm::vec2 a = m_footprint[m_footprint.size() - 2];
            const glm::vec2 b = m_footprint.back();
            m_footprint.insert(m_footprint.end() - 1, (a + b) * .5f); m_dirty = true;
        }

        ImGui::SeparatorText("Openings");
        for (int i = 0; i < static_cast<int>(m_openings.size()); ++i) {
            Opening& opening = m_openings[static_cast<std::size_t>(i)];
            ImGui::PushID(1000 + i);
            if (ImGui::TreeNodeEx("Opening", ImGuiTreeNodeFlags_DefaultOpen,
                                  "%s %d", OpeningName(opening.type), i + 1)) {
                int type = static_cast<int>(opening.type);
                const char* types[] = {"Door", "Window", "Arch", "Stairwell"};
                if (ImGui::Combo("Type", &type, types, 4)) { opening.type = static_cast<OpeningType>(type); m_dirty = true; }
                m_dirty |= ImGui::DragInt("Wall Segment", &opening.segment, 1, 0,
                    std::max(0, static_cast<int>(m_footprint.size()) - 1));
                m_dirty |= ImGui::DragInt("Storey", &opening.storey, 1, 0, std::max(0, m_storeys - 1));
                m_dirty |= ImGui::SliderFloat("Along Wall", &opening.offset, 0.f, 1.f, "%.2f");
                m_dirty |= ImGui::DragFloat("Width", &opening.width, .05f, .2f, 20.f, "%.2f m");
                if (opening.type != OpeningType::Stairwell)
                    m_dirty |= ImGui::DragFloat("Height", &opening.height, .05f, .2f, 20.f, "%.2f m");
                if (opening.type == OpeningType::Window)
                    m_dirty |= ImGui::DragFloat("Sill", &opening.sill, .05f, 0.f, 10.f, "%.2f m");
                if (opening.type == OpeningType::Stairwell)
                    m_dirty |= ImGui::DragFloat("Opening Depth", &opening.sill, .05f, .4f, 20.f, "%.2f m");
                if (ImGui::Button("Remove Opening")) { m_openings.erase(m_openings.begin() + i); m_dirty = true; ImGui::TreePop(); ImGui::PopID(); break; }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        if (ImGui::Button("Add Door")) { m_openings.push_back({}); m_dirty = true; }
        ImGui::SameLine();
        if (ImGui::Button("Add Window")) { Opening opening; opening.type = OpeningType::Window; m_openings.push_back(opening); m_dirty = true; }
        ImGui::SameLine();
        if (ImGui::Button("Add Stairwell")) { Opening opening; opening.type = OpeningType::Stairwell; opening.width = 2.0f; opening.sill = 3.0f; m_openings.push_back(opening); m_dirty = true; }
        ImGui::SeparatorText("Surface Materials");
        DrawMaterialCombo("Walls", m_wallMaterial);
        DrawMaterialCombo("Floors / Ceilings", m_floorMaterial);
        DrawMaterialCombo("Roof", m_roofMaterial);

        ImGui::TableNextColumn();
        DrawFootprintPreview();
        std::string validation;
        if (!ValidFootprint(&validation)) ImGui::TextColored({1,.35f,.25f,1}, "%s", validation.c_str());
        else ImGui::Text("Preview: %d generated pieces", static_cast<int>(GenerateParts().size()));
        ImGui::TextWrapped("Generation is non-destructive: only objects carrying this building name are replaced. Generated pieces are regular scene objects and can be converted to a prefab or level instance with the existing tools.");
        ImGui::EndTable();
    }
    if (!m_status.empty()) ImGui::TextWrapped("%s", m_status.c_str());
    ImGui::End();
    return result;
}
