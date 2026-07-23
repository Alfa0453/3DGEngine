#include "EditorAssets.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

namespace {

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string NormalizeSlashes(std::string value) {
    std::replace(value.begin(), value.end(), '\\', '/');
    return value;
}

} // namespace

bool EditorAssets::Refresh(const std::string &rootPath, std::string *error)
{
    m_rootPath = rootPath;
    m_assets.clear();
    m_folders.clear();
    m_totalFileCount = 0;

    std::error_code ec;
    if (!fs::exists(rootPath, ec)) {
        fs::create_directories(rootPath, ec);
    }
    if (ec || !fs::is_directory(rootPath, ec)) {
        if (error) *error = "Could not create asset folder: " + rootPath;
        return false;
    }

    ec.clear();
    for (const fs::directory_entry& entry : fs::recursive_directory_iterator(rootPath, ec)) {
        if (entry.is_regular_file(ec)) {
            ++m_totalFileCount;
        }
    }
    if (ec) {
        if (error) *error = "Could not count Content files.";
        return false;
    }

    ec.clear();
    const fs::path current = CurrentPath();
    if (!fs::exists(current, ec) || !fs::is_directory(current, ec)) {
        m_currentFolder.clear();
    }

    ec.clear();
    const fs::path browserPath = CurrentPath();
    for (const fs::directory_entry& entry : fs::directory_iterator(browserPath, ec)) {
        if (ec) {
            if (error) *error = "Could not scan asset folder.";
            m_assets.clear();
            m_folders.clear();
            return false;
        }

        const fs::path path = entry.path();
        const std::string relative = NormalizeSlashes(fs::relative(path, rootPath, ec).string());
        if (ec) {
            continue;
        }

        if (entry.is_directory(ec)) {
            Folder folder;
            folder.relativePath = relative;
            folder.displayName = path.filename().string();
            m_folders.push_back(folder);
            continue;
        }
        if (!entry.is_regular_file(ec)) {
            continue;
        }

        Asset asset;
        asset.relativePath = relative;
        asset.displayName = path.filename().string();
        asset.type = ClassifyExtension(Lower(path.extension().string()));
        m_assets.push_back(asset);
    }

    std::sort(m_folders.begin(), m_folders.end(), [](const Folder& a, const Folder& b) {
        return a.displayName < b.displayName;
    });
    std::sort(m_assets.begin(), m_assets.end(), [](const Asset& a, const Asset& b) {
        return a.displayName < b.displayName;
    });

    if (m_assets.empty()) {
        m_selectedIndex = -1;
    } else if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_assets.size())) {
        m_selectedIndex = 0;
    }
    if (m_selectedFolderIndex >= static_cast<int>(m_folders.size())) {
        m_selectedFolderIndex = -1;
    }
    if (m_selectedIndex >= static_cast<int>(m_assets.size())) {
        m_selectedIndex = -1;
    }
    if (m_selectedType == SelectionType::Folder && m_selectedFolderIndex < 0) {
        m_selectedType = SelectionType::None;
    }
    if (m_selectedType == SelectionType::Asset && m_selectedIndex < 0) {
        m_selectedType = SelectionType::None;
    }

    return true;
}

bool EditorAssets::CreateFolder(const std::string &name, std::string *error)
{
    const std::string cleanName = SanitizeFolderName(name);
    if (cleanName.empty()) {
        if (error) *error = "Folder name is empty.";
        return false;
    }

    std::error_code ec;
    const fs::path folderPath = fs::path(CurrentPath()) / cleanName;
    if (fs::exists(folderPath, ec)) {
        if (error) *error = "Folder already exists: " + cleanName;
        return false;
    }
    fs::create_directories(folderPath, ec);
    if (ec) {
        if (error) *error = "Could not create folder: " + cleanName;
        return false;
    }
    return Refresh(m_rootPath, error);
}

bool EditorAssets::ImportAsset(const std::string &sourcePath, std::string *error)
{
    const fs::path source(sourcePath);
    std::error_code ec;
    if (!fs::exists(source, ec) || !fs::is_regular_file(source, ec)) {
        if (error) *error = "Import source file not found: " + sourcePath;
        return false;
    }

    fs::path destination = fs::path(CurrentPath()) / source.filename();
    if (fs::exists(destination, ec)) {
        const std::string stem = source.stem().string();
        const std::string extension = source.extension().string();
        int suffix = 1;
        do {
            destination = fs::path(CurrentPath())
                / (stem + "_" + std::to_string(suffix) + extension);
            ++suffix;
        } while (fs::exists(destination, ec));
    }

    fs::copy_file(source, destination, fs::copy_options::none, ec);
    if (ec) {
        if (error) *error = "Could not import asset: " + sourcePath;
        return false;
    }
    return Refresh(m_rootPath, error);
}

bool EditorAssets::EnterFolder(int index, std::string *error)
{
    if (index < 0 || index >= static_cast<int>(m_folders.size())) {
        if (error) *error = "Folder selection is invalid.";
        return false;
    }

    m_currentFolder = m_folders[static_cast<std::size_t>(index)].relativePath;
    m_selectedType = SelectionType::None;
    m_selectedFolderIndex = -1;
    m_selectedIndex = -1;
    return Refresh(m_rootPath, error);
}

bool EditorAssets::EnterSelectedFolder(std::string *error)
{
    if (m_selectedType != SelectionType::Folder) {
        if (error) *error = "No folder selected.";
        return false;
    }
    return EnterFolder(m_selectedFolderIndex, error);
}

bool EditorAssets::GoUp(std::string *error)
{
    if (m_currentFolder.empty()) {
        return true;
    }

    fs::path parent = fs::path(m_currentFolder).parent_path();
    m_currentFolder = NormalizeSlashes(parent.string());
    m_selectedType = SelectionType::None;
    m_selectedFolderIndex = -1;
    m_selectedIndex = -1;
    return Refresh(m_rootPath, error);
}

bool EditorAssets::CopySelected(std::string *error)
{
    if (m_selectedType == SelectionType::Folder) {
        const Folder* folder = SelectedFolder();
        if (!folder) {
            if (error) *error = "NO folder selecte.";
            return false;
        }
        m_clipboardRelativePath = folder->relativePath;
        m_clipboardIsFolder = true;
        return true;
    }

    if (m_selectedType == SelectionType::Asset) {
        const Asset* asset = SelectedAsset();
        if (!asset) {
            if (error) *error = "No asse selected.";
            return false;
        }
        m_clipboardRelativePath = asset->relativePath;
        m_clipboardIsFolder = false;
        return true;
    }

    if (error) *error = "No content entry selected.";
    return false;
}

bool EditorAssets::PasteCopied(std::string *error)
{
    if (m_clipboardRelativePath.empty()) {
        if (error) *error = "Nothing copied.";
        return false;
    }

    std::error_code ec;
    const fs::path source = FullPathForRelative(m_clipboardRelativePath);
    if (!fs::exists(source, ec)) {
        if (error) *error = "Copied Content entry no longer exists.";
        return false;
    }

    fs::path destination = UniqueDestinationPath(fs::path(CurrentPath()) / source.filename());
    if (m_clipboardIsFolder) {
        const fs::path canonicalSource = fs::weakly_canonical(source, ec);
        ec.clear();
        const fs::path canonicalDestinationParent = fs::weakly_canonical(destination.parent_path(), ec);
        const std::string sourceString = NormalizeSlashes(canonicalSource.string());
        const std::string destinationParentString = NormalizeSlashes(canonicalDestinationParent.string());
        const bool destinationInsideSource = destinationParentString == sourceString
            || (destinationParentString.size() > sourceString.size()
                && destinationParentString.compare(0, sourceString.size(), sourceString) == 0
                && destinationParentString[sourceString.size()] == '/');
        if (!sourceString.empty()
            && destinationInsideSource) {
            if (error) *error = "Cannot paste a folder inside itself.";
            return false;
        }
        fs::copy(source, destination,
            fs::copy_options::recursive | fs::copy_options::copy_symlinks, ec);
    } else {
        fs::copy_file(source, destination, fs::copy_options::none, ec);
    }

    if (ec) {
        if (error) *error = "Could not paste copied Content entry.";
        return false;
    }
    return Refresh(m_rootPath, error);
}

bool EditorAssets::RenameSelectedFolder(const std::string& newName, std::string* error)
{
    const Folder* folder = SelectedFolder();
    if (m_selectedType != SelectionType::Folder || !folder) {
        if (error) *error = "Select a folder to rename.";
        return false;
    }

    const std::string cleanName = SanitizeFolderName(newName);
    if (cleanName.empty() || cleanName == "." || cleanName == "..") {
        if (error) *error = "Enter a valid folder name.";
        return false;
    }
    if (cleanName.back() == '.') {
        if (error) *error = "Folder names cannot end with a period.";
        return false;
    }

    const std::string oldRelative = folder->relativePath;
    const fs::path source = FullPathForRelative(oldRelative);
    const fs::path destination = source.parent_path() / cleanName;
    if (source.filename().string() == cleanName) {
        if (error) *error = "The folder already has that name.";
        return false;
    }

    std::error_code ec;
    const bool caseOnlyRename = Lower(source.filename().string()) == Lower(cleanName);
    if (!caseOnlyRename && fs::exists(destination, ec)) {
        if (error) *error = "A folder or asset with that name already exists.";
        return false;
    }

    if (caseOnlyRename) {
        const fs::path temporary = UniqueDestinationPath(
            source.parent_path() / (source.filename().string() + ".rename_tmp"));
        fs::rename(source, temporary, ec);
        if (!ec) {
            fs::rename(temporary, destination, ec);
            if (ec) {
                std::error_code rollbackError;
                fs::rename(temporary, source, rollbackError);
            }
        }
    } else {
        fs::rename(source, destination, ec);
    }
    if (ec) {
        if (error) *error = "Could not rename folder to: " + cleanName;
        return false;
    }

    const std::string newRelative = NormalizeSlashes(
        (fs::path(oldRelative).parent_path() / cleanName).string());
    if (m_clipboardRelativePath == oldRelative
        || (m_clipboardRelativePath.size() > oldRelative.size()
            && m_clipboardRelativePath.compare(0, oldRelative.size(), oldRelative) == 0
            && m_clipboardRelativePath[oldRelative.size()] == '/')) {
        m_clipboardRelativePath = newRelative
            + m_clipboardRelativePath.substr(oldRelative.size());
    }

    m_selectedType = SelectionType::None;
    m_selectedFolderIndex = -1;
    if (!Refresh(m_rootPath, error)) return false;
    for (int i = 0; i < static_cast<int>(m_folders.size()); ++i) {
        if (m_folders[static_cast<std::size_t>(i)].relativePath == newRelative) {
            SelectFolderIndex(i);
            break;
        }
    }
    return true;
}

bool EditorAssets::DeleteSelectedEntry(std::string *error)
{
    std::string relative;
    bool deletingFolder = false;
    if (m_selectedType == SelectionType::Folder) {
        const Folder* folder = SelectedFolder();
        if (!folder) {
            if (error) *error = "No folder selected.";
            return false;
        }
        relative = folder->relativePath;
        deletingFolder = true;
    } else if (m_selectedType == SelectionType::Asset) {
        const Asset* asset = SelectedAsset();
        if (!asset) {
            if (error) *error = "No asset selected.";
            return false;
        }
        relative = asset->relativePath;
    } else {
        if (error) *error = "No Content entry selected.";
        return false;
    }

    std::error_code ec;
    const fs::path target = FullPathForRelative(relative);
    if (deletingFolder) {
        fs::remove_all(target, ec);
    } else {
        fs::remove(target, ec);
    }
    if (ec) {
        if (error) *error = "Could not delete Content entry.";
        return false;
    }

    if (m_clipboardRelativePath == relative) {
        m_clipboardRelativePath.clear();
        m_clipboardIsFolder = false;
    }
    m_selectedType = SelectionType::None;
    m_selectedFolderIndex = -1;
    m_selectedIndex = -1;
    return Refresh(m_rootPath, error);
}

const EditorAssets::Folder *EditorAssets::SelectedFolder() const
{
    if (m_selectedFolderIndex < 0 || m_selectedFolderIndex >= static_cast<int>(m_folders.size())) {
        return nullptr;
    }
    return &m_folders[static_cast<std::size_t>(m_selectedFolderIndex)];
}

const EditorAssets::Asset *EditorAssets::SelectedAsset() const
{
    if (m_selectedType != SelectionType::Asset
        || m_selectedIndex < 0
        || m_selectedIndex >= static_cast<int>(m_assets.size())) {
        return nullptr;
    }
    return &m_assets[static_cast<std::size_t>(m_selectedIndex)];
}

std::string EditorAssets::CopiedDisplayName() const
{
    if (m_clipboardRelativePath.empty()) {
        return {};
    }
    return fs::path(m_clipboardRelativePath).filename().string();
}

std::string EditorAssets::SelectedAssetFullPath() const 
{
    const Asset* asset = SelectedAsset();
    return asset ? FullPathForRelative(asset->relativePath) : std::string();
}

std::string EditorAssets::CopiedFullPath() const
{
    return m_clipboardRelativePath.empty() ? std::string() : FullPathForRelative(m_clipboardRelativePath);
}

void EditorAssets::SelectNext()
{
    if (m_assets.empty()) {
        m_selectedType = SelectionType::None;
        m_selectedIndex = -1;
        return;
    }
    m_selectedType = SelectionType::Asset;
    m_selectedFolderIndex = -1;
    m_selectedIndex = (m_selectedIndex + 1) % static_cast<int>(m_assets.size());
}

void EditorAssets::SelectPrevious()
{
    if (m_assets.empty()) {
        m_selectedType = SelectionType::None;
        m_selectedIndex = -1;
        return;
    }
    m_selectedType = SelectionType::Asset;
    m_selectedFolderIndex = -1;
    m_selectedIndex = (m_selectedIndex <= 0)
        ? static_cast<int>(m_assets.size()) - 1
        : m_selectedIndex - 1;
}

void EditorAssets::SelectFolderIndex(int index)
{
    if (index < 0 || index >= static_cast<int>(m_folders.size())) {
        return;
    }
    m_selectedType = SelectionType::Folder;
    m_selectedFolderIndex = index;
    m_selectedIndex = -1;
}

void EditorAssets::SelectIndex(int index)
{
    if (index < 0 || index >= static_cast<int>(m_assets.size())) {
        return;
    }
    m_selectedType = SelectionType::Asset;
    m_selectedFolderIndex = -1;
    m_selectedIndex = index;
}

const char *EditorAssets::TypeName(Type type)
{
    switch (type) {
        case Type::Model: return "Model";
        case Type::SkeletalModel: return "Skeletal Model";
        case Type::Material: return "Material";
        case Type::Texture: return "Texture";
        case Type::Shader: return "Shader";
        case Type::Audio: return "Audio";
        case Type::Scene: return "Scene";
        case Type::Particle: return "Particle";
        case Type::ParticleEffect: return "Particle Effect";
        case Type::Hud: return "HUD";
        case Type::Character: return "Character";
        case Type::BehaviorGraph: return "Behavior Tree";
        case Type::Script: return "Script";
        case Type::Other: return "Other";
    }
    return "Other";
}

EditorAssets::Type EditorAssets::ClassifyExtension(const std::string &extension)
{
    if (extension == ".obj" || extension == ".fbx" || extension == ".gltf" || extension == ".glb") {
    return Type::Model;
    }
    if (extension == ".3dgmat") {
        return Type::Material;
    }
    if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".tga"
        || extension == ".raw" || extension == ".r16" || extension == ".exr") {
        return Type::Texture;
    }
    if (extension == ".vert" || extension == ".frag" || extension == ".glsl"
        || extension == ".3dgshader") {
        return Type::Shader;
    }
    if (extension == ".wav" || extension == ".ogg" || extension == ".mp3"
        || extension == ".flac" || extension == ".3dgaudio"
        || extension == ".3dgmusic" || extension == ".3dgmixer") {
        return Type::Audio;
    }
    if (extension == ".scene") {
        return Type::Scene;
    }
    if (extension == ".particle") {
        return Type::Particle;
    }
    if (extension == ".particlefx") {
        return Type::ParticleEffect;
    }
    if (extension == ".hud") {
        return Type::Hud;
    }
    if (extension == ".3dgcharacter") {
        return Type::Character;
    }
    if (extension == ".btgraph") {
        return Type::BehaviorGraph;
    }
    if (extension == ".h" || extension == ".hpp"
        || extension == ".cpp" || extension == ".cc") {
        return Type::Script;
    }
    return Type::Other;
}

std::string EditorAssets::SanitizeFolderName(const std::string &name)
{
    std::string clean;
    clean.reserve(name.size());
    for (char c: name) {
        const bool invalid = c == '<' || c == '>' || c == ':' || c == '"'
            || c == '/' || c == '\\' || c == '|' || c == '?' || c == '*';
        if (!invalid) {
            clean.push_back(c);
        }
    }

    while (!clean.empty() && std::isspace(static_cast<unsigned char>(clean.front()))) {
        clean.erase(clean.begin());
    }
    while (!clean.empty() && std::isspace(static_cast<unsigned char>(clean.back()))) {
        clean.pop_back();
    }
    return clean;
}

fs::path EditorAssets::UniqueDestinationPath(const fs::path& destination)
{
    if (!fs::exists(destination)) {
        return destination;
    }
    const std::string stem = destination.stem().string();
    const std::string extension = destination.extension().string();
    const fs::path parent = destination.parent_path();
    int suffix = 1;
    fs::path candidate;
    do {
        candidate = parent / (stem + "_" + std::to_string(suffix) + extension);
        ++suffix;
    } while (fs::exists(candidate));
    return candidate;
}

std::string EditorAssets::CurrentPath() const
{
    return (fs::path(m_rootPath) / m_currentFolder).string();
}

std::string EditorAssets::FullPathForRelative(const std::string &relativePath) const
{
    return (fs::path(m_rootPath) / relativePath).string();
}
