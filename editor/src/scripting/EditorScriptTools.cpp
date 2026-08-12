#include "EditorScriptTools.h"

#include <algorithm>
#include <fstream>
#include <cstdint>
#include <cctype>
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

bool PackageProject(const std::filesystem::path& projectRoot,
                    const std::filesystem::path& cookedRoot,
                    const std::filesystem::path& outputRoot,
                    const std::string& projectName,
                    const std::string& configuration,
                    bool staticRuntime,
                    bool cleanOutput,
                    bool createZip,
                    std::filesystem::path* outputArtifact,
                    std::string* error) {
#if defined(_WIN32)
    const std::filesystem::path sourceDir = EngineSourceDirectory();
    if (sourceDir.empty()) {
        if (error) *error = "Engine source location is unavailable.";
        return false;
    }

    std::error_code ec;
    if (!std::filesystem::is_regular_file(cookedRoot / "CookManifest.3dgmanifest", ec)
        || !std::filesystem::is_regular_file(cookedRoot / "player.cfg", ec)
        || !std::filesystem::is_regular_file(
            cookedRoot / "Content" / "AssetRegistry.3dgdb", ec)) {
        if (error) *error = "Cooked project is incomplete: " + cookedRoot.string();
        return false;
    }

    std::string safeName = projectName;
    for (char& c : safeName) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') c = '_';
    }
    if (safeName.empty()) safeName = "Game";

    const std::filesystem::path absoluteOutput =
        std::filesystem::absolute(outputRoot, ec).lexically_normal();
    if (ec) {
        if (error) *error = "Could not resolve package output directory.";
        return false;
    }
    const std::filesystem::path buildDir =
        projectRoot / "Intermediate" / "Packaging" / "Build";
    const std::filesystem::path stageDir = absoluteOutput / safeName;
    const std::filesystem::path zipPath =
        absoluteOutput / (safeName + "-" + configuration + ".zip");
    const std::filesystem::path logPath =
        projectRoot / "Intermediate" / "Packaging" / "package.log";

    std::filesystem::create_directories(absoluteOutput, ec);
    if (ec) {
        if (error) *error = "Could not create package output directory: " + ec.message();
        return false;
    }
    if (cleanOutput) {
        // Both targets are direct children of the user-selected output root.
        // Never recurse on a computed path outside that verified parent.
        const std::filesystem::path stageParent = stageDir.parent_path().lexically_normal();
        const std::filesystem::path zipParent = zipPath.parent_path().lexically_normal();
        if (stageParent != absoluteOutput || zipParent != absoluteOutput) {
            if (error) *error = "Refusing to clean a package path outside the output directory.";
            return false;
        }
        std::filesystem::remove_all(stageDir, ec);
        if (ec) {
            if (error) *error = "Could not clean staged package: " + ec.message();
            return false;
        }
        std::filesystem::remove(zipPath, ec);
        ec.clear();
    }

    std::wstring configure = L"cmake -S " + Quote(sourceDir)
        + L" -B " + Quote(buildDir)
        + L" -DTHREEDG_ACTIVE_PROJECT_ROOT=" + Quote(projectRoot)
        + L" -DPLAYER_COOKED_DIR=" + Quote(cookedRoot)
        + L" -DPLAYER_GAME_DIR="
        + L" -DBUILD_TESTING=OFF"
        + std::wstring(staticRuntime
            ? L" -DGAMEENGINE_STATIC_RUNTIME=ON"
            : L" -DGAMEENGINE_STATIC_RUNTIME=OFF");
    DWORD exitCode = 1;
    if (!RunCommand(std::move(configure), sourceDir, logPath, false,
                    &exitCode, error) || exitCode != 0) {
        if (error && error->empty())
            *error = "Package configuration failed; see " + logPath.string();
        return false;
    }

    std::wstring build = L"cmake --build " + Quote(buildDir)
        + L" --config \"" + std::wstring(configuration.begin(), configuration.end())
        + L"\" --target player --parallel";
    if (!RunCommand(std::move(build), projectRoot, logPath, true,
                    &exitCode, error) || exitCode != 0) {
        if (error && error->empty())
            *error = "Release player build failed; see " + logPath.string();
        return false;
    }

    // The project script module is produced by the player dependency build.
    // Refresh the cooked copy before install so the package always contains the
    // same scripts that were just compiled.
    const std::filesystem::path scriptModule = ProjectScriptBinary(projectRoot);
    if (std::filesystem::is_regular_file(scriptModule, ec)) {
        std::filesystem::copy_file(scriptModule, cookedRoot / scriptModule.filename(),
            std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            if (error) *error = "Could not stage project script module: " + ec.message();
            return false;
        }
    }

    std::wstring install = L"cmake --install " + Quote(buildDir)
        + L" --config \"" + std::wstring(configuration.begin(), configuration.end())
        + L"\" --prefix " + Quote(stageDir) + L" --component player";
    if (!RunCommand(std::move(install), projectRoot, logPath, true,
                    &exitCode, error) || exitCode != 0) {
        if (error && error->empty())
            *error = "Package staging failed; see " + logPath.string();
        return false;
    }

    if (createZip) {
        std::wstring archive = L"cmake -E tar cf " + Quote(zipPath)
            + L" --format=zip .";
        if (!RunCommand(std::move(archive), stageDir, logPath, true,
                        &exitCode, error) || exitCode != 0) {
            if (error && error->empty())
                *error = "Package archive creation failed; see " + logPath.string();
            return false;
        }
    }

    if (outputArtifact) *outputArtifact = createZip ? zipPath : stageDir;
    return true;
#else
    (void)projectRoot; (void)cookedRoot; (void)outputRoot; (void)projectName;
    (void)configuration; (void)staticRuntime; (void)cleanOutput; (void)createZip;
    (void)outputArtifact;
    if (error) *error = "Project packaging is not implemented on this platform.";
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
