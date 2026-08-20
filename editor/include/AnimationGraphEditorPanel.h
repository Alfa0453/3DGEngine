#pragma once

#include "AnimationGraphAsset.h"

#include <engine/animation/AnimationController.h>
#include <engine/assets/RuntimeAssetManager.h>
#include <engine/graphics/Framebuffer.h>

#include <glm/glm.hpp>

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine {
class SkinnedModel;
class SkinnedRenderer;
}

// Authors reusable animation state machines (.3dggraph): add clips from .3dgclip assets,
// build states / transitions / blend spaces / locomotion, preview them live on a test
// rig, and save to Content. Characters just reference one of these.
class AnimationGraphEditorPanel {
public:
    ~AnimationGraphEditorPanel();
    void QueueOpen(const std::string& path);
    void SetPreferredPreviewMesh(const std::string& path) {
        m_preferredPreviewMesh = path;
    }
    void Draw(const std::string& assetRoot, bool* open, bool* assetSaved,
              std::string* message, float deltaTime);
    bool IsDirty() const { return m_dirty; }
    const std::string& Path() const { return m_path; }
    bool SaveForShutdown(std::string* error);
    // Dependency refresh only; changing a .3dgclip must not dirty the graph.
    void InvalidateClipMetadata(const std::string&) { m_controllerDirty = true; }

private:
    struct AssetChoice { std::string path; std::string displayName; };
    enum class SelectionType { None, State, Transition };

    void RefreshChoices(const std::string& assetRoot);
    void ResetPreview();
    unsigned int RenderPreview(int width, int height, float deltaTime);
    void SyncBuffers();

    AnimationGraphAsset m_asset;
    std::string m_path;
    std::string m_pendingOpen;
    std::array<char, 128> m_nameBuffer{};
    std::array<char, 128> m_modelSearch{};
    std::array<char, 128> m_clipSearch{};
    std::string m_scannedRoot;
    std::vector<AssetChoice> m_modelChoices;
    std::vector<AssetChoice> m_clipChoices;
    std::string m_preferredPreviewMesh;

    // Live preview.
    engine::RuntimeAssetManager m_assets;
    std::unique_ptr<engine::SkinnedRenderer> m_renderer;
    std::optional<engine::Framebuffer> m_fbo;
    const engine::SkinnedModel* m_model = nullptr;   // preview rig with the graph's clips merged
    std::string m_loadedSignature;
    std::string m_clipMetadataSignature;
    std::string m_error;
    engine::AnimationController m_controller;
    bool m_controllerDirty = true;
    std::unordered_map<std::string, float> m_params;
    std::vector<glm::mat4> m_pose;
    float m_yaw = 0.0f;
    float m_pitch = 0.0f;
    float m_zoom = 1.0f;
    bool  m_playing = true;
    bool  m_dirty = false;
    int   m_size = 0;

    // Node-canvas authoring state. Positions themselves live in the asset; these
    // values are session-only view/interaction state.
    SelectionType m_selectionType = SelectionType::None;
    engine::AssetHandle m_selectedId;
    glm::vec2 m_canvasPan{20.0f, 20.0f};
    float m_canvasZoom = 1.0f;
    glm::vec2 m_contextGraphPosition{80.0f, 80.0f};
    engine::AssetHandle m_linkSourceId; // invalid while linking from Any State
    bool m_linking = false;
    bool m_linkingFromAny = false;
};
