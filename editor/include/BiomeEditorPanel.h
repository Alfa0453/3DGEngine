#pragma once

#include "EditorAssets.h"
#include <engine/assets/BiomeAsset.h>

#include <string>
#include <vector>

class BiomeEditorPanel {
public:
    struct Result { bool saved = false; bool applyRequested = false; std::string message; };
    void QueueOpen(const std::string& path) { m_pendingOpen = path; }
    Result Draw(EditorAssets& assets, const std::string& contentRoot, bool* open);
    const engine::BiomeAssetData& Biome() const { return m_biome; }
    const std::string& Path() const { return m_path; }

private:
    void NewBiome();
    bool Load(const std::string& path, std::string* error);
    bool Save(const std::string& root, std::string* error);
    void RefreshPreview();
    void DrawPreview();
    bool AssetCombo(const char* label, EditorAssets::Type type,
                    std::string& path, engine::AssetHandle& id,
                    EditorAssets& assets);
    engine::BiomeAssetData m_biome;
    std::vector<engine::BiomePlacement> m_preview;
    std::string m_path, m_pendingOpen, m_status;
    bool m_dirty = true;
};
