#include "EditorProject.h"

void EditorProject::Load(engine::Config &config)
{
    m_projectName = config.GetString("project.name", "Untitled Project");
    m_assetRoot = config.GetString("project.asses", "Content");
    if (m_assetRoot == "assets") {
        m_assetRoot = "Content";
    }
    m_scenePath = config.GetString("project.scene", "editor.scene");
}

void EditorProject::Save(engine::Config &config) const
{
    config.Set("project.name", m_projectName);
    config.Set("project.assets", m_assetRoot);
    config.Set("project.scene", m_scenePath);
}
