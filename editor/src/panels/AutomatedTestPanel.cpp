#include "AutomatedTestPanel.h"

#include "EditorPanels.h"
#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <regex>
#include <sstream>
#include <thread>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#endif

namespace {
std::string ReadText(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    constexpr std::size_t maxBytes = 2u * 1024u * 1024u;
    if (text.size() > maxBytes) text.erase(0, text.size() - maxBytes);
    return text;
}

void AppendHeading(const std::filesystem::path& path, const std::string& heading) {
    std::ofstream output(path, std::ios::app | std::ios::binary);
    output << "\n===== " << heading << " =====\n";
}

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

#if defined(_WIN32)
std::wstring Quote(const std::filesystem::path& path) { return L"\"" + path.wstring() + L"\""; }
std::wstring Widen(const std::string& value) { return std::wstring(value.begin(), value.end()); }

int RunCommand(std::wstring command, const std::filesystem::path& workingDirectory,
               const std::filesystem::path& logPath,
               const std::shared_ptr<std::atomic_bool>& cancel) {
    HANDLE log = CreateFileW(logPath.wstring().c_str(), GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log == INVALID_HANDLE_VALUE) return -1;
    SetFilePointer(log, 0, nullptr, FILE_END);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdOutput = log;
    startup.hStdError = log;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION process{};
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
    }
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');
    const BOOL launched = CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr,
        TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, workingDirectory.wstring().c_str(),
        &startup, &process);
    CloseHandle(log);
    if (!launched) { if (job) CloseHandle(job); return -1; }
    if (job && !AssignProcessToJobObject(job, process.hProcess)) {
        CloseHandle(job);
        job = nullptr;
    }
    ResumeThread(process.hThread);
    while (WaitForSingleObject(process.hProcess, 50) == WAIT_TIMEOUT) {
        if (cancel && cancel->load()) {
            if (job) TerminateJobObject(job, 2);
            else TerminateProcess(process.hProcess, 2);
            WaitForSingleObject(process.hProcess, INFINITE);
            break;
        }
    }
    DWORD exitCode = 1;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (job) CloseHandle(job);
    return static_cast<int>(exitCode);
}
#else
int RunCommand(const std::string& command, const std::filesystem::path& workingDirectory,
               const std::filesystem::path& logPath,
               const std::shared_ptr<std::atomic_bool>& cancel) {
    if (cancel && cancel->load()) return 2;
    const std::string full = "cd \"" + workingDirectory.string() + "\" && " + command
        + " >> \"" + logPath.string() + "\" 2>&1";
    return std::system(full.c_str());
}
#endif
}

AutomatedTestPanel::~AutomatedTestPanel() {
    if (m_cancel) m_cancel->store(true);
    if (m_future.valid()) m_future.wait();
}

std::string AutomatedTestPanel::CategoryOf(const std::string& name) {
    const std::string n = Lower(name);
    if (n.find("script") != std::string::npos || n.find("lua") != std::string::npos) return "Scripting";
    if (n.find("animation") != std::string::npos || n.find("skeletal") != std::string::npos
        || n.find("ragdoll") != std::string::npos || n.find("pose") != std::string::npos
        || n.find("ik_") != std::string::npos) return "Animation";
    if (n.find("shader") != std::string::npos || n.find("material") != std::string::npos
        || n.find("lighting") != std::string::npos || n.find("render") != std::string::npos
        || n.find("image") != std::string::npos) return "Rendering";
    if (n.find("ai_") != std::string::npos || n.find("collision") != std::string::npos
        || n.find("physics") != std::string::npos || n.find("nav") != std::string::npos) return "Physics & AI";
    if (n.find("asset") != std::string::npos || n.find("import") != std::string::npos
        || n.find("foliage") != std::string::npos || n.find("biome") != std::string::npos) return "Assets";
    if (n.find("scene") != std::string::npos || n.find("editor") != std::string::npos
        || n.find("world") != std::string::npos || n.find("terrain") != std::string::npos
        || n.find("spline") != std::string::npos || n.find("road") != std::string::npos) return "Editor & World";
    if (n.find("quest") != std::string::npos || n.find("dialogue") != std::string::npos
        || n.find("inventory") != std::string::npos || n.find("combat") != std::string::npos
        || n.find("ability") != std::string::npos || n.find("spawn") != std::string::npos
        || n.find("save") != std::string::npos || n.find("interaction") != std::string::npos) return "Gameplay";
    if (n.find("perf") != std::string::npos || n.find("benchmark") != std::string::npos) return "Performance";
    return "Engine Core";
}

std::vector<AutomatedTestPanel::TestResult> AutomatedTestPanel::ParseTests(const std::string& output) {
    std::vector<TestResult> tests;
    std::istringstream lines(output);
    std::string line;
    const std::regex secondsPattern(R"(([0-9]+(?:\.[0-9]+)?)\s+sec)");
    while (std::getline(lines, line)) {
        const std::size_t marker = line.find("Test #");
        if (marker == std::string::npos) continue;
        const std::size_t colon = line.find(':', marker);
        if (colon == std::string::npos) continue;
        std::size_t begin = line.find_first_not_of(" \t", colon + 1);
        if (begin == std::string::npos) continue;
        std::size_t end = line.find_first_of(" \t", begin);
        const std::string name = line.substr(begin, end == std::string::npos ? end : end - begin);
        if (name.empty()) continue;
        auto found = std::find_if(tests.begin(), tests.end(), [&](const TestResult& t) { return t.name == name; });
        if (found == tests.end()) {
            tests.push_back({name, CategoryOf(name), Status::NotRun, 0.0});
            found = std::prev(tests.end());
        }
        if (line.find("Passed") != std::string::npos) found->status = Status::Passed;
        else if (line.find("Timeout") != std::string::npos) found->status = Status::Timeout;
        else if (line.find("Failed") != std::string::npos || line.find("***") != std::string::npos)
            found->status = Status::Failed;
        std::smatch seconds;
        if (std::regex_search(line, seconds, secondsPattern)) found->durationSeconds = std::stod(seconds[1].str());
    }
    return tests;
}

AutomatedTestPanel::RunResult AutomatedTestPanel::Execute(
    Action action, std::filesystem::path engineRoot, std::filesystem::path buildRoot,
    std::filesystem::path logPath, std::string configuration, std::string regex,
    int timeoutSeconds, bool configureAndBuild,
    const std::shared_ptr<std::atomic_bool>& cancel) {
    RunResult result;
    const auto start = std::chrono::steady_clock::now();
    std::error_code ec;
    std::filesystem::create_directories(buildRoot, ec);
    std::filesystem::create_directories(logPath.parent_path(), ec);
    std::filesystem::remove(logPath, ec);
    int code = 0;
    auto cancelled = [&]() { return cancel && cancel->load(); };

    if (configureAndBuild) {
        AppendHeading(logPath, "CONFIGURE TEST BUILD");
#if defined(_WIN32)
        code = RunCommand(L"cmake -S " + Quote(engineRoot) + L" -B " + Quote(buildRoot)
            + L" -DTHREEDG_BUILD_TESTS=ON -DBUILD_TESTING=ON -DBUILD_DEMOS=OFF",
            engineRoot, logPath, cancel);
#else
        code = RunCommand("cmake -S \"" + engineRoot.string() + "\" -B \"" + buildRoot.string()
            + "\" -DTHREEDG_BUILD_TESTS=ON -DBUILD_TESTING=ON -DBUILD_DEMOS=OFF",
            engineRoot, logPath, cancel);
#endif
        if (code == 0 && !cancelled()) {
            AppendHeading(logPath, "BUILD TESTS");
#if defined(_WIN32)
            code = RunCommand(L"cmake --build " + Quote(buildRoot) + L" --config \""
                + Widen(configuration) + L"\" --parallel", engineRoot, logPath, cancel);
#else
            code = RunCommand("cmake --build \"" + buildRoot.string() + "\" --config \""
                + configuration + "\" --parallel", engineRoot, logPath, cancel);
#endif
        }
    }
    if (code == 0 && !cancelled()) {
        AppendHeading(logPath, action == Action::Discover ? "DISCOVER TESTS" : "RUN TESTS");
#if defined(_WIN32)
        std::wstring command = L"ctest --test-dir " + Quote(buildRoot) + L" -C \""
            + Widen(configuration) + L"\"";
        if (action == Action::Discover) command += L" -N";
        else command += L" --output-on-failure --progress --timeout " + std::to_wstring(timeoutSeconds);
        if (!regex.empty()) command += L" -R \"" + Widen(regex) + L"\"";
        code = RunCommand(std::move(command), engineRoot, logPath, cancel);
#else
        std::string command = "ctest --test-dir \"" + buildRoot.string() + "\" -C \""
            + configuration + "\"";
        if (action == Action::Discover) command += " -N";
        else command += " --output-on-failure --progress --timeout " + std::to_string(timeoutSeconds);
        if (!regex.empty()) command += " -R \"" + regex + "\"";
        code = RunCommand(command, engineRoot, logPath, cancel);
#endif
    }
    result.output = ReadText(logPath);
    result.tests = ParseTests(result.output);
    result.cancelled = cancelled();
    result.exitCode = code;
    result.completed = !result.cancelled;
    result.durationSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    if (result.cancelled) result.message = "Test operation cancelled.";
    else if (code == 0) result.message = action == Action::Discover
        ? "Test discovery complete." : "Test run passed.";
    else result.message = "Test operation failed (exit " + std::to_string(code) + "). Review the output.";
    return result;
}

void AutomatedTestPanel::Start(Action action, const std::filesystem::path& engineRoot,
                               const std::string& regex, bool configureAndBuild) {
    if (m_running) return;
    m_engineRoot = engineRoot;
    if (m_buildRoot.empty()) m_buildRoot = engineRoot / "build-tests";
    m_logPath = m_buildRoot / "automated_test_panel.log";
    m_cancel = std::make_shared<std::atomic_bool>(false);
    m_running = true;
    m_status = configureAndBuild ? "Configuring and building tests..."
        : action == Action::Discover ? "Discovering tests..." : "Running tests...";
    const std::string configuration = m_configuration == 0 ? "Debug" : "Release";
    const auto cancel = m_cancel;
    const auto build = m_buildRoot;
    const auto log = m_logPath;
    const int timeout = m_timeoutSeconds;
    m_future = std::async(std::launch::async,
        [action, engineRoot, build, log, configuration, regex, timeout,
         configureAndBuild, cancel]() {
            return Execute(action, engineRoot, build, log, configuration, regex,
                           timeout, configureAndBuild, cancel);
        });
}

void AutomatedTestPanel::RefreshLiveOutput() {
    if (!m_logPath.empty()) m_output = ReadText(m_logPath);
}

void AutomatedTestPanel::Poll() {
    if (!m_running || !m_future.valid()) return;
    if (m_future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    RunResult result = m_future.get();
    m_running = false;
    m_output = std::move(result.output);
    m_tests = std::move(result.tests);
    m_status = std::move(result.message);
    m_lastDuration = result.durationSeconds;
    m_lastExitCode = result.exitCode;
    if (m_selectedTest >= static_cast<int>(m_tests.size())) m_selectedTest = -1;
}

void AutomatedTestPanel::Draw(const std::filesystem::path& engineRoot,
                              const std::filesystem::path& editorBuildRoot,
                              bool* open) {
    Poll();
    if (!ImGui::Begin(EditorPanels::Name(EditorPanels::Panel::AutomatedTest), open)) {
        ImGui::End(); return;
    }
    if (m_buildRoot.empty()) m_buildRoot = engineRoot / "build-tests";
    if (m_running && ImGui::GetTime() >= m_nextLogRefresh) {
        RefreshLiveOutput(); m_nextLogRefresh = ImGui::GetTime() + 0.25;
    }

    static const char* categories[] = {"All", "Engine Core", "Assets", "Rendering", "Physics & AI",
                                       "Animation", "Gameplay", "Editor & World", "Scripting", "Performance"};
    ImGui::TextWrapped("Runs the engine's CTest suite in a separate test build so the active editor build is not reconfigured.");
    ImGui::Text("Source: %s", engineRoot.string().c_str());
    ImGui::Text("Test build: %s", m_buildRoot.string().c_str());
    if (!editorBuildRoot.empty()) ImGui::TextDisabled("Active editor build remains: %s", editorBuildRoot.string().c_str());
    const bool suitePresent = std::filesystem::is_regular_file(engineRoot / "tests" / "CMakeLists.txt");
    if (!suitePresent) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.2f, 1.0f),
            "The engine test suite is missing: tests/CMakeLists.txt");
        ImGui::TextWrapped("Restore or create the tests folder before configuring a test build. Existing test-build results can still be inspected.");
    }
    const char* configurations[] = {"Debug", "Release"};
    ImGui::SetNextItemWidth(120.0f);
    ImGui::Combo("Configuration##automated_tests", &m_configuration, configurations, 2);
    ImGui::SameLine(); ImGui::Checkbox("Configure/build before run##automated_tests", &m_configureBeforeRun);
    ImGui::SetNextItemWidth(140.0f);
    ImGui::DragInt("Per-test timeout##automated_tests", &m_timeoutSeconds, 1.0f, 1, 3600, "%d s");

    ImGui::BeginDisabled(m_running || engineRoot.empty()
                         || (m_configureBeforeRun && !suitePresent));
    if (ImGui::Button("Discover##automated_tests")) Start(Action::Discover, engineRoot, {}, m_configureBeforeRun);
    ImGui::SameLine();
    if (ImGui::Button("Run all##automated_tests")) Start(Action::Run, engineRoot, {}, m_configureBeforeRun);
    ImGui::SameLine();
    const bool hasSelection = m_selectedTest >= 0 && m_selectedTest < static_cast<int>(m_tests.size());
    ImGui::BeginDisabled(!hasSelection);
    if (ImGui::Button("Run selected##automated_tests")) {
        Start(Action::Run, engineRoot, "^" + m_tests[static_cast<std::size_t>(m_selectedTest)].name + "$",
              m_configureBeforeRun);
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    if (m_running) {
        ImGui::SameLine();
        if (ImGui::Button("Stop##automated_tests") && m_cancel) {
            m_cancel->store(true); m_status = "Stopping test process...";
        }
    }
    ImGui::Text("%s", m_status.c_str());
    if (m_lastExitCode >= 0) ImGui::Text("Last exit: %d | duration: %.2f s", m_lastExitCode, m_lastDuration);

    ImGui::SetNextItemWidth(160.0f); ImGui::Combo("Category##automated_tests", &m_category, categories, 10);
    ImGui::SameLine(); ImGui::SetNextItemWidth(190.0f);
    ImGui::InputTextWithHint("##automated_test_filter", "Filter test names...", m_nameFilter, sizeof(m_nameFilter));
    ImGui::SameLine(); ImGui::Checkbox("Passed##automated_tests", &m_showPassed);
    ImGui::SameLine(); ImGui::Checkbox("Failed##automated_tests", &m_showFailed);

    std::string filteredRegex;
    const std::string loweredFilter = Lower(m_nameFilter);
    for (const TestResult& test : m_tests) {
        if (m_category > 0 && test.category != categories[m_category]) continue;
        if (!loweredFilter.empty() && Lower(test.name).find(loweredFilter) == std::string::npos) continue;
        std::string escaped;
        for (char c : test.name) {
            if (std::string(".^$|()[]*+?{}\\").find(c) != std::string::npos) escaped.push_back('\\');
            escaped.push_back(c);
        }
        if (!filteredRegex.empty()) filteredRegex += "|";
        filteredRegex += escaped;
    }
    if (!filteredRegex.empty()) filteredRegex = "^(" + filteredRegex + ")$";
    ImGui::BeginDisabled(m_running || filteredRegex.empty()
                         || (m_configureBeforeRun && !suitePresent));
    if (ImGui::Button("Run filtered##automated_tests"))
        Start(Action::Run, engineRoot, filteredRegex, m_configureBeforeRun);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(m_output.empty());
    if (ImGui::Button("Save report##automated_tests")) {
        std::error_code reportError;
        const std::filesystem::path reportDir = engineRoot / "TestReports";
        std::filesystem::create_directories(reportDir, reportError);
        const std::filesystem::path reportPath = reportDir / "latest_test_report.txt";
        std::ofstream report(reportPath, std::ios::binary);
        if (!reportError && report) {
            report << "3DGEngine automated test report\nStatus: " << m_status
                   << "\nExit code: " << m_lastExitCode
                   << "\nDuration: " << m_lastDuration << " seconds\n\n" << m_output;
            m_status = "Saved report: " + reportPath.string();
        } else m_status = "Could not save the test report.";
    }
    ImGui::EndDisabled();

    int passed = 0, failed = 0, notRun = 0;
    for (const TestResult& test : m_tests) {
        if (test.status == Status::Passed) ++passed;
        else if (test.status == Status::Failed || test.status == Status::Timeout) ++failed;
        else ++notRun;
    }
    ImGui::Text("Discovered: %zu | passed: %d | failed: %d | not run: %d",
                m_tests.size(), passed, failed, notRun);
    if (ImGui::BeginTable("automated_test_results", 4,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable
        | ImGuiTableFlags_ScrollY, ImVec2(0.0f, 240.0f))) {
        ImGui::TableSetupColumn("Test"); ImGui::TableSetupColumn("Category");
        ImGui::TableSetupColumn("Status"); ImGui::TableSetupColumn("Duration");
        ImGui::TableHeadersRow();
        for (int i = 0; i < static_cast<int>(m_tests.size()); ++i) {
            const TestResult& test = m_tests[static_cast<std::size_t>(i)];
            if (m_category > 0 && test.category != categories[m_category]) continue;
            if (!loweredFilter.empty() && Lower(test.name).find(loweredFilter) == std::string::npos) continue;
            if (!m_showPassed && test.status == Status::Passed) continue;
            if (!m_showFailed && (test.status == Status::Failed || test.status == Status::Timeout)) continue;
            const char* status = test.status == Status::Passed ? "Passed" : test.status == Status::Failed
                ? "Failed" : test.status == Status::Timeout ? "Timeout" : "Not run";
            ImGui::PushID(i); ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
            if (ImGui::Selectable(test.name.c_str(), m_selectedTest == i,
                                  ImGuiSelectableFlags_SpanAllColumns)) m_selectedTest = i;
            ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(test.category.c_str());
            ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(status);
            ImGui::TableSetColumnIndex(3); ImGui::Text("%.3f s", test.durationSeconds);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::SeparatorText("Output");
    ImGui::Checkbox("Follow output##automated_tests", &m_autoScroll);
    if (ImGui::BeginChild("automated_test_output", ImVec2(0.0f, 220.0f), true,
                          ImGuiWindowFlags_HorizontalScrollbar)) {
        ImGui::TextUnformatted(m_output.empty() ? "No test output yet." : m_output.c_str());
        if (m_running && m_autoScroll) ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
    ImGui::End();
}
