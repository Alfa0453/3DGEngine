// Breakout entry point. Built on the engine + its ECS; entirely separate from
// the Pong game (game/). Loads config, creates the app, runs it.
#include "BreakoutApp.h"

#include <engine/core/Config.h>

#include <cstdio>
#include <exception>

int main() {
    try {
        engine::Config config("breakout.cfg");
        BreakoutApp app(config);
        app.Run();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Fatal error: %s\n", e.what());
        return 1;
    }
    return 0;
}