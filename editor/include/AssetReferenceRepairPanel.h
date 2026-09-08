#pragma once

#include "AssetReferenceRepair.h"

#include <array>
#include <string>

class AssetReferenceRepairPanel {
public:
    struct Result { bool registryChanged = false; std::string revealPath; std::string message; };
    Result Draw(engine::AssetRegistry& registry, const std::string& contentRoot, bool* open);
    void Invalidate() { m_scanned = false; }
private:
    AssetReferenceRepair m_repair;
    std::array<char, 160> m_search{};
    int m_kindFilter = 0;
    bool m_repairableOnly = false;
    bool m_scanned = false;
    bool m_confirmApply = false;
};
