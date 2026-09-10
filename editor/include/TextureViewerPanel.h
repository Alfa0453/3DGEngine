#pragma once

#include <string>
#include <vector>

class EditorAssets;

namespace engine {
class RuntimeAssetManager;
}

// Dockable browser and preview for source images and engine-owned texture assets.
// RuntimeAssetManager owns the GPU textures; this panel only keeps stable paths.
class TextureViewerPanel {
public:
    void QueueOpen(const std::string& path);
    void Draw(EditorAssets& assets, engine::RuntimeAssetManager& runtimeAssets,
              bool* open);

private:
    void Refresh(EditorAssets& assets);
    void Select(const std::string& path);

    std::string m_root;
    std::string m_path;
    std::string m_pendingOpen;
    std::vector<std::string> m_textures;
    char m_filter[128]{};
    float m_thumbnailSize = 56.0f;
    float m_zoom = 1.0f;
    bool m_fit = true;
    bool m_checkerboard = true;
};
