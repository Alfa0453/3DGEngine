#include "EditorProject.h"

void EditorProject::Load(engine::Config &config)
{
    m_projectName = config.GetString("project.name", "Untitled Project");
    m_assetRoot = config.GetString("project.asses", "Content");
    if (m_assetRoot == "assets") {
        m_assetRoot = "Content";
    }
    m_scenePath = config.GetString("project.scene", "editor.scene");

    m_recentScenes.clear();
    const int count = config.GetInt("project.recent.count", 0);
    for (int i = 0; i < count && i < kMaxRecentScenes; ++i) {
        const std::string path = config.GetString("project.recent." + std::to_string(i), "");
        if (!path.empty()) {
            AddRecentScene(path);
        }
    }
    AddRecentScene(m_scenePath);
}

void EditorProject::Save(engine::Config &config) const
{
    config.Set("project.name", m_projectName);
    config.Set("project.assets", m_assetRoot);
    config.Set("project.scene", m_scenePath);
    config.Set("project.recent.count", static_cast<int>(m_recentScenes.size()));
    for (int i = 0; i < kMaxRecentScenes; ++i) {
        const std::string key = "project.recent." + std::to_string(i);
        config.Set(key, i < static_cast<int>(m_recentScenes.size()) ? m_recentScenes[static_cast<std::size_t>(i)] : "");
    }
}

void EditorProject::AddRecentScene(const std::string& path)
{
    if (path.empty()) {
        return;
    }

    m_recentScenes.erase(std::remove(m_recentScenes.begin(), m_recentScenes.end(), path), m_recentScenes.end());
    m_recentScenes.insert(m_recentScenes.begin(), path);
    if (m_recentScenes.size() > static_cast<std::size_t>(kMaxRecentScenes)) {
        m_recentScenes.resize(static_cast<std::size_t>(kMaxRecentScenes));
    }
}
