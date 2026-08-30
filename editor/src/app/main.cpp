#include "EditorApp.h"

#include <engine/core/HighPerformanceGPU.h>

#include <engine/core/Config.h>

#include <array>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>

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

} // namespace

int main(int argc, char** argv) {
    InstallCrashLogging();
    try {
        g_startupPhase = "loading editor configuration";
        engine::Config config("editor.cfg");
        if (argc > 1 && argv[1] && *argv[1]) {
            config.Set("editor.current_project", argv[1]);
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
