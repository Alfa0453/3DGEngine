#include "engine/assets/AssetRegistry.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_set>
#include <utility>

namespace engine {
namespace {

std::string PathKey(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

void SetError(std::string* error, const std::string& message) {
    if (error) *error = message;
}

bool IsNativePath(const std::filesystem::path& path) {
    return NativeAssetTypeFromExtension(path.extension().string()) != AssetType::Unknown
        || path.extension() == ".3dgasset";
}

AssetType AuthoredAssetType(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (extension == ".3dgcharacter") return AssetType::Character;
    if (extension == ".3dgclip") return AssetType::AnimationClip;
    if (extension == ".3dggraph") return AssetType::AnimationGraph;
    if (extension == ".3dgmat") return AssetType::Material;
    if (extension == ".3dgshader") return AssetType::Shader;
    if (extension == ".particle") return AssetType::Particle;
    if (extension == ".particlefx") return AssetType::ParticleEffect;
    if (extension == ".hud") return AssetType::Hud;
    if (extension == ".btgraph") return AssetType::BehaviorTree;
    if (extension == ".3dgaudio" || extension == ".3dgmixer"
        || extension == ".3dgmusic") return AssetType::Audio;
    if (extension == ".scene") return AssetType::Scene;
    if (extension == ".3dgragdoll") return AssetType::Ragdoll;
    if (extension == ".3dgretarget") return AssetType::AnimationRetarget;
    if (extension == ".3dgability") return AssetType::Ability;
    if (extension == ".3dgprefab") return AssetType::Prefab;
    if (extension == ".3dgweather") return AssetType::Weather;
    if (extension == ".3dgbuilding") return AssetType::Building;
    if (extension == ".3dgroad") return AssetType::Road;
    if (extension == ".3dgfence") return AssetType::FenceWall;
    if (extension == ".3dgdestruction") return AssetType::Destruction;
    if (extension == ".3dginteraction") return AssetType::Interaction;
    if (extension == ".3dgportal") return AssetType::Portal;
    if (extension == ".3dgquest") return AssetType::Quest;
    if (extension == ".3dgdialogue") return AssetType::Dialogue;
    if (extension == ".3dgitem") return AssetType::Item;
    if (extension == ".3dgcombat") return AssetType::Combat;
    if (extension == ".3dgspawn") return AssetType::Spawn;
    return AssetType::Unknown;
}

bool ReadAuthoredMetadata(const std::filesystem::path& path,
                          AssetType type, NativeAssetHeader* header,
                          std::string* error) {
    std::ifstream input(path);
    std::string magic;
    int version = 0;
    std::string idText;
    if (!header || !(input >> magic >> version >> idText)
        || !AssetHandle::Parse(idText, &header->id)) {
        SetError(error, "Authored asset has no stable ID.");
        return false;
    }
    const bool validMagic =
        (type == AssetType::Character && magic == "3DG_CHARACTER")
        || (type == AssetType::AnimationClip && magic == "3DG_CLIP")
        || (type == AssetType::AnimationGraph && magic == "3DG_GRAPH")
        || (type == AssetType::Material && magic == "3DG_MATERIAL")
        || (type == AssetType::Shader && magic == "3DG_SHADER")
        || (type == AssetType::Particle && magic == "3DG_PARTICLE")
        || (type == AssetType::ParticleEffect
            && magic == "3DG_PARTICLE_EFFECT")
        || (type == AssetType::Hud && magic == "3DG_HUD")
        || (type == AssetType::BehaviorTree
            && magic == "3DG_BEHAVIOR_GRAPH")
        || (type == AssetType::Audio
            && (magic == "3DGAUDIO_CUE"
                || magic == "3DGAUDIO_MIXER"
                || magic == "3DGAUDIO_MUSIC"))
        || (type == AssetType::Scene
            && (magic == "3DGEditorScene" || magic == "3DGRuntimeScene"))
        || (type == AssetType::Ragdoll && magic == "3DG_RAGDOLL")
        || (type == AssetType::AnimationRetarget && magic == "3DG_RETARGET")
        || (type == AssetType::Ability && magic == "3DG_ABILITY")
        || (type == AssetType::Prefab && magic == "3DG_PREFAB")
        || (type == AssetType::Weather && magic == "3DG_WEATHER")
        || (type == AssetType::Building && magic == "3DG_BUILDING")
        || (type == AssetType::Road && magic == "3DG_ROAD")
        || (type == AssetType::FenceWall && magic == "3DGFenceWall")
        || (type == AssetType::Destruction && magic == "3DG_DESTRUCTION")
        || (type == AssetType::Interaction && magic == "3DG_INTERACTION")
        || (type == AssetType::Portal && magic == "3DG_PORTAL")
        || (type == AssetType::Quest && magic == "3DG_QUEST")
        || (type == AssetType::Dialogue && magic == "3DG_DIALOGUE")
        || (type == AssetType::Item && magic == "3DG_ITEM")
        || (type == AssetType::Combat && magic == "3DG_COMBAT")
        || (type == AssetType::Spawn && magic == "3DG_SPAWN");
    if (!validMagic || version < 1) {
        SetError(error, "Authored asset metadata is invalid.");
        return false;
    }
    header->type = type;
    header->assetVersion = static_cast<std::uint32_t>(version);
    header->importerVersion = 1;
    header->dependencies.clear();
    std::string token;
    while (input >> token) {
        if (token != "ASSET_DEPS") continue;
        std::size_t count = 0;
        input >> count;
        if (!input || count > 4096) {
            SetError(error, "Authored asset dependency metadata is invalid.");
            return false;
        }
        header->dependencies.resize(count);
        for (AssetHandle& dependency : header->dependencies) {
            input >> idText;
            if (!input || !AssetHandle::Parse(idText, &dependency)) {
                SetError(error, "Authored asset dependency ID is invalid.");
                return false;
            }
        }
        break;
    }
    SetError(error, {});
    return true;
}

} // namespace

std::string AssetRegistry::NormalizeVirtualPath(const std::string& path) {
    std::string value = path;
    std::replace(value.begin(), value.end(), '\\', '/');
    while (value.find("//") != std::string::npos)
        value.replace(value.find("//"), 2, "/");
    while (value.rfind("./", 0) == 0) value.erase(0, 2);
    if (value.rfind("Content/", 0) == 0) value.erase(0, 8);
    if (value.rfind("/Content/", 0) == 0) value.erase(0, 9);
    if (value.rfind("/Game/", 0) != 0) {
        while (!value.empty() && value.front() == '/') value.erase(value.begin());
        value = "/Game/" + value;
    }
    while (value.size() > 6 && value.back() == '/') value.pop_back();
    return value;
}

std::string AssetRegistry::DefaultRegistryPath(const std::string& contentRoot) {
    return (std::filesystem::path(contentRoot) / "AssetRegistry.3dgdb").string();
}

bool AssetRegistry::Register(AssetRegistryEntry entry, std::string* error) {
    if (!entry.id.Valid()) {
        SetError(error, "Cannot register an asset with an invalid handle.");
        return false;
    }
    if (!IsKnownAssetType(entry.type)) {
        SetError(error, "Cannot register an asset with an unknown type.");
        return false;
    }
    entry.virtualPath = NormalizeVirtualPath(entry.virtualPath);
    if (entry.virtualPath == "/Game/" || entry.virtualPath.empty()) {
        SetError(error, "Cannot register an asset without a virtual path.");
        return false;
    }

    const std::string pathKey = PathKey(entry.virtualPath);
    const auto existingPath = m_byPath.find(pathKey);
    if (existingPath != m_byPath.end()
        && m_entries[existingPath->second].id != entry.id) {
        SetError(error, "Another asset already owns path " + entry.virtualPath);
        return false;
    }

    const auto existingId = m_byId.find(entry.id);
    if (existingId != m_byId.end()) {
        m_entries[existingId->second] = std::move(entry);
    } else {
        m_entries.push_back(std::move(entry));
    }
    RebuildIndexes();
    SetError(error, {});
    return true;
}

bool AssetRegistry::Remove(AssetHandle id) {
    const auto found = m_byId.find(id);
    if (found == m_byId.end()) return false;
    m_entries.erase(m_entries.begin() + static_cast<std::ptrdiff_t>(found->second));
    RebuildIndexes();
    return true;
}

bool AssetRegistry::Move(AssetHandle id, const std::string& newVirtualPath,
                         std::string* error) {
    AssetRegistryEntry* entry = Find(id);
    if (!entry) {
        SetError(error, "Asset handle is not registered.");
        return false;
    }
    AssetRegistryEntry moved = *entry;
    moved.virtualPath = newVirtualPath;
    return Register(std::move(moved), error);
}

void AssetRegistry::Clear() {
    m_entries.clear();
    m_byId.clear();
    m_byPath.clear();
}

const AssetRegistryEntry* AssetRegistry::Find(AssetHandle id) const {
    const auto found = m_byId.find(id);
    return found == m_byId.end() ? nullptr : &m_entries[found->second];
}

AssetRegistryEntry* AssetRegistry::Find(AssetHandle id) {
    const auto found = m_byId.find(id);
    return found == m_byId.end() ? nullptr : &m_entries[found->second];
}

const AssetRegistryEntry* AssetRegistry::FindByPath(const std::string& virtualPath) const {
    const auto found = m_byPath.find(PathKey(NormalizeVirtualPath(virtualPath)));
    return found == m_byPath.end() ? nullptr : &m_entries[found->second];
}

std::vector<const AssetRegistryEntry*> AssetRegistry::ByType(AssetType type) const {
    std::vector<const AssetRegistryEntry*> result;
    for (const AssetRegistryEntry& entry : m_entries)
        if (entry.type == type) result.push_back(&entry);
    return result;
}

std::vector<AssetHandle> AssetRegistry::Referencers(AssetHandle dependency) const {
    std::vector<AssetHandle> result;
    for (const AssetRegistryEntry& entry : m_entries) {
        if (std::find(entry.dependencies.begin(), entry.dependencies.end(), dependency)
            != entry.dependencies.end()) result.push_back(entry.id);
    }
    return result;
}

std::vector<AssetRegistryIssue> AssetRegistry::Validate() const {
    std::vector<AssetRegistryIssue> issues;
    for (const AssetRegistryEntry& entry : m_entries) {
        if (!entry.id.Valid())
            issues.push_back({AssetRegistryIssue::Severity::Error, entry.id,
                              "Asset handle is invalid."});
        if (!IsKnownAssetType(entry.type))
            issues.push_back({AssetRegistryIssue::Severity::Error, entry.id,
                              "Asset type is unknown."});
        if (entry.virtualPath.empty() || entry.virtualPath.rfind("/Game/", 0) != 0)
            issues.push_back({AssetRegistryIssue::Severity::Error, entry.id,
                              "Asset virtual path is invalid."});
        std::unordered_set<AssetHandle, AssetHandleHash> seen;
        for (AssetHandle dependency : entry.dependencies) {
            if (dependency == entry.id) {
                issues.push_back({AssetRegistryIssue::Severity::Error, entry.id,
                                  "Asset depends on itself."});
            } else if (!Find(dependency)) {
                issues.push_back({AssetRegistryIssue::Severity::Warning, entry.id,
                                  "Dependency is not present in the registry: "
                                      + dependency.ToString()});
            }
            if (!seen.insert(dependency).second) {
                issues.push_back({AssetRegistryIssue::Severity::Warning, entry.id,
                                  "Dependency is listed more than once: "
                                      + dependency.ToString()});
            }
        }
    }
    return issues;
}

bool AssetRegistry::Save(const std::string& path, std::string* error) const {
    const std::filesystem::path destination(path);
    std::error_code ec;
    if (destination.has_parent_path())
        std::filesystem::create_directories(destination.parent_path(), ec);
    if (ec) {
        SetError(error, "Could not create asset registry directory: " + ec.message());
        return false;
    }

    const std::filesystem::path temporary = destination.string() + ".tmp";
    {
        std::ofstream output(temporary, std::ios::trunc);
        if (!output) {
            SetError(error, "Could not open asset registry for writing: " + path);
            return false;
        }
        output << "3DGAssetRegistry " << CurrentVersion << '\n';
        output << "assets " << m_entries.size() << '\n';
        for (const AssetRegistryEntry& entry : m_entries) {
            output << "asset " << entry.id.ToString() << ' '
                   << static_cast<std::uint32_t>(entry.type) << ' '
                   << std::quoted(entry.virtualPath) << ' '
                   << std::quoted(entry.sourcePath) << ' '
                   << entry.sourceHash << ' ' << entry.importerVersion << ' '
                   << entry.dependencies.size();
            for (AssetHandle dependency : entry.dependencies)
                output << ' ' << dependency.ToString();
            output << '\n';
        }
        if (!output) {
            SetError(error, "Could not finish writing asset registry: " + path);
            return false;
        }
    }

    const std::filesystem::path backup = destination.string() + ".bak";
    std::filesystem::remove(backup, ec);
    ec.clear();
    if (std::filesystem::exists(destination, ec)) {
        std::filesystem::rename(destination, backup, ec);
        if (ec) {
            std::filesystem::remove(temporary);
            SetError(error, "Could not replace asset registry: " + ec.message());
            return false;
        }
    }
    ec.clear();
    std::filesystem::rename(temporary, destination, ec);
    if (ec) {
        std::error_code rollback;
        if (std::filesystem::exists(backup, rollback))
            std::filesystem::rename(backup, destination, rollback);
        SetError(error, "Could not commit asset registry: " + ec.message());
        return false;
    }
    std::filesystem::remove(backup, ec);
    SetError(error, {});
    return true;
}

bool AssetRegistry::Load(const std::string& path, std::string* error) {
    std::ifstream input(path);
    if (!input) {
        SetError(error, "Could not open asset registry: " + path);
        return false;
    }

    std::string magic;
    std::uint32_t version = 0;
    std::string assetsTag;
    std::size_t count = 0;
    input >> magic >> version >> assetsTag >> count;
    if (!input || magic != "3DGAssetRegistry" || version != CurrentVersion
        || assetsTag != "assets" || count > 1000000) {
        SetError(error, "Asset registry header is invalid or unsupported.");
        return false;
    }

    AssetRegistry loaded;
    for (std::size_t i = 0; i < count; ++i) {
        std::string tag;
        std::string handleText;
        std::uint32_t type = 0;
        std::size_t dependencyCount = 0;
        AssetRegistryEntry entry;
        input >> tag >> handleText >> type
              >> std::quoted(entry.virtualPath)
              >> std::quoted(entry.sourcePath)
              >> entry.sourceHash >> entry.importerVersion >> dependencyCount;
        if (!input || tag != "asset" || dependencyCount > 4096
            || !AssetHandle::Parse(handleText, &entry.id)) {
            SetError(error, "Asset registry entry is invalid or truncated.");
            return false;
        }
        entry.type = static_cast<AssetType>(type);
        entry.dependencies.resize(dependencyCount);
        for (AssetHandle& dependency : entry.dependencies) {
            input >> handleText;
            if (!input || !AssetHandle::Parse(handleText, &dependency)) {
                SetError(error, "Asset registry dependency is invalid.");
                return false;
            }
        }
        std::string registerError;
        if (!loaded.Register(std::move(entry), &registerError)) {
            SetError(error, "Asset registry contains a conflict: " + registerError);
            return false;
        }
    }
    *this = std::move(loaded);
    SetError(error, {});
    return true;
}

bool AssetRegistry::RebuildFromContent(const std::string& contentRoot,
                                       std::string* error) {
    const std::filesystem::path root(contentRoot);
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec)) {
        SetError(error, "Content root does not exist: " + contentRoot);
        return false;
    }

    AssetRegistry rebuilt;
    for (std::filesystem::recursive_directory_iterator it(root, ec), end;
         it != end; it.increment(ec)) {
        if (ec) {
            SetError(error, "Could not scan Content: " + ec.message());
            return false;
        }
        if (!it->is_regular_file(ec)) continue;

        NativeAssetHeader header;
        std::string headerError;
        const AssetType authoredType = AuthoredAssetType(it->path());
        if (IsNativePath(it->path())) {
            if (!ReadNativeAssetHeaderFile(
                    it->path().string(), &header, &headerError)) {
                SetError(error, it->path().string() + ": " + headerError);
                return false;
            }
        } else if (authoredType != AssetType::Unknown) {
            // Legacy authored assets do not have an ID yet. They remain usable
            // through path fallback and gain an identity on their next save.
            if (!ReadAuthoredMetadata(
                    it->path(), authoredType, &header, &headerError))
                continue;
        } else {
            continue;
        }
        AssetRegistryEntry entry;
        entry.id = header.id;
        entry.type = header.type;
        entry.virtualPath = NormalizeVirtualPath(
            std::filesystem::relative(it->path(), root, ec).generic_string());
        entry.sourceHash = header.sourceHash;
        entry.importerVersion = header.importerVersion;
        entry.dependencies = header.dependencies;
        if (const AssetRegistryEntry* previous = Find(entry.id)) {
            if (entry.sourcePath.empty()) entry.sourcePath = previous->sourcePath;
            if (entry.sourceHash == 0) entry.sourceHash = previous->sourceHash;
            if (entry.importerVersion == 0)
                entry.importerVersion = previous->importerVersion;
        }
        if (ec || !rebuilt.Register(std::move(entry), &headerError)) {
            SetError(error, "Could not register native asset: "
                            + (ec ? ec.message() : headerError));
            return false;
        }
    }
    *this = std::move(rebuilt);
    SetError(error, {});
    return true;
}

bool AssetRegistry::SynchronizeAuthoredAssets(
    const std::string& contentRoot, std::string* error) {
    const std::filesystem::path root(contentRoot);
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec)) {
        SetError(error, "Content root does not exist: " + contentRoot);
        return false;
    }
    std::unordered_set<AssetHandle, AssetHandleHash> discovered;
    for (std::filesystem::recursive_directory_iterator it(root, ec), end;
         it != end; it.increment(ec)) {
        if (ec) {
            SetError(error, "Could not scan authored assets: " + ec.message());
            return false;
        }
        if (!it->is_regular_file(ec)) continue;
        NativeAssetHeader header;
        std::string metadataError;
        const bool native = IsNativePath(it->path());
        const AssetType authoredType = AuthoredAssetType(it->path());
        if (native) {
            if (!ReadNativeAssetHeaderFile(
                    it->path().string(), &header, &metadataError)) {
                // A partial copy, legacy placeholder, or corrupt asset must not
                // prevent every other Content operation. Leave it unregistered;
                // validation/import UI can report the individual bad file while
                // valid authored assets continue to synchronize.
                continue;
            }
        } else {
            if (authoredType == AssetType::Unknown) continue;
            if (!ReadAuthoredMetadata(
                    it->path(), authoredType, &header, &metadataError))
                continue; // Legacy asset gains an identity on its next save.
        }
        AssetRegistryEntry entry;
        entry.id = header.id;
        entry.type = native ? header.type : authoredType;
        entry.virtualPath = NormalizeVirtualPath(
            std::filesystem::relative(it->path(), root, ec).generic_string());
        entry.sourceHash = header.sourceHash;
        entry.importerVersion = header.importerVersion;
        entry.dependencies = header.dependencies;
        if (const AssetRegistryEntry* previous = Find(header.id)) {
            entry.sourcePath = previous->sourcePath;
            if (entry.sourceHash == 0) entry.sourceHash = previous->sourceHash;
            if (entry.importerVersion == 0)
                entry.importerVersion = previous->importerVersion;
        }
        if (const AssetRegistryEntry* previous =
                FindByPath(entry.virtualPath);
            previous && previous->id != entry.id)
            Remove(previous->id);
        if (ec || !Register(std::move(entry), &metadataError)) {
            SetError(error, "Could not register authored asset: "
                + (ec ? ec.message() : metadataError));
            return false;
        }
        discovered.insert(header.id);
    }
    std::vector<AssetHandle> removed;
    for (const AssetRegistryEntry& entry : m_entries) {
        const std::filesystem::path registeredPath(entry.virtualPath);
        if ((IsNativePath(registeredPath)
             || AuthoredAssetType(registeredPath) != AssetType::Unknown)
            && discovered.find(entry.id) == discovered.end())
            removed.push_back(entry.id);
    }
    for (AssetHandle id : removed) Remove(id);
    SetError(error, {});
    return true;
}

void AssetRegistry::RebuildIndexes() {
    m_byId.clear();
    m_byPath.clear();
    for (std::size_t i = 0; i < m_entries.size(); ++i) {
        m_byId[m_entries[i].id] = i;
        m_byPath[PathKey(m_entries[i].virtualPath)] = i;
    }
}

} // namespace engine
