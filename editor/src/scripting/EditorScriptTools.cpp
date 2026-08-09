#include "EditorScriptTools.h"

#include <algorithm>
#include <fstream>
#include <cstdint>
#include <sstream>
#include <utility>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#endif

namespace {

std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return input ? std::string((std::istreambuf_iterator<char>(input)),
                               std::istreambuf_iterator<char>())
                 : std::string();
}

#if defined(_WIN32)
std::wstring Quote(const std::filesystem::path& path) {
    return L"\"" + path.wstring() + L"\"";
}

bool RunCommand(std::wstring command, const std::filesystem::path& workingDirectory,
                const std::filesystem::path& logPath, bool append,
                DWORD* exitCode, std::string* error) {
    std::error_code ec;
    std::filesystem::create_directories(logPath.parent_path(), ec);
    HANDLE log = CreateFileW(logPath.wstring().c_str(), GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
        append ? OPEN_ALWAYS : CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log == INVALID_HANDLE_VALUE) {
        if (error) *error = "Could not open build log: " + logPath.string();
        return false;
    }
    if (append) SetFilePointer(log, 0, nullptr, FILE_END);
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
    CloseHandle(log);
    if (!launched) {
        if (error) *error = "Could not start the build process.";
        return false;
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (exitCode) *exitCode = code;
    return true;
}
#endif

} // namespace

namespace EditorScriptTools {

bool OpenExternalEditor(PreferredCodeEditor editor,
                        const std::string& customExecutable,
                        const std::filesystem::path& scriptPath,
                        const std::filesystem::path& projectRoot,
                        std::string* error, int line, int column) {
#if defined(_WIN32)
    std::wstring executable;
    std::wstring arguments;
    switch (editor) {
    case PreferredCodeEditor::VisualStudioCode:
        executable = L"code.cmd";
        if (line > 0) {
            std::filesystem::path target = scriptPath.wstring() + L":"
                + std::to_wstring(line) + L":" + std::to_wstring(std::max(column, 1));
            arguments = L"-g " + Quote(target);
        } else {
            arguments = L"-g " + Quote(scriptPath);
        }
        break;
    case PreferredCodeEditor::VisualStudio:
        executable = L"devenv";
        arguments = L"/Edit " + Quote(scriptPath);
        if (line > 0) {
            arguments += L" /Command \"Edit.Goto " + std::to_wstring(line) + L"\"";
        }
        break;
    case PreferredCodeEditor::Rider:
        executable = L"rider64.exe";
        arguments = line > 0
            ? L"--line " + std::to_wstring(line) + L" " + Quote(scriptPath)
            : Quote(scriptPath);
        break;
    case PreferredCodeEditor::Custom:
        executable.assign(customExecutable.begin(), customExecutable.end());
        arguments = Quote(scriptPath);
        break;
    case PreferredCodeEditor::BuiltIn:
        if (error) *error = "Built-in editor does not require an external launch.";
        return false;
    }
    if (executable.empty()) {
        if (error) *error = "Choose an editor executable first.";
        return false;
    }

    const HINSTANCE result = ShellExecuteW(nullptr, L"open", executable.c_str(),
        arguments.c_str(), projectRoot.wstring().c_str(), SW_SHOWNORMAL);
    if (reinterpret_cast<std::intptr_t>(result) <= 32) {
        if (error) {
            *error = "Could not launch the selected editor. Install it, add it to PATH, or choose Custom and enter its executable path.";
        }
        return false;
    }
    return true;
#else
    (void)editor; (void)customExecutable; (void)scriptPath; (void)projectRoot;
    (void)line; (void)column;
    if (error) *error = "External editor launching is not implemented on this platform.";
    return false;
#endif
}

bool LaunchCompileAndRestart(const std::filesystem::path& projectRoot,
                             const std::string& configuration,
                             std::string* error) {
#if defined(_WIN32)
    std::wstring editorPath(32768, L'\0');
    const DWORD editorLength = GetModuleFileNameW(nullptr, editorPath.data(),
                                                   static_cast<DWORD>(editorPath.size()));
    if (editorLength == 0 || static_cast<std::size_t>(editorLength) >= editorPath.size()) {
        if (error) *error = "Could not determine the editor executable path.";
        return false;
    }
    editorPath.resize(editorLength);
    const std::filesystem::path helperPath =
        std::filesystem::path(editorPath).parent_path() / "3DGScriptCompiler.exe";
    if (!std::filesystem::exists(helperPath)) {
        if (error) *error = "Script compiler helper is missing: " + helperPath.string();
        return false;
    }

    std::wostringstream command;
    command << Quote(helperPath) << L' ' << GetCurrentProcessId() << L' '
            << Quote(projectRoot) << L' ' << Quote(editorPath) << L" \""
            << std::wstring(configuration.begin(), configuration.end()) << L"\"";
    std::wstring mutableCommand = command.str();
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const BOOL launched = CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr,
        FALSE, CREATE_NO_WINDOW | DETACHED_PROCESS, nullptr,
        projectRoot.wstring().c_str(), &startup, &process);
    if (!launched) {
        if (error) *error = "Could not start the script compiler helper.";
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
#else
    (void)projectRoot; (void)configuration;
    if (error) *error = "Compile-and-restart is not implemented on this platform.";
    return false;
#endif
}

bool BuildTarget(const std::filesystem::path& projectRoot,
                 const std::string& configuration,
                 const std::string& target,
                 std::string* error) {
#if defined(_WIN32)
    const std::filesystem::path sourceDir = EngineSourceDirectory();
    const std::filesystem::path buildDir = EngineBuildDirectory();
    const std::filesystem::path logPath = projectRoot / "Intermediate" / "Scripts"
        / (target == "game_scripts" ? "script_compile.log" : "target_build.log");
    if (sourceDir.empty() || buildDir.empty()) {
        if (error) *error = "Engine source/build location is unavailable.";
        return false;
    }

    std::wstring configure = L"cmake -S " + Quote(sourceDir) + L" -B " + Quote(buildDir)
        + L" -DTHREEDG_ACTIVE_PROJECT_ROOT=" + Quote(projectRoot);
    DWORD configureExit = 1;
    if (!RunCommand(std::move(configure), sourceDir, logPath, false,
                    &configureExit, error) || configureExit != 0) {
        if (error && error->empty()) {
            *error = "CMake configuration failed; see " + logPath.string();
        }
        return false;
    }

    std::wstring command = L"cmake --build \"" + buildDir.wstring()
        + L"\" --config \"" + std::wstring(configuration.begin(), configuration.end())
        + L"\" --target " + std::wstring(target.begin(), target.end()) + L" -- /m";
    DWORD exitCode = 1;
    if (!RunCommand(std::move(command), projectRoot, logPath, true,
                    &exitCode, error)) return false;
    if (exitCode != 0) {
        if (error) *error = "cmake build of '" + target + "' failed (exit "
            + std::to_string(exitCode) + "); see " + logPath.string();
        return false;
    }
    return true;
#else
    (void)projectRoot; (void)configuration; (void)target;
    if (error) *error = "Building targets is not implemented on this platform.";
    return false;
#endif
}

std::filesystem::path ExecutableDirectory() {
#if defined(_WIN32)
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
                                            static_cast<DWORD>(buffer.size()));
    if (length == 0 || static_cast<std::size_t>(length) >= buffer.size()) {
        return {};
    }
    buffer.resize(length);
    return std::filesystem::path(buffer).parent_path();
#else
    return {};
#endif
}

std::filesystem::path EngineSourceDirectory() {
#ifdef THREEDG_ENGINE_SOURCE_DIR
    return std::filesystem::path(THREEDG_ENGINE_SOURCE_DIR);
#else
    return {};
#endif
}

std::filesystem::path EngineBuildDirectory() {
#ifdef THREEDG_ENGINE_BUILD_DIR
    return std::filesystem::path(THREEDG_ENGINE_BUILD_DIR);
#else
    return {};
#endif
}

std::filesystem::path ProjectScriptBinary(const std::filesystem::path& projectRoot) {
#if defined(_WIN32)
    return projectRoot / "Binaries" / "game_scripts.dll";
#else
    return projectRoot / "Binaries" / "libgame_scripts.so";
#endif
}

std::filesystem::path ProjectScriptStagingPath(
    const std::filesystem::path& projectRoot, int slot) {
    const char suffix = slot == 1 ? 'b' : 'a';
#if defined(_WIN32)
    return projectRoot / "Intermediate" / "Scripts" /
        (std::string("game_scripts_loaded_") + suffix + ".dll");
#else
    return projectRoot / "Intermediate" / "Scripts" /
        (std::string("libgame_scripts_loaded_") + suffix + ".so");
#endif
}

std::string ReadLastBuildLog(const std::filesystem::path& projectRoot) {
    return ReadFile(projectRoot / "Intermediate" / "Scripts" / "script_compile.log");
}

std::string ReadLastBuildStatus(const std::filesystem::path& projectRoot) {
    return ReadFile(projectRoot / "Intermediate" / "Scripts" / "script_compile.status");
}

} // namespace EditorScriptTools
