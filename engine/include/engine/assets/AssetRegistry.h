#pragma once

#include "engine/assets/AssetIdentity.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace engine {

struct AssetRegistryEntry {
    AssetHandle id;
    AssetType type = AssetType::Unknown;
    std::string virtualPath;
    std::string sourcePath;
    std::uint64_t sourceHash = 0;
    std::uint32_t importerVersion = 0;
    std::vector<AssetHandle> dependencies;
};

struct AssetRegistryIssue {
    enum class Severity { Warning, Error };
    Severity severity = Severity::Error;
    AssetHandle asset;
    std::string message;
};

class AssetRegistry {
public:
    static constexpr std::uint32_t CurrentVersion = 1;

    bool Register(AssetRegistryEntry entry, std::string* error = nullptr);
    bool Remove(AssetHandle id);
    bool Move(AssetHandle id, const std::string& newVirtualPath,
              std::string* error = nullptr);
    void Clear();

    const AssetRegistryEntry* Find(AssetHandle id) const;
    AssetRegistryEntry* Find(AssetHandle id);
    const AssetRegistryEntry* FindByPath(const std::string& virtualPath) const;
    std::vector<const AssetRegistryEntry*> ByType(AssetType type) const;
    std::vector<AssetHandle> Referencers(AssetHandle dependency) const;

    const std::vector<AssetRegistryEntry>& Entries() const { return m_entries; }
    std::vector<AssetRegistryIssue> Validate() const;

    bool Save(const std::string& path, std::string* error = nullptr) const;
    bool Load(const std::string& path, std::string* error = nullptr);
    bool RebuildFromContent(const std::string& contentRoot,
                            std::string* error = nullptr);
    // Updates text-authored assets (scene/character/clip/graph) without
    // revalidating unrelated native binaries during a routine browser refresh.
    bool SynchronizeAuthoredAssets(const std::string& contentRoot,
                                   std::string* error = nullptr);

    static std::string NormalizeVirtualPath(const std::string& path);
    static std::string DefaultRegistryPath(const std::string& contentRoot);

private:
    void RebuildIndexes();

    std::vector<AssetRegistryEntry> m_entries;
    std::unordered_map<AssetHandle, std::size_t, AssetHandleHash> m_byId;
    std::unordered_map<std::string, std::size_t> m_byPath;
};

} // namespace engine
