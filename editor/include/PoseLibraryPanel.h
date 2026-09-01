#pragma once

#include "EditorAssets.h"
#include <engine/assets/PoseLibraryAsset.h>
#include <engine/assets/SkeletalAsset.h>

#include <string>
#include <vector>

class PoseLibraryPanel {
public:
    struct Result { bool saved=false; std::string message; };
    Result Draw(EditorAssets& assets, const std::string& assetRoot, bool* open);
    void QueueOpen(std::string path) { m_pendingOpen=std::move(path); }
    bool IsDirty() const { return m_dirty; }
    const std::string& Path() const { return m_path; }
    bool SaveForShutdown(const std::string& root, std::string* error);
private:
    void New(const std::string& root);
    bool Load(const std::string& path, const std::string& root, std::string* error);
    bool LoadSkeleton(const std::string& root, std::string* error);
    bool LoadAnimation(const std::string& root, std::string* error);
    void SampleSource();
    void DrawPreview();
    engine::PoseLibraryAssetData m_asset;
    engine::SkeletonAssetData m_skeleton;
    engine::AnimationAssetData m_animation;
    std::vector<engine::BoneLocal> m_sourcePose;
    std::string m_path,m_pendingOpen,m_animationPath,m_status,m_search;
    int m_selectedPose=-1,m_selectedBone=-1;
    float m_time=0.0f,m_previewBlend=1.0f;
    bool m_skeletonLoaded=false,m_animationLoaded=false,m_dirty=false;
};
