#include "CollisionAnalyzerPanel.h"

#include "EditorPanels.h"
#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <utility>

namespace {
constexpr const char* kChannels[] = {
    "Default", "World Static", "World Dynamic", "Player", "Enemy",
    "Collectible", "Projectile", "Camera Blocker", "Trigger"
};

const char* PhaseName(int phase) {
    if (phase == 0) return "Enter";
    if (phase == 1) return "Stay";
    return "Exit";
}

const char* ChannelName(std::uint32_t layer) {
    for (int i = 0; i < 9; ++i) if (layer == (1u << i)) return kChannels[i];
    return layer == 0 ? "None" : "Custom / multiple";
}

int ChannelIndex(std::uint32_t layer) {
    for (int i = 0; i < 9; ++i) if (layer == (1u << i)) return i;
    return -1;
}

bool ContainsInsensitive(const std::string& value, const char* filter) {
    if (!filter || !*filter) return true;
    std::string needle(filter), haystack(value);
    std::transform(needle.begin(), needle.end(), needle.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::transform(haystack.begin(), haystack.end(), haystack.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return haystack.find(needle) != std::string::npos;
}
}

void CollisionAnalyzerPanel::Record(ContactEvent event) {
    event.sequence = m_nextSequence++;
    m_events.push_back(std::move(event));
    constexpr std::size_t kMaxEvents = 512;
    if (m_events.size() > kMaxEvents)
        m_events.erase(m_events.begin(), m_events.begin() + (m_events.size() - kMaxEvents));
}

void CollisionAnalyzerPanel::ClearEvents() { m_events.clear(); }

bool CollisionAnalyzerPanel::PairAllowed(const ColliderRow& a, const ColliderRow& b,
                                         bool matrixEnabled, bool matrixAllows) {
    const bool masksAllow = (a.mask & b.layer) != 0u && (b.mask & a.layer) != 0u;
    return masksAllow && (!matrixEnabled || matrixAllows);
}

CollisionAnalyzerPanel::Result CollisionAnalyzerPanel::Draw(const Snapshot& snapshot, bool* open) {
    Result result;
    if (!ImGui::Begin(EditorPanels::Name(EditorPanels::Panel::CollisionAnalyzer), open)) {
        ImGui::End(); return result;
    }

    ImGui::Text("%s | %zu colliders | %d candidate pairs | %d manifolds",
        snapshot.playMode ? "PLAY - live contacts" : "EDIT - authored setup",
        snapshot.colliders.size(), snapshot.candidatePairs, snapshot.manifolds);
    ImGui::TextDisabled("Broad phase: %s, %d occupied cells | deepest penetration %.4f m",
        snapshot.broadphaseValid ? "valid" : "not built", snapshot.occupiedGridCells,
        snapshot.deepestPenetration);
    if (!snapshot.playMode)
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f),
            "Enter Play to capture contacts, overlaps, impulses, and penetration.");

    if (ImGui::Checkbox("Scene contact guides##collision_analyzer", &m_showSceneGuides)) {}
    ImGui::SameLine(); ImGui::Checkbox("Selected object only##collision_analyzer", &m_selectedOnly);
    ImGui::SameLine(); ImGui::Checkbox("Triggers only##collision_analyzer", &m_triggersOnly);
    ImGui::SameLine(); ImGui::Checkbox("Enter/Exit only##collision_analyzer", &m_enterExitOnly);
    if (ImGui::Button("Clear captured events##collision_analyzer")) {
        ClearEvents(); result.clearGuides = true;
    }
    ImGui::SameLine(); ImGui::Checkbox("Follow newest##collision_analyzer", &m_follow);

    if (ImGui::BeginTabBar("##collision_analyzer_tabs")) {
        if (ImGui::BeginTabItem("Contacts & Overlaps")) {
            ImGui::SetNextItemWidth(220.0f);
            ImGui::InputTextWithHint("##collision_event_filter", "Filter object name...",
                                     m_filter, sizeof(m_filter));
            ImGui::SameLine();
            const char* phases[] = {"All phases", "Enter", "Stay", "Exit"};
            ImGui::SetNextItemWidth(115.0f);
            ImGui::Combo("##collision_phase", &m_phaseFilter, phases, 4);
            ImGui::SameLine();
            const char* kinds[] = {"All events", "Contacts", "Overlaps"};
            ImGui::SetNextItemWidth(115.0f);
            ImGui::Combo("##collision_kind", &m_kindFilter, kinds, 3);

            if (ImGui::BeginTable("##collision_events", 7,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable
                    | ImGuiTableFlags_ScrollY, ImVec2(0.0f, 290.0f))) {
                ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 45.0f);
                ImGui::TableSetupColumn("Phase", ImGuiTableColumnFlags_WidthFixed, 60.0f);
                ImGui::TableSetupColumn("Pair", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 65.0f);
                ImGui::TableSetupColumn("Penetration", ImGuiTableColumnFlags_WidthFixed, 85.0f);
                ImGui::TableSetupColumn("Impulse", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                ImGui::TableSetupColumn("Point / normal", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();
                for (const ContactEvent& event : m_events) {
                    if (m_phaseFilter > 0 && event.phase != m_phaseFilter - 1) continue;
                    if (m_kindFilter == 1 && event.trigger) continue;
                    if (m_kindFilter == 2 && !event.trigger) continue;
                    if (!ContainsInsensitive(event.objectA, m_filter)
                        && !ContainsInsensitive(event.objectB, m_filter)) continue;
                    ImGui::PushID(static_cast<int>(event.sequence));
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::Text("%llu", static_cast<unsigned long long>(event.sequence));
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(PhaseName(event.phase));
                    ImGui::TableNextColumn(); ImGui::Text("%s <-> %s", event.objectA.c_str(), event.objectB.c_str());
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(event.trigger ? "Overlap" : "Contact");
                    ImGui::TableNextColumn(); ImGui::Text("%.4f m", event.penetration);
                    ImGui::TableNextColumn(); ImGui::Text("%.3f", event.impulse);
                    ImGui::TableNextColumn();
                    if (event.phase == 2 || event.trigger) ImGui::TextDisabled("not applicable");
                    else ImGui::Text("P %.2f %.2f %.2f | N %.2f %.2f %.2f",
                        event.point.x, event.point.y, event.point.z,
                        event.normal.x, event.normal.y, event.normal.z);
                    ImGui::PopID();
                }
                if (m_follow && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f)
                    ImGui::SetScrollHereY(1.0f);
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Collider Ownership")) {
            if (snapshot.colliders.empty()) ImGui::TextDisabled("No collider components in this scene.");
            if (ImGui::BeginTable("##collider_owners", 8,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable
                    | ImGuiTableFlags_ScrollY, ImVec2(0.0f, 340.0f))) {
                ImGui::TableSetupColumn("Object", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Shape"); ImGui::TableSetupColumn("Channel");
                ImGui::TableSetupColumn("Mask"); ImGui::TableSetupColumn("Body");
                ImGui::TableSetupColumn("Island"); ImGui::TableSetupColumn("Contacts");
                ImGui::TableSetupColumn("Flags"); ImGui::TableHeadersRow();
                for (const ColliderRow& row : snapshot.colliders) {
                    ImGui::PushID(static_cast<int>(row.entity)); ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(row.name.c_str());
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(row.shape.c_str());
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(ChannelName(row.layer));
                    ImGui::TableNextColumn(); ImGui::Text("0x%08X", row.mask);
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(row.rigidBody ? (row.kinematic ? "Kinematic" : "Dynamic") : "Static");
                    ImGui::TableNextColumn(); ImGui::Text("%d", row.island);
                    ImGui::TableNextColumn(); ImGui::Text("%d", row.activeContacts);
                    ImGui::TableNextColumn(); ImGui::Text("%s%s", row.trigger ? "Trigger " : "", row.sleeping ? "Sleeping" : "");
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Response Diagnostic")) {
            auto pairCombo = [&](const char* label, int* index) {
                const char* preview = (*index >= 0 && *index < static_cast<int>(snapshot.colliders.size()))
                    ? snapshot.colliders[static_cast<std::size_t>(*index)].name.c_str() : "Choose collider...";
                if (ImGui::BeginCombo(label, preview)) {
                    for (int i = 0; i < static_cast<int>(snapshot.colliders.size()); ++i) {
                        ImGui::PushID(i); const bool selected = i == *index;
                        if (ImGui::Selectable(snapshot.colliders[static_cast<std::size_t>(i)].name.c_str(), selected)) *index = i;
                        if (selected) ImGui::SetItemDefaultFocus(); ImGui::PopID();
                    }
                    ImGui::EndCombo();
                }
            };
            pairCombo("Collider A##collision_pair", &m_pairA);
            pairCombo("Collider B##collision_pair", &m_pairB);
            if (m_pairA >= 0 && m_pairB >= 0 && m_pairA < static_cast<int>(snapshot.colliders.size())
                && m_pairB < static_cast<int>(snapshot.colliders.size())) {
                const ColliderRow& a = snapshot.colliders[static_cast<std::size_t>(m_pairA)];
                const ColliderRow& b = snapshot.colliders[static_cast<std::size_t>(m_pairB)];
                const bool aResponds = (a.mask & b.layer) != 0u;
                const bool bResponds = (b.mask & a.layer) != 0u;
                const int ai = ChannelIndex(a.layer), bi = ChannelIndex(b.layer);
                const bool matrixAllows = ai < 0 || bi < 0 || snapshot.layerMatrix[ai][bi];
                const bool allowed = PairAllowed(a, b, snapshot.layerMatrixActive, matrixAllows);
                ImGui::TextColored(allowed ? ImVec4(0.25f, 0.95f, 0.4f, 1.0f)
                                           : ImVec4(1.0f, 0.3f, 0.18f, 1.0f),
                    "%s", allowed ? (a.trigger || b.trigger ? "OVERLAP events are allowed" : "BLOCKING contact is allowed")
                                  : "This pair is filtered out");
                ImGui::BulletText("A mask %s B channel", aResponds ? "includes" : "excludes");
                ImGui::BulletText("B mask %s A channel", bResponds ? "includes" : "excludes");
                if (snapshot.layerMatrixActive)
                    ImGui::BulletText("Global channel matrix %s this pair", matrixAllows ? "allows" : "blocks");
            }
            ImGui::SeparatorText("Global channel matrix");
            ImGui::TextDisabled("Read-only here; per-collider masks and the optional world matrix must both allow a pair.");
            if (ImGui::BeginTable("##collision_matrix", 10, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                ImGui::TableNextRow(); ImGui::TableNextColumn();
                for (int x = 0; x < 9; ++x) { ImGui::TableNextColumn(); ImGui::Text("%d", x + 1); if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", kChannels[x]); }
                for (int y = 0; y < 9; ++y) {
                    ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::Text("%d", y + 1); if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", kChannels[y]);
                    for (int x = 0; x < 9; ++x) { ImGui::TableNextColumn(); ImGui::TextUnformatted(snapshot.layerMatrix[y][x] ? "Yes" : "No"); }
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    result.showSceneGuides = m_showSceneGuides;
    result.selectedOnly = m_selectedOnly;
    result.triggersOnly = m_triggersOnly;
    result.enterExitOnly = m_enterExitOnly;
    ImGui::End();
    return result;
}
