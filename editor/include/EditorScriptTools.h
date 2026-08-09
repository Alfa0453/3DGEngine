#pragma once

#include <filesystem>
#include <string>

enum class PreferredCodeEditor {
    BuiltIn = 0,
    VisualStudioCode,
    VisualStudio,
    Rider,
    Custom
};

namespace EditorScriptTools {

bool OpenExternalEditor(PreferredCodeEditor editor,
                        const std::string& customExecutable,
                        const std::filesystem::path& scriptPath,
                        const std::filesystem::path& projectRoot,
                        std::string* error = nullptr,
                        int line = 0,
                        int column = 0);

bool LaunchCompileAndRestart(const std::filesystem::path& projectRoot,
                             const std::string& configuration,
                             std::string* error = nullptr);

// Synchronously builds a single CMake target (e.g. "player") and waits for it to finish.
// Output goes to <projectRoot>/build/target_build.log. Used at cook time so the packaged
// player has current scripts even though the iterate loop only rebuilds the editor.
bool BuildTarget(const std::filesystem::path& projectRoot,
                 const std::string& configuration,
                 const std::string& target,
                 std::string* error = nullptr);

// Directory of the running editor executable (where game_scripts.dll is emitted).
std::filesystem::path ExecutableDirectory();
std::filesystem::path EngineSourceDirectory();
std::filesystem::path EngineBuildDirectory();
std::filesystem::path ProjectScriptBinary(const std::filesystem::path& projectRoot);
// Two alternating staging files let a newly built DLL be loaded and validated while the
// previous module remains resident for rollback.
std::filesystem::path ProjectScriptStagingPath(const std::filesystem::path& projectRoot,
                                               int slot = 0);

std::string ReadLastBuildLog(const std::filesystem::path& projectRoot);
std::string ReadLastBuildStatus(const std::filesystem::path& projectRoot);

} // namespace EditorScriptTools
