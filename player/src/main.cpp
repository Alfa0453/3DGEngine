#include "RuntimePlayerApp.h"

#include <engine/core/Config.h>
#include <engine/core/Paths.h>
#include <engine/core/HighPerformanceGPU.h>

#include <cstdio>
#include <exception>
#include <filesystem>
#include <string>

// Standalone runtime player. Boots an editor-exported scene and runs it with no
// editor. Scene selection order:
//   1) command-line argument:   player <scene.3dgscene>
//   2) player.cfg key "player.scene" (relative paths are resolved next to the exe)
//
// Config + relative scene paths resolve against the executable directory, so a
// packaged build (player.exe + game/ beside it) runs from any working directory.
int main(int argc, char** argv) {
    try {
        const std::filesystem::path exeDir = engine::ExecutableDir();

        engine::Config config((exeDir / "player.cfg").string());

        std::string scenePath = (argc > 1) ? argv[1] : config.GetString("player.scene", "");
        if (!scenePath.empty()) {
            const std::filesystem::path p(scenePath);
            if (p.is_relative()) scenePath = (exeDir / p).string();
        }

        RuntimePlayerApp app(config, scenePath);
        app.Run();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Fatal error: %s\n", e.what());
        return 1;
    }
    return 0;
}
