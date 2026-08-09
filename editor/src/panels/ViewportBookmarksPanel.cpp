#include "ViewportBookmarksPanel.h"

#include "EditorPanels.h"
#include "EditorScene.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstring>

namespace {
bool ContainsInsensitive(const std::string& value, const char* filter) {
    if (!filter || !*filter) return true;
    std::string a = value, b = filter;
    std::transform(a.begin(), a.end(), a.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::transform(b.begin(), b.end(), b.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return a.find(b) != std::string::npos;
}
}

ViewportBookmarksPanel::Result ViewportBookmarksPanel::Draw(
    const EditorScene& scene, bool* open) {
    Result result;
    if (!ImGui::Begin(EditorPanels::Name(EditorPanels::Panel::ViewportBookmarks), open)) {
        ImGui::End();
        return result;
    }

    const auto& bookmarks = scene.ViewportBookmarks();
    if (m_selected >= bookmarks.size()) m_selected = static_cast<std::size_t>(-1);

    ImGui::TextDisabled("Save editor viewpoints without creating gameplay cameras.");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##BookmarkFilter", "Filter bookmarks...", m_filter, sizeof(m_filter));
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 124.0f);
    ImGui::InputText("##BookmarkName", m_name, sizeof(m_name));
    ImGui::SameLine();
    if (ImGui::Button("Capture Current", ImVec2(120.0f, 0.0f))) {
        result.action = Action::Capture;
        result.name = m_name;
        result.blendDuration = m_blendDuration;
    }
    ImGui::SliderFloat("Travel time", &m_blendDuration, 0.0f, 3.0f, "%.2f s");
    if (ImGui::Button("Frame Selected Object")) result.action = Action::FrameSelected;
    ImGui::SameLine();
    ImGui::TextDisabled("Then capture the resulting view if desired.");
    ImGui::Separator();

    if (bookmarks.empty()) {
        ImGui::TextDisabled("No viewport bookmarks saved in this scene.");
    } else if (ImGui::BeginChild("##ViewportBookmarkList",
                                 ImVec2(0.0f, std::max(100.0f, ImGui::GetContentRegionAvail().y - 42.0f)), true)) {
        for (std::size_t i = 0; i < bookmarks.size(); ++i) {
            const auto& bookmark = bookmarks[i];
            if (!ContainsInsensitive(bookmark.name, m_filter)) continue;
            ImGui::PushID(static_cast<int>(i));
            const float labelWidth = std::max(80.0f, ImGui::GetContentRegionAvail().x - 190.0f);
            if (ImGui::Selectable(bookmark.name.c_str(), m_selected == i,
                                  ImGuiSelectableFlags_None, ImVec2(labelWidth, 0.0f))) {
                m_selected = i;
                std::strncpy(m_name, bookmark.name.c_str(), sizeof(m_name) - 1);
                m_name[sizeof(m_name) - 1] = '\0';
                m_blendDuration = bookmark.blendDuration;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Go")) {
                result.action = Action::Visit;
                result.index = i;
                result.blendDuration = m_blendDuration;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Update")) {
                result.action = Action::Overwrite;
                result.index = i;
                result.name = bookmark.name;
                result.blendDuration = m_blendDuration;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("X")) {
                result.action = Action::Delete;
                result.index = i;
            }
            ImGui::TextDisabled("  (%.1f, %.1f, %.1f)  FOV %.0f",
                bookmark.position.x, bookmark.position.y, bookmark.position.z, bookmark.fov);
            ImGui::PopID();
        }
    }
    if (!bookmarks.empty()) ImGui::EndChild();

    if (m_selected < bookmarks.size()) {
        ImGui::Separator();
        if (ImGui::Button("Rename Selected")) {
            result.action = Action::Rename;
            result.index = m_selected;
            result.name = m_name;
            result.blendDuration = m_blendDuration;
        }
    }
    ImGui::End();
    return result;
}
