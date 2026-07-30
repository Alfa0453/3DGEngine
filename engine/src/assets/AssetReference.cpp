#include "engine/assets/AssetReference.h"

#include "engine/assets/AssetRegistry.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace engine {
namespace {

void SetError(std::string* error, const std::string& message) {
    if (error) *error = message;
}

bool TypeMatches(AssetType actual, AssetType expected) {
    return expected == AssetType::Unknown || actual == expected;
}

std::string AbsoluteFromVirtual(const std::string& contentRoot,
                                const std::string& virtualPath) {
    std::string relative = AssetRegistry::NormalizeVirtualPath(virtualPath);
    constexpr const char* prefix = "/Game/";
    if (relative.rfind(prefix, 0) == 0)
        relative.erase(0, std::char_traits<char>::length(prefix));
    return (std::filesystem::path(contentRoot)
            / std::filesystem::path(relative))
        .lexically_normal().string();
}

std::string VirtualFromPath(const std::string& contentRoot,
                            const std::string& path) {
    if (path.empty()) return {};
    if (path.rfind("/Game/", 0) == 0)
        return AssetRegistry::NormalizeVirtualPath(path);
    std::error_code ec;
    const std::filesystem::path root =
        std::filesystem::absolute(contentRoot, ec).lexically_normal();
    if (ec) return {};
    std::filesystem::path candidate(path);
    if (candidate.is_relative()) {
        std::string normalized = path;
        std::replace(normalized.begin(), normalized.end(), '\\', '/');
        if (normalized.rfind("Content/", 0) == 0)
            candidate = root / normalized.substr(8);
        else
            candidate = root / candidate;
    }
    const std::filesystem::path absolute =
        std::filesystem::absolute(candidate, ec).lexically_normal();
    if (ec) return {};
    const std::filesystem::path relative = absolute.lexically_relative(root);
    if (relative.empty() || relative.is_absolute()
        || (relative.begin() != relative.end()
            && relative.begin()->generic_string() == ".."))
        return {};
    return AssetRegistry::NormalizeVirtualPath(relative.generic_string());
}

} // namespace

AssetReference MakeAssetReference(const AssetRegistry* registry,
                                  const std::string& contentRoot,
                                  const std::string& path,
                                  AssetType expected) {
    AssetReference result;
    result.fallbackPath = path;
    if (!registry || path.empty()) return result;
    const std::string virtualPath = VirtualFromPath(contentRoot, path);
    const AssetRegistryEntry* entry = virtualPath.empty()
        ? nullptr : registry->FindByPath(virtualPath);
    if (entry && TypeMatches(entry->type, expected)) result.id = entry->id;
    return result;
}

std::string ResolveAssetReference(const AssetRegistry* registry,
                                  const std::string& contentRoot,
                                  const AssetReference& reference,
                                  AssetType expected,
                                  std::string* error) {
    if (registry && reference.id.Valid()) {
        const AssetRegistryEntry* entry = registry->Find(reference.id);
        if (entry && TypeMatches(entry->type, expected)) {
            SetError(error, {});
            return AbsoluteFromVirtual(contentRoot, entry->virtualPath);
        }
    }
    if (!reference.fallbackPath.empty()) {
        const std::string virtualPath =
            VirtualFromPath(contentRoot, reference.fallbackPath);
        if (registry && !virtualPath.empty()) {
            const AssetRegistryEntry* entry = registry->FindByPath(virtualPath);
            if (entry && TypeMatches(entry->type, expected)) {
                SetError(error, {});
                return AbsoluteFromVirtual(contentRoot, entry->virtualPath);
            }
        }
        std::filesystem::path fallback(reference.fallbackPath);
        if (fallback.is_relative() && !contentRoot.empty()) {
            std::string normalized = reference.fallbackPath;
            std::replace(normalized.begin(), normalized.end(), '\\', '/');
            fallback = normalized.rfind("Content/", 0) == 0
                ? std::filesystem::path(contentRoot) / normalized.substr(8)
                : std::filesystem::path(contentRoot) / fallback;
        }
        SetError(error, {});
        return fallback.lexically_normal().string();
    }
    SetError(error, reference.id.Valid()
        ? "Asset ID is not present in the registry: " + reference.id.ToString()
        : "Asset reference is empty.");
    return {};
}

std::string FindContentRootForAsset(const std::string& path) {
    std::error_code ec;
    std::filesystem::path cursor =
        std::filesystem::absolute(path, ec).lexically_normal();
    if (ec) return {};
    if (cursor.has_filename()) cursor = cursor.parent_path();
    while (!cursor.empty()) {
        std::string name = cursor.filename().string();
        std::transform(name.begin(), name.end(), name.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (name == "content") return cursor.string();
        const std::filesystem::path parent = cursor.parent_path();
        if (parent == cursor) break;
        cursor = parent;
    }
    return {};
}

} // namespace engine
