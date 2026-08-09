#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#endif

#if defined(_WIN32)
namespace {

std::wstring Quote(const std::filesystem::path& path) {
    return L"\"" + path.wstring() + L"\"";
}

bool RunProcess(std::wstring command, const std::filesystem::path& workingDirectory,
                HANDLE log, DWORD* exitCode) {
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdOutput = log;
    startup.hStdError = log;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION process{};
    const BOOL launched = CreateProcessW(nullptr, command.data(), nullptr, nullptr,
        TRUE, CREATE_NO_WINDOW, nullptr, workingDirectory.wstring().c_str(),
        &startup, &process);
    if (!launched) return false;
    WaitForSingleObject(process.hProcess, INFINITE);
    GetExitCodeProcess(process.hProcess, exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

bool RunBuild(const std::filesystem::path& projectRoot,
              const std::wstring& configuration,
              DWORD* exitCode) {
    const std::filesystem::path sourceRoot(THREEDG_ENGINE_SOURCE_DIR);
    const std::filesystem::path buildRoot(THREEDG_ENGINE_BUILD_DIR);
    const std::filesystem::path intermediate =
        projectRoot / "Intermediate" / "Scripts";
    std::error_code ec;
    std::filesystem::create_directories(intermediate, ec);
    const std::filesystem::path logPath = intermediate / "script_compile.log";
    HANDLE log = CreateFileW(logPath.wstring().c_str(), GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log == INVALID_HANDLE_VALUE) return false;

    std::wstring configure = L"cmake -S " + Quote(sourceRoot)
        + L" -B " + Quote(buildRoot)
        + L" -DTHREEDG_ACTIVE_PROJECT_ROOT=" + Quote(projectRoot);
    DWORD configureExit = 1;
    if (!RunProcess(std::move(configure), sourceRoot, log, &configureExit)
        || configureExit != 0) {
        CloseHandle(log);
        *exitCode = configureExit;
        return false;
    }

    // Project scripts compile into <Project>/Binaries/game_scripts.dll; rebuilding the
    // editor itself is no longer necessary for a gameplay-script change.
    std::wstring command = L"cmake --build " + Quote(buildRoot)
        + L" --config \"" + configuration
        + L"\" --target game_scripts -- /m";
    const bool launched = RunProcess(std::move(command), projectRoot, log, exitCode);
    CloseHandle(log);
    return launched;
}

} // namespace
#endif

int main(int argc, char** argv) {
#if defined(_WIN32)
    if (argc < 5) return 2;
    const DWORD parentId = static_cast<DWORD>(std::stoul(argv[1]));
    const std::filesystem::path root = argv[2];
    const std::filesystem::path editor = argv[3];
    const std::string configurationUtf8 = argv[4];
    const std::wstring configuration(configurationUtf8.begin(), configurationUtf8.end());

    if (HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, parentId)) {
        WaitForSingleObject(parent, INFINITE);
        CloseHandle(parent);
    }

    DWORD exitCode = 1;
    const bool launched = RunBuild(root, configuration, &exitCode);
    std::filesystem::create_directories(root / "Intermediate" / "Scripts");
    std::ofstream status(root / "Intermediate" / "Scripts" / "script_compile.status");
    if (launched && exitCode == 0) status << "success\n";
    else status << "failed " << (launched ? exitCode : GetLastError()) << "\n";
    status.close();

    std::wstring command = Quote(editor) + L" " + Quote(root / "Project.3dgproject");
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (CreateProcessW(editor.wstring().c_str(), command.data(), nullptr, nullptr,
            FALSE, 0, nullptr, root.wstring().c_str(), &startup, &process)) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
    return launched && exitCode == 0 ? 0 : 1;
#else
    (void)argc; (void)argv;
    return 1;
#endif
}
