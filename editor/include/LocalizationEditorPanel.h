#pragma once

#include <engine/assets/LocalizationAsset.h>

#include <string>

class LocalizationEditorPanel {
public:
    struct Result { bool saved = false; std::string message; };
    Result Draw(const std::string& contentRoot, bool* open);
    void QueueOpen(std::string path) { m_pendingOpen = std::move(path); }
    bool SaveForShutdown(const std::string& contentRoot, std::string* error);
    bool IsDirty() const { return m_dirty; }

private:
    void New(const std::string& contentRoot);
    bool Save(std::string* error);
    bool ImportCsv(const std::string& path, std::string* error);
    bool ExportCsv(const std::string& path, std::string* error) const;
    int MissingCount(const std::string& language) const;

    engine::LocalizationAssetData m_asset;
    std::string m_path, m_pendingOpen, m_status;
    std::string m_newLanguage = "fr";
    std::string m_search;
    int m_selectedEntry = -1;
    int m_previewLanguage = 0;
    int m_selectedAsset = -1;
    int m_selectedSubtitle = -1;
    bool m_dirty = false;
    bool m_pseudoPreview = false;
};
