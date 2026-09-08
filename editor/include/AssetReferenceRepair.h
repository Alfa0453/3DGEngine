#pragma once

#include <engine/assets/AssetRegistry.h>

#include <cstddef>
#include <string>
#include <vector>

class AssetReferenceRepair {
public:
    enum class Kind { MissingPath, MismatchedId, StaleRegistryPath, MissingDependency, MissingSource };
    struct Finding {
        Kind kind = Kind::MissingPath;
        std::string ownerPath;
        std::string oldValue;
        std::string replacement;
        std::string message;
        engine::AssetHandle asset;
        std::size_t offset = 0;
        std::size_t length = 0;
        bool repairable = false;
        bool selected = false;
    };
    struct ApplyResult {
        int repaired = 0;
        int filesChanged = 0;
        std::string backupRoot;
        std::string reportPath;
        std::string error;
    };

    void Scan(const engine::AssetRegistry& registry, const std::string& contentRoot);
    ApplyResult Apply(engine::AssetRegistry& registry, const std::string& contentRoot);
    const std::vector<Finding>& Findings() const { return m_findings; }
    std::vector<Finding>& Findings() { return m_findings; }
    const std::string& LastScanSummary() const { return m_summary; }
    static const char* KindName(Kind kind);

private:
    std::vector<Finding> m_findings;
    std::string m_summary;
};
