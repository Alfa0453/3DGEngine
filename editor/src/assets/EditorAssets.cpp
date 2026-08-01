#include "EditorAssets.h"

#include <engine/assets/AssetRegistry.h>
#include <engine/assets/SkeletalAsset.h>
#include <engine/assets/StaticMeshAsset.h>
#include <engine/assets/TextureAsset.h>

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

    if (m_assetRegistry) {
        std::string registryError;
        if (!m_assetRegistry->SynchronizeAuthoredAssets(
                rootPath, &registryError)
            || !m_assetRegistry->Save(
                engine::AssetRegistry::DefaultRegistryPath(rootPath),
                &registryError)) {
            if (error) *error = "Content scanned, but the asset registry could not "
                "be synchronized: " + registryError;
            return false;
        }
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

bool EditorAssets::CreateFolderAt(const std::string& parentRelativePath,
                                  const std::string& name,
                                  std::string* createdRelativePath,
                                  std::string* error) {
    const std::string cleanName = SanitizeFolderName(name);
    if (cleanName.empty() || cleanName == "." || cleanName == ".."
        || cleanName.back() == '.') {
        if (error) *error = "Enter a valid folder name.";
        return false;
    }

    fs::path relativeParent = fs::path(NormalizeSlashes(parentRelativePath))
        .lexically_normal();
    if (relativeParent.is_absolute()) {
        if (error) *error = "The destination must be inside this project's Content folder.";
        return false;
    }
    for (const fs::path& part : relativeParent) {
        if (part == "..") {
            if (error) *error = "The destination cannot leave the Content folder.";
            return false;
        }
    }

    const fs::path parent = fs::path(m_rootPath) / relativeParent;
    std::error_code ec;
    if (!fs::is_directory(parent, ec)) {
        if (error) *error = "The selected destination folder no longer exists.";
        return false;
    }
    const fs::path folder = parent / cleanName;
    if (fs::exists(folder, ec)) {
        if (error) *error = "Folder already exists: " + cleanName;
        return false;
    }
    fs::create_directory(folder, ec);
    if (ec) {
        if (error) *error = "Could not create folder: " + cleanName;
        return false;
    }
    if (createdRelativePath) {
        *createdRelativePath = NormalizeSlashes(
            (relativeParent / cleanName).string());
        if (*createdRelativePath == ".") createdRelativePath->clear();
    }
    return Refresh(m_rootPath, error);
}

bool EditorAssets::ImportAssetToFolder(
    const std::string& sourcePath,
    const std::string& destinationRelativePath,
    std::string* error) {
    return ImportAssetToFolder(
        sourcePath, destinationRelativePath, ModelImportMode::Automatic, error);
}

bool EditorAssets::ImportAssetToFolder(
    const std::string& sourcePath,
    const std::string& destinationRelativePath,
    ModelImportMode modelMode,
    std::string* error) {
    fs::path relative = fs::path(NormalizeSlashes(destinationRelativePath))
        .lexically_normal();
    if (relative == ".") relative.clear();
    if (relative.is_absolute()) {
        if (error) *error = "The import destination must be inside Content.";
        return false;
    }
    for (const fs::path& part : relative) {
        if (part == "..") {
            if (error) *error = "The import destination cannot leave Content.";
            return false;
        }
    }
    std::error_code ec;
    const fs::path destination = fs::path(m_rootPath) / relative;
    if (!fs::is_directory(destination, ec)) {
        if (error) *error = "The selected import destination no longer exists.";
        return false;
    }

    const std::string originalFolder = m_currentFolder;
    m_currentFolder = NormalizeSlashes(relative.string());
    const bool imported = ImportAsset(sourcePath, modelMode, error);
    m_currentFolder = originalFolder;
    if (!imported) {
        std::string ignored;
        Refresh(m_rootPath, &ignored);
        return false;
    }
    return Refresh(m_rootPath, error);
}

bool EditorAssets::ImportAsset(const std::string &sourcePath, std::string *error)
{
    return ImportAsset(sourcePath, ModelImportMode::Automatic, error);
}

bool EditorAssets::ImportAsset(const std::string& sourcePath,
                               ModelImportMode modelMode,
                               std::string* error)
{
    m_lastImportMessage.clear();
    const fs::path source(sourcePath);
    std::error_code ec;
    if (!fs::exists(source, ec) || !fs::is_regular_file(source, ec)) {
        if (error) *error = "Import source file not found: " + sourcePath;
        return false;
    }

    const std::string extension = Lower(source.extension().string());
    const bool staticMeshSource = extension == ".obj" || extension == ".fbx"
        || extension == ".gltf" || extension == ".glb" || extension == ".dae"
        || extension == ".ply" || extension == ".stl";
    if (staticMeshSource) {
        const fs::path destination = UniqueDestinationPath(
            fs::path(CurrentPath()) / (source.stem().string() + ".3dgmesh"));
        engine::StaticMeshImportResult result;
        std::string staticError;
        const bool tryStatic = modelMode == ModelImportMode::Automatic
            || modelMode == ModelImportMode::StaticMesh;
        if (tryStatic && engine::ImportStaticMeshToAsset(
                source.string(), destination.string(), m_rootPath,
                m_staticMeshImportOptions, m_assetRegistry, &result,
                &staticError)) {
            m_lastImportMessage = "Imported static mesh: " + destination.filename().string()
                + " (" + std::to_string(result.subMeshCount) + " submesh(es), "
                + std::to_string(result.vertexCount) + " vertices, "
                + std::to_string(result.triangleCount) + " triangles, "
                + std::to_string(result.embeddedTextureCount) + " texture(s))";
            if (!Refresh(m_rootPath, error)) return false;
            const std::string importedRelative = NormalizeSlashes(
                fs::relative(destination, m_rootPath, ec).string());
            for (int i = 0; i < static_cast<int>(m_assets.size()); ++i) {
                if (m_assets[static_cast<std::size_t>(i)].relativePath == importedRelative) {
                    SelectIndex(i);
                    break;
                }
            }
            return true;
        }
        if (modelMode == ModelImportMode::StaticMesh) {
            if (error) *error = staticError;
            return false;
        }

        fs::path base = fs::path(CurrentPath()) / source.stem();
        const auto generatedAnimationExists = [&](const fs::path& candidate) {
            const fs::path folder = candidate.parent_path();
            const std::string prefix = candidate.filename().string() + "_";
            std::error_code scanError;
            for (fs::directory_iterator it(folder, scanError), end;
                 !scanError && it != end; it.increment(scanError)) {
                if (!it->is_regular_file(scanError)) continue;
                const fs::path path = it->path();
                if (Lower(path.extension().string()) == ".3dganim"
                    && path.stem().string().rfind(prefix, 0) == 0)
                    return true;
            }
            return false;
        };
        int suffix = 1;
        while (fs::exists(base.string() + ".3dgskmesh", ec)
               || fs::exists(base.string() + ".3dgskel", ec)
               || generatedAnimationExists(base)) {
            base = fs::path(CurrentPath())
                / (source.stem().string() + "_" + std::to_string(suffix++));
        }
        engine::SkeletalImportResult skeletalResult;
        std::string skeletalError;
        if (!engine::ImportSkeletalAssetsToContent(
                source.string(), base.string(), m_rootPath,
                m_skeletalImportOptions, m_assetRegistry,
                &skeletalResult, &skeletalError)) {
            if (error) {
                *error = tryStatic
                    ? staticError + " Skeletal import also failed: "
                        + skeletalError
                    : skeletalError;
            }
            return false;
        }
        m_lastImportMessage = "Imported skeletal source: "
            + std::to_string(skeletalResult.animationPaths.size())
            + " animation clip(s)";
        if (!skeletalResult.skeletalMeshPath.empty())
            m_lastImportMessage += ", mesh "
                + fs::path(skeletalResult.skeletalMeshPath).filename().string();
        if (!skeletalResult.skeletonPath.empty())
            m_lastImportMessage += ", skeleton "
                + fs::path(skeletalResult.skeletonPath).filename().string();
        if (!Refresh(m_rootPath, error)) return false;
        std::string selectedPath = !skeletalResult.skeletalMeshPath.empty()
            ? skeletalResult.skeletalMeshPath
            : (!skeletalResult.animationPaths.empty()
                ? skeletalResult.animationPaths.front()
                : skeletalResult.skeletonPath);
        const std::string importedRelative = NormalizeSlashes(
            fs::relative(selectedPath, m_rootPath, ec).string());
        for (int i = 0; i < static_cast<int>(m_assets.size()); ++i) {
            if (m_assets[static_cast<std::size_t>(i)].relativePath == importedRelative) {
                SelectIndex(i);
                break;
            }
        }
        return true;
    }

    const bool textureSource = extension == ".png" || extension == ".jpg"
        || extension == ".jpeg" || extension == ".tga";
    if (textureSource) {
        const fs::path destination = UniqueDestinationPath(
            fs::path(CurrentPath()) / (source.stem().string() + ".3dgtex"));
        engine::TextureImportResult result;
        if (!engine::ImportTextureToAsset(
                source.string(), destination.string(), m_rootPath,
                m_textureImportOptions, m_assetRegistry, &result, error))
            return false;
        m_lastImportMessage = "Imported texture: "
            + destination.filename().string() + " ("
            + std::to_string(result.width) + "x"
            + std::to_string(result.height) + ")";
        if (!Refresh(m_rootPath, error)) return false;
        const std::string importedRelative = NormalizeSlashes(
            fs::relative(destination, m_rootPath, ec).string());
        for (int i = 0; i < static_cast<int>(m_assets.size()); ++i) {
            if (m_assets[static_cast<std::size_t>(i)].relativePath
                == importedRelative) {
                SelectIndex(i);
                break;
            }
        }
        return true;
    }

    fs::path destination = fs::path(CurrentPath()) / source.filename();
    if (fs::exists(destination, ec)) {
        const std::string stem = source.stem().string();
        const std::string copiedExtension = source.extension().string();
        int suffix = 1;
        do {
            destination = fs::path(CurrentPath())
                / (stem + "_" + std::to_string(suffix) + copiedExtension);
            ++suffix;
        } while (fs::exists(destination, ec));
    }

    fs::copy_file(source, destination, fs::copy_options::none, ec);
    if (ec) {
        if (error) *error = "Could not import asset: " + sourcePath;
        return false;
    }
    m_lastImportMessage = "Imported asset: " + destination.filename().string();
    return Refresh(m_rootPath, error);
}

std::vector<std::string> EditorAssets::ContentFolderPaths() const {
    std::vector<std::string> folders{std::string()};
    std::error_code ec;
    if (m_rootPath.empty() || !fs::is_directory(m_rootPath, ec)) return folders;
    for (fs::recursive_directory_iterator it(
             m_rootPath, fs::directory_options::skip_permission_denied, ec), end;
         it != end; it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        if (!it->is_directory(ec)) continue;
        const fs::path relative = fs::relative(it->path(), m_rootPath, ec);
        if (!ec) folders.push_back(NormalizeSlashes(relative.string()));
    }
    std::sort(folders.begin() + 1, folders.end());
    return folders;
}

std::vector<std::string> EditorAssets::ContentAssetPaths(Type type) const {
    std::vector<std::string> paths;
    std::error_code ec;
    if (m_rootPath.empty() || !fs::is_directory(m_rootPath, ec)) return paths;
    for (fs::recursive_directory_iterator it(
             m_rootPath, fs::directory_options::skip_permission_denied, ec), end;
         it != end; it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        if (!it->is_regular_file(ec)
            || ClassifyExtension(Lower(it->path().extension().string())) != type)
            continue;
        const fs::path relative = fs::relative(it->path(), m_rootPath, ec);
        if (!ec) paths.push_back(NormalizeSlashes(relative.string()));
    }
    std::sort(paths.begin(), paths.end());
    return paths;
}

bool EditorAssets::ReimportSelectedStaticMesh(std::string* error) {
    m_lastImportMessage.clear();
    const Asset* selected = SelectedAsset();
    if (!selected || selected->type != Type::Model
        || Lower(fs::path(selected->relativePath).extension().string())
               != ".3dgmesh") {
        if (error) *error = "Select a native .3dgmesh asset to reimport.";
        return false;
    }
    if (!m_assetRegistry) {
        if (error) *error = "The project asset registry is unavailable.";
        return false;
    }
    const engine::AssetRegistryEntry* entry =
        m_assetRegistry->FindByPath(selected->relativePath);
    if (!entry || entry->sourcePath.empty()) {
        if (error) *error = "The selected mesh has no recorded import source.";
        return false;
    }
    std::error_code ec;
    if (!fs::is_regular_file(entry->sourcePath, ec)) {
        if (error) *error = "The original mesh source is unavailable: "
            + entry->sourcePath;
        return false;
    }

    const std::string selectedRelative = selected->relativePath;
    const std::string sourcePath = entry->sourcePath;
    const fs::path destination = FullPathForRelative(selectedRelative);
    engine::StaticMeshImportResult result;
    if (!engine::ImportStaticMeshToAsset(
            sourcePath, destination.string(), m_rootPath,
            m_staticMeshImportOptions, m_assetRegistry, &result, error)) {
        return false;
    }
    m_lastImportMessage = "Reimported static mesh: " + destination.filename().string()
        + " (" + std::to_string(result.vertexCount) + " vertices, "
        + std::to_string(result.triangleCount) + " triangles)";
    if (!Refresh(m_rootPath, error)) return false;
    for (int i = 0; i < static_cast<int>(m_assets.size()); ++i) {
        if (m_assets[static_cast<std::size_t>(i)].relativePath == selectedRelative) {
            SelectIndex(i);
            break;
        }
    }
    return true;
}

bool EditorAssets::ReimportSelectedSkeletalAssets(std::string* error) {
    m_lastImportMessage.clear();
    const Asset* selected = SelectedAsset();
    if (!selected || selected->type != Type::SkeletalModel
        || Lower(fs::path(selected->relativePath).extension().string())
               != ".3dgskmesh") {
        if (error) *error = "Select a native .3dgskmesh asset to reimport.";
        return false;
    }
    if (!m_assetRegistry) {
        if (error) *error = "The project asset registry is unavailable.";
        return false;
    }
    const engine::AssetRegistryEntry* entry =
        m_assetRegistry->FindByPath(selected->relativePath);
    if (!entry || entry->sourcePath.empty()) {
        if (error) *error = "The selected skeletal mesh has no recorded source.";
        return false;
    }
    const std::string sourcePath = entry->sourcePath;
    std::error_code ec;
    if (!fs::is_regular_file(sourcePath, ec)) {
        if (error) *error = "The original skeletal source is unavailable: "
            + sourcePath;
        return false;
    }
    const fs::path destination = FullPathForRelative(selected->relativePath);
    fs::path base = destination;
    base.replace_extension();
    engine::SkeletalImportResult result;
    engine::SkeletalImportOptions reimportOptions = m_skeletalImportOptions;
    reimportOptions.importSkeletalMesh = true;
    reimportOptions.importSkeleton = true;
    reimportOptions.reuseSkeletonPath.clear();
    if (!engine::ImportSkeletalAssetsToContent(
            sourcePath, base.string(), m_rootPath, reimportOptions,
            m_assetRegistry, &result, error))
        return false;
    m_lastImportMessage = "Reimported skeletal assets: "
        + std::to_string(result.boneCount) + " bones, "
        + std::to_string(result.vertexCount) + " vertices, "
        + std::to_string(result.animationPaths.size()) + " animation clip(s)";
    const std::string selectedRelative = selected->relativePath;
    if (!Refresh(m_rootPath, error)) return false;
    for (int i = 0; i < static_cast<int>(m_assets.size()); ++i) {
        if (m_assets[static_cast<std::size_t>(i)].relativePath == selectedRelative) {
            SelectIndex(i);
            break;
        }
    }
    return true;
}

bool EditorAssets::ReimportSelectedTexture(std::string* error) {
    m_lastImportMessage.clear();
    const Asset* selected = SelectedAsset();
    if (!selected || selected->type != Type::Texture
        || Lower(fs::path(selected->relativePath).extension().string())
               != ".3dgtex") {
        if (error) *error = "Select a native .3dgtex asset to reimport.";
        return false;
    }
    if (!m_assetRegistry) {
        if (error) *error = "The project asset registry is unavailable.";
        return false;
    }
    const engine::AssetRegistryEntry* entry =
        m_assetRegistry->FindByPath(selected->relativePath);
    if (!entry || entry->sourcePath.empty()) {
        if (error) *error = "The selected texture has no recorded import source.";
        return false;
    }
    std::error_code ec;
    if (!fs::is_regular_file(entry->sourcePath, ec)) {
        if (error) *error = "The original texture source is unavailable: "
            + entry->sourcePath;
        return false;
    }
    const std::string selectedRelative = selected->relativePath;
    const fs::path destination = FullPathForRelative(selectedRelative);
    engine::TextureImportResult result;
    if (!engine::ImportTextureToAsset(
            entry->sourcePath, destination.string(), m_rootPath,
            m_textureImportOptions, m_assetRegistry, &result, error))
        return false;
    m_lastImportMessage = "Reimported texture: "
        + destination.filename().string() + " ("
        + std::to_string(result.width) + "x"
        + std::to_string(result.height) + ")";
    if (!Refresh(m_rootPath, error)) return false;
    for (int i = 0; i < static_cast<int>(m_assets.size()); ++i) {
        if (m_assets[static_cast<std::size_t>(i)].relativePath
            == selectedRelative) {
            SelectIndex(i);
            break;
        }
    }
    return true;
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
        m_clipboardIsCut = false;
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
        m_clipboardIsCut = false;
        return true;
    }

    if (error) *error = "No content entry selected.";
    return false;
}

bool EditorAssets::CutSelected(std::string *error)
{
    // Same selection capture as Copy, but flag it so Paste moves (and clears) it.
    if (!CopySelected(error)) return false;
    m_clipboardIsCut = true;
    return true;
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
            if (error) *error = m_clipboardIsCut
                ? "Cannot move a folder inside itself."
                : "Cannot paste a folder inside itself.";
            return false;
        }
    }

    if (m_clipboardIsCut) {
        // Move: rename first (cheap, same-volume); fall back to copy + delete across
        // volumes. A cut can only be pasted once, so clear the clipboard afterwards.
        fs::rename(source, destination, ec);
        if (ec) {
            ec.clear();
            if (m_clipboardIsFolder) {
                fs::copy(source, destination,
                    fs::copy_options::recursive | fs::copy_options::copy_symlinks, ec);
            } else {
                fs::copy_file(source, destination, fs::copy_options::none, ec);
            }
            if (!ec) {
                std::error_code removeError;
                fs::remove_all(source, removeError);
            }
        }
        if (ec) {
            if (error) *error = "Could not move Content entry.";
            return false;
        }
        m_clipboardRelativePath.clear();
        m_clipboardIsCut = false;
    } else if (m_clipboardIsFolder) {
        fs::copy(source, destination,
            fs::copy_options::recursive | fs::copy_options::copy_symlinks, ec);
        if (ec) { if (error) *error = "Could not paste copied Content entry."; return false; }
    } else {
        fs::copy_file(source, destination, fs::copy_options::none, ec);
        if (ec) { if (error) *error = "Could not paste copied Content entry."; return false; }
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
        case Type::Skeleton: return "Skeleton";
        case Type::Animation: return "Animation";
        case Type::Material: return "Material";
        case Type::Texture: return "Texture";
        case Type::Shader: return "Shader";
        case Type::Audio: return "Audio";
        case Type::Scene: return "Scene";
        case Type::Particle: return "Particle";
        case Type::ParticleEffect: return "Particle Effect";
        case Type::Hud: return "HUD";
        case Type::Character: return "Character";
        case Type::AnimationClip: return "Animation Clip";
        case Type::AnimationGraph: return "Animation Graph";
        case Type::BehaviorGraph: return "Behavior Tree";
        case Type::Prefab: return "Prefab";
        case Type::Script: return "Script";
        case Type::World: return "World";
        case Type::Foliage: return "Foliage";
        case Type::Other: return "Other";
    }
    return "Other";
}

EditorAssets::Type EditorAssets::ClassifyExtension(const std::string &extension)
{
    if (extension == ".3dgmesh") {
        return Type::Model;
    }
    if (extension == ".3dgskmesh") {
        return Type::SkeletalModel;
    }
    if (extension == ".3dgskel") {
        return Type::Skeleton;
    }
    if (extension == ".3dganim") {
        return Type::Animation;
    }
    if (extension == ".obj" || extension == ".fbx" || extension == ".gltf"
        || extension == ".glb" || extension == ".dae" || extension == ".ply"
        || extension == ".stl") {
        return Type::Model;
    }
    if (extension == ".3dgmat") {
        return Type::Material;
    }
    if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".tga"
        || extension == ".raw" || extension == ".r16" || extension == ".exr"
        || extension == ".3dgtex") {
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
    if (extension == ".3dgprefab") {
        return Type::Prefab;
    }
    if (extension == ".3dgclip") {
        return Type::AnimationClip;
    }
    if (extension == ".3dggraph") {
        return Type::AnimationGraph;
    }
    if (extension == ".btgraph") {
        return Type::BehaviorGraph;
    }
    if (extension == ".3dgworld") {
        return Type::World;
    }
    if (extension == ".3dgfoliage") {
        return Type::Foliage;
    }
    if (extension == ".h" || extension == ".hpp" || extension == ".lua"
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
