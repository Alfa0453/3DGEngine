#include "engine/assets/AssetCooker.h"

#include "engine/assets/AssetRegistry.h"

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

} // namespace engine
