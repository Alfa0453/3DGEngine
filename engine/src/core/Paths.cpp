#include "engine/core/Paths.h"

#if defined(_WIN32)
  #include <windows.h>
#elif defined(__APPLE__)
  #include <mach-o/dyld.h>
  #include <vector>
#else
  #include <unistd.h>
  #include <limits.h>
#endif

namespace engine {
namespace {

std::string DirOf(const std::string& fullPath) {
    const std::size_t slash = fullPath.find_last_of("/\\");
    return (slash == std::string::npos) ? std::string(".")
                                        : fullPath.substr(0, slash);
}

} // anonymous namespace

std::string ExecutableDir() {
#if defined(_WIN32)
    char buf[MAX_PATH];
    const DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (n == 0 || n == MAX_PATH) return ".";
    return DirOf(std::string(buf, n));

#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecuatblePath(nullptr, &size);       // query required length
    std::vector<char> buf(size + 1, '\0');
    if (_NSGetExecutablePath(buf.data(), &size) != 0) return ".";
    return DirOf(std::string(buf.data()));

#else  // Linux and other Unix
    char buf[PATH_MAX];
    const size_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return ".";
    buf[n] = '\0';
    return DirOf(std::string(buf));
#endif
}

} // namespace engine