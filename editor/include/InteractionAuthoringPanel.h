#pragma once

#include "EditorAssets.h"
#include <engine/assets/InteractionAsset.h>

#include <string>

class InteractionAuthoringPanel {
public:
    struct Result {
        bool applySelected = false;
        bool saved = false;
        std::string message;
    };

    Result Draw(EditorAssets& assets, const std::string& assetRoot, float deltaSeconds, bool* open);
    void QueueOpen(std::string path) { m_pendingOpen = std::move(path); }
    const engine::InteractionAssetData& Asset() const { return m_asset; }
    const std::string& Path() const { return m_path; }
    bool IsDirty() const { return m_dirty; }
    bool SaveForShutdown(const std::string& root, std::string* error);

private:
    void New(const std::string& root);
    void Preset(engine::InteractionMotionType type);
    bool AssetCombo(const char* label, EditorAssets::Type type, std::string& path,
                    engine::AssetHandle& id, EditorAssets& assets, const char* empty);
    void DrawPreview(float deltaSeconds);

    engine::InteractionAssetData m_asset;
    std::string m_path;
    std::string m_pendingOpen;
    std::string m_status;
    float m_previewAlpha = 0.0f;
    bool m_previewPlaying = false;
    bool m_previewOpening = true;
    bool m_dirty = false;   // untouched panel has no unsaved changes (born-dirty was a bug)
};
