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
    const std::string& LastSavedScenePath() const { return m_lastSavedScenePath; }
    bool HasLastSavedScene() const { return m_hasLastSavedScene; }
    std::string ScenesRoot() const;
    std::string ResolveScenePath(const std::string& path) const;
    const std::string& ProjectName() const { return m_projectName; }
    const std::string& ProjectFilePath() const { return m_projectFilePath; }
    bool HasProjectFile() const { return !m_projectFilePath.empty(); }
    const std::vector<std::string>& RecentScenes() const { return m_recentScenes; }

    // Scaffold a brand-new empty project on disk: creates <projectDir>/Content/Scenes,
    // points the asset root at it (absolute), and writes a Project.3dgproject file so
    // the project can be reopened later. Returns false (with *error) on failure.
    bool CreateProject(const std::string& projectDir, const std::string& name, std::string* error);

    // Open an existing Project.3dgproject: loads its settings into outConfig (making a
    // relative asset root absolute against the project folder) and applies them via Load().
    bool OpenProjectFile(const std::string& projectFile, engine::Config& outConfig, std::string* error);

    void SetAssetRoot(const std::string& path) { m_assetRoot = path; }
    void SetScenePath(const std::string& path) { m_scenePath = path; }
    void MarkCurrentSceneSaved() {
        m_lastSavedScenePath = m_scenePath;
        m_hasLastSavedScene = true;
    }
    void AddRecentScene(const std::string& path);

private:
    static constexpr int kMaxRecentScenes = 8;

    std::string m_projectName = "Untitled Project";
    std::string m_assetRoot = "assets";
    std::string m_scenePath = "Content/Scenes/Main.scene";
    std::string m_lastSavedScenePath = "Content/Scenes/Main.scene";
    std::string m_projectFilePath;   // path to the active Project.3dgproject (empty = legacy single project)
    bool m_hasLastSavedScene = false;
    std::vector<std::string> m_recentScenes;
};
