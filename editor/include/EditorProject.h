#pragma once

#include <engine/core/Config.h>

#include <string>
#include <vector>

class EditorProject {
public:
    void Load(engine::Config& config);
    void Save(engine::Config& config) const;

    const std::string& AssetRoot() const { return m_assetRoot; }
    const std::string& ScenePath() const { return m_scenePath; }
    std::string ScenesRoot() const;
    std::string ResolveScenePath(const std::string& path) const;
    const std::string& ProjectName() const { return m_projectName; }
    const std::vector<std::string>& RecentScenes() const { return m_recentScenes; }

    void SetAssetRoot(const std::string& path) { m_assetRoot = path; }
    void SetScenePath(const std::string& path) { m_scenePath = path; }
    void AddRecentScene(const std::string& path);

private:
    static constexpr int kMaxRecentScenes = 8;

    std::string m_projectName = "Untitled Project";
    std::string m_assetRoot = "assets";
    std::string m_scenePath = "Content/Scenes/Main.scene";
    std::vector<std::string> m_recentScenes;
};
