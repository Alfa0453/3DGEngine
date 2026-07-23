#include <filesystem>
#include <fstream>
#include <string>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#endif

#if defined(_WIN32)
namespace {

std::wstring Quote(const std::filesystem::path& path) {
    return L"\"" + path.wstring() + L"\"";
}

bool RunBuild(const std::filesystem::path& root, const std::wstring& configuration,
              DWORD* exitCode) {
    const std::filesystem::path logPath = root / "build" / "script_compile.log";
    HANDLE log = CreateFileW(logPath.wstring().c_str(), GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log == INVALID_HANDLE_VALUE) return false;

    std::wstring command = L"cmake --build " + Quote(root / "build")
        + L" --config \"" + configuration
        + L"\" --target 3DGEditor player -- /m:1";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdOutput = log;
    startup.hStdError = log;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION process{};
    const BOOL launched = CreateProcessW(nullptr, command.data(), nullptr, nullptr,
        TRUE, CREATE_NO_WINDOW, nullptr, root.wstring().c_str(), &startup, &process);
    CloseHandle(log);
    if (!launched) return false;
    WaitForSingleObject(process.hProcess, INFINITE);
    GetExitCodeProcess(process.hProcess, exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

bool SyncEditorScripts(const std::filesystem::path& editor,
                       const std::filesystem::path& root) {
    const std::filesystem::path source = editor.parent_path() / "Content" / "Scripts";
    const std::filesystem::path destination = root / "Content" / "Scripts";
    std::error_code ec;
    if (!std::filesystem::is_directory(source, ec)) return true;
    std::filesystem::create_directories(destination, ec);
    if (ec) return false;

    for (std::filesystem::directory_iterator it(source, ec), end;
         !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec) || it->path().extension() != ".h") continue;
        std::filesystem::copy_file(it->path(), destination / it->path().filename(),
            std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) return false;
    }
    return !ec;
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
    const bool synchronized = SyncEditorScripts(editor, root);
    const bool launched = synchronized && RunBuild(root, configuration, &exitCode);
    std::ofstream status(root / "build" / "script_compile.status");
    if (launched && exitCode == 0) status << "success\n";
    else if (!synchronized) status << "failed script synchronization\n";
    else status << "failed " << (launched ? exitCode : GetLastError()) << "\n";
    status.close();

    std::wstring command = Quote(editor);
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
