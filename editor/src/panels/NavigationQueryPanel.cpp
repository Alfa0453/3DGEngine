#include "NavigationQueryPanel.h"

#include "EditorPanels.h"
#include <engine/ai/AStar.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <queue>

namespace {
float DistanceXZ(const glm::vec3& a, const glm::vec3& b) {
    const glm::vec2 d(a.x - b.x, a.z - b.z);
    return glm::length(d);
}

bool SegmentHitsExpandedBox(const glm::vec3& a, const glm::vec3& b,
                            const NavigationQueryPanel::Obstacle& obstacle,
                            float radius, glm::vec3* hit) {
    const glm::vec2 mn(obstacle.center.x - obstacle.halfExtents.x - radius,
                       obstacle.center.z - obstacle.halfExtents.z - radius);
    const glm::vec2 mx(obstacle.center.x + obstacle.halfExtents.x + radius,
                       obstacle.center.z + obstacle.halfExtents.z + radius);
    const glm::vec2 p(a.x, a.z), d(b.x - a.x, b.z - a.z);
    float lo = 0.0f, hi = 1.0f;
    for (int axis = 0; axis < 2; ++axis) {
        if (std::abs(d[axis]) < 1.0e-6f) {
            if (p[axis] < mn[axis] || p[axis] > mx[axis]) return false;
            continue;
        }
        float t0 = (mn[axis] - p[axis]) / d[axis];
        float t1 = (mx[axis] - p[axis]) / d[axis];
        if (t0 > t1) std::swap(t0, t1);
        lo = std::max(lo, t0); hi = std::min(hi, t1);
        if (lo > hi) return false;
    }
    if (hit) *hit = glm::mix(a, b, lo);
    return true;
}
}

void NavigationQueryPanel::AnalyzeMesh(const engine::ai::NavMesh& mesh) {
    const int count = static_cast<int>(mesh.polys.size());
    std::vector<unsigned char> visited(static_cast<std::size_t>(count), 0);
    m_regions = 0; m_portals = 0;
    for (int a = 0; a < count; ++a) {
        for (int b = a + 1; b < count; ++b) if (mesh.Adjacent(a, b)) ++m_portals;
        if (visited[static_cast<std::size_t>(a)]) continue;
        ++m_regions; visited[static_cast<std::size_t>(a)] = 1;
        std::queue<int> pending; pending.push(a);
        while (!pending.empty()) {
            const int current = pending.front(); pending.pop();
            for (int other = 0; other < count; ++other) {
                if (!visited[static_cast<std::size_t>(other)] && mesh.Adjacent(current, other)) {
                    visited[static_cast<std::size_t>(other)] = 1; pending.push(other);
                }
            }
        }
    }
}

void NavigationQueryPanel::RunQuery(const Snapshot& snapshot) {
    m_hasQuery = true; m_success = false; m_path.clear(); m_obstacleHits.clear();
    m_pathLength = 0.0f; m_pathCost = 0.0f; m_usedTestLink = false;
    m_startPoly = -1; m_goalPoly = -1;
    m_startOnMesh = false; m_goalOnMesh = false; m_regions = 0; m_portals = 0;

    if (m_source == 0) {
        if (!snapshot.mesh || snapshot.mesh->polys.empty()) {
            m_status = "No baked navmesh. Rebuild the navigation preview first."; return;
        }
        AnalyzeMesh(*snapshot.mesh);
        m_startPoly = snapshot.mesh->PolyAt(m_start);
        m_goalPoly = snapshot.mesh->PolyAt(m_goal);
        m_startOnMesh = m_startPoly >= 0; m_goalOnMesh = m_goalPoly >= 0;
        m_success = snapshot.mesh->FindPath(m_start, m_goal, m_path);
        if (!m_success && m_testLinkEnabled) {
            auto throughLink = [&](const glm::vec3& linkStart, const glm::vec3& linkEnd,
                                   std::vector<glm::vec3>* result) {
                std::vector<glm::vec3> first, second;
                if (!snapshot.mesh->FindPath(m_start, linkStart, first)
                    || !snapshot.mesh->FindPath(linkEnd, m_goal, second)) return false;
                *result = std::move(first);
                if (result->empty() || DistanceXZ(result->back(), linkStart) > 0.001f)
                    result->push_back(linkStart);
                result->push_back(linkEnd);
                if (!second.empty()) result->insert(result->end(), second.begin() + 1, second.end());
                return true;
            };
            m_success = throughLink(m_testLinkStart, m_testLinkEnd, &m_path);
            if (!m_success && m_testLinkBidirectional)
                m_success = throughLink(m_testLinkEnd, m_testLinkStart, &m_path);
            m_usedTestLink = m_success;
        }
    } else {
        if (!snapshot.grid || snapshot.grid->width <= 0 || snapshot.grid->height <= 0) {
            m_status = snapshot.playMode ? "No runtime navigation grid is available."
                                         : "The grid query source is available in Play mode.";
            return;
        }
        const glm::ivec2 startCell = snapshot.grid->WorldToCell(m_start);
        const glm::ivec2 goalCell = snapshot.grid->WorldToCell(m_goal);
        m_startOnMesh = snapshot.grid->Walkable(startCell.x, startCell.y);
        m_goalOnMesh = snapshot.grid->Walkable(goalCell.x, goalCell.y);
        engine::ai::AStar::FindPathWorld(*snapshot.grid, m_start, m_goal, m_path, m_allowDiagonal);
        m_success = !m_path.empty();
    }
    for (std::size_t i = 1; i < m_path.size(); ++i)
        m_pathLength += DistanceXZ(m_path[i - 1], m_path[i]);
    m_pathCost = m_pathLength * m_costPerMeter + (m_usedTestLink ? m_testLinkCost : 0.0f);
    for (const Obstacle& obstacle : snapshot.obstacles) {
        if (!obstacle.dynamic) continue;
        for (std::size_t i = 1; i < m_path.size(); ++i) {
            glm::vec3 hit;
            if (SegmentHitsExpandedBox(m_path[i - 1], m_path[i], obstacle, m_agentRadius, &hit)) {
                m_obstacleHits.push_back(hit); break;
            }
        }
    }
    if (!m_success) m_status = "Unreachable: no route connects the selected endpoints.";
    else if (!m_obstacleHits.empty()) m_status = "Path found, but a live dynamic obstacle currently intersects it.";
    else if (!m_startOnMesh || !m_goalOnMesh) m_status = "Path found after snapping one or both endpoints to the nearest polygon.";
    else m_status = "Path found.";
}

NavigationQueryPanel::Result NavigationQueryPanel::Draw(const Snapshot& snapshot, bool* open) {
    Result result;
    if (!ImGui::Begin(EditorPanels::Name(EditorPanels::Panel::NavigationQuery), open)) {
        ImGui::End(); return result;
    }
    ImGui::Text("%s navigation query", snapshot.playMode ? "Runtime" : "Editor");
    const char* sources[] = {"NavMesh (funnel path)", "NavGrid (A*)"};
    ImGui::SetNextItemWidth(220.0f); ImGui::Combo("Query source##nav_query", &m_source, sources, 2);
    if (!snapshot.playMode && m_source == 1)
        ImGui::TextDisabled("NavGrid queries require Play mode; editor preview uses NavMesh.");

    ImGui::DragFloat3("Start##nav_query", &m_start.x, 0.1f, -100000.0f, 100000.0f, "%.2f");
    if (ImGui::Button("Start from selected##nav_query") && snapshot.hasSelectedPosition) m_start = snapshot.selectedPosition;
    ImGui::SameLine(); if (ImGui::Button("Start from camera##nav_query")) m_start = snapshot.cameraPosition;
    if (!snapshot.hasSelectedPosition && ImGui::IsItemHovered()) ImGui::SetTooltip("No scene object is selected.");
    ImGui::DragFloat3("Goal##nav_query", &m_goal.x, 0.1f, -100000.0f, 100000.0f, "%.2f");
    if (ImGui::Button("Goal from selected##nav_query") && snapshot.hasSelectedPosition) m_goal = snapshot.selectedPosition;
    ImGui::SameLine(); if (ImGui::Button("Goal from camera##nav_query")) m_goal = snapshot.cameraPosition;

    ImGui::SeparatorText("Agent and build test");
    ImGui::SetNextItemWidth(150.0f); ImGui::DragFloat("Agent radius##nav_query", &m_agentRadius, 0.02f, 0.05f, 20.0f, "%.2f m");
    ImGui::SetNextItemWidth(150.0f); ImGui::DragFloat("Agent height##nav_query", &m_agentHeight, 0.05f, 0.1f, 50.0f, "%.2f m");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Recorded for clearance diagnosis. The current navigation bake is XZ-only and does not voxelize ceilings yet.");
    ImGui::SetNextItemWidth(150.0f); ImGui::DragFloat("Cell size##nav_query", &m_cellSize, 0.02f, 0.05f, 10.0f, "%.2f m");
    if (m_source == 1) ImGui::Checkbox("Allow diagonal movement##nav_query", &m_allowDiagonal);
    ImGui::SetNextItemWidth(150.0f); ImGui::DragFloat("Cost per meter##nav_query", &m_costPerMeter, 0.05f, 0.0f, 10000.0f, "%.2f");
    if (ImGui::TreeNode("Test off-mesh link##nav_query")) {
        ImGui::Checkbox("Enabled##nav_test_link", &m_testLinkEnabled);
        ImGui::Checkbox("Bidirectional##nav_test_link", &m_testLinkBidirectional);
        ImGui::DragFloat3("Link start##nav_test_link", &m_testLinkStart.x, 0.1f, -100000.0f, 100000.0f, "%.2f");
        ImGui::DragFloat3("Link end##nav_test_link", &m_testLinkEnd.x, 0.1f, -100000.0f, 100000.0f, "%.2f");
        ImGui::SetNextItemWidth(150.0f); ImGui::DragFloat("Traversal cost##nav_test_link", &m_testLinkCost, 0.05f, 0.0f, 100000.0f, "%.2f");
        ImGui::TextDisabled("Query-only link: use it to verify a proposed jump, ladder, or teleport connection before authoring runtime support.");
        ImGui::TreePop();
    }
    if (ImGui::Button("Rebuild for agent##nav_query")) result.rebuildRequested = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rebakes the editor navigation mesh with this radius and cell size.");
    ImGui::SameLine(); if (ImGui::Button("Run query##nav_query")) RunQuery(snapshot);
    ImGui::SameLine(); ImGui::Checkbox("Show navigation surface##nav_query", &m_showNavigationSurface);

    ImGui::SeparatorText("Result");
    ImGui::TextWrapped("%s", m_status.c_str());
    if (m_hasQuery) {
        const ImVec4 good(0.25f, 0.95f, 0.4f, 1.0f), bad(1.0f, 0.3f, 0.18f, 1.0f);
        ImGui::TextColored(m_success ? good : bad, "%s", m_success ? "REACHABLE" : "UNREACHABLE");
        ImGui::Text("Waypoints: %zu | Length: %.2f m | Cost: %.2f%s",
            m_path.size(), m_pathLength, m_pathCost, m_usedTestLink ? " | used test link" : "");
        ImGui::Text("Start: %s | Goal: %s", m_startOnMesh ? "on navigation" : "off navigation",
                    m_goalOnMesh ? "on navigation" : "off navigation");
        if (m_source == 0)
            ImGui::Text("Start polygon: %d | Goal polygon: %d | Regions: %d | Portals: %d",
                m_startPoly, m_goalPoly, m_regions, m_portals);
        ImGui::Text("Dynamic obstacle intersections: %zu", m_obstacleHits.size());
        if (m_regions > 1)
            ImGui::TextColored(bad, "%d disconnected navigation regions detected.", m_regions);
    }
    ImGui::SeparatorText("Capability notes");
    ImGui::BulletText("Polygon portals are tested as part of every NavMesh route.");
    ImGui::BulletText("Dynamic obstacles are checked against the returned route and highlighted red.");
    ImGui::BulletText("Proposed jump/ladder/teleport links can be tested, but are query-only until a runtime link asset is authored.");
    ImGui::BulletText("Vertical ceiling clearance is not yet represented by this 2D bake.");

    result.showNavigationSurface = m_showNavigationSurface;
    ImGui::End();
    return result;
}
