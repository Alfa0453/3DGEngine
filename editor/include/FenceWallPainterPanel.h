#pragma once

#include "EditorAssets.h"
#include "EditorScene.h"
#include <engine/assets/FenceWallAsset.h>

#include <string>

class FenceWallPainterPanel {
public:
    struct Result {
        bool createSpline = false;
        bool build = false;
        bool remove = false;
        bool saved = false;
        std::string message;
    };

    Result Draw(const EditorScene& scene, EditorAssets& assets,
                const std::string& assetRoot, bool* open);
    void QueueOpen(std::string path) { m_pendingOpen = std::move(path); }
    const engine::FenceWallAssetData& Asset() const { return m_asset; }
    const std::string& Path() const { return m_path; }
    const std::string& SplineName() const { return m_splineName; }
    void SetSplineName(std::string name) { m_splineName = std::move(name); }
    bool IsDirty() const { return m_dirty; }
    bool SaveForShutdown(const std::string& root, std::string* error);

private:
    void New(const std::string& root);
    bool AssetCombo(const char* label, EditorAssets::Type type,
                    std::string& path, engine::AssetHandle& id,
                    EditorAssets& assets, const char* emptyLabel);
    void DrawPreview() const;

    engine::FenceWallAssetData m_asset;
    std::string m_path;
    std::string m_pendingOpen;
    std::string m_splineName;
    std::string m_status;
    bool m_dirty = false;   // untouched panel has no unsaved changes (born-dirty was a bug)
};
