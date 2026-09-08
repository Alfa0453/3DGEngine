#pragma once

#include <filesystem>
#include <functional>
#include <future>
#include <string>
#include <vector>

class ProjectMigrationPanel {
public:
    enum class State { Current, Legacy, Future, Invalid, Unknown };
    using SceneMigrator = std::function<bool(const std::filesystem::path&, bool, std::string*)>;
    struct Result {
        bool assetsChanged = false;
        bool currentSceneChanged = false;
        std::string message;
    };

    Result Draw(const std::filesystem::path& projectRoot,
                const std::filesystem::path& currentScene,
                bool currentSceneDirty,
                const SceneMigrator& sceneMigrator,
                bool* open);

private:
    struct Entry {
        std::filesystem::path path;
        std::string relativePath;
        std::string type;
        std::string magic;
        int version = 0;
        int targetVersion = 0;
        State state = State::Unknown;
        bool migratable = false;
        bool selected = false;
        std::string detail;
    };
    struct ScanResult { std::vector<Entry> entries; std::string error; };

    void StartScan(const std::filesystem::path& root);
    void PollScan();
    Result ApplySelected(const SceneMigrator& sceneMigrator,
                         const std::filesystem::path& currentScene);
    bool Migrate(const Entry& entry, bool write, const SceneMigrator& sceneMigrator,
                 std::string* error) const;
    void WriteReport(const std::filesystem::path& path, const std::string& outcome,
                     const std::vector<std::string>& details) const;

    std::filesystem::path m_projectRoot;
    std::vector<Entry> m_entries;
    std::future<ScanResult> m_scan;
    bool m_scanning = false;
    bool m_initialized = false;
    bool m_showCurrent = false;
    bool m_showUnknown = false;
    std::string m_status;
    std::string m_lastBackup;
    std::string m_lastReport;
    char m_filter[160]{};
};
