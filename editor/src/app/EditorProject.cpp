#include "EditorProject.h"

#include <filesystem>

namespace {

bool IsAuthoredSceneFile(const std::filesystem::path& path)
{
    const std::string filename = path.filename().string();
    return path.extension() == ".scene"
        && filename.find(".autosave.scene") == std::string::npos
        && filename.find(".runtime.scene") == std::string::npos;
}

std::string NewestSavedScene(const std::string& scenesRoot)
{
    std::error_code ec;
    std::filesystem::file_time_type newestTime{};
    std::filesystem::path newestPath;
    bool found = false;

    for (std::filesystem::directory_iterator it(scenesRoot, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec) || !IsAuthoredSceneFile(it->path())) {
            continue;
        }
        const auto writeTime = it->last_write_time(ec);
        if (ec) {
            ec.clear();
            continue;
        }
        if (!found || writeTime > newestTime) {
            found = true;
            newestTime = writeTime;
            newestPath = it->path();
        }
    }
    return found ? newestPath.lexically_normal().string() : std::string();
}

} // namespace

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

    std::string storedScene = config.GetString("project.last_saved_scene", "");
    if (storedScene.empty() || !std::filesystem::is_regular_file(ResolveScenePath(storedScene), ec)) {
        const std::string newestScene = NewestSavedScene(ScenesRoot());
        if (!newestScene.empty()) {
            storedScene = newestScene;
        } else {
            storedScene = config.GetString("project.scene", "");
        }
    }
    m_hasLastSavedScene = !storedScene.empty();
    m_scenePath = storedScene.empty()
        ? ResolveScenePath("Main.scene")
        : ResolveScenePath(storedScene);
    m_lastSavedScenePath = m_scenePath;
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

bool EditorProject::CreateProject(const std::string& projectDir, const std::string& name, std::string* error)
{
    std::error_code ec;
    const std::filesystem::path dir = std::filesystem::absolute(projectDir, ec);
    if (ec) { if (error) *error = "cannot resolve project path"; return false; }

    std::filesystem::create_directories(dir, ec);
    if (ec) { if (error) *error = "cannot create project folder"; return false; }

    const std::filesystem::path content = dir / "Content";
    const std::filesystem::path scenes  = content / "Scenes";
    std::filesystem::create_directories(scenes, ec);
    if (ec) { if (error) *error = "cannot create Content/Scenes folder"; return false; }

    m_projectName        = name.empty() ? std::string("New Project") : name;
    m_assetRoot          = content.lexically_normal().string();
    m_scenePath          = (scenes / "Main.scene").lexically_normal().string();
    m_lastSavedScenePath = m_scenePath;
    m_hasLastSavedScene  = false;
    m_recentScenes.clear();
    m_projectFilePath    = (dir / "Project.3dgproject").lexically_normal().string();

    // Write an initial project file so the project can be reopened later.
    engine::Config cfg;
    Save(cfg);
    if (!cfg.Save(m_projectFilePath)) { if (error) *error = "cannot write project file"; return false; }
    return true;
}

bool EditorProject::OpenProjectFile(const std::string& projectFile, engine::Config& outConfig, std::string* error)
{
    std::error_code ec;
    if (!std::filesystem::is_regular_file(projectFile, ec)) {
        if (error) *error = "project file not found";
        return false;
    }

    outConfig = engine::Config(projectFile);   // loads the stored keys and remembers the path
    m_projectFilePath = std::filesystem::absolute(projectFile, ec).lexically_normal().string();

    // Resolve a relative asset root against the project folder so projects are
    // location-independent (not tied to the process working directory).
    const std::filesystem::path projectRoot = std::filesystem::path(m_projectFilePath).parent_path();
    std::string assets = outConfig.GetString("project.assets", "Content");
    if (std::filesystem::path(assets).is_relative())
        assets = (projectRoot / assets).lexically_normal().string();
    outConfig.Set("project.assets", assets);

    Load(outConfig);
    return true;
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
    config.Set("project.last_saved_scene",
        m_hasLastSavedScene ? m_lastSavedScenePath : std::string());
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
