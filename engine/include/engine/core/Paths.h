#pragma once

#include <string>

namespace engine {

// Absolute path to the directory containing the running executable, with no
// trailing slash. Use it to locate assets next to the binary so a packaged
// build runs regardless of the working directory:
//
//     std::string shader = engine::ExecutableDir() + "/assets/shaders/x.vert";
//
// Cross-platform (Windows / Linux / macOS). Returns "." if it cannot be found.
std::string ExecutableDir();

} // namespace engine