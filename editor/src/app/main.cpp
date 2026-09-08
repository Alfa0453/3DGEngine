#include "EditorApp.h"

#include <engine/core/HighPerformanceGPU.h>

#include <engine/core/Config.h>
#include <engine/core/LaunchAuthority.h>   // launcher-authority handshake

#include <array>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <Windows.h>
#endif

namespace {

const char* g_startupPhase = "process entry";

std::filesystem::path CrashLogPath() {
    std::error_code error;
    const auto executableDirectory = std::filesystem::path(
#ifdef _WIN32
        [] {
            std::array<char, 32768> path{};
            const DWORD length = GetModuleFileNameA(nullptr, path.data(),
                                                    static_cast<DWORD>(path.size()));
            return std::string(path.data(), length);
        }()
#else
        std::filesystem::current_path(error).string()
#endif
    ).parent_path();
    return executableDirectory / "3DGEditor_crash.log";
}

void WriteCrashLog(const char* reason, const char* detail = nullptr,
                   const void* exceptionAddress = nullptr) noexcept {
    try {
        std::ofstream output(CrashLogPath(), std::ios::out | std::ios::trunc);
        output << "3DG Editor startup failure\n"
               << "Phase: " << (g_startupPhase ? g_startupPhase : "unknown") << '\n'
               << "Reason: " << (reason ? reason : "unknown") << '\n';
        if (detail && *detail) output << "Detail: " << detail << '\n';
#ifdef _WIN32
        const auto moduleBase = reinterpret_cast<std::uintptr_t>(
            GetModuleHandleW(nullptr));
        if (exceptionAddress) {
            output << "Exception RVA: 0x" << std::hex
                   << (reinterpret_cast<std::uintptr_t>(exceptionAddress) - moduleBase)
                   << std::dec << '\n';
        }
        std::array<void*, 64> frames{};
        const USHORT count = CaptureStackBackTrace(
            0, static_cast<DWORD>(frames.size()), frames.data(), nullptr);
        output << "Stack RVAs:";
        for (USHORT i = 0; i < count; ++i) {
            const auto address = reinterpret_cast<std::uintptr_t>(frames[i]);
            output << " 0x" << std::hex
                   << (address >= moduleBase ? address - moduleBase : address);
        }
        output << std::dec << '\n';
#endif
    } catch (...) {
        // A crash reporter must never cause a second exception.
    }
}

void InstallCrashLogging() {
    std::set_terminate([] {
        std::string detail;
        if (const auto exception = std::current_exception()) {
            try {
                std::rethrow_exception(exception);
            } catch (const std::exception& error) {
                detail = error.what();
            } catch (...) {
                detail = "non-standard C++ exception";
            }
        }
        WriteCrashLog("std::terminate", detail.c_str());
        std::_Exit(EXIT_FAILURE);
    });
    std::signal(SIGABRT, [](int) {
        WriteCrashLog("SIGABRT/assertion failure");
        std::_Exit(EXIT_FAILURE);
    });
#ifdef _WIN32
    SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* info) -> LONG {
        const void* address = info && info->ExceptionRecord
            ? info->ExceptionRecord->ExceptionAddress : nullptr;
        WriteCrashLog("unhandled structured exception", nullptr, address);
        return EXCEPTION_EXECUTE_HANDLER;
    });
#endif
}

std::filesystem::path ExecutableDirectory() {
#ifdef _WIN32
    std::array<char, 32768> path{};
    const DWORD length = GetModuleFileNameA(nullptr, path.data(), static_cast<DWORD>(path.size()));
    return std::filesystem::path(std::string(path.data(), length)).parent_path();
#else
    std::error_code ec;
    return std::filesystem::current_path(ec);
#endif
}

// The editor is not a standalone entry point: when it is started without a valid launcher token it
// re-opens the 3DG Launcher (so it "can only be opened again through the created project or the
// launcher") and exits. Best-effort spawn; failure still results in the editor refusing to run.
void RelaunchLauncher() {
    // The launcher is the dist root; the editor may sit beside it (dev/flat) or in an 'engine' subdir
    // (installed / per-project), in which case the launcher is one directory up. Try both.
    const std::filesystem::path editorDir = ExecutableDirectory();
    std::error_code ec;
    std::filesystem::path launcher = editorDir / "3DGLauncher.exe";
    if (!std::filesystem::is_regular_file(launcher, ec))
        launcher = editorDir.parent_path() / "3DGLauncher.exe";
    if (!std::filesystem::is_regular_file(launcher, ec)) return;
#ifdef _WIN32
    std::wstring cmd = L"\"" + launcher.wstring() + L"\"";
    std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
    mutableCmd.push_back(L'\0');
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const std::wstring dir = ExecutableDirectory().wstring();
    if (CreateProcessW(launcher.wstring().c_str(), mutableCmd.data(), nullptr, nullptr, FALSE, 0,
                       nullptr, dir.c_str(), &startup, &process)) {
        CloseHandle(process.hThread); CloseHandle(process.hProcess);
    }
#else
    const std::string cmd = "\"" + launcher.string() + "\" &";
    (void)std::system(cmd.c_str());
#endif
}

} // namespace

int main(int argc, char** argv) {
    InstallCrashLogging();
    try {
        // ---- Launcher authority: the editor only opens with a valid, fresh launcher token bound to
        // the project being opened. Otherwise it routes back through the launcher and refuses to run.
        g_startupPhase = "validating launcher authority";
        std::string launchToken, projectArg;
        const std::size_t flagLen = std::strlen(engine::kLaunchTokenFlag);
        for (int i = 1; i < argc; ++i) {
            const std::string a = (argv[i] && *argv[i]) ? argv[i] : "";
            if (a.rfind(engine::kLaunchTokenFlag, 0) == 0) launchToken = a.substr(flagLen);
            else if (!a.empty() && projectArg.empty()) projectArg = a;
        }
        const std::uint64_t nowMs = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        std::string authError;
        constexpr std::uint64_t kTokenMaxAgeMs = 30000;   // token must be used within 30s of minting
        if (!engine::ValidateLaunchToken(launchToken, nowMs, kTokenMaxAgeMs, projectArg, &authError)) {
            WriteCrashLog("editor launched without launcher authority", authError.c_str());
            RelaunchLauncher();
            return EXIT_FAILURE;
        }

        g_startupPhase = "loading editor configuration";
        engine::Config config("editor.cfg");
        if (!projectArg.empty()) {
            config.Set("editor.current_project", projectArg);
        }
        g_startupPhase = "constructing EditorApp";
        EditorApp app(config);
        g_startupPhase = "initializing/running EditorApp";
        app.Run();
        g_startupPhase = "normal shutdown";
        return 0;
    } catch (const std::exception& error) {
        WriteCrashLog("uncaught startup exception", error.what());
    } catch (...) {
        WriteCrashLog("uncaught non-standard startup exception");
    }
    return EXIT_FAILURE;
}
