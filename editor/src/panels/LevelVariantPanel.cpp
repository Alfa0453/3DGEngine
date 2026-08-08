#include "LevelVariantPanel.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>

std::string LevelVariantPanel::SanitizeName(std::string name) {
    for (char& c : name) {
        const unsigned char value = static_cast<unsigned char>(c);
        if (!std::isalnum(value) && c != '_' && c != '-') c = '_';
    }
    while (!name.empty() && name.front() == '_') name.erase(name.begin());
    while (!name.empty() && name.back() == '_') name.pop_back();
    return name;
}

std::string LevelVariantPanel::TargetPath(const std::string& assetRoot) const {
    const std::string name = SanitizeName(m_name.data());
    if (name.empty()) return {};
    return (std::filesystem::path(assetRoot) / "LevelVariants"
            / (name + ".3dgvariant")).string();
}

void LevelVariantPanel::Refresh(const std::string& assetRoot) {
    const std::string selectedPath = m_selected >= 0
        && m_selected < static_cast<int>(m_variants.size())
        ? m_variants[static_cast<std::size_t>(m_selected)].path : std::string();
    m_scannedRoot = assetRoot;
    m_variants.clear();
    std::error_code ec;
    const std::filesystem::path folder = std::filesystem::path(assetRoot) / "LevelVariants";
    std::filesystem::create_directories(folder, ec);
    for (std::filesystem::directory_iterator it(
             folder, std::filesystem::directory_options::skip_permission_denied, ec), end;
         it != end; it.increment(ec)) {
        if (ec || !it->is_regular_file(ec)
            || it->path().extension() != ".3dgvariant") continue;
        Variant variant;
        variant.name = it->path().stem().string();
        variant.path = it->path().string();
        variant.bytes = it->file_size(ec);
        if (ec) { ec.clear(); variant.bytes = 0; }
        m_variants.push_back(std::move(variant));
    }
    std::sort(m_variants.begin(), m_variants.end(), [](const Variant& a, const Variant& b) {
        return a.name < b.name;
    });
    m_selected = -1;
    for (int i = 0; i < static_cast<int>(m_variants.size()); ++i)
        if (m_variants[static_cast<std::size_t>(i)].path == selectedPath) m_selected = i;
}

void LevelVariantPanel::SetStatus(std::string status, bool error) {
    m_status = std::move(status);
    m_statusError = error;
}

LevelVariantPanel::Result LevelVariantPanel::Draw(const std::string& assetRoot,
                                                   bool* open) {
    Result result;
    if (m_scannedRoot != assetRoot) Refresh(assetRoot);
    if (!ImGui::Begin("Level Snapshots & Variants", open)) { ImGui::End(); return result; }

    if (ImGui::Button("Refresh")) Refresh(assetRoot);
    ImGui::SameLine();
    ImGui::TextDisabled("Content/LevelVariants");
    const float listHeight = std::clamp(ImGui::GetContentRegionAvail().y * 0.42f,
                                        130.0f, 360.0f);
    if (ImGui::BeginChild("##LevelVariants", ImVec2(0, listHeight), true)) {
        for (int i = 0; i < static_cast<int>(m_variants.size()); ++i) {
            const Variant& variant = m_variants[static_cast<std::size_t>(i)];
            ImGui::PushID(i);
            const std::string label = variant.name + "  ("
                + std::to_string(variant.bytes / 1024) + " KB)";
            if (ImGui::Selectable(label.c_str(), i == m_selected)) {
                m_selected = i;
                std::snprintf(m_name.data(), m_name.size(), "%s", variant.name.c_str());
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", variant.path.c_str());
            ImGui::PopID();
        }
        if (m_variants.empty()) ImGui::TextDisabled("No level variants captured yet.");
    }
    ImGui::EndChild();

    ImGui::InputText("Variant Name", m_name.data(), m_name.size());
    const std::string targetPath = TargetPath(assetRoot);
    if (targetPath.empty()) ImGui::BeginDisabled();
    if (ImGui::Button("Capture Current Level", ImVec2(-1, 0))) {
        result.action = Action::Capture;
        result.targetPath = targetPath;
    }
    if (targetPath.empty()) ImGui::EndDisabled();

    const bool hasSelection = m_selected >= 0
        && m_selected < static_cast<int>(m_variants.size());
    if (!hasSelection) ImGui::BeginDisabled();
    if (ImGui::Button("Restore Selected")) result.action = Action::Restore;
    ImGui::SameLine();
    if (ImGui::Button("Compare")) result.action = Action::Compare;
    if (ImGui::Button("Overwrite")) result.action = Action::Overwrite;
    ImGui::SameLine();
    if (ImGui::Button("Duplicate As")) result.action = Action::Duplicate;
    ImGui::SameLine();
    if (ImGui::Button("Rename")) result.action = Action::Rename;
    if (ImGui::Button("Delete Selected", ImVec2(-1, 0))) result.action = Action::Delete;
    if (!hasSelection) ImGui::EndDisabled();

    if (hasSelection) result.sourcePath = m_variants[static_cast<std::size_t>(m_selected)].path;
    if (result.action == Action::Duplicate || result.action == Action::Rename)
        result.targetPath = targetPath;
    if (result.action == Action::Overwrite) result.targetPath = result.sourcePath;

    if (!m_status.empty()) {
        ImGui::SeparatorText("Result");
        ImGui::TextColored(m_statusError ? ImVec4(1.0f, 0.3f, 0.25f, 1.0f)
                                         : ImVec4(0.35f, 0.9f, 0.5f, 1.0f),
                           "%s", m_status.c_str());
    }
    ImGui::TextDisabled("Restoring a variant is undoable and does not change the current scene file.");
    ImGui::End();
    return result;
}
