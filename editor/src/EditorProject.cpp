#include "EditorProject.h"

#include <filesystem>

void EditorProject::Load(engine::Config &config)
{
    m_projectName = config.GetString("project.name", "Untitled Project");
    const std::string legacyAssetRoot =
        config.GetString("project.asses", "Content");
    m_assetRoot = config.GetString("project.assets", legacyAssetRoot);
    if (m_assetRoot == "assets") {
        m_assetRoot = "Content";
    }
    std::error_code ec;
    std::filesystem::create_directories(ScenesRoot(), ec);

    const std::string storedScene =
        config.GetString("project.scene", "");
    m_scenePath = storedScene.empty()
        ? ResolveScenePath("Main.scene")
        : ResolveScenePath(storedScene);
    // Preserve a legacy root-level scene by copying it into the project Scenes
    // folder the first time the organized layout is used.
    if (!storedScene.empty()
        && std::filesystem::path(storedScene).parent_path().empty()
        && std::filesystem::exists(storedScene, ec)
        && !std::filesystem::exists(m_scenePath, ec)) {
        std::filesystem::copy_file(
            storedScene, m_scenePath,
            std::filesystem::copy_options::skip_existing, ec);
    }

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

std::string EditorProject::ScenesRoot() const
{
    return (std::filesystem::path(m_assetRoot) / "Scenes").string();
}

std::string EditorProject::ResolveScenePath(const std::string& value) const
{
    std::filesystem::path path =
        value.empty() ? std::filesystem::path("Main.scene")
                      : std::filesystem::path(value);
    if (path.extension() != ".scene") path.replace_extension(".scene");
    if (path.is_absolute()) return path.lexically_normal().string();

    const std::filesystem::path assetRoot(m_assetRoot);
    const std::filesystem::path normalized = path.lexically_normal();
    const std::string generic = normalized.generic_string();
    const std::string assetPrefix = assetRoot.lexically_normal().generic_string() + "/";
    if (generic.rfind(assetPrefix, 0) == 0)
        return normalized.string();
    if (!normalized.empty() && normalized.begin()->string() == "Scenes")
        return (assetRoot / normalized).lexically_normal().string();
    return (std::filesystem::path(ScenesRoot()) / normalized.filename())
        .lexically_normal().string();
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
