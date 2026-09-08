#include "LocalizationEditorPanel.h"
#include "EditorPanels.h"

#include <engine/gameplay/Localization.h>
#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>
#include <vector>

namespace {
bool Text(const char* label, std::string& value, std::size_t capacity = 2048) {
    std::vector<char> buffer(std::max(capacity, value.size() + 256));
    std::snprintf(buffer.data(), buffer.size(), "%s", value.c_str());
    if (!ImGui::InputText(label, buffer.data(), buffer.size())) return false;
    value = buffer.data(); return true;
}
std::string Lower(std::string value) { std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); }); return value; }
std::string CsvCell(const std::string& value) { std::string out = "\""; for (char c : value) { if (c == '"') out += '"'; out += c; } return out + '"'; }
std::vector<std::string> CsvRow(const std::string& line) {
    std::vector<std::string> result; std::string cell; bool quoted = false;
    for (std::size_t i = 0; i < line.size(); ++i) { const char c = line[i]; if (c == '"') { if (quoted && i + 1 < line.size() && line[i + 1] == '"') { cell += '"'; ++i; } else quoted = !quoted; } else if (c == ',' && !quoted) { result.push_back(cell); cell.clear(); } else cell += c; }
    result.push_back(cell); return result;
}
std::string Pseudo(std::string value) { std::string out = "[!! "; for (char c : value) { out += c; if (std::string("aeiouAEIOU").find(c) != std::string::npos) out += c; } return out + " !!]"; }
}

void LocalizationEditorPanel::New(const std::string& root) { m_asset = {}; m_asset.header.id = engine::AssetHandle::Generate(); m_asset.name = "GameLocalization"; m_path = (std::filesystem::path(root) / "GameAssets" / "Localization" / "GameLocalization.3dgloc").string(); m_selectedEntry = -1; m_previewLanguage = 0; m_dirty = true; m_status.clear(); }
bool LocalizationEditorPanel::Save(std::string* error) { if (!engine::SaveLocalizationAsset(m_path, m_asset, error)) return false; m_dirty = false; engine::Localization::Instance().SetTable(m_asset); return true; }
bool LocalizationEditorPanel::SaveForShutdown(const std::string& root, std::string* error) { if (m_path.empty()) New(root); return Save(error); }
int LocalizationEditorPanel::MissingCount(const std::string& language) const { int count = 0; for (const auto& e : m_asset.entries) { if (language == m_asset.sourceLanguage && !e.source.empty()) continue; auto it = e.translations.find(language); if (it == e.translations.end() || it->second.empty()) ++count; } return count; }

bool LocalizationEditorPanel::ExportCsv(const std::string& path, std::string* error) const {
    std::ofstream out(path, std::ios::binary); if (!out) { if (error) *error = "Could not create CSV: " + path; return false; }
    out << "key,source,context"; for (const auto& l : m_asset.languages) out << ',' << CsvCell(l); out << '\n';
    for (const auto& e : m_asset.entries) { out << CsvCell(e.key) << ',' << CsvCell(e.source) << ',' << CsvCell(e.context); for (const auto& l : m_asset.languages) { auto it = e.translations.find(l); out << ',' << CsvCell(it == e.translations.end() ? std::string{} : it->second); } out << '\n'; }
    return static_cast<bool>(out);
}
bool LocalizationEditorPanel::ImportCsv(const std::string& path, std::string* error) {
    std::ifstream in(path, std::ios::binary); std::string line; if (!in || !std::getline(in, line)) { if (error) *error = "Could not read CSV: " + path; return false; }
    auto header = CsvRow(line); if (header.size() < 4 || Lower(header[0]) != "key") { if (error) *error = "CSV header must be key,source,context,<languages...>."; return false; }
    for (std::size_t i = 3; i < header.size(); ++i) if (std::find(m_asset.languages.begin(), m_asset.languages.end(), header[i]) == m_asset.languages.end()) m_asset.languages.push_back(header[i]);
    while (std::getline(in, line)) { auto row = CsvRow(line); if (row.empty() || row[0].empty()) continue; auto it = std::find_if(m_asset.entries.begin(), m_asset.entries.end(), [&](const auto& e) { return e.key == row[0]; }); if (it == m_asset.entries.end()) { m_asset.entries.push_back({}); it = std::prev(m_asset.entries.end()); it->key = row[0]; } if (row.size() > 1) it->source = row[1]; if (row.size() > 2) it->context = row[2]; for (std::size_t i = 3; i < std::min(row.size(), header.size()); ++i) it->translations[header[i]] = row[i]; }
    engine::NormalizeLocalizationAsset(m_asset); m_dirty = true; return true;
}

LocalizationEditorPanel::Result LocalizationEditorPanel::Draw(const std::string& root, bool* open) {
    Result result; if (!m_asset.header.id.Valid()) New(root);
    if (!m_pendingOpen.empty()) { std::string error; if (engine::LoadLocalizationAsset(m_pendingOpen, &m_asset, &error)) { m_path = m_pendingOpen; m_dirty = false; m_selectedEntry = -1; m_status = "Loaded " + m_path; engine::Localization::Instance().SetTable(m_asset); } else m_status = error; m_pendingOpen.clear(); }
    if (!ImGui::Begin(EditorPanels::Name(EditorPanels::Panel::LocalizationEditor), open)) { ImGui::End(); return result; }
    if (ImGui::Button("New")) New(root); ImGui::SameLine();
    if (ImGui::Button("Save")) { std::string error; if (Save(&error)) { result.saved = true; result.message = "Saved localization asset: " + m_path; } else result.message = error; }
    ImGui::SameLine(); if (ImGui::Button("Import CSV")) { std::string error; const auto path = std::filesystem::path(m_path).replace_extension(".csv").string(); if (ImportCsv(path, &error)) m_status = "Imported " + path; else m_status = error; }
    ImGui::SameLine(); if (ImGui::Button("Export CSV")) { std::string error; const auto path = std::filesystem::path(m_path).replace_extension(".csv").string(); if (ExportCsv(path, &error)) m_status = "Exported " + path; else m_status = error; }
    ImGui::SameLine(); ImGui::TextUnformatted(m_path.c_str());
    m_dirty |= Text("Table Name", m_asset.name); m_dirty |= Text("Source Language", m_asset.sourceLanguage); m_dirty |= Text("Fallback Language", m_asset.fallbackLanguage);
    ImGui::SeparatorText("Languages");
    for (std::size_t i = 0; i < m_asset.languages.size(); ++i) { ImGui::PushID(static_cast<int>(i)); ImGui::Text("%s  (%d missing)", m_asset.languages[i].c_str(), MissingCount(m_asset.languages[i])); ImGui::SameLine(); const bool protectedLanguage = m_asset.languages[i] == m_asset.sourceLanguage || m_asset.languages[i] == m_asset.fallbackLanguage; if (ImGui::SmallButton("Remove") && !protectedLanguage) { const std::string removed = m_asset.languages[i]; m_asset.languages.erase(m_asset.languages.begin() + static_cast<std::ptrdiff_t>(i)); m_asset.languageFonts.erase(removed); for (auto& e : m_asset.entries) e.translations.erase(removed); for (auto& e : m_asset.assets) e.variants.erase(removed); for (auto& e : m_asset.subtitles) e.voicePaths.erase(removed); m_dirty = true; ImGui::PopID(); break; } ImGui::PopID(); }
    Text("New Language##Localization", m_newLanguage); ImGui::SameLine(); if (ImGui::Button("Add Language")) { if (!m_newLanguage.empty() && std::find(m_asset.languages.begin(), m_asset.languages.end(), m_newLanguage) == m_asset.languages.end()) { m_asset.languages.push_back(m_newLanguage); m_dirty = true; } }
    if (ImGui::TreeNode("Language Fonts")) { ImGui::TextDisabled("Use a .3dgfont containing the glyphs required by each language."); for (const auto& language : m_asset.languages) m_dirty |= Text((language + " Font##LocalizationFont").c_str(), m_asset.languageFonts[language]); ImGui::TreePop(); }
    ImGui::SeparatorText("Text Entries"); Text("Search##Localization", m_search); ImGui::SameLine(); if (ImGui::Button("Add Entry")) { engine::LocalizationEntry e; e.key = "New.Key" + std::to_string(m_asset.entries.size() + 1); e.source = "New text"; m_asset.entries.push_back(std::move(e)); m_selectedEntry = static_cast<int>(m_asset.entries.size() - 1); m_dirty = true; }
    const float listWidth = std::min(300.0f, ImGui::GetContentRegionAvail().x * .35f); ImGui::BeginChild("LocalizationEntryList", {listWidth, 360}, true); const std::string filter = Lower(m_search);
    for (std::size_t i = 0; i < m_asset.entries.size(); ++i) { const auto& e = m_asset.entries[i]; if (!filter.empty() && Lower(e.key + " " + e.source + " " + e.context).find(filter) == std::string::npos) continue; ImGui::PushID(static_cast<int>(i)); if (ImGui::Selectable(e.key.c_str(), m_selectedEntry == static_cast<int>(i))) m_selectedEntry = static_cast<int>(i); ImGui::PopID(); }
    ImGui::EndChild(); ImGui::SameLine(); ImGui::BeginGroup();
    if (m_selectedEntry >= 0 && m_selectedEntry < static_cast<int>(m_asset.entries.size())) { auto& e = m_asset.entries[static_cast<std::size_t>(m_selectedEntry)]; ImGui::PushID("LocalizationEntryDetails"); m_dirty |= Text("Key", e.key); m_dirty |= Text("Source Text", e.source, 8192); m_dirty |= Text("Translator Context", e.context, 4096); for (const auto& language : m_asset.languages) { auto& value = e.translations[language]; m_dirty |= Text((language + "##Translation").c_str(), value, 8192); } if (ImGui::Button("Delete Entry")) { m_asset.entries.erase(m_asset.entries.begin() + m_selectedEntry); m_selectedEntry = std::min(m_selectedEntry, static_cast<int>(m_asset.entries.size()) - 1); m_dirty = true; } ImGui::PopID(); }
    else ImGui::TextDisabled("Select an entry to edit its translations."); ImGui::EndGroup();
    if (ImGui::CollapsingHeader("Localized Assets")) {
        ImGui::TextDisabled("Resolve language-specific textures, audio, movies, or other assets from one key.");
        if (ImGui::Button("Add Localized Asset")) { engine::LocalizedAssetEntry entry; entry.key = "Asset.Key" + std::to_string(m_asset.assets.size() + 1); m_asset.assets.push_back(std::move(entry)); m_selectedAsset = static_cast<int>(m_asset.assets.size() - 1); m_dirty = true; }
        for (std::size_t i = 0; i < m_asset.assets.size(); ++i) { ImGui::PushID(static_cast<int>(i)); if (ImGui::Selectable(m_asset.assets[i].key.c_str(), m_selectedAsset == static_cast<int>(i))) m_selectedAsset = static_cast<int>(i); ImGui::PopID(); }
        if (m_selectedAsset >= 0 && m_selectedAsset < static_cast<int>(m_asset.assets.size())) { auto& entry = m_asset.assets[static_cast<std::size_t>(m_selectedAsset)]; ImGui::PushID("LocalizedAssetDetails"); m_dirty |= Text("Asset Key", entry.key); m_dirty |= Text("Fallback Asset", entry.fallbackPath); for (const auto& language : m_asset.languages) m_dirty |= Text((language + " Asset##Variant").c_str(), entry.variants[language]); if (ImGui::Button("Delete Localized Asset")) { m_asset.assets.erase(m_asset.assets.begin() + m_selectedAsset); m_selectedAsset = -1; m_dirty = true; } ImGui::PopID(); }
    }
    if (ImGui::CollapsingHeader("Subtitle Timeline")) {
        ImGui::TextDisabled("Subtitle cues reference text keys and may select a localized voice asset.");
        if (ImGui::Button("Add Subtitle Cue")) { engine::LocalizationSubtitleCue cue; cue.id = "Cue" + std::to_string(m_asset.subtitles.size() + 1); if (m_selectedEntry >= 0 && m_selectedEntry < static_cast<int>(m_asset.entries.size())) cue.textKey = m_asset.entries[static_cast<std::size_t>(m_selectedEntry)].key; m_asset.subtitles.push_back(std::move(cue)); m_selectedSubtitle = static_cast<int>(m_asset.subtitles.size() - 1); m_dirty = true; }
        for (std::size_t i = 0; i < m_asset.subtitles.size(); ++i) { ImGui::PushID(static_cast<int>(i)); const auto& cue = m_asset.subtitles[i]; const std::string label = cue.id + "  " + std::to_string(cue.startSeconds) + "s"; if (ImGui::Selectable(label.c_str(), m_selectedSubtitle == static_cast<int>(i))) m_selectedSubtitle = static_cast<int>(i); ImGui::PopID(); }
        if (m_selectedSubtitle >= 0 && m_selectedSubtitle < static_cast<int>(m_asset.subtitles.size())) { auto& cue = m_asset.subtitles[static_cast<std::size_t>(m_selectedSubtitle)]; ImGui::PushID("SubtitleDetails"); m_dirty |= Text("Cue ID", cue.id); m_dirty |= Text("Text Key", cue.textKey); m_dirty |= Text("Speaker", cue.speaker); m_dirty |= ImGui::DragFloat("Start (s)", &cue.startSeconds, .05f, 0.0f, 86400.0f); m_dirty |= ImGui::DragFloat("Duration (s)", &cue.durationSeconds, .05f, .05f, 3600.0f); for (const auto& language : m_asset.languages) m_dirty |= Text((language + " Voice##SubtitleVoice").c_str(), cue.voicePaths[language]); if (ImGui::Button("Delete Subtitle Cue")) { m_asset.subtitles.erase(m_asset.subtitles.begin() + m_selectedSubtitle); m_selectedSubtitle = -1; m_dirty = true; } ImGui::PopID(); }
    }
    ImGui::SeparatorText("Language Preview"); if (!m_asset.languages.empty()) { m_previewLanguage = std::clamp(m_previewLanguage, 0, static_cast<int>(m_asset.languages.size()) - 1); if (ImGui::BeginCombo("Preview Language", m_asset.languages[static_cast<std::size_t>(m_previewLanguage)].c_str())) { for (std::size_t i = 0; i < m_asset.languages.size(); ++i) if (ImGui::Selectable(m_asset.languages[i].c_str(), m_previewLanguage == static_cast<int>(i))) m_previewLanguage = static_cast<int>(i); ImGui::EndCombo(); } ImGui::SameLine(); ImGui::Checkbox("Pseudo-localize", &m_pseudoPreview); engine::Localization preview; preview.SetTable(m_asset); preview.SetLanguage(m_asset.languages[static_cast<std::size_t>(m_previewLanguage)]); if (m_selectedEntry >= 0 && m_selectedEntry < static_cast<int>(m_asset.entries.size())) { const auto& e = m_asset.entries[static_cast<std::size_t>(m_selectedEntry)]; auto text = preview.Text(e.key, e.source); if (m_pseudoPreview) text = Pseudo(text); ImGui::TextWrapped("%s", text.c_str()); } }
    std::string validation; if (!engine::ValidateLocalizationAsset(m_asset, &validation)) ImGui::TextColored({1,.35f,.25f,1}, "%s", validation.c_str()); if (m_dirty) ImGui::TextColored({1,.7f,.2f,1}, "Unsaved changes"); if (!m_status.empty()) ImGui::TextWrapped("%s", m_status.c_str()); ImGui::End(); return result;
}
