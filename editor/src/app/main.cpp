#include "EditorApp.h"

#include <engine/core/HighPerformanceGPU.h>

#include <engine/core/Config.h>

int main(int argc, char** argv) {
    engine::Config config("editor.cfg");
    if (argc > 1 && argv[1] && *argv[1]) {
        config.Set("editor.current_project", argv[1]);
    }
    EditorApp app(config);
    app.Run();
    return 0;
}
