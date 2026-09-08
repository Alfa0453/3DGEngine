#pragma once

#include <engine/assets/AssetRegistry.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <future>
#include <string>
#include <vector>

class BuildSizeAnalyzerPanel {
public:
    BuildSizeAnalyzerPanel() = default;
    ~BuildSizeAnalyzerPanel();

    void Draw(const std::filesystem::path& projectRoot,
              const std::filesystem::path& contentRoot,
              const std::filesystem::path& packageOutput,
              const engine::AssetRegistry& registry,
              bool* open);

    enum class Category : std::uint8_t {
        Runtime, Scenes, Meshes, Animation, Textures, MaterialsShaders,
        Audio, Scripts, Vfx, UiLocalization, Configuration, Other, Count
    };
    struct FileEntry {
        std::string path;
        Category category = Category::Other;
        std::uint64_t rawBytes = 0;
        std::uint64_t compressedBytes = 0;
        std::uint64_t fingerprint = 0;
        int duplicateGroup = 0;
        bool unused = false;
    };
    struct CategoryTotal {
        std::uint64_t rawBytes = 0;
        std::uint64_t compressedBytes = 0;
        std::uint64_t files = 0;
        std::uint64_t unusedBytes = 0;
        std::uint64_t duplicateBytes = 0;
    };
    struct ScanResult {
        std::vector<FileEntry> files;
        std::array<CategoryTotal, static_cast<std::size_t>(Category::Count)> categories{};
        std::uint64_t rawBytes = 0;
        std::uint64_t compressedBytes = 0;
        std::uint64_t unusedBytes = 0;
        std::uint64_t duplicateBytes = 0;
        std::uint64_t manifestFiles = 0;
        int duplicateGroups = 0;
        bool compressedIsEstimate = true;
        bool unusedAvailable = false;
        std::string error;
    };

private:

    void SetTarget(const std::filesystem::path& path);
    void StartScan(const std::filesystem::path& contentRoot,
                   const engine::AssetRegistry& registry);
    void Poll();
    void ExportReport(bool csv);
    static ScanResult ScanFolder(const std::filesystem::path& root,
                                 const std::filesystem::path& contentRoot,
                                 std::vector<std::string> registryUnused);
    static ScanResult ScanZip(const std::filesystem::path& path);

    std::filesystem::path m_projectRoot;
    std::filesystem::path m_contentRoot;
    std::array<char, 768> m_target{};
    std::array<char, 160> m_filter{};
    std::future<ScanResult> m_future;
    ScanResult m_result;
    bool m_running = false;
    bool m_initialized = false;
    bool m_onlyIssues = false;
    bool m_onlyUnused = false;
    bool m_onlyDuplicates = false;
    int m_sort = 0;
    float m_totalBudgetMiB = 2048.0f;
    std::array<float, static_cast<std::size_t>(Category::Count)> m_categoryBudgetMiB{};
    std::string m_status = "Choose a staged folder, package, or project Content folder.";
    std::string m_lastReport;
};
