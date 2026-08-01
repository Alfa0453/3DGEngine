#include "engine/assets/AssetCooker.h"

#include "engine/assets/AssetRegistry.h"
#include "engine/scene/WorldManifest.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_set>

namespace engine {
namespace {

void SetError(std::string* error, const std::string& message) {
    if (error) *error = message;
}

bool IsWithin(const std::filesystem::path& child,
              const std::filesystem::path& parent) {
    const std::filesystem::path absoluteChild =
        std::filesystem::absolute(child).lexically_normal();
    const std::filesystem::path absoluteParent =
        std::filesystem::absolute(parent).lexically_normal();
    auto childIt = absoluteChild.begin();
    for (auto parentIt = absoluteParent.begin();
         parentIt != absoluteParent.end(); ++parentIt, ++childIt) {
        if (childIt == absoluteChild.end() || *childIt != *parentIt)
            return false;
    }
    return true;
}

bool VirtualPathToRelative(const std::string& virtualPath,
                           std::filesystem::path* relative) {
    if (!relative) return false;
    const std::string normalized =
        AssetRegistry::NormalizeVirtualPath(virtualPath);
    if (normalized.rfind("/Game/", 0) != 0 || normalized.size() <= 6)
        return false;
    const std::filesystem::path value(normalized.substr(6));
    if (value.is_absolute()) return false;
    for (const std::filesystem::path& part : value) {
        if (part == "..") return false;
    }
    *relative = value.lexically_normal();
    return !relative->empty();
}

std::uint64_t HashFile(const std::filesystem::path& path,
                       std::uint64_t* size, std::string* error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        SetError(error, "Could not read asset while cooking: " + path.string());
        return 0;
    }
    constexpr std::uint64_t offset = 1469598103934665603ull;
    constexpr std::uint64_t prime = 1099511628211ull;
    std::uint64_t hash = offset;
    std::uint64_t bytes = 0;
    char buffer[64 * 1024];
    while (input) {
        input.read(buffer, sizeof(buffer));
        const std::streamsize count = input.gcount();
        for (std::streamsize i = 0; i < count; ++i) {
            hash ^= static_cast<unsigned char>(buffer[i]);
            hash *= prime;
        }
        bytes += static_cast<std::uint64_t>(count);
    }
    if (!input.eof()) {
        SetError(error, "Could not finish reading asset while cooking: "
            + path.string());
        return 0;
    }
    if (size) *size = bytes;
    return hash;
}

bool ReadSceneMetadata(const std::filesystem::path& path,
                       AssetHandle* sceneId,
                       std::vector<AssetHandle>* dependencies,
                       std::string* error) {
    std::ifstream input(path);
    std::string magic;
    int version = 0;
    std::string idText;
    if (!input || !(input >> magic >> version >> idText)
        || magic != "3DGRuntimeScene" || version < 73
        || !AssetHandle::Parse(idText, sceneId)) {
        SetError(error,
            "Cook requires a current exported runtime scene with a stable ID.");
        return false;
    }
    std::string token;
    while (input >> token) {
        if (token != "ASSET_DEPS") continue;
        std::size_t count = 0;
        input >> count;
        if (!input || count > 4096) {
            SetError(error, "Runtime scene dependency list is invalid.");
            return false;
        }
        dependencies->resize(count);
        for (AssetHandle& dependency : *dependencies) {
            input >> idText;
            if (!input || !AssetHandle::Parse(idText, &dependency)) {
                SetError(error, "Runtime scene contains an invalid dependency ID.");
                return false;
            }
        }
        return true;
    }
    SetError(error, "Runtime scene does not contain asset dependency metadata.");
    return false;
}

bool CollectDependency(AssetHandle id, const AssetRegistry& registry,
                       std::unordered_set<AssetHandle, AssetHandleHash>* visiting,
                       std::unordered_set<AssetHandle, AssetHandleHash>* visited,
                       std::vector<const AssetRegistryEntry*>* ordered,
                       std::string* error) {
    if (visited->find(id) != visited->end()) return true;
    const AssetRegistryEntry* entry = registry.Find(id);
    if (!entry) {
        SetError(error, "Required asset is missing from the registry: "
            + id.ToString());
        return false;
    }
    if (!visiting->insert(id).second) {
        SetError(error, "Asset dependency cycle includes: " + entry->virtualPath);
        return false;
    }
    for (AssetHandle dependency : entry->dependencies) {
        if (!CollectDependency(
                dependency, registry, visiting, visited, ordered, error))
            return false;
    }
    visiting->erase(id);
    visited->insert(id);
    ordered->push_back(entry);
    return true;
}

bool ReplaceOutput(const std::filesystem::path& staging,
                   const std::filesystem::path& output, std::string* error) {
    const std::filesystem::path parent =
        std::filesystem::absolute(output).lexically_normal().parent_path();
    if (parent.empty() || !IsWithin(output, parent) || output == output.root_path()) {
        SetError(error, "Cook output path is unsafe.");
        return false;
    }
    std::error_code ec;
    std::filesystem::remove_all(output, ec);
    if (ec) {
        SetError(error, "Could not replace previous cook output: " + ec.message());
        return false;
    }
    std::filesystem::rename(staging, output, ec);
    if (ec) {
        SetError(error, "Could not commit cook output: " + ec.message());
        return false;
    }
    return true;
}

} // namespace

bool AssetCooker::CookRuntimeScene(const std::string& contentRoot,
                                   const std::string& runtimeScenePath,
                                   const std::string& outputRoot,
                                   const AssetRegistry& registry,
                                   AssetCookResult* result,
                                   std::string* error) {
    const std::filesystem::path content =
        std::filesystem::absolute(contentRoot).lexically_normal();
    const std::filesystem::path scene =
        std::filesystem::absolute(runtimeScenePath).lexically_normal();
    const std::filesystem::path output =
        std::filesystem::absolute(outputRoot).lexically_normal();
    const std::filesystem::path staging =
        output.parent_path() / (output.filename().string() + ".staging");
    if (!std::filesystem::is_directory(content)
        || !std::filesystem::is_regular_file(scene)) {
        SetError(error, "Content root or exported runtime scene does not exist.");
        return false;
    }
    if (IsWithin(output, content) || IsWithin(content, output)
        || output == output.root_path() || staging == staging.root_path()) {
        SetError(error, "Cook output must be outside the source Content folder.");
        return false;
    }

    AssetHandle sceneId;
    std::vector<AssetHandle> roots;
    if (!ReadSceneMetadata(scene, &sceneId, &roots, error)) return false;

    std::unordered_set<AssetHandle, AssetHandleHash> visiting;
    std::unordered_set<AssetHandle, AssetHandleHash> visited;
    std::vector<const AssetRegistryEntry*> ordered;
    for (AssetHandle root : roots) {
        if (!CollectDependency(
                root, registry, &visiting, &visited, &ordered, error))
            return false;
    }
    std::sort(ordered.begin(), ordered.end(),
        [](const AssetRegistryEntry* a, const AssetRegistryEntry* b) {
            return a->virtualPath < b->virtualPath;
        });

    std::error_code ec;
    std::filesystem::remove_all(staging, ec);
    ec.clear();
    const std::filesystem::path cookedContent = staging / "Content";
    const std::filesystem::path cookedScene =
        cookedContent / "Scenes" / scene.filename();
    std::filesystem::create_directories(cookedScene.parent_path(), ec);
    if (ec) {
        SetError(error, "Could not create cook staging folder: " + ec.message());
        return false;
    }
    std::filesystem::copy_file(
        scene, cookedScene, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        const std::string copyError = ec.message();
        std::error_code cleanupError;
        std::filesystem::remove_all(staging, cleanupError);
        SetError(error, "Could not copy runtime scene: " + copyError);
        return false;
    }

    // Lua source is executable game content rather than a C++ build input. Keep
    // its Content/Scripts-relative path intact so the serialized Script
    // component resolves identically in Editor Play and in the packaged player.
    // Script files are tiny, and copying the folder also supports scripts loaded
    // dynamically by another Lua script rather than referenced by one scene.
    const std::filesystem::path authoredScripts = content / "Scripts";
    if (std::filesystem::is_directory(authoredScripts, ec)) {
        ec.clear();
        for (std::filesystem::recursive_directory_iterator it(authoredScripts, ec), end;
             !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec) || it->path().extension() != ".lua") continue;
            const std::filesystem::path relative =
                std::filesystem::relative(it->path(), content, ec);
            if (ec) break;
            const std::filesystem::path destination = cookedContent / relative;
            std::filesystem::create_directories(destination.parent_path(), ec);
            if (!ec) {
                std::filesystem::copy_file(
                    it->path(), destination,
                    std::filesystem::copy_options::overwrite_existing, ec);
            }
            if (ec) break;
        }
        if (ec) {
            const std::string copyError = ec.message();
            std::error_code cleanupError;
            std::filesystem::remove_all(staging, cleanupError);
            SetError(error, "Could not copy Lua scripts into cook output: "
                + copyError);
            return false;
        }
    } else {
        ec.clear();
    }

    AssetRegistry cookedRegistry;
    std::vector<CookedAssetEntry> cookedAssets;
    for (const AssetRegistryEntry* entry : ordered) {
        std::filesystem::path relative;
        if (!VirtualPathToRelative(entry->virtualPath, &relative)) {
            std::filesystem::remove_all(staging, ec);
            SetError(error, "Asset has an unsafe virtual path: "
                + entry->virtualPath);
            return false;
        }
        const std::filesystem::path source = content / relative;
        const std::filesystem::path destination = cookedContent / relative;
        if (!IsWithin(source, content) || !std::filesystem::is_regular_file(source)) {
            std::filesystem::remove_all(staging, ec);
            SetError(error, "Required asset file is missing: " + source.string());
            return false;
        }
        std::filesystem::create_directories(destination.parent_path(), ec);
        if (!ec) {
            std::filesystem::copy_file(source, destination,
                std::filesystem::copy_options::overwrite_existing, ec);
        }
        if (ec) {
            const std::string copyError = ec.message();
            std::error_code cleanupError;
            std::filesystem::remove_all(staging, cleanupError);
            SetError(error, "Could not copy cooked asset: " + copyError);
            return false;
        }
        std::uint64_t size = 0;
        const std::uint64_t hash = HashFile(source, &size, error);
        if (hash == 0 && size != 0) {
            std::filesystem::remove_all(staging, ec);
            return false;
        }
        cookedAssets.push_back(
            {entry->id, entry->type, entry->virtualPath, size, hash});
        std::string registryError;
        if (!cookedRegistry.Register(*entry, &registryError)) {
            std::filesystem::remove_all(staging, ec);
            SetError(error, "Could not create cooked registry: " + registryError);
            return false;
        }
    }
    AssetRegistryEntry sceneEntry;
    sceneEntry.id = sceneId;
    sceneEntry.type = AssetType::Scene;
    sceneEntry.virtualPath = AssetRegistry::NormalizeVirtualPath(
        (std::filesystem::path("Scenes") / scene.filename()).generic_string());
    sceneEntry.importerVersion = 1;
    sceneEntry.dependencies = roots;
    std::string registryError;
    if (!cookedRegistry.Register(sceneEntry, &registryError)
        || !cookedRegistry.Save(
            AssetRegistry::DefaultRegistryPath(cookedContent.string()),
            &registryError)) {
        std::filesystem::remove_all(staging, ec);
        SetError(error, "Could not save cooked registry: " + registryError);
        return false;
    }

    std::ofstream manifest(staging / "CookManifest.3dgmanifest",
                           std::ios::trunc);
    if (!manifest) {
        std::filesystem::remove_all(staging, ec);
        SetError(error, "Could not create cook manifest.");
        return false;
    }
    manifest << "3DGCookManifest 1\n";
    manifest << "scene " << sceneId.ToString() << ' '
             << std::quoted(
                    (std::filesystem::path("Content") / "Scenes"
                     / scene.filename()).generic_string())
             << '\n';
    manifest << "assets " << cookedAssets.size() << '\n';
    for (const CookedAssetEntry& asset : cookedAssets) {
        manifest << "asset " << asset.id.ToString() << ' '
                 << static_cast<std::uint32_t>(asset.type) << ' '
                 << std::quoted(asset.virtualPath) << ' '
                 << asset.fileSize << ' ' << asset.contentHash << '\n';
    }
    manifest.close();
    if (!manifest) {
        std::filesystem::remove_all(staging, ec);
        SetError(error, "Could not finish writing cook manifest.");
        return false;
    }

    std::ofstream config(staging / "player.cfg", std::ios::trunc);
    config << "window.width = 1280\n"
           << "window.height = 720\n"
           << "window.vsync = true\n"
           << "player.scene = "
           << (std::filesystem::path("Content") / "Scenes"
               / scene.filename()).generic_string()
           << '\n';
    config.close();
    if (!config) {
        std::filesystem::remove_all(staging, ec);
        SetError(error, "Could not create cooked player configuration.");
        return false;
    }

    if (!ReplaceOutput(staging, output, error)) {
        std::filesystem::remove_all(staging, ec);
        return false;
    }
    if (result) {
        result->outputRoot = output.string();
        result->runtimeScenePath =
            (output / "Content" / "Scenes" / scene.filename()).string();
        result->assets = std::move(cookedAssets);
    }
    SetError(error, {});
    return true;
}

bool AssetCooker::CookRuntimeWorld(const std::string& contentRoot,
                                   const std::string& runtimeWorldPath,
                                   const std::string& outputRoot,
                                   const AssetRegistry& registry,
                                   AssetCookResult* result,
                                   std::string* error) {
    namespace fs = std::filesystem;
    const fs::path content = fs::absolute(contentRoot).lexically_normal();
    const fs::path worldPath = fs::absolute(runtimeWorldPath).lexically_normal();
    const fs::path output = fs::absolute(outputRoot).lexically_normal();
    const fs::path worldDir = worldPath.parent_path();
    const fs::path staging =
        output.parent_path() / (output.filename().string() + ".worldstaging");

    WorldManifest world;
    if (!LoadWorldManifest(worldPath.string(), &world, error)) return false;
    if (world.persistentScenePath.empty()) {
        SetError(error, "World has no persistent runtime scene.");
        return false;
    }
    if (IsWithin(output, content) || IsWithin(content, output)
        || output == output.root_path() || staging == staging.root_path()) {
        SetError(error, "Cook output must be outside the source Content folder.");
        return false;
    }

    const auto resolveScene = [&](const std::string& value) {
        const fs::path path(value);
        return fs::absolute(path.is_absolute() ? path : worldDir / path)
            .lexically_normal();
    };

    std::vector<fs::path> scenes;
    scenes.push_back(resolveScene(world.persistentScenePath));
    for (const LevelRef& level : world.levels)
        scenes.push_back(resolveScene(level.scenePath));

    std::unordered_set<std::string> cookedNames;
    std::vector<AssetHandle> sceneIds;
    std::vector<std::vector<AssetHandle>> sceneRoots;
    sceneIds.reserve(scenes.size());
    sceneRoots.reserve(scenes.size());
    for (const fs::path& scene : scenes) {
        if (!fs::is_regular_file(scene)) {
            SetError(error, "World runtime scene is missing: " + scene.string());
            return false;
        }
        const std::string name = scene.filename().string();
        if (!cookedNames.insert(name).second) {
            SetError(error, "World contains runtime scenes with the same file name: "
                + name);
            return false;
        }
        AssetHandle sceneId;
        std::vector<AssetHandle> roots;
        if (!ReadSceneMetadata(scene, &sceneId, &roots, error)) return false;
        sceneIds.push_back(sceneId);
        sceneRoots.push_back(std::move(roots));
    }

    // Reuse the single-scene cook to create an atomic, relocatable baseline
    // (scripts, directory layout, and validation), then replace its registry
    // with the union required by the complete world.
    std::error_code ec;
    fs::remove_all(staging, ec);
    AssetCookResult baseResult;
    if (!CookRuntimeScene(content.string(), scenes.front().string(),
            staging.string(), registry, &baseResult, error)) {
        return false;
    }

    std::unordered_set<AssetHandle, AssetHandleHash> visiting;
    std::unordered_set<AssetHandle, AssetHandleHash> visited;
    std::vector<const AssetRegistryEntry*> ordered;
    for (const auto& roots : sceneRoots) {
        for (AssetHandle root : roots) {
            if (!CollectDependency(
                    root, registry, &visiting, &visited, &ordered, error)) {
                fs::remove_all(staging, ec);
                return false;
            }
        }
    }
    std::sort(ordered.begin(), ordered.end(),
        [](const AssetRegistryEntry* a, const AssetRegistryEntry* b) {
            return a->virtualPath < b->virtualPath;
        });

    const fs::path cookedContent = staging / "Content";
    const fs::path cookedScenes = cookedContent / "Scenes";
    fs::create_directories(cookedScenes, ec);
    AssetRegistry cookedRegistry;
    std::vector<CookedAssetEntry> cookedAssets;
    for (const AssetRegistryEntry* entry : ordered) {
        fs::path relative;
        if (!VirtualPathToRelative(entry->virtualPath, &relative)) {
            fs::remove_all(staging, ec);
            SetError(error, "Asset has an unsafe virtual path: "
                + entry->virtualPath);
            return false;
        }
        const fs::path source = content / relative;
        const fs::path destination = cookedContent / relative;
        if (!IsWithin(source, content) || !fs::is_regular_file(source)) {
            fs::remove_all(staging, ec);
            SetError(error, "Required asset file is missing: " + source.string());
            return false;
        }
        fs::create_directories(destination.parent_path(), ec);
        if (!ec) {
            fs::copy_file(source, destination,
                fs::copy_options::overwrite_existing, ec);
        }
        if (ec) {
            const std::string copyError = ec.message();
            fs::remove_all(staging, ec);
            SetError(error, "Could not copy cooked asset: " + copyError);
            return false;
        }
        std::uint64_t size = 0;
        const std::uint64_t hash = HashFile(source, &size, error);
        if (hash == 0 && size != 0) {
            fs::remove_all(staging, ec);
            return false;
        }
        cookedAssets.push_back(
            {entry->id, entry->type, entry->virtualPath, size, hash});
        std::string registryError;
        if (!cookedRegistry.Register(*entry, &registryError)) {
            fs::remove_all(staging, ec);
            SetError(error, "Could not create cooked registry: " + registryError);
            return false;
        }
    }

    WorldManifest cookedWorld = world;
    if (!cookedWorld.id.Valid()) cookedWorld.id = AssetHandle::Generate();
    cookedWorld.persistentScenePath = scenes.front().filename().string();
    for (std::size_t i = 0; i < cookedWorld.levels.size(); ++i)
        cookedWorld.levels[i].scenePath = scenes[i + 1].filename().string();

    for (std::size_t i = 0; i < scenes.size(); ++i) {
        const fs::path destination = cookedScenes / scenes[i].filename();
        fs::copy_file(scenes[i], destination,
            fs::copy_options::overwrite_existing, ec);
        if (ec) {
            const std::string copyError = ec.message();
            fs::remove_all(staging, ec);
            SetError(error, "Could not copy world runtime scene: " + copyError);
            return false;
        }
        AssetRegistryEntry sceneEntry;
        sceneEntry.id = sceneIds[i];
        sceneEntry.type = AssetType::Scene;
        sceneEntry.virtualPath = AssetRegistry::NormalizeVirtualPath(
            (fs::path("Scenes") / scenes[i].filename()).generic_string());
        sceneEntry.importerVersion = 1;
        sceneEntry.dependencies = sceneRoots[i];
        std::string registryError;
        if (!cookedRegistry.Register(sceneEntry, &registryError)) {
            fs::remove_all(staging, ec);
            SetError(error, "Could not register world scene: " + registryError);
            return false;
        }
    }

    const fs::path cookedWorldPath = cookedScenes / "world.3dgworld";
    if (!SaveWorldManifest(cookedWorldPath.string(), cookedWorld, error)) {
        fs::remove_all(staging, ec);
        return false;
    }
    AssetRegistryEntry worldEntry;
    worldEntry.id = cookedWorld.id;
    worldEntry.type = AssetType::World;
    worldEntry.virtualPath = "/Game/Scenes/world.3dgworld";
    worldEntry.importerVersion = 1;
    worldEntry.dependencies = sceneIds;
    std::string registryError;
    if (!cookedRegistry.Register(worldEntry, &registryError)
        || !cookedRegistry.Save(
            AssetRegistry::DefaultRegistryPath(cookedContent.string()),
            &registryError)) {
        fs::remove_all(staging, ec);
        SetError(error, "Could not save cooked world registry: " + registryError);
        return false;
    }

    std::ofstream manifest(staging / "CookManifest.3dgmanifest", std::ios::trunc);
    manifest << "3DGCookManifest 1\n"
             << "world " << worldEntry.id.ToString() << ' '
             << std::quoted(
                    (fs::path("Content") / "Scenes" / "world.3dgworld")
                        .generic_string())
             << '\n'
             << "scenes " << scenes.size() << '\n';
    for (std::size_t i = 0; i < scenes.size(); ++i) {
        manifest << "scene " << sceneIds[i].ToString() << ' '
                 << std::quoted(
                        (fs::path("Content") / "Scenes" / scenes[i].filename())
                            .generic_string())
                 << '\n';
    }
    manifest << "assets " << cookedAssets.size() << '\n';
    for (const CookedAssetEntry& asset : cookedAssets) {
        manifest << "asset " << asset.id.ToString() << ' '
                 << static_cast<std::uint32_t>(asset.type) << ' '
                 << std::quoted(asset.virtualPath) << ' '
                 << asset.fileSize << ' ' << asset.contentHash << '\n';
    }
    manifest.close();
    if (!manifest) {
        fs::remove_all(staging, ec);
        SetError(error, "Could not finish writing world cook manifest.");
        return false;
    }

    std::ofstream config(staging / "player.cfg", std::ios::trunc);
    config << "window.width = 1280\n"
           << "window.height = 720\n"
           << "window.vsync = true\n"
           << "player.scene = Content/Scenes/world.3dgworld\n";
    config.close();
    if (!config) {
        fs::remove_all(staging, ec);
        SetError(error, "Could not create cooked world player configuration.");
        return false;
    }

    if (!ReplaceOutput(staging, output, error)) {
        fs::remove_all(staging, ec);
        return false;
    }
    if (result) {
        result->outputRoot = output.string();
        result->runtimeScenePath =
            (output / "Content" / "Scenes" / "world.3dgworld").string();
        result->assets = std::move(cookedAssets);
    }
    SetError(error, {});
    return true;
}

} // namespace engine
