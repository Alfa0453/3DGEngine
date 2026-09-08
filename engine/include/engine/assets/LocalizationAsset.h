#pragma once

#include "engine/assets/AssetIdentity.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine {

inline constexpr std::uint32_t kLocalizationAssetVersion = 1;

struct LocalizationEntry {
    std::string key;
    std::string source;
    std::string context;
    std::unordered_map<std::string, std::string> translations;
};

struct LocalizedAssetEntry {
    std::string key;
    std::string fallbackPath;
    std::unordered_map<std::string, std::string> variants;
};

struct LocalizationSubtitleCue {
    std::string id;
    std::string textKey;
    std::string speaker;
    float startSeconds = 0.0f;
    float durationSeconds = 2.0f;
    std::unordered_map<std::string, std::string> voicePaths;
};

struct LocalizationAssetData {
    NativeAssetHeader header;
    std::string name = "GameLocalization";
    std::string sourceLanguage = "en";
    std::string fallbackLanguage = "en";
    std::vector<std::string> languages{"en"};
    std::unordered_map<std::string, std::string> languageFonts;
    std::vector<LocalizationEntry> entries;
    std::vector<LocalizedAssetEntry> assets;
    std::vector<LocalizationSubtitleCue> subtitles;
};

void NormalizeLocalizationAsset(LocalizationAssetData& asset);
bool ValidateLocalizationAsset(const LocalizationAssetData& asset, std::string* error = nullptr);
bool SaveLocalizationAsset(const std::string& path, LocalizationAssetData asset,
                           std::string* error = nullptr);
bool LoadLocalizationAsset(const std::string& path, LocalizationAssetData* asset,
                           std::string* error = nullptr);

} // namespace engine
