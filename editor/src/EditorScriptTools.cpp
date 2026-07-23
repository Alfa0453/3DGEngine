#include "EditorScriptTools.h"

#include <fstream>
#include <cstdint>
#include <sstream>

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
#endif

} // namespace

namespace EditorScriptTools {

bool OpenExternalEditor(PreferredCodeEditor editor,
                        const std::string& customExecutable,
                        const std::filesystem::path& scriptPath,
                        const std::filesystem::path& projectRoot,
                        std::string* error) {
#if defined(_WIN32)
    std::wstring executable;
    std::wstring arguments;
    switch (editor) {
    case PreferredCodeEditor::VisualStudioCode:
        executable = L"code.cmd";
        arguments = L"-g " + Quote(scriptPath);
        break;
    case PreferredCodeEditor::VisualStudio:
        executable = L"devenv";
        arguments = L"/Edit " + Quote(scriptPath);
        break;
    case PreferredCodeEditor::Rider:
        executable = L"rider64.exe";
        arguments = Quote(scriptPath);
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

std::string ReadLastBuildLog(const std::filesystem::path& projectRoot) {
    return ReadFile(projectRoot / "build" / "script_compile.log");
}

std::string ReadLastBuildStatus(const std::filesystem::path& projectRoot) {
    return ReadFile(projectRoot / "build" / "script_compile.status");
}

} // namespace EditorScriptTools
