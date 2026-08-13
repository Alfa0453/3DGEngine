#pragma once

#include <engine/assets/AnimationRetargetAsset.h>
#include <engine/assets/SkeletalAsset.h>

#include <string>
#include <vector>

class EditorAssets;

class AnimationRetargetingPanel {
public:
    void Draw(EditorAssets& assets, const std::string& assetRoot, bool* open,
              bool* assetsChanged = nullptr, std::string* message = nullptr);
    void QueueOpen(const std::string& path) { m_queuedPath = path; }

private:
    bool LoadInputs(const std::string& assetRoot, std::string* error);
    bool AutoMap(const std::string& assetRoot, std::string* error);
    bool SaveProfile(const std::string& assetRoot, std::string* error);
    bool RetargetSelected(const std::string& assetRoot, bool allClips,
                          std::string* error);
    bool LoadProfile(const std::string& path, std::string* error);
    void DrawSkeletonComparison();

    engine::AnimationRetargetAssetData m_profile;
    engine::SkeletonAssetData m_sourceSkeleton;
    engine::SkeletonAssetData m_targetSkeleton;
    engine::AnimationAssetData m_sourceAnimation;
    std::string m_sourceSkeletonPath;
    std::string m_targetSkeletonPath;
    std::string m_animationPath;
    std::string m_profilePath;
    std::string m_outputName = "Retargeted";
    std::string m_queuedPath;
    std::string m_status;
    int m_selectedClip = 0;
    int m_selectedMapping = -1;
    float m_previewTime = 0.0f;
    bool m_previewPlaying = false;
    bool m_inputsLoaded = false;
    bool m_dirty = false;
};
