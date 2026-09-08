#include "AiPerceptionDebuggerPanel.h"

#include "EditorPanels.h"
#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace {
bool ContainsInsensitive(const std::string& value, const char* filter) {
    if (!filter || !*filter) return true;
    std::string lhs = value, rhs = filter;
    std::transform(lhs.begin(), lhs.end(), lhs.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::transform(rhs.begin(), rhs.end(), rhs.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lhs.find(rhs) != std::string::npos;
}

const char* YesNo(bool value) { return value ? "yes" : "no"; }
}

bool AiPerceptionDebuggerPanel::Visible(const AgentRow& row) const {
    if (m_selectedOnly && m_selectedEntity != std::numeric_limits<std::uint32_t>::max()
        && row.entity != m_selectedEntity) return false;
    if (m_teamFilter >= 0 && row.team != m_teamFilter) return false;
    if (m_onlySensing && !row.seesTarget && !row.heardNoise) return false;
    return ContainsInsensitive(row.name, m_filter)
        || ContainsInsensitive(row.targetName, m_filter);
}

void AiPerceptionDebuggerPanel::Observe(const Snapshot& snapshot) {
    if (!snapshot.playMode || m_freeze) return;
    const double now = ImGui::GetTime();
    for (const AgentRow& row : snapshot.agents) {
        Previous& previous = m_previous[row.entity];
        if (!previous.initialized) {
            previous = {row.targetEntity, row.seesTarget, row.heardNoise, true};
            continue;
        }
        auto add = [&](const std::string& message) {
            m_history.push_back({now, row.entity, row.name, message});
            if (m_history.size() > 512) m_history.erase(m_history.begin(), m_history.begin() + 128);
        };
        if (row.targetEntity != previous.target) {
            add(row.targetValid ? "target acquired: " + row.targetName : "target cleared");
        }
        if (row.seesTarget != previous.sees) {
            add(row.seesTarget ? "sight acquired" : "sight lost");
        }
        if (row.heardNoise && !previous.heard) {
            add(row.heardFromSquad ? "received squad alert" : "heard sound stimulus");
        }
        previous = {row.targetEntity, row.seesTarget, row.heardNoise, true};
    }
}

void AiPerceptionDebuggerPanel::Draw(const Snapshot& snapshot, bool* open) {
    Observe(snapshot);
    if (!ImGui::Begin(EditorPanels::Name(EditorPanels::Panel::AiPerceptionDebugger), open)) {
        ImGui::End(); return;
    }

    if (!snapshot.playMode) {
        ImGui::TextWrapped("Enter Play mode to inspect the perception decisions used by live AI agents.");
        ImGui::End(); return;
    }

    ImGui::Text("Agents: %zu | active sounds: %zu", snapshot.agents.size(), snapshot.activeSounds);
    ImGui::SetNextItemWidth(190.0f);
    ImGui::InputTextWithHint("##ai_perception_filter", "Filter agent or target...", m_filter, sizeof(m_filter));
    ImGui::SameLine();
    if (ImGui::BeginCombo("Team##ai_perception", m_teamFilter < 0 ? "All teams" : std::to_string(m_teamFilter).c_str())) {
        if (ImGui::Selectable("All teams", m_teamFilter < 0)) m_teamFilter = -1;
        for (int team = 0; team <= 8; ++team) {
            const std::string label = "Team " + std::to_string(team);
            if (ImGui::Selectable(label.c_str(), m_teamFilter == team)) m_teamFilter = team;
        }
        ImGui::EndCombo();
    }
    ImGui::Checkbox("Only sensing##ai_perception", &m_onlySensing);
    ImGui::SameLine(); ImGui::Checkbox("Freeze history##ai_perception", &m_freeze);

    ImGui::SeparatorText("Scene visualization");
    ImGui::Checkbox("Show guides##ai_perception", &m_showSceneGuides);
    ImGui::SameLine(); ImGui::Checkbox("Selected only##ai_perception", &m_selectedOnly);
    ImGui::Checkbox("Vision cone and sight ray##ai_perception", &m_showVision);
    ImGui::SameLine(); ImGui::Checkbox("Hearing range##ai_perception", &m_showHearing);
    ImGui::SameLine(); ImGui::Checkbox("Last known position##ai_perception", &m_showLastKnown);

    ImGui::SeparatorText("Live agents");
    if (ImGui::BeginTable("ai_perception_agents", 7,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable
            | ImGuiTableFlags_ScrollY, ImVec2(0.0f, 230.0f))) {
        ImGui::TableSetupColumn("Agent"); ImGui::TableSetupColumn("Team");
        ImGui::TableSetupColumn("State"); ImGui::TableSetupColumn("Target");
        ImGui::TableSetupColumn("Sight"); ImGui::TableSetupColumn("Hearing");
        ImGui::TableSetupColumn("Reason"); ImGui::TableHeadersRow();
        for (const AgentRow& row : snapshot.agents) {
            if (!Visible(row)) continue;
            ImGui::PushID(static_cast<int>(row.entity));
            ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
            if (ImGui::Selectable(row.name.c_str(), m_selectedEntity == row.entity,
                                  ImGuiSelectableFlags_SpanAllColumns)) m_selectedEntity = row.entity;
            ImGui::TableSetColumnIndex(1); ImGui::Text("%d", row.team);
            ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(row.state.c_str());
            ImGui::TableSetColumnIndex(3); ImGui::TextUnformatted(row.targetValid ? row.targetName.c_str() : "None");
            ImGui::TableSetColumnIndex(4);
            ImGui::TextColored(row.seesTarget ? ImVec4(0.3f, 1.0f, 0.4f, 1.0f)
                                               : ImVec4(0.95f, 0.35f, 0.25f, 1.0f),
                               "%s", row.seesTarget ? "Visible" : "Not visible");
            ImGui::TableSetColumnIndex(5);
            ImGui::Text("%s", row.heardNoise ? (row.heardFromSquad ? "Squad" : "Sound") : "None");
            ImGui::TableSetColumnIndex(6);
            const char* reason = !row.targetValid ? "No target"
                : !row.inRange ? "Out of range"
                : !row.inFov ? "Outside FOV"
                : !row.lineOfSight ? "Occluded" : "Detected";
            ImGui::TextUnformatted(reason);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    const AgentRow* selected = nullptr;
    for (const AgentRow& row : snapshot.agents) if (row.entity == m_selectedEntity) { selected = &row; break; }
    if (!selected && !snapshot.agents.empty()) {
        selected = &snapshot.agents.front(); m_selectedEntity = selected->entity;
    }
    if (selected) {
        ImGui::SeparatorText("Selected agent decision");
        ImGui::Text("%s (entity %u) | team %d | %s brain", selected->name.c_str(),
                    selected->entity, selected->team, selected->behaviorGraph ? "behavior graph" : "built-in");
        ImGui::Text("Target: %s | team %d | distance %.2f m | angle %.1f deg",
                    selected->targetValid ? selected->targetName.c_str() : "None",
                    selected->targetTeam, selected->targetDistance, selected->targetAngleDeg);
        ImGui::Text("Range: %s (%.2f m) | FOV: %s (+/- %.1f deg) | clear LOS: %s",
                    YesNo(selected->inRange), selected->visionRange, YesNo(selected->inFov),
                    selected->visionHalfAngleDeg, YesNo(selected->lineOfSight));
        ImGui::Text("Heard: %s | range %.2f m | loudness %.3f%s", YesNo(selected->heardNoise),
                    selected->hearingRange, selected->heardLoudness,
                    selected->heardFromSquad ? " | squad alert" : "");
        if (selected->hasLastKnown)
            ImGui::Text("Last known: (%.2f, %.2f, %.2f)", selected->lastKnownPosition.x,
                        selected->lastKnownPosition.y, selected->lastKnownPosition.z);
        else ImGui::TextDisabled("No last-known target or stimulus position.");
    }

    ImGui::SeparatorText("Perception history");
    if (ImGui::Button("Clear history##ai_perception")) { m_history.clear(); m_previous.clear(); }
    ImGui::SameLine(); ImGui::TextDisabled("Records target, sight, sound, and squad-alert changes while open.");
    if (ImGui::BeginChild("ai_perception_history", ImVec2(0.0f, 150.0f), true)) {
        for (auto it = m_history.rbegin(); it != m_history.rend(); ++it) {
            if (m_selectedOnly && m_selectedEntity != std::numeric_limits<std::uint32_t>::max()
                && it->entity != m_selectedEntity) continue;
            ImGui::Text("%7.2f  %s: %s", it->time, it->agent.c_str(), it->message.c_str());
        }
    }
    ImGui::EndChild();
    ImGui::End();
}
