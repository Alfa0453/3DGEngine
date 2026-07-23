#pragma once

#include "CharacterAsset.h"

#include <engine/assets/RuntimeAssetManager.h>
#include <engine/animation/AnimationController.h>
#include <engine/graphics/Framebuffer.h>
#include <engine/graphics/Mesh.h>

#include <array>
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <unordered_map>

namespace engine {
class SkinnedModel;
class SkinnedRenderer;
class Shader;
}

class CharacterEditorPanel {
public:
    ~CharacterEditorPanel();
    void QueueOpen(const std::string& path);
    void Draw(EditorScene& scene, const std::string& assetRoot, bool* open,
              bool* assetSaved, std::string* message,
              float deltaTime);

    // The currently edited character (used to instantiate it into the scene).
    const CharacterAsset& Asset() const { return m_asset; }
    // True once if the user asked to add the character to the scene as a new object.
    bool ConsumeAddToSceneRequest() {
        const bool requested = m_addToSceneRequested;
        m_addToSceneRequested = false;
        return requested;
    }

private:
    struct AssetChoice {
        std::string path;
        std::string displayName;
    };

    void SyncBuffers();
    void RefreshAssetChoices(const std::string& assetRoot);
    unsigned int RenderModelPreview(int width, int height, float deltaTime);
    void ResetPreviewModel();
    void RebuildPreviewGraph();
    void RebuildColliderGuide();
    CharacterAsset m_asset;
    std::string m_path;
    std::string m_pendingOpen;
    int m_component = 0;
    bool m_dirty = false;
    std::array<char, 260> m_pathBuffer{};
    std::array<char, 128> m_nameBuffer{};
    std::array<char, 260> m_modelBuffer{};
    std::array<char, 260> m_materialBuffer{};
    std::array<char, 128> m_modelSearch{};
    std::array<char, 128> m_materialSearch{};
    std::array<char, 128> m_animSearch{};   // filter for the animation-file picker
    std::array<char, 128> m_idleBuffer{};
    std::array<char, 128> m_walkBuffer{};
    std::array<char, 128> m_runBuffer{};
    std::array<char, 260> m_behaviorBuffer{};
    std::array<char, 128> m_scriptClassBuffer{};
    std::array<char, 260> m_scriptPathBuffer{};
    std::vector<AssetChoice> m_modelChoices;
    std::vector<AssetChoice> m_materialChoices;
    std::string m_scannedAssetRoot;

    engine::RuntimeAssetManager m_previewAssets;
    std::unique_ptr<engine::SkinnedRenderer> m_previewRenderer;
    std::optional<engine::Framebuffer> m_previewFramebuffer;
    const engine::SkinnedModel* m_previewModel = nullptr;
    std::string m_previewModelPath;
    std::string m_previewAnimSignature;   // rebuild the preview when merged clips change
    std::string m_previewError;
    std::vector<glm::mat4> m_previewPose;
    int m_previewClip = 0;
    float m_previewTime = 0.0f;
    float m_previewYaw = 0.0f;
    float m_previewPitch = 0.0f;
    float m_previewZoom = 1.0f;
    bool m_previewPlaying = true;

    // Render-only model-offset gizmo state. The gizmo is drawn as an ImGui overlay on
    // the preview image; these are filled by RenderModelPreview each frame so the Draw
    // pass can project the handles and hit-test the mouse against them.
    int  m_gizmoMode = 0;            // 0 = Move, 1 = Rotate, 2 = Scale
    int  m_activeGizmoAxis = -1;     // 0 = X, 1 = Y, 2 = Z while dragging; -1 = none
    bool m_gizmoDragging = false;
    glm::mat4 m_previewViewProj{1.0f};   // camera view-projection used for the preview
    glm::mat4 m_previewModelMatrix{1.0f};// mesh model matrix (frame * offset)
    glm::mat4 m_previewGizmoFrame{1.0f}; // orbit * fit; columns give the offset axes
    glm::vec3 m_previewModelCenter{0.0f};
    bool m_previewGraphDirty = true;
    engine::AnimationController m_previewController;
    std::unordered_map<std::string, float> m_previewGraphParameters;
    std::unique_ptr<engine::Shader> m_colliderGuideShader;
    std::optional<engine::Mesh> m_colliderGuideMesh;
    engine::ecs::Collider m_cachedGuideCollider;
    bool m_colliderGuideDirty = true;
    bool m_showColliderGuide = true;
    bool m_addToSceneRequested = false;
};
