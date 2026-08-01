#pragma once

#include "AnimationClipAsset.h"

#include <engine/assets/RuntimeAssetManager.h>
#include <engine/graphics/Framebuffer.h>

#include <glm/glm.hpp>

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace engine {
class SkinnedModel;
class SkinnedRenderer;
}

// Authors reusable animation-clip assets (.3dgclip): pick an engine-imported native
// source (.3dgskmesh with embedded clips, or a .3dganim clip), choose a clip in it,
// set strip-root-motion / loop / speed, preview it, and save to Content. The Character
// Editor then references these clips instead of re-specifying them.
class ClipEditorPanel {
public:
    ~ClipEditorPanel();
    void QueueOpen(const std::string& path);
    void QueueSource(const std::string& animationPath,
                     const std::string& previewMeshPath);
    void Draw(const std::string& assetRoot, bool* open, bool* assetSaved,
              std::string* message, float deltaTime);

private:
    struct AssetChoice { std::string path; std::string displayName; };

    void RefreshChoices(const std::string& assetRoot);
    void ResetPreview();
    unsigned int RenderPreview(int width, int height, float deltaTime);
    void SyncBuffers();

    AnimationClipAsset m_asset;
    std::string m_path;
    std::string m_pendingOpen;
    std::string m_pendingSource;
    std::string m_pendingPreviewMesh;
    std::array<char, 128> m_nameBuffer{};
    std::array<char, 128> m_sourceSearch{};
    std::array<char, 128> m_meshSearch{};
    std::string m_scannedRoot;
    std::vector<AssetChoice> m_modelChoices;

    // Preview state (not saved; a rig to show the animation on when the source file
    // has no mesh of its own, e.g. a Mixamo "without skin" clip).
    std::string m_previewMeshPath;

    engine::RuntimeAssetManager m_assets;
    std::unique_ptr<engine::SkinnedRenderer> m_renderer;
    std::optional<engine::Framebuffer> m_fbo;
    const engine::SkinnedModel* m_sourceModel = nullptr;  // for clip enumeration
    std::string m_loadedSource;
    std::string m_loadedKey;          // source + preview-mesh + strip + clip, to detect reloads
    bool m_sourceIsAnimOnly = false;  // source is a .3dganim (merged onto the preview mesh)
    std::string m_error;
    std::vector<glm::mat4> m_pose;
    int   m_clipIndex = 0;
    float m_time = 0.0f;
    bool  m_playing = true;
    float m_yaw = 0.0f;
    float m_pitch = 0.0f;
    float m_zoom = 1.0f;
    int   m_size = 0;
};
