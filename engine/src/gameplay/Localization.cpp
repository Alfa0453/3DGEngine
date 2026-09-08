#include "engine/gameplay/Localization.h"
#include "engine/core/Paths.h"

#include <algorithm>
#include <filesystem>

namespace engine { namespace {
std::string Resolve(const std::string& path) {
    const std::filesystem::path p(path); if (std::filesystem::exists(p)) return p.string();
    const std::filesystem::path executable(ExecutableDir());
    for (const auto& candidate : {executable / p, executable / "Content" / p, executable.parent_path() / p})
        if (std::filesystem::exists(candidate)) return candidate.string();
    return path;
}
template<class T> const T* Find(const std::vector<T>& entries, const std::string& key) {
    auto it = std::find_if(entries.begin(), entries.end(), [&](const T& entry) { return entry.key == key; });
    return it == entries.end() ? nullptr : &*it;
}
}

Localization& Localization::Instance() { static Localization value; return value; }
bool Localization::Load(const std::string& path, std::string* error) { LocalizationAssetData value; if (!LoadLocalizationAsset(Resolve(path), &value, error)) return false; SetTable(std::move(value)); return true; }
void Localization::SetTable(LocalizationAssetData table) { NormalizeLocalizationAsset(table); m_table = std::move(table); if (!SetLanguage(m_language)) m_language = m_table.fallbackLanguage; }
bool Localization::SetLanguage(const std::string& language) { if (std::find(m_table.languages.begin(), m_table.languages.end(), language) == m_table.languages.end()) return false; m_language = language; return true; }
bool Localization::Has(const std::string& key) const { return Find(m_table.entries, key) || Find(m_table.assets, key); }
std::string Localization::Text(const std::string& key, const std::string& fallback) const {
    const auto* entry = Find(m_table.entries, key); if (!entry) return fallback.empty() ? key : fallback;
    auto value = [&](const std::string& language) -> const std::string* { auto it = entry->translations.find(language); return it != entry->translations.end() && !it->second.empty() ? &it->second : nullptr; };
    if (const auto* result = value(m_language)) return *result;
    if (const auto* result = value(m_table.fallbackLanguage)) return *result;
    return !entry->source.empty() ? entry->source : (fallback.empty() ? key : fallback);
}
std::string Localization::Format(const std::string& key, const std::unordered_map<std::string, std::string>& arguments, const std::string& fallback) const {
    std::string result = Text(key, fallback);
    for (const auto& argument : arguments) { const std::string token = "{" + argument.first + "}"; for (auto pos = result.find(token); pos != std::string::npos; pos = result.find(token, pos + argument.second.size())) result.replace(pos, token.size(), argument.second); }
    return result;
}
std::string Localization::Asset(const std::string& key, const std::string& fallback) const {
    const auto* entry = Find(m_table.assets, key); if (!entry) return fallback;
    auto it = entry->variants.find(m_language); if (it != entry->variants.end() && !it->second.empty()) return it->second;
    it = entry->variants.find(m_table.fallbackLanguage); if (it != entry->variants.end() && !it->second.empty()) return it->second;
    return !entry->fallbackPath.empty() ? entry->fallbackPath : fallback;
}
} // namespace engine
