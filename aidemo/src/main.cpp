#include "AiDemoApp.h"
#include <engine/core/HighPerformanceGPU.h>
#include <engine/core/Config.h>
#include <cstdio>
#include <exception>
int main() {
    try {
        engine::Config config("aidemo.cfg");
        AiDemoApp app(config);
        app.Run();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Fatal error: %s\n", e.what());
        return 1;
    }
    return 0;
}
