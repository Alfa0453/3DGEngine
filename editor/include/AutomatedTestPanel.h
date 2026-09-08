#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <future>
#include <memory>
#include <string>
#include <vector>

class AutomatedTestPanel {
public:
    AutomatedTestPanel() = default;
    ~AutomatedTestPanel();

    AutomatedTestPanel(const AutomatedTestPanel&) = delete;
    AutomatedTestPanel& operator=(const AutomatedTestPanel&) = delete;

    void Draw(const std::filesystem::path& engineRoot,
              const std::filesystem::path& editorBuildRoot,
              bool* open);

private:
    enum class Action { Discover, Run };
    enum class Status { NotRun, Passed, Failed, Timeout };
    struct TestResult {
        std::string name;
        std::string category;
        Status status = Status::NotRun;
        double durationSeconds = 0.0;
    };
    struct RunResult {
        bool completed = false;
        bool cancelled = false;
        int exitCode = -1;
        double durationSeconds = 0.0;
        std::string message;
        std::string output;
        std::vector<TestResult> tests;
    };

    void Start(Action action, const std::filesystem::path& engineRoot,
               const std::string& regex, bool configureAndBuild);
    void Poll();
    void RefreshLiveOutput();
    static RunResult Execute(Action action, std::filesystem::path engineRoot,
                             std::filesystem::path buildRoot,
                             std::filesystem::path logPath,
                             std::string configuration, std::string regex,
                             int timeoutSeconds, bool configureAndBuild,
                             const std::shared_ptr<std::atomic_bool>& cancel);
    static std::vector<TestResult> ParseTests(const std::string& output);
    static std::string CategoryOf(const std::string& name);

    std::filesystem::path m_engineRoot;
    std::filesystem::path m_buildRoot;
    std::filesystem::path m_logPath;
    std::future<RunResult> m_future;
    std::shared_ptr<std::atomic_bool> m_cancel;
    std::vector<TestResult> m_tests;
    std::string m_output;
    std::string m_status = "Discover tests to begin.";
    char m_nameFilter[96]{};
    int m_configuration = 0;
    int m_category = 0;
    int m_selectedTest = -1;
    int m_timeoutSeconds = 120;
    bool m_running = false;
    bool m_configureBeforeRun = true;
    bool m_showPassed = true;
    bool m_showFailed = true;
    bool m_autoScroll = true;
    double m_nextLogRefresh = 0.0;
    double m_lastDuration = 0.0;
    int m_lastExitCode = -1;
};
