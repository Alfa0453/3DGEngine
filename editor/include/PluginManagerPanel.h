#pragma once

#include <filesystem>
#include <future>
#include <string>
#include <vector>

class PluginManagerPanel {
public:
    PluginManagerPanel() = default;
    ~PluginManagerPanel();
    void Draw(const std::filesystem::path& engineRoot,
              const std::filesystem::path& projectRoot,
              const std::filesystem::path& projectFile,
              bool* open);

    enum class Scope { Engine, Project };
    enum class Type { Runtime, Editor, Both, Content };
    struct Dependency { std::string id; std::string minimumVersion; bool optional = false; };
    struct Plugin {
        std::string id, name, version, author, description;
        Type type = Type::Both;
        Scope scope = Scope::Project;
        int engineApi = 1;
        bool required = false;
        bool defaultEnabled = false;
        bool enabled = false;
        bool stagedEnabled = false;
        std::filesystem::path descriptor, root, cmakeDirectory, binary;
        std::vector<Dependency> dependencies;
        std::vector<std::string> errors, warnings;
    };
    struct ScanResult { std::vector<Plugin> plugins; std::string error; };

private:
    void StartScan(const std::filesystem::path& engineRoot,
                   const std::filesystem::path& projectRoot,
                   const std::filesystem::path& projectFile);
    void Poll();
    void Validate();
    bool Apply(const std::filesystem::path& projectRoot,
               const std::filesystem::path& projectFile);
    bool HasPendingChanges() const;

    std::vector<Plugin> m_plugins;
    std::future<ScanResult> m_future;
    bool m_scanning = false;
    bool m_rescanRequested = false;
    bool m_initialized = false;
    std::filesystem::path m_projectRoot;
    std::filesystem::path m_pendingEngineRoot;
    std::filesystem::path m_pendingProjectRoot;
    std::filesystem::path m_pendingProjectFile;
    int m_selected = -1;
    char m_filter[160]{};
    bool m_enabledOnly = false;
    bool m_issuesOnly = false;
    std::string m_status;
};
