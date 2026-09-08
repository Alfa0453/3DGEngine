#pragma once

#include "engine/assets/LocalizationAsset.h"

#include <string>
#include <unordered_map>

namespace engine {

class Localization {
public:
    static Localization& Instance();
    bool Load(const std::string& assetPath, std::string* error = nullptr);
    void SetTable(LocalizationAssetData table);
    bool SetLanguage(const std::string& language);
    const std::string& Language() const { return m_language; }
    const LocalizationAssetData& Table() const { return m_table; }
    std::string Text(const std::string& key, const std::string& fallback = {}) const;
    std::string Format(const std::string& key,
                       const std::unordered_map<std::string, std::string>& arguments,
                       const std::string& fallback = {}) const;
    std::string Asset(const std::string& key, const std::string& fallback = {}) const;
    bool Has(const std::string& key) const;

private:
    LocalizationAssetData m_table;
    std::string m_language = "en";
};

} // namespace engine
