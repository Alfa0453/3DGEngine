#include "engine/assets/LocalizationAsset.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <unordered_set>

namespace engine { namespace {
void Error(std::string* e, const std::string& value) { if (e) *e = value; }
}

void NormalizeLocalizationAsset(LocalizationAssetData& asset) {
    if (asset.name.empty()) asset.name = "GameLocalization";
    auto clean = [](std::string& language) {
        std::replace(language.begin(), language.end(), '_', '-');
        language.erase(std::remove_if(language.begin(), language.end(),
            [](unsigned char c) { return c == ' ' || c == '\t'; }), language.end());
    };
    clean(asset.sourceLanguage); clean(asset.fallbackLanguage);
    if (asset.sourceLanguage.empty()) asset.sourceLanguage = "en";
    if (asset.fallbackLanguage.empty()) asset.fallbackLanguage = asset.sourceLanguage;
    std::vector<std::string> languages;
    for (auto language : asset.languages) {
        clean(language);
        if (!language.empty() && std::find(languages.begin(), languages.end(), language) == languages.end())
            languages.push_back(std::move(language));
    }
    auto add = [&](const std::string& language) {
        if (std::find(languages.begin(), languages.end(), language) == languages.end()) languages.push_back(language);
    };
    add(asset.sourceLanguage); add(asset.fallbackLanguage);
    asset.languages = std::move(languages);
    for (auto it = asset.languageFonts.begin(); it != asset.languageFonts.end();) {
        if (std::find(asset.languages.begin(), asset.languages.end(), it->first) == asset.languages.end()) it = asset.languageFonts.erase(it); else ++it;
    }
    std::sort(asset.entries.begin(), asset.entries.end(), [](const auto& a, const auto& b) { return a.key < b.key; });
    std::sort(asset.assets.begin(), asset.assets.end(), [](const auto& a, const auto& b) { return a.key < b.key; });
}

bool ValidateLocalizationAsset(const LocalizationAssetData& asset, std::string* error) {
    if (asset.languages.empty()) { Error(error, "Localization asset needs at least one language."); return false; }
    std::unordered_set<std::string> languages(asset.languages.begin(), asset.languages.end());
    if (!languages.count(asset.sourceLanguage) || !languages.count(asset.fallbackLanguage)) {
        Error(error, "Source and fallback languages must exist in the language list."); return false;
    }
    std::unordered_set<std::string> keys;
    for (const auto& entry : asset.entries) {
        if (entry.key.empty() || !keys.insert(entry.key).second) {
            Error(error, entry.key.empty() ? "Localization keys cannot be empty." : "Duplicate localization key: " + entry.key);
            return false;
        }
    }
    for (const auto& entry : asset.assets) {
        if (entry.key.empty() || !keys.insert(entry.key).second) {
            Error(error, entry.key.empty() ? "Localized asset keys cannot be empty." : "Duplicate localization key: " + entry.key);
            return false;
        }
    }
    std::unordered_set<std::string> cueIds;
    for (const auto& cue : asset.subtitles) {
        if (cue.id.empty() || !cueIds.insert(cue.id).second) { Error(error, cue.id.empty() ? "Subtitle cue IDs cannot be empty." : "Duplicate subtitle cue: " + cue.id); return false; }
        if (cue.textKey.empty()) { Error(error, "Subtitle cue needs a text key: " + cue.id); return false; }
        if (cue.startSeconds < 0.0f || cue.durationSeconds <= 0.0f) { Error(error, "Subtitle timing is invalid: " + cue.id); return false; }
    }
    return true;
}

bool SaveLocalizationAsset(const std::string& path, LocalizationAssetData asset, std::string* error) {
    NormalizeLocalizationAsset(asset);
    if (!ValidateLocalizationAsset(asset, error)) return false;
    asset.header.type = AssetType::Localization;
    asset.header.assetVersion = kLocalizationAssetVersion;
    if (!asset.header.id.Valid()) asset.header.id = AssetHandle::Generate();
    asset.header.dependencies.clear();
    std::error_code ec; std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    std::ofstream out(path, std::ios::binary);
    if (!out) { Error(error, "Could not create localization asset: " + path); return false; }
    out << "3DG_LOCALIZATION " << kLocalizationAssetVersion << ' ' << asset.header.id.ToString() << '\n';
    out << "ASSET_DEPS 0\n";
    out << "TABLE " << std::quoted(asset.name) << ' ' << std::quoted(asset.sourceLanguage) << ' '
        << std::quoted(asset.fallbackLanguage) << '\n';
    for (const auto& language : asset.languages) out << "LANGUAGE " << std::quoted(language) << '\n';
    for (const auto& font : asset.languageFonts) out << "FONT " << std::quoted(font.first) << ' ' << std::quoted(font.second) << '\n';
    for (const auto& entry : asset.entries) {
        out << "ENTRY " << std::quoted(entry.key) << ' ' << std::quoted(entry.source) << ' ' << std::quoted(entry.context) << '\n';
        for (const auto& value : entry.translations)
            out << "TEXT " << std::quoted(entry.key) << ' ' << std::quoted(value.first) << ' ' << std::quoted(value.second) << '\n';
    }
    for (const auto& entry : asset.assets) {
        out << "ASSET " << std::quoted(entry.key) << ' ' << std::quoted(entry.fallbackPath) << '\n';
        for (const auto& value : entry.variants)
            out << "VARIANT " << std::quoted(entry.key) << ' ' << std::quoted(value.first) << ' ' << std::quoted(value.second) << '\n';
    }
    for (const auto& cue : asset.subtitles) {
        out << "SUBTITLE " << std::quoted(cue.id) << ' ' << std::quoted(cue.textKey) << ' ' << std::quoted(cue.speaker) << ' ' << cue.startSeconds << ' ' << cue.durationSeconds << '\n';
        for (const auto& voice : cue.voicePaths) out << "SUBTITLE_VOICE " << std::quoted(cue.id) << ' ' << std::quoted(voice.first) << ' ' << std::quoted(voice.second) << '\n';
    }
    out << "END\n";
    if (!out) { Error(error, "Failed while writing localization asset."); return false; }
    return true;
}

bool LoadLocalizationAsset(const std::string& path, LocalizationAssetData* output, std::string* error) {
    if (!output) { Error(error, "Localization output is null."); return false; }
    std::ifstream in(path, std::ios::binary); LocalizationAssetData asset;
    std::string magic, id, record; std::uint32_t version = 0; std::size_t count = 0;
    if (!(in >> magic >> version >> id) || magic != "3DG_LOCALIZATION" || version != kLocalizationAssetVersion
        || !AssetHandle::Parse(id, &asset.header.id)) { Error(error, "Invalid localization asset header."); return false; }
    if (!(in >> record >> count) || record != "ASSET_DEPS" || count != 0) { Error(error, "Invalid localization dependencies."); return false; }
    auto textEntry = [&](const std::string& key) -> LocalizationEntry* { for (auto& e : asset.entries) if (e.key == key) return &e; return nullptr; };
    auto assetEntry = [&](const std::string& key) -> LocalizedAssetEntry* { for (auto& e : asset.assets) if (e.key == key) return &e; return nullptr; };
    auto subtitle = [&](const std::string& id) -> LocalizationSubtitleCue* { for (auto& e : asset.subtitles) if (e.id == id) return &e; return nullptr; };
    while (in >> record && record != "END") {
        if (record == "TABLE") in >> std::quoted(asset.name) >> std::quoted(asset.sourceLanguage) >> std::quoted(asset.fallbackLanguage);
        else if (record == "LANGUAGE") { std::string language; in >> std::quoted(language); asset.languages.push_back(std::move(language)); }
        else if (record == "FONT") { std::string language, fontPath; in >> std::quoted(language) >> std::quoted(fontPath); asset.languageFonts[language] = std::move(fontPath); }
        else if (record == "ENTRY") { LocalizationEntry e; in >> std::quoted(e.key) >> std::quoted(e.source) >> std::quoted(e.context); asset.entries.push_back(std::move(e)); }
        else if (record == "TEXT") { std::string key, language, value; in >> std::quoted(key) >> std::quoted(language) >> std::quoted(value); auto* e = textEntry(key); if (!e) { Error(error, "Translation references missing key: " + key); return false; } e->translations[language] = std::move(value); }
        else if (record == "ASSET") { LocalizedAssetEntry e; in >> std::quoted(e.key) >> std::quoted(e.fallbackPath); asset.assets.push_back(std::move(e)); }
        else if (record == "VARIANT") { std::string key, language, value; in >> std::quoted(key) >> std::quoted(language) >> std::quoted(value); auto* e = assetEntry(key); if (!e) { Error(error, "Variant references missing key: " + key); return false; } e->variants[language] = std::move(value); }
        else if (record == "SUBTITLE") { LocalizationSubtitleCue cue; in >> std::quoted(cue.id) >> std::quoted(cue.textKey) >> std::quoted(cue.speaker) >> cue.startSeconds >> cue.durationSeconds; asset.subtitles.push_back(std::move(cue)); }
        else if (record == "SUBTITLE_VOICE") { std::string cueId, language, pathValue; in >> std::quoted(cueId) >> std::quoted(language) >> std::quoted(pathValue); auto* cue = subtitle(cueId); if (!cue) { Error(error, "Subtitle voice references missing cue: " + cueId); return false; } cue->voicePaths[language] = std::move(pathValue); }
        else { Error(error, "Unknown localization record: " + record); return false; }
        if (!in) { Error(error, "Localization asset is truncated or corrupt."); return false; }
    }
    asset.header.type = AssetType::Localization; asset.header.assetVersion = version;
    NormalizeLocalizationAsset(asset);
    if (!ValidateLocalizationAsset(asset, error)) return false;
    *output = std::move(asset); return true;
}
} // namespace engine
