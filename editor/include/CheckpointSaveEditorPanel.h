#pragma once

#include "EditorAssets.h"
#include <engine/assets/SaveProfileAsset.h>
#include <engine/ecs/Components.h>
#include <engine/ecs/Registry.h>

#include <string>

class CheckpointSaveEditorPanel {
public:
    struct Result { bool saved = false; std::string message; };
    Result Draw(EditorAssets& assets, const std::string& root,
                engine::ecs::Registry* runtimeRegistry,
                const std::string& scenePath, float playtimeSeconds,
                const std::string& selectedName,
                const engine::ecs::Transform* selectedTransform, bool* open);
    void QueueOpen(std::string path) { m_pendingOpen = std::move(path); }
    bool IsDirty() const { return m_dirty; }
    const std::string& Path() const { return m_path; }
    bool SaveForShutdown(const std::string& root, std::string* error);
private:
    void New(const std::string& root);
    engine::SaveProfileAssetData m_asset;
    std::string m_path, m_pendingOpen, m_status;
    int m_selectedCheckpoint = -1;
    bool m_dirty = false;
};
