#include "EditorApp.h"

#include <engine/core/HighPerformanceGPU.h>

#include <engine/core/Config.h>

int main() {
    engine::Config config("editor.cfg");
    EditorApp app(config);
    app.Run();
    return 0;
}