#pragma once

#include <engine/assets/FontAsset.h>
#include <engine/assets/LocalizationAsset.h>
#include <engine/ui/Hud.h>

#include <array>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

struct ImVec2;

class UiLocalizationPreviewPanel {
public:
    struct Resolution { const char* name; int width; int height; };
    void Draw(const engine::HudDocument& hud,
              const std::string& hudPath,
              const std::filesystem::path& contentRoot,
              const engine::HudContext& context,
              const std::function<unsigned int(const std::string&)>& textureLookup,
              bool* open);

private:
    struct Issue {
        enum class Severity { Info, Warning, Error } severity = Severity::Info;
        std::string widget;
        std::string detail;
    };
    struct FontCacheEntry { engine::FontAsset asset; bool valid = false; std::string error; };

    void RefreshLocalizationAssets(const std::filesystem::path& contentRoot);
    void LoadLocalization(const std::filesystem::path& path);
    std::string LocalizedText(const engine::HudWidget& widget,
                              const engine::HudContext& context,
                              bool* missingKey, bool* missingTranslation) const;
    std::string Expanded(std::string value) const;
    bool IsRtlLanguage() const;
    const FontCacheEntry* Font(const std::string& path,
                               const std::filesystem::path& contentRoot);
    int MissingGlyphs(const std::string& text, const engine::FontAsset& font) const;
    std::vector<Issue> Inspect(const engine::HudDocument& hud,
                               const engine::HudContext& context,
                               int width, int height,
                               const std::filesystem::path& contentRoot);
    void DrawPreview(const engine::HudDocument& hud,
                     const engine::HudContext& context,
                     const std::function<unsigned int(const std::string&)>& textureLookup,
                     const std::filesystem::path& contentRoot,
                     ImVec2 available);

    std::filesystem::path m_contentRoot;
    std::vector<std::filesystem::path> m_localizationAssets;
    int m_localizationIndex = -1;
    engine::LocalizationAssetData m_localization;
    bool m_hasLocalization = false;
    int m_languageIndex = 0;
    int m_resolutionIndex = 1;
    int m_customWidth = 1920;
    int m_customHeight = 1080;
    float m_dpiScale = 1.0f;
    float m_safeAreaX = 0.05f;
    float m_safeAreaY = 0.05f;
    float m_expansion = 1.0f;
    bool m_pseudoLocalization = false;
    bool m_forceRtl = false;
    bool m_autoRtl = true;
    bool m_showSafeArea = true;
    bool m_showDiagnostics = true;
    bool m_initialized = false;
    std::string m_status;
    std::unordered_map<std::string, FontCacheEntry> m_fonts;
};
