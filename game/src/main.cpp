#include "PongApp.h"

#include <engine/core/Config.h>

#include <cstdio>
#include <exception>

int main() {
    try {
        // Loads pong.cfg if present (else uses built-in defaults). PongApp writes
        // it back on exit so settings like fullscreen and vsync persist.
        engine::Config config("pong.cfg");
        PongApp app(config);
        app.Run();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Fatal Error: %s\n", e.what());
        return 1;
    }
    return 0;
}