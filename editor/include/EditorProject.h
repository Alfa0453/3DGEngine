#pragma once

#include <engine/core/Config.h>

#include <string>

class EditorProject {
public:
    void Load(engine::Config& config);
    void Save(engine::Config& config) const;

    const std::string& AssetRoot() const { return m_assetRoot; }
    const std::string& ScenePath() const { return m_scenePath; }
    const std::string& ProjectName() const { return m_projectName; }

    void SetAssetRoot(const std::string& path) { m_assetRoot = path; }
    void SetScenePath(const std::string& path) { m_scenePath = path; }

private:
    std::string m_projectName = "Untitled Project";
    std::string m_assetRoot = "assets";
    std::string m_scenePath = "editor.scene";
};