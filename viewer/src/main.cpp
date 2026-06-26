// Model-viewer entry point. A third target on the engine: it loads a model and
// orbits a camera around it, proving the OBJ loader end-to-end on the GPU.
// Independent of both games.
#include "ViewerApp.h"

#include <engine/core/Config.h>

#include <cstdio>
#include <exception>

int main() {
    try {
        engine::Config config("viewer.cfg");
        ViewerApp app(config);
        app.Run();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Fatal error: %s\n", e.what());
        return 1;
    }
    return 0;
}