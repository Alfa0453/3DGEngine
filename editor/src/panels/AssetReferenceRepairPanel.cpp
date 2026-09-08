#include "AssetReferenceRepairPanel.h"
#include "EditorPanels.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace { std::string Lower(std::string value) { std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); }); return value; } }

AssetReferenceRepairPanel::Result AssetReferenceRepairPanel::Draw(engine::AssetRegistry& registry, const std::string& root, bool* open) {
    Result result; if (!ImGui::Begin(EditorPanels::Name(EditorPanels::Panel::AssetReferenceRepair), open)) { ImGui::End(); return result; }
    if (!m_scanned) { m_repair.Scan(registry, root); m_scanned = true; }
    if (ImGui::Button("Scan Project")) { m_repair.Scan(registry, root); m_scanned = true; m_confirmApply = false; } ImGui::SameLine();
    if (ImGui::Button("Select Repairable")) for (auto& f : m_repair.Findings()) f.selected = f.repairable; ImGui::SameLine();
    if (ImGui::Button("Clear Selection")) for (auto& f : m_repair.Findings()) f.selected = false;
    ImGui::TextWrapped("%s", m_repair.LastScanSummary().c_str());
    ImGui::InputTextWithHint("##AssetRepairSearch", "Search owner, old value, replacement, or message", m_search.data(), m_search.size()); ImGui::SameLine();
    const char* kinds[] = {"All issue types", "Missing path", "Mismatched ID", "Moved asset", "Missing dependency", "Missing import source"}; ImGui::SetNextItemWidth(190); ImGui::Combo("##AssetRepairKind", &m_kindFilter, kinds, 6); ImGui::SameLine(); ImGui::Checkbox("Repairable only", &m_repairableOnly);
    ImGui::Separator(); int selected = 0; for (const auto& f : m_repair.Findings()) if (f.selected && f.repairable) ++selected;
    if (ImGui::BeginTable("AssetRepairFindings", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY, {0, 430})) {
        ImGui::TableSetupColumn("Apply", ImGuiTableColumnFlags_WidthFixed, 48); ImGui::TableSetupColumn("Issue", ImGuiTableColumnFlags_WidthFixed, 120); ImGui::TableSetupColumn("Owner"); ImGui::TableSetupColumn("Current"); ImGui::TableSetupColumn("Replacement"); ImGui::TableSetupColumn("Details"); ImGui::TableHeadersRow();
        const std::string query = Lower(m_search.data());
        for (std::size_t i = 0; i < m_repair.Findings().size(); ++i) { auto& f = m_repair.Findings()[i]; if (m_kindFilter && static_cast<int>(f.kind) != m_kindFilter - 1) continue; if (m_repairableOnly && !f.repairable) continue; const std::string hay = Lower(f.ownerPath + " " + f.oldValue + " " + f.replacement + " " + f.message); if (!query.empty() && hay.find(query) == std::string::npos) continue;
            ImGui::PushID(static_cast<int>(i)); ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); if (f.repairable) ImGui::Checkbox("##Apply", &f.selected); else ImGui::TextDisabled("--"); ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(AssetReferenceRepair::KindName(f.kind)); ImGui::TableSetColumnIndex(2); const std::string owner = std::filesystem::path(f.ownerPath).filename().string(); if (ImGui::Selectable((owner + "##Owner").c_str())) result.revealPath = f.ownerPath; if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", f.ownerPath.c_str()); ImGui::TableSetColumnIndex(3); ImGui::TextWrapped("%s", f.oldValue.c_str()); ImGui::TableSetColumnIndex(4); ImGui::TextWrapped("%s", f.replacement.empty() ? "(manual repair required)" : f.replacement.c_str()); ImGui::TableSetColumnIndex(5); ImGui::TextWrapped("%s", f.message.c_str()); ImGui::PopID(); }
        ImGui::EndTable();
    }
    ImGui::Text("%d selected repair(s)", selected); ImGui::SameLine();
    if (ImGui::Button("Preview Selected")) m_confirmApply = selected > 0;
    if (m_confirmApply) { ImGui::SeparatorText("Apply Preview"); ImGui::TextWrapped("The selected repairs will update authored assets and the registry. Original files are copied to Saved/AssetReferenceRepair before writing."); if (ImGui::Button("Apply Selected Repairs")) { auto applied = m_repair.Apply(registry, root); if (!applied.error.empty()) result.message = applied.error; else { result.registryChanged = true; result.message = "Repaired " + std::to_string(applied.repaired) + " reference(s) in " + std::to_string(applied.filesChanged) + " file(s). Backup: " + applied.backupRoot + " Report: " + applied.reportPath; m_repair.Scan(registry, root); } m_confirmApply = false; } ImGui::SameLine(); if (ImGui::Button("Cancel")) m_confirmApply = false; }
    ImGui::End(); return result;
}
