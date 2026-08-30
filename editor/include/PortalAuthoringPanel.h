#pragma once

#include "EditorAssets.h"
#include <engine/assets/PortalAsset.h>

#include <string>
#include <vector>

class PortalAuthoringPanel {
public:
    struct Result { bool applySelected = false; bool saved = false; std::string message; };
    Result Draw(EditorAssets& assets, const std::vector<std::string>& sceneObjects,
                const std::string& assetRoot, bool* open);
    void QueueOpen(std::string path) { m_pendingOpen = std::move(path); }
    const engine::PortalAssetData& Asset() const { return m_asset; }
    const std::string& Path() const { return m_path; }
    bool IsDirty() const { return m_dirty; }
    bool SaveForShutdown(const std::string& root, std::string* error);

private:
    void New(const std::string& root);
    bool AssetCombo(const char* label, EditorAssets::Type type, std::string& path,
                    engine::AssetHandle& id, EditorAssets& assets, const char* empty);
    void DrawPreview() const;

    engine::PortalAssetData m_asset;
    std::string m_path, m_pendingOpen, m_status;
    bool m_dirty = false;
};
