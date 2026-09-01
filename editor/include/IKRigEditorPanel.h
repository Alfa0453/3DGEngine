#pragma once

#include "EditorAssets.h"
#include <engine/assets/IKRigAsset.h>
#include <engine/assets/SkeletalAsset.h>

#include <string>

class IKRigEditorPanel {
public:
    struct Result {
        bool applySelected = false;
        bool saved = false;
        std::string message;
    };

    Result Draw(EditorAssets& assets, const std::string& assetRoot, bool* open);
    void QueueOpen(std::string path) { m_pendingOpen = std::move(path); }
    const engine::IKRigAssetData& Asset() const { return m_asset; }
    const std::string& Path() const { return m_path; }
    bool IsDirty() const { return m_dirty; }
    bool SaveForShutdown(const std::string& root, std::string* error);

private:
    void New(const std::string& root);
    bool Load(const std::string& path, const std::string& root, std::string* error);
    bool LoadSkeleton(const std::string& root, std::string* error);
    bool AutoSetup();
    void DrawPreview();
    bool BoneCombo(const char* label, std::string& bone);

    engine::IKRigAssetData m_asset;
    engine::SkeletonAssetData m_skeleton;
    std::string m_path, m_pendingOpen, m_status;
    int m_selectedGoal = -1;
    bool m_skeletonLoaded = false;
    bool m_dirty = false;
};
